// SPDX-License-Identifier: Apache-2.0
/*
 * lights-mainline: Android lights HAL (AIDL android.hardware.light.ILights,
 * version 2) for a mainline kernel: the backlight through
 * /sys/class/backlight, the touch key LED through /sys/class/leds, and the
 * battery, notification and attention lights as lights that do nothing (the
 * Galaxy Tab S3 has no such LED).
 *
 * The do-nothing lights are there for OneUI system images: their
 * LightsService drives a "service LED" through the notification light and
 * does not check that it exists.
 *
 * Built with the NDK against the platform's libbinder_ndk (LLNDK); replaces
 * a vendor's lights service binary in place.
 */
#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* libbinder_ndk platform API (LLNDK), not in the NDK's headers */
binder_exception_t AServiceManager_addService(AIBinder *binder, const char *instance);
bool ABinderProcess_setThreadPoolMaxThreadCount(uint32_t threads);
void ABinderProcess_joinThreadPool(void);
void AIBinder_markVintfStability(AIBinder *binder);

#define TAG "lights-mainline"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#define ILIGHTS "android.hardware.light.ILights"
#define ILIGHTS_V2_HASH "c7d3d941d303c70d1c22759a0b09e41930c1cddb"
#define GET_INTERFACE_VERSION 0x00fffffe
#define GET_INTERFACE_HASH 0x00fffffd
enum { SET_LIGHT_STATE = 1, GET_LIGHTS };

/* android.hardware.light.LightType */
enum { BACKLIGHT = 0, BUTTONS = 2, BATTERY = 3, NOTIFICATIONS = 4, ATTENTION = 5 };

struct light {
	int type;
	char path[256];		/* brightness file, empty for a light that does nothing */
	char max_path[256];
};

static struct light lights[] = {
	{ .type = BACKLIGHT }, { .type = BUTTONS }, { .type = BATTERY },
	{ .type = NOTIFICATIONS }, { .type = ATTENTION },
};
#define NLIGHTS ((int)(sizeof(lights) / sizeof(lights[0])))

static void find_sysfs(void)
{
	struct dirent *de;
	DIR *d;

	d = opendir("/sys/class/backlight");
	while (d && (de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		snprintf(lights[0].path, sizeof(lights[0].path), "/sys/class/backlight/%s/brightness", de->d_name);
		snprintf(lights[0].max_path, sizeof(lights[0].max_path), "/sys/class/backlight/%s/max_brightness", de->d_name);
		break;
	}
	if (d)
		closedir(d);
	d = opendir("/sys/class/leds");
	while (d && (de = readdir(d))) {
		if (!strstr(de->d_name, "touchkey"))
			continue;
		snprintf(lights[1].path, sizeof(lights[1].path), "/sys/class/leds/%s/brightness", de->d_name);
		snprintf(lights[1].max_path, sizeof(lights[1].max_path), "/sys/class/leds/%s/max_brightness", de->d_name);
		break;
	}
	if (d)
		closedir(d);
	LOGI("backlight: %s, buttons: %s", lights[0].path[0] ? lights[0].path : "none",
	     lights[1].path[0] ? lights[1].path : "none");
}

static void set_brightness(const struct light *l, int color)
{
	/* the framework sends brightness as a grey ARGB colour */
	int r = (color >> 16) & 0xff, g = (color >> 8) & 0xff, b = color & 0xff;
	int level = (77 * r + 150 * g + 29 * b) >> 8, max = 255;
	char buf[16];
	FILE *f;

	if (!l->path[0])
		return;
	f = fopen(l->max_path, "re");
	if (f) {
		if (fgets(buf, sizeof(buf), f))
			max = atoi(buf);
		fclose(f);
	}
	f = fopen(l->path, "we");
	if (!f)
		return;
	fprintf(f, "%d\n", level * max / 255);
	fclose(f);
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

static binder_status_t lights_transact(AIBinder *binder, transaction_code_t code,
				       const AParcel *in, AParcel *out)
{
	binder_status_t r;
	int32_t id, present, size, color = 0;

	(void)binder;
	switch (code) {
	case GET_INTERFACE_VERSION:
		r = reply_ok(out);
		return r ?: AParcel_writeInt32(out, 2);
	case GET_INTERFACE_HASH:
		r = reply_ok(out);
		return r ?: AParcel_writeString(out, ILIGHTS_V2_HASH, (int32_t)strlen(ILIGHTS_V2_HASH));
	case SET_LIGHT_STATE:
		/* id, then HwLightState: presence, size, color first */
		if (AParcel_readInt32(in, &id) || AParcel_readInt32(in, &present) || !present ||
		    AParcel_readInt32(in, &size) || AParcel_readInt32(in, &color))
			return reply_exception(out, EX_ILLEGAL_ARGUMENT);
		if (id < 0 || id >= NLIGHTS)
			return reply_exception(out, EX_UNSUPPORTED_OPERATION);
		set_brightness(&lights[id], color);
		return reply_ok(out);
	case GET_LIGHTS:
		r = reply_ok(out);
		if (r)
			return r;
		/* HwLight[]: count, then each with presence and size */
		AParcel_writeInt32(out, NLIGHTS);
		for (int i = 0; i < NLIGHTS; i++) {
			int32_t start, end;

			AParcel_writeInt32(out, 1);
			start = AParcel_getDataPosition(out);
			AParcel_writeInt32(out, 0);
			AParcel_writeInt32(out, i);		/* id */
			AParcel_writeInt32(out, 0);		/* ordinal */
			AParcel_writeByte(out, (int8_t)lights[i].type);
			end = AParcel_getDataPosition(out);
			AParcel_setDataPosition(out, start);
			AParcel_writeInt32(out, end - start);
			AParcel_setDataPosition(out, end);
		}
		return STATUS_OK;
	}
	return STATUS_UNKNOWN_TRANSACTION;
}

static void *on_create(void *args) { return args; }
static void on_destroy(void *userdata) { (void)userdata; }

int main(void)
{
	AIBinder_Class *cls;
	AIBinder *b;
	binder_exception_t e;

	find_sysfs();
	cls = AIBinder_Class_define(ILIGHTS, on_create, on_destroy, lights_transact);
	b = AIBinder_new(cls, NULL);
	AIBinder_markVintfStability(b);
	ABinderProcess_setThreadPoolMaxThreadCount(0);
	e = AServiceManager_addService(b, ILIGHTS "/default");
	if (e != EX_NONE) {
		LOGE("addService(" ILIGHTS "/default): exception %d", e);
		return 1;
	}
	LOGI("ILights/default (v2) up, %d lights", NLIGHTS);
	ABinderProcess_joinThreadPool();
	return 0;
}
