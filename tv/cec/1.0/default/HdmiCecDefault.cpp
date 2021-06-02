/*
 * Copyright (C) 2021 The Android Open Source Project
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

#define LOG_TAG "android.hardware.tv.cec@1.0-impl"
#include <android-base/logging.h>

#include <cutils/properties.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/cec.h>
#include <linux/ioctl.h>
#include <sys/eventfd.h>

#include "HdmiCecDefault.h"

namespace android {
namespace hardware {
namespace tv {
namespace cec {
namespace V1_0 {
namespace implementation {

int mCecFd;
int mExitFd;
sp<IHdmiCecCallback> mCallback;

HdmiCecDefault::HdmiCecDefault() {
    mCecFd = -1;
    mExitFd = -1;
    mCallback = nullptr;
}

HdmiCecDefault::~HdmiCecDefault() {
    release();
}

// Methods from ::android::hardware::tv::cec::V1_0::IHdmiCec follow.
Return<Result> HdmiCecDefault::addLogicalAddress(CecLogicalAddress addr) {
    if (addr < CecLogicalAddress::TV || addr >= CecLogicalAddress::BROADCAST) {
        LOG(ERROR) << "Add logical address failed, Invalid address";
        return Result::FAILURE_INVALID_ARGS;
    }

    struct cec_log_addrs cecLogAddrs;
    int ret = ioctl(mCecFd, CEC_ADAP_G_LOG_ADDRS, &cecLogAddrs);
    if (ret) {
        LOG(ERROR) << "Add logical address failed, Error = " << strerror(errno);
        return Result::FAILURE_BUSY;
    }

    cecLogAddrs.cec_version = getCecVersion();
    cecLogAddrs.vendor_id = getVendorId();

    unsigned int logAddrType = CEC_LOG_ADDR_TYPE_UNREGISTERED;
    unsigned int allDevTypes = 0;
    unsigned int primDevType = 0xff;
    switch (addr) {
        case CecLogicalAddress::TV:
            primDevType = CEC_OP_PRIM_DEVTYPE_TV;
            logAddrType = CEC_LOG_ADDR_TYPE_TV;
            allDevTypes = CEC_OP_ALL_DEVTYPE_TV;
            break;
        case CecLogicalAddress::RECORDER_1:
        case CecLogicalAddress::RECORDER_2:
        case CecLogicalAddress::RECORDER_3:
            primDevType = CEC_OP_PRIM_DEVTYPE_RECORD;
            logAddrType = CEC_LOG_ADDR_TYPE_RECORD;
            allDevTypes = CEC_OP_ALL_DEVTYPE_RECORD;
            break;
        case CecLogicalAddress::TUNER_1:
        case CecLogicalAddress::TUNER_2:
        case CecLogicalAddress::TUNER_3:
        case CecLogicalAddress::TUNER_4:
            primDevType = CEC_OP_PRIM_DEVTYPE_TUNER;
            logAddrType = CEC_LOG_ADDR_TYPE_TUNER;
            allDevTypes = CEC_OP_ALL_DEVTYPE_TUNER;
            break;
        case CecLogicalAddress::PLAYBACK_1:
        case CecLogicalAddress::PLAYBACK_2:
        case CecLogicalAddress::PLAYBACK_3:
            primDevType = CEC_OP_PRIM_DEVTYPE_PLAYBACK;
            logAddrType = CEC_LOG_ADDR_TYPE_PLAYBACK;
            allDevTypes = CEC_OP_ALL_DEVTYPE_PLAYBACK;
            cecLogAddrs.flags |= CEC_LOG_ADDRS_FL_ALLOW_RC_PASSTHRU;
            break;
        case CecLogicalAddress::AUDIO_SYSTEM:
            primDevType = CEC_OP_PRIM_DEVTYPE_AUDIOSYSTEM;
            logAddrType = CEC_LOG_ADDR_TYPE_AUDIOSYSTEM;
            allDevTypes = CEC_OP_ALL_DEVTYPE_AUDIOSYSTEM;
            break;
        case CecLogicalAddress::FREE_USE:
            primDevType = CEC_OP_PRIM_DEVTYPE_PROCESSOR;
            logAddrType = CEC_LOG_ADDR_TYPE_SPECIFIC;
            allDevTypes = CEC_OP_ALL_DEVTYPE_SWITCH;
            break;
        case CecLogicalAddress::UNREGISTERED:
            cecLogAddrs.flags |= CEC_LOG_ADDRS_FL_ALLOW_UNREG_FALLBACK;
            break;
    }

    int logAddrIndex = cecLogAddrs.num_log_addrs;

    cecLogAddrs.num_log_addrs += 1;
    cecLogAddrs.log_addr[logAddrIndex] = static_cast<cec_logical_address_t>(addr);
    cecLogAddrs.log_addr_type[logAddrIndex] = logAddrType;
    cecLogAddrs.primary_device_type[logAddrIndex] = primDevType;
    cecLogAddrs.all_device_types[logAddrIndex] = allDevTypes;
    cecLogAddrs.features[logAddrIndex][0] = 0;
    cecLogAddrs.features[logAddrIndex][1] = 0;

    ret = ioctl(mCecFd, CEC_ADAP_S_LOG_ADDRS, &cecLogAddrs);
    if (ret) {
        LOG(ERROR) << "Add logical address failed, Error = " << strerror(errno);
        return Result::FAILURE_BUSY;
    }
    return Result::SUCCESS;
}

Return<void> HdmiCecDefault::clearLogicalAddress() {
    struct cec_log_addrs cecLogAddrs;
    memset(&cecLogAddrs, 0, sizeof(cecLogAddrs));
    int ret = ioctl(mCecFd, CEC_ADAP_S_LOG_ADDRS, &cecLogAddrs);
    if (ret) {
        LOG(ERROR) << "Clear logical Address failed, Error = " << strerror(errno);
    }
    return Void();
}

Return<void> HdmiCecDefault::getPhysicalAddress(getPhysicalAddress_cb callback) {
    uint16_t addr;
    int ret = ioctl(mCecFd, CEC_ADAP_G_PHYS_ADDR, &addr);
    if (ret) {
        LOG(ERROR) << "Get physical address failed, Error = " << strerror(errno);
        callback(Result::FAILURE_INVALID_STATE, addr);
        return Void();
    }
    callback(Result::SUCCESS, addr);
    return Void();
}

Return<SendMessageResult> HdmiCecDefault::sendMessage(const CecMessage& message) {
    struct cec_msg cecMsg;
    memset(&cecMsg, 0, sizeof(cec_msg));

    int initiator = static_cast<cec_logical_address_t>(message.initiator);
    int destination = static_cast<cec_logical_address_t>(message.destination);

    cecMsg.msg[0] = (initiator << 4) | destination;
    for (size_t i = 0; i < message.body.size(); ++i) {
        cecMsg.msg[i + 1] = message.body[i];
    }
    cecMsg.len = message.body.size() + 1;

    int ret = ioctl(mCecFd, CEC_TRANSMIT, &cecMsg);

    if (ret) {
        LOG(ERROR) << "Send message failed, Error = " << strerror(errno);
        return SendMessageResult::FAIL;
    }

    if (cecMsg.tx_status != CEC_TX_STATUS_OK) {
        LOG(ERROR) << "Send message tx_status = " << cecMsg.tx_status;
    }

    switch (cecMsg.tx_status) {
        case CEC_TX_STATUS_OK:
            return SendMessageResult::SUCCESS;
        case CEC_TX_STATUS_ARB_LOST:
            return SendMessageResult::BUSY;
        case CEC_TX_STATUS_NACK:
            return SendMessageResult::NACK;
        default:
            return SendMessageResult::FAIL;
    }
}

Return<void> HdmiCecDefault::setCallback(const sp<IHdmiCecCallback>& callback) {
    if (mCallback != nullptr) {
        mCallback->unlinkToDeath(this);
        mCallback = nullptr;
    }

    if (callback != nullptr) {
        mCallback = callback;
        mCallback->linkToDeath(this, 0 /*cookie*/);
    }
    return Void();
}

