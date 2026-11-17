// SPDX-License-Identifier: Apache-2.0
/*
 * health-mainline: Android health HAL (AIDL android.hardware.health.IHealth,
 * version 1) for a mainline kernel's power_supply class, plus a stub of
 * Samsung's vendor.samsung.hardware.health.ISehHealth as its binder
 * extension.
 *
 * OneUI system images take IHealth/default, call getExtension() on it and
 * use ISehHealth without a null check (BatteryService then dies in a loop);
 * they also get their battery updates only through Samsung's
 * ISehHealthInfoCallback. So the extension takes Samsung callbacks and sends
 * them SehHealthInfo { aospHealthInfo, Samsung fields left at their defaults }.
 * Plain system images use IHealth and never look at the extension.
 *
 * Built with the NDK against the platform's libbinder_ndk (an LLNDK library,
 * so vendor code may use it); written in C, no libc++. It replaces a vendor's
 * health service binary in place (same init service, VINTF entry and label).
 */
#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <linux/netlink.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* libbinder_ndk platform API (LLNDK), not in the NDK's headers */
binder_exception_t AServiceManager_addService(AIBinder *binder, const char *instance);
bool ABinderProcess_setThreadPoolMaxThreadCount(uint32_t threads);
void ABinderProcess_startThreadPool(void);
void ABinderProcess_joinThreadPool(void);
void AIBinder_markVintfStability(AIBinder *binder);

#define TAG "health-mainline"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#define IHEALTH "android.hardware.health.IHealth"
#define IHEALTH_CB "android.hardware.health.IHealthInfoCallback"
#define ISEH "vendor.samsung.hardware.health.ISehHealth"
#define ISEH_CB "vendor.samsung.hardware.health.ISehHealthInfoCallback"
#define IHEALTH_V1_HASH "94e77215594f8ad98ab33a769263d48fdabed92e"

/* AIDL meta transactions */
#define GET_INTERFACE_VERSION 0x00fffffe
#define GET_INTERFACE_HASH 0x00fffffd

/* IHealth v1, in declaration order from FIRST_CALL_TRANSACTION */
enum {
	H_REGISTER_CALLBACK = 1, H_UNREGISTER_CALLBACK, H_UPDATE,
	H_GET_CHARGE_COUNTER, H_GET_CURRENT_NOW, H_GET_CURRENT_AVERAGE,
	H_GET_CAPACITY, H_GET_ENERGY_COUNTER, H_GET_CHARGE_STATUS,
	H_GET_STORAGE_INFO, H_GET_DISK_STATS, H_GET_HEALTH_INFO,
};

/* android.hardware.health enums */
enum { STATUS_UNKNOWN = 1, STATUS_CHARGING, STATUS_DISCHARGING, STATUS_NOT_CHARGING, STATUS_FULL };
enum { HEALTH_UNKNOWN = 1, HEALTH_GOOD, HEALTH_OVERHEAT, HEALTH_DEAD, HEALTH_OVER_VOLTAGE,
	HEALTH_UNSPECIFIED_FAILURE, HEALTH_COLD };
#define CAPACITY_LEVEL_UNSUPPORTED (-1)

struct health_info {
	bool ac, usb, wireless, dock;
	int max_current_ua, max_voltage_uv;
	int status, health;
	bool present;
	int level, voltage_mv, temp_dc, current_ua, cycles, full_uah, counter_uah;
	char technology[32];
	int current_avg_ua;
	int full_design_uah;
};

#define MAX_CB 16
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static AIBinder *aosp_cbs[MAX_CB], *seh_cbs[MAX_CB];
static AIBinder_Class *health_class, *seh_class, *aosp_cb_class, *seh_cb_class;

/* power_supply sysfs */

static int read_str(const char *dir, const char *attr, char *buf, size_t len)
{
	char path[512];
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", dir, attr);
	f = fopen(path, "re");
	if (!f)
		return -1;
	if (!fgets(buf, (int)len, f)) {
		fclose(f);
		return -1;
	}
	fclose(f);
	buf[strcspn(buf, "\n")] = 0;
	return 0;
}

static int read_int(const char *dir, const char *attr, int def)
{
	char buf[32];

	return read_str(dir, attr, buf, sizeof(buf)) ? def : atoi(buf);
}

static int status_of(const char *s)
{
	if (!strcmp(s, "Charging"))
		return STATUS_CHARGING;
	if (!strcmp(s, "Discharging"))
		return STATUS_DISCHARGING;
	if (!strcmp(s, "Not charging"))
		return STATUS_NOT_CHARGING;
	if (!strcmp(s, "Full"))
		return STATUS_FULL;
	return STATUS_UNKNOWN;
}

