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
 */
#pragma once

#include <map>
#include <memory>
#include <vector>

#include "Sensor.h"
#include "SubHal.h"

namespace android::hardware::sensors::V2_1::subhal::implementation {

using ::android::hardware::sensors::V2_1::implementation::IHalProxyCallback;
using ::android::hardware::sensors::V2_1::implementation::ISensorsSubHal;

class SensorsSubHal : public ISensorsSubHal, public ISensorsEventCallback {
    using RateLevel = ::android::hardware::sensors::V1_0::RateLevel;
    using SharedMemInfo = ::android::hardware::sensors::V1_0::SharedMemInfo;

  public:
    SensorsSubHal();

    Return<void> getSensorsList_2_1(getSensorsList_2_1_cb _hidl_cb) override;
    Return<Result> setOperationMode(OperationMode mode) override;
    Return<Result> activate(int32_t sensorHandle, bool enabled) override;
    Return<Result> batch(int32_t sensorHandle, int64_t samplingPeriodNs,
                         int64_t maxReportLatencyNs) override;
    Return<Result> flush(int32_t sensorHandle) override;
    Return<Result> injectSensorData_2_1(const Event& event) override;
    Return<void> registerDirectChannel(const SharedMemInfo& mem,
                                       registerDirectChannel_cb _hidl_cb) override;
    Return<Result> unregisterDirectChannel(int32_t channelHandle) override;
    Return<void> configDirectReport(int32_t sensorHandle, int32_t channelHandle, RateLevel rate,
                                    configDirectReport_cb _hidl_cb) override;
    Return<void> debug(const hidl_handle& fd, const hidl_vec<hidl_string>& args) override;
    const std::string getName() override { return "gts3-IIO-SensorsSubHal"; }
    Return<Result> initialize(const sp<IHalProxyCallback>& halProxyCallback) override;

    void postEvents(const std::vector<Event>& events, ScopedWakelock wakelock) override;
    ScopedWakelock createScopedWakelock(bool lock) override;

  private:
    std::map<int32_t, std::unique_ptr<IioSensor>> mSensors;
    sp<IHalProxyCallback> mCallback;
};

}  // namespace android::hardware::sensors::V2_1::subhal::implementation
