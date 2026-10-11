// SPDX-License-Identifier: GPL-2.0-only
#include "spd_fixture.h"
#include <cassert>
#include <cerrno>
#include <iostream>
using namespace octool::core;
using namespace octool::test;
namespace {
const SpdField *baseField(const SpdSnapshot &s, const std::string &name) {
    const SpdField *found = nullptr;
    for (const auto &f : s.fields) if (f.name == name) { assert(!found); found = &f; }
    return found;
}
unsigned baseValue(const SpdSnapshot &s, const std::string &name) {
    const auto *f = baseField(s,name); assert(f); return unsigned(std::stoul(f->value));
}
const char *baseSecondary[] = {"tRRD_L", "tCCD_L", "tCCD_L_WR", "tCCD_L_WR2", "tFAW",
    "tCCD_L_WTR", "tCCD_S_WTR", "tRTP", "tCCD_M", "tCCD_M_WR", "tCCD_M_WTR"};
std::string baseTimingName(unsigned i, bool cycles) {
    return "SPD " + std::string(baseSecondary[i]) + (cycles ? " cycle lower limit (nCK)" : " minimum (ps)");
}
void ddr4BaseWriteDelays() {
    const auto original = spdBaseTimingsFixture(false); const auto first = decodeSpd(original);
    assert(baseValue(first,"SPD tWR minimum (ps)") == 36375);
    assert(baseValue(first,"SPD tWTR_S minimum (ps)") == 70500);
    assert(baseValue(first,"SPD tWTR_L minimum (ps)") == 138750);
    for (unsigned byte = 41; byte <= 45; ++byte) for (unsigned code = 0; code < 256; ++code) {
        auto b = original; b[byte] = std::uint8_t(code); spdSeal(b,0,126); const auto s = decodeSpd(b);
        const unsigned wr = byte == 41 ? (code % 16) * 256 + 0x23 : byte == 42 ? 256 + code : 0x123;
        const unsigned ws = byte == 43 ? (code % 16) * 256 + 0x34 : byte == 44 ? 512 + code : 0x234;
        const unsigned wl = byte == 43 ? (code / 16) * 256 + 0x56 : byte == 45 ? 1024 + code : 0x456;
        assert(!s.error && baseValue(s,"SPD tWR minimum (ps)") == wr*125);
        assert(baseValue(s,"SPD tWTR_S minimum (ps)") == ws*125 && baseValue(s,"SPD tWTR_L minimum (ps)") == wl*125);
        assert(s.xmp.profiles[0].raw == first.xmp.profiles[0].raw);
    }
    for (unsigned code : {0u,255u}) {
        auto b = original; std::fill(b.begin()+41,b.begin()+46,std::uint8_t(code));
        std::fill(b.begin()+117,b.begin()+126,std::uint8_t(0x80)); spdSeal(b,0,126);
        const auto s = decodeSpd(b);
        for (const char *name : {"SPD tWR minimum (ps)", "SPD tWTR_S minimum (ps)", "SPD tWTR_L minimum (ps)"})
            assert(baseValue(s,name) == (code ? 511875u : 0u)); // No unrelated fine offset.
    }
}
void ddr5BaseSecondaryValues() {
    auto b = spdBaseTimingsFixture(true); auto s = decodeSpd(b);
    for (unsigned i = 0; i < 11; ++i) {
        assert(baseValue(s,baseTimingName(i,false)) == 1200+179*i);
        assert(baseValue(s,baseTimingName(i,true)) == 3+5*i);
    }
    const auto before = s;
    spdWord(b,20,65535); spdWord(b,30,1); spdSeal(b,0,510); s = decodeSpd(b);
    for (unsigned i = 0; i < 11; ++i) for (bool ck : {false,true})
        assert(baseValue(s,baseTimingName(i,ck)) == baseValue(before,baseTimingName(i,ck)));
    for (unsigned code : {0u,255u}) {
        std::fill(b.begin()+70,b.begin()+103,std::uint8_t(code)); spdWord(b,20,0); spdSeal(b,0,510); s = decodeSpd(b);
        for (unsigned i = 0; i < 11; ++i) {
            assert(baseValue(s,baseTimingName(i,false)) == (code ? 65535u : 0u));
            assert(baseValue(s,baseTimingName(i,true)) == code);
        }
    }
}
void ddr5BaseTimingByteBoundaries() {
    const auto original = spdBaseTimingsFixture(true); const auto before = decodeSpd(original);
    for (unsigned byte = 0; byte < 33; ++byte) for (unsigned code = 0; code < 256; ++code) {
        auto b = original; b[70+byte] = std::uint8_t(code); spdSeal(b,0,510); const auto s = decodeSpd(b);
        assert(!s.error && s.cas.latencies == before.cas.latencies);
        for (unsigned i = 0; i < 11; ++i) {
            unsigned ps = 1200+179*i, ck = 3+5*i;
            if (byte == i*3) ps = (ps/256)*256+code;
            if (byte == i*3+1) ps = code*256+ps%256;
            if (byte == i*3+2) ck = code;
            assert(baseValue(s,baseTimingName(i,false)) == ps && baseValue(s,baseTimingName(i,true)) == ck);
        }
        assert(baseValue(s,"SPD tAA minimum (ps)") == 16000);
        for (unsigned i = 0; i < 3; ++i) assert(s.xmp.profiles[i].raw == before.xmp.profiles[i].raw);
    }
}
void baseTimingRevisionAndTimebaseGates() {
    for (unsigned revision = 0; revision < 256; ++revision) {
        auto b = spdBaseTimingsFixture(true); b[1] = std::uint8_t(revision); spdSeal(b,0,510);
        const auto s = decodeSpd(b); assert(!s.error && s.xmp.profiles[0].values.size() == 30);
        for (unsigned i = 0; i < 11; ++i) for (bool ck : {false,true})
            assert(bool(baseField(s,baseTimingName(i,ck))) == (revision/16 == 1 && (i < 8 || revision%16 >= 2)));
    }
    for (unsigned timebase = 0; timebase < 256; ++timebase) {
        auto b = spdBaseTimingsFixture(false); b[17] = std::uint8_t(timebase); spdSeal(b,0,126);
        const auto s = decodeSpd(b);
        assert(bool(baseField(s,"SPD tWR minimum (ps)")) == (timebase%16 == 0));
        assert(bool(baseField(s,"SPD tWTR_S minimum (ps)")) == (timebase%16 == 0));
        assert(bool(baseField(s,"SPD tWTR_L minimum (ps)")) == (timebase%16 == 0));
        assert(s.cas.decoded && s.xmp.profiles[0].values.size() == 13);
    }
    auto b = spdBaseTimingsFixture(false); b[1] = 0x20; spdSeal(b,0,126);
    assert(!baseField(decodeSpd(b),"SPD tWR minimum (ps)"));
}
void baseTimingCaptureAndSectionIsolation() {
    for (bool ddr5 : {false,true}) {
        const auto original = spdBaseTimingsFixture(ddr5); const unsigned minimum = ddr5 ? 236 : 128;
        const std::string field = ddr5 ? baseTimingName(10,false) : "SPD tWTR_L minimum (ps)";
        for (unsigned size = 0; size <= original.size(); ++size) {
            const auto s = decodeSpd(std::vector<std::uint8_t>(original.begin(),original.begin()+size));
            assert(bool(baseField(s,field)) == (size >= minimum));
            if (size >= minimum) assert(s.crcChecked == (!ddr5 || size >= 512));
        }
        auto b = original; b[ddr5 ? 70 : 41] ^= 1;
        auto s = decodeSpd(b); assert(s.error == -EILSEQ && !baseField(s,field) && !s.xmp.inspected);
    }
    auto b = spdBaseTimingsFixture(true); b[711] ^= 1;
    auto s = decodeSpd(b); assert(s.xmp.error == -EILSEQ && baseValue(s,baseTimingName(0,false)) == 1200);
    b = spdBaseTimingsFixture(true); b[94] ^= 1; b.resize(236); s = decodeSpd(b);
    assert(!s.error && !s.crcChecked && baseField(s,baseTimingName(10,true)));
    b = spdBaseTimingsFixture(true); b[1] = 0x11; spdSeal(b,0,510); s = decodeSpd(b);
    assert(baseValue(s,baseTimingName(7,false)) == 2453 && !baseField(s,baseTimingName(8,false)));
}
void basePublicTimingExamples() {
    // Timing bytes only from the pinned illumos AQD test fixtures. The rest
    // of these modules remains synthetic; this is not hardware acceptance.
    auto b = spdFixture();
    const std::uint8_t bytes[] = {0x88,0x13,8,0x88,0x13,8,0x20,0x4e,32,0x10,0x27,16,
        0x15,0x34,32,0x10,0x27,16,0xc4,9,4,0x4c,0x1d,12};
    std::copy(bytes,bytes+sizeof(bytes),b.begin()+70); spdSeal(b,0,510);
    auto s = decodeSpd(b);
    const unsigned times[] = {5000,5000,20000,10000,13333,10000,2500,7500};
    const unsigned cycles[] = {8,8,32,16,32,16,4,12};
    for (unsigned i = 0; i < 8; ++i) {
        assert(baseValue(s,baseTimingName(i,false)) == times[i]);
        assert(baseValue(s,baseTimingName(i,true)) == cycles[i]);
    }
    assert(!baseField(s,baseTimingName(8,false)));
    b = spdDdr4XmpFixture(); b[1] = 0x11;
    b[41] = 0; b[42] = 0x78; b[43] = 0; b[44] = 0x14; b[45] = 0x3c; spdSeal(b,0,126); s = decodeSpd(b);
    assert(baseValue(s,"SPD tWR minimum (ps)") == 15000);
    assert(baseValue(s,"SPD tWTR_S minimum (ps)") == 2500);
    assert(baseValue(s,"SPD tWTR_L minimum (ps)") == 7500);
}
void threeProfiles() {
    const auto bytes = spdFixture(); const auto out = decodeSpd(bytes);
    assert(!out.error && out.crcValid && !out.xmp.error && out.xmp.present && out.xmp.profiles.size() == 3);
    assert(out.xmp.crc.checked && out.xmp.crc.valid && out.xmp.enabledMask == 7 && !out.xmp.expoPresent);
    for (unsigned i = 0; i < 3; ++i) {
        const auto &p = out.xmp.profiles[i];
        assert(p.index == i + 1 && p.offset == 704 + 64 * i && p.enabled && !p.error && p.crc.valid && p.nameValid);
        assert(p.raw.size() == 64 && p.values.size() == 30);
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
void xmpSecondaryValues() {
    const auto bytes = spdFixture(); const auto r = decodeSpd(bytes);
    const char *names[] = {"tRRD_L", "tCCD_L_WR", "tCCD_L_WR2", "tCCD_L_WTR",
        "tCCD_S_WTR", "tCCD_L", "tRTP", "tFAW"};
    const unsigned times[] = {5000,20000,10000,10000,2500,5000,7500,10000};
    const unsigned cycles[] = {8,32,16,16,4,8,12,32};
    for (unsigned i = 0; i < 3; ++i) {
        const auto &p = r.xmp.profiles[i]; assert(!p.error && p.values.size() == 30);
        for (unsigned t = 0; t < 8; ++t) {
            const auto &time = p.values[14+2*t], &limit = p.values[15+2*t];
            assert(time.name == std::string(names[t])+" minimum" && time.unit == "ps" && time.value == times[t]+17*i);
            assert(limit.name == std::string(names[t])+" cycle lower limit" && limit.unit == "nCK" && limit.value == cycles[t]+i);
        }
        // Guard against the upstream derived-tick getter's WR/read mix-up.
        assert(p.values[16].value != p.values[24].value);
    }
    assert(bytes == spdFixture());
}
void xmpSecondaryByteBoundaries() {
    const auto original = spdFixture(); const auto reference = decodeSpd(original);
    // Exercise every byte of all 16 quantities and ensure no adjacent field
    // changes. Both high-bit bytes and unsigned one-byte limits are covered.
    for (unsigned t = 0; t < 8; ++t) for (unsigned byte = 0; byte < 3; ++byte)
        for (unsigned code = 0; code < 256; ++code) {
            auto b = original; const unsigned off = 768+31+3*t;
            b[off+byte] = std::uint8_t(code); spdSeal(b,768,62);
            const auto r = decodeSpd(b); const auto &p = r.xmp.profiles[1];
            assert(!r.xmp.error && p.crc.valid && p.values.size() == 30);
            for (unsigned f = 0; f < 30; ++f) {
                unsigned expected = reference.xmp.profiles[1].values[f].value;
                if (byte < 2 && f == 14+2*t)
                    expected = byte ? (expected & 255) | (code << 8) : (expected & 65280) | code;
                if (byte == 2 && f == 15+2*t) expected = code;
                assert(p.values[f].value == expected);
                assert(r.xmp.profiles[0].values[f].value == reference.xmp.profiles[0].values[f].value);
                assert(r.xmp.profiles[2].values[f].value == reference.xmp.profiles[2].values[f].value);
            }
        }
}
void xmpSecondaryZerosAndMaximums() {
    auto b = spdFixture();
    for (unsigned code : {0u,255u}) {
        std::fill(b.begin()+735,b.begin()+759,std::uint8_t(code)); spdSeal(b,704,62);
        auto r = decodeSpd(b); const auto &p = r.xmp.profiles[0];
        assert(!r.xmp.error && p.values.size() == 30);
        for (unsigned f = 14; f < 30; ++f) assert(p.values[f].value == (f % 2 ? code : code*257));
        assert(p.values[11].value == 295); // Refresh remains ns, not ps.
    }
    b[736] ^= 1; auto r = decodeSpd(b);
    assert(r.xmp.error == -EILSEQ && r.xmp.profiles[0].values.empty());
    assert(r.xmp.profiles[1].values.size() == 30 && r.xmp.profiles[2].values.size() == 30);
    b[643] = 6; spdSeal(b,640,62); r = decodeSpd(b);
    assert(!r.xmp.error && r.xmp.profiles[0].values.empty() && !r.xmp.profiles[0].crc.checked);
    assert(r.xmp.profiles[1].values.size() == 30);
}
void xmpSecondaryIndependentRequirements() {
    const auto original = spdFixture(); const auto reference = decodeSpd(original);
    for (unsigned clock : {1u,333u,65535u}) {
        auto b = original; spdWord(b,709,clock); spdSeal(b,704,62);
        const auto p = decodeSpd(b).xmp.profiles[0]; assert(!p.error);
        for (unsigned f = 14; f < 30; ++f)
            assert(p.values[f].value == reference.xmp.profiles[0].values[f].value);
    }
    auto b = original; spdWord(b,735,1); b[737] = 255; spdSeal(b,704,62);
    const auto p = decodeSpd(b).xmp.profiles[0]; assert(!p.error);
    assert(p.values[14].value == 1 && p.values[15].value == 255);
    b = spdExpoFixture(); const auto r = decodeSpd(b);
    assert(!r.xmp.error && r.xmp.profiles[0].values.size() == 30 && r.xmp.profiles[2].values.empty());
    assert(r.expo.profiles[0].values.size() == 13); // No EXPO enhanced-timing inference.
}
void baseCasBitmaps() {
    const auto ddr4 = decodeSpd(spdDdr4XmpFixture()), ddr5 = decodeSpd(spdFixture());
    assert(ddr4.cas.captured && ddr4.cas.decoded && ddr4.cas.offset == 20 && ddr4.cas.raw.size() == 4);
    assert(ddr4.cas.latencies == std::vector<unsigned>({7,16,30,36}) && !ddr4.cas.reservedBitsSet);
    assert(ddr5.cas.captured && ddr5.cas.decoded && ddr5.cas.offset == 24 && ddr5.cas.raw.size() == 5);
    assert(ddr5.cas.latencies == std::vector<unsigned>({20,34,36,98}));
    for (unsigned high = 0; high < 2; ++high) for (unsigned bit = 0; bit < 32; ++bit) {
        auto b = spdDdr4XmpFixture(); std::fill(b.begin()+20,b.begin()+24,std::uint8_t(0));
        b[23] = std::uint8_t(high ? 0x80 : 0); b[20+bit/8] |= std::uint8_t(1u << (bit%8)); spdSeal(b,0,126);
        const auto r = decodeSpd(b); assert(!r.error && !r.xmp.error && r.cas.decoded);
        assert(r.cas.reservedBitsSet == (bit == 30));
        if (bit < 30) assert(r.cas.latencies == std::vector<unsigned>({(high ? 23u : 7u)+bit}));
        else assert(r.cas.latencies.empty()); // Range/reserved bits are never CL bits.
    }
    for (unsigned bit = 0; bit < 40; ++bit) {
        auto b = spdFixture(); std::fill(b.begin()+24,b.begin()+29,std::uint8_t(0));
        b[24+bit/8] = std::uint8_t(1u << (bit%8)); spdSeal(b,0,510);
        const auto r = decodeSpd(b); assert(!r.error && r.cas.latencies == std::vector<unsigned>({20+2*bit}));
    }
}
void xmpCasBitmaps() {
    const auto original = spdFixture(); const auto reference = decodeSpd(original);
    for (unsigned slot = 0; slot < 3; ++slot) {
        const auto &p = reference.xmp.profiles[slot];
        assert(p.cas.decoded && p.cas.offset == 711+64*slot && p.cas.raw.size() == 5);
        assert(p.cas.latencies == std::vector<unsigned>({22+2*slot,40+2*slot,98-2*slot}));
        for (unsigned bit = 0; bit < 40; ++bit) {
            auto b = original; const unsigned off = 704+64*slot;
            std::fill(b.begin()+off+7,b.begin()+off+12,std::uint8_t(0)); b[off+7+bit/8] = std::uint8_t(1u << (bit%8)); spdSeal(b,off,62);
            const auto r = decodeSpd(b); assert(!r.xmp.error && r.xmp.profiles[slot].cas.latencies == std::vector<unsigned>({20+2*bit}));
            assert(r.cas.latencies == reference.cas.latencies);
            for (unsigned other = 0; other < 3; ++other) if (other != slot)
                assert(r.xmp.profiles[other].cas.latencies == reference.xmp.profiles[other].cas.latencies);
        }
    }
}
void casCombinedBitsAndTimingIndependence() {
    for (unsigned byte = 0; byte < 5; ++byte) for (unsigned code = 0; code < 256; ++code) {
        auto b = spdFixture(); std::fill(b.begin()+711,b.begin()+716,std::uint8_t(0)); b[711+byte] = std::uint8_t(code); spdSeal(b,704,62);
        const auto p = decodeSpd(b).xmp.profiles[0]; std::vector<unsigned> expected;
        for (unsigned bit = 0; bit < 8; ++bit) if (code & (1u << bit)) expected.push_back(20+16*byte+2*bit);
        assert(!p.error && p.cas.latencies == expected && p.values.size() == 30);
    }
    auto b = spdFixture(); std::fill(b.begin()+711,b.begin()+716,std::uint8_t(255));
    spdWord(b,709,1); spdWord(b,717,65535); spdSeal(b,704,62);
    auto r = decodeSpd(b); assert(r.xmp.profiles[0].cas.latencies.size() == 40);
    assert(r.xmp.profiles[0].cas.latencies.front() == 20 && r.xmp.profiles[0].cas.latencies.back() == 98);
    spdWord(b,709,65535); spdWord(b,717,1); spdSeal(b,704,62);
    assert(decodeSpd(b).xmp.profiles[0].cas.latencies == r.xmp.profiles[0].cas.latencies);
    b = spdDdr4XmpFixture(); std::fill(b.begin()+20,b.begin()+24,std::uint8_t(255)); spdSeal(b,0,126); r = decodeSpd(b);
    assert(r.cas.reservedBitsSet && r.cas.latencies.size() == 30 && r.cas.latencies.front() == 23 && r.cas.latencies.back() == 52);
}
void xmpCommandRateCodes() {
    for (unsigned code = 0; code < 256; ++code) {
        auto b = spdFixture(); b[764] = std::uint8_t(code); spdSeal(b,704,62);
        const auto r = decodeSpd(b); const auto &p = r.xmp.profiles[0]; const auto &rate = p.commandRate;
        assert(!r.xmp.error && rate.captured && rate.raw == code && rate.code == (code & 15));
        assert(rate.recognized == ((code & 15) <= 3));
        assert(p.values.size() == 30 && p.cas.latencies == std::vector<unsigned>({22,40,98}));
        assert(r.xmp.profiles[1].commandRate.raw == 0x12 && r.xmp.profiles[2].commandRate.raw == 0x13);
    }
}
void casAndCommandCaptureGates() {
    auto b = spdFixture(); b.resize(236); auto r = decodeSpd(b);
    assert(!r.error && !r.crcChecked && r.cas.decoded && !r.xmp.inspected);
    b.resize(235); r = decodeSpd(b); assert(r.error && !r.cas.captured);
    b = spdFixture(); b[24] ^= 1; r = decodeSpd(b); assert(r.error == -EILSEQ && !r.cas.captured);
    b = spdFixture(); b[1] = 0x20; spdSeal(b,0,510); r = decodeSpd(b);
    assert(!r.error && r.cas.captured && !r.cas.decoded && r.cas.latencies.empty() && r.cas.raw.size() == 5);
    assert(r.xmp.profiles[0].cas.decoded); // XMP has its own revision.
    b = spdDdr4XmpFixture(); b[17] = 15; spdSeal(b,0,126); r = decodeSpd(b);
    assert(r.cas.decoded && !r.xmp.profiles[0].cas.captured && !r.xmp.profiles[0].commandRate.captured);
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        b = spdFixture();
        if (scenario == 0) b[711] ^= 1;
        if (scenario == 1) { b[643] = 6; spdSeal(b,640,62); }
        if (scenario == 2) { spdWord(b,709,0); spdSeal(b,704,62); }
        if (scenario == 3) b.resize(767);
        if (scenario == 4) { b = spdExpoFixture(); b[643] = 7; spdSeal(b,640,62); }
        r = decodeSpd(b); const unsigned bad = scenario == 4 ? 2 : 0;
        assert(!r.xmp.profiles[bad].cas.captured && !r.xmp.profiles[bad].commandRate.captured);
        if (scenario != 3) assert(r.xmp.profiles[bad == 2 ? 0 : 1].cas.decoded);
        assert(r.cas.decoded);
    }
    b = spdFixture(); b[642] = 0x31; spdSeal(b,640,62); r = decodeSpd(b);
    assert(r.xmp.profiles.empty() && r.cas.decoded);
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
        assert(!r.error && !r.xmp.error && r.xmp.profiles[0].values.size() == 30);
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
    xmpSecondaryValues(); xmpSecondaryByteBoundaries(); xmpSecondaryZerosAndMaximums(); xmpSecondaryIndependentRequirements();
    baseCasBitmaps(); xmpCasBitmaps(); casCombinedBitsAndTimingIndependence(); xmpCommandRateCodes(); casAndCommandCaptureGates();
    ddr4BaseWriteDelays(); ddr5BaseSecondaryValues(); ddr5BaseTimingByteBoundaries();
    baseTimingRevisionAndTimebaseGates(); baseTimingCaptureAndSectionIsolation();
    basePublicTimingExamples();
    std::cout << "32 SPD XMP/EXPO scenario groups passed (capture lengths, CRC, base timings, revision gates, CAS and profiles)\n";
}
