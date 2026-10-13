// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../core/hardware.h"
#include <cerrno>
#include <functional>
#include <vector>

// Synthetic firmware only. Query and settings commands are counted separately.
struct IntelBclkVoltageFixture : octool::core::HardwareBackend {
    bool amd = false, msr = true, hypervisor = false, locked = false, discardChange = false;
    bool busyBefore = false;
    unsigned maximum = 0x20;
    unsigned model = 0xb7, stepping = 1, expectedCpu = 130, wrongCpu = 0;
    unsigned calls = 0, mutations = 0, busyCommand = 0, failCommand = 0, status = 0;
    int failAt = -1;
    std::uint32_t data = 0xa5c32110;
    std::uint64_t mailbox = 0, flexExtra = UINT64_C(1) << 63;
    std::vector<octool::core::Request> requests;
    std::vector<unsigned> commands;
    std::function<void()> afterRequest;
    bool failing(unsigned cpu) {
        if (cpu != expectedCpu) ++wrongCpu;
        return int(calls++) == failAt;
    }
    unsigned settingsAttempts() const {
        unsigned n = 0;
        for (const auto &r : requests) if (r.write && ((r.value >> 32) & 255) == 0x15) ++n;
        return n;
    }
    octool::core::Reply execute(const octool::core::Request &r) override {
        using namespace octool::core;
        requests.push_back(r); Reply out;
        if (failing(r.cpu)) { out.error = -EACCES; out.value = UINT64_MAX; return out; }
        if (r.space != Space::Msr || r.width != 8) { out.error = -EINVAL; return out; }
        if (r.address == 0x194 && !r.write) out.value = flexExtra | (locked ? UINT64_C(1) << 20 : 0);
        else if (r.address == 0x150 && !r.write) {
            out.value = mailbox;
            if ((busyBefore && commands.empty()) ||
                (busyCommand && !commands.empty() && commands.back() == busyCommand)) out.value |= UINT64_C(1) << 63;
        } else if (r.address == 0x150 && r.write) {
            const unsigned command = unsigned((r.value >> 32) & 255);
            commands.push_back(command);
            if ((command != 0x14 && command != 0x15) ||
                (r.value >> 32) != (UINT64_C(0x80000000) | command) ||
                (command == 0x14 && std::uint32_t(r.value))) { out.error = -EINVAL; return out; }
            const unsigned response = command == failCommand ? status : 0;
            if (command == 0x15 && !response) {
                ++mutations;
                if (!discardChange) data = std::uint32_t(r.value);
            }
            mailbox = (std::uint64_t(response) << 32) | data;
        } else out.error = -EINVAL;
        if (afterRequest) afterRequest();
        return out;
    }
    octool::core::CpuIdReply cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t) override {
        octool::core::CpuIdReply out;
        if (failing(cpu)) { out.error = -EACCES; out.words[0] = UINT32_MAX; return out; }
        if (leaf == 0) {
            out.words[0] = maximum; out.words[1] = amd ? 0x68747541 : 0x756e6547;
            out.words[2] = amd ? 0x444d4163 : 0x6c65746e; out.words[3] = amd ? 0x69746e65 : 0x49656e69;
        } else if (leaf == 1) {
            out.words[0] = 0x600 | ((model & 15) << 4) | ((model & 240) << 12) | stepping;
            out.words[2] = (hypervisor ? 1u << 31 : 0);
            out.words[3] = msr ? 1u << 5 : 0;
        }
        else out.error = -EINVAL;
        return out;
    }
    octool::core::Backend backend(octool::core::Space) const override { return octool::core::Backend::Module; }
    void clearTrace() { calls = 0; failAt = -1; requests.clear(); commands.clear(); }
};
