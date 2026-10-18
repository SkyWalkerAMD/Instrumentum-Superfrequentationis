// SPDX-License-Identifier: GPL-2.0-only
#include "intel_memory.h"
#include "intel_mchbar.h"
#include "bounded_json.h"
#include <cerrno>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>

namespace octool { namespace core {
namespace {
using intel_mchbar::baseMask;
std::string hex(std::uint64_t value, unsigned width) {
    std::ostringstream s; s.imbue(std::locale::classic());
    s << "\"0x" << std::hex << std::setw(int(width)) << std::setfill('0') << value << '"'; return s.str();
}
std::uint64_t word64(const capture_json::Json &v) {
    using namespace capture_json;
    check(v.kind == Json::String && v.text.size() == 18 && v.text.compare(0, 2, "0x") == 0, "Expected 64-bit hex word");
    std::uint64_t result = 0;
    for (std::size_t i = 2; i < 18; ++i) { const auto d = hexDigit(v.text[i]); check(d < 16, "Invalid hex word"); result = (result << 4) | d; }
    return result;
}
}
const std::vector<IntelMemoryRegisterSpec> &intelMemoryRegisters(unsigned version) {
    // Intel 743846-001, sections 3.2.46, 3.2.49-50, 3.2.70-76, 3.2.79-83,
    // 3.2.89-91, 3.2.93-94, 3.2.100 and 3.2.103. Context precedes timing registers.
    static const std::vector<IntelMemoryRegisterSpec> specs = {
        {0xd800,4}, {0xd80c,4}, {0xe454,4}, {0xe088,8}, {0xe000,8}, {0xe008,4},
        {0xe00c,4}, {0xe010,4}, {0xe014,4}, {0xe018,4}, {0xe070,8}, {0xe43c,4},
        {0xe020,8}, {0xe050,8}, {0xe438,4}, {0xe440,4}, {0xe448,8}, {0xe4c0,8}, {0xe0b8,4},
        {0xe080,4}, {0xe478,4}
    };
    static const std::vector<IntelMemoryRegisterSpec> v1(specs.begin(), specs.begin()+12), v2(specs.begin(), specs.begin()+14),
        v3(specs.begin(), specs.begin()+18), v4(specs.begin(), specs.begin()+19), empty;
    return version == 5 ? specs : version == 4 ? v4 : version == 3 ? v3 : version == 2 ? v2 : version == 1 ? v1 : empty;
}
const std::vector<IntelMemoryField> &intelMemoryFields(unsigned version) {
    static const std::vector<IntelMemoryField> fields = {
        {"tCL",0xe070,16,22,3}, {"tRCD",0xe000,51,58,3}, {"tRP",0xe000,0,7,3},
        {"tRAS",0xe000,42,50,3}, {"tCWL",0xe070,24,31,3},
        {"tWRPRE",0xe000,32,41,3}, {"tRDPRE",0xe000,13,19,3}, {"tPPD",0xe000,20,23,1},
        {"tFAW",0xe008,0,8,3}, {"tRRD_SG",0xe008,9,14,3}, {"tRRD_DG",0xe008,15,21,3},
        {"tREFSBRD",0xe008,24,31,2}, {"tRFC",0xe43c,18,30,3}, {"tREFI",0xe43c,0,17,3},
        {"tRDRD_SG",0xe00c,0,6,3}, {"tRDRD_DG",0xe00c,8,14,3},
        {"tRDRD_DR",0xe00c,16,23,3}, {"tRDRD_DD",0xe00c,24,31,3},
        {"tRDWR_SG",0xe010,0,7,3}, {"tRDWR_DG",0xe010,8,15,3},
        {"tRDWR_DR",0xe010,16,23,3}, {"tRDWR_DD",0xe010,24,31,3},
        {"tWRRD_SG",0xe014,0,8,3}, {"tWRRD_DG",0xe014,9,17,3},
        {"tWRRD_DR",0xe014,18,24,3}, {"tWRRD_DD",0xe014,25,31,3},
        {"tWRWR_SG",0xe018,0,6,3}, {"tWRWR_DG",0xe018,8,14,3},
        {"tWRWR_DR",0xe018,16,22,3}, {"tWRWR_DD",0xe018,24,31,3},
        {"RTL_Rank0",0xe020,0,7,3,"QCLK",1}, {"RTL_Rank1",0xe020,8,15,3,"QCLK",2},
        {"RTL_Rank2",0xe020,16,23,3,"QCLK",4}, {"RTL_Rank3",0xe020,24,31,3,"QCLK",8},
        {"tPRPDEN",0xe050,59,63,3}, {"tCSL",0xe050,48,53,3,"MC_DCLK"},
        {"tCSH",0xe050,42,47,3,"MC_DCLK"}, {"tWRPDEN",0xe050,32,41,3},
        {"tRDPDEN",0xe050,21,28,3}, {"tXPDLL",0xe050,14,20,1},
        {"tXP",0xe050,7,13,3}, {"tCKE",0xe050,0,6,3},
        {"tCPDED",0xe088,56,60,1,"tCK",0,true},
        // These are programmed policy/delay codes, including zero. Reading
        // them does not validate firmware configuration against recommendations.
        {"tREFIx9",0xe438,24,31,3,"1024_DCLK",0,true},
        {"Refresh_block_wait",0xe438,20,23,3,"MC_DCLK",0,true},
        {"Self_refresh_exit_debits",0xe438,18,19,3,"count",0,true},
        {"High_priority_refresh_on_MRS",0xe438,17,17,3,"flag",0,true},
        {"Count_tREFI_when_refresh_disabled",0xe438,16,16,3,"flag",0,true},
        {"Refresh_panic_watermark",0xe438,12,15,3,"tREFI_count",0,true},
        {"Refresh_high_priority_watermark",0xe438,8,11,3,"tREFI_count",0,true},
        {"Opportunistic_refresh_idle",0xe438,0,7,3,"DCLK",0,true},
        // 3.2.91 and 3.2.103 do not specify units for TXSDLL / TSR.
        {"tXSDLL",0xe440,0,12,3,"encoded",0,true},
        {"tZQCAL",0xe448,32,44,2,"2*tCK",0,true},
        {"tZQCS",0xe448,10,20,1,"tCK",0,true},
        {"tZQLAT",0xe448,10,20,2,"tCK",0,true},
        {"tCKESR",0xe4c0,52,57,1,"encoded",0,true},
        {"Self_refresh_tCSL",0xe4c0,52,57,2,"encoded",0,true},
        // 3.2.83: these are permission/configuration bits and programmed
        // durations, not evidence that a power state is currently active.
        {"DDRIO_self_refresh_enable",0xe0b8,31,31,3,"flag",0,true},
        {"DDRIO_power_down_enable",0xe0b8,30,30,3,"flag",0,true},
        {"DDRIO_idle_enable",0xe0b8,29,29,3,"flag",0,true},
        {"DDRIO_CKE_valid_enable",0xe0b8,28,28,3,"flag",0,true},
        {"DDRIO_CKE_valid_length",0xe0b8,24,27,3,"DCLK",0,true},
        {"DDRIO_self_refresh_length",0xe0b8,20,23,3,"MC_DCLK",0,true},
        {"DDRIO_self_refresh_latency",0xe0b8,14,19,3,"8*tCK",0,true},
        {"DDRIO_power_down_length",0xe0b8,10,13,3,"MC_DCLK",0,true},
        {"DDRIO_power_down_latency",0xe0b8,5,9,3,"tCK",0,true},
        {"DDRIO_idle_length",0xe0b8,1,4,3,"encoded",0,true},
        {"DDRIO_raise_CKE_after_exit",0xe0b8,0,0,3,"flag",0,true},
        // 3.2.81: each mask selects ODT pins 3:0 when accessing this target
        // rank. These are stored masks; the matrix enable bit is not defined
        // in the published SC_GS_CFG table. No active termination is inferred.
        {"ODT_read_rank0_mask",0xe080,0,3,3,"odt_pin_mask",1,true},
        {"ODT_read_rank1_mask",0xe080,4,7,3,"odt_pin_mask",2,true},
        {"ODT_read_rank2_mask",0xe080,8,11,3,"odt_pin_mask",4,true},
        {"ODT_read_rank3_mask",0xe080,12,15,3,"odt_pin_mask",8,true},
        {"ODT_write_rank0_mask",0xe080,16,19,3,"odt_pin_mask",1,true},
        {"ODT_write_rank1_mask",0xe080,20,23,3,"odt_pin_mask",2,true},
        {"ODT_write_rank2_mask",0xe080,24,27,3,"odt_pin_mask",4,true},
        {"ODT_write_rank3_mask",0xe080,28,31,3,"odt_pin_mask",8,true},
        // 3.2.100: additions/subtractions are DCLK counts. The Gear2 1QCLK
        // option is a configuration flag, not a duration in this clock domain.
        // Do not combine these with tCWL to claim an effective delay or IOL.
        {"Write_delay_add_tCWL",0xe478,6,11,3,"DCLK",0,true},
        {"Write_delay_subtract_tCWL",0xe478,0,5,3,"DCLK",0,true},
        {"Write_delay_add_1QCLK",0xe478,12,12,3,"flag",0,true}
    };
    static const std::vector<IntelMemoryField> v1(fields.begin(), fields.begin()+30), v2(fields.begin(), fields.begin()+43),
        v3(fields.begin(), fields.begin()+57), v4(fields.begin(), fields.begin()+68), empty;
    return version == 5 ? fields : version == 4 ? v4 : version == 3 ? v3 : version == 2 ? v2 : version == 1 ? v1 : empty;
}
const char *intelMemoryUnit(const IntelMemorySnapshot &s, const IntelMemoryField &f) {
    // QCLK and MC DCLK are already controller-domain quantities. Do not apply
    // the DRAM timing divide override, or guess a tCK/ns conversion, to them.
    const std::string unit = f.unit;
    return (unit == "tCK" || unit == "2*tCK" || unit == "8*tCK") && (s.noGear2Divide || s.noGear4Divide) ? "encoded" : f.unit;
}
std::uint32_t intelMemoryOffset(const IntelMemoryTarget &t, std::uint32_t offset) {
    return offset + t.controller * 0x10000 + (offset == 0xd800 ? 0 : t.channel * (offset == 0xd80c ? 4 : 0x800));
}
IntelMemorySnapshot decodeIntelMemory(const IntelMemoryTarget &t, const std::vector<IntelMemoryRegister> &registers, unsigned version) {
    IntelMemorySnapshot out; out.target = t; out.version = version;
    const auto &specs = intelMemoryRegisters(version);
    if (specs.empty() || t.controller > 1 || t.channel > 1 || registers.size() != specs.size()) { out.error = -EINVAL; return out; }
    std::map<std::uint32_t, std::uint64_t> raw;
    for (const auto &r : registers) {
        if (!raw.emplace(r.offset, r.raw).second) { out.error = -EINVAL; return out; }
    }
    for (const auto &spec : specs) {
        const auto it = raw.find(spec.offset);
        if (it == raw.end() || (spec.width == 4 && it->second > UINT32_MAX)) { out.error = -EINVAL; return out; }
        const auto full = spec.width == 4 ? UINT64_C(0xffffffff) : UINT64_MAX;
        const bool policy = spec.offset == 0xe438 || spec.offset == 0xe440 || spec.offset == 0xe448 || spec.offset == 0xe4c0 ||
            spec.offset == 0xe0b8 || spec.offset == 0xe080 || spec.offset == 0xe478;
        if (it->second == full || (!it->second && spec.offset != 0xd800 && spec.offset != 0xe088 && !policy)) { out.error = -ENODATA; return out; }
    }
    out.dramType = unsigned(raw[0xd800] & 7);
    if (out.dramType > 1) { out.error = -ENOTSUP; return out; }
    if (!out.dramType && t.channel) { out.error = -ENODEV; return out; }
    // Rank occupancy resets to 0x0f even for an unpopulated channel. A real
    // channel also requires a nonzero DIMM L or S size (section 3.2.49).
    if (!(raw[0xd80c] & 0x007f007f)) { out.error = -ENODATA; return out; }
    const auto occupancy = raw[0xe454];
    if (!occupancy || (occupancy & ~UINT64_C(15))) { out.error = -ENODATA; return out; }
    out.rankMask = unsigned(occupancy);
    const auto scheduler = raw[0xe088];
    out.gear2 = (scheduler & (UINT64_C(1) << 31)) != 0;
    out.gear4 = (scheduler & (UINT64_C(1) << 15)) != 0;
    out.noGear2Divide = (scheduler & (UINT64_C(1) << 30)) != 0;
    out.noGear4Divide = (scheduler & (UINT64_C(1) << 16)) != 0;
    out.commandStretch = unsigned((scheduler >> 3) & 3); out.nTo1 = unsigned((scheduler >> 5) & 7);
    for (const auto &field : intelMemoryFields(version)) {
        IntelMemoryValue v;
        v.encoded = unsigned((raw[field.offset] >> field.low) & ((UINT64_C(1) << (field.high - field.low + 1)) - 1));
        if (!(field.dramTypes & (1u << out.dramType))) v.error = -ENOTSUP;
        else if (field.rankBit && !(out.rankMask & field.rankBit)) v.error = -ENODEV;
        // Do not turn an uninitialized register into a timing measurement.
        // tCPDED and subsequent policy/delay codes preserve zero as configured.
        // Absent ranks have no RTL measurement or applicable ODT target mask.
        else if (!v.encoded && !field.zeroAllowed) { out.error = -ENODATA; out.values.clear(); return out; }
        out.values.push_back(v);
    }
    out.registers = registers; out.valid = true; return out;
}
IntelMemorySnapshot readIntelMemory(HardwareSession &s, const IntelMemoryTarget &t) {
    IntelMemorySnapshot out; out.target = t; out.offline = false;
    if (t.controller > 1 || t.channel > 1) { out.error = -EINVAL; return out; }
    if ((out.error = intel_mchbar::identify(s,t.cpu,out.identity))) return out;
    if ((out.error = intel_mchbar::readContext(s,out.mchbar))) return out;
    out.pciIdentity = 0xa7008086;
    std::vector<IntelMemoryRegister> registers;
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (const auto &spec : intelMemoryRegisters()) {
            if ((out.error = s.checkpoint())) return out;
            Request req; req.space = Space::Memory; req.width = spec.width;
            req.address = (out.mchbar & baseMask) + intelMemoryOffset(t, spec.offset);
            const auto r = s.execute(req); if ((out.error = r.error)) return out;
            if (spec.offset == 0xd800) {
                if (r.value == UINT32_MAX || (r.value & 7) > 1) { out.error = -ENOTSUP; return out; }
                if (!(r.value & 7) && t.channel) { out.error = -ENODEV; return out; }
            }
            if (spec.offset == 0xe454 && (!r.value || (r.value & ~UINT64_C(15)))) { out.error = -ENODATA; return out; }
            if (spec.offset == 0xd80c && (r.value == UINT32_MAX || !(r.value & 0x007f007f))) { out.error = -ENODATA; return out; }
            if (!pass) { IntelMemoryRegister v; v.offset = spec.offset; v.raw = r.value; registers.push_back(v); }
            else {
                for (const auto &v : registers) if (v.offset == spec.offset && v.raw != r.value) { out.error = -EAGAIN; return out; }
            }
        }
        std::uint64_t bar = 0;
        if ((out.error = intel_mchbar::readContext(s,bar))) return out;
        if (bar != out.mchbar) { out.error = -EAGAIN; return out; }
    }
    const auto decoded = decodeIntelMemory(t, registers); out.error = decoded.error;
    if (!out.error) {
        const auto id = out.identity; const auto bar = out.mchbar;
        out = decoded; out.offline = false; out.identity = id; out.mchbar = bar; out.pciIdentity = 0xa7008086;
    }
    return out;
}
std::string intelMemoryJson(const IntelMemorySnapshot &s) {
    if (!s.valid || s.error) return "null";
    const auto d = decodeIntelMemory(s.target, s.registers, s.version);
    if (d.error) return "null";
    const auto boolean = [](bool v) { return v ? "true" : "false"; };
    std::string out = "{\"format\":\"octool-intel-memory-v" + std::to_string(d.version) + "\",\"layout\":\"raptor-lake-s-743846-001\",\"controller\":" + std::to_string(s.target.controller) +
        ",\"channel\":" + std::to_string(s.target.channel) + ",\"dram_type\":\"" + (d.dramType ? "DDR5" : "DDR4") +
        "\",\"rank_mask\":" + std::to_string(d.rankMask) + ",\"gear2\":" + boolean(d.gear2) + ",\"gear4\":" + boolean(d.gear4) +
        ",\"no_gear2_divide\":" + boolean(d.noGear2Divide) + ",\"no_gear4_divide\":" + boolean(d.noGear4Divide) +
        ",\"command_stretch_encoded\":" + std::to_string(d.commandStretch) + ",\"n_to_1_encoded\":" + std::to_string(d.nTo1) +
        ",\"offline\":" + boolean(s.offline) + ",\"origin_verified\":false,\"atomic_snapshot_guaranteed\":false,\"registers\":[";
    for (const auto &r : s.registers) {
        if (out.back() != '[') out += ',';
        out += "{\"offset\":" + hex(r.offset, 8) + ",\"raw\":" + hex(r.raw, 16) + '}';
    }
    out += "],\"fields\":[";
    const auto &fields = intelMemoryFields(d.version);
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i) out += ',';
        const auto &v = d.values[i]; const auto &f = fields[i];
        out += "{\"name\":\"" + std::string(f.name) + "\",\"offset\":" + hex(f.offset, 8) + ",\"low\":" + std::to_string(f.low) +
            ",\"high\":" + std::to_string(f.high) + ",\"error\":" + std::to_string(v.error) + ",\"encoded\":" +
            (v.error ? "null" : std::to_string(v.encoded)) + ",\"unit\":" + (v.error ? "null" : "\"" + std::string(intelMemoryUnit(d,f)) + "\"") + '}';
    }
    return out + "]}";
}
IntelMemorySnapshot parseIntelMemoryCapture(const std::string &json) {
    using namespace capture_json;
    IntelMemorySnapshot out;
    try {
        const auto root = Parser(json).parse(); const Json *data = &root;
        check(root.kind == Json::Object, "Expected snapshot object");
        if (root.has("schema_version")) {
            check(!root.has("format") && uint(root.at("schema_version"), 1) == 1 && equals(root.at("ok"), Json::Boolean, "true") &&
                uint(root.at("error"), 0) == 0, "Only successful schema v1 reports can be imported");
            check(equals(root.at("command"), Json::String, "intel-memory-read") || equals(root.at("command"), Json::String, "intel-memory-decode"), "Not an Intel memory report");
            data = &root.at("data").at("memory");
        }
        const unsigned version = equals(data->at("format"), Json::String, "octool-intel-memory-v1") ? 1 :
            equals(data->at("format"), Json::String, "octool-intel-memory-v2") ? 2 :
            equals(data->at("format"), Json::String, "octool-intel-memory-v3") ? 3 :
            equals(data->at("format"), Json::String, "octool-intel-memory-v4") ? 4 :
            equals(data->at("format"), Json::String, "octool-intel-memory-v5") ? 5 : 0;
        check(version &&
            equals(data->at("layout"), Json::String, "raptor-lake-s-743846-001"), "Unsupported Intel memory layout");
        IntelMemoryTarget t; t.controller = uint(data->at("controller"), 1); t.channel = uint(data->at("channel"), 1);
        const auto &regs = data->at("registers");
        check(regs.kind == Json::Array && regs.items.size() == intelMemoryRegisters(version).size(), "Expected a complete register snapshot for the declared version");
        std::vector<IntelMemoryRegister> registers;
        for (const auto &item : regs.items) { IntelMemoryRegister r; r.offset = word(item.at("offset")); r.raw = word64(item.at("raw")); registers.push_back(r); }
        out = decodeIntelMemory(t, registers, version);
        if (out.error) out.detail = "Invalid, absent or unsupported channel register geometry";
    } catch (const std::invalid_argument &e) { out = IntelMemorySnapshot{}; out.error = -EINVAL; out.detail = e.what(); }
    return out;
}
} }
