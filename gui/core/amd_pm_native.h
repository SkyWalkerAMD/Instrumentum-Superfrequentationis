// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "amd_pm.h"
#include "amd_smu.h"

namespace octool { namespace core {
enum class AmdPmNativePhase { Validate, Version, Address, Transfer, Read, Recheck, Decode, Complete };
const char *amdPmNativePhaseName(AmdPmNativePhase phase);
struct AmdPmNativeRead {
    int error = 0;
    AmdPmNativePhase phase = AmdPmNativePhase::Validate;
    unsigned cpu = 0, physicalAddressBits = 0;
    std::uint32_t version = 0, reportedBytes = 0, response = 0;
    std::uint64_t address = 0;
    bool metadataValid = false, addressValid = false;
    bool transferAttempted = false, transferAccepted = false, metadataRechecked = false;
    std::size_t bytesRead = 0;
    AmdPmSnapshot snapshot;
};
// Native Shimada 1Ah/08h, domain 0 BDF 00:00.0 only. Caller holds the
// HardwareService transaction. Uses RSMU 5/4/3, not the BIOS mailbox.
// Require an exact known version AND the same nonzero firmware-reported size.
// A complete buffer is published only after all reads and metadata rechecks.
// Firmware acknowledgement/unchanged metadata do not prove sample atomicity,
// freshness, physical topology or authenticity. Never changes tuning settings.
AmdPmNativeRead readShimadaPmNative(HardwareSession &session, const SmuTarget &target,
                                  const AmdPmOptions &options = AmdPmOptions());
std::string amdPmNativeJson(const AmdPmNativeRead &result);
} }
