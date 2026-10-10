// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "intel_controls_fixture.h"

struct IntelTurboFixture : IntelControlsFixture {
    bool hybrid = true, hasMsr = true;
    unsigned maximumLeaf = 7;
    IntelTurboFixture() {
        expectedCpu = 130;
        regs[0xce] = (UINT64_C(1) << 28) | (UINT64_C(1) << 63) | 0x2000;
        regs[0x194] = 0x1200;
        regs[0x1ad] = UINT64_C(0x35363738393a3b3c);
        regs[0x1ae] = UINT64_C(0x0807060504030201);
        regs[0x650] = UINT64_C(0x25262728292a2b2c);
        regs[0x651] = UINT64_C(0x100e0c0a08060402);
    }
    octool::core::CpuIdReply cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf) override {
        auto result = IntelControlsFixture::cpuid(cpu, leaf, subleaf);
        if (result.error) return result;
        if (leaf == 0) result.words[0] = maximumLeaf;
        if (leaf == 1) result.words[3] = hasMsr ? 1u << 5 : 0;
        if (leaf == 7) result.words[3] = hybrid ? 1u << 15 : 0;
        return result;
    }
};
