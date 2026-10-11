// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "intel_controls_fixture.h"
#include "../core/intel_memory.h"

namespace intel_memory_fixture {
inline std::vector<octool::core::IntelMemoryRegister> registers(unsigned type = 1) {
    const std::uint32_t offsets[] = {0xd800,0xe454,0xe088,0xe000,0xe008,0xe00c,0xe010,0xe014,0xe018,0xe070,0xe43c};
    // Independent synthetic words, including reserved bits and values >2^53.
    const std::uint64_t words[] = {type,3,UINT64_C(0x180000068),UINT64_C(0x814160500042402a),
        0x18041820,0x0f0e0d8c,0x13121110,0x14248e46,0x17969594,UINT64_C(0x9000000022240000),0x08c05dc0};
    std::vector<octool::core::IntelMemoryRegister> result;
    for (unsigned i = 0; i < 11; ++i) { octool::core::IntelMemoryRegister r; r.offset = offsets[i]; r.raw = words[i]; result.push_back(r); }
    return result;
}
inline unsigned expected(unsigned field) {
    const unsigned values[] = {36,40,42,88,34,80,18,4,32,12,8,24,560,24000,12,13,14,15,16,17,18,19,70,71,9,10,20,21,22,23};
    return values[field];
}
struct Device : IntelControlsFixture {
    unsigned controller = 0, channel = 0, dramType = 1;
    bool hypervisor = false;
    std::uint64_t bar = UINT64_C(0x12fedc0001);
    std::uint32_t pciId = 0xa7008086, pciClass = 0x06000001;
    std::map<std::uint32_t, std::uint64_t> memory;
    Device() { expectedCpu = 130; for (const auto &r : registers()) memory[r.offset] = r.raw; }
    octool::core::CpuIdReply cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t sub) override {
        auto r = IntelControlsFixture::cpuid(cpu, leaf, sub);
        if (!r.error && leaf == 1 && hypervisor) r.words[2] |= 1u << 31;
        return r;
    }
    octool::core::Reply execute(const octool::core::Request &r) override {
        using namespace octool::core;
        requests.push_back(r); Reply out;
        if (r.write) { ++writes; out.error = -EPERM; return out; }
        if (int(reads++) == failRead) { out.error = -EACCES; out.value = UINT64_MAX; return out; }
        if (r.space == Space::Pci && r.width == 4 && !r.bus && !r.device && !r.function) {
            if (r.address == 0) out.value = pciId;
            else if (r.address == 8) out.value = pciClass;
            else if (r.address == 0x48) out.value = bar & UINT32_MAX;
            else if (r.address == 0x4c) out.value = bar >> 32;
            else out.error = -EINVAL;
        } else if (r.space == Space::Memory) {
            bool found = false;
            for (const auto &spec : intelMemoryRegisters()) {
                const auto address = (bar & ~UINT64_C(0x1ffff)) + controller * 0x10000 + spec.offset + (spec.offset == 0xd800 ? 0 : channel * 0x800);
                if (r.address == address && r.width == spec.width) { out.value = memory[spec.offset]; found = true; break; }
            }
            if (!found) out.error = -EFAULT;
        } else out.error = -EINVAL;
        if (afterRequest) afterRequest(r);
        return out;
    }
};
}
