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

#include "Sensor.h"

#include <fcntl.h>
#include <hardware/sensors.h>
#include <log/log.h>
#include <unistd.h>
#include <utils/SystemClock.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace android::hardware::sensors::V2_1::subhal::implementation {

using ::android::hardware::sensors::V1_0::MetaDataEventType;
using ::android::hardware::sensors::V1_0::SensorFlagBits;
using ::android::hardware::sensors::V1_0::SensorStatus;

static bool isOnChange(SensorType t) {
    return t == SensorType::LIGHT;
}

IioSensor::IioSensor(int32_t handle, ISensorsEventCallback* callback, const IioDevice& dev)
    : mDev(dev), mCallback(callback), mThread(this) {
    mInfo.sensorHandle = handle;
    mInfo.name = dev.model;
    mInfo.vendor = dev.vendor;
    mInfo.version = 1;
    mInfo.type = dev.type;
    switch (dev.type) {
        case SensorType::ACCELEROMETER:
            mInfo.typeAsString = SENSOR_STRING_TYPE_ACCELEROMETER;
            break;
        case SensorType::GYROSCOPE:
            mInfo.typeAsString = SENSOR_STRING_TYPE_GYROSCOPE;
            break;
        case SensorType::MAGNETIC_FIELD:
            mInfo.typeAsString = SENSOR_STRING_TYPE_MAGNETIC_FIELD;
            break;
        case SensorType::LIGHT:
            mInfo.typeAsString = SENSOR_STRING_TYPE_LIGHT;
            break;
        default:
            break;
    }
    mInfo.maxRange = dev.maxRange;
    mInfo.resolution = dev.scale * dev.toAndroid;
    mInfo.power = dev.power;
    mInfo.minDelay = 1000000 / dev.rates.back();   // us, at the fastest rate
    mInfo.maxDelay = 1000000 / dev.rates.front();  // us, at the slowest
    mInfo.fifoReservedEventCount = 0;
    mInfo.fifoMaxEventCount = 0;
    mInfo.requiredPermission = "";
    mInfo.flags = static_cast<uint32_t>(isOnChange(dev.type) ? SensorFlagBits::ON_CHANGE_MODE
                                                             : SensorFlagBits::CONTINUOUS_MODE);
    mRate = dev.rates.back();
    mBuf.resize(dev.scanSize * 16);
    mFd = open(("/dev/iio:device" + std::to_string(dev.num)).c_str(), O_RDONLY | O_NONBLOCK);
    if (mFd < 0) ALOGE("%s: can't open /dev/iio:device%d: %s", dev.name.c_str(), dev.num,
                       strerror(errno));
}

void IioSensor::start() {
    mThread.start();
}

IioSensor::~IioSensor() {
    {
        auto lock = mThread.lock();
        mEnabled = false;
        mThread.stop();
        mThread.notifyAll();
    }
    mThread.join();
    if (mFd >= 0) close(mFd);
}

void IioSensor::batch(int64_t samplingPeriodNs) {
    if (samplingPeriodNs <= 0) samplingPeriodNs = 1;
    // The slowest rate at least as fast as asked for, else the fastest
    const double want = 1e9 / samplingPeriodNs;
    unsigned rate = mDev.rates.back();
    for (unsigned r : mDev.rates)
        if (r >= want - 0.5) {
            rate = r;
            break;
        }
    auto lock = mThread.lock();
    if (rate == mRate) return;
    // qcom_smgr takes a new rate only while the buffer is off (EBUSY)
    if (mEnabled) iioEnable(mDev, false);
    if (iioSetRate(mDev, rate) == 0) mRate = rate;
    if (mEnabled && iioEnable(mDev, true)) {
        ALOGE("%s: can't enable its buffer again", mDev.name.c_str());
        mEnabled = false;
    }
}

void IioSensor::activate(bool enable) {
    auto lock = mThread.lock();
    if (enable == mEnabled) return;
    if (enable) iioSetRate(mDev, mRate);
    if (iioEnable(mDev, enable) && enable) {
        ALOGE("%s: can't enable its buffer", mDev.name.c_str());
        return;
    }
    mEnabled = enable;
    mHaveLast = false;
    mThread.notifyAll();
}

