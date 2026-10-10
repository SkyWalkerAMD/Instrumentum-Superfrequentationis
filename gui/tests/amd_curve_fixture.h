// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../core/hardware.h"
#include <cerrno>
#include <functional>
#include <vector>

// Synthetic query mailbox. An unexpected space, address, command or target
// fails instead of accidentally accepting a BIOS/MP1 tuning command.
struct AmdCurveFixture : octool::core::HardwareBackend {
    bool amd = true;
    unsigned family = 0x1a, expectedCpu = 130, wrongCpu = 0, calls = 0;
    unsigned commands = 0, argumentReads = 0, polls = 0, busyPolls = 0;
    std::uint32_t pciId = 0x153a1022, index = 0, response = 1, completion = 1;
    std::uint32_t argument = 0, submitted = 0, returned = 0xffffffe2;
    int failAt = -1;
    bool busyBefore = false;
    std::vector<octool::core::Request> requests;
    std::function<void()> afterRequest;
    bool failing() { return int(calls++) == failAt; }
    octool::core::Reply execute(const octool::core::Request &r) override {
        using namespace octool::core;
        requests.push_back(r); Reply out;
        if (failing()) { out.error = -EACCES; out.value = UINT64_MAX; return out; }
        if (r.space != Space::Pci || r.width != 4 || r.bus || r.device || r.function) out.error = -EINVAL;
        else if (!r.write && r.address == 0) out.value = pciId;
        else if (r.write && r.address == 0xb8) {
            if (r.value != 0x3b10970 && r.value != 0x3b10924 && r.value != 0x3b10a40) out.error = -EINVAL;
            else index = std::uint32_t(r.value);
        } else if (r.address != 0xbc) out.error = -EINVAL;
        else if (r.write) {
            if (index == 0x3b10970 && !r.value) response = 0;
            else if (index == 0x3b10a40) submitted = argument = std::uint32_t(r.value);
            else if (index == 0x3b10924 && r.value == 0xa3) {
                ++commands; polls = 0; response = completion;
                if (completion == 1) argument = returned;
            } else out.error = -EINVAL;
        } else if (index == 0x3b10970) {
            ++polls;
            out.value = (!commands && busyBefore) || (commands && polls <= busyPolls) ? 0 : response;
        } else if (index == 0x3b10a40) { ++argumentReads; out.value = argument; }
        else out.error = -EINVAL;
        if (afterRequest) afterRequest();
        return out;
    }
    octool::core::CpuIdReply cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t) override {
        octool::core::CpuIdReply out;
        if (cpu != expectedCpu) ++wrongCpu;
        if (failing()) { out.error = -EACCES; return out; }
        if (!leaf) {
            out.words[0] = 1; out.words[1] = amd ? 0x68747541 : 0x756e6547;
            out.words[2] = amd ? 0x444d4163 : 0x6c65746e; out.words[3] = amd ? 0x69746e65 : 0x49656e69;
        } else if (leaf == 1) out.words[0] = ((family - 15) << 20) | 0xf80;
        else out.error = -EINVAL;
        if (afterRequest) afterRequest();
        return out;
    }
    octool::core::Backend backend(octool::core::Space) const override { return octool::core::Backend::Module; }
};
