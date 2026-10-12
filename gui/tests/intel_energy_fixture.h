// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "intel_controls_fixture.h"

struct IntelEnergyFixture : IntelControlsFixture {
    bool hasMsr = true, hypervisor = false, inconsistentSignature = false;
    unsigned maximumLeaf = 1, stepping = 0;
    IntelEnergyFixture() {
        expectedCpu = 130;
        regs.clear(); regs[0x1fc] = UINT64_C(0xa5c3211098600243);
    }
    octool::core::CpuIdReply cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf) override {
        auto result = IntelControlsFixture::cpuid(cpu, leaf, subleaf);
        if (result.error) return result;
        if (!leaf) result.words[0] = maximumLeaf;
        if (leaf == 1) {
            result.words[0] |= stepping;
            if (inconsistentSignature && cpuCalls % 3 == 0) result.words[0] ^= 1;
            result.words[3] = hasMsr ? 1u << 5 : 0;
            result.words[2] = hypervisor ? 1u << 31 : 0;
        }
        return result;
    }
};
