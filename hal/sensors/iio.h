/*
 * Copyright (C) 2026 The gts3lwifi-mainline project
 * SPDX-License-Identifier: Apache-2.0
 *
 * The Galaxy Tab S3's sensors as the kernel shows them: IIO devices of the
 * qcom_smgr driver (the SLPI's sensor manager), buffer-only, a timestamp
 * channel last.
 */
#pragma once

#include <android/hardware/sensors/2.1/types.h>

#include <string>
#include <vector>

namespace android::hardware::sensors::V2_1::subhal::implementation {

using ::android::hardware::sensors::V2_1::SensorType;

struct IioChannel {
    std::string name;  // e.g. in_accel_x
    unsigned index;
    unsigned offset;  // in a scan, after IIO's alignment
    unsigned storageBytes;
    unsigned bits;
    unsigned shift;
    bool isSigned;
    bool bigEndian;
};

struct IioDevice {
    // What the table in iio.cpp knows about this device
    std::string name;  // the IIO name, e.g. qcom-smgr-accel
    SensorType type;
    std::string prefix;  // attribute prefix, e.g. in_accel
    std::string model;   // the chip, for SensorInfo
    std::string vendor;
    float toAndroid;  // IIO unit -> Android unit (Gauss -> uT: 100)
    float maxRange;   // in Android units
    float power;      // mA, roughly
    unsigned fastest;  // Hz: qcom_smgr's rate at probe, its highest

    // Read from sysfs
    std::string sysfs;  // /sys/bus/iio/devices/iio:deviceN
    int num = -1;       // N, for /dev/iio:deviceN
    float scale = 1.0f;
    float matrix[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};  // in_mount_matrix, row major
    std::vector<IioChannel> data;  // the data channels, by index
    int timestampOffset = -1;      // -1: no timestamp channel
    size_t scanSize = 0;
    bool boottime = false;           // timestamps on CLOCK_BOOTTIME
    std::vector<unsigned> rates;     // Hz we offer, ascending
};

// The known sensors present now, set up for reading: all scan elements on,
// timestamps on CLOCK_BOOTTIME, buffer off. Returns them in a fixed order.
std::vector<IioDevice> iioFindSensors();

int iioEnable(const IioDevice& dev, bool enable);
int iioSetRate(const IioDevice& dev, unsigned hz);

}  // namespace android::hardware::sensors::V2_1::subhal::implementation
