// SPDX-License-Identifier: BSD-3-Clause
/*
 * fp-hybris-test: drive Samsung's Android fingerprint HAL
 * (fingerprint.default.so -> libbauthserver.so -> the QSEE app over the
 * open libQSEEComAPI, with the egis-et5xx-ctrl kernel driver) on Linux
 * through libhybris. Uses the Android fingerprint HAL 2.x device API.
 *
 *   fp-hybris-test            open the HAL, print its auth id, enumerate
 *   fp-hybris-test enroll     start enrolment (touch the Home key repeatedly)
 *   fp-hybris-test auth       start authentication (touch the key)
 *
 * Needs: HYBRIS_LD_LIBRARY_PATH pointing at the Android lib tree
 * (…/system/lib64:…/vendor/lib64:…/vendor/lib64/hw), /dev/esfp0,
 * /dev/qseecom, /data/vendor/biometrics (writable).
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* hardware/hardware.h (Android 9), LP64 layout */
struct hw_module_t;
struct hw_device_t;
typedef struct hw_module_methods_t {
	int (*open)(const struct hw_module_t *module, const char *id, struct hw_device_t **device);
} hw_module_methods_t;
typedef struct hw_module_t {
	uint32_t tag; uint16_t module_api_version; uint16_t hal_api_version;
	const char *id, *name, *author;
	hw_module_methods_t *methods; void *dso; uint64_t reserved[32 - 7];
} hw_module_t;
typedef struct hw_device_t {
	uint32_t tag; uint32_t version; hw_module_t *module; uint64_t reserved[12];
	int (*close)(struct hw_device_t *device);
} hw_device_t;

/* hardware/fingerprint.h (HAL 2.0/2.1) */
typedef enum { FINGERPRINT_ERROR = -1, FINGERPRINT_ACQUIRED = 1, FINGERPRINT_TEMPLATE_ENROLLING = 3,
	       FINGERPRINT_TEMPLATE_REMOVED = 4, FINGERPRINT_AUTHENTICATED = 5, FINGERPRINT_TEMPLATE_ENUMERATING = 6 } fingerprint_msg_type_t;
typedef struct { uint32_t gid; uint32_t fid; } fingerprint_finger_id_t;
typedef struct { fingerprint_finger_id_t finger; uint32_t samples_remaining; uint64_t msg; } fingerprint_enroll_t;
typedef struct { fingerprint_finger_id_t finger; uint32_t remaining_templates; } fingerprint_iterator_t;
typedef struct { int32_t acquired_info; } fingerprint_acquired_t;
typedef struct { fingerprint_finger_id_t finger; uint8_t hat[69]; } fingerprint_authenticated_t;
typedef struct {
	fingerprint_msg_type_t type;
	union { int32_t error; fingerprint_enroll_t enroll; fingerprint_iterator_t removed;
		fingerprint_acquired_t acquired; fingerprint_authenticated_t authenticated; fingerprint_iterator_t enumerated; } data;
} fingerprint_msg_t;
typedef void (*fingerprint_notify_t)(const fingerprint_msg_t *msg);
typedef struct fingerprint_device {
	hw_device_t common;
	fingerprint_notify_t notify;
	uint64_t (*pre_enroll)(struct fingerprint_device *dev);
	int (*enroll)(struct fingerprint_device *dev, const uint8_t *hat, uint32_t gid, uint32_t timeout_sec);
	int (*post_enroll)(struct fingerprint_device *dev);
	uint64_t (*get_authenticator_id)(struct fingerprint_device *dev);
	int (*cancel)(struct fingerprint_device *dev);
	int (*enumerate)(struct fingerprint_device *dev);
	int (*remove)(struct fingerprint_device *dev, uint32_t gid, uint32_t fid);
	int (*set_active_group)(struct fingerprint_device *dev, uint32_t gid, const char *store_path);
	int (*authenticate)(struct fingerprint_device *dev, uint64_t operation_id, uint32_t gid);
	int (*set_notify)(struct fingerprint_device *dev, fingerprint_notify_t notify);
	void *reserved[4];
} fingerprint_device_t;

