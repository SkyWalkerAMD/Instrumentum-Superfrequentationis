// SPDX-License-Identifier: GPL-2.0-only
#include "amd_pm.h"
#include "bounded_json.h"
#include <cerrno>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace octool { namespace core {
namespace {
const std::size_t maximumBytes = 11636;
bool validOptions(const AmdPmOptions &o) { return !o.hasBclk || (std::isfinite(o.bclkMHz) && o.bclkMHz > 0); }
std::uint64_t little(const std::vector<std::uint8_t> &b, std::size_t off, unsigned size) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < size; ++i) value |= std::uint64_t(b[off + i]) << (8 * i);
    return value;
}
std::string number(double value) {
    if (!std::isfinite(value)) return "null";
    std::ostringstream s; s.imbue(std::locale::classic()); s << std::setprecision(17) << value; return s.str();
}
std::string quote(const std::string &s) {
    std::string out = "\"";
    const char *hex = "0123456789abcdef";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += char(c);
    }
    return out + '"';
}
std::string word(std::uint32_t value) {
    std::ostringstream s; s.imbue(std::locale::classic()); s << "\"0x" << std::hex << std::setw(8) << std::setfill('0') << value << '"'; return s.str();
}
void field(AmdPmSnapshot &out, std::vector<AmdPmField> &target, const char *name,
           const char *unit, unsigned offset, bool clock = false, const char *group = "") {
    AmdPmField v; v.name = name; v.unit = unit; v.offset = offset; v.clock = clock; v.group = group;
    v.raw = std::uint32_t(little(out.raw, offset, 4));
    // Classify both quiet and signalling NaNs before touching the FPU.
    if ((v.raw & 0x7f800000u) == 0x7f800000u) v.error = -EDOM;
    else {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "PM tables require IEEE binary32");
        float stored = 0; std::memcpy(&stored, &v.raw, sizeof(stored)); v.value = stored;
        if (clock && out.options.hasBclk) {
            v.scaledValue = (v.value / 100.0) * out.options.bclkMHz;
            if (!std::isfinite(v.scaledValue) || (v.value != 0 && v.scaledValue == 0)) v.scaleError = -ERANGE;
            else v.scaled = true;
        }
    }
    if (!out.error) out.error = v.error ? v.error : v.scaleError;
    target.push_back(v);
}
}
const std::vector<AmdPmLayout> &amdPmLayouts() {
    static const std::vector<AmdPmLayout> layouts = {
        {0x730204, 2844}, {0x730404, 4100}, {0x730604, 5356}, {0x730804, 6612},
        {0x730a04, 7868}, {0x730c04, 9124}, {0x730e04, 10380}, {0x731004, 11636}};
    return layouts;
}
std::size_t amdPmTableSize(std::uint32_t version) {
    for (const auto &layout : amdPmLayouts()) if (layout.version == version) return layout.bytes;
    for (const auto &layout : amdGranitePmLayouts()) if (layout.version == version) return layout.bytes;
    return 0;
}
const std::vector<AmdPmLayout> &amdGranitePmLayouts() {
    static const std::vector<AmdPmLayout> layouts = {{0x620105,1828}, {0x620205,2452}};
    return layouts;
}
bool isGranitePm(std::uint32_t version) { return version == 0x620105 || version == 0x620205; }
AmdPmLayoutAudit auditLegacyAmdPmLayout(std::uint32_t version, std::size_t captured, unsigned fused) {
    AmdPmLayoutAudit out; out.version = version; out.capturedBytes = captured;
    out.expectedBytes = amdPmTableSize(version); out.fusedCcdInput = fused;
    if (!out.expectedBytes || isGranitePm(version)) { out.error = -ENOTSUP; return out; }
    if (fused < 1 || fused > 16) { out.error = -EINVAL; return out; }
    if (captured != out.expectedBytes) out.error = -EMSGSIZE;
    switch (fused) {
    case 1: case 2: case 4: case 8: case 10: case 12: case 14: case 16: out.ccdSlots = fused; break;
    default: out.ccdSlots = 2; out.constructorFallback = true; break;
    }
    out.coreSlots = out.ccdSlots * 8;
    const unsigned coreStart = (version < 0x730404 ? 0x5f4 : 0x634) + (fused <= 2 ? 0x40 : 0);
    const unsigned ccdStart = (version < 0x730404 ? 0x1f74 : 0x2134) + (fused <= 2 ? 0x40 : 0);
    const unsigned coreEnd = coreStart + 15 * out.coreSlots * 4, ccdEnd = ccdStart + 11 * out.ccdSlots * 4;
    const char *cores[] = {"Power", "Voltage", "Temp", "Freq Ghz", "Eff Freq", "C0 Residency", "CC1 Residency", "CC6 Residency",
        "OS PState", "CPPC Max Freq", "CPPC Min Freq", "EDC CAC", "UOPs", "Mem Band", "Core Bound"};
    const char *ccds[] = {"Logic Power", "VDDM Power", "VDDM Voltage", "Temp", "Freq Ghz", "Eff Freq", "CCA Limit", "CCA CAC", "CCA Residency", "EDC CAC", "FLL BTC"};
    for (unsigned group = 0; group < 2; ++group) {
        const unsigned count = group ? 11 : 15, slots = group ? out.ccdSlots : out.coreSlots;
        const unsigned start = group ? ccdStart : coreStart, otherFirst = group ? coreStart : ccdStart, otherEnd = group ? coreEnd : ccdEnd;
        for (unsigned i = 0; i < count; ++i) {
            AmdPmLayoutRange r; r.group = group ? "ccd" : "core"; r.label = group ? ccds[i] : cores[i];
            r.first = start + i * slots * 4; r.end = r.first + slots * 4; r.slotCount = slots;
            for (unsigned off = r.first; off < r.end; off += 4) {
                if (std::size_t(off) + 4 > captured) ++r.outsideWords;
                if (off >= otherFirst && off < otherEnd) ++r.overlapWords;
                // All other public offsets precede the earliest original core
                // start. These are the exact common-field collisions.
                if (off == 0x714 || off == 0x71c) ++r.publicFieldConflicts;
            }
            out.outsideWords += r.outsideWords;
            if (!group) out.overlappingWords += r.overlapWords;
            out.publicFieldConflicts += r.publicFieldConflicts;
            out.ranges.push_back(r);
        }
    }
    return out;
}
std::string amdPmLayoutAuditJson(const AmdPmLayoutAudit &s) {
    std::string out = "{\"format\":\"octool-amd-pm-layout-audit\",\"schema\":1,\"version\":" + word(s.version) +
        ",\"captured_bytes\":" + std::to_string(s.capturedBytes) + ",\"expected_bytes\":" + std::to_string(s.expectedBytes) +
        ",\"fused_ccd_input\":" + std::to_string(s.fusedCcdInput) + ",\"core_slots\":" + std::to_string(s.coreSlots) +
        ",\"ccd_slots\":" + std::to_string(s.ccdSlots) + ",\"constructor_fallback\":" + (s.constructorFallback ? "true" : "false") +
        ",\"outside_words\":" + std::to_string(s.outsideWords) + ",\"overlapping_words\":" + std::to_string(s.overlappingWords) +
        ",\"public_field_conflicts\":" + std::to_string(s.publicFieldConflicts) + ",\"error\":" + std::to_string(s.error) +
        ",\"decoder_enabled\":false,\"physical_mapping_verified\":false,\"hardware_accessed\":false,\"ranges\":[";
    bool first = true;
    for (const auto &r : s.ranges) {
        if (!first) out += ',';
        first = false;
        out += "{\"group\":" + quote(r.group) + ",\"label\":" + quote(r.label) + ",\"first_byte\":" + word(r.first) +
            ",\"end_byte_exclusive\":" + word(r.end) + ",\"slots\":" + std::to_string(r.slotCount) +
            ",\"outside_words\":" + std::to_string(r.outsideWords) + ",\"overlap_words\":" + std::to_string(r.overlapWords) +
            ",\"public_field_conflicts\":" + std::to_string(r.publicFieldConflicts) + '}';
    }
    return out + "]}";
}
AmdPmSnapshot decodeAmdPm(const std::vector<std::uint8_t> &bytes, std::uint32_t version, const AmdPmOptions &options) {
    AmdPmSnapshot out; out.version = version; out.options = options;
    out.graniteRidge = isGranitePm(version);
    out.capturedBytes = bytes.size(); out.expectedBytes = amdPmTableSize(version);
    if (bytes.size() <= maximumBytes) out.raw = bytes;
    if (!validOptions(options)) { out.error = -EINVAL; out.detail = "Supplied BCLK must be positive and finite"; return out; }
    if (!out.expectedBytes) { out.error = -ENOTSUP; out.detail = "Unsupported exact PM table version"; return out; }
    if (bytes.size() != out.expectedBytes) { out.error = -EMSGSIZE; out.detail = "PM table length does not match its version"; return out; }
    if (out.graniteRidge) {
        if (options.hasBclk) { out.error = -ENOTSUP; out.detail = "BCLK scaling is not defined for Granite Ridge PM storage"; return out; }
        // SMU 98.84.0: common copy is 4b4h bytes. MP5's byte-checked integer
        // scatter writes 18*8*N core words and 12*N CCD words (N=1 or 2).
        // Eight row units are traced in the pinned 98.84.0 MP5 producer. They
        // arrive averaged/scaled already: never apply the conversion a second time.
        const char *names[] = {"FCLK candidate","UCLK candidate","MCLK candidate","VDD_MISC candidate",
            "VDDCR_SOC candidate","CLDO_VDDG_IOD candidate","CLDO_VDDG_CCD candidate","CLDO_VDDP candidate"};
        const unsigned indices[] = {71,75,79,58,83,259,261,269};
        for (unsigned i = 0; i < 8; ++i) field(out,out.fields,names[i],"unverified",indices[i]*4,false,"granite_common");
        const char *quantities[] = {"voltage", "current", "power", "temperature"};
        const char *units[] = {"V", "A", "W", "deg C"};
        for (unsigned channel = 0; channel < 3; ++channel) for (unsigned quantity = 0; quantity < 4; ++quantity) {
            const auto name = "SVI channel " + std::to_string(channel) + " " + quantities[quantity] + " (averaged)";
            field(out,out.fields,name.c_str(),units[quantity],4*(49+5*channel+quantity),false,"granite_svi");
            auto &v = out.fields.back(); v.firmwareChannel = channel;
            v.unitEvidence = "granite-smu-98.84.0-static";
        }
        out.firmwareCcdSlots = version == 0x620205 ? 2 : 1;
        const unsigned cores = out.firmwareCcdSlots*8;
        for (unsigned row = 0; row < 18; ++row) for (unsigned slot = 0; slot < cores; ++slot) {
            field(out,out.firmwareCores,("Core row " + std::to_string(row)).c_str(),"unverified",0x4b4+4*(row*cores+slot),false,"core_storage");
            auto &v = out.firmwareCores.back(); v.row = row; v.slot = slot; v.slotCount = cores;
            if(row==1) { v.name="Core voltage model (averaged)"; v.unit="V"; }
            if(row==2) { v.name="Core temperature (sensor maximum, averaged)"; v.unit="deg C"; }
            if(row>=5 && row<=7) {
                const char *residencies[]={"C0 residency candidate (averaged)","CC1 residency candidate (averaged)","CC6 residency candidate (averaged)"};
                v.name=residencies[row-5]; v.unit="%";
            }
            if(row==1 || row==2 || (row>=5 && row<=7)) v.unitEvidence="granite-mp5-98.84.0-static";
        }
        for (unsigned row = 0; row < 12; ++row) for (unsigned slot = 0; slot < out.firmwareCcdSlots; ++slot) {
            field(out,out.firmwareCcds,("CCD row " + std::to_string(row)).c_str(),"unverified",0x4b4+4*(18*cores+row*out.firmwareCcdSlots+slot),false,"ccd_storage");
            auto &v = out.firmwareCcds.back(); v.row = row; v.slot = slot; v.slotCount = out.firmwareCcdSlots;
            if(row==3) { v.name="CCD sensor maximum A (averaged)"; v.unit="deg C"; }
            if(row==4) { v.name="CCD sensor maximum B (conditional, averaged)"; v.unit="deg C"; }
            if(row==11) { v.name="MP5 interval counter ratio (averaged)"; v.unit="ratio"; }
            if(row==3 || row==4 || row==11) v.unitEvidence="granite-mp5-98.84.0-static";
        }
        for (unsigned offset = 0; offset < bytes.size(); offset += 4)
            field(out,out.graniteRaw,("Word " + std::to_string(offset/4)).c_str(),"unverified",offset,false,"granite_storage");
        out.detail = out.error ? "Some Granite Ridge stored words are non-finite; raw bits and other values are retained" :
            "Granite Ridge storage decoded using the 98.84.0 unit reference. SVI channels report averaged voltage, current, power and temperature; failed reads can retain old values and current scale mode 0 produces zero. Their validity flags are absent from this table. Channel numbers do not identify physical rails or verify applied settings. Core model voltage is not measured rail voltage; conditional sensors may retain old values. Other units, CPU/CCD mapping and capture provenance remain unverified.";
        return out;
    }
    // Independently implemented from the pinned layout facts in
    // docs/amd-pm-recovery.md; never infer per-core target IDs from this table.
    field(out, out.fields, "FCLK", "MHz", 0x20c, true);
    field(out, out.fields, "UCLK", "MHz", 0x21c, true);
    field(out, out.fields, "MCLK", "MHz", 0x22c, true);
    field(out, out.fields, "VDDCR_SOC", "V", 0x1a4);
    field(out, out.fields, "CLDO_VDDP", "V", 0x5cc);
    // 714/71c belong to the MP5 core arrays. Earlier public mappings mislabeled
    // these as VDDG voltages; do not preserve that interpretation in captures.
    field(out, out.fields, "VDD_MISC", "V", 0x1b8);
    const char *rails[] = {"VDD", "VDD1", "SoC", "Misc"};
    const char *names[] = {"requested voltage (VID)", "measured voltage", "current", "VRM temperature"};
    const char *units[] = {"V", "V", "A", "C"};
    const unsigned offsets[] = {0, 4, 8, 16};
    for (unsigned r = 0; r < 4; ++r) for (unsigned f = 0; f < 4; ++f)
        field(out, out.fields, (std::string(rails[r]) + " " + names[f]).c_str(), units[f], 0x178 + 20 * r + offsets[f]);
    // Pinned original constructor + common-field decoder replay, documented in
    // docs/amd-pm-limits.md. Keep these separate from the public telemetry facts.
    // Indices 0/1 are original labels, not CPU/CCD or physical rail identifiers.
    const char *limits[] = {"Fast PPT Limit", "Fast PPT Value", "TDC0 Limit", "TDC0 Current Value",
        "TDC1 Limit", "TDC1 Current Value", "THM0 Limit", "THM0 Current Value",
        "THM1 Limit", "THM1 Current Value", "FIT Limit", "FIT Current Value",
        "VID0 Limit", "VID0 Current Value", "VID1 Limit", "VID1 Current Value",
        "EDC_LIMIT_0", "PEAK_TELEMETRY_CURRENT_0", "PCC_LIMIT_0", "PCC_RESIDENCY_0",
        "EDC_LIMIT_1", "PEAK_TELEMETRY_CURRENT_1", "PCC_LIMIT_1", "PCC_RESIDENCY_1"};
    for (unsigned i = 0; i < 24; ++i)
        field(out, out.originalLimits, limits[i], "unverified", i < 16 ? i * 4 : 0x1c8 + (i-16)*4);
    // Original constructor/get_pm_patch common vectors, independently replayed
    // in legacy-amd-pm-package.py. Deliberately exclude core/CCD paths, SVI power
    // guesses and the original's conflicting clock mappings.
    const char *power[] = {"CPU Power","SOC Power","Misc Power","Mem Power","1.8V Power","ROC Power","Socket Power"};
    const char *workload[] = {"PPT_WC","PPT_ACTUAL","TDC_0_WC","TDC_0_ACTUAL","TDC_1_WC","TDC_1_ACTUAL","FIT_WC","FIT_ACTUAL"};
    const char *model[] = {"VDDCR_CPU_DYN_POWER_WC","VDDCR_CPU_LKG_POWER_WC","VDDCR_SOC_DYN_POWER_WC","VDDCR_SOC_LKG_POWER_WC"};
    const char *voltage[] = {"FIT PID0","LATCHUP0","PPT_VOLTAGE_0","TDC_VOLTAGE_0","THM_VOLTAGE_0",
        "FIT_VOLTAGE_1","LATCHUP_VOLTAGE_1","PPT_VOLTAGE_1","TDC_VOLTAGE_1","THM_VOLTAGE_1"};
    const char *contributors[] = {"PPT0","TDC0","STAPM0","THM0","HTFMax0","VRHOT0","ProcHot0","Voltage0",
        "EDC0","CState0","PPT_SLOW0","PPT_APU0","PPT1","TDC1","STAPM1","THM1","HTFMax1","VRHOT1",
        "ProcHot1","Voltage1","EDC1","CState1","PPT_SLOW1","PPT_APU1"};
    const auto append = [&](const char *group, unsigned offset, const char *const *labels, unsigned count) {
        for (unsigned i = 0; i < count; ++i)
            field(out, out.originalPackage, labels[i], "unverified", offset + 4*i, false, group);
    };
    append("power", 0x60, power, 7);
    append("workload", 0x40, workload, 8);
    append("power_model", 0x7c, model, 4);
    append("voltage_policy", 0x8c, voltage, 10);
    append("limit_contributors", 0xd0, contributors, 24);
    // Two independent original vectors, checked by legacy-amd-pm-fabric.py.
    // Keep these stored floats distinct from public clock fields and BCLK
    // scaling. Exclude the original FCLK/UCLK/MCLK vector with conflicting
    // offsets; a label alone cannot establish a physical frequency or link.
    const char *links[] = {"FCLK_PCIE_UP_THRESHOLD","FCLK_PCIE_BANDWIDTH","FCLK_PCIE_DOWN_THRESHOLD",
        "FCLK_XGMI_UP_THRESHOLD","FCLK_XGMI_BANDWIDTH","FCLK_XGMI_DOWN_THRESHOLD",
        "FCLK_CXL_UP_THRESHOLD","FCLK_CXL_BANDWIDTH","FCLK_CXL_DOWN_THRESHOLD"};
    const char *clocks[] = {"SMNCLK","SMNCLK_EFF","WAFLCLK","WAFLCLK_EFF",
        "MP0CLK","MP0CLK_EFF","MP1CLK","MP1CLK_EFF","MPIOCLK","MPIOCLK_EFF",
        "ACLK","ACLK_EFF","SVI3CLK","SVI3CLK_EFF","MPDMA_TF_CLK","MPDMA_TF_CLK_EFF",
        "MPDMA_PM_CLK","MPDMA_PM_CLK_EFF"};
    for (unsigned i = 0; i < 9; ++i)
        field(out, out.originalFabric, links[i], "unverified", 0x154 + 4*i, false, "link_bandwidth");
    for (unsigned i = 0; i < 18; ++i)
        field(out, out.originalFabric, clocks[i], "unverified", 0x24c + 4*i, false, "internal_clocks");
    // Original common vectors independently replayed by legacy-amd-pm-arrays.py.
    // Slots are zero-based storage order; labels do not establish physical
    // endpoints, actual DIMM population, units, or Boolean/mask semantics.
    const auto array = [&](const char *group, const char *name, unsigned offset, unsigned count) {
        for (unsigned i = 0; i < count; ++i) {
            field(out, out.originalArrays, name, "unverified", offset + 4*i, false, group);
            out.originalArrays.back().slot = i;
            out.originalArrays.back().slotCount = count;
        }
    };
    array("gmi_storage", "GMI_FULL_WIDTH", 0x294, 12);
    array("gmi_storage", "GMI_HALF_WIDTH", 0x2c4, 12);
    array("gmi_storage", "GMI_QUARTER_WIDTH", 0x2f4, 12);
    array("dimm_storage", "DIMM_POWER", 0x324, 24);
    array("dimm_storage", "DIMM_TEMP", 0x384, 24);
    array("pcie_storage", "PCIE_ALLOWED", 0x3e4, 1);
    array("pcie_storage", "PCIE_ACTIVE", 0x3e8, 1);
    array("pcie_storage", "PCIE_UP_THRESHOLD", 0x3ec, 1);
    array("pcie_storage", "PCIE_DOWN_THRESHOLD", 0x3f0, 1);
    array("pcie_storage", "PCIE_IOHC_BANDWIDTH[4]", 0x3f4, 4);
    // Exact version/size validation above is mandatory. SMU 05 reports N and
    // 634h + N*274h bytes; MP5 37dd2..37e52 independently scatters 18*8 core
    // words followed by 13 CCD words. See amd-smu-firmware-research.md.
    out.firmwareCcdSlots = unsigned((out.expectedBytes - 0x634) / 0x274);
    const unsigned coreSlots = out.firmwareCcdSlots * 8;
    const char *coreRows[] = {"Legacy Power", "Legacy Voltage", "Legacy Temp", "Legacy Freq Ghz",
        "Legacy Eff Freq", "Legacy C0 Residency", "Legacy CC1 Residency", "Legacy CC6 Residency",
        "Legacy OS PState", "Legacy CPPC Max Freq", "Legacy CPPC Min Freq", "Legacy EDC CAC",
        "Legacy UOPs", "Legacy Mem Band", "Legacy Core Bound", "Core row 15", "Core row 16", "Core row 17"};
    const char *ccdRows[] = {"Legacy Logic Power", "Legacy VDDM Power", "Legacy VDDM Voltage", "Legacy Temp",
        "Legacy Freq Ghz", "Legacy Eff Freq", "Legacy CCA Limit", "Legacy CCA CAC", "Legacy CCA Residency",
        "Legacy EDC CAC", "Legacy FLL BTC", "CCD row 11", "CCD row 12"};
    const auto firmwareRows = [&](std::vector<AmdPmField> &target, const char *group, const char *const *names,
                                  unsigned count, unsigned slots, unsigned start) {
        for (unsigned row = 0; row < count; ++row) for (unsigned slot = 0; slot < slots; ++slot) {
            field(out, target, names[row], "unverified", start + 4*(row*slots+slot), false, group);
            target.back().slot = slot; target.back().slotCount = slots; target.back().row = row;
        }
    };
    firmwareRows(out.firmwareCores, "core_storage", coreRows, 18, coreSlots, 0x634);
    firmwareRows(out.firmwareCcds, "ccd_storage", ccdRows, 13, out.firmwareCcdSlots, 0x634 + 18*coreSlots*4);
    if (out.error) out.detail = "Some fields are non-finite or clock scaling is out of range; valid stored fields are retained";
    return out;
}
std::string amdPmJson(const AmdPmSnapshot &s) {
    std::string raw; const char *digits = "0123456789abcdef";
    for (auto b : s.raw) { raw += digits[b >> 4]; raw += digits[b & 15]; }
    std::string out = "{\"format\":\"octool-amd-pm\",\"schema\":1,\"version\":" + word(s.version) +
        ",\"captured_bytes\":" + std::to_string(s.capturedBytes) + ",\"expected_bytes\":" + std::to_string(s.expectedBytes) +
        ",\"error\":" + std::to_string(s.error) + ",\"detail\":" + quote(s.detail) + ",\"bclk_mhz\":" +
        (s.options.hasBclk && validOptions(s.options) ? number(s.options.bclkMHz) : "null") + ",\"raw_hex\":" + quote(raw) + ",\"fields\":[";
    auto appendFields = [&out](const std::vector<AmdPmField> &fields) {
      bool first = true;
      for (const auto &v : fields) {
        if (!first) out += ',';
        first = false;
        out += "{\"name\":" + quote(v.name) + ",\"unit\":" + quote(v.unit) + ",\"offset\":" + word(v.offset) +
            ",\"raw\":" + word(v.raw) + ",\"error\":" + std::to_string(v.error) + ",\"stored_value\":" +
            (v.error ? "null" : number(v.value)) + ",\"clock\":" + (v.clock ? "true" : "false") +
            ",\"scale_error\":" + std::to_string(v.scaleError) + ",\"scaled_clock_mhz\":" +
            (v.scaled ? number(v.scaledValue) : "null");
        if (!v.group.empty()) out += ",\"group\":" + quote(v.group);
        if (!v.unitEvidence.empty()) out += ",\"unit_evidence\":" + quote(v.unitEvidence) + ",\"capture_firmware_verified\":false";
        if (v.group == "granite_svi") out += ",\"firmware_channel\":" + std::to_string(v.firmwareChannel) +
            ",\"physical_rail_verified\":false,\"freshness_verified\":false,\"setting_readback_verified\":false";
        if (v.slotCount) out += ",\"slot\":" + std::to_string(v.slot) + ",\"slot_count\":" + std::to_string(v.slotCount);
        if (v.group == "core_storage" || v.group == "ccd_storage") out += ",\"row\":" + std::to_string(v.row);
        out += '}';
      }
    };
    appendFields(s.fields);
    out += "],\"original_limits_units_verified\":false,\"original_limits\":[";
    appendFields(s.originalLimits);
    out += "],\"original_package_units_verified\":false,\"original_package_targets_verified\":false,\"original_package\":[";
    appendFields(s.originalPackage);
    out += "],\"original_fabric_units_verified\":false,\"original_fabric_targets_verified\":false,\"original_fabric\":[";
    appendFields(s.originalFabric);
    out += "],\"original_arrays_units_verified\":false,\"original_arrays_targets_verified\":false,\"original_arrays\":[";
    appendFields(s.originalArrays);
    out += "],\"firmware_arrays_layout\":" + quote(s.graniteRidge ? "granite-ridge-18-core-12-ccd-rows" : "shimada-18-core-13-ccd-rows") + ",\"firmware_ccd_storage_slots\":" +
        std::to_string(s.firmwareCcdSlots) + ",\"firmware_arrays_units_verified\":false,\"firmware_arrays_targets_verified\":false," +
        "\"firmware_arrays_labels_verified\":false,\"firmware_core_arrays\":[";
    appendFields(s.firmwareCores);
    out += "],\"firmware_ccd_arrays\":[";
    appendFields(s.firmwareCcds);
    out += "],\"platform\":" + quote(s.graniteRidge ? "granite-ridge" : "shimada") + ",\"granite_stored_words\":[";
    appendFields(s.graniteRaw);
    return out + "]}";
}
AmdPmSnapshot parseAmdPmCapture(const std::string &input) {
    using capture_json::Json; using capture_json::check;
    AmdPmSnapshot out;
    try {
        const auto root = capture_json::Parser(input, amdPmCaptureMaximumBytes, 65536).parse();
        const Json *record = &root;
        if (root.has("schema_version")) {
            check(capture_json::uint(root.at("schema_version"), 1) == 1, "Unsupported CLI report version");
            const auto &command = root.at("command");
            check(command.kind == Json::String && (command.text == "amd-pm-read" || command.text == "amd-pm-decode" || command.text == "amd-pm-native-read"), "Not an AMD PM report");
            record = &root.at("data").at("pm");
        }
        check(capture_json::equals(record->at("format"), Json::String, "octool-amd-pm"), "Not an AMD PM capture");
        check(capture_json::uint(record->at("schema"), 1) == 1, "Unsupported PM capture schema");
        const auto version = capture_json::word(record->at("version"));
        const auto &raw = record->at("raw_hex");
        check(raw.kind == Json::String && raw.text.size() <= maximumBytes * 2 && raw.text.size() % 2 == 0, "Invalid PM raw byte length");
        std::vector<std::uint8_t> bytes;
        for (std::size_t i = 0; i < raw.text.size(); i += 2) {
            const auto a = capture_json::hexDigit(raw.text[i]), b = capture_json::hexDigit(raw.text[i + 1]);
            check(a < 16 && b < 16, "Invalid PM raw hex byte"); bytes.push_back(std::uint8_t(16 * a + b));
        }
        check(capture_json::uint(record->at("captured_bytes"), unsigned(maximumBytes)) == bytes.size(), "Captured byte count mismatch");
        AmdPmOptions options;
        const auto &bclk = record->at("bclk_mhz");
        if (bclk.kind != Json::Null) {
            check(bclk.kind == Json::Number, "BCLK must be a number or null");
            std::istringstream n(bclk.text); n.imbue(std::locale::classic());
            options.hasBclk = true;
            check(bool(n >> options.bclkMHz) && n.eof() && validOptions(options), "BCLK must be positive and finite");
        }
        out = decodeAmdPm(bytes, version, options);
    } catch (const std::invalid_argument &e) { out.error = -EINVAL; out.detail = e.what(); }
    return out;
}
AmdPmDriverRead readAmdPmDriver(const AmdPmReader &reader, const AmdPmOptions &options) {
    AmdPmDriverRead out;
    auto fail = [&](int error, const char *detail) {
        out.snapshot.error = error; out.snapshot.detail = detail; return out;
    };
    if (!reader || !validOptions(options)) return fail(-EINVAL, "Invalid PM reader or BCLK option");
    auto metadata = [&](std::uint32_t &version, std::uint64_t &size) {
        std::vector<std::uint8_t> bytes;
        int error = reader("pm_table_version", bytes, 8);
        if (error) return error;
        if (bytes.size() != 4) return -EINVAL;
        version = std::uint32_t(little(bytes, 0, 4));
        error = reader("pm_table_size", bytes, 8);
        if (error) return error;
        if (bytes.size() != 4 && bytes.size() != 8) return -EINVAL;
        size = little(bytes, 0, unsigned(bytes.size())); return 0;
    };
    std::uint32_t version = 0, afterVersion = 0; std::uint64_t size = 0, afterSize = 0;
    int error = metadata(version, size);
    if (error) return fail(error, "Cannot read binary PM table metadata from the installed driver");
    out.snapshot.version = version; out.snapshot.expectedBytes = amdPmTableSize(version); out.snapshot.options = options;
    if (!out.snapshot.expectedBytes) return fail(-ENOTSUP, "Unsupported exact PM table version; table was not read");
    if (size != out.snapshot.expectedBytes) return fail(-EMSGSIZE, "Driver PM table size disagrees with its version; table was not read");
    std::vector<std::uint8_t> bytes; out.tableReadAttempted = true;
    error = reader("pm_table", bytes, out.snapshot.expectedBytes);
    if (error) return fail(error, "Driver PM table read failed");
    error = metadata(afterVersion, afterSize);
    if (error) return fail(error, "Cannot recheck driver metadata; sample discarded");
    if (version != afterVersion || size != afterSize) return fail(-EAGAIN, "Driver PM metadata changed during read; sample discarded");
    out.snapshot = decodeAmdPm(bytes, version, options);
    return out;
}
} }
