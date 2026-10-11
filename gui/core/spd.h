// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace octool { namespace core {
struct SpdField { std::string name, value; };
struct SpdCrc {
    bool checked = false, valid = false;
    unsigned stored = 0, computed = 0;
};
struct SpdQuantity { std::string name, unit; unsigned value; };
struct SpdXmpProfile {
    unsigned index = 0, offset = 0;
    bool enabled = false, blockedByExpo = false, nameSupported = false, nameValid = false;
    int timebaseRaw = -1; // XMP 2.0 per-profile byte; absent for XMP 3.0.
    int error = 0;
    std::string name;
    SpdCrc crc;
    std::vector<std::uint8_t> raw;
    std::vector<SpdQuantity> values;
};
struct SpdXmp {
    bool inspected = false, present = false, expoInspected = false, expoPresent = false;
    bool headerCaptured = false, crcSupported = false;
    unsigned revision = 0, enabledMask = 0, configurationRaw = 0;
    int error = 0;
    SpdCrc crc;
    std::vector<std::uint8_t> raw;
    std::vector<SpdXmpProfile> profiles;
};
struct SpdExpoProfile {
    unsigned index = 0, offset = 0;
    bool enabled = false;
    int error = 0;
    std::vector<std::uint8_t> raw;
    std::vector<SpdQuantity> values;
};
struct SpdExpo {
    bool inspected = false, present = false;
    unsigned revision = 0, configurationRaw = 0, featuresRaw = 0;
    int error = 0;
    // One CRC covers the entire 128-byte bundle, including both profiles.
    SpdCrc crc;
    std::vector<std::uint8_t> raw;
    std::vector<SpdExpoProfile> profiles;
};
struct SpdSnapshot {
    // Base decoding error. Extension errors are separate so a damaged section
    // cannot hide valid JEDEC fields or make them appear to have a bad CRC.
    int error = 0;
    unsigned memoryType = 0;
    bool crcChecked = false, crcValid = false;
    std::vector<SpdField> fields;
    SpdXmp xmp;
    SpdExpo expo;
};
std::uint16_t spdCrc16(const std::uint8_t *bytes, std::size_t length);
// Bounded offline decoder, also used for sysfs EEPROM reads. Never writes SPD.
// DDR4/DDR5 base CRC, identity/geometry and revision-1 timing requirements.
// DDR5 XMP 3.0 manufacturer profiles 1..3 have independent CRC checks.
// DDR4 XMP 2.0 profiles 1..2 have no extension CRC or descriptive names.
// Their per-profile time bases must encode 125 ps MTB / signed 1 ps FTB.
// Timings are stored requirements, never measured or currently trained values.
// EXPO 1.0 basic profiles use a shared bundle CRC. XMP user slots and EXPO
// enhanced timings, DIMMs-per-channel and other flag meanings are not decoded.
SpdSnapshot decodeSpd(const std::vector<std::uint8_t> &bytes);
} }
