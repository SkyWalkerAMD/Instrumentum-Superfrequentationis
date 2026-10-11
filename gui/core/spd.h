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
struct SpdCas {
    bool captured = false, decoded = false, reservedBitsSet = false;
    unsigned offset = 0;
    std::string encoding;
    std::vector<std::uint8_t> raw;
    std::vector<unsigned> latencies;
};
struct SpdCommandRate {
    bool captured = false, recognized = false;
    unsigned raw = 0, code = 0;
};
struct SpdXmpProfile {
    unsigned index = 0, offset = 0;
    bool enabled = false, enableKnown = true, blockedByExpo = false, nameSupported = false, nameValid = false;
    // User slots have no established enable bit. This describes captured bytes,
    // not firmware presence, selection or applicability of the stored values.
    std::string storageState;
    int timebaseRaw = -1; // XMP 2.0 per-profile byte; absent for XMP 3.0.
    int recommendedDimmsPerChannel = -1; // XMP 2.0 header, not installed DIMMs.
    int error = 0;
    std::string name;
    SpdCrc crc;
    SpdCas cas;
    SpdCommandRate commandRate;
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
    std::vector<SpdXmpProfile> userProfiles;
};
struct SpdExpoProfile {
    unsigned index = 0, offset = 0;
    bool enabled = false;
    int error = 0;
    std::vector<std::uint8_t> raw;
    std::vector<SpdQuantity> values;
    // Stored ps words; enhanced-feature enable semantics remain unknown.
    std::vector<SpdQuantity> storedEnhancedTimings;
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
    SpdCas cas;
    SpdXmp xmp;
    SpdExpo expo;
};
std::uint16_t spdCrc16(const std::uint8_t *bytes, std::size_t length);
// Bounded offline decoder, also used for sysfs EEPROM reads. Never writes SPD.
// DDR4/DDR5 base CRC, identity/geometry and revision-1 timing requirements.
// DDR4 tWR/tWTR_S/tWTR_L use 12-bit MTB counts with no fine correction.
// DDR5 base has eight independent ps/nCK pairs; revision 1.2 adds three more.
// Added timing quantities retain zero codes; none imply Auto or trained values.
// DDR5 XMP 3.0 manufacturer profiles 1..3 have independent CRC checks.
// Eight secondary timings retain separate ps and nCK lower requirements;
// no clock conversion or effective/trained timing is inferred from them.
// Revision-1 base SPD and XMP 3.0 CAS bitmaps list supported requirements;
// DDR4 base bit 30 is reserved, bit 31 selects CL7 or CL23 as the origin.
// XMP 3.0 command-rate codes 1..3 mean 1N..3N; zero means unspecified.
// XMP 2.0 CAS uses its own CL7..24 bitmap, not the base SPD range selector.
// Its per-profile recommended DIMM count is independent of installed topology.
// DDR4 XMP 2.0 profiles 1..2 have no extension CRC or descriptive names.
// Their per-profile time bases must encode 125 ps MTB / signed 1 ps FTB.
// Timings are stored requirements, never measured or currently trained values.
// XMP 3.0 user slots are inspected separately from manufacturer enable bits.
// EXPO 1.0 includes stored enhanced timing words under the shared bundle CRC;
// their enable semantics, XMP 3.0 DIMMs-per-channel and other flags are unknown.
SpdSnapshot decodeSpd(const std::vector<std::uint8_t> &bytes);
} }