Return<int32_t> HdmiCecDefault::getCecVersion() {
    return property_get_int32("ro.hdmi.cec_version", CEC_OP_CEC_VERSION_1_4);
}

Return<uint32_t> HdmiCecDefault::getVendorId() {
    return property_get_int32("ro.hdmi.vendor_id", 0x000c03 /* HDMI LLC vendor ID */);
}

Return<void> HdmiCecDefault::getPortInfo(getPortInfo_cb callback) {
    uint16_t addr;
    int ret = ioctl(mCecFd, CEC_ADAP_G_PHYS_ADDR, &addr);
    if (ret) {
        LOG(ERROR) << "Get port info failed, Error = " << strerror(errno);
    }

    unsigned int type = property_get_int32("ro.hdmi.device_type", CEC_DEVICE_PLAYBACK);
    hidl_vec<HdmiPortInfo> portInfos(1);
    portInfos[0] = {.type = (type == CEC_DEVICE_TV ? HdmiPortType::INPUT : HdmiPortType::OUTPUT),
                    .portId = 1,
                    .cecSupported = true,
                    .arcSupported = false,
                    .physicalAddress = addr};
    callback(portInfos);
    return Void();
}

Return<void> HdmiCecDefault::setOption(OptionKey /*key*/, bool /*value*/) {
    return Void();
}

