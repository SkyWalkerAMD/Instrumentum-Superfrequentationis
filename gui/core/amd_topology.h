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
struct AmdApobRegisterRoute {
    unsigned group=0, subgroup=0, core=0, thread=0, bit=0;
    std::uint32_t providerAAddress=0, providerBAddress=0;
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
    unsigned referenceThreadStride=0, differentThreadPrefixes=0;
    std::array<unsigned,2> referenceRouteAliases{};
    std::vector<AmdApobRegisterRoute> referenceRoutes;
    std::vector<AmdApobGroup> groups;
};
// Offline group 3/type 3 record INCLUDING its 48-byte header (16-byte fields
// plus 32-byte HMAC), or a complete APOB container with exactly one such record.
// The whole container is bounds checked before returning any decoded groups.
// HMAC is not verified. Other records and header extensions are not interpreted.
// Reference: ASUS ProArt X870E-CREATOR WIFI 2503, D20482F8 protocol providers.
// This accepts a reference layout, not a verified platform/firmware identity.
// Stored IDs are not asserted to be physical CCD/core, OS CPU or PM target IDs.
// Reference routes describe two DIFFERENT pinned protocol +0x48 selectors.
// Only raw nonzero flags 0/1 produce candidates; the firmware's unchecked
// thread argument is never accepted here. No provider or write is selected.
AmdApobTopology decodeAmdApobTopology(const std::vector<std::uint8_t> &record);
std::string amdApobTopologyJson(const AmdApobTopology &topology);
struct AmdApobCpuCandidate {
    unsigned group=0, subgroup=0, core=0, thread=0;
    unsigned storedGroup=0, storedSubgroup=0, storedCore=0;
    std::uint32_t apicId=0;
    bool matched=false;
    unsigned cpu=0;
};
struct AmdApobComparison {
    int error=0;
    std::string detail;
    bool inputsRechecked=false, completeReferenceMatch=false;
    unsigned matched=0, missingApics=0;
    AmdApobTopology record;
    AmdTopologyCatalog catalog;
    // In requested CPU order: Fn0, Fn80000001, Fn0B subleaf 0.
    std::vector<std::array<std::array<std::uint32_t,4>,3>> inputs;
    std::vector<AmdApobCpuCandidate> candidates;
    std::vector<unsigned> unmatchedCpus;
};
// Read-only comparison against the pinned 2503 enumeration, restricted to the
// published Model 44h, SMT2, redundant-type-3, single-socket 32-APIC shape.
// The record's origin/HMAC, installed provider, proximity callbacks and PM
// storage order are NOT verified. Even a complete APIC match is only a
// reference comparison, never a physical target authorization.
// Bounds/shape errors precede CPUID; any read/change discards all candidates.
AmdApobComparison readAmdApobComparison(HardwareSession &session,
    const std::vector<std::uint8_t> &record,const std::vector<unsigned> &cpus,bool includeL3=false);
std::string amdApobComparisonJson(const AmdApobComparison &comparison);
} }