static int health_of(const char *s)
{
	if (!strcmp(s, "Good"))
		return HEALTH_GOOD;
	if (!strcmp(s, "Overheat"))
		return HEALTH_OVERHEAT;
	if (!strcmp(s, "Dead"))
		return HEALTH_DEAD;
	if (!strcmp(s, "Over voltage"))
		return HEALTH_OVER_VOLTAGE;
	if (!strcmp(s, "Cold"))
		return HEALTH_COLD;
	if (!strcmp(s, "Unspecified failure"))
		return HEALTH_UNSPECIFIED_FAILURE;
	return HEALTH_UNKNOWN;
}

/* usb_type lists the possible types with the active one in brackets */
static bool usb_is_ac(const char *dir)
{
	char buf[128], *b, *e;

	if (read_str(dir, "usb_type", buf, sizeof(buf)) || !(b = strchr(buf, '[')) ||
	    !(e = strchr(b, ']')))
		return false;
	*e = 0;
	b++;
	return !strcmp(b, "DCP") || !strcmp(b, "CDP") || !strcmp(b, "ACA") || !strcmp(b, "C") ||
	       !strncmp(b, "PD", 2) || !strcmp(b, "BrickID");
}

static void read_health(struct health_info *h)
{
	char dir[300], buf[64];
	struct dirent *de;
	DIR *d;
	bool have_health = false;

	memset(h, 0, sizeof(*h));
	h->status = STATUS_UNKNOWN;
	h->health = HEALTH_UNKNOWN;
	h->level = 50;
	strcpy(h->technology, "Li-ion");

	d = opendir("/sys/class/power_supply");
	if (!d)
		return;
	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		snprintf(dir, sizeof(dir), "/sys/class/power_supply/%s", de->d_name);
		if (read_str(dir, "type", buf, sizeof(buf)))
			continue;
		if (!strcmp(buf, "Battery")) {
			h->present = read_int(dir, "present", 1);
			h->level = read_int(dir, "capacity", h->level);
			h->voltage_mv = read_int(dir, "voltage_now", 0) / 1000;
			h->temp_dc = read_int(dir, "temp", 250);
			h->current_ua = read_int(dir, "current_now", 0);
			h->current_avg_ua = read_int(dir, "current_avg", h->current_ua);
			h->cycles = read_int(dir, "cycle_count", 0);
			h->full_uah = read_int(dir, "charge_full", 0);
			h->full_design_uah = read_int(dir, "charge_full_design", 0);
			if (!h->full_uah)
				h->full_uah = h->full_design_uah;
			h->counter_uah = read_int(dir, "charge_counter",
						  (int)((long long)h->full_uah * h->level / 100));
			if (!read_str(dir, "status", buf, sizeof(buf)))
				h->status = status_of(buf);
			if (!read_str(dir, "health", buf, sizeof(buf))) {
				h->health = health_of(buf);
				have_health = true;
			}
			if (!read_str(dir, "technology", buf, sizeof(buf)))
				snprintf(h->technology, sizeof(h->technology), "%s", buf);
			continue;
		}
		if (!read_int(dir, "online", 0))
			continue;
		if (!strcmp(buf, "Mains"))
			h->ac = true;
		else if (!strcmp(buf, "Wireless"))
			h->wireless = true;
		else if (!strncmp(buf, "USB", 3))
			*(usb_is_ac(dir) ? &h->ac : &h->usb) = true;
		{
			int ua = read_int(dir, "current_max", 0);
			int uv = read_int(dir, "voltage_max", 0);

			if (!ua)
				ua = read_int(dir, "input_current_limit", 0);
			if (!uv)
				uv = 5000000;
			if (ua > h->max_current_ua) {
				h->max_current_ua = ua;
				h->max_voltage_uv = uv;
			}
		}
		if (!have_health && !read_str(dir, "health", buf, sizeof(buf)))
			h->health = health_of(buf);
	}
	closedir(d);
	if (h->health == HEALTH_UNKNOWN && h->present)
		h->health = HEALTH_GOOD;
}

static long now_s(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec;
}

/*
 * What the charger does at full charge is not news: it tops the battery up
 * (Full, Charging, Not charging in turns, a moment of "Over voltage" health
 * now and then), and OneUI plays its charger sound at each change. Report
 * full while a charger is connected at 100 %, and a bad health only once it
 * has lasted HEALTH_HOLD seconds.
 */
