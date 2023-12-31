/*
 * Copyright 2021 The Android Open Source Project
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

#include "DemuxTests.h"

AssertionResult DemuxTests::getDemuxIds(std::vector<int32_t>& demuxIds) {
    ndk::ScopedAStatus status;
    status = mService->getDemuxIds(&demuxIds);
    return AssertionResult(status.isOk());
}

AssertionResult DemuxTests::openDemux(std::shared_ptr<IDemux>& demux, int32_t& demuxId) {
    std::vector<int32_t> id;
    auto status = mService->openDemux(&id, &mDemux);
    if (status.isOk()) {
        demux = mDemux;
        demuxId = id[0];
    }
    return AssertionResult(status.isOk());
}

AssertionResult DemuxTests::openDemuxById(int32_t demuxId, std::shared_ptr<IDemux>& demux) {
    auto status = mService->openDemuxById(demuxId, &mDemux);
    if (status.isOk()) {
        demux = mDemux;
    }
    return AssertionResult(status.isOk());
}

AssertionResult DemuxTests::setDemuxFrontendDataSource(int32_t frontendId) {
    EXPECT_TRUE(mDemux) << "Test with openDemux first.";
    auto status = mDemux->setFrontendDataSource(frontendId);
    return AssertionResult(status.isOk());
}

AssertionResult DemuxTests::getDemuxCaps(DemuxCapabilities& demuxCaps) {
    auto status = mService->getDemuxCaps(&demuxCaps);
    return AssertionResult(status.isOk());
}

AssertionResult DemuxTests::getDemuxInfo(int32_t demuxId, DemuxInfo& demuxInfo) {
    auto status = mService->getDemuxInfo(demuxId, &demuxInfo);
    return AssertionResult(status.isOk());
}

AssertionResult DemuxTests::getAvSyncId(std::shared_ptr<IFilter> filter, int32_t& avSyncHwId) {
    EXPECT_TRUE(mDemux) << "Demux is not opened yet.";

    auto status = mDemux->getAvSyncHwId(filter, &avSyncHwId);
    return AssertionResult(status.isOk());
}

AssertionResult DemuxTests::getAvSyncTime(int32_t avSyncId) {
    EXPECT_TRUE(mDemux) << "Demux is not opened yet.";

    int64_t syncTime;
    auto status = mDemux->getAvSyncTime(avSyncId, &syncTime);
    return AssertionResult(status.isOk());
}

AssertionResult DemuxTests::closeDemux() {
    EXPECT_TRUE(mDemux) << "Test with openDemux first.";
    auto status = mDemux->close();
    mDemux = nullptr;
    return AssertionResult(status.isOk());
}
