// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "amd_umc.h"
#include <array>
#include <string>

namespace octool { namespace core {
// Offline data is untrusted: identity/target metadata never authorizes IO.
struct UmcCapture {
    int error = -1;
    std::string detail, snapshotJson;
    UmcTarget target;
    bool hasCpu = false, hasPci = false;
    std::vector<UmcRegister> registers;
    UmcDecode decoded;
};
// GUI v1 snapshots (partial allowed), successful CLI read reports (complete),
// and CLI decode reports (partial allowed). Recomputes fields from raw words.
// UTF-8, <=64 KiB, depth <=16, <=8192 values, no duplicate object keys.
UmcCapture parseUmcCapture(const std::string &json);

enum class UmcChange { Unchanged, Changed, RawOnly, MissingBefore, MissingAfter, MissingBoth };
const char *umcChangeName(UmcChange state);
struct UmcFieldChange {
    UmcValue before, after;
    UmcChange state = UmcChange::MissingBoth;
};
struct UmcComparison {
    int error = 0;
    std::array<unsigned, 6> counts{};
    std::vector<UmcFieldChange> fields;
};
// Same bank and refresh slot required. CPU/PCI numbering may differ across
// systems; matching indices do NOT establish physical channel equivalence.
UmcComparison compareUmcCaptures(const UmcCapture &before, const UmcCapture &after);
} }
