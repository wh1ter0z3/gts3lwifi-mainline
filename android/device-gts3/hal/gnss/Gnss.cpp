/*
 * Copyright (C) 2026 The gts3lwifi-mainline project
 * SPDX-License-Identifier: Apache-2.0
 *
 * The GNSS engine in the modem DSP (Samsung's GNSS-only "modemlite" image on
 * the SM-T820, the LTE image on the SM-T825), through its QMI LOC service:
 * a standalone session (no SUPL, no predicted orbits) with the network time
 * and position the framework injects. As pmOS's gnss-share qmi_loc driver,
 * which reads only its NMEA; here also the modem's own position reports,
 * which carry its uncertainty, and its satellite list.
 */
#define LOG_TAG "gts3-gnss"

#include "Gnss.h"

#include <android/binder_status.h>
#include <log/log.h>
#include <utils/SystemClock.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>

namespace aidl::android::hardware::gnss::gts3 {

using GnssSvInfo = IGnssCallback::GnssSvInfo;
using GnssStatusValue = IGnssCallback::GnssStatusValue;

// QMI LOC event mask: position report, satellite info, NMEA, time and
// position requests, engine and session state
static constexpr uint64_t kEvents = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 4) | (1 << 6) |
                                    (1 << 7) | (1 << 8);
static constexpr uint32_t kModeStandalone = 4;
static constexpr uint8_t kSessionId = 1;

static ndk::ScopedAStatus unsupported() {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

static int64_t nowUtcMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
}

Gnss::Gnss() : mLoc(this) {
    mLoc.start();
}

std::shared_ptr<IGnssCallback> Gnss::callback() {
    std::lock_guard<std::mutex> lock(mLock);
    return mCallback;
}

