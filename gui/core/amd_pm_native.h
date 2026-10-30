// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "amd_pm.h"
#include "amd_smu.h"

namespace octool { namespace core {
enum class AmdPmNativePhase { Validate, Version, Topology, Address, Transfer, Read, Recheck, TopologyRecheck, Decode, Complete };
const char *amdPmNativePhaseName(AmdPmNativePhase phase);
struct AmdPmNativeRead {
    int error = 0;
    bool graniteRidge = false;
    AmdPmNativePhase phase = AmdPmNativePhase::Validate;
    unsigned cpu = 0, physicalAddressBits = 0;
    std::uint32_t version = 0, reportedBytes = 0, response = 0;
    std::uint64_t address = 0;
    bool metadataValid = false, addressValid = false;
    bool transferAttempted = false, transferAccepted = false, metadataRechecked = false;
    bool mappingRequested = false, mappingValid = false, mappingRechecked = false;
    std::uint32_t smuVersion = 0;
    unsigned presentMaskBefore = 0, presentMaskAfter = 0, queriedBefore = 0, queriedAfter = 0;
    // Ascending present-mask bits: firmware MP5 target IDs, not OS CPUs/APICs.
    // Shimada requires both complete masks to agree with PM width. Granite
    // permits only its pinned two-slot initializer consequence, IDs [0,1].
    std::vector<unsigned> firmwareCcdIds;
    std::size_t bytesRead = 0;
    AmdPmSnapshot snapshot;
};
// Native Shimada 1Ah/08h, domain 0 BDF 00:00.0 only. Caller holds the
// HardwareService transaction. Uses RSMU 5/4/3, not the BIOS mailbox.
// Require an exact known version AND the same nonzero firmware-reported size.
// A complete buffer is published only after all reads and metadata rechecks.
// Firmware acknowledgement/unchanged metadata do not prove sample atomicity,
// freshness, physical topology or authenticity. Never changes tuning settings.
// Optional mapping requires SMU 115.46.0, and uses A5 presence acknowledgements.
// A5 also refreshes firmware clock telemetry. It does not request tuning writes.
// No active-core mask or mapping from firmware core slots to OS CPUs is inferred.
AmdPmNativeRead readShimadaPmNative(HardwareSession &session, const SmuTarget &target,
                                  const AmdPmOptions &options = AmdPmOptions(), bool mapCcds = false);
// Separate Granite Ridge 1Ah/44h, AMD 1022:14d8, BDF 0 profile. The pinned
// 98.84.0 protocol supports exact 620105/620205 versions with reported lengths.
// Optional mapping is restricted to 620205/2452: the two-target initializer
// implies IDs [0,1]. One-slot tables are ambiguous; no A5 query is sent.
// No BCLK scaling or tuning command is allowed by this path.
AmdPmNativeRead readGranitePmNative(HardwareSession &session, unsigned cpu,
                                  const AmdPmOptions &options = AmdPmOptions(), bool mapCcds = false);
std::string amdPmNativeJson(const AmdPmNativeRead &result);
} }
