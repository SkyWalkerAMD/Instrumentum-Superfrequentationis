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
    if (!ddr5) {
        out.crcChecked = true;
        out.crcValid = spdCrc16(b.data(), 126) == unsigned(b[126] | unsigned(b[127]) << 8);
        if (!out.crcValid) { out.error = -EILSEQ; return out; }
    }
    number("SPD revision (encoded)", b[1]);
    const unsigned type = b[3] & 15;
    add("Module type", type == 1 ? "RDIMM" : type == 2 ? "UDIMM" : type == 3 ? "SO-DIMM" : type == 4 ? "LRDIMM" : "Encoding " + std::to_string(type));
    const unsigned ranks = 1 + ((b[ddr5 ? 234 : 12] >> 3) & 7);
    const unsigned widthCode = b[ddr5 ? 6 : 12] & 7, busCode = b[ddr5 ? 235 : 13] & 7;
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
    } else {
        static const unsigned densities[] = {0, 4, 8, 12, 16, 24, 32, 48, 64};
        const unsigned density = b[4] & 31;
        if (density < sizeof(densities)/sizeof(densities[0]) && densities[density]) number("First SDRAM density per die (Gbit)", densities[density]);
        number("Package die count (encoded)", b[4] >> 5);
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
