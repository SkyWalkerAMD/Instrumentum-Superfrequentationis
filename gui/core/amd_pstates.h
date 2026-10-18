// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <cstdint>

namespace octool {
namespace core {

enum class PstateFrequencyStatus {
    UnsupportedFamily, Disabled, ReservedFid, DividerOff, UnverifiedDivider, Valid
};
struct PstateValue {
    bool enabled = false;
    bool validFrequency = false;
    double frequencyMHz = 0;
    unsigned frequencyId = 0, vidBits = 0, iddValueBits = 0, iddDivBits = 0;
    bool hasDivider = false, rawBit32 = false;
    unsigned dividerId = 0;
    PstateFrequencyStatus frequencyStatus = PstateFrequencyStatus::UnsupportedFamily;
};
// 17h/19h use FID[7:0] and DFS[13:8]; 1Ah uses FID[11:0].
// The 17h/19h VID result is the low byte only, not a complete model-specific VID.
PstateValue decodeAmdPstate(unsigned family, std::uint64_t raw);
PstateValue decodeFamily1aPstate(std::uint64_t raw);
const char *amdPstateLayout(unsigned family);
const char *pstateFrequencyStatusName(PstateFrequencyStatus status);

// Implementations select the requested OS logical CPU and preserve errors.
// Return 0 on success or a negative errno; output on failure is ignored.
// This interface deliberately offers no register writes.
class PstateReader {
public:
    virtual ~PstateReader() = default;
    virtual int cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf,
                      std::uint32_t words[4]) = 0;
    virtual int readMsr(unsigned cpu, std::uint32_t index, std::uint64_t &value) = 0;
};

// Complete means the sampling pass ended; failures/row errors still matter.
enum class PstateStatus {
    CpuReadFailed, NotAmd, UnsupportedFamily, CapabilityUnavailable,
    CapabilityNotAdvertised, LimitReadFailed, Complete, InconsistentLimit
};
enum class PstateRowStatus { NotRead, AboveLimit, ReadFailed, Read };

struct PstateRow {
    PstateRowStatus status = PstateRowStatus::NotRead;
    int error = 0;
    std::uint64_t raw = 0;
};
struct PstateSnapshot {
    unsigned cpu = 0;
    PstateStatus status = PstateStatus::CpuReadFailed;
    int error = 0;
    bool hasIdentity = false;
    unsigned family = 0, model = 0, stepping = 0;
    bool hasLimit = false;
    std::uint64_t limitRaw = 0;
    unsigned minimum = 0, maximum = 0, failures = 0;
    std::array<PstateRow, 8> rows{};
};

// One synchronous, explicit sample. No Qt, OS calls, clock, global state,
// implicit construction-time probes, voltage/current decoding, or writes.
PstateSnapshot readAmdPstates(PstateReader &reader, unsigned cpu);

} // namespace core
} // namespace octool
