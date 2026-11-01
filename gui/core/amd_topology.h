// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "register_update.h"
#include <array>
#include <string>

namespace octool { namespace core {
struct AmdTopologyLevel {
    std::array<std::uint32_t,4> words{};
    unsigned type=0, shift=0, logicalCount=0;
    std::uint32_t globalId=0;
};
struct AmdL3Cache {
    std::array<std::uint32_t,4> words{};
    unsigned subleaf=0, lineBytes=0, partitions=0, ways=0, sharingCapacity=0, sharingShift=0;
    std::uint64_t sets=0, bytes=0;
    std::uint32_t cacheId=0;
};
struct AmdTopology {
    int error=0;
    unsigned cpu=0;
    CpuIdentity identity;
    unsigned ccdLevelType=0;
    bool redundantCcdLevel=false;
    std::uint32_t apicId=0, socketId=0, ccdInSocket=0, coreInCcd=0, threadInCore=0;
    std::vector<AmdTopologyLevel> levels;
    bool l3Requested=false, l3Valid=false;
    AmdL3Cache l3;
};
// CPUID Fn80000026: PPR 57238 pp142-144; Model 44h uses PPR 57896 pp34,119-121
// (one Complex per CCD; level type 3 documented as reserved). Published Model
// 44h captures also expose type 3 with exactly the same boundary/count as type
// 2. Accept that redundant level, retaining both raw levels and their source.
// No core-count heuristics or renumbering of sparse APIC IDs. These are CPUID
// topology IDs, NOT an assertion that SMU firmware uses the same target IDs.
AmdTopology readAmdTopology(HardwareSession &session,unsigned cpu,bool includeL3=false);
// Optional Fn8000001D geometry identifies the selected CPU's L3 capacity and
// sharing ID. It does not identify a PM storage slot or certify a tuning target.
std::string amdTopologyJson(const AmdTopology &topology);
struct AmdTopologyCatalog {
    int error=0;
    bool l3Requested=false, rechecked=false;
    unsigned failedCpu=0;
    bool hasFailedCpu=false;
    std::vector<unsigned> requestedCpus;
    std::vector<AmdTopology> cpus;
};
// Two CPUID sweeps over the caller's explicit OS CPU set. Never sorts or
// renumbers sparse IDs, fills missing CPUs, or binds these IDs to PM slots.
// Any failure/change discards the catalog; no partial identity map is returned.
AmdTopologyCatalog readAmdTopologyCatalog(HardwareSession &session,
    const std::vector<unsigned> &cpus,bool includeL3=false);
std::string amdTopologyCatalogJson(const AmdTopologyCatalog &catalog);

struct AmdApobCore {
    unsigned ordinal=0, storedId=0, threadCount=0;
    std::array<unsigned,2> threadFlags{};
};
struct AmdApobSubgroup {
    unsigned ordinal=0, storedId=0;
    std::vector<AmdApobCore> cores;
};
struct AmdApobGroup {
    unsigned ordinal=0, storedId=0;
    std::vector<AmdApobSubgroup> subgroups;
};
struct AmdApobTopology {
    int error=0;
    std::string detail;
    std::uint32_t instance=0;
    std::size_t bytes=0, extensionBytes=0, inputBytes=0, recordOffset=0;
    bool container=false;
    std::uint32_t containerVersion=0;
    unsigned containerRecords=0;
    unsigned duplicateIds=0, nonPrefixThreadFlags=0;
    std::vector<AmdApobGroup> groups;
};
// Offline group 3/type 3 record INCLUDING its 48-byte header (16-byte fields
// plus 32-byte HMAC), or a complete APOB container with exactly one such record.
// The whole container is bounds checked before returning any decoded groups.
// HMAC is not verified. Other records and header extensions are not interpreted.
// Reference: ASUS ProArt X870E-CREATOR WIFI 2503, D20482F8 protocol providers.
// This accepts a reference layout, not a verified platform/firmware identity.
// Stored IDs are not asserted to be physical CCD/core, OS CPU or PM target IDs.
AmdApobTopology decodeAmdApobTopology(const std::vector<std::uint8_t> &record);
std::string amdApobTopologyJson(const AmdApobTopology &topology);
} }
