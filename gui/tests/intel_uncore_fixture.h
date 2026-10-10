// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "intel_controls_fixture.h"

struct IntelUncoreFixture : IntelControlsFixture {
    bool hasMsr = true, hypervisor = false;
    unsigned maximumLeaf = 1;
    IntelUncoreFixture() {
        expectedCpu = 130;
        // Nonzero reserved bits, including 7 and 15, must survive updates.
        regs.clear(); regs[0x620] = UINT64_C(0x9876543210fe8880) | 45;
    }
    octool::core::CpuIdReply cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf) override {
        auto result = IntelControlsFixture::cpuid(cpu, leaf, subleaf);
        if (result.error) return result;
        if (!leaf) result.words[0] = maximumLeaf;
        if (leaf == 1) {
            result.words[3] = hasMsr ? 1u << 5 : 0;
            result.words[2] = hypervisor ? 1u << 31 : 0;
        }
        return result;
    }
};
