/*
 * Copyright (C) 2026 The gts3lwifi-mainline project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "gts3-gnss"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "Gnss.h"

using aidl::android::hardware::gnss::gts3::Gnss;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(2);
    ABinderProcess_startThreadPool();

    auto gnss = ndk::SharedRefBase::make<Gnss>();
    const std::string instance = std::string(Gnss::descriptor) + "/default";
    CHECK_EQ(AServiceManager_addService(gnss->asBinder().get(), instance.c_str()), STATUS_OK);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // not reached
}
