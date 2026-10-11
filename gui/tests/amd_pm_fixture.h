// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../core/amd_pm.h"
#include <cerrno>
#include <map>
#include <string>
#include <vector>
namespace pm_fixture {
inline void put(std::vector<std::uint8_t> &b, unsigned offset, std::uint32_t word) {
    for (unsigned i = 0; i < 4; ++i) b.at(offset + i) = std::uint8_t(word >> (8 * i));
}
inline std::vector<std::uint8_t> bytes(std::size_t size = 2844) {
    std::vector<std::uint8_t> b(size);
    // Independent, pinned IEEE-754 words. They are synthetic, not a dump from
    // any machine. Do not generate expected results with the production codec.
    const std::uint32_t words[][2] = {
        {0x20c,0x44fa0000}, {0x21c,0x44e10000}, {0x22c,0x45610000},
        {0x5cc,0x3f700000}, {0x714,0x3f500000}, {0x71c,0x3f480000},
        {0x178,0x3fa00000}, {0x17c,0x3f99999a}, {0x180,0x42c80000}, {0x188,0x428c0000},
        {0x18c,0x3fa66666}, {0x190,0x3fa40000}, {0x194,0x42f00000}, {0x19c,0x42900000},
        {0x1a0,0x3f8ccccd}, {0x1a4,0x3f880000}, {0x1a8,0x41a00000}, {0x1b0,0x42700000},
        {0x1b4,0x3f666666}, {0x1b8,0x3f600000}, {0x1bc,0x40800000}, {0x1c4,0x42480000}};
    for (const auto &entry : words) put(b, entry[0], entry[1]);
    for (unsigned r = 0; r < 4; ++r) put(b, 0x184 + 20 * r, 0x7fc00000); // unmapped second-current words
    return b;
}
inline std::vector<std::uint8_t> le(std::uint64_t n, unsigned width) {
    std::vector<std::uint8_t> b(width);
    for (unsigned i = 0; i < width; ++i) b[i] = std::uint8_t(n >> (8 * i));
    return b;
}
struct Driver {
    std::map<std::string, std::vector<std::uint8_t>> files;
    std::vector<std::string> calls;
    unsigned failCall = 0, mutateAfter = 0;
    Driver() : files{{"pm_table_version", le(0x730204, 4)}, {"pm_table_size", le(2844, 8)}, {"pm_table", bytes()}} {}
    int read(const std::string &name, std::vector<std::uint8_t> &b, std::size_t maximum) {
        calls.push_back(name); b.clear();
        if (failCall == calls.size()) return -EACCES;
        if (mutateAfter == calls.size()) files["pm_table_version"] = le(0x730404, 4);
        const auto found = files.find(name); if (found == files.end()) return -ENOENT;
        if (found->second.size() > maximum) return -EFBIG;
        b = found->second; return 0;
    }
    octool::core::AmdPmReader reader() {
        return [this](const std::string &name, std::vector<std::uint8_t> &b, std::size_t max) { return read(name, b, max); };
    }
};
}
