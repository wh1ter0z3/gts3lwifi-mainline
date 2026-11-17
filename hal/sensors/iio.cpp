/*
 * Copyright (C) 2026 The gts3lwifi-mainline project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "gts3-sensors"

#include "iio.h"

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <dirent.h>
#include <log/log.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace android::hardware::sensors::V2_1::subhal::implementation {

using ::android::base::ReadFileToString;
using ::android::base::Split;
using ::android::base::StartsWith;
using ::android::base::Trim;
using ::android::base::WriteStringToFile;

static const char* const kIioDir = "/sys/bus/iio/devices";

// What the SLPI has on this tablet (qcom_smgr: accel, gyro and mag values
// in 16.16 fixed point, light in plain lux). Ranges and currents are the
// chips' data sheet values; the SLPI may run them narrower.
static const IioDevice kKnown[] = {
        {.name = "qcom-smgr-accel",
         .type = SensorType::ACCELEROMETER,
         .prefix = "in_accel",
         .model = "LSM6DS3 accelerometer",
         .vendor = "STMicroelectronics",
         .toAndroid = 1.0f,
         .maxRange = 78.4532f,  // 8 g
         .power = 0.25f},
        {.name = "qcom-smgr-gyro",
         .type = SensorType::GYROSCOPE,
         .prefix = "in_anglvel",
         .model = "LSM6DS3 gyroscope",
         .vendor = "STMicroelectronics",
         .toAndroid = 1.0f,
         .maxRange = 34.906585f,  // 2000 dps
         .power = 0.9f},
        {.name = "qcom-smgr-mag",
         .type = SensorType::MAGNETIC_FIELD,
         .prefix = "in_magn",
         .model = "AK09916C magnetometer",
         .vendor = "Asahi Kasei",
         .toAndroid = 100.0f,  // IIO's Gauss to Android's microtesla
         .maxRange = 4912.0f,
         .power = 1.1f},
        {.name = "qcom-smgr-light",
         .type = SensorType::LIGHT,
         .prefix = "in_intensity_both",
         .model = "CM3323 light sensor",
         .vendor = "Capella",
         .toAndroid = 1.0f,
         .maxRange = 65535.0f,
         .power = 0.2f},
};

// Rates offered below the device's own (its sampling_frequency at start,
// which is its fastest): SMGR takes any of them, and the framework asks for
// 5 (normal), 16 (UI), 50 (game) and the fastest
static const unsigned kRates[] = {1, 5, 10, 16, 25, 50, 100, 200};

static bool readStr(const std::string& path, std::string* out) {
    if (!ReadFileToString(path, out)) return false;
    *out = Trim(*out);
    return true;
}

static bool writeStr(const std::string& path, const std::string& value) {
    if (WriteStringToFile(value, path)) return true;
    ALOGE("writing %s to %s: %s", value.c_str(), path.c_str(), strerror(errno));
    return false;
}

// "le:s32/32>>0"
static bool parseType(const std::string& s, IioChannel* c) {
    char endian, sign;
    unsigned bits, storage, shift = 0;
    if (sscanf(s.c_str(), "%ce:%c%u/%u>>%u", &endian, &sign, &bits, &storage, &shift) < 4)
        return false;
    if (storage == 0 || storage % 8 || storage > 64 || bits > storage) return false;
    c->bigEndian = endian == 'b';
    c->isSigned = sign == 's';
    c->bits = bits;
    c->storageBytes = storage / 8;
    c->shift = shift;
    return true;
}

// Turns on every scan element, reads their layout, and works out where each
// lands in a scan: IIO aligns each element to its own size and pads the scan
// to the largest one.
static bool readScan(IioDevice* d) {
    const std::string dir = d->sysfs + "/scan_elements";
    std::unique_ptr<DIR, int (*)(DIR*)> dp(opendir(dir.c_str()), closedir);
    if (!dp) return false;
    std::vector<IioChannel> all;
    while (const dirent* e = readdir(dp.get())) {
        std::string n = e->d_name;
        if (n.size() < 4 || n.compare(n.size() - 3, 3, "_en")) continue;
        IioChannel c{};
        c.name = n.substr(0, n.size() - 3);
        std::string v;
        if (!readStr(dir + "/" + n, &v) || v != "1") {
            if (!writeStr(dir + "/" + n, "1")) return false;
        }
        if (!readStr(dir + "/" + c.name + "_index", &v) || !::android::base::ParseUint(v, &c.index))
            return false;
        if (!readStr(dir + "/" + c.name + "_type", &v) || !parseType(v, &c)) return false;
        all.push_back(c);
    }
    std::sort(all.begin(), all.end(),
              [](const IioChannel& a, const IioChannel& b) { return a.index < b.index; });
    unsigned off = 0, largest = 1;
    for (auto& c : all) {
        off = (off + c.storageBytes - 1) / c.storageBytes * c.storageBytes;
        c.offset = off;
        off += c.storageBytes;
        largest = std::max(largest, c.storageBytes);
        if (c.name == "in_timestamp")
            d->timestampOffset = c.offset;
        else
            d->data.push_back(c);
    }
    d->scanSize = (off + largest - 1) / largest * largest;
    return !d->data.empty();
}

static void readMatrix(IioDevice* d) {
    std::string v;
    if (!readStr(d->sysfs + "/in_mount_matrix", &v)) return;
    std::vector<std::string> f;
    for (const auto& row : Split(v, ";"))
        for (const auto& x : Split(row, ",")) f.push_back(Trim(x));
    if (f.size() != 9) return;
    float m[9];
    for (int i = 0; i < 9; i++)
        if (sscanf(f[i].c_str(), "%f", &m[i]) != 1) return;
    std::copy(m, m + 9, d->matrix);
}

std::vector<IioDevice> iioFindSensors() {
    std::vector<IioDevice> found;
    std::unique_ptr<DIR, int (*)(DIR*)> dp(opendir(kIioDir), closedir);
    if (!dp) {
        ALOGE("no %s", kIioDir);
        return found;
    }
    while (const dirent* e = readdir(dp.get())) {
        int num;
        if (sscanf(e->d_name, "iio:device%d", &num) != 1) continue;
        const std::string sysfs = std::string(kIioDir) + "/" + e->d_name;
        std::string name;
        if (!readStr(sysfs + "/name", &name)) continue;
        auto k = std::find_if(std::begin(kKnown), std::end(kKnown),
                              [&](const IioDevice& d) { return d.name == name; });
        if (k == std::end(kKnown)) continue;

        IioDevice d = *k;
        d.sysfs = sysfs;
        d.num = num;
        iioEnable(d, false);  // the layout can't change while it streams
        std::string v;
        if (readStr(sysfs + "/" + d.prefix + "_scale", &v)) sscanf(v.c_str(), "%f", &d.scale);
        readMatrix(&d);
        d.boottime = writeStr(sysfs + "/current_timestamp_clock", "boottime\n");
        if (!readScan(&d)) {
            ALOGE("%s: can't set up its scan elements", name.c_str());
            continue;
        }
        unsigned fastest = 0;
        if (readStr(sysfs + "/" + d.prefix + "_sampling_frequency", &v))
            ::android::base::ParseUint(v, &fastest);
        if (fastest == 0) fastest = 5;
        for (unsigned r : kRates)
            if (r < fastest) d.rates.push_back(r);
        d.rates.push_back(fastest);
        ALOGI("%s: iio:device%d, scale %g, %zu channel(s), scan %zu bytes, %u Hz max%s", name.c_str(),
              num, d.scale, d.data.size(), d.scanSize, fastest,
              d.boottime ? "" : ", timestamps not on boottime");
        found.push_back(d);
    }
    // A fixed order, so sensor handles don't depend on the IIO numbering
    std::sort(found.begin(), found.end(), [](const IioDevice& a, const IioDevice& b) {
        auto ia = std::find_if(std::begin(kKnown), std::end(kKnown),
                               [&](const IioDevice& k) { return k.name == a.name; });
        auto ib = std::find_if(std::begin(kKnown), std::end(kKnown),
                               [&](const IioDevice& k) { return k.name == b.name; });
        return ia < ib;
    });
    return found;
}

int iioEnable(const IioDevice& dev, bool enable) {
    return writeStr(dev.sysfs + "/buffer/enable", enable ? "1" : "0") ? 0 : -errno;
}

int iioSetRate(const IioDevice& dev, unsigned hz) {
    return writeStr(dev.sysfs + "/" + dev.prefix + "_sampling_frequency", std::to_string(hz))
                   ? 0
                   : -errno;
}

}  // namespace android::hardware::sensors::V2_1::subhal::implementation
