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
} }
