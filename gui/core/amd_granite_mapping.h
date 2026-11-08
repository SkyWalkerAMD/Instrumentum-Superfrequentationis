// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace octool { namespace core {
struct AmdGraniteMappingInput {
    std::uint32_t tableVersion = 0, presenceMask = 0;
    std::array<std::uint32_t,2> coreMasks{};
    bool smt = true;
};
struct AmdGraniteMappingTarget {
    unsigned firmwareTarget = 0;
    bool present = false;
    int pmCcdSlot = -1, commandTargetRank = -1;
    // Raw input is retained even for absent targets. Only present targets are
    // read by the reference initializer; absent inputs are not consumed.
    std::uint32_t rawCoreMask = 0, storedCoreMask = 0, enumeratedCoreMask = 0;
    std::uint32_t discardedHighBits = 0, nonEnumeratedBits = 0;
};
struct AmdGraniteMappingCore {
    unsigned pmCcdSlot = 0, pmCoreSlot = 0, localCoreSlot = 0, firmwareTarget = 0;
    bool enabled = false;
    int commandCoreRank = -1;
    // Diagnostic coordinates only. In the checked consumers, sibling indexes
    // alias one core. No public host command or runtime target is authenticated.
    std::vector<unsigned> commandIndexes, encodedCoordinates;
};
struct AmdGraniteMapping {
    int error = 0;
    std::string detail;
    AmdGraniteMappingInput input;
    unsigned pmCcdSlots = 0, activeTargets = 0, enabledCores = 0, threadsPerCore = 0;
    std::vector<AmdGraniteMappingTarget> targets;
    std::vector<AmdGraniteMappingCore> coreSlots;
    std::array<std::uint16_t,64> commandRemap{};
};
// Offline reference diagnostic for BIOS 2503 / SMU 98.84.0, with explicitly
// caller-supplied masks and SMT mode. No hardware reads, writes, OS topology,
// firmware authentication or physical CCD binding is performed. Values cannot
// be inferred from a PM float table. PM storage ranks include every present
// target; command ranks include present targets with a nonzero LOW 16-bit mask.
// Bits 8..15 affect that nonzero test but do not enumerate additional cores.
// A table version / presence-width mismatch fails without returning mappings.
AmdGraniteMapping diagnoseAmdGraniteMapping(const AmdGraniteMappingInput &input);
std::string amdGraniteMappingJson(const AmdGraniteMapping &mapping);
} }