#define HEALTH_HOLD 10
static void smooth_health(struct health_info *h)
{
	static long bad_since;

	if ((h->ac || h->usb || h->wireless || h->dock) && h->level >= 100 &&
	    (h->status == STATUS_CHARGING || h->status == STATUS_NOT_CHARGING))
		h->status = STATUS_FULL;
	if (h->health != HEALTH_GOOD && h->health != HEALTH_UNKNOWN) {
		if (!bad_since)
			bad_since = now_s();
		if (now_s() - bad_since < HEALTH_HOLD)
			h->health = HEALTH_GOOD;
	} else {
		bad_since = 0;
	}
}

/* what a user sees: the rest (voltage, current, temperature) changes all the time */
static bool visible_change(const struct health_info *a, const struct health_info *b)
{
	return a->ac != b->ac || a->usb != b->usb || a->wireless != b->wireless ||
	       a->dock != b->dock || a->status != b->status || a->health != b->health ||
	       a->present != b->present || a->level != b->level;
}

/* parcels */

/* a stable AIDL parcelable: its size (including this field), then the fields */
static int32_t parcelable_begin(AParcel *p)
{
	int32_t start = AParcel_getDataPosition(p);

	AParcel_writeInt32(p, 0);
	return start;
}

static void parcelable_end(AParcel *p, int32_t start)
{
	int32_t end = AParcel_getDataPosition(p);

	AParcel_setDataPosition(p, start);
	AParcel_writeInt32(p, end - start);
	AParcel_setDataPosition(p, end);
}

/* HealthInfo v1 as a non-null parcelable value */
static void write_health_info(AParcel *p, const struct health_info *h)
{
	int32_t s;

	AParcel_writeInt32(p, 1);
	s = parcelable_begin(p);
	AParcel_writeBool(p, h->ac);
	AParcel_writeBool(p, h->usb);
	AParcel_writeBool(p, h->wireless);
	AParcel_writeBool(p, h->dock);
	AParcel_writeInt32(p, h->max_current_ua);
	AParcel_writeInt32(p, h->max_voltage_uv);
	AParcel_writeInt32(p, h->status);
	AParcel_writeInt32(p, h->health);
	AParcel_writeBool(p, h->present);
	AParcel_writeInt32(p, h->level);
	AParcel_writeInt32(p, h->voltage_mv);
	AParcel_writeInt32(p, h->temp_dc);
	AParcel_writeInt32(p, h->current_ua);
	AParcel_writeInt32(p, h->cycles);
	AParcel_writeInt32(p, h->full_uah);
	AParcel_writeInt32(p, h->counter_uah);
	AParcel_writeString(p, h->technology, (int32_t)strlen(h->technology));
	AParcel_writeInt32(p, h->current_avg_ua);
	AParcel_writeInt32(p, 0);	/* diskStats: empty */
	AParcel_writeInt32(p, 0);	/* storageInfos: empty */
	AParcel_writeInt32(p, CAPACITY_LEVEL_UNSUPPORTED);
	AParcel_writeInt64(p, -1);	/* batteryChargeTimeToFullNowSeconds: unsupported */
	AParcel_writeInt32(p, h->full_design_uah);
	parcelable_end(p, s);
}

/* Samsung's SehHealthInfo: aospHealthInfo first, the rest left at defaults */
static void write_seh_health_info(AParcel *p, const struct health_info *h)
{
	int32_t s;

	AParcel_writeInt32(p, 1);
	s = parcelable_begin(p);
	write_health_info(p, h);
	parcelable_end(p, s);
}

static binder_status_t reply_ok(AParcel *out)
{
	AStatus *st = AStatus_newOk();
	binder_status_t r = AParcel_writeStatusHeader(out, st);

	AStatus_delete(st);
	return r;
}

static binder_status_t reply_exception(AParcel *out, binder_exception_t e)
{
	AStatus *st = AStatus_fromExceptionCode(e);
	binder_status_t r = AParcel_writeStatusHeader(out, st);

	AStatus_delete(st);
	return r;
}

/* callbacks */

static bool cb_add(AIBinder **list, AIBinder *b)
{
	for (int i = 0; i < MAX_CB; i++)
		if (list[i] == b) {
			AIBinder_decStrong(b);
			return true;
		}
	for (int i = 0; i < MAX_CB; i++)
		if (!list[i]) {
			list[i] = b;
			return true;
		}
	AIBinder_decStrong(b);
	return false;
}

static bool cb_remove(AIBinder **list, AIBinder *b)
{
	for (int i = 0; i < MAX_CB; i++)
		if (list[i] == b) {
			AIBinder_decStrong(list[i]);
			list[i] = NULL;
			return true;
		}
	return false;
}

