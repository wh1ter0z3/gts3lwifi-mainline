/*
 * Copyright (C) 2026 The gts3lwifi-mainline project
 * SPDX-License-Identifier: Apache-2.0
 *
 * A QMI LOC client: the modem DSP's location service (QRTR service 16,
 * version 2), found through the kernel's QRTR name service, on one socket.
 * Message and TLV numbers: libqmi's data/qmi-service-loc.json.
 */
#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace aidl::android::hardware::gnss::gts3 {

// Requests (and their responses) and indications
enum : uint16_t {
    LOC_REG_EVENTS = 0x21,
    LOC_START = 0x22,
    LOC_STOP = 0x23,
    LOC_POSITION_REPORT_IND = 0x24,
    LOC_GNSS_SV_INFO_IND = 0x25,
    LOC_NMEA_IND = 0x26,
    LOC_INJECT_TIME_REQ_IND = 0x28,
    LOC_INJECT_POSITION_REQ_IND = 0x2a,
    LOC_ENGINE_STATE_IND = 0x2b,
    LOC_FIX_SESSION_STATE_IND = 0x2c,
    LOC_INJECT_UTC_TIME = 0x38,
    LOC_INJECT_POSITION = 0x39,
    LOC_DELETE_ASSIST_DATA = 0x44,
    LOC_SET_OPERATION_MODE = 0x4a,
};

// A message's TLVs by type
using Tlvs = std::map<uint8_t, std::string>;
using TlvList = std::vector<std::pair<uint8_t, std::string>>;

template <typename T>
std::string tlvOf(const T& v) {
    return std::string(reinterpret_cast<const char*>(&v), sizeof(v));
}

template <typename T>
bool tlvGet(const Tlvs& tlvs, uint8_t type, T* out) {
    auto i = tlvs.find(type);
    if (i == tlvs.end() || i->second.size() < sizeof(T)) return false;
    memcpy(out, i->second.data(), sizeof(T));
    return true;
}

class QmiLoc {
  public:
    class Listener {
      public:
        virtual ~Listener() = default;
        // The service appeared (the modem runs) or went away
        virtual void onService(bool up) = 0;
        virtual void onIndication(uint16_t msgId, const Tlvs& tlvs) = 0;
    };

    explicit QmiLoc(Listener* listener) : mListener(listener) {}
    ~QmiLoc();

    // Opens the socket, asks for the service and starts the reader thread
    bool start();
    // false while the service isn't there
    bool request(uint16_t msgId, const TlvList& tlvs);
    bool connected();

  private:
    void run();
    void handleControl(const uint8_t* data, size_t len);
    void handleMessage(const uint8_t* data, size_t len);

    Listener* mListener;
    int mFd = -1;
    int mStopFd = -1;
    uint32_t mLocalNode = 0;
    std::mutex mLock;  // the service address and the transaction counter
    bool mUp = false;
    uint32_t mNode = 0;
    uint32_t mPort = 0;
    uint16_t mTxn = 0;
    std::thread mThread;
};

}  // namespace aidl::android::hardware::gnss::gts3