ndk::ScopedAStatus Gnss::setCallback(const std::shared_ptr<IGnssCallback>& callback) {
    {
        std::lock_guard<std::mutex> lock(mLock);
        mCallback = callback;
    }
    if (!callback) return ndk::ScopedAStatus::ok();
    callback->gnssSetCapabilitiesCb(IGnssCallback::CAPABILITY_SCHEDULING |
                                    IGnssCallback::CAPABILITY_ON_DEMAND_TIME);
    IGnssCallback::GnssSystemInfo info;
    info.yearOfHw = 2016;
    info.name = "Qualcomm MSM8996 modem GNSS (QMI LOC)";
    callback->gnssSetSystemInfoCb(info);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Gnss::close() {
    stop();
    std::lock_guard<std::mutex> lock(mLock);
    mCallback = nullptr;
    return ndk::ScopedAStatus::ok();
}

void Gnss::setupLocked() {
    mLoc.request(LOC_REG_EVENTS, {{0x01, tlvOf(kEvents)}});
    mLoc.request(LOC_SET_OPERATION_MODE, {{0x01, tlvOf(kModeStandalone)}});
}

void Gnss::startSessionLocked() {
    const uint32_t periodic = 1, intermediate = 1, interval = std::max(mIntervalMs, 100);
    mSession = mLoc.request(LOC_START, {{0x01, tlvOf(kSessionId)},
                                        {0x10, tlvOf(periodic)},
                                        {0x12, tlvOf(intermediate)},
                                        {0x13, tlvOf(interval)}});
    ALOGI("session %s, every %u ms", mSession ? "started" : "waits for the modem", interval);
}

ndk::ScopedAStatus Gnss::start() {
    std::shared_ptr<IGnssCallback> cb;
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (!mStarted) {
            mStarted = true;
            startSessionLocked();
        }
        cb = mCallback;
    }
    if (cb) cb->gnssStatusCb(GnssStatusValue::SESSION_BEGIN);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Gnss::stop() {
    std::shared_ptr<IGnssCallback> cb;
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (!mStarted) return ndk::ScopedAStatus::ok();
        mStarted = false;
        if (mSession) mLoc.request(LOC_STOP, {{0x01, tlvOf(kSessionId)}});
        mSession = false;
        ALOGI("session stopped");
        cb = mCallback;
    }
    if (cb) cb->gnssStatusCb(GnssStatusValue::SESSION_END);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Gnss::setPositionMode(const PositionModeOptions& options) {
    std::lock_guard<std::mutex> lock(mLock);
    const int interval = std::max(options.minIntervalMs, 100);
    if (interval == mIntervalMs) return ndk::ScopedAStatus::ok();
    mIntervalMs = interval;
    // A running session takes the new interval when started again
    if (mStarted && mSession) {
        mLoc.request(LOC_STOP, {{0x01, tlvOf(kSessionId)}});
        startSessionLocked();
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Gnss::injectTime(int64_t timeMs, int64_t timeReferenceMs, int uncertaintyMs) {
    // The time now: the network's, plus what has passed since it was taken
    const uint64_t utc = timeMs + (::android::elapsedRealtime() - timeReferenceMs);
    const uint32_t unc = std::max(uncertaintyMs, 0);
    if (!mLoc.request(LOC_INJECT_UTC_TIME, {{0x01, tlvOf(utc)}, {0x02, tlvOf(unc)}}))
        return ndk::ScopedAStatus::fromServiceSpecificError(IGnss::ERROR_GENERIC);
    ALOGI("injected the network time (%u ms)", unc);
    return ndk::ScopedAStatus::ok();
}

void Gnss::injectPosition(const GnssLocation& l) {
    if (!(l.gnssLocationFlags & GnssLocation::HAS_LAT_LONG) ||
        !(l.gnssLocationFlags & GnssLocation::HAS_HORIZONTAL_ACCURACY))
        return;
    const float unc = l.horizontalAccuracyMeters;
    const uint8_t confidence = 68;
    const uint64_t utc = l.timestampMillis > 0 ? l.timestampMillis : nowUtcMs();
    if (mLoc.request(LOC_INJECT_POSITION, {{0x10, tlvOf(l.latitudeDegrees)},
                                           {0x11, tlvOf(l.longitudeDegrees)},
                                           {0x12, tlvOf(unc)},
                                           {0x13, tlvOf(confidence)},
                                           {0x1b, tlvOf(utc)}}))
        ALOGI("injected a position (%.0f m)", unc);
}

ndk::ScopedAStatus Gnss::injectLocation(const GnssLocation& location) {
    injectPosition(location);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Gnss::injectBestLocation(const GnssLocation& location) {
    injectPosition(location);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Gnss::deleteAidingData(IGnss::GnssAidingData flags) {
    if (flags == IGnss::GnssAidingData::ALL) {
        const uint8_t all = 1;
        mLoc.request(LOC_DELETE_ASSIST_DATA, {{0x01, tlvOf(all)}});
        ALOGI("deleted all aiding data");
        return ndk::ScopedAStatus::ok();
    }
    // QMI LOC's GNSS data mask for what Android names; ephemeris and almanac
    // go per satellite there, and are left alone
    const int f = static_cast<int>(flags);
    uint64_t mask = 0;
    if (f & static_cast<int>(IGnss::GnssAidingData::POSITION)) mask |= 1 << 10;
    if (f & static_cast<int>(IGnss::GnssAidingData::TIME)) mask |= (1 << 11) | (1 << 2) | (1 << 6);
    if (f & static_cast<int>(IGnss::GnssAidingData::IONO)) mask |= 1 << 12;
    if (f & static_cast<int>(IGnss::GnssAidingData::UTC)) mask |= 1 << 13;
    if (f & static_cast<int>(IGnss::GnssAidingData::HEALTH)) mask |= 1 << 14;
    if (f & static_cast<int>(IGnss::GnssAidingData::SVDIR)) mask |= (1 << 0) | (1 << 4) | (1 << 8);
    if (f & static_cast<int>(IGnss::GnssAidingData::SVSTEER)) mask |= (1 << 1) | (1 << 5) | (1 << 9);
    if (f & static_cast<int>(IGnss::GnssAidingData::SADATA)) mask |= 1 << 15;
    if (f & static_cast<int>(IGnss::GnssAidingData::RTI)) mask |= 1 << 16;
    if (mask) {
        const uint8_t all = 0;
        mLoc.request(LOC_DELETE_ASSIST_DATA, {{0x01, tlvOf(all)}, {0x11, tlvOf(mask)}});
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Gnss::startSvStatus() {
    std::lock_guard<std::mutex> lock(mLock);
    mSvStatus = true;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Gnss::stopSvStatus() {
    std::lock_guard<std::mutex> lock(mLock);
    mSvStatus = false;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Gnss::startNmea() {
    std::lock_guard<std::mutex> lock(mLock);
    mNmea = true;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Gnss::stopNmea() {
    std::lock_guard<std::mutex> lock(mLock);
    mNmea = false;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Gnss::getExtensionGnssConfiguration(
        std::shared_ptr<IGnssConfiguration>* _aidl_return) {
    static auto configuration = ndk::SharedRefBase::make<GnssConfiguration>();
    *_aidl_return = configuration;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Gnss::getExtensionPsds(std::shared_ptr<IGnssPsds>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionGnssMeasurement(std::shared_ptr<IGnssMeasurementInterface>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionGnssPowerIndication(std::shared_ptr<IGnssPowerIndication>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionGnssBatching(std::shared_ptr<IGnssBatching>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionGnssGeofence(std::shared_ptr<IGnssGeofence>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionGnssNavigationMessage(
        std::shared_ptr<IGnssNavigationMessageInterface>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionAGnss(std::shared_ptr<IAGnss>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionAGnssRil(std::shared_ptr<IAGnssRil>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionGnssDebug(std::shared_ptr<IGnssDebug>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionGnssVisibilityControl(
        std::shared_ptr<visibility_control::IGnssVisibilityControl>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionGnssAntennaInfo(std::shared_ptr<IGnssAntennaInfo>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionMeasurementCorrections(
        std::shared_ptr<measurement_corrections::IMeasurementCorrectionsInterface>*) {
    return unsupported();
}

void Gnss::onService(bool up) {
    std::lock_guard<std::mutex> lock(mLock);
    if (!up) {
        mSession = false;
        mEngineOn = false;
        return;
    }
    // A new modem knows nothing of us: events, mode, and the session again
    setupLocked();
    if (mStarted) startSessionLocked();
}

void Gnss::onIndication(uint16_t msgId, const Tlvs& tlvs) {
    switch (msgId) {
        case LOC_POSITION_REPORT_IND:
            reportPosition(tlvs);
            break;
        case LOC_GNSS_SV_INFO_IND:
            reportSvInfo(tlvs);
            break;
        case LOC_NMEA_IND:
            reportNmea(tlvs);
            break;
        case LOC_INJECT_TIME_REQ_IND:
            if (auto cb = callback()) {
                {
                    std::lock_guard<std::mutex> lock(mLock);
                    mTimeRequests++;
                }
                cb->gnssRequestTimeCb();
            }
            break;
        case LOC_INJECT_POSITION_REQ_IND:
            if (auto cb = callback()) cb->gnssRequestLocationCb(false, false);
            break;
        case LOC_ENGINE_STATE_IND: {
            int32_t state;
            if (!tlvGet(tlvs, 0x01, &state)) break;
            {
                std::lock_guard<std::mutex> lock(mLock);
                mEngineOn = state == 1;
            }
            ALOGI("engine %s", state == 1 ? "on" : "off");
            if (auto cb = callback())
                cb->gnssStatusCb(state == 1 ? GnssStatusValue::ENGINE_ON
                                            : GnssStatusValue::ENGINE_OFF);
            break;
        }
        default:
            break;
    }
}

void Gnss::reportPosition(const Tlvs& tlvs) {
    uint32_t status;
    double lat, lon;
    // Only finished fixes: intermediate reports are on, so that the engine
    // reports as soon as it has one
    if (!tlvGet(tlvs, 0x01, &status) || status != 0) return;
    if (!tlvGet(tlvs, 0x10, &lat) || !tlvGet(tlvs, 0x11, &lon)) return;

    GnssLocation l;
    l.gnssLocationFlags = GnssLocation::HAS_LAT_LONG;
    l.latitudeDegrees = lat;
    l.longitudeDegrees = lon;
    float f;
    uint8_t assumed = 0;
    tlvGet(tlvs, 0x2d, &assumed);
    if (!assumed && tlvGet(tlvs, 0x1a, &f)) {  // above the WGS 84 ellipsoid
        l.altitudeMeters = f;
        l.gnssLocationFlags |= GnssLocation::HAS_ALTITUDE;
    }
    if (tlvGet(tlvs, 0x18, &f)) {
        l.speedMetersPerSec = f;
        l.gnssLocationFlags |= GnssLocation::HAS_SPEED;
    }
    if (tlvGet(tlvs, 0x20, &f)) {
        l.bearingDegrees = f;
        l.gnssLocationFlags |= GnssLocation::HAS_BEARING;
    }
    if (tlvGet(tlvs, 0x12, &f)) {
        l.horizontalAccuracyMeters = f;
        l.gnssLocationFlags |= GnssLocation::HAS_HORIZONTAL_ACCURACY;
    }
    if (!assumed && tlvGet(tlvs, 0x1c, &f)) {
        l.verticalAccuracyMeters = f;
        l.gnssLocationFlags |= GnssLocation::HAS_VERTICAL_ACCURACY;
    }
    if (tlvGet(tlvs, 0x19, &f)) {
        l.speedAccuracyMetersPerSecond = f;
        l.gnssLocationFlags |= GnssLocation::HAS_SPEED_ACCURACY;
    }
    if (tlvGet(tlvs, 0x21, &f)) {
        l.bearingAccuracyDegrees = f;
        l.gnssLocationFlags |= GnssLocation::HAS_BEARING_ACCURACY;
    }
    uint64_t utc;
    l.timestampMillis = tlvGet(tlvs, 0x25, &utc) ? static_cast<int64_t>(utc) : nowUtcMs();
    l.elapsedRealtime.flags = ElapsedRealtime::HAS_TIMESTAMP_NS;
    l.elapsedRealtime.timestampNs = ::android::elapsedRealtimeNano();

    // The satellites used: a count and the QMI satellite numbers
    std::set<uint16_t> used;
    auto u = tlvs.find(0x2c);
    if (u != tlvs.end() && !u->second.empty()) {
        const size_t n = std::min<size_t>(static_cast<uint8_t>(u->second[0]),
                                          (u->second.size() - 1) / 2);
        for (size_t i = 0; i < n; i++) {
            uint16_t sv;
            memcpy(&sv, u->second.data() + 1 + 2 * i, 2);
            used.insert(sv);
        }
    }
    std::shared_ptr<IGnssCallback> cb;
    {
        std::lock_guard<std::mutex> lock(mLock);
        mUsed = std::move(used);
        mFixes++;
        if (!mStarted) return;
        cb = mCallback;
    }
    if (cb) cb->gnssLocationCb(l);
}

// QMI LOC's satellite numbers to Android's constellation and number
static bool svOf(uint32_t system, uint16_t id, GnssConstellationType* c, int* svid) {
    switch (system) {
        case 1:  // GPS (and QZSS at 193-200)
            *c = id >= 193 && id <= 200 ? GnssConstellationType::QZSS : GnssConstellationType::GPS;
            *svid = id;
            return true;
        case 2:  // Galileo, 301-336
            *c = GnssConstellationType::GALILEO;
            *svid = id > 300 ? id - 300 : id;
            return true;
        case 3:  // SBAS, 33-64 for PRN 120-151
            *c = GnssConstellationType::SBAS;
            *svid = id < 120 ? id + 87 : id;
            return true;
        case 4:  // BeiDou, 201-237
            *c = GnssConstellationType::BEIDOU;
            *svid = id > 200 ? id - 200 : id;
            return true;
        case 5:  // GLONASS, 65-96 for slots 1-32
            *c = GnssConstellationType::GLONASS;
            *svid = id > 64 ? id - 64 : id;
            return true;
        default:
            return false;
    }
}

void Gnss::reportSvInfo(const Tlvs& tlvs) {
    auto list = tlvs.find(0x10);
    if (list == tlvs.end() || list->second.empty()) return;
    // A count, then per satellite: valid mask, system, number, health, status,
    // info mask (ephemeris, almanac), elevation, azimuth, C/N0
    static constexpr size_t kEntry = 4 + 4 + 2 + 1 + 4 + 1 + 4 + 4 + 4;
    const std::string& s = list->second;
    const size_t n = std::min<size_t>(static_cast<uint8_t>(s[0]), (s.size() - 1) / kEntry);

    std::vector<GnssSvInfo> svs;
    std::set<uint16_t> used;
    std::shared_ptr<IGnssCallback> cb;
    {
        std::lock_guard<std::mutex> lock(mLock);
        mSvReports++;
        if (!mStarted || !mSvStatus || !mCallback) return;
        used = mUsed;
        cb = mCallback;
    }
    for (size_t i = 0; i < n; i++) {
        const char* e = s.data() + 1 + i * kEntry;
        uint32_t valid, system;
        uint16_t id;
        uint8_t mask;
        float elevation, azimuth, snr;
        memcpy(&valid, e, 4);
        memcpy(&system, e + 4, 4);
        memcpy(&id, e + 8, 2);
        memcpy(&mask, e + 15, 1);
        memcpy(&elevation, e + 16, 4);
        memcpy(&azimuth, e + 20, 4);
        memcpy(&snr, e + 24, 4);
        if (!(valid & (1 << 0)) || !(valid & (1 << 1))) continue;
        GnssSvInfo sv;
        if (!svOf(system, id, &sv.constellation, &sv.svid)) continue;
        sv.cN0Dbhz = valid & (1 << 7) ? snr : 0;
        sv.elevationDegrees = valid & (1 << 5) ? elevation : 0;
        sv.azimuthDegrees = valid & (1 << 6) ? azimuth : 0;
        sv.svFlag = 0;
        if (valid & (1 << 4)) {
            if (mask & 0x01) sv.svFlag |= static_cast<int>(IGnssCallback::GnssSvFlags::HAS_EPHEMERIS_DATA);
            if (mask & 0x02) sv.svFlag |= static_cast<int>(IGnssCallback::GnssSvFlags::HAS_ALMANAC_DATA);
        }
        if (used.count(id)) sv.svFlag |= static_cast<int>(IGnssCallback::GnssSvFlags::USED_IN_FIX);
        svs.push_back(sv);
    }
    cb->gnssSvStatusCb(svs);
}

void Gnss::reportNmea(const Tlvs& tlvs) {
    auto t = tlvs.find(0x01);
    if (t == tlvs.end()) return;
    std::shared_ptr<IGnssCallback> cb;
    {
        std::lock_guard<std::mutex> lock(mLock);
        mNmeaReports++;
        if (!mStarted || !mNmea || !mCallback) return;
        cb = mCallback;
    }
    const std::string text = t->second.substr(0, t->second.find('\0'));
    const int64_t now = nowUtcMs();
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && (line[0] == '$' || line[0] == '!')) cb->gnssNmeaCb(now, line);
    }
}

binder_status_t Gnss::dump(int fd, const char**, uint32_t) {
    std::lock_guard<std::mutex> lock(mLock);
    // Counts only: no position here
    dprintf(fd,
            "modem location service: %s\nengine: %s\nframework session: %s, engine session: %s, "
            "every %d ms\nsatellite status %s, NMEA %s\nfixes %u, satellite reports %u, NMEA "
            "reports %u, time requests %u\n",
            mLoc.connected() ? "up" : "not there", mEngineOn ? "on" : "off",
            mStarted ? "on" : "off", mSession ? "on" : "off", mIntervalMs, mSvStatus ? "on" : "off",
            mNmea ? "on" : "off", mFixes, mSvReports, mNmeaReports, mTimeRequests);
    return STATUS_OK;
}

}  // namespace aidl::android::hardware::gnss::gts3
