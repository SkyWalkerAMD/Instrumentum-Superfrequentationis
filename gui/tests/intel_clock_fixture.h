// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "intel_memory_fixture.h"
#include "../core/intel_clock.h"

namespace intel_clock_fixture {
inline std::vector<octool::core::IntelClockRegister> registers() {
    const std::uint32_t offsets[] = {0x5e00,0x5e04,0x5f60};
    const std::uint64_t words[] = {0x00140017,0x49cc1120,101250};
    std::vector<octool::core::IntelClockRegister> result;
    for (unsigned i = 0; i < 3; ++i) { octool::core::IntelClockRegister r; r.offset = offsets[i]; r.raw = words[i]; result.push_back(r); }
    return result;
}
struct Device : intel_memory_fixture::Device {
    std::map<std::uint32_t,std::uint64_t> clock;
    Device() { for (const auto &r : registers()) clock[r.offset] = r.raw; }
    octool::core::Reply execute(const octool::core::Request &r) override {
        using namespace octool::core;
        if (r.space != Space::Memory) return intel_memory_fixture::Device::execute(r);
        requests.push_back(r); Reply out;
        if (r.write) { ++writes; out.error = -EPERM; return out; }
        if (int(reads++) == failRead) { out.error = -EACCES; out.value = UINT64_MAX; return out; }
        bool found = false;
        for (const auto &spec : intelClockRegisters()) {
            if (r.address == (bar & ~UINT64_C(0x1ffff))+spec.offset && r.width == spec.width) { out.value = clock[spec.offset]; found = true; break; }
        }
        if (!found) out.error = -EFAULT;
        if (afterRequest) afterRequest(r);
        return out;
    }
};
}
