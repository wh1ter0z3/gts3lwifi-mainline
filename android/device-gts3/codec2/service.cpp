// Adapted from SamuraiArtem's android_device_samsung_j5xnlte (codec2-aidl/), Apache-2.0.
// Codec2 service exposing the Venus video core to Android.
//
// external/v4l2_codec2 only ships a HIDL service, which is dead on this build:
// hwservicemanager exits at boot ("HIDL is not supported on this device"), so a
// HIDL service runs but can never register. The framework is on AIDL here -
// com.android.media.codec.flags.aidl_hal is enabled and the software store
// already registers as IComponentStore/software - so the same V4L2 component
// store is wrapped in the AIDL ComponentStore instead. The body follows
// frameworks/av/media/codec2/hal/services/vendor.cpp.

#define LOG_TAG "android.hardware.media.c2-service-v4l2"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <codec2/aidl/ComponentStore.h>
#include <log/log.h>
#include <minijail.h>

#include <v4l2_codec2/v4l2/V4L2ComponentStore.h>

// Installed by the android.hardware.media.c2-default-seccomp_policy module.
static constexpr char kBaseSeccompPolicyPath[] =
        "/vendor/etc/seccomp_policy/"
        "android.hardware.media.c2-default-seccomp_policy";

// Optional, does not exist by default.
static constexpr char kExtSeccompPolicyPath[] =
        "/vendor/etc/seccomp_policy/"
        "android.hardware.media.c2-extended-seccomp_policy";

// A stacked IPC sequence can alternate between binder and hwbinder calls, so
// keep spare threads around (see b/35283480).
static constexpr size_t kThreadCount = 16;

int main(int /* argc */, char ** /* argv */) {
    ALOGD("Service starting...");

    signal(SIGPIPE, SIG_IGN);
    android::SetUpMinijail(kBaseSeccompPolicyPath, kExtSeccompPolicyPath);

    ABinderProcess_setThreadPoolMaxThreadCount(kThreadCount);
    ABinderProcess_startThreadPool();

    using namespace ::aidl::android::hardware::media::c2;

    std::shared_ptr<IComponentStore> store =
            ::ndk::SharedRefBase::make<utils::ComponentStore>(
                    android::V4L2ComponentStore::Create());
    if (store == nullptr) {
        ALOGE("Cannot create Codec2's IComponentStore service.");
        return 1;
    }

    const std::string name = std::string(IComponentStore::descriptor) + "/default";
    binder_exception_t ex = AServiceManager_addService(store->asBinder().get(), name.c_str());
    if (ex != EX_NONE) {
        ALOGE("Cannot register %s (exception %d).", name.c_str(), ex);
        return 1;
    }
    ALOGI("Codec2's V4L2 IComponentStore registered as %s.", name.c_str());

    ABinderProcess_joinThreadPool();
    return 0;
}