Result IioSensor::flush() {
    if (!mEnabled) return Result::BAD_VALUE;
    Event ev{};
    ev.sensorHandle = mInfo.sensorHandle;
    ev.sensorType = SensorType::META_DATA;
    ev.u.meta.what = MetaDataEventType::META_DATA_FLUSH_COMPLETE;
    mCallback->postEvents({ev}, mCallback->createScopedWakelock(false));
    return Result::OK;
}

void IioSensor::setOperationMode(OperationMode mode) {
    auto lock = mThread.lock();
    if (mMode == mode) return;
    mMode = mode;
    mThread.notifyAll();
}

void IioSensor::pollSensor() {
    {
        auto lock = mThread.lock();
        if (!mEnabled || mMode != OperationMode::NORMAL) {
            mThread.wait_locked(lock, [this] {
                return (mEnabled && mMode == OperationMode::NORMAL) || mThread.isStopped();
            });
            return;
        }
    }
    readEvents();
}

void IioSensor::readEvents() {
    if (mFd < 0) {
        usleep(1000000);
        return;
    }
    pollfd pfd{.fd = mFd, .events = POLLIN};
    // Wake up now and then to see whether it was turned off
    if (poll(&pfd, 1, 500) <= 0 || !(pfd.revents & POLLIN)) return;
    const ssize_t n = read(mFd, mBuf.data(), mBuf.size());
    if (n < (ssize_t)mDev.scanSize) return;

    std::vector<Event> events;
    for (ssize_t off = 0; off + (ssize_t)mDev.scanSize <= n; off += mDev.scanSize) {
        Event ev{};
        if (parse(mBuf.data() + off, &ev)) events.push_back(ev);
    }
    if (!events.empty()) mCallback->postEvents(events, mCallback->createScopedWakelock(false));
}

static double readChannel(const uint8_t* scan, const IioChannel& c) {
    uint64_t raw = 0;
    for (unsigned i = 0; i < c.storageBytes; i++) {
        const unsigned b = c.bigEndian ? i : c.storageBytes - 1 - i;
        raw = (raw << 8) | scan[c.offset + b];
    }
    raw >>= c.shift;
    if (c.bits < 64) raw &= (1ULL << c.bits) - 1;
    if (c.isSigned && c.bits < 64 && (raw & (1ULL << (c.bits - 1))))
        return (double)(int64_t)(raw | ~((1ULL << c.bits) - 1));
    return c.isSigned ? (double)(int64_t)raw : (double)raw;
}

bool IioSensor::parse(const uint8_t* scan, Event* ev) {
    ev->sensorHandle = mInfo.sensorHandle;
    ev->sensorType = mInfo.type;
    if (mDev.timestampOffset >= 0 && mDev.boottime) {
        int64_t ts;
        memcpy(&ts, scan + mDev.timestampOffset, sizeof(ts));
        ev->timestamp = ts;
    } else {
        ev->timestamp = elapsedRealtimeNano();
    }
    const float k = mDev.scale * mDev.toAndroid;

    if (mDev.data.size() == 1) {
        const float v = readChannel(scan, mDev.data[0]) * k;
        if (isOnChange(mInfo.type) && mHaveLast && v == mLast) return false;
        mHaveLast = true;
        mLast = v;
        ev->u.scalar = v;
        return true;
    }
    if (mDev.data.size() < 3) return false;
    // The mount matrix turns the chip's axes into the tablet's
    float s[3], d[3];
    for (int i = 0; i < 3; i++) s[i] = readChannel(scan, mDev.data[i]) * k;
    for (int i = 0; i < 3; i++)
        d[i] = mDev.matrix[i * 3] * s[0] + mDev.matrix[i * 3 + 1] * s[1] +
               mDev.matrix[i * 3 + 2] * s[2];
    ev->u.vec3.x = d[0];
    ev->u.vec3.y = d[1];
    ev->u.vec3.z = d[2];
    ev->u.vec3.status = mInfo.type == SensorType::MAGNETIC_FIELD ? SensorStatus::ACCURACY_MEDIUM
                                                                  : SensorStatus::ACCURACY_HIGH;
    return true;
}

}  // namespace android::hardware::sensors::V2_1::subhal::implementation