static void notify(const fingerprint_msg_t *m)
{
	switch (m->type) {
	case FINGERPRINT_ERROR: printf("[cb] error %d\n", m->data.error); break;
	case FINGERPRINT_ACQUIRED: printf("[cb] acquired %d (finger seen)\n", m->data.acquired.acquired_info); break;
	case FINGERPRINT_TEMPLATE_ENROLLING: printf("[cb] enrolling: finger %u, %u samples left\n", m->data.enroll.finger.fid, m->data.enroll.samples_remaining); break;
	case FINGERPRINT_TEMPLATE_REMOVED: printf("[cb] removed finger %u\n", m->data.removed.finger.fid); break;
	case FINGERPRINT_AUTHENTICATED: printf("[cb] authenticated: finger %u%s\n", m->data.authenticated.finger.fid, m->data.authenticated.finger.fid ? "" : " (no match)"); break;
	case FINGERPRINT_TEMPLATE_ENUMERATING: printf("[cb] enumerated finger %u, %u more\n", m->data.enumerated.finger.fid, m->data.enumerated.remaining_templates); break;
	default: printf("[cb] msg type %d\n", m->type);
	}
	fflush(stdout);
}

int main(int argc, char **argv)
{
	void *h;
	hw_module_t *mod;
	hw_device_t *dev;
	fingerprint_device_t *fp;
	int ret;

	h = dlopen("libhybris-common.so.1", RTLD_NOW);
	if (!h)
		h = dlopen("libhybris-common.so", RTLD_NOW);
	if (!h) { fprintf(stderr, "libhybris-common: %s\n", dlerror()); return 1; }
	void *(*android_dlopen)(const char *, int) = dlsym(h, "android_dlopen");
	void *(*android_dlsym)(void *, const char *) = dlsym(h, "android_dlsym");
	const char *(*android_dlerror)(void) = dlsym(h, "android_dlerror");
	if (!android_dlopen || !android_dlsym) { fprintf(stderr, "no android_dlopen in hybris\n"); return 1; }

	void *ah = android_dlopen("fingerprint.default.so", 2 /* RTLD_NOW */);
	if (!ah) { fprintf(stderr, "android_dlopen(fingerprint.default.so): %s\n", android_dlerror ? android_dlerror() : "?"); return 1; }
	mod = android_dlsym(ah, "HMI");
	if (!mod) { fprintf(stderr, "no HMI symbol\n"); return 1; }
	printf("HAL module: id=%s name=%s author=%s api=%#x\n", mod->id, mod->name, mod->author, mod->module_api_version);
	mod->dso = ah;
	ret = mod->methods->open(mod, "fingerprint", &dev);
	printf("open -> %d\n", ret);
	if (ret) return 1;
	fp = (fingerprint_device_t *)dev;
	printf("device version %#x\n", dev->version);
	fp->set_notify(fp, notify);
	ret = fp->set_active_group(fp, 0, "/data/vendor/biometrics/fp/User_0");
	printf("set_active_group -> %d\n", ret);
	printf("authenticator id: %llu\n", (unsigned long long)fp->get_authenticator_id(fp));
	ret = fp->enumerate(fp);
	printf("enumerate -> %d\n", ret);
	sleep(1);
	if (argc > 1 && !strcmp(argv[1], "enroll")) {
		uint64_t chal = fp->pre_enroll(fp);
		uint8_t hat[69] = {};
		printf("pre_enroll challenge %llu; enroll (touch the Home key repeatedly, 60 s)\n", (unsigned long long)chal);
		ret = fp->enroll(fp, hat, 0, 60);
		printf("enroll -> %d\n", ret);
		sleep(60);
		fp->post_enroll(fp);
	} else if (argc > 1 && !strcmp(argv[1], "auth")) {
		printf("authenticate: touch the Home key (30 s)\n");
		ret = fp->authenticate(fp, 1, 0);
		printf("authenticate -> %d\n", ret);
		sleep(30);
	}
	fp->cancel(fp);
	dev->close(dev);
	return 0;
}
