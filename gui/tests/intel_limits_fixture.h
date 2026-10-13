// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "intel_energy_fixture.h"
struct IntelLimitsFixture : IntelEnergyFixture {
    // Independent SDM W0C mask, not derived from the production decoder.
    static constexpr std::uint64_t logs=UINT64_C(0x3df30000);
    std::uint64_t newEvents=0, reassert=0;
    IntelLimitsFixture() { regs.clear(); regs[0x64f]=UINT64_C(0x8765432100030000); }
    octool::core::Reply execute(const octool::core::Request &r) override {
        if (!r.write) return IntelEnergyFixture::execute(r);
        checkCpu(r.cpu); requests.push_back(r); octool::core::Reply out;
        if (r.space!=octool::core::Space::Msr || r.width!=8 || r.address!=0x64f) { out.error=-EINVAL; return out; }
        if (int(writes++)==failWrite) out.error=-EIO;
        else if (!discardChange) {
            const auto current=regs[0x64f] | newEvents;
            regs[0x64f]=(current & ~logs) | (current & logs & r.value) | reassert;
        }
        if (afterRequest) afterRequest(r);
        return out;
    }
};