/* healthInfoChanged (the only method, so code 1) to one callback; false if it died */
static bool notify_one(AIBinder *cb, AIBinder_Class *cls, bool seh, const struct health_info *h)
{
	AParcel *in = NULL, *out = NULL;
	binder_status_t r;

	if (!AIBinder_associateClass(cb, cls)) {
		LOGE("callback is not a %s", seh ? ISEH_CB : IHEALTH_CB);
		return false;
	}
	if (AIBinder_prepareTransaction(cb, &in) != STATUS_OK)
		return false;
	if (seh)
		write_seh_health_info(in, h);
	else
		write_health_info(in, h);
	r = AIBinder_transact(cb, 1, &in, &out, FLAG_ONEWAY);
	AParcel_delete(out);
	return r != STATUS_DEAD_OBJECT;
}

/*
 * Callbacks get the state when it changed visibly, when a minute has gone
 * by, and on request (update(), a new callback) at most every MIN_INTERVAL
 * seconds: the charger and fuel gauge send uevents every second or so.
 */
#define PERIOD 60
#define MIN_INTERVAL 2
enum notify_why { NOTIFY_EVENT, NOTIFY_REQUEST, NOTIFY_NEW_CALLBACK };

static void notify_all(enum notify_why why)
{
	static struct health_info last;
	static long last_at = -PERIOD;
	struct health_info h;
	long now = now_s();

	read_health(&h);
	smooth_health(&h);
	pthread_mutex_lock(&lock);
	if ((why == NOTIFY_EVENT && !visible_change(&h, &last) && now - last_at < PERIOD) ||
	    (why == NOTIFY_REQUEST && !visible_change(&h, &last) && now - last_at < MIN_INTERVAL)) {
		pthread_mutex_unlock(&lock);
		return;
	}
	last = h;
	last_at = now;
	for (int i = 0; i < MAX_CB; i++) {
		if (aosp_cbs[i] && !notify_one(aosp_cbs[i], aosp_cb_class, false, &h))
			cb_remove(aosp_cbs, aosp_cbs[i]);
		if (seh_cbs[i] && !notify_one(seh_cbs[i], seh_cb_class, true, &h))
			cb_remove(seh_cbs, seh_cbs[i]);
	}
	pthread_mutex_unlock(&lock);
}

/* IHealth */

static binder_status_t health_transact(AIBinder *binder, transaction_code_t code,
				       const AParcel *in, AParcel *out)
{
	struct health_info h;
	AIBinder *cb = NULL;
	binder_status_t r;

	(void)binder;
	switch (code) {
	case GET_INTERFACE_VERSION:
		r = reply_ok(out);
		return r ?: AParcel_writeInt32(out, 1);
	case GET_INTERFACE_HASH:
		r = reply_ok(out);
		return r ?: AParcel_writeString(out, IHEALTH_V1_HASH, (int32_t)strlen(IHEALTH_V1_HASH));
	case H_REGISTER_CALLBACK:
	case H_UNREGISTER_CALLBACK:
		if (AParcel_readStrongBinder(in, &cb) != STATUS_OK || !cb)
			return reply_exception(out, EX_ILLEGAL_ARGUMENT);
		pthread_mutex_lock(&lock);
		if (code == H_REGISTER_CALLBACK)
			cb_add(aosp_cbs, cb);
		else {
			cb_remove(aosp_cbs, cb);
			AIBinder_decStrong(cb);
		}
		pthread_mutex_unlock(&lock);
		r = reply_ok(out);
		if (code == H_REGISTER_CALLBACK)
			notify_all(NOTIFY_NEW_CALLBACK);
		return r;
	case H_UPDATE:
		notify_all(NOTIFY_REQUEST);
		return reply_ok(out);
	case H_GET_ENERGY_COUNTER:
		return reply_exception(out, EX_UNSUPPORTED_OPERATION);
	case H_GET_STORAGE_INFO:
	case H_GET_DISK_STATS:
		r = reply_ok(out);
		return r ?: AParcel_writeInt32(out, 0);
	}
	if (code < H_GET_CHARGE_COUNTER || code > H_GET_HEALTH_INFO)
		return STATUS_UNKNOWN_TRANSACTION;

	read_health(&h);
	smooth_health(&h);
	r = reply_ok(out);
	if (r)
		return r;
	switch (code) {
	case H_GET_CHARGE_COUNTER:
		return AParcel_writeInt32(out, h.counter_uah);
	case H_GET_CURRENT_NOW:
		return AParcel_writeInt32(out, h.current_ua);
	case H_GET_CURRENT_AVERAGE:
		return AParcel_writeInt32(out, h.current_avg_ua);
	case H_GET_CAPACITY:
		return AParcel_writeInt32(out, h.level);
	case H_GET_CHARGE_STATUS:
		return AParcel_writeInt32(out, h.status);
	default:	/* H_GET_HEALTH_INFO */
		write_health_info(out, &h);
		return STATUS_OK;
	}
}

