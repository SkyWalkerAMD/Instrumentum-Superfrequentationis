// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "amd_pm_native_fixture.h"
namespace granite_pm_fixture {
inline std::vector<std::uint8_t> bytes(unsigned size = 2452) {
    std::vector<std::uint8_t> b(size);
    for (unsigned off = 0; off < size; off += 4) pm_fixture::put(b,off,0x3f000000+off/4);
    pm_fixture::put(b,71*4,0x44fa0000); // independent binary32 fixture: 2000
    pm_fixture::put(b,75*4,0x44e10000); // 1800
    pm_fixture::put(b,79*4,0x45610000); // 3600
    pm_fixture::put(b,58*4,0x3f600000); // .875
    const std::uint32_t telemetry[3][4] = {
        {0x3fa00000,0x41480000,0x417a0000,0x42720000}, // 1.25 V, 12.5 A, 15.625 W, 60.5 C
        {0x3fc00000,0x41a00000,0x41f00000,0x42828000}, // 1.5 V, 20 A, 30 W, 65.25 C
        {0x3f800000,0x40000000,0x40000000,0x428c0000}}; // 1 V, 2 A, 2 W, 70 C
    for (unsigned channel=0;channel<3;++channel) for(unsigned kind=0;kind<4;++kind)
        pm_fixture::put(b,4*(49+channel*5+kind),telemetry[channel][kind]);
    const unsigned slots=size==1828?1:2;
    const std::uint32_t counters[]={0x3f200000,0x3e000000,0x41c80000,0x42960000,0x43160000}; // .625, .125, 25, 75, 150
    for(unsigned row=13;row<18;++row) for(unsigned core=0;core<8*slots;++core)
        pm_fixture::put(b,0x4b4+4*(row*8*slots+core),counters[row-13]);
    pm_fixture::put(b,0x4b4+4*(144*slots+11*slots),0x41c80000); // already 25 percent
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
