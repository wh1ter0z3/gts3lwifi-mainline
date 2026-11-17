/*
 * Copyright (C) 2020 The Android Open Source Project
 * Copyright (C) 2026 The gts3lwifi-mainline project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * From device/google/trout/hal/sensors/2.1 (AOSP's IIO sub-HAL): the
 * sensor base class and thread kept, the IIO side rewritten for qcom_smgr.
 */
#pragma once

#include <android/hardware/sensors/2.1/types.h>
#include <poll.h>

#include <mutex>
#include <vector>

#include "SensorThread.h"
#include "V2_0/ScopedWakelock.h"
#include "iio.h"

namespace android::hardware::sensors::V2_1::subhal::implementation {

using ::android::hardware::sensors::V1_0::OperationMode;
using ::android::hardware::sensors::V1_0::Result;
using ::android::hardware::sensors::V2_0::implementation::ScopedWakelock;
using ::android::hardware::sensors::V2_1::Event;
using ::android::hardware::sensors::V2_1::SensorInfo;

class ISensorsEventCallback {
  public:
    virtual ~ISensorsEventCallback() = default;
    virtual void postEvents(const std::vector<Event>& events, ScopedWakelock wakelock) = 0;
    virtual ScopedWakelock createScopedWakelock(bool lock) = 0;
};

class IioSensor {
  public:
    IioSensor(int32_t handle, ISensorsEventCallback* callback, const IioDevice& dev);
    ~IioSensor();

    // After construction: the thread calls back into this object
    void start();

    const SensorInfo& getSensorInfo() const { return mInfo; }
    const IioDevice& device() const { return mDev; }
    void batch(int64_t samplingPeriodNs);
    void activate(bool enable);
    Result flush();
    void setOperationMode(OperationMode mode);

  private:
    friend class SensorThread;
    void pollSensor();  // one round of the thread's loop
    void readEvents();
    bool parse(const uint8_t* scan, Event* ev);

    IioDevice mDev;
    SensorInfo mInfo;
    ISensorsEventCallback* mCallback;
    bool mEnabled = false;
    OperationMode mMode = OperationMode::NORMAL;
    unsigned mRate = 0;
    int mFd = -1;
    std::vector<uint8_t> mBuf;
    bool mHaveLast = false;  // on-change: the last value reported
    float mLast = 0;
    SensorThread mThread;
};

}  // namespace android::hardware::sensors::V2_1::subhal::implementation
