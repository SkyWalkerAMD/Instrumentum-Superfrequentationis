// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace octool { namespace core {
struct SpdField { std::string name, value; };
struct SpdSnapshot {
    int error = 0;
    unsigned memoryType = 0;
    bool crcChecked = false, crcValid = false;
    std::vector<SpdField> fields;
};
std::uint16_t spdCrc16(const std::uint8_t *bytes, std::size_t length);
// Bounded offline decoder, also used for sysfs EEPROM reads. Never writes SPD.
// DDR5 identity/geometry only; no assumptions about trained timings or XMP.
SpdSnapshot decodeSpd(const std::vector<std::uint8_t> &bytes);
} }
