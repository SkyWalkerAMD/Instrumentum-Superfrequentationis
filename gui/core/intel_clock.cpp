// SPDX-License-Identifier: GPL-2.0-only
#include "intel_clock.h"
#include "intel_mchbar.h"
#include "bounded_json.h"
#include <cerrno>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>

namespace octool { namespace core {
namespace {
std::string hex(std::uint64_t value, unsigned width) {
    std::ostringstream s; s.imbue(std::locale::classic());
    s << '"' << "0x" << std::hex << std::setw(int(width)) << std::setfill('0') << value << '"'; return s.str();
}
std::uint64_t word64(const capture_json::Json &v) {
    using namespace capture_json;
    check(v.kind == Json::String && v.text.size() == 18 && v.text.compare(0,2,"0x") == 0,"Expected 64-bit hex word");
    std::uint64_t result = 0;
    for (std::size_t i = 2; i < 18; ++i) { const auto d = hexDigit(v.text[i]); check(d < 16,"Invalid hex word"); result = (result<<4)|d; }
    return result;
}
}
const std::vector<IntelClockRegisterSpec> &intelClockRegisters() {
    // Intel 743846-001 sections 3.3.42, 3.3.43 and 3.3.51.
    static const std::vector<IntelClockRegisterSpec> specs = {{0x5e00,4},{0x5e04,4},{0x5f60,8}};
    return specs;
}
IntelClockSnapshot decodeIntelClock(const std::vector<IntelClockRegister> &registers) {
    IntelClockSnapshot out;
    if (registers.size() != 3) { out.error = -EINVAL; return out; }
    std::map<std::uint32_t,std::uint64_t> raw;
    for (const auto &r : registers) if (!raw.emplace(r.offset,r.raw).second) { out.error = -EINVAL; return out; }
    for (const auto &spec : intelClockRegisters()) {
        const auto it = raw.find(spec.offset);
        if (it == raw.end() || (spec.width == 4 && it->second > UINT32_MAX)) { out.error = -EINVAL; return out; }
        if (it->second == (spec.width == 4 ? UINT64_C(0xffffffff) : UINT64_MAX)) { out.error = -ENODATA; return out; }
    }
    if (raw[0x5e00] & (UINT64_C(1)<<31)) { out.error = -EAGAIN; return out; }
    const auto bclk = raw[0x5f60], data = raw[0x5e04];
    if (bclk >> 32) out.bclkError = -ENOTSUP;
    else if (!bclk || bclk == UINT32_MAX) out.bclkError = -ENODATA;
    else out.bclkKhz = std::uint32_t(bclk);
    const unsigned gear = unsigned((data>>12)&3), reference = unsigned((data>>8)&15), ratio = unsigned(data&255);
    // These values describe the last MRC request, never the current clock or
    // measured rail voltage. Ratio zero is the documented PLL-shutdown encoding.
    if ((data & 0x8001c000) || gear > 2 || reference > 1 || ratio == 1 || ratio == 2) out.mrcError = -ENOTSUP;
    else {
        out.mrcGear = 1u<<gear; out.mrcReference = reference; out.mrcRatio = ratio;
        out.mrcVddqMillivolts = unsigned((data>>17)&1023)*5;
        // The 4-bit field conflicts with the manual's 32 * 0.25 A example.
        // Preserve its encoding instead of publishing an unconfirmed current.
        out.mrcIccMaxEncoded = unsigned((data>>27)&15);
    }
    out.registers = registers; out.valid = true; return out;
}
IntelClockSnapshot readIntelClock(HardwareSession &s, unsigned cpu) {
    IntelClockSnapshot out; out.cpu = cpu; out.offline = false;
    if ((out.error = intel_mchbar::identify(s,cpu,out.identity))) return out;
    if ((out.error = intel_mchbar::readContext(s,out.mchbar))) return out;
    out.pciIdentity = 0xa7008086;
    std::vector<IntelClockRegister> registers;
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (const auto &spec : intelClockRegisters()) {
            if ((out.error = s.checkpoint())) return out;
            Request req; req.space = Space::Memory; req.width = spec.width;
            req.address = (out.mchbar & intel_mchbar::baseMask) + spec.offset;
            const auto r = s.execute(req); if ((out.error = r.error)) return out;
            if (spec.offset == 0x5e00) {
                if (r.value == UINT32_MAX) { out.error = -ENODATA; return out; }
                if (r.value & (UINT64_C(1)<<31)) { out.error = -EAGAIN; return out; }
            }
            if (!pass) { IntelClockRegister v; v.offset = spec.offset; v.raw = r.value; registers.push_back(v); }
            else for (const auto &v : registers) if (v.offset == spec.offset && v.raw != r.value) { out.error = -EAGAIN; return out; }
        }
        std::uint64_t bar = 0;
        if ((out.error = intel_mchbar::readContext(s,bar))) return out;
        if (bar != out.mchbar) { out.error = -EAGAIN; return out; }
    }
    const auto decoded = decodeIntelClock(registers); out.error = decoded.error;
    if (!out.error) {
        const auto id = out.identity; const auto bar = out.mchbar;
        out = decoded; out.cpu = cpu; out.offline = false; out.identity = id; out.mchbar = bar; out.pciIdentity = 0xa7008086;
    }
    return out;
}
std::string intelClockJson(const IntelClockSnapshot &s) {
    if (!s.valid || s.error) return "null";
    const auto d = decodeIntelClock(s.registers); if (d.error) return "null";
    const auto value = [](unsigned n, int error) { return error ? std::string("null") : std::to_string(n); };
    std::string out = "{\"format\":\"octool-intel-clock-v1\",\"layout\":\"raptor-lake-s-743846-001\",\"offline\":" +
        std::string(s.offline ? "true" : "false") + ",\"origin_verified\":false,\"atomic_snapshot_guaranteed\":false,\"current_memory_frequency_available\":false,\"registers\":[";
    for (const auto &r : s.registers) {
        if (out.back() != '[') out += ',';
        out += "{\"offset\":" + hex(r.offset,8) + ",\"raw\":" + hex(r.raw,16) + '}';
    }
    out += "],\"bclk\":{\"error\":" + std::to_string(d.bclkError) + ",\"reported_khz\":" + value(d.bclkKhz,d.bclkError) +
        "},\"last_mrc_request\":{\"error\":" + std::to_string(d.mrcError) + ",\"gear_ratio\":" + value(d.mrcGear,d.mrcError) +
        ",\"reference_clock_code\":" + value(d.mrcReference,d.mrcError) + ",\"qclk_ratio_encoded\":" + value(d.mrcRatio,d.mrcError) +
        ",\"pll_shutdown_encoded\":" + (d.mrcError ? "null" : d.mrcRatio ? "false" : "true") +
        ",\"vddq_tx_mv\":" + value(d.mrcVddqMillivolts,d.mrcError) + ",\"vddq_tx_iccmax_encoded\":" + value(d.mrcIccMaxEncoded,d.mrcError) + "}}";
    return out;
}
IntelClockSnapshot parseIntelClockCapture(const std::string &json) {
    using namespace capture_json;
    IntelClockSnapshot out;
    try {
        const auto root = Parser(json).parse(); const Json *data = &root;
        check(root.kind == Json::Object,"Expected snapshot object");
        if (root.has("schema_version")) {
            check(!root.has("format") && uint(root.at("schema_version"),1) == 1 && equals(root.at("ok"),Json::Boolean,"true") &&
                uint(root.at("error"),0) == 0,"Only successful schema v1 reports can be imported");
            check(equals(root.at("command"),Json::String,"intel-clock-read") || equals(root.at("command"),Json::String,"intel-clock-decode"),"Not an Intel clock report");
            data = &root.at("data").at("clock");
        }
        check(equals(data->at("format"),Json::String,"octool-intel-clock-v1") && equals(data->at("layout"),Json::String,"raptor-lake-s-743846-001"),"Unsupported Intel clock layout");
        const auto &regs = data->at("registers"); check(regs.kind == Json::Array && regs.items.size() == 3,"Expected all three PCU registers");
        std::vector<IntelClockRegister> registers;
        for (const auto &item : regs.items) { IntelClockRegister r; r.offset = word(item.at("offset")); r.raw = word64(item.at("raw")); registers.push_back(r); }
        out = decodeIntelClock(registers); if (out.error) out.detail = "Invalid or busy PCU register snapshot";
    } catch (const std::invalid_argument &e) { out = IntelClockSnapshot{}; out.error = -EINVAL; out.detail = e.what(); }
    return out;
}
} }
