// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../core/spd.h"
#include <algorithm>

namespace octool { namespace test {
inline void spdWord(std::vector<std::uint8_t> &b, unsigned off, unsigned value) {
    b[off] = std::uint8_t(value); b[off + 1] = std::uint8_t(value >> 8);
}
inline void spdSeal(std::vector<std::uint8_t> &b, unsigned start, unsigned count) {
    spdWord(b, start + count, core::spdCrc16(b.data() + start, count));
}
// Synthetic module, not a hardware dump. Each profile has a different tCK,
// voltage and name to catch slot addressing errors in both frontends.
inline std::vector<std::uint8_t> spdFixture() {
    std::vector<std::uint8_t> b(1024);
    b[1] = 0x10; b[2] = 0x12; b[3] = 2; b[4] = 4; b[6] = 0x20; b[235] = 0x22;
    spdWord(b, 20, 416); spdWord(b, 30, 16000); spdSeal(b, 0, 510);
    b[640] = 0x0c; b[641] = 0x4a; b[642] = 0x30; b[643] = 7;
    const char *names[] = {"Performance", "Balanced", "Third profile"};
    for (unsigned i = 0; i < 3; ++i) {
        const std::string name = names[i];
        std::copy(name.begin(), name.end(), b.begin() + 654 + i * 16);
        const unsigned off = 704 + 64 * i;
        b[off] = 0x30; b[off + 1] = std::uint8_t(0x25 + i); b[off + 2] = 0x25; b[off + 4] = 0x22;
        spdWord(b, off + 5, 333 + 24 * i);
        spdWord(b, off + 13, 12654); spdWord(b, off + 15, 12654); spdWord(b, off + 17, 12654);
        spdWord(b, off + 19, 25974); spdWord(b, off + 21, 38628); spdWord(b, off + 23, 30000);
        spdWord(b, off + 25, 295); spdWord(b, off + 27, 160); spdWord(b, off + 29, 130);
        spdSeal(b, off, 62);
    }
    spdSeal(b, 640, 62);
    return b;
}
// Synthetic EXPO/XMP hybrid. Basic quantities match the pinned public hybrid
// sample; reserved/enhanced bytes deliberately differ (see recovery evidence).
inline std::vector<std::uint8_t> spdExpoFixture() {
    auto b = spdFixture(); b[643] = 3; spdSeal(b,640,62);
    std::fill(b.begin()+832,b.begin()+960,std::uint8_t(0));
    const std::string magic = "EXPO"; std::copy(magic.begin(),magic.end(),b.begin()+832);
    b[836] = 0x10; b[837] = 0x33; b[838] = 0x11;
    const unsigned timings[][10] = {
        {333,12654,12654,12654,25974,38628,30000,295,160,130},
        {357,14280,14280,14280,29988,44268,30000,295,160,130}
    };
    for (unsigned i = 0; i < 2; ++i) {
        const unsigned off = 842 + 40 * i;
        b[off] = b[off+1] = std::uint8_t(0x25-i); b[off+2] = 0x30;
        for (unsigned t = 0; t < 10; ++t) spdWord(b,off+4+2*t,timings[i][t]);
        std::fill(b.begin()+off+24,b.begin()+off+40,std::uint8_t(0xa5));
    }
    spdSeal(b,832,126);
    return b;
}
} }
