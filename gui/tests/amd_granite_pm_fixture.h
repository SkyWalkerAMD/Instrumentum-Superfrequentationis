// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "amd_pm_native_fixture.h"
namespace granite_pm_fixture {
inline std::vector<std::uint8_t> bytes(unsigned size = 2452) {
    std::vector<std::uint8_t> b(size);
    for (unsigned off = 0; off < size; off += 4) pm_fixture::put(b,off,0x3f000000+off/4);
    const std::uint32_t dpm[5][4] = {
        {0x41c80000,0,0x42480000,0x41c80000}, // sample shares: 25, 0, 50, 25
        {0x44fa0000,0x44e10000,0x44c80000,0x44af0000}, // policy A: 2000, 1800, 1600, 1400
        {0x44e10000,0x44c80000,0x44960000,0x447a0000}, // policy B: 1800, 1600, 1200, 1000
        {0x45610000,0x44c80000,0x45160000,0x447a0000}, // policy C: 3600, 1600, 2400, 1000
        {0x3f800000,0x3f900000,0x3fa00000,0x3fb00000}}; // policy V: 1, 1.125, 1.25, 1.375
    for(unsigned kind=0;kind<5;++kind) for(unsigned state=0;state<4;++state)
        pm_fixture::put(b,4*(67+4*kind+state),dpm[kind][state]);
    pm_fixture::put(b,19*4,0x3fc00000); // 1.5 V shared request, distinct from command/telemetry
    const unsigned configIndices[]={259,260,261,262,269};
    const std::uint32_t configBits[]={0x3f400000,0x3f900000,0x3f600000,0x3fa00000,0x3f800000};
    for(unsigned i=0;i<5;++i) pm_fixture::put(b,4*configIndices[i],configBits[i]); // units unverified
    pm_fixture::put(b,48*4,0x3f900000); // 1.125 V command mirror
    pm_fixture::put(b,53*4,0x3f600000); // .875 V command mirror
    pm_fixture::put(b,58*4,0x3f200000); // .625 V command mirror
    const std::uint32_t telemetry[3][4] = {
        {0x3fa00000,0x41480000,0x417a0000,0x42720000}, // 1.25 V, 12.5 A, 15.625 W, 60.5 C
        {0x3fc00000,0x41a00000,0x41f00000,0x42828000}, // 1.5 V, 20 A, 30 W, 65.25 C
        {0x3f800000,0x40000000,0x40000000,0x428c0000}}; // 1 V, 2 A, 2 W, 70 C
    for (unsigned channel=0;channel<3;++channel) for(unsigned kind=0;kind<4;++kind)
        pm_fixture::put(b,4*(49+channel*5+kind),telemetry[channel][kind]);
    const unsigned storageSlots=size==1828?1:2;
    for(unsigned core=0;core<8*storageSlots;++core) {
        pm_fixture::put(b,0x4b4+4*(3*8*storageSlots+core),0x408c0000); // 4.375 GHz divider model
        pm_fixture::put(b,0x4b4+4*(8*8*storageSlots+core),0x40600000); // averaged code 3.5
        pm_fixture::put(b,0x4b4+4*(9*8*storageSlots+core),0x40b00000); // 5.5 GHz request policy A
        pm_fixture::put(b,0x4b4+4*(10*8*storageSlots+core),0x40c80000); // 6.25 GHz B can exceed A; no clamp
        pm_fixture::put(b,0x4b4+4*(11*8*storageSlots+core),0x42ff0000); // averaged code 127.5
    }
    for(unsigned ccd=0;ccd<storageSlots;++ccd) {
        pm_fixture::put(b,0x4b4+4*(144*storageSlots+5*storageSlots+ccd),0x40c00000); // 6 GHz model
        pm_fixture::put(b,0x4b4+4*(144*storageSlots+7*storageSlots+ccd),0x46ffff00); // averaged code 32767.5
        pm_fixture::put(b,0x4b4+4*(144*storageSlots+9*storageSlots+ccd),0x43162000); // 150.125 percent; not clamped
        pm_fixture::put(b,0x4b4+4*(144*storageSlots+10*storageSlots+ccd),0x40b80000); // 5.75 GHz fused policy
    }
    const std::uint32_t counters[]={0x3f200000,0x3e000000,0x41c80000,0x42960000,0x43160000}; // .625, .125, 25, 75, 150
    for(unsigned row=13;row<18;++row) for(unsigned core=0;core<8*storageSlots;++core)
        pm_fixture::put(b,0x4b4+4*(row*8*storageSlots+core),counters[row-13]);
    pm_fixture::put(b,0x4b4+4*(144*storageSlots+11*storageSlots),0x41c80000); // already 25 percent
    pm_fixture::put(b,size-4,0x42c80000); // 100, including the final CCD word
    return b;
}
struct Device : AmdPmNativeFixture {
    Device() {
        model = 0x44; pciId = 0x14d81022; smuVersion = 0x625400;
        commandRegister = 0x3b10524; responseRegister = 0x3b10570;
        addressArgsOnes = false; version = 0x620205; size = 2452;
        bytes = granite_pm_fixture::bytes();
    }
};
}
