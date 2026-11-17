/*
 * Copyright (C) 2026 The gts3lwifi-mainline project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <aidl/android/hardware/gnss/BnGnss.h>
#include <aidl/android/hardware/gnss/BnGnssConfiguration.h>

#include <memory>
#include <mutex>
#include <set>

#include "QmiLoc.h"

namespace aidl::android::hardware::gnss::gts3 {

// Accepts the framework's settings: they are for SUPL and the like, which a
// standalone engine doesn't use
class GnssConfiguration : public BnGnssConfiguration {
  public:
    ndk::ScopedAStatus setSuplVersion(int) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setSuplMode(int) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setLppProfile(int) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setGlonassPositioningProtocol(int) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus setEmergencySuplPdn(bool) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setEsExtensionSec(int) override { return ndk::ScopedAStatus::ok(); }
    ndk::ScopedAStatus setBlocklist(const std::vector<BlocklistedSource>&) override {
        return ndk::ScopedAStatus::ok();
    }
};

class Gnss : public BnGnss, public QmiLoc::Listener {
  public:
    Gnss();

    ndk::ScopedAStatus setCallback(const std::shared_ptr<IGnssCallback>& callback) override;
    ndk::ScopedAStatus close() override;
    ndk::ScopedAStatus start() override;
    ndk::ScopedAStatus stop() override;
    ndk::ScopedAStatus injectTime(int64_t timeMs, int64_t timeReferenceMs,
                                  int uncertaintyMs) override;
    ndk::ScopedAStatus injectLocation(const GnssLocation& location) override;
    ndk::ScopedAStatus injectBestLocation(const GnssLocation& location) override;
    ndk::ScopedAStatus deleteAidingData(IGnss::GnssAidingData aidingDataFlags) override;
    ndk::ScopedAStatus setPositionMode(const PositionModeOptions& options) override;
    ndk::ScopedAStatus startSvStatus() override;
    ndk::ScopedAStatus stopSvStatus() override;
    ndk::ScopedAStatus startNmea() override;
    ndk::ScopedAStatus stopNmea() override;

    ndk::ScopedAStatus getExtensionGnssConfiguration(
            std::shared_ptr<IGnssConfiguration>* _aidl_return) override;
    // Not there: the framework then goes without them
    ndk::ScopedAStatus getExtensionPsds(std::shared_ptr<IGnssPsds>* _aidl_return) override;
    ndk::ScopedAStatus getExtensionGnssMeasurement(
            std::shared_ptr<IGnssMeasurementInterface>* _aidl_return) override;
    ndk::ScopedAStatus getExtensionGnssPowerIndication(
            std::shared_ptr<IGnssPowerIndication>* _aidl_return) override;
    ndk::ScopedAStatus getExtensionGnssBatching(
            std::shared_ptr<IGnssBatching>* _aidl_return) override;
    ndk::ScopedAStatus getExtensionGnssGeofence(
            std::shared_ptr<IGnssGeofence>* _aidl_return) override;
    ndk::ScopedAStatus getExtensionGnssNavigationMessage(
            std::shared_ptr<IGnssNavigationMessageInterface>* _aidl_return) override;
    ndk::ScopedAStatus getExtensionAGnss(std::shared_ptr<IAGnss>* _aidl_return) override;
    ndk::ScopedAStatus getExtensionAGnssRil(std::shared_ptr<IAGnssRil>* _aidl_return) override;
    ndk::ScopedAStatus getExtensionGnssDebug(std::shared_ptr<IGnssDebug>* _aidl_return) override;
    ndk::ScopedAStatus getExtensionGnssVisibilityControl(
            std::shared_ptr<visibility_control::IGnssVisibilityControl>* _aidl_return) override;
    ndk::ScopedAStatus getExtensionGnssAntennaInfo(
            std::shared_ptr<IGnssAntennaInfo>* _aidl_return) override;
    ndk::ScopedAStatus getExtensionMeasurementCorrections(
            std::shared_ptr<measurement_corrections::IMeasurementCorrectionsInterface>*
                    _aidl_return) override;

    binder_status_t dump(int fd, const char** args, uint32_t numArgs) override;

  private:
    void onService(bool up) override;
    void onIndication(uint16_t msgId, const Tlvs& tlvs) override;

    void setupLocked();         // events and mode, once per service
    void startSessionLocked();  // the engine's session, as the framework asked
    void reportPosition(const Tlvs& tlvs);
    void reportSvInfo(const Tlvs& tlvs);
    void reportNmea(const Tlvs& tlvs);
    void injectPosition(const GnssLocation& location);
    std::shared_ptr<IGnssCallback> callback();

    QmiLoc mLoc;
    std::mutex mLock;
    std::shared_ptr<IGnssCallback> mCallback;
    bool mStarted = false;  // the framework wants fixes
    bool mSession = false;  // the engine has a session
    bool mSvStatus = true;
    bool mNmea = true;
    int mIntervalMs = 1000;
    std::set<uint16_t> mUsed;  // the satellites (QMI numbers) of the last fix
    // For dumpsys
    unsigned mFixes = 0, mSvReports = 0, mNmeaReports = 0, mTimeRequests = 0;
    bool mEngineOn = false;
};

}  // namespace aidl::android::hardware::gnss::gts3