/*
 * ISehHealth stub. OneUI calls registerCallback, unregisterCallback, update
 * and sehWriteEnableToParam; their codes are not known here, so: a call whose
 * first argument is a binder registers it (or unregisters it if known), any
 * other call counts as update. Replies are "no exception" plus zeros, which
 * reads as false/0 for whatever a method returns.
 */
static binder_status_t seh_transact(AIBinder *binder, transaction_code_t code,
				    const AParcel *in, AParcel *out)
{
	AIBinder *cb = NULL;
	binder_status_t r;
	bool known = true;	/* no binder: an update() */

	(void)binder;
	if (code == GET_INTERFACE_VERSION) {
		r = reply_ok(out);
		return r ?: AParcel_writeInt32(out, 1);
	}
	if (code == GET_INTERFACE_HASH) {
		r = reply_ok(out);
		return r ?: AParcel_writeString(out, "notfrozen", 9);
	}
	if (AParcel_readStrongBinder(in, &cb) == STATUS_OK && cb) {
		pthread_mutex_lock(&lock);
		known = cb_remove(seh_cbs, cb);
		if (!known)
			cb_add(seh_cbs, cb);
		else
			AIBinder_decStrong(cb);
		pthread_mutex_unlock(&lock);
		LOGI("ISehHealth call %u: %s a callback", code, known ? "removed" : "added");
	} else {
		LOGI("ISehHealth call %u: update", code);
	}
	r = reply_ok(out);
	for (int i = 0; !r && i < 4; i++)
		r = AParcel_writeInt32(out, 0);
	notify_all(known ? NOTIFY_REQUEST : NOTIFY_NEW_CALLBACK);
	return r;
}

static void *on_create(void *args) { return args; }
static void on_destroy(void *userdata) { (void)userdata; }
static binder_status_t no_transact(AIBinder *b, transaction_code_t c, const AParcel *i, AParcel *o)
{
	(void)b; (void)c; (void)i; (void)o;
	return STATUS_UNKNOWN_TRANSACTION;
}

/* power_supply uevents (and once a minute anyway) push updates to callbacks */
static void uevent_loop(void)
{
	struct sockaddr_nl addr = { .nl_family = AF_NETLINK, .nl_groups = 1 };
	char buf[4096];
	struct pollfd pfd;
	int fd;

	fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
	if (fd >= 0 && bind(fd, (struct sockaddr *)&addr, sizeof(addr))) {
		close(fd);
		fd = -1;
	}
	pfd.fd = fd;
	pfd.events = POLLIN;
	for (;;) {
		bool power = false;

		if (fd < 0) {
			sleep(60);
		} else if (poll(&pfd, 1, 60000) > 0) {
			ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);

			if (n <= 0)
				continue;
			buf[n] = 0;
			for (char *s = buf; s < buf + n; s += strlen(s) + 1)
				if (!strcmp(s, "SUBSYSTEM=power_supply"))
					power = true;
			if (!power)
				continue;
		}
		notify_all(NOTIFY_EVENT);
	}
}

int main(int argc, char **argv)
{
	AIBinder *health, *seh;
	binder_exception_t e;

	if (argc > 1 && !strcmp(argv[1], "--charger"))
		return 0;	/* no charger mode UI here */

	health_class = AIBinder_Class_define(IHEALTH, on_create, on_destroy, health_transact);
	seh_class = AIBinder_Class_define(ISEH, on_create, on_destroy, seh_transact);
	aosp_cb_class = AIBinder_Class_define(IHEALTH_CB, on_create, on_destroy, no_transact);
	seh_cb_class = AIBinder_Class_define(ISEH_CB, on_create, on_destroy, no_transact);

	health = AIBinder_new(health_class, NULL);
	seh = AIBinder_new(seh_class, NULL);
	AIBinder_markVintfStability(health);
	AIBinder_markVintfStability(seh);
	if (AIBinder_setExtension(health, seh) != STATUS_OK)
		LOGE("could not set the ISehHealth extension");

	ABinderProcess_setThreadPoolMaxThreadCount(2);
	ABinderProcess_startThreadPool();
	e = AServiceManager_addService(health, IHEALTH "/default");
	if (e != EX_NONE) {
		LOGE("addService(" IHEALTH "/default): exception %d", e);
		return 1;
	}
	LOGI("IHealth/default (v1) up, with an ISehHealth extension");
	uevent_loop();
	return 0;
}
