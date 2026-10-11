// SPDX-License-Identifier: GPL-2.0-only
#include "spd_fixture.h"
#include <cassert>
#include <cerrno>
#include <iostream>
using namespace octool::core;
using namespace octool::test;
namespace {
void threeProfiles() {
    const auto bytes = spdFixture(); const auto out = decodeSpd(bytes);
    assert(!out.error && out.crcValid && !out.xmp.error && out.xmp.present && out.xmp.profiles.size() == 3);
    assert(out.xmp.crc.checked && out.xmp.crc.valid && out.xmp.enabledMask == 7 && !out.xmp.expoPresent);
    for (unsigned i = 0; i < 3; ++i) {
        const auto &p = out.xmp.profiles[i];
        assert(p.index == i + 1 && p.offset == 704 + 64 * i && p.enabled && !p.error && p.crc.valid && p.nameValid);
        assert(p.raw.size() == 64 && p.values.size() == 14);
        const unsigned expected[] = {1800, 1250 + 50 * i, 1250, 1100, 333 + 24 * i,
            12654, 12654, 12654, 25974, 38628, 30000, 295, 160, 130};
        for (unsigned f = 0; f < 14; ++f) {
            assert(p.values[f].value == expected[f]);
            assert(p.values[f].unit == (f < 4 ? "mV" : f < 11 ? "ps" : "ns"));
        }
    }
    assert(out.xmp.profiles[2].name == "Third profile");
    assert(bytes == spdFixture());
}
void truncation() {
    const auto original = spdFixture();
    for (unsigned n = 0; n <= original.size(); ++n) {
        const auto r = decodeSpd(std::vector<std::uint8_t>(original.begin(), original.begin() + n));
        if (n < 236) { assert(r.error && !r.xmp.inspected); continue; }
        assert(!r.error);
        if (n < 642) { assert(!r.xmp.inspected && !r.xmp.error); continue; }
        assert(r.xmp.inspected && r.xmp.present);
        if (n < 704) { assert(r.xmp.error == -EMSGSIZE && r.xmp.profiles.empty()); continue; }
        assert(r.xmp.profiles.size() == 3);
        for (const auto &p : r.xmp.profiles) {
            assert(p.error == (n < p.offset + 64 ? -EMSGSIZE : 0));
            assert(p.values.empty() == (n < p.offset + 64));
        }
    }
}
void corruptedSections() {
    const auto original = spdFixture();
    for (unsigned block = 0; block < 4; ++block) {
        for (unsigned bit = 0; bit < 64 * 8; ++bit) {
            // Magic mismatch means "absent", not an invented damaged header.
            if (!block && bit < 16) continue;
            auto b = original; b[640 + 64 * block + bit / 8] ^= std::uint8_t(1u << (bit % 8));
            const auto r = decodeSpd(b);
            assert(!r.error && r.crcValid && r.xmp.error == -EILSEQ);
            if (!block) assert(r.xmp.profiles.empty() && !r.xmp.crc.valid);
            else for (unsigned i = 0; i < 3; ++i) {
                const auto &p = r.xmp.profiles[i];
                assert(p.error == (i + 1 == block ? -EILSEQ : 0));
                assert(p.values.empty() == (i + 1 == block));
            }
        }
    }
    auto b = original; b[20] ^= 1;
    const auto r = decodeSpd(b); assert(r.error == -EILSEQ && !r.xmp.inspected);
}
void gatesAndOverlap() {
    for (unsigned mask = 0; mask < 8; ++mask) {
        auto selected = spdFixture(); selected[643] = std::uint8_t(mask); spdSeal(selected,640,62);
        const auto r = decodeSpd(selected); assert(!r.xmp.error);
        for (unsigned i = 0; i < 3; ++i) {
            assert(r.xmp.profiles[i].enabled == ((mask & (1u << i)) != 0));
            assert(r.xmp.profiles[i].values.empty() == ((mask & (1u << i)) == 0));
        }
    }
    auto b = spdFixture(); b[643] = 0; spdSeal(b,640,62);
    std::fill(b.begin()+704,b.end(),std::uint8_t(0xff));
    auto r = decodeSpd(b); assert(!r.xmp.error);
    for (const auto &p : r.xmp.profiles) assert(!p.enabled && !p.crc.checked && p.values.empty());
    b = spdFixture(); b[642] = 0x31; spdSeal(b,640,62);
    r = decodeSpd(b); assert(r.xmp.error == -ENOTSUP && r.xmp.profiles.empty());
    b = spdFixture(); b[640] = 0;
    r = decodeSpd(b); assert(!r.xmp.present && r.xmp.inspected && !r.xmp.error);
    b = spdFixture(); const std::string expo = "EXPO"; std::copy(expo.begin(),expo.end(),b.begin()+832);
    r = decodeSpd(b); assert(r.xmp.expoPresent && r.xmp.error == -EINVAL && r.xmp.profiles[2].blockedByExpo);
    assert(!r.xmp.profiles[2].crc.checked && r.xmp.profiles[2].values.empty() && !r.xmp.profiles[0].error);
    b[643] = 3; spdSeal(b,640,62);
    r = decodeSpd(b); assert(!r.xmp.error && r.xmp.profiles[2].blockedByExpo && !r.xmp.profiles[2].enabled);
    b[2] = 0x0c; spdSeal(b,0,126);
    r = decodeSpd(b); assert(!r.error && r.xmp.inspected && !r.xmp.present);
}
void boundedNames() {
    auto b = spdFixture(); std::fill(b.begin()+654,b.begin()+670,'A'); spdSeal(b,640,62);
    auto r = decodeSpd(b); assert(r.xmp.profiles[0].name == std::string(16,'A'));
    b[655] = 0; b[656] = 0xff; spdSeal(b,640,62);
    r = decodeSpd(b); assert(r.xmp.profiles[0].nameValid && r.xmp.profiles[0].name == "A");
    for (unsigned bad : {1u, 31u, 127u, 255u}) {
        b[654] = std::uint8_t(bad); spdSeal(b,640,62); r = decodeSpd(b);
        assert(!r.xmp.error && !r.xmp.profiles[0].nameValid && r.xmp.profiles[0].name.empty());
    }
}
void numericBoundaries() {
    for (unsigned encoded = 0; encoded <= 255; ++encoded) {
        auto b = spdFixture(); b[705] = std::uint8_t(encoded); spdSeal(b,704,62);
        const auto p = decodeSpd(b).xmp.profiles[0];
        assert(!p.error && p.values[1].value == (encoded >> 5)*1000+(encoded & 31)*50);
    }
    auto b = spdFixture(); spdWord(b,709,0); spdSeal(b,704,62);
    auto p = decodeSpd(b).xmp.profiles[0]; assert(p.error == -EINVAL && p.crc.valid && p.values.empty());
    spdWord(b,709,65535); spdWord(b,717,0x1234); spdWord(b,729,65535); spdSeal(b,704,62);
    p = decodeSpd(b).xmp.profiles[0]; assert(!p.error && p.values[4].value == 65535 && p.values[5].value == 0x1234 && p.values[11].value == 65535);
    b.resize(4096); assert(!decodeSpd(b).xmp.error);
    b.resize(4097); assert(decodeSpd(b).error == -EINVAL);
}
void expoProfiles() {
    const auto b = spdExpoFixture(); const auto r = decodeSpd(b);
    assert(!r.error && !r.xmp.error && !r.expo.error && r.expo.present && r.expo.crc.valid);
    assert(r.expo.raw.size() == 128 && r.expo.profiles.size() == 2 && r.expo.revision == 0x10);
    assert(r.expo.configurationRaw == 0x33 && r.expo.featuresRaw == 0x11);
    assert(r.xmp.profiles[2].blockedByExpo && !r.xmp.profiles[2].enabled);
    const unsigned expected[][13] = {
        {1250,1250,1800,333,12654,12654,12654,25974,38628,30000,295,160,130},
        {1200,1200,1800,357,14280,14280,14280,29988,44268,30000,295,160,130}
    };
    for (unsigned i = 0; i < 2; ++i) {
        const auto &p = r.expo.profiles[i];
        assert(p.index == i+1 && p.offset == 842+40*i && p.enabled && !p.error && p.raw.size() == 40 && p.values.size() == 13);
        for (unsigned v = 0; v < 13; ++v) {
            assert(p.values[v].value == expected[i][v]);
            assert(p.values[v].unit == (v < 3 ? "mV" : v < 10 ? "ps" : "ns"));
        }
    }
    assert(b == spdExpoFixture());
}
void expoTruncation() {
    const auto b = spdExpoFixture();
    for (unsigned n = 0; n <= b.size(); ++n) {
        const auto r = decodeSpd(std::vector<std::uint8_t>(b.begin(),b.begin()+n));
        if (n < 836) { assert(!r.expo.inspected && !r.expo.error); continue; }
        assert(!r.error && !r.xmp.error && r.expo.inspected && r.expo.present);
        assert(r.expo.error == (n < 960 ? -EMSGSIZE : 0));
        assert(r.expo.crc.checked == (n >= 960) && r.expo.profiles.empty() == (n < 960));
    }
}
void expoCorruptionAndIsolation() {
    const auto original = spdExpoFixture();
    for (unsigned bit = 0; bit < 128 * 8; ++bit) {
        auto b = original; b[832+bit/8] ^= std::uint8_t(1u << (bit%8));
        const auto r = decodeSpd(b);
        assert(!r.error && !r.xmp.error && r.xmp.profiles[0].values.size() == 14);
        assert(r.expo.profiles.empty());
        if (bit < 32) assert(!r.expo.present && !r.expo.error);
        else assert(r.expo.present && r.expo.error == -EILSEQ && r.expo.raw.size() == 128);
    }
    auto b = original; b[660] ^= 1;
    auto r = decodeSpd(b); assert(r.xmp.error == -EILSEQ && !r.expo.error && r.expo.profiles.size() == 2);
    b = original; b[640] = 0;
    r = decodeSpd(b); assert(!r.xmp.present && !r.expo.error && r.expo.profiles.size() == 2);
    b = original; b[643] = 7; spdSeal(b,640,62);
    r = decodeSpd(b); assert(r.xmp.error == -EINVAL && !r.expo.error && r.expo.profiles.size() == 2);
    b = original; b[20] ^= 1;
    r = decodeSpd(b); assert(r.error == -EILSEQ && !r.expo.inspected);
    b = original; b[2] = 0x0c; spdSeal(b,0,126);
    r = decodeSpd(b); assert(!r.error && !r.expo.inspected);
}
void expoFlagsAndRevision() {
    for (unsigned flags = 0; flags < 256; ++flags) {
        auto b = spdExpoFixture(); b[837] = std::uint8_t(flags); b[838] = std::uint8_t(255-flags); spdSeal(b,832,126);
        const auto r = decodeSpd(b); assert(!r.expo.error && r.expo.configurationRaw == flags && r.expo.featuresRaw == 255-flags);
        for (unsigned i = 0; i < 2; ++i) {
            const auto &p = r.expo.profiles[i]; const bool enabled = (flags & (1u << (i*4))) != 0;
            assert(p.enabled == enabled && p.values.empty() == !enabled);
        }
    }
    auto b = spdExpoFixture(); b[837] = 0;
    std::fill(b.begin()+842,b.begin()+922,std::uint8_t(0xff)); spdSeal(b,832,126);
    auto r = decodeSpd(b); assert(!r.expo.error && r.expo.profiles[0].values.empty() && r.expo.profiles[1].values.empty());
    for (unsigned revision : {0u, 0x11u, 0x20u, 0xffu}) {
        b[836] = std::uint8_t(revision); spdSeal(b,832,126); r = decodeSpd(b);
        assert(r.expo.error == -ENOTSUP && r.expo.crc.valid && r.expo.profiles.empty() && r.expo.raw.size() == 128);
    }
}
void expoNumericBoundaries() {
    for (unsigned encoded = 0; encoded < 256; ++encoded) {
        auto b = spdExpoFixture(); b[842] = std::uint8_t(encoded); spdSeal(b,832,126);
        assert(decodeSpd(b).expo.profiles[0].values[0].value == (encoded>>5)*1000+(encoded&31)*50);
    }
    auto b = spdExpoFixture(); spdWord(b,846,0); spdSeal(b,832,126);
    auto r = decodeSpd(b); assert(r.expo.error == -EINVAL && r.expo.crc.valid && r.expo.profiles[0].values.empty());
    assert(!r.expo.profiles[1].error && r.expo.profiles[1].values.size() == 13);
    spdWord(b,846,65535); spdWord(b,848,0x1234); spdWord(b,860,65535); spdSeal(b,832,126);
    r = decodeSpd(b); assert(!r.expo.error && r.expo.profiles[0].values[3].value == 65535);
    assert(r.expo.profiles[0].values[4].value == 0x1234 && r.expo.profiles[0].values[10].value == 65535);
    b.resize(4096); assert(!decodeSpd(b).expo.error);
    b.resize(4097); assert(decodeSpd(b).error == -EINVAL);
}
void ddr4Profiles() {
    const auto b = spdDdr4XmpFixture(); const auto r = decodeSpd(b);
    assert(!r.error && r.crcValid && r.xmp.present && r.xmp.headerCaptured && !r.xmp.error);
    assert(r.xmp.revision == 0x20 && r.xmp.raw.size() == 9 && r.xmp.enabledMask == 3);
    assert(!r.xmp.crcSupported && !r.xmp.crc.checked && !r.expo.inspected && !r.xmp.expoInspected);
    const unsigned expected[][13] = {
        {1350,625,9999,11001,11998,36000,51975,350000,260000,160000,30000,3997,6004},
        {1200,667,10124,11126,12123,36125,52100,350125,260125,160125,30125,4122,6129}
    };
    assert(r.xmp.profiles.size() == 2);
    for (unsigned i = 0; i < 2; ++i) {
        const auto &p = r.xmp.profiles[i];
        assert(p.index == i+1 && p.offset == 393+47*i && p.enabled && !p.error && !p.crc.checked);
        assert(!p.nameSupported && !p.nameValid && p.name.empty() && p.timebaseRaw == 0 && p.raw.size() == 47 && p.values.size() == 13);
        for (unsigned v = 0; v < 13; ++v) {
            assert(p.values[v].value == expected[i][v]);
            assert(p.values[v].unit == (v ? "ps" : "mV"));
        }
    }
    assert(r.xmp.profiles[0].values[9].name == "tRFC4 minimum" && b == spdDdr4XmpFixture());
}
void ddr4Truncation() {
    const auto b = spdDdr4XmpFixture();
    for (unsigned n = 0; n <= b.size(); ++n) {
        const auto r = decodeSpd(std::vector<std::uint8_t>(b.begin(),b.begin()+n));
        if (n < 128) { assert(r.error && !r.xmp.inspected); continue; }
        assert(!r.error && r.crcValid && !r.xmp.crc.checked);
        if (n < 386) { assert(!r.xmp.inspected && !r.xmp.error); continue; }
        assert(r.xmp.present);
        if (n < 393) { assert(r.xmp.error == -EMSGSIZE && !r.xmp.headerCaptured && r.xmp.profiles.empty()); continue; }
        assert(r.xmp.headerCaptured && r.xmp.profiles.size() == 2);
        for (const auto &p : r.xmp.profiles) {
            assert(p.error == (n < p.offset+47 ? -EMSGSIZE : 0));
            assert(p.values.empty() == (n < p.offset+47));
        }
    }
}
void ddr4FlagsAndRevision() {
    for (unsigned flags = 0; flags < 256; ++flags) {
        auto b = spdDdr4XmpFixture(); b[386] = std::uint8_t(flags);
        const auto r = decodeSpd(b); assert(!r.xmp.error && r.xmp.configurationRaw == flags);
        for (unsigned i = 0; i < 2; ++i) {
            const auto &p = r.xmp.profiles[i];
            assert(p.enabled == bool(flags & (1u<<i)) && p.values.empty() == !p.enabled);
        }
    }
    auto b = spdDdr4XmpFixture(); b[386] = 0;
    std::fill(b.begin()+393,b.end(),std::uint8_t(0xff)); b[388] = b[389] = 0xff;
    auto r = decodeSpd(b); assert(!r.xmp.error && r.xmp.profiles[0].raw.size() == 47);
    b.resize(393); r = decodeSpd(b); assert(!r.xmp.error && r.xmp.profiles[0].raw.empty());
    for (unsigned revision : {0u,0x10u,0x21u,0x30u,0xffu}) {
        b[387] = std::uint8_t(revision); r = decodeSpd(b);
        assert(r.xmp.error == -ENOTSUP && r.xmp.headerCaptured && r.xmp.profiles.empty());
    }
    b[384] = 0; r = decodeSpd(b); assert(r.xmp.inspected && !r.xmp.present && !r.xmp.error);
}
void ddr4Timebases() {
    for (unsigned code = 0; code < 256; ++code) {
        auto b = spdDdr4XmpFixture(); b[388] = std::uint8_t(code);
        // The base timebase is intentionally unsupported: XMP has its own.
        b[17] = 0x0f; spdSeal(b,0,126);
        const auto r = decodeSpd(b); assert(!r.error && !r.xmp.profiles[1].error);
        const auto &p = r.xmp.profiles[0];
        assert(p.timebaseRaw == int(code) && p.error == ((code&15) ? -ENOTSUP : 0));
        assert(p.values.empty() == bool(code&15));
    }
    auto b = spdDdr4XmpFixture(); b[389] = 4;
    auto r = decodeSpd(b); assert(!r.xmp.profiles[0].error && r.xmp.profiles[1].error == -ENOTSUP);
    b[386] = 1; r = decodeSpd(b); assert(!r.xmp.error && !r.xmp.profiles[1].enabled);
}
void ddr4SignedTimingsAndBounds() {
    const unsigned fine[] = {38,37,36,35,34,33,32}, positions[] = {1,2,3,4,6,11,12};
    const unsigned coarse[] = {5,80,88,96,416,32,48};
    for (unsigned f = 0; f < 7; ++f) for (unsigned code = 0; code < 256; ++code) {
        auto b = spdDdr4XmpFixture(); b[393+fine[f]] = std::uint8_t(code);
        const auto p = decodeSpd(b).xmp.profiles[0];
        const int adjustment = code < 128 ? int(code) : int(code)-256;
        assert(!p.error && p.values[positions[f]].value == unsigned(int(coarse[f])*125+adjustment));
        assert(p.values[5].value == 36000); // tRAS never receives tRC's fine offset.
    }
    for (unsigned code = 0; code < 256; ++code) {
        auto b = spdDdr4XmpFixture(); b[393] = std::uint8_t(code);
        assert(decodeSpd(b).xmp.profiles[0].values[0].value == (code>>7)*1000+(code&127)*10);
    }
    auto b = spdDdr4XmpFixture(); b[396] = b[431] = 0;
    auto r = decodeSpd(b); assert(r.xmp.error == -EINVAL && r.xmp.profiles[0].values.empty() && !r.xmp.profiles[1].error);
    b[396] = 1; b[431] = 0x80; r = decodeSpd(b); assert(r.xmp.profiles[0].error == -EINVAL);
    b = spdDdr4XmpFixture(); b[401] = 0; b[430] = 0xff;
    r = decodeSpd(b); assert(r.xmp.profiles[0].error == -EINVAL && r.xmp.profiles[0].values.empty());
    b = spdDdr4XmpFixture(); b[404] = b[405] = b[406] = b[413] = b[414] = 0xff; b[427] = 0x7f;
    spdWord(b,407,65535); r = decodeSpd(b);
    assert(!r.xmp.error && r.xmp.profiles[0].values[5].value == 511875);
    assert(r.xmp.profiles[0].values[6].value == 512002 && r.xmp.profiles[0].values[7].value == 8191875);
    assert(r.xmp.profiles[0].values[10].value == 511875);
    b.resize(4096); assert(!decodeSpd(b).xmp.error);
    b.resize(4097); assert(decodeSpd(b).error == -EINVAL);
}
void ddr4IntegrityAndTypeIsolation() {
    auto b = spdDdr4XmpFixture(); b[393] ^= 1;
    auto r = decodeSpd(b);
    assert(!r.error && r.crcValid && !r.xmp.error && !r.xmp.crc.checked && !r.xmp.profiles[0].crc.checked);
    assert(r.xmp.profiles[0].values[0].value == 1340); // No invented extension checksum.
    b[18] ^= 1; r = decodeSpd(b); assert(r.error == -EILSEQ && !r.xmp.inspected);
    b = spdDdr4XmpFixture(); b.resize(1024); b[2] = 0x12; spdSeal(b,0,510);
    r = decodeSpd(b); assert(!r.error && r.xmp.inspected && !r.xmp.present && r.xmp.profiles.empty());
}
}
int main() {
    threeProfiles(); truncation(); corruptedSections(); gatesAndOverlap(); boundedNames(); numericBoundaries();
    expoProfiles(); expoTruncation(); expoCorruptionAndIsolation(); expoFlagsAndRevision(); expoNumericBoundaries();
    ddr4Profiles(); ddr4Truncation(); ddr4FlagsAndRevision(); ddr4Timebases(); ddr4SignedTimingsAndBounds(); ddr4IntegrityAndTypeIsolation();
    std::cout << "17 SPD XMP/EXPO scenario groups passed (capture lengths, CRC, timebases and signed timing boundaries)\n";
}
