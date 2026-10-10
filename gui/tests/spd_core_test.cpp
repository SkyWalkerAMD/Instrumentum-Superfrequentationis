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
    r = decodeSpd(b); assert(!r.error && !r.xmp.inspected);
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
}
int main() {
    threeProfiles(); truncation(); corruptedSections(); gatesAndOverlap(); boundedNames(); numericBoundaries();
    std::cout << "6 SPD XMP scenario groups passed (all capture lengths and section corruption bits)\n";
}
