// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../core/hardware.h"
#include <cerrno>
#include <functional>
#include <map>
#include <vector>

// Synthetic ordinary MSRs. No OS devices or host CPUID are used.
struct IntelControlsFixture : octool::core::HardwareBackend {
    bool amd = false, hwp = true, epp = true, activityWindow = true, discardChange = false;
    unsigned model = 0xb7, expectedCpu = UINT32_MAX, wrongCpu = 0;
    int failRead = -1, failWrite = -1, failCpuid = -1;
    unsigned reads = 0, writes = 0, cpuCalls = 0;
    std::vector<octool::core::Request> requests;
    std::map<std::uint64_t, std::uint64_t> regs;
    std::function<void(const octool::core::Request &)> afterRequest;
    IntelControlsFixture() {
        regs[0x606] = UINT64_C(0xa0e03); // 1/8 W, 1/1024 s.
        regs[0x610] = UINT64_C(0x0800000000000000) | (UINT64_C(2000) << 32) |
            (UINT64_C(1) << 47) | 1000 | (1 << 15);
        regs[0x770] = 1; regs[0x771] = 0x08203040;
        regs[0x774] = UINT64_C(0x8000100080004008);
    }
    void checkCpu(unsigned cpu) { if (expectedCpu != UINT32_MAX && cpu != expectedCpu) ++wrongCpu; }
    octool::core::Reply execute(const octool::core::Request &r) override {
        using namespace octool::core;
        checkCpu(r.cpu); requests.push_back(r); Reply out;
        if (r.space != Space::Msr || r.width != 8) { out.error = -EINVAL; return out; }
        if (r.write) {
            if (int(writes++) == failWrite) out.error = -EIO;
            else if (!discardChange) regs[r.address] = r.value;
        } else {
            if (int(reads++) == failRead) { out.error = -EACCES; out.value = UINT64_MAX; }
            else out.value = regs[r.address];
        }
        if (afterRequest) afterRequest(r);
        return out;
    }
    octool::core::CpuIdReply cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t) override {
        checkCpu(cpu); octool::core::CpuIdReply r;
        if (int(cpuCalls++) == failCpuid) { r.error = -EACCES; return r; }
        if (!leaf) {
            r.words[0] = 6; r.words[1] = amd ? 0x68747541 : 0x756e6547;
            r.words[3] = amd ? 0x69746e65 : 0x49656e69; r.words[2] = amd ? 0x444d4163 : 0x6c65746e;
        }
        if (leaf == 1) r.words[0] = amd ? 0x00b00f20 : 0x600 | ((model & 15) << 4) | ((model & 240) << 12);
        if (leaf == 6) r.words[0] = (hwp ? 1u << 7 : 0) | (epp ? 1u << 10 : 0) | (activityWindow ? 1u << 9 : 0);
        return r;
    }
    octool::core::Backend backend(octool::core::Space) const override { return octool::core::Backend::Module; }
};
