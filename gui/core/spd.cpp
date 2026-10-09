// SPDX-License-Identifier: GPL-2.0-only
#include "spd.h"
#include <cerrno>
#include <iomanip>
#include <sstream>

namespace octool { namespace core {
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
    auto number = [&add](const char *name, unsigned value) { add(name, std::to_string(value)); };
    add("Memory type", ddr5 ? "DDR5" : "DDR4");
    if (!ddr5 || b.size() >= 512) {
        out.crcChecked = true;
        const unsigned end = ddr5 ? 510 : 126;
        out.crcValid = spdCrc16(b.data(), end) == unsigned(b[end] | unsigned(b[end+1]) << 8);
        if (!out.crcValid) { out.error = -EILSEQ; return out; }
    }
    number("SPD revision (encoded)", b[1]);
    const unsigned type = b[3] & 15;
    add("Module type", type == 1 ? "RDIMM" : type == 2 ? "UDIMM" : type == 3 ? "SO-DIMM" : type == 4 ? "LRDIMM" : "Encoding " + std::to_string(type));
    const unsigned ranks = 1 + ((b[ddr5 ? 234 : 12] >> 3) & 7);
    // DDR5 I/O width is byte 6 bits 7:5, unlike DDR4's low three bits.
    // Advantech AQD-SD5V16GE48-SB: byte 6 = 0x20 describes x8 devices.
    const unsigned widthCode = ddr5 ? b[6] >> 5 : b[12] & 7;
    const unsigned busCode = b[ddr5 ? 235 : 13] & 7;
    number(ddr5 ? "Package ranks per channel" : "Package ranks", ranks);
    if (widthCode <= 3) number("SDRAM device width (bits)", 4u << widthCode);
    if (busCode <= 3) number(ddr5 ? "Primary bus width per channel (bits)" : "Primary bus width (bits)", 8u << busCode);
    if (!ddr5) {
        number("Bus extension (encoded)", (b[13] >> 3) & 3);
        // Only symmetric monolithic packages. 3DS/asymmetric module capacity
        // cannot be obtained from the simple rank multiplier.
        if (!(b[6] & 0x80) && !(b[12] & 0x40) && (b[4] & 15) <= 7 && widthCode <= 3 && busCode <= 3 && busCode+1 >= widthCode) {
            const unsigned density = 256u << (b[4] & 15);
            number("Module capacity (MiB)", density/8 * ((8u << busCode)/(4u << widthCode)) * ranks);
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
        }
    } else {
        static const unsigned densities[] = {0, 4, 8, 12, 16, 24, 32, 48, 64};
        const unsigned density = b[4] & 31;
        if (density < sizeof(densities)/sizeof(densities[0]) && densities[density]) number("First SDRAM density per die (Gbit)", densities[density]);
        number("Package die count (encoded)", b[4] >> 5);
        const unsigned channels = 1 + (b[235] >> 5), extension = (b[235] >> 3) & 3;
        number("Subchannels per module", channels);
        if (extension <= 2) number("Bus extension per subchannel (bits)", extension*4);
        // Restrict capacity to symmetric, monolithic packages. Mixed-density
        // ranks and stacked packages need a different capacity calculation.
        if (!(b[234] & 0x40) && !(b[4] & 0xe0) && density < 9 && densities[density] &&
            widthCode <= 3 && busCode <= 3 && busCode+1 >= widthCode) {
            number("Module capacity (MiB)", densities[density]*128 * ((8u << busCode)/(4u << widthCode)) * ranks * channels);
        }
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
    return out;
}
} }
