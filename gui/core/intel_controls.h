// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "register_update.h"
#include <string>

namespace octool { namespace core {
enum class IntelField { Pl1, Pl2, Pl1Enable, Pl2Enable, Pl1Clamp, Pl2Clamp,
    Pl1Window, Pl2Window, HwpMin, HwpMax, HwpDesired, HwpEpp };
struct IntelReading {
    std::string name, unit;
    std::uint32_t msr = 0;
    int error = 0;
    std::uint64_t raw = 0;
    double value = 0;
    bool decoded = false, editable = false;
    IntelField field = IntelField::Pl1;
};
struct IntelSnapshot {
    unsigned cpu = 0;
    CpuIdentity identity;
    int error = 0;
    bool rapl = false, hwp = false, epp = false, hwpEnabled = false;
    Reply units, powerLimit, hwpRequest, hwpCapabilities;
    std::vector<IntelReading> readings;
};
bool hasIntelRaplProfile(const CpuIdentity &id);
double raplWindowSeconds(unsigned code, unsigned timeExponent);
int encodeRaplWindow(double seconds, unsigned timeExponent, unsigned &code);
IntelSnapshot readIntelControls(HardwareSession &session, unsigned cpu);
UpdateResult applyIntelControl(HardwareSession &session, const IntelSnapshot &snapshot,
                               IntelField field, double value);
} }
