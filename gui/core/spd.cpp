// SPDX-License-Identifier: GPL-2.0-only
#include "spd.h"
#include <algorithm>
#include <cerrno>
#include <iomanip>
#include <sstream>

namespace octool { namespace core {
namespace {
unsigned word(const std::vector<std::uint8_t> &b, unsigned offset) {
    return unsigned(b[offset]) | (unsigned(b[offset + 1]) << 8);
}
enum class CasEncoding { Ddr4Base, Xmp2, Ddr5 };
SpdCas casBitmap(const std::vector<std::uint8_t> &b, unsigned offset, CasEncoding encoding, bool revisionKnown) {
    SpdCas out;
    const bool ddr5 = encoding == CasEncoding::Ddr5, xmp2 = encoding == CasEncoding::Xmp2;
    const unsigned size = ddr5 ? 5 : 4;
    if (offset > b.size() || b.size() - offset < size) return out;
    out.captured = true; out.offset = offset;
    out.raw.assign(b.begin() + offset, b.begin() + offset + size);
    if (!revisionKnown) return out;
    out.decoded = true;
    out.encoding = ddr5 ? "ddr5-cl20-98" : xmp2 ? "xmp2-cl7-24" : "ddr4-base-range";
    out.reservedBitsSet = xmp2 ? (b[offset+2] & 0xfc) != 0 || b[offset+3] != 0 :
        !ddr5 && (b[offset + 3] & 0x40) != 0;
    const unsigned first = ddr5 ? 20 : !xmp2 && (b[offset + 3] & 0x80) ? 23 : 7;
    const unsigned bits = ddr5 ? 40 : xmp2 ? 18 : 30, step = ddr5 ? 2 : 1;
    for (unsigned bit = 0; bit < bits; ++bit)
        if (b[offset + bit / 8] & (1u << (bit % 8))) out.latencies.push_back(first + step * bit);
    return out;
}
SpdCrc blockCrc(const std::vector<std::uint8_t> &b, unsigned offset, unsigned count = 62) {
    SpdCrc crc;
    crc.checked = true;
    crc.stored = word(b, offset + count);
    crc.computed = spdCrc16(b.data() + offset, count);
    crc.valid = crc.stored == crc.computed;
    return crc;
}
void profileName(const std::vector<std::uint8_t> &b, unsigned offset, SpdXmpProfile &p) {
    p.nameSupported = true;
    p.nameValid = true;
    for (unsigned i = 0; i < 16 && b[offset + i]; ++i) {
        const auto c = b[offset + i];
        if (c < 32 || c > 126) { p.nameValid = false; p.name.clear(); return; }
        p.name += char(c);
    }
    while (!p.name.empty() && p.name.back() == ' ') p.name.pop_back();
}
SpdXmp decodeXmp2(const std::vector<std::uint8_t> &b) {
    SpdXmp out;
    if (b.size() < 386) return out;
    out.inspected = true;
    out.present = b[384] == 0x0c && b[385] == 0x4a;
    if (!out.present) return out;
    if (b.size() < 393) { out.error = -EMSGSIZE; return out; }
    out.headerCaptured = true;
    out.raw.assign(b.begin() + 384, b.begin() + 393);
    out.revision = b[387]; out.configurationRaw = b[386];
    if (out.revision != 0x20) { out.error = -ENOTSUP; return out; }
    out.enabledMask = out.configurationRaw & 3;
    for (unsigned i = 0; i < 2; ++i) {
        SpdXmpProfile p;
        p.index = i + 1; p.offset = 393 + 47 * i;
        p.enabled = (out.enabledMask & (1u << i)) != 0;
        p.timebaseRaw = b[388 + i];
        if (p.enabled) p.recommendedDimmsPerChannel = 1 + ((out.configurationRaw >> (2 + 2*i)) & 3);
        if (b.size() >= p.offset + 47)
            p.raw.assign(b.begin() + p.offset, b.begin() + p.offset + 47);
        if (p.enabled) {
            if (p.raw.empty()) p.error = -EMSGSIZE;
            else if (p.timebaseRaw & 15) p.error = -ENOTSUP;
            else {
                const unsigned off = p.offset, voltage = b[off];
                p.values.push_back({"VDD", "mV", (voltage >> 7) * 1000 + (voltage & 127) * 10});
                // XMP 2.0 uses its own timebase bytes, not base SPD byte 17.
                // Fine offsets are signed two's complement. Byte 427 belongs
                // to tRC, while tRAS has no fine adjustment.
                const char *names[] = {"tCK minimum", "tAA minimum", "tRCD minimum", "tRP minimum",
                    "tRAS minimum", "tRC minimum", "tRFC1 minimum", "tRFC2 minimum", "tRFC4 minimum",
                    "tFAW minimum", "tRRD_S minimum", "tRRD_L minimum"};
                const unsigned coarse[] = {b[off+3], b[off+8], b[off+9], b[off+10],
                    (unsigned(b[off+11]&15)<<8)|b[off+12], (unsigned(b[off+11]>>4)<<8)|b[off+13],
                    word(b,off+14), word(b,off+16), word(b,off+18),
                    (unsigned(b[off+20]&15)<<8)|b[off+21], b[off+22], b[off+23]};
                const unsigned fine[] = {38,37,36,35,0,34,0,0,0,0,33,32};
                for (unsigned t = 0; t < 12; ++t) {
                    const unsigned code = fine[t] ? b[off+fine[t]] : 0;
                    const int adjustment = code < 128 ? int(code) : int(code)-256;
                    const int ps = int(coarse[t])*125 + adjustment;
                    if (ps < 0 || (t == 0 && ps == 0)) { p.error = -EINVAL; break; }
                    p.values.push_back({names[t], "ps", unsigned(ps)});
                }
                if (p.error) p.values.clear();
                else p.cas = casBitmap(b, off + 4, CasEncoding::Xmp2, true);
            }
        }
        // Base CRC does not cover any XMP 2.0 byte. Never mark these profiles
        // CRC-verified or infer enablement from nonzero payload data.
        if (!out.error && p.error) out.error = p.error;
        out.profiles.push_back(p);
    }
    return out;
}
void decodeXmp3Values(const std::vector<std::uint8_t> &b, SpdXmpProfile &p) {
    p.crc = blockCrc(b, p.offset);
    if (!p.crc.valid) { p.error = -EILSEQ; return; }
    if (!word(b, p.offset + 5)) { p.error = -EINVAL; return; }
    const char *rails[] = {"VPP", "VDD", "VDDQ", "Memory controller voltage"};
    const unsigned voltageOffsets[] = {0, 1, 2, 4};
    for (unsigned v = 0; v < 4; ++v) {
        const unsigned code = b[p.offset + voltageOffsets[v]];
        p.values.push_back({rails[v], "mV", (code >> 5) * 1000 + (code & 31) * 50});
    }
    // Explicit little-endian loads: no packed-struct casts or
    // host-endianness assumptions. Refresh timings use ns.
    const char *names[] = {"tCK minimum", "tAA minimum", "tRCD minimum", "tRP minimum",
        "tRAS minimum", "tRC minimum", "tWR minimum", "tRFC1 minimum", "tRFC2 minimum", "tRFCsb minimum"};
    const unsigned offsets[] = {5, 13, 15, 17, 19, 21, 23, 25, 27, 29};
    for (unsigned t = 0; t < 10; ++t)
        p.values.push_back({names[t], t >= 7 ? "ns" : "ps", word(b, p.offset + offsets[t])});
    // Each secondary timing stores two independent requirements:
    // a little-endian time in ps and a one-byte lower limit in
    // clock cycles. Neither is a measured or derived cycle count.
    const char *secondary[] = {"tRRD_L", "tCCD_L_WR", "tCCD_L_WR2", "tCCD_L_WTR",
        "tCCD_S_WTR", "tCCD_L", "tRTP", "tFAW"};
    for (unsigned t = 0; t < 8; ++t) {
        const unsigned off = p.offset + 31 + 3 * t;
        p.values.push_back({std::string(secondary[t]) + " minimum", "ps", word(b, off)});
        p.values.push_back({std::string(secondary[t]) + " cycle lower limit", "nCK", b[off + 2]});
    }
    p.cas = casBitmap(b, p.offset + 7, CasEncoding::Ddr5, true);
    p.commandRate.captured = true;
    p.commandRate.raw = b[p.offset + 60];
    p.commandRate.code = p.commandRate.raw & 15;
    p.commandRate.recognized = p.commandRate.code <= 3;
}
SpdXmp decodeXmp3(const std::vector<std::uint8_t> &b) {
    SpdXmp out;
    out.crcSupported = true;
    // A short base-only capture cannot establish absence of an extension.
    if (b.size() < 642) return out;
    out.inspected = true;
    out.present = b[640] == 0x0c && b[641] == 0x4a;
    if (b.size() >= 836) {
        out.expoInspected = true;
        out.expoPresent = b[832] == 'E' && b[833] == 'X' && b[834] == 'P' && b[835] == 'O';
    }
    if (!out.present) return out;
    if (b.size() < 704) { out.error = -EMSGSIZE; return out; }
    out.headerCaptured = true;
    out.raw.assign(b.begin() + 640, b.begin() + 704);
    out.revision = b[642];
    out.configurationRaw = b[643];
    out.crc = blockCrc(b, 640);
    if (!out.crc.valid) { out.error = -EILSEQ; return out; }
    if (out.revision != 0x30) { out.error = -ENOTSUP; return out; }
    out.enabledMask = b[643] & 7;
    for (unsigned i = 0; i < 3; ++i) {
        SpdXmpProfile p;
        p.index = i + 1; p.offset = 704 + 64 * i;
        p.enabled = (out.enabledMask & (1u << i)) != 0;
        p.blockedByExpo = i == 2 && out.expoPresent;
        profileName(b, 654 + 16 * i, p);
        if (p.enabled) {
            if (p.blockedByExpo) p.error = -EINVAL;
            else if (b.size() < p.offset + 64) p.error = -EMSGSIZE;
            else {
                p.raw.assign(b.begin() + p.offset, b.begin() + p.offset + 64);
                decodeXmp3Values(b, p);
            }
        }
        if (!out.error && p.error) out.error = p.error;
        out.profiles.push_back(p);
    }
    // Optional user storage is independent of the three manufacturer enable
    // bits. Missing bytes cannot establish absence; blank patterns are only
    // reported as observations. CRC zero can also belong to nonzero data.
    for (unsigned i = 0; i < 2; ++i) {
        SpdXmpProfile p;
        p.index = i + 1; p.offset = 896 + 64 * i; p.enableKnown = false;
        p.blockedByExpo = i == 0 && out.expoPresent;
        if (b.size() > p.offset)
            p.raw.assign(b.begin() + p.offset, b.begin() + std::min(b.size(), std::size_t(p.offset + 64)));
        if (p.blockedByExpo) p.storageState = "overlap_expo";
        else if (p.raw.empty()) p.storageState = "not_captured";
        else if (p.raw.size() != 64) p.storageState = "incomplete";
        else if (std::all_of(p.raw.begin(), p.raw.end(), [](std::uint8_t v) { return v == 0; }))
            p.storageState = "all_zero";
        else if (std::all_of(p.raw.begin(), p.raw.end(), [](std::uint8_t v) { return v == 255; }))
            p.storageState = "all_ff";
        else {
            p.storageState = "stored";
            decodeXmp3Values(b, p);
        }
        // An uncaptured optional slot is informational, not a failed enabled
        // profile. Complete nonblank data must pass the same CRC/value checks.
        if (!out.error && p.error) out.error = p.error;
        out.userProfiles.push_back(p);
    }
    return out;
}
SpdExpo decodeExpo(const std::vector<std::uint8_t> &b) {
    SpdExpo out;
    if (b.size() < 836) return out;
    out.inspected = true;
    out.present = b[832] == 'E' && b[833] == 'X' && b[834] == 'P' && b[835] == 'O';
    if (!out.present) return out;
    if (b.size() < 960) { out.error = -EMSGSIZE; return out; }
    out.raw.assign(b.begin() + 832, b.begin() + 960);
    out.revision = b[836]; out.configurationRaw = b[837]; out.featuresRaw = b[838];
    out.crc = blockCrc(b, 832, 126);
    if (!out.crc.valid) { out.error = -EILSEQ; return out; }
    if (out.revision != 0x10) { out.error = -ENOTSUP; return out; }
    for (unsigned i = 0; i < 2; ++i) {
        SpdExpoProfile p;
        p.index = i + 1; p.offset = 842 + 40 * i;
        p.enabled = (out.configurationRaw & (1u << (4 * i))) != 0;
        p.raw.assign(b.begin() + p.offset, b.begin() + p.offset + 40);
        // Disabled slots may contain stale data. Neither nonzero bytes nor a
        // nonzero computed CRC establishes that a profile is enabled.
        if (p.enabled) {
            if (!word(b, p.offset + 4)) p.error = -EINVAL;
            else {
                const char *rails[] = {"VDD", "VDDQ", "VPP"};
                for (unsigned v = 0; v < 3; ++v) {
                    const unsigned code = b[p.offset + v];
                    p.values.push_back({rails[v], "mV", (code >> 5) * 1000 + (code & 31) * 50});
                }
                const char *names[] = {"tCK minimum", "tAA minimum", "tRCD minimum", "tRP minimum",
                    "tRAS minimum", "tRC minimum", "tWR minimum", "tRFC1 minimum", "tRFC2 minimum", "tRFCsb minimum"};
                for (unsigned t = 0; t < 10; ++t)
                    p.values.push_back({names[t], t >= 7 ? "ns" : "ps", word(b, p.offset + 4 + 2 * t)});
                // These words have an established layout, but the feature-byte
                // enable bits do not. Keep them separate from basic timings.
                const char *enhanced[] = {"tRRD_L", "tCCD_L", "tCCD_L_WR", "tCCD_L_WR2",
                    "tFAW", "tCCD_L_WTR", "tCCD_S_WTR", "tRTP"};
                for (unsigned t = 0; t < 8; ++t)
                    p.storedEnhancedTimings.push_back({std::string(enhanced[t]) + " minimum", "ps", word(b, p.offset + 24 + 2*t)});
            }
        }
        if (!out.error && p.error) out.error = p.error;
        out.profiles.push_back(p);
    }
    return out;
}
}
std::uint16_t spdCrc16(const std::uint8_t *bytes, std::size_t length) {
    std::uint16_t crc = 0;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= std::uint16_t(unsigned(bytes[i]) << 8);
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = std::uint16_t((unsigned(crc) << 1) ^ ((crc & 0x8000) ? 0x1021u : 0u));
    }
    return crc;
}
SpdSnapshot decodeSpd(const std::vector<std::uint8_t> &b) {
    SpdSnapshot out;
    if (b.size() < 3 || b.size() > 4096) { out.error = -EINVAL; return out; }
    out.memoryType = b[2];
    if (b[2] != 0x0c && b[2] != 0x12) { out.error = -ENOTSUP; return out; }
    const bool ddr5 = b[2] == 0x12;
    if (b.size() < (ddr5 ? 236u : 128u)) { out.error = -EMSGSIZE; return out; }
    auto add = [&out](const std::string &name, const std::string &value) { out.fields.push_back({name, value}); };
    auto number = [&add](const std::string &name, std::uint64_t value) { add(name, std::to_string(value)); };
    add("Memory type", ddr5 ? "DDR5" : "DDR4");
    if (!ddr5 || b.size() >= 512) {
        out.crcChecked = true;
        const unsigned end = ddr5 ? 510 : 126;
        out.crcValid = spdCrc16(b.data(), end) == unsigned(b[end] | unsigned(b[end+1]) << 8);
        if (!out.crcValid) { out.error = -EILSEQ; return out; }
    }
    number("SPD revision (encoded)", b[1]);
    out.cas = casBitmap(b, ddr5 ? 24 : 20, ddr5 ? CasEncoding::Ddr5 : CasEncoding::Ddr4Base, (b[1] >> 4) == 1);
    const unsigned type = b[3] & 15;
    add("Module type", type == 1 ? "RDIMM" : type == 2 ? "UDIMM" : type == 3 ? "SO-DIMM" : type == 4 ? "LRDIMM" : "Encoding " + std::to_string(type));
    const unsigned ranks = 1 + ((b[ddr5 ? 234 : 12] >> 3) & 7);
    // DDR5 I/O width is byte 6 bits 7:5, unlike DDR4's low three bits.
    // Advantech AQD-SD5V16GE48-SB: byte 6 = 0x20 describes x8 devices.
    const unsigned widthCode = ddr5 ? b[6] >> 5 : b[12] & 7;
    const unsigned busCode = b[ddr5 ? 235 : 13] & 7;
    const bool baseLayout = (b[1] >> 4) == 1;
    const bool moduleLayout = baseLayout && (!ddr5 || (b[192] >> 4) == 1);
    if (moduleLayout) number(ddr5 ? "Package ranks per channel" : "Package ranks", ranks);
    if (baseLayout && widthCode <= 3) number("SDRAM device width (bits)", 4u << widthCode);
    if (moduleLayout && busCode <= 3) number(ddr5 ? "Primary bus width per channel (bits)" : "Primary bus width (bits)", 8u << busCode);
    if (!ddr5) {
        number("Bus extension (encoded)", (b[13] >> 3) & 3);
        static const unsigned densities[] = {256,512,1024,2048,4096,8192,16384,32768,12288,24576};
        const unsigned density = b[4] & 15;
        if (baseLayout && density < 10) number("SDRAM density per die (Mbit)", densities[density]);
        number("SDRAM package type (encoded)", b[6] >> 7);
        number("Package die count (encoded)", (b[6] >> 4) & 7);
        const unsigned dies = 1 + ((b[6] >> 4) & 7), loading = b[6] & 3;
        if (baseLayout) number("Package die count", dies);
        number("Signal loading (encoded)", b[6] & 3);
        if (moduleLayout) add("Rank organization", (b[12] & 0x40) ? "Asymmetric" : "Symmetric");
        const bool monolithic = !(b[6] & 0x80), stacked = !monolithic && dies >= 2 && (loading == 1 || loading == 2);
        if (baseLayout) add("SDRAM package", monolithic ? "Monolithic" : loading == 1 ? "Multi-load stack" : loading == 2 ? "3DS" : "Unknown signal loading");
        const bool firstValid = !(b[6] & 12) && ((monolithic && dies == 1 && loading == 0) || stacked);
        const bool asymmetric = moduleLayout && (b[12] & 0x40);
        // Multi-load stacks already express their additional ranks in byte 12.
        // Single-load 3DS packages instead multiply by the number of dies.
        if (moduleLayout && !asymmetric && firstValid &&
            density < 10 && widthCode <= 3 && busCode <= 3 && busCode+1 >= widthCode) {
            number("Module capacity (MiB)", densities[density]/8 * ((8u << busCode)/(4u << widthCode)) * ranks * (stacked && loading == 2 ? dies : 1));
        }
        if (asymmetric) {
            const unsigned secondDies = 1 + ((b[10] >> 4) & 7), secondLoading = b[10] & 3;
            const unsigned ratio = (b[10] >> 2) & 3;
            const bool secondMono = !(b[10] & 0x80);
            const bool secondValid = (secondMono && secondDies == 1 && secondLoading == 0) ||
                (!secondMono && secondDies >= 2 && (secondLoading == 1 || secondLoading == 2));
            number("Second SDRAM package type (encoded)", b[10] >> 7);
            number("Second package die count (encoded)", (b[10] >> 4) & 7);
            number("Second signal loading (encoded)", secondLoading);
            number("SDRAM density ratio (encoded)", ratio);
            if (secondValid) {
                number("Second package die count", secondDies);
                add("Second SDRAM package", secondMono ? "Monolithic" : secondLoading == 1 ? "Multi-load stack" : "3DS");
            }
            // JEDEC 21-C Annex L DDR4 release 27A, table 26. Density steps
            // are NOT shifts or decrements of the byte-4 encoding: 16->12->8,
            // 32->24->16, 12->8->4 and 24->16->12 Gbit are explicit entries.
            static const unsigned smaller[][2] = {{0,0},{256,0},{512,256},{1024,512},{2048,1024},
                {4096,2048},{12288,8192},{24576,16384},{8192,4096},{16384,12288}};
            const unsigned secondDensity = density < 10 && ratio < 3 ?
                (ratio ? smaller[density][ratio-1] : densities[density]) : 0;
            if (secondDensity) number("Second SDRAM density per die (Mbit)", secondDensity);
            unsigned firstRanks = 0, secondRanks = 0;
            if (firstValid && secondValid) {
                // Tables 25/30/31: paired SDP density groups, paired 3DS
                // stacks, and three-rank SDP+DDP in either order. A DDP has
                // two chip-select loads; SDP+DDP is 1+2, NOT ceil(3/2)+1.
                const bool firstDdp = stacked && loading == 1 && dies == 2;
                const bool secondDdp = !secondMono && secondLoading == 1 && secondDies == 2;
                if ((monolithic && secondMono && (ranks == 2 || ranks == 4)) ||
                    (loading == 2 && secondLoading == 2 && ranks == 2) ||
                    (firstDdp && secondDdp && ranks == 4)) firstRanks = secondRanks = ranks/2;
                else if (ranks == 3 && monolithic && secondDdp) { firstRanks = 1; secondRanks = 2; }
                else if (ranks == 3 && firstDdp && secondMono) { firstRanks = 2; secondRanks = 1; }
            }
            if (firstRanks && secondDensity && widthCode <= 3 && busCode <= 3 && busCode+1 >= widthCode) {
                const unsigned firstLogical = firstRanks * (loading == 2 ? dies : 1);
                const unsigned secondLogical = secondRanks * (secondLoading == 2 ? secondDies : 1);
                const std::uint64_t scale = (8u << busCode)/(4u << widthCode);
                const std::uint64_t first = std::uint64_t(densities[density])/8 * scale * firstLogical;
                const std::uint64_t second = std::uint64_t(secondDensity)/8 * scale * secondLogical;
                number("First geometry package ranks", firstRanks); number("Second geometry package ranks", secondRanks);
                number("First geometry capacity rank factor", firstLogical); number("Second geometry capacity rank factor", secondLogical);
                number("First geometry capacity (MiB)", first); number("Second geometry capacity (MiB)", second);
                number("Module capacity (MiB)", first+second);
            } else add("Module capacity status", "Not decoded: invalid or unsupported DDR4 rank geometry");
        }
        if ((b[1] >> 4) == 1 && !(b[17] & 15)) {
            // DDR4's defined time bases are 125 ps and a signed 1 ps
            // adjustment. Do not treat the fine byte as unsigned.
            auto timing = [&](const char *name, unsigned coarse, unsigned fine) {
                const int adjustment = fine ? (b[fine] < 128 ? int(b[fine]) : int(b[fine])-256) : 0;
                const int picoseconds = int(coarse)*125 + adjustment;
                if (coarse && picoseconds > 0) number(name, unsigned(picoseconds));
            };
            timing("SPD tCK minimum (ps)",b[18],125); timing("SPD tCK maximum (ps)",b[19],124);
            timing("SPD tAA minimum (ps)",b[24],123); timing("SPD tRCD minimum (ps)",b[25],122);
            timing("SPD tRP minimum (ps)",b[26],121);
            timing("SPD tRAS minimum (ps)",((b[27]&15)<<8)|b[28],0);
            timing("SPD tRC minimum (ps)",(unsigned(b[27]>>4)<<8)|b[29],120);
            timing("SPD tRFC1 minimum (ps)",unsigned(b[30])|(unsigned(b[31])<<8),0);
            timing("SPD tRFC2 minimum (ps)",unsigned(b[32])|(unsigned(b[33])<<8),0);
            timing("SPD tRFC4 minimum (ps)",unsigned(b[34])|(unsigned(b[35])<<8),0);
            timing("SPD tFAW minimum (ps)",((b[36]&15)<<8)|b[37],0);
            timing("SPD tRRD_S minimum (ps)",b[38],119); timing("SPD tRRD_L minimum (ps)",b[39],118);
            timing("SPD tCCD_L minimum (ps)",b[40],117);
            // These three timings have 12-bit MTB counts and no fine offset.
            // Byte 41's high nibble is reserved; byte 43 shares two timings.
            number("SPD tWR minimum (ps)", (((unsigned(b[41]) & 15) << 8) | b[42]) * 125);
            number("SPD tWTR_S minimum (ps)", (((unsigned(b[43]) & 15) << 8) | b[44]) * 125);
            number("SPD tWTR_L minimum (ps)", ((unsigned(b[43] >> 4) << 8) | b[45]) * 125);
        }
    } else {
        static const unsigned densities[] = {0, 4, 8, 12, 16, 24, 32, 48, 64};
        const unsigned density = b[4] & 31;
        if (baseLayout && density < sizeof(densities)/sizeof(densities[0]) && densities[density]) number("First SDRAM density per die (Gbit)", densities[density]);
        const unsigned package = b[4] >> 5, channelCode = b[235] >> 5;
        const bool asymmetric = moduleLayout && (b[234] & 0x40);
        const unsigned secondDensity = b[8] & 31, secondPackage = b[8] >> 5, secondWidth = b[10] >> 5;
        const unsigned dies[] = {1,2,2,4,8,16};
        const char *packages[] = {"Monolithic", "Dual-die package", "2H 3DS", "4H 3DS", "8H 3DS", "16H 3DS"};
        number("Package die count (encoded)", package);
        if (baseLayout && package < 6) { number("Package die count", dies[package]); add("SDRAM package", packages[package]); }
        number("Module geometry revision (encoded)", b[192]);
        if (moduleLayout) add("Rank organization", (b[234] & 0x40) ? "Asymmetric" : "Symmetric");
        else add("Module geometry", "Unsupported base or common revision");
        // The channel field is an exponent (1,2,4,8), not an additive count.
        // Codes 4..7 are reserved, so no capacity may be inferred from them.
        const unsigned channels = channelCode < 4 ? 1u << channelCode : 0, extension = (b[235] >> 3) & 3;
        number("Subchannels per module (encoded)", channelCode);
        if (moduleLayout && channels) number("Subchannels per module", channels);
        if (moduleLayout && extension <= 2) number("Bus extension per subchannel (bits)", extension*4);
        if (asymmetric) {
            number("Second SDRAM density (encoded)", secondDensity);
            number("Second package die count (encoded)", secondPackage);
            number("Second SDRAM device width (encoded)", secondWidth);
            if (secondDensity && secondDensity < 9) number("Second SDRAM density per die (Gbit)", densities[secondDensity]);
            if (secondPackage < 6) {
                number("Second package die count", dies[secondPackage]); add("Second SDRAM package", packages[secondPackage]);
            }
            if (secondWidth <= 3) number("Second SDRAM device width (bits)", 4u << secondWidth);
        }
        // DDP ranks already count the separate loads. 3DS logical ranks also
        // include the dies per package. On asymmetric DDR5 modules, the first
        // geometry describes even ranks and the second describes odd ranks.
        // Each geometry uses its OWN device width and die multiplier.
        const bool geometryValid = moduleLayout && channels && package < 6 && density && density < 9 &&
            widthCode <= 3 && busCode <= 3 && busCode+1 >= widthCode &&
            (!asymmetric || (ranks >= 2 && secondPackage < 6 && secondDensity && secondDensity < 9 &&
                             secondWidth <= 3 && busCode+1 >= secondWidth));
        if (geometryValid) {
            const unsigned firstRanks = asymmetric ? (ranks+1)/2 : ranks, secondRanks = asymmetric ? ranks/2 : 0;
            const std::uint64_t first = std::uint64_t(densities[density])*128 * ((8u << busCode)/(4u << widthCode)) *
                firstRanks * channels * (package >= 2 ? dies[package] : 1);
            const std::uint64_t second = asymmetric ? std::uint64_t(densities[secondDensity])*128 *
                ((8u << busCode)/(4u << secondWidth)) * secondRanks * channels * (secondPackage >= 2 ? dies[secondPackage] : 1) : 0;
            if (asymmetric) {
                number("First geometry package ranks per channel (even)", firstRanks);
                number("Second geometry package ranks per channel (odd)", secondRanks);
                number("First geometry capacity (MiB)", first); number("Second geometry capacity (MiB)", second);
            }
            number("Module capacity (MiB)", first+second);
        } else if (asymmetric) add("Module capacity status", "Not decoded: invalid or unsupported rank geometry");
        // JEDEC base timing requirements stored in SPD, not measured timings
        // or a decoded XMP/EXPO profile. Only the defined revision-1 layout.
        if ((b[1] >> 4) == 1) {
            const char *names[] = {"SPD tCK minimum (ps)", "SPD tCK maximum (ps)",
                "SPD tAA minimum (ps)", "SPD tRCD minimum (ps)", "SPD tRP minimum (ps)",
                "SPD tRAS minimum (ps)", "SPD tRC minimum (ps)", "SPD tWR minimum (ps)",
                "SPD tRFC1 minimum (ns)", "SPD tRFC2 minimum (ns)", "SPD tRFCsb minimum (ns)"};
            const unsigned offsets[] = {20,22,30,32,34,36,38,40,42,44,46};
            for (unsigned i = 0; i < sizeof(offsets)/sizeof(offsets[0]); ++i) {
                const unsigned off = offsets[i], value = unsigned(b[off]) | (unsigned(b[off+1]) << 8);
                if (value) number(names[i], value);
            }
            if ((package >= 2 && package <= 5) || (asymmetric && secondPackage >= 2 && secondPackage <= 5)) {
                number("SPD tRFC1 different logical rank minimum (ns)", word(b,48));
                number("SPD tRFC2 different logical rank minimum (ns)", word(b,50));
                number("SPD tRFCsb different logical rank minimum (ns)", word(b,52));
            }
            // Base SPD has a different order and offset from XMP. Keep the
            // stored ps and nCK requirements separate, including zero values.
            // The final three pairs first appeared in additive revision 1.2.
            const char *secondary[] = {"tRRD_L", "tCCD_L", "tCCD_L_WR", "tCCD_L_WR2",
                "tFAW", "tCCD_L_WTR", "tCCD_S_WTR", "tRTP", "tCCD_M", "tCCD_M_WR", "tCCD_M_WTR"};
            const unsigned count = (b[1] & 15) >= 2 ? 11 : 8;
            for (unsigned i = 0; i < count; ++i) {
                const unsigned off = 70 + 3 * i;
                number("SPD " + std::string(secondary[i]) + " minimum (ps)", word(b, off));
                number("SPD " + std::string(secondary[i]) + " cycle lower limit (nCK)", b[off+2]);
            }
        }
    }
    const unsigned part = ddr5 ? 521 : 329, length = ddr5 ? 30 : 20, serial = ddr5 ? 517 : 325;
    if (b.size() >= part+length) {
        std::string text;
        for (unsigned i = part; i < part+length; ++i) text += b[i] >= 32 && b[i] < 127 ? char(b[i]) : '?';
        while (!text.empty() && text.back() == ' ') text.pop_back();
        add("Part number", text);
    }
    if (b.size() >= serial+4) {
        std::ostringstream hex; hex << std::hex << std::setfill('0');
        for (unsigned i = serial; i < serial+4; ++i) hex << std::setw(2) << unsigned(b[i]);
        add("Serial number (hex)", hex.str());
    }
    if (ddr5) { out.xmp = decodeXmp3(b); out.expo = decodeExpo(b); }
    else out.xmp = decodeXmp2(b);
    return out;
}
} }
