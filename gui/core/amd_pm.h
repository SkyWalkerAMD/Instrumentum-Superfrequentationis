// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace octool { namespace core {
// PM captures include the full table and derived arrays. Other capture formats
// retain their existing 64 KiB bound; JSON depth/node limits are unchanged.
constexpr std::size_t amdPmCaptureMaximumBytes = 131072;
struct AmdPmLayout { std::uint32_t version; std::size_t bytes; };
const std::vector<AmdPmLayout> &amdPmLayouts();
std::size_t amdPmTableSize(std::uint32_t version);
struct AmdPmLayoutRange {
    std::string group, label;
    unsigned first = 0, end = 0, slots = 0, outsideWords = 0, overlapWords = 0, publicFieldConflicts = 0;
};
struct AmdPmLayoutAudit {
    int error = 0;
    std::uint32_t version = 0;
    std::size_t capturedBytes = 0, expectedBytes = 0;
    unsigned fusedCcdInput = 0, coreSlots = 0, ccdSlots = 0;
    unsigned outsideWords = 0, overlappingWords = 0, publicFieldConflicts = 0;
    bool constructorFallback = false;
    std::vector<AmdPmLayoutRange> ranges;
};
// Offline replay of the original DEFAULT constructor's range decisions.
// Input is explicit: never infer physical topology from table version/length.
// No bytes are read and no telemetry decoder is enabled, even if ranges fit.
AmdPmLayoutAudit auditLegacyAmdPmLayout(std::uint32_t version, std::size_t capturedBytes, unsigned fusedCcdInput);
std::string amdPmLayoutAuditJson(const AmdPmLayoutAudit &audit);
struct AmdPmOptions { bool hasBclk = false; double bclkMHz = 0; };
struct AmdPmField {
    std::string name, unit, group;
    unsigned offset = 0;
    // Nonzero only for original array storage. Never a physical target ID.
    unsigned slot = 0, slotCount = 0;
    std::uint32_t raw = 0;
    bool clock = false, scaled = false;
    int error = 0, scaleError = 0;
    double value = 0, scaledValue = 0;
};
struct AmdPmSnapshot {
    int error = 0;
    std::uint32_t version = 0;
    std::size_t capturedBytes = 0, expectedBytes = 0;
    AmdPmOptions options;
    std::string detail;
    std::vector<std::uint8_t> raw;
    std::vector<AmdPmField> fields;
    // Original Shimada monitor mappings. Labels/float storage are recovered;
    // physical units, rail/target identity and setter semantics are unverified.
    std::vector<AmdPmField> originalLimits;
    // Five additional original common monitor groups. Group IDs organize the
    // original labels; they do not certify units, active policies or targets.
    std::vector<AmdPmField> originalPackage;
    // Original link thresholds and internal-clock storage. No frequency,
    // bandwidth unit, active link state or physical index mapping is certified.
    std::vector<AmdPmField> originalFabric;
    // GMI/DIMM/PCIe stored arrays. Counts describe storage, not installed or
    // active devices. No unit, topology, enable mask or physical ID inferred.
    std::vector<AmdPmField> originalArrays;
};
// Exact version and length; no prefix fallback or inferred CPU/core identity.
// IEEE binary32 little endian. Invalid fields retain their bits, not a fake 0.
AmdPmSnapshot decodeAmdPm(const std::vector<std::uint8_t> &bytes, std::uint32_t version,
                         const AmdPmOptions &options = AmdPmOptions());
// Shared GUI/CLI capture. Import recomputes fields from raw_hex and accepts a
// capture directly or the data.pm member of an amd-pm-read/decode CLI report.
std::string amdPmJson(const AmdPmSnapshot &snapshot);
AmdPmSnapshot parseAmdPmCapture(const std::string &json);

// Reader receives a leaf name and a maximum size. It must read from offset 0
// through EOF, return negative errno, and perform no fallback or auto-loading.
using AmdPmReader = std::function<int(const std::string &, std::vector<std::uint8_t> &, std::size_t)>;
struct AmdPmDriverRead {
    AmdPmSnapshot snapshot;
    bool tableReadAttempted = false;
};
// Existing ryzen_smu binary sysfs ABI: LE u32 version, LE u32/u64 size.
// Metadata is checked before and after the full read. This cannot guarantee
// atomicity/freshness of the driver's buffer or certify a physical target.
AmdPmDriverRead readAmdPmDriver(const AmdPmReader &reader,
                              const AmdPmOptions &options = AmdPmOptions());
} }
