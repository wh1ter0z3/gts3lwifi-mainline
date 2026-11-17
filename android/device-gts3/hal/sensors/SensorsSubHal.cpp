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
#define LOG_TAG "gts3-sensors"

#include "SensorsSubHal.h"

#include <log/log.h>
#include <unistd.h>

#include <cstdio>
#include <sstream>

using ::android::hardware::sensors::V2_1::implementation::ISensorsSubHal;
using ::android::hardware::sensors::V2_1::subhal::implementation::SensorsSubHal;

// The multihal loads this library (vendor/etc/sensors/hals.conf) and calls this
ISensorsSubHal* sensorsHalGetSubHal_2_1(uint32_t* version) {
    static SensorsSubHal subHal;
    *version = SUB_HAL_2_1_VERSION;
    return &subHal;
}

namespace android::hardware::sensors::V2_1::subhal::implementation {

using ::android::hardware::Void;

SensorsSubHal::SensorsSubHal() {
    int32_t handle = 1;
    for (const auto& dev : iioFindSensors()) {
        auto s = std::make_unique<IioSensor>(handle, this, dev);
        s->start();
        mSensors[handle++] = std::move(s);
    }
    ALOGI("%zu sensor(s)", mSensors.size());
}

Return<void> SensorsSubHal::getSensorsList_2_1(getSensorsList_2_1_cb _hidl_cb) {
    std::vector<SensorInfo> list;
    for (const auto& s : mSensors) list.push_back(s.second->getSensorInfo());
    _hidl_cb(list);
    return Void();
}

Return<Result> SensorsSubHal::setOperationMode(OperationMode mode) {
    for (auto& s : mSensors) s.second->setOperationMode(mode);
    return Result::OK;
}

Return<Result> SensorsSubHal::activate(int32_t sensorHandle, bool enabled) {
    auto s = mSensors.find(sensorHandle);
    if (s == mSensors.end()) return Result::BAD_VALUE;
    s->second->activate(enabled);
    return Result::OK;
}

Return<Result> SensorsSubHal::batch(int32_t sensorHandle, int64_t samplingPeriodNs,
                                    int64_t /* maxReportLatencyNs */) {
    auto s = mSensors.find(sensorHandle);
    if (s == mSensors.end()) return Result::BAD_VALUE;
    s->second->batch(samplingPeriodNs);
    return Result::OK;
}

Return<Result> SensorsSubHal::flush(int32_t sensorHandle) {
    auto s = mSensors.find(sensorHandle);
    if (s == mSensors.end()) return Result::BAD_VALUE;
    return s->second->flush();
}

Return<Result> SensorsSubHal::injectSensorData_2_1(const Event& /* event */) {
    return Result::INVALID_OPERATION;
}

Return<void> SensorsSubHal::registerDirectChannel(const SharedMemInfo& /* mem */,
                                                  registerDirectChannel_cb _hidl_cb) {
    _hidl_cb(Result::INVALID_OPERATION, -1 /* channelHandle */);
    return Void();
}

Return<Result> SensorsSubHal::unregisterDirectChannel(int32_t /* channelHandle */) {
    return Result::INVALID_OPERATION;
}

Return<void> SensorsSubHal::configDirectReport(int32_t /* sensorHandle */,
                                               int32_t /* channelHandle */, RateLevel /* rate */,
                                               configDirectReport_cb _hidl_cb) {
    _hidl_cb(Result::INVALID_OPERATION, 0 /* reportToken */);
    return Void();
}

Return<void> SensorsSubHal::debug(const hidl_handle& fd, const hidl_vec<hidl_string>& /* args */) {
    if (fd.getNativeHandle() == nullptr || fd->numFds < 1) return Void();
    FILE* out = fdopen(dup(fd->data[0]), "w");
    if (!out) return Void();
    std::ostringstream o;
    for (const auto& s : mSensors) {
        const SensorInfo& i = s.second->getSensorInfo();
        const IioDevice& d = s.second->device();
        o << i.sensorHandle << ": " << i.name << " (" << d.name << ", " << d.sysfs << "), "
          << d.rates.front() << "-" << d.rates.back() << " Hz, scale " << d.scale
          << (d.boottime ? "" : ", timestamps not on boottime") << "\n";
    }
    fprintf(out, "%s", o.str().c_str());
    fclose(out);
    return Void();
}

Return<Result> SensorsSubHal::initialize(const sp<IHalProxyCallback>& halProxyCallback) {
    mCallback = halProxyCallback;
    setOperationMode(OperationMode::NORMAL);
    return Result::OK;
}

void SensorsSubHal::postEvents(const std::vector<Event>& events, ScopedWakelock wakelock) {
    if (mCallback) mCallback->postEvents(events, std::move(wakelock));
}

ScopedWakelock SensorsSubHal::createScopedWakelock(bool lock) {
    return mCallback->createScopedWakelock(lock);
}

}  // namespace android::hardware::sensors::V2_1::subhal::implementation
