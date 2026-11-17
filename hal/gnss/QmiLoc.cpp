/*
 * Copyright (C) 2026 The gts3lwifi-mainline project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "gts3-gnss"

#include "QmiLoc.h"

#include <linux/qrtr.h>
#include <log/log.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>

namespace aidl::android::hardware::gnss::gts3 {

static constexpr uint32_t kLocService = 16;
static constexpr uint32_t kLocVersion = 2;
static constexpr uint8_t kQmiRequest = 0, kQmiResponse = 2, kQmiIndication = 4;
static constexpr size_t kQmiHeader = 7;  // type, transaction, message, length

QmiLoc::~QmiLoc() {
    if (mStopFd >= 0) {
        uint64_t one = 1;
        (void)!write(mStopFd, &one, sizeof(one));
    }
    if (mThread.joinable()) mThread.join();
    if (mFd >= 0) close(mFd);
    if (mStopFd >= 0) close(mStopFd);
}

bool QmiLoc::start() {
    mFd = socket(AF_QIPCRTR, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (mFd < 0) {
        ALOGE("no QRTR socket: %s", strerror(errno));
        return false;
    }
    sockaddr_qrtr sq{};
    socklen_t len = sizeof(sq);
    if (getsockname(mFd, reinterpret_cast<sockaddr*>(&sq), &len) < 0) {
        ALOGE("getsockname: %s", strerror(errno));
        return false;
    }
    mLocalNode = sq.sq_node;
    mStopFd = eventfd(0, EFD_CLOEXEC);

    // The name service reports the service now and whenever it comes and goes
    qrtr_ctrl_pkt pkt{};
    pkt.cmd = QRTR_TYPE_NEW_LOOKUP;
    pkt.server.service = kLocService;
    sockaddr_qrtr ns{.sq_family = AF_QIPCRTR, .sq_node = mLocalNode, .sq_port = QRTR_PORT_CTRL};
    if (sendto(mFd, &pkt, sizeof(pkt), 0, reinterpret_cast<sockaddr*>(&ns), sizeof(ns)) < 0) {
        ALOGE("QRTR lookup: %s", strerror(errno));
        return false;
    }
    mThread = std::thread([this] { run(); });
    return true;
}

bool QmiLoc::connected() {
    std::lock_guard<std::mutex> lock(mLock);
    return mUp;
}

bool QmiLoc::request(uint16_t msgId, const TlvList& tlvs) {
    std::string body;
    for (const auto& [type, value] : tlvs) {
        const uint16_t n = value.size();
        body += static_cast<char>(type);
        body += tlvOf(n);
        body += value;
    }
    std::lock_guard<std::mutex> lock(mLock);
    if (!mUp) return false;
    mTxn = mTxn % 0xffff + 1;
    const uint16_t length = body.size();
    std::string msg = std::string(1, static_cast<char>(kQmiRequest)) + tlvOf(mTxn) + tlvOf(msgId) +
                      tlvOf(length) + body;
    sockaddr_qrtr to{.sq_family = AF_QIPCRTR, .sq_node = mNode, .sq_port = mPort};
    if (sendto(mFd, msg.data(), msg.size(), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to)) < 0) {
        ALOGE("request 0x%02x: %s", msgId, strerror(errno));
        return false;
    }
    return true;
}

void QmiLoc::run() {
    std::vector<uint8_t> buf(65536);
    for (;;) {
        pollfd fds[2] = {{.fd = mFd, .events = POLLIN}, {.fd = mStopFd, .events = POLLIN}};
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            ALOGE("poll: %s", strerror(errno));
            return;
        }
        if (fds[1].revents) return;
        if (!(fds[0].revents & POLLIN)) continue;
        sockaddr_qrtr from{};
        socklen_t len = sizeof(from);
        const ssize_t n = recvfrom(mFd, buf.data(), buf.size(), 0,
                                   reinterpret_cast<sockaddr*>(&from), &len);
        if (n < 0) {
            if (errno != EINTR && errno != EAGAIN) ALOGE("recvfrom: %s", strerror(errno));
            continue;
        }
        if (from.sq_node == mLocalNode && from.sq_port == QRTR_PORT_CTRL) {
            handleControl(buf.data(), n);
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(mLock);
            if (!mUp || from.sq_node != mNode || from.sq_port != mPort) continue;
        }
        handleMessage(buf.data(), n);
    }
}

void QmiLoc::handleControl(const uint8_t* data, size_t len) {
    if (len < sizeof(qrtr_ctrl_pkt)) return;
    qrtr_ctrl_pkt pkt;
    memcpy(&pkt, data, sizeof(pkt));
    const uint32_t service = pkt.server.service, version = pkt.server.instance & 0xff;
    bool changed = false, up = false;
    if (pkt.cmd == QRTR_TYPE_NEW_SERVER && service == kLocService) {
        if (version != kLocVersion) {
            ALOGW("location service version %u, not %u: ignored", version, kLocVersion);
            return;
        }
        std::lock_guard<std::mutex> lock(mLock);
        if (!mUp) {
            mUp = changed = up = true;
            mNode = pkt.server.node;
            mPort = pkt.server.port;
            ALOGI("location service on QRTR node %u port %u", mNode, mPort);
        }
    } else if (pkt.cmd == QRTR_TYPE_DEL_SERVER && service == kLocService) {
        std::lock_guard<std::mutex> lock(mLock);
        if (mUp && pkt.server.node == mNode && pkt.server.port == mPort) {
            mUp = false;
            changed = true;
            ALOGI("location service gone (modem stopped?)");
        }
    } else if (pkt.cmd == QRTR_TYPE_NEW_SERVER && service == 0) {
        // The end of the listing
        if (!connected()) ALOGI("no location service yet (is the modem running?)");
    }
    if (changed) mListener->onService(up);
}

void QmiLoc::handleMessage(const uint8_t* data, size_t len) {
    if (len < kQmiHeader) return;
    const uint8_t type = data[0];
    uint16_t msgId, length;
    memcpy(&msgId, data + 3, 2);
    memcpy(&length, data + 5, 2);
    const size_t end = std::min(len, kQmiHeader + length);
    Tlvs tlvs;
    for (size_t i = kQmiHeader; i + 3 <= end;) {
        uint16_t n;
        memcpy(&n, data + i + 1, 2);
        if (i + 3 + n > end) break;
        tlvs[data[i]] = std::string(reinterpret_cast<const char*>(data + i + 3), n);
        i += 3 + n;
    }
    if (type == kQmiResponse) {
        uint16_t result[2];
        if (tlvGet(tlvs, 0x02, &result) && result[0])
            ALOGW("request 0x%02x failed: error %u", msgId, result[1]);
    } else if (type == kQmiIndication) {
        mListener->onIndication(msgId, tlvs);
    }
}

}  // namespace aidl::android::hardware::gnss::gts3