Return<void> HdmiCecDefault::setLanguage(const hidl_string& /*language*/) {
    return Void();
}

Return<void> HdmiCecDefault::enableAudioReturnChannel(int32_t /*portId*/, bool /*enable*/) {
    return Void();
}

Return<bool> HdmiCecDefault::isConnected(int32_t /*portId*/) {
    uint16_t addr;
    int ret = ioctl(mCecFd, CEC_ADAP_G_PHYS_ADDR, &addr);
    if (ret) {
        LOG(ERROR) << "Is connected failed, Error = " << strerror(errno);
        return false;
    }
    if (addr == CEC_PHYS_ADDR_INVALID) {
        return false;
    }
    return true;
}

// Initialise the cec file descriptor
Return<Result> HdmiCecDefault::init() {
    const char* path = "/dev/cec0";
    mCecFd = open(path, O_RDWR);
    if (mCecFd < 0) {
        LOG(ERROR) << "Failed to open " << path << ", Error = " << strerror(errno);
        return Result::FAILURE_NOT_SUPPORTED;
    }
    mExitFd = eventfd(0, EFD_NONBLOCK);
    if (mExitFd < 0) {
        LOG(ERROR) << "Failed to open eventfd, Error = " << strerror(errno);
        release();
        return Result::FAILURE_NOT_SUPPORTED;
    }

    // Ensure the CEC device supports required capabilities
    struct cec_caps caps = {};
    int ret = ioctl(mCecFd, CEC_ADAP_G_CAPS, &caps);
    if (ret) {
        LOG(ERROR) << "Unable to query cec adapter capabilities, Error = " << strerror(errno);
        release();
        return Result::FAILURE_NOT_SUPPORTED;
    }

    if (!(caps.capabilities & (CEC_CAP_LOG_ADDRS | CEC_CAP_TRANSMIT | CEC_CAP_PASSTHROUGH))) {
        LOG(ERROR) << "Wrong cec adapter capabilities " << caps.capabilities;
        release();
        return Result::FAILURE_NOT_SUPPORTED;
    }

    uint32_t mode = CEC_MODE_INITIATOR;
    ret = ioctl(mCecFd, CEC_S_MODE, &mode);
    if (ret) {
        LOG(ERROR) << "Unable to set initiator mode, Error = " << strerror(errno);
        release();
        return Result::FAILURE_NOT_SUPPORTED;
    }

    return Result::SUCCESS;
}

Return<void> HdmiCecDefault::release() {
    if (mExitFd > 0) {
        uint64_t tmp = 1;
        write(mExitFd, &tmp, sizeof(tmp));
    }
    if (mExitFd > 0) {
        close(mExitFd);
    }
    if (mCecFd > 0) {
        close(mCecFd);
    }
    setCallback(nullptr);
    return Void();
}
}  // namespace implementation
}  // namespace V1_0
}  // namespace cec
}  // namespace tv
}  // namespace hardware
}  // namespace android
