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
