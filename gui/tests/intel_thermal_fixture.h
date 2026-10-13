// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "intel_energy_fixture.h"

struct IntelThermalFixture : IntelEnergyFixture {
    bool dts = true;
    IntelThermalFixture() {
        maximumLeaf=6; regs.clear();
        regs[0xce]=UINT64_C(0x8765432140000000);
        regs[0x1a2]=UINT64_C(0xfedcba98456400a5); // TjMax 100 C, offset 5 C; preserve all other bits.
    }
    octool::core::CpuIdReply cpuid(unsigned cpu,std::uint32_t leaf,std::uint32_t subleaf) override {
        auto result=IntelEnergyFixture::cpuid(cpu,leaf,subleaf);
        if (!result.error && leaf==6) result.words[0]=dts?1:0;
        return result;
    }
};
