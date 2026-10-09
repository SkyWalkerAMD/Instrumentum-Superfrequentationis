// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <cstdint>

namespace octool {
namespace core {

struct PstateValue {
    bool enabled = false;
    bool validFrequency = false;
    unsigned frequencyMHz = 0;
    unsigned frequencyId = 0, vidBits = 0, iddValueBits = 0, iddDivBits = 0;
};
PstateValue decodeFamily1aPstate(std::uint64_t raw);

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
    CapabilityNotAdvertised, LimitReadFailed, Complete
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
    unsigned maximum = 0, failures = 0;
    std::array<PstateRow, 8> rows{};
};

// One synchronous, explicit sample. No Qt, OS calls, clock, global state,
// implicit construction-time probes, voltage/current decoding, or writes.
PstateSnapshot readAmdPstates(PstateReader &reader, unsigned cpu);

} // namespace core
} // namespace octool
