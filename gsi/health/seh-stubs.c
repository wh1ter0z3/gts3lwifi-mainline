// SPDX-License-Identifier: Apache-2.0
/*
 * seh-stubs: registers stand-ins for Samsung vendor HALs that a OneUI system
 * image waits for (ServiceManager.waitForService blocks system_server's main
 * thread until the Watchdog kills it) on a vendor that has none of them.
 *
 * Each argument is an AIDL interface descriptor, registered as
 * "<descriptor>/default" (and declared in the vendor's VINTF manifest); every
 * call gets "no exception" plus zeros, which reads as 0/false/null/empty for
 * whatever the method returns. Interface version 1, hash "notfrozen".
 *
 * Built with the NDK against the platform's libbinder_ndk (LLNDK).
 */
#include <android/binder_ibinder.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <stdbool.h>
#include <stdio.h>

binder_exception_t AServiceManager_addService(AIBinder *binder, const char *instance);
bool ABinderProcess_setThreadPoolMaxThreadCount(uint32_t threads);
void ABinderProcess_joinThreadPool(void);
void AIBinder_markVintfStability(AIBinder *binder);

#define TAG "seh-stubs"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#define GET_INTERFACE_VERSION 0x00fffffe
#define GET_INTERFACE_HASH 0x00fffffd

static binder_status_t stub_transact(AIBinder *binder, transaction_code_t code,
				     const AParcel *in, AParcel *out)
{
	const char *name = AIBinder_getUserData(binder);
	AStatus *st = AStatus_newOk();
	binder_status_t r = AParcel_writeStatusHeader(out, st);

	(void)in;
	AStatus_delete(st);
	if (r)
		return r;
	if (code == GET_INTERFACE_VERSION)
		return AParcel_writeInt32(out, 1);
	if (code == GET_INTERFACE_HASH)
		return AParcel_writeString(out, "notfrozen", 9);
	LOGI("%s: call %u", name, code);
	for (int i = 0; !r && i < 4; i++)
		r = AParcel_writeInt32(out, 0);
	return r;
}

static void *on_create(void *args) { return args; }
static void on_destroy(void *userdata) { (void)userdata; }

int main(int argc, char **argv)
{
	int up = 0;

	ABinderProcess_setThreadPoolMaxThreadCount(1);
	for (int i = 1; i < argc; i++) {
		AIBinder_Class *cls = AIBinder_Class_define(argv[i], on_create, on_destroy, stub_transact);
		AIBinder *b = AIBinder_new(cls, argv[i]);
		char instance[256];
		binder_exception_t e;

		AIBinder_markVintfStability(b);
		snprintf(instance, sizeof(instance), "%s/default", argv[i]);
		e = AServiceManager_addService(b, instance);
		if (e != EX_NONE)
			LOGE("addService(%s): exception %d", instance, e);
		else
			up++;
	}
	LOGI("%d of %d stubs up", up, argc - 1);
	ABinderProcess_joinThreadPool();
	return 0;
}
