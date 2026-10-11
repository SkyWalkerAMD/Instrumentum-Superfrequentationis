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
    b[1] = 0x10; b[2] = 0x12; b[3] = 2; b[4] = 4; b[6] = 0x20; b[192] = 0x10; b[235] = 0x22;
    b[24] = 0x81; b[25] = 1; b[28] = 0x80; // CL20,34,36,98; distinct from XMP.
    spdWord(b, 20, 416); spdWord(b, 30, 16000); spdSeal(b, 0, 510);
    b[640] = 0x0c; b[641] = 0x4a; b[642] = 0x30; b[643] = 7;
    const char *names[] = {"Performance", "Balanced", "Third profile"};
    for (unsigned i = 0; i < 3; ++i) {
        const std::string name = names[i];
        std::copy(name.begin(), name.end(), b.begin() + 654 + i * 16);
        const unsigned off = 704 + 64 * i;
        b[off] = 0x30; b[off + 1] = std::uint8_t(0x25 + i); b[off + 2] = 0x25; b[off + 4] = 0x22;
        spdWord(b, off + 5, 333 + 24 * i);
        b[off+7] = std::uint8_t(2u << i); b[off+8] = std::uint8_t(4u << i); b[off+11] = std::uint8_t(0x80u >> i);
        b[off+60] = std::uint8_t(0x11+i); // Preserve uninterpreted high nibble.
        spdWord(b, off + 13, 12654); spdWord(b, off + 15, 12654); spdWord(b, off + 17, 12654);
        spdWord(b, off + 19, 25974); spdWord(b, off + 21, 38628); spdWord(b, off + 23, 30000);
        spdWord(b, off + 25, 295); spdWord(b, off + 27, 160); spdWord(b, off + 29, 130);
        // First slot uses the pinned public XMP profile's secondary quantities.
        // Other slots have distinct markers, not suggested hardware settings.
        const unsigned times[] = {5000,20000,10000,10000,2500,5000,7500,10000};
        const unsigned limits[] = {8,32,16,16,4,8,12,32};
        for (unsigned t = 0; t < 8; ++t) {
            spdWord(b,off+31+3*t,times[t]+17*i);
            b[off+33+3*t] = std::uint8_t(limits[t]+i);
        }
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
// User-slot contents do not establish firmware enablement. Manufacturer data
// and user data intentionally differ so both frontend groups can be checked.
inline std::vector<std::uint8_t> spdUserFixture(bool expo = false) {
    auto b = expo ? spdExpoFixture() : spdFixture();
    for (unsigned i = 0; i < 2; ++i) {
        if (expo && i == 0) continue;
        const unsigned off = 896 + 64*i;
        std::copy_n(b.begin()+704+64*i,64,b.begin()+off);
        spdWord(b,off+5,400+25*i); b[off+1] = std::uint8_t(0x26+i);
        spdSeal(b,off,62);
    }
    return b;
}
// Nonzero payload with a zero computed AND stored CRC, pinned by the existing
// xmp-user-presence-research.json counterexample. Not suggested hardware data.
inline void spdZeroCrcUserSlot(std::vector<std::uint8_t> &b, unsigned off = 896) {
    const std::string data = "30252500224d010000000000006e316e316e317665e49630752701a000c533000000000000000000000000000000000000000000000000000000000000000000";
    for (unsigned i = 0; i < 64; ++i) {
        const auto digit = [](char c) { return unsigned(c <= '9' ? c-'0' : c-'a'+10); };
        b[off+i] = std::uint8_t(digit(data[2*i])*16+digit(data[2*i+1]));
    }
}
// Independent synthetic DDR4 XMP 2.0 slots. Deliberately distinct coarse/fine
// fields catch byte addressing, signed arithmetic and tRAS/tRC mix-ups.
inline std::vector<std::uint8_t> spdDdr4XmpFixture() {
    std::vector<std::uint8_t> b(512);
    b[1] = 0x10; b[2] = 0x0c; b[3] = 2; b[4] = 5; b[12] = 1; b[13] = 3;
    b[20] = 1; b[21] = 2; b[22] = 0x80; b[23] = 0x20; // CL7,16,30,36.
    b[18] = 10; b[24] = 110; spdSeal(b,0,126);
    b[384] = 0x0c; b[385] = 0x4a; b[386] = 3; b[387] = 0x20;
    for (unsigned i = 0; i < 2; ++i) {
        const unsigned off = 393 + 47*i;
        b[off] = std::uint8_t(i ? 0x94 : 0xa3); b[off+3] = std::uint8_t(5+i);
        b[off+8] = std::uint8_t(80+i); b[off+9] = std::uint8_t(88+i); b[off+10] = std::uint8_t(96+i);
        b[off+11] = 0x11; b[off+12] = std::uint8_t(0x20+i); b[off+13] = std::uint8_t(0xa0+i);
        spdWord(b,off+14,2800+i); spdWord(b,off+16,2080+i); spdWord(b,off+18,1280+i);
        b[off+20] = 0xf0; b[off+21] = std::uint8_t(240+i); // Reserved high nibble ignored.
        b[off+22] = std::uint8_t(32+i); b[off+23] = std::uint8_t(48+i);
        b[off+32] = 4; b[off+33] = 0xfd; b[off+34] = 0xe7;
        b[off+35] = 0xfe; b[off+36] = 1; b[off+37] = 0xff; b[off+38] = std::uint8_t(i ? 0xad : 0);
    }
    return b;
}
// Distinct base timing markers, not a hardware dump or suggested settings.
inline std::vector<std::uint8_t> spdProfileGeometryFixture(bool ddr5) {
    auto b = ddr5 ? spdFixture() : spdDdr4XmpFixture();
    if (ddr5) {
        b[4] = 0x65; // 24 Gbit, 4H 3DS; two package ranks per subchannel.
        b[234] = 8; b[235] = 0x4a; // Four 32-bit subchannels, four ECC bits each.
        spdWord(b,48,321); spdWord(b,50,234); spdWord(b,52,123);
        spdSeal(b,0,510);
    } else {
        b[386] = 0x37; // Enabled profiles recommend two / four DIMMs per channel.
        b[397] = 0xb4; b[398] = 5; // Published XMP 2.0 example: CL9,11,12,14,15,17.
        b[444] = 1; b[446] = 2; b[447] = 0x80; // CL7,24 plus a reserved bit, not a range switch.
    }
    return b;
}
// Two unlike geometries: even ranks use 24 Gb x8 4H 3DS, odd ranks
// use 16 Gb x16 monolithic devices. Three ranks, two 32-bit subchannels.
// 196608 MiB + 8192 MiB = 204800 MiB. Synthetic, not an installed SKU.
inline std::vector<std::uint8_t> spdAsymmetricFixture() {
    auto b = spdProfileGeometryFixture(true);
    b[4] = 0x65; b[6] = 0x20; b[8] = 4; b[10] = 0x40;
    b[234] = 0x50; b[235] = 0x2a;
    spdSeal(b,0,510); return b;
}
// Synthetic DDR4 primary SDP + secondary DDP, x8 / 64-bit data bus.
// Three package ranks: primary 16 Gbit x 1, secondary 12 Gbit x 2.
// Density ratio 1 is 16->12 Gbit, not a halving. ECC is excluded.
inline std::vector<std::uint8_t> spdDdr4AsymmetricFixture() {
    auto b = spdDdr4XmpFixture();
    b[4] = 6; b[6] = 0; b[10] = 0x95; b[12] = 0x51; b[13] = 0x0b;
    spdSeal(b,0,126); return b;
}
inline std::vector<std::uint8_t> spdBaseTimingsFixture(bool ddr5) {
    auto b = ddr5 ? spdFixture() : spdDdr4XmpFixture();
    if (ddr5) {
        b[1] = 0x12;
        for (unsigned i = 0; i < 11; ++i) {
            spdWord(b,70+3*i,1200+179*i);
            b[72+3*i] = std::uint8_t(3+5*i);
        }
    } else {
        b[41] = 0xa1; b[42] = 0x23; b[43] = 0x42; b[44] = 0x34; b[45] = 0x56;
    }
    spdSeal(b,0,ddr5 ? 510 : 126);
    return b;
}
} }
