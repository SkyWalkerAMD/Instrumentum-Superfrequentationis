// SPDX-License-Identifier: GPL-2.0-only
#include "commands.h"
#include "json.h"
#include "core/amd_curve.h"
#include "core/amd_pstates.h"
#include "core/amd_smu.h"
#include "core/amd_topology.h"
#include "core/amd_umc.h"
#include "core/umc_capture.h"
#include "core/intel_controls.h"
#include "core/intel_oc.h"
#include "core/intel_turbo.h"
#include "core/intel_uncore.h"
#include "core/spd.h"
#include "platform/linux_inventory_native.h"
#include "platform/system_info.h"
#include <cerrno>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/utsname.h>
#include <unistd.h>

namespace octool { namespace cli {
namespace {
using namespace core;
using Options = std::map<std::string, std::string>;
struct Result { int error = 0; Fields data; };
std::string hex(std::uint64_t n, unsigned width = 8) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "0x" << std::hex << std::setw(int(width)) << std::setfill('0') << n; return quote(out.str());
}
void require(bool condition, const std::string &message) {
    if (!condition) throw std::invalid_argument(message);
}
std::string get(const Options &o, const std::string &key) {
    auto i = o.find(key); require(i != o.end(), "Required option: " + key); return i->second;
}
std::uint64_t wideInteger(const std::string &text, std::uint64_t maximum, const std::string &key) {
    const bool hexadecimal = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    std::uint64_t value = 0; const unsigned base = hexadecimal ? 16 : 10;
    require(!text.empty(), "Empty number: " + key);
    for (std::size_t i = hexadecimal ? 2 : 0; i < text.size(); ++i) {
        const char c = text[i];
        const unsigned digit = c >= '0' && c <= '9' ? unsigned(c - '0') : c >= 'a' && c <= 'f' ? unsigned(c - 'a' + 10) : c >= 'A' && c <= 'F' ? unsigned(c - 'A' + 10) : 255;
        require(digit < base, "Invalid unsigned number: " + key);
        require(digit <= maximum && value <= (maximum - digit) / base, "Number out of range: " + key);
        value = value * base + digit;
    }
    return value;
}
unsigned uintOption(const Options &o, const std::string &key, unsigned max) { return unsigned(wideInteger(get(o, key), max, key)); }
unsigned optional(const Options &o, const std::string &key, unsigned max, unsigned fallback = 0) {
    return o.count(key) ? uintOption(o, key, max) : fallback;
}
double decimal(const std::string &text) {
    std::istringstream in(text); in.imbue(std::locale::classic()); double value = 0;
    require(!text.empty() && text.find_first_of(" \t\r\n") == std::string::npos &&
        bool(in >> value) && in.eof() && std::isfinite(value), "--value must be a finite decimal number");
    return value;
}
const std::map<std::string, IntelField> intelFields = {
    {"pl1", IntelField::Pl1}, {"pl2", IntelField::Pl2}, {"pl1-enable", IntelField::Pl1Enable},
    {"pl2-enable", IntelField::Pl2Enable}, {"pl1-clamp", IntelField::Pl1Clamp}, {"pl2-clamp", IntelField::Pl2Clamp},
    {"pl1-window", IntelField::Pl1Window}, {"pl2-window", IntelField::Pl2Window},
    {"hwp-min", IntelField::HwpMin}, {"hwp-max", IntelField::HwpMax}, {"hwp-desired", IntelField::HwpDesired}, {"hwp-epp", IntelField::HwpEpp},
    {"hwp-window-us", IntelField::HwpActivityWindow}
};
struct Command {
    std::string name, path, otherPath, field;
    unsigned cpu = 0, timeout = 10000, ccd = 0, core = 0, point = 0, group = 0;
    unsigned minimumRatio = 0, maximumRatio = 0;
    double value = 0;
    bool forEdit = false;
    SmuTarget smu; SmuCommand message; UmcTarget umc;
    Request reg;
    IntelOcDomain domain = IntelOcDomain::Core;
    IntelOcVoltageMode mode = IntelOcVoltageMode::Adaptive;
    IntelTurboKind turboKind = IntelTurboKind::Primary;
};
Command parse(const std::vector<std::string> &args) {
    Command c; c.name = args.at(0);
    const std::set<std::string> hardware = {"cpu", "amd-pstates", "intel-read", "intel-set", "intel-oc-read", "intel-oc-set", "intel-vf-read", "intel-vf-set", "intel-turbo-read", "intel-turbo-set",
        "intel-uncore-read", "intel-uncore-set", "amd-smu-probe", "amd-smu-read", "amd-smu-send", "amd-curve-read", "amd-topology", "amd-umc-read", "register-read", "register-write"};
    const bool umcOffline = c.name == "amd-umc-decode" || c.name == "amd-umc-diff";
    require(hardware.count(c.name) || c.name == "diagnose" || c.name == "inventory" || c.name == "spd-decode" || umcOffline, "Unknown command: " + c.name);
    std::set<std::string> keys = {"--json"};
    if (hardware.count(c.name)) { keys.insert("--cpu"); keys.insert("--timeout-ms"); }
    const bool rawRegister = c.name == "register-read" || c.name == "register-write";
    const bool writing = c.name == "intel-set" || c.name == "intel-oc-set" || c.name == "intel-vf-set" || c.name == "intel-turbo-set" || c.name == "intel-uncore-set" || c.name == "amd-smu-send" || c.name == "register-write";
    const bool vf = c.name == "intel-vf-read" || c.name == "intel-vf-set";
    const bool turbo = c.name == "intel-turbo-read" || c.name == "intel-turbo-set";
    if (turbo) keys.insert("--core-type");
    if (c.name == "intel-turbo-set") { keys.insert("--group"); keys.insert("--value"); }
    if (c.name == "intel-uncore-set") { keys.insert("--minimum-ratio"); keys.insert("--maximum-ratio"); }
    if (rawRegister) {
        for (const char *key : {"--space", "--address", "--width", "--bus", "--device", "--function"}) keys.insert(key);
        if (writing) keys.insert("--value");
    }
    if (writing) keys.insert("--apply");
    if (c.name == "intel-set" || c.name == "intel-oc-set") { keys.insert("--field"); keys.insert("--value"); }
    if (c.name == "intel-oc-read" || c.name == "intel-oc-set" || vf) keys.insert("--domain");
    if (c.name == "intel-oc-set") keys.insert("--mode");
    if (vf) keys.insert("--point");
    if (c.name == "intel-vf-read") keys.insert("--for-edit");
    if (c.name == "intel-vf-set") keys.insert("--value");
    const bool smu = c.name == "amd-smu-probe" || c.name == "amd-smu-read" || c.name == "amd-smu-send";
    if (smu) keys.insert("--profile");
    if (smu || c.name == "amd-umc-read") for (const char *key : {"--bus", "--device", "--function"}) keys.insert(key);
    if (c.name == "amd-smu-read" || c.name == "amd-smu-send") keys.insert("--message");
    if (c.name == "amd-smu-send") for (unsigned i = 0; i < 6; ++i) keys.insert("--arg" + std::to_string(i));
    if (c.name == "amd-curve-read") { keys.insert("--ccd"); keys.insert("--core"); }
    if (c.name == "amd-umc-read") { keys.insert("--bank"); keys.insert("--refresh-slot"); }
    if (c.name == "spd-decode") keys.insert("--file");
    if (umcOffline) keys.insert("--file");
    if (c.name == "amd-umc-diff") keys.insert("--compare");
    Options options;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto key = args[i];
        require(keys.count(key) != 0, "Unknown option for " + c.name + ": " + key);
        require(!options.count(key), "Duplicate option: " + key);
        const bool flag = key == "--json" || key == "--apply" || key == "--for-edit";
        if (!flag) require(i + 1 < args.size() && args[i + 1].compare(0, 2, "--") != 0, "Missing value: " + key);
        options[key] = flag ? "true" : args[++i];
    }
    if (rawRegister) {
        const auto space = get(options, "--space");
        require(space == "msr" || space == "mmio" || space == "pci", "--space must be msr, mmio or pci");
        c.reg.space = space == "msr" ? Space::Msr : space == "pci" ? Space::Pci : Space::Memory;
        c.reg.write = writing;
        c.reg.width = c.reg.space == Space::Msr ? int(optional(options, "--width", 8, 8)) : int(uintOption(options, "--width", 8));
        c.reg.address = wideInteger(get(options, "--address"), c.reg.space == Space::Msr ? UINT32_MAX : c.reg.space == Space::Pci ? 255 : UINT64_MAX, "--address");
        if (writing) c.reg.value = wideInteger(get(options, "--value"), UINT64_MAX, "--value");
        if (c.reg.space == Space::Pci) {
            c.reg.bus = uintOption(options, "--bus", 255); c.reg.device = uintOption(options, "--device", 31);
            c.reg.function = uintOption(options, "--function", 7);
        } else {
            require(!options.count("--bus") && !options.count("--device") && !options.count("--function"), "PCI target options require --space pci");
        }
        require(validateRequest(c.reg) == ValidationError::None, "Invalid register width, alignment, address range or value width");
    }
    if (hardware.count(c.name)) {
        if (!rawRegister || c.reg.space == Space::Msr) c.cpu = uintOption(options, "--cpu", 1048575);
        else require(!options.count("--cpu"), "--cpu is only used with MSR registers; select an address or PCI target instead");
        c.reg.cpu = c.cpu;
        c.timeout = optional(options, "--timeout-ms", 120000, 10000);
        require(c.timeout > 0, "--timeout-ms must be 1..120000");
    }
    if (writing) require(options.count("--apply") != 0, "Settings commands require --apply; nothing was opened or changed");
    if (c.name == "intel-uncore-set") {
        c.minimumRatio = uintOption(options, "--minimum-ratio", 127);
        c.maximumRatio = uintOption(options, "--maximum-ratio", 127);
        std::uint64_t encoded = 0;
        require(!encodeIntelUncoreRange(0, c.minimumRatio, c.maximumRatio, encoded), "Ring / LLC range requires 1 <= minimum <= maximum <= 127");
    }
    if (turbo) {
        const auto kind = get(options, "--core-type");
        require(kind == "p" || kind == "e", "--core-type must be p or e");
        c.turboKind = kind == "p" ? IntelTurboKind::Primary : IntelTurboKind::Secondary;
        if (writing) {
            c.group = uintOption(options, "--group", 7); c.value = decimal(get(options, "--value"));
            require(c.value >= 1 && c.value <= 85 && std::floor(c.value) == c.value, "Turbo group ratio must be an integer 1..85");
        }
    }
    if (c.name == "intel-set" || c.name == "intel-oc-set") {
        c.field = get(options, "--field"); c.value = decimal(get(options, "--value"));
        if (c.name == "intel-set") {
            const auto field = intelFields.find(c.field);
            require(field != intelFields.end(), "Unknown Intel control field: " + c.field);
            const auto f = field->second;
            if (f == IntelField::Pl1 || f == IntelField::Pl2) require(c.value > 0 && c.value <= 32767, "Power must be >0 and <=32767 W; hardware units impose further limits");
            else if (f == IntelField::Pl1Window || f == IntelField::Pl2Window) require(c.value > 0 && c.value <= raplWindowSeconds(127, 0), "Averaging window is out of range");
            else if (f <= IntelField::Pl2Clamp) require(c.value == 0 || c.value == 1, "Enable/clamp must be 0 or 1");
            else if (f == IntelField::HwpActivityWindow) require(c.value >= 0 && c.value <= 1270000000 && std::floor(c.value) == c.value, "HWP window must be an integer 0..1270000000 us (0 = automatic)");
            else require(c.value >= 0 && c.value <= 255 && std::floor(c.value) == c.value, "HWP value must be an integer 0..255");
        } else {
            require(c.field == "offset-mv" || c.field == "max-ratio" || c.field == "target-mv", "OC field must be offset-mv, max-ratio or target-mv");
            std::uint32_t encoded = 0;
            if (c.field == "target-mv") {
                const auto mode = get(options, "--mode");
                require(mode == "adaptive" || mode == "override", "--mode must be adaptive or override");
                c.mode = mode == "adaptive" ? IntelOcVoltageMode::Adaptive : IntelOcVoltageMode::Override;
                require(c.value >= 1 && c.value <= 2000 && std::floor(c.value) == c.value, "Voltage target must be an integer 1..2000 mV");
                require(!encodeIntelOcVoltage(unsigned(c.value), c.mode, 0, encoded), "Invalid voltage target or mode");
            } else {
                require(!options.count("--mode"), "--mode is only valid with --field target-mv");
                if (c.field == "offset-mv") require(!encodeIntelOcOffset(c.value, 0, encoded), "Voltage offset is out of range");
                else require(c.value >= 1 && c.value <= 85 && std::floor(c.value) == c.value, "Maximum ratio must be an integer 1..85");
            }
        }
    }
    if (c.name == "intel-oc-read" || c.name == "intel-oc-set" || vf) {
        const auto domain = get(options, "--domain");
        require(domain == "core" || domain == "cache", "--domain must be core or cache");
        c.domain = domain == "core" ? IntelOcDomain::Core : IntelOcDomain::Cache;
    }
    if (vf && (writing || options.count("--point"))) {
        c.point = uintOption(options, "--point", 15);
        require(c.point > 0, "--point must be 1..15; omit it to query all candidates");
    }
    c.forEdit = options.count("--for-edit") != 0;
    if (c.forEdit) require(c.point > 0, "--for-edit requires one explicit --point (1..15)");
    if (c.name == "intel-vf-set") {
        c.value = decimal(get(options, "--value")); std::uint32_t encoded = 0;
        require(!encodeIntelOcOffset(c.value, 0, encoded), "V/F offset is out of range");
    }
    c.smu.cpu = c.umc.cpu = c.cpu;
    if (smu || c.name == "amd-umc-read") {
        c.smu.bus = c.umc.bus = optional(options, "--bus", 255);
        c.smu.device = c.umc.device = optional(options, "--device", 31);
        c.smu.function = c.umc.function = optional(options, "--function", 7);
    }
    if (smu) {
        const auto profile = get(options, "--profile");
        require(profile == "shimada" || profile == "phoenix" || profile == "gpt", "--profile must be shimada, phoenix or gpt");
        c.smu.profile = profile == "shimada" ? SmuProfile::Shimada : profile == "phoenix" ? SmuProfile::Phoenix : SmuProfile::Gpt;
    }
    if (c.name == "amd-smu-read" || c.name == "amd-smu-send") {
        c.message.message = uintOption(options, "--message", UINT32_MAX);
        require(allowedSmuMessage(c.smu.profile, c.message.message), "Message is not in the recovered profile allowlist");
        if (c.name == "amd-smu-read") require(c.message.message == 1 || c.message.message == 2, "Query message must be 1 or 2");
        else {
            c.message.args[0] = uintOption(options, "--arg0", UINT32_MAX);
            for (unsigned i = 1; i < 6; ++i) c.message.args[i] = optional(options, "--arg" + std::to_string(i), UINT32_MAX);
        }
    }
    if (c.name == "amd-curve-read") {
        c.ccd = uintOption(options, "--ccd", 15); c.core = uintOption(options, "--core", 7);
        c.smu.profile = SmuProfile::Shimada;
    }
    if (c.name == "amd-umc-read") { c.umc.bank = uintOption(options, "--bank", 22); c.umc.refreshSlot = uintOption(options, "--refresh-slot", 15); }
    if (c.name == "spd-decode") { c.path = get(options, "--file"); require(!c.path.empty(), "Empty SPD path"); }
    if (umcOffline) {
        c.path = get(options, "--file"); require(!c.path.empty(), "Empty snapshot path");
        if (c.name == "amd-umc-diff") { c.otherPath = get(options, "--compare"); require(!c.otherPath.empty(), "Empty comparison path"); }
    }
    return c;
}
std::string identity(const CpuIdentity &id) {
    if (id.error) return "null";
    return object({{"vendor", quote(id.amd ? "AuthenticAMD" : id.intel ? "GenuineIntel" : "unknown")},
        {"family", number(id.family)}, {"model", number(id.model)}, {"stepping", number(id.stepping)}, {"signature", hex(id.signature)}});
}
std::string pci(unsigned bus, unsigned device, unsigned function) {
    return object({{"domain", "0"}, {"bus", number(bus)}, {"device", number(device)}, {"function", number(function)}});
}
std::string ocSnapshot(const IntelOcSnapshot &s) {
    return object({{"valid", boolean(s.valid && !s.error)}, {"error", number(s.error)}, {"identity", identity(s.identity)},
        {"locked", boolean(s.locked)}, {"firmware_status", number(s.response.firmwareStatus)},
        {"raw", s.valid && !s.error ? hex(s.response.data) : "null"},
        {"offset_mv", s.valid && !s.error ? number(intelOcOffsetMillivolts(s.response.data)) : "null"},
        {"target_mv", s.valid && !s.error ? number(intelOcTargetMillivolts(s.response.data)) : "null"},
        {"target_mode", s.valid && !s.error ? quote(intelOcVoltageMode(s.response.data) == IntelOcVoltageMode::Adaptive ? "adaptive" : "override") : "null"},
        {"max_ratio", s.valid && !s.error ? number(s.response.data & 255) : "null"}});
}
std::string uncoreSnapshot(const IntelUncoreSnapshot &s) {
    const bool valid = s.valid && !s.error;
    return object({{"error", number(s.error)}, {"valid", boolean(valid)}, {"identity", identity(s.identity)},
        {"editable", boolean(intelUncoreEditable(s))}, {"raw", valid ? hex(s.raw, 16) : "null"},
        {"minimum_ratio", valid ? number(intelUncoreMinimum(s.raw)) : "null"},
        {"maximum_ratio", valid ? number(intelUncoreMaximum(s.raw)) : "null"}});
}
std::string turboSnapshot(const IntelTurboSnapshot &s) {
    const bool valid = s.valid && !s.error;
    std::vector<std::string> groups;
    if (valid) for (unsigned i = 0; i < 8; ++i) {
        const unsigned count = intelTurboByte(s.coreCounts, i);
        groups.push_back(object({{"group", number(i)}, {"active_core_threshold", number(count)},
            {"ratio", number(intelTurboByte(s.ratios, i))}, {"active", boolean(count != 0)},
            {"editable", boolean(count && s.layoutValid && s.programmable && !s.locked)}}));
    }
    return object({{"error", number(s.error)}, {"valid", boolean(valid)}, {"identity", identity(s.identity)},
        {"hybrid", valid ? boolean(s.hybrid) : "null"}, {"programmable", valid ? boolean(s.programmable) : "null"},
        {"locked", valid ? boolean(s.locked) : "null"}, {"layout_valid", valid ? boolean(s.layoutValid) : "null"},
        {"ratios_raw", valid ? hex(s.ratios, 16) : "null"}, {"core_counts_raw", valid ? hex(s.coreCounts, 16) : "null"},
        {"groups", array(groups)}});
}
std::string vfEditSnapshot(const IntelVfEditSnapshot &s) {
    const bool valid = s.valid && !s.error;
    const auto response = [](const IntelOcResponse &r) {
        return object({{"error", number(r.error)}, {"query_attempted", boolean(r.commandAttempted)},
            {"completed", boolean(r.completed)}, {"firmware_status", r.completed ? number(r.firmwareStatus) : "null"}});
    };
    return object({{"valid", boolean(valid)}, {"error", number(s.error)}, {"identity", identity(s.identity)},
        {"editable", boolean(intelVfEditable(s))}, {"locked", valid ? boolean((s.flexRatio & (UINT64_C(1) << 20)) != 0) : "null"},
        {"per_core_override", valid ? boolean((s.control.data & 8) != 0) : "null"},
        {"domain_configuration_default", valid ? boolean(intelVfDomainDefault(s)) : "null"},
        {"flex_ratio_raw", valid ? hex(s.flexRatio, 16) : "null"}, {"control_raw", valid ? hex(s.control.data) : "null"},
        {"domain_raw", valid ? hex(s.legacy.data) : "null"}, {"point_raw", valid ? hex(s.value.data) : "null"},
        {"ratio", valid ? number(s.value.data & 255) : "null"},
        {"offset_mv", valid ? number(intelOcOffsetMillivolts(s.value.data)) : "null"},
        {"control_query", response(s.control)}, {"domain_query", response(s.legacy)}, {"point_query", response(s.value)}});
}
class Pstates final : public PstateReader {
public:
    explicit Pstates(HardwareSession &s) : s_(s) {}
    int cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf, std::uint32_t words[4]) override {
        const auto r = s_.cpuid(cpu, leaf, subleaf);
        if (!r.error) for (unsigned i = 0; i < 4; ++i) words[i] = r.words[i];
        return r.error;
    }
    int readMsr(unsigned cpu, std::uint32_t index, std::uint64_t &value) override {
        const auto r = s_.execute(msrRequest(cpu, index)); if (!r.error) value = r.value; return r.error;
    }
private: HardwareSession &s_;
};
Result hardware(const Command &c, HardwareSession &s) {
    Result out;
    const bool rawRegister = c.name == "register-read" || c.name == "register-write";
    if (!rawRegister || c.reg.space == Space::Msr) out.data.push_back({"cpu", number(c.cpu)});
    if (rawRegister) {
        const auto &r = c.reg;
        out.data.push_back({"space", quote(r.space == Space::Msr ? "msr" : r.space == Space::Pci ? "pci" : "mmio")});
        out.data.push_back({"address", hex(r.address, r.space == Space::Memory ? 16 : 8)});
        out.data.push_back({"width", number(r.width)});
        if (r.space == Space::Pci) out.data.push_back({"pci", pci(r.bus, r.device, r.function)});
        const int ready = s.checkpoint();
        Reply reply;
        if (ready) reply.error = ready;
        else reply = s.execute(r);
        out.error = reply.error;
        out.data.push_back({"value", !r.write && !out.error ? hex(reply.value, unsigned(r.width) * 2) : "null"});
        out.data.push_back({"write_attempted", boolean(r.write && !ready)});
        out.data.push_back({"completed_writes", number(r.write && !out.error ? 1 : 0)});
        out.data.push_back({"submitted", r.write && !out.error ? hex(r.value, unsigned(r.width) * 2) : "null"});
        // Arbitrary registers can be write-only, volatile, W1C or commands.
        // An automatic read, comparison or replay would change their semantics.
        out.data.push_back({"verified", "false"});
    } else if (c.name == "cpu") {
        const auto id = identifyCpu(s, c.cpu); out.error = id.error; out.data.push_back({"identity", identity(id)});
    } else if (c.name == "amd-pstates") {
        Pstates reader(s); const auto p = readAmdPstates(reader, c.cpu);
        static const char *const statuses[] = {"cpu_read_failed", "not_amd", "unsupported_family", "capability_unavailable", "capability_not_advertised", "limit_read_failed", "complete"};
        static const char *const rows[] = {"not_read", "above_limit", "read_failed", "read"};
        out.error = p.error ? p.error : p.status != PstateStatus::Complete ? -ENOTSUP : 0;
        out.data.push_back({"status", quote(statuses[unsigned(p.status)])});
        out.data.push_back({"maximum", p.status == PstateStatus::Complete ? number(p.maximum) : "null"});
        std::vector<std::string> values;
        for (unsigned i = 0; i < p.rows.size(); ++i) {
            const auto &r = p.rows[i]; const auto d = decodeFamily1aPstate(r.raw);
            const bool valid = r.status == PstateRowStatus::Read;
            if (r.error && !out.error) out.error = r.error;
            values.push_back(object({{"pstate", number(i)}, {"status", quote(rows[unsigned(r.status)])}, {"error", number(r.error)},
                {"raw", valid ? hex(r.raw, 16) : "null"}, {"enabled", valid ? boolean(d.enabled) : "null"},
                {"frequency_mhz", valid && d.validFrequency ? number(d.frequencyMHz) : "null"},
                {"vid_bits", valid ? number(d.vidBits) : "null"}, {"idd_value_bits", valid ? number(d.iddValueBits) : "null"},
                {"idd_div_bits", valid ? number(d.iddDivBits) : "null"}}));
        }
        out.data.push_back({"rows", array(values)});
    } else if (c.name == "intel-read" || c.name == "intel-set") {
        const auto snapshot = readIntelControls(s, c.cpu); out.error = snapshot.error;
        out.data.push_back({"identity", identity(snapshot.identity)});
        if (c.name == "intel-set") {
            IntelControlUpdate result;
            if (!out.error) { result = applyIntelControl(s, snapshot, intelFields.at(c.field), c.value); out.error = result.error; }
            out.data.push_back({"field", quote(c.field)}); out.data.push_back({"requested", number(c.value)});
            out.data.push_back({"write_attempted", boolean(result.writeAttempted)});
            out.data.push_back({"completed_writes", number(result.completed)});
            out.data.push_back({"verified", boolean(result.verified && !out.error)});
            out.data.push_back({"unchanged", boolean(result.unchanged)});
            out.data.push_back({"msr", result.msr ? hex(result.msr) : "null"});
            out.data.push_back({"readback_value", result.verified && !out.error ? number(result.readbackValue) : "null"});
            out.data.push_back({"expected_raw", result.msr ? hex(result.expected, 16) : "null"});
            const auto &observed = result.msr == 0x610 ? result.after.powerLimit : result.after.hwpRequest;
            out.data.push_back({"readback_raw", result.readbackValid ? hex(observed.value, 16) : "null"});
            std::vector<std::string> submitted;
            for (auto v : result.submitted) submitted.push_back(hex(v, 16));
            out.data.push_back({"submitted", array(submitted)});
        } else {
            std::vector<std::string> values;
            for (const auto &r : snapshot.readings) {
                if (r.error && !out.error) out.error = r.error;
                values.push_back(object({{"name", quote(r.name)}, {"unit", quote(r.unit)}, {"msr", hex(r.msr)},
                    {"error", number(r.error)}, {"raw", r.error ? "null" : hex(r.raw, 16)},
                    {"value", r.decoded && !r.error ? number(r.value) : "null"}, {"editable", boolean(r.editable)}}));
            }
            if (values.empty() && !out.error) out.error = -ENOTSUP;
            out.data.push_back({"readings", array(values)});
        }
    } else if (c.name == "intel-oc-read" || c.name == "intel-oc-set") {
        const auto snapshot = readIntelOc(s, c.cpu, c.domain); out.error = snapshot.error;
        out.data.push_back({"domain", quote(c.domain == IntelOcDomain::Core ? "core" : "cache")});
        out.data.push_back({"before", ocSnapshot(snapshot)});
        if (c.name == "intel-oc-set") {
            IntelOcUpdate result;
            if (!out.error) {
                result = c.field == "target-mv" ? applyIntelOcVoltage(s, snapshot, unsigned(c.value), c.mode)
                    : c.field == "offset-mv" ? applyIntelOcOffset(s, snapshot, c.value) : applyIntelOcRatio(s, snapshot, unsigned(c.value));
                out.error = result.error;
            }
            out.data.push_back({"field", quote(c.field)}); out.data.push_back({"requested", number(c.value)});
            if (c.field == "target-mv") out.data.push_back({"requested_mode", quote(c.mode == IntelOcVoltageMode::Adaptive ? "adaptive" : "override")});
            out.data.push_back({"write_attempted", boolean(result.writeAttempted)}); out.data.push_back({"verified", boolean(result.verified)});
            out.data.push_back({"stage", number(unsigned(result.stage))}); out.data.push_back({"after", ocSnapshot(result.readback)});
        }
    } else if (c.name == "intel-uncore-read" || c.name == "intel-uncore-set") {
        const auto snapshot = readIntelUncore(s, c.cpu); out.error = snapshot.error;
        out.data.push_back({"msr", hex(0x620)});
        out.data.push_back({"scope", quote("shared Ring / LLC domain of selected CPU")});
        out.data.push_back({"before", uncoreSnapshot(snapshot)});
        out.data.push_back({"hardware_effect_measured", "false"});
        if (c.name == "intel-uncore-set") {
            IntelUncoreUpdate result;
            if (!out.error) { result = applyIntelUncoreRange(s, snapshot, c.minimumRatio, c.maximumRatio); out.error = result.error; }
            out.data.push_back({"requested_minimum_ratio", number(c.minimumRatio)});
            out.data.push_back({"requested_maximum_ratio", number(c.maximumRatio)});
            out.data.push_back({"write_attempted", boolean(result.writeAttempted)});
            out.data.push_back({"completed_writes", number(result.completed)});
            out.data.push_back({"verified", boolean(result.verified && !out.error)});
            out.data.push_back({"unchanged", boolean(result.unchanged)});
            out.data.push_back({"expected_raw", result.expected ? hex(result.expected, 16) : "null"});
            out.data.push_back({"after", uncoreSnapshot(result.after)});
        }
    } else if (c.name == "intel-turbo-read" || c.name == "intel-turbo-set") {
        const auto snapshot = readIntelTurbo(s, c.cpu, c.turboKind); out.error = snapshot.error;
        out.data.push_back({"core_type", quote(c.turboKind == IntelTurboKind::Primary ? "p" : "e")});
        out.data.push_back({"ratio_msr", hex(intelTurboRatioMsr(c.turboKind))});
        out.data.push_back({"core_count_msr", hex(intelTurboCountMsr(c.turboKind))});
        out.data.push_back({"before", turboSnapshot(snapshot)});
        out.data.push_back({"hardware_effect_measured", "false"});
        if (c.name == "intel-turbo-set") {
            IntelTurboUpdate result;
            if (!out.error) { result = applyIntelTurboRatio(s, snapshot, c.group, unsigned(c.value)); out.error = result.error; }
            out.data.push_back({"group", number(c.group)}); out.data.push_back({"requested_ratio", number(c.value)});
            out.data.push_back({"write_attempted", boolean(result.writeAttempted)});
            out.data.push_back({"completed_writes", number(result.completed)});
            out.data.push_back({"verified", boolean(result.verified && !out.error)});
            out.data.push_back({"unchanged", boolean(result.unchanged)});
            out.data.push_back({"expected_raw", result.expected ? hex(result.expected, 16) : "null"});
            out.data.push_back({"after", turboSnapshot(result.after)});
        }
    } else if (c.name == "intel-vf-set" || (c.name == "intel-vf-read" && c.forEdit)) {
        const auto before = readIntelVfEdit(s, c.cpu, c.domain, c.point); out.error = before.error;
        out.data.push_back({"domain", quote(c.domain == IntelOcDomain::Core ? "core" : "cache")});
        out.data.push_back({"selected_point", number(c.point)});
        out.data.push_back({"edit_context", vfEditSnapshot(before)});
        out.data.push_back({"hardware_effect_measured", "false"});
        if (c.name == "intel-vf-set") {
            IntelVfUpdate result;
            if (!out.error) { result = applyIntelVfOffset(s, before, c.value); out.error = result.error; }
            out.data.push_back({"requested_offset_mv", number(c.value)});
            out.data.push_back({"write_attempted", boolean(result.writeAttempted)});
            out.data.push_back({"verified", boolean(result.verified && !out.error)});
            out.data.push_back({"unchanged", boolean(result.unchanged)});
            out.data.push_back({"stage", number(unsigned(result.stage))});
            out.data.push_back({"submitted_raw", result.writeAttempted ? hex(result.submitted) : "null"});
            out.data.push_back({"expected_point_raw", before.valid ? hex(result.expected) : "null"});
            out.data.push_back({"settings_completed", boolean(result.response.completed)});
            out.data.push_back({"settings_firmware_status", result.response.completed ? number(result.response.firmwareStatus) : "null"});
            out.data.push_back({"after", vfEditSnapshot(result.readback)});
        }
    } else if (c.name == "intel-vf-read") {
        const auto snapshot = readIntelVf(s, c.cpu, c.domain, c.point); out.error = snapshot.error;
        out.data.push_back({"domain", quote(c.domain == IntelOcDomain::Core ? "core" : "cache")});
        out.data.push_back({"identity", identity(snapshot.identity)});
        out.data.push_back({"selected_point", c.point ? number(c.point) : "null"});
        out.data.push_back({"requested_points", number(c.point ? 1 : 15)});
        out.data.push_back({"scan_completed", boolean(snapshot.scanCompleted)});
        std::vector<std::string> values;
        for (const auto &point : snapshot.points) {
            const auto &r = point.response; const bool valid = r.completed && !r.error;
            values.push_back(object({{"point", number(point.point)}, {"valid", boolean(valid)},
                {"error", number(r.error)}, {"query_attempted", boolean(r.commandAttempted)},
                {"completed", boolean(r.completed)}, {"firmware_status", r.completed ? number(r.firmwareStatus) : "null"},
                {"raw", valid ? hex(r.data) : "null"}, {"ratio", valid ? number(r.data & 255) : "null"},
                {"offset_mv", valid ? number(intelOcOffsetMillivolts(r.data)) : "null"}}));
        }
        out.data.push_back({"points", array(values)});
    } else if (c.name == "amd-smu-probe" || c.name == "amd-smu-read" || c.name == "amd-smu-send") {
        out.data.push_back({"pci", pci(c.smu.bus, c.smu.device, c.smu.function)});
        out.data.push_back({"profile", quote(c.smu.profile == SmuProfile::Shimada ? "shimada" : c.smu.profile == SmuProfile::Phoenix ? "phoenix" : "gpt")});
        if (c.name == "amd-smu-probe") {
            const auto p = probeSmu(s, c.smu); out.error = p.error;
            out.data.push_back({"identity", identity(p.identity)}); out.data.push_back({"pci_identity", p.error ? "null" : hex(p.pciIdentity)});
        } else {
            const auto r = sendSmu(s, c.smu, c.message); out.error = r.error;
            out.data.push_back({"message", hex(c.message.message)}); out.data.push_back({"message_attempted", boolean(r.messageAttempted)});
            out.data.push_back({"response", hex(r.response)}); out.data.push_back({"completed_commands", number(r.completedCommands)});
            out.data.push_back({"hardware_effect_measured", "false"});
            std::vector<std::string> values, requested;
            for (auto v : c.message.args) requested.push_back(hex(v));
            out.data.push_back({"requested_args", array(requested)});
            if (!r.error) for (auto v : r.args) values.push_back(hex(v));
            out.data.push_back({"args", r.error ? "null" : array(values)});
        }
    } else if (c.name == "amd-curve-read") {
        const auto r = readShimadaCurve(s, c.smu, c.ccd, c.core); out.error = r.error ? r.error : !r.valid ? -EIO : 0;
        out.data.push_back({"firmware_ccd", number(c.ccd)}); out.data.push_back({"firmware_core", number(c.core)});
        out.data.push_back({"physical_mapping_verified", "false"}); out.data.push_back({"message_attempted", boolean(r.messageAttempted)});
        out.data.push_back({"response", hex(r.response)}); out.data.push_back({"raw", out.error ? "null" : hex(r.raw)});
        out.data.push_back({"raw_signed", out.error ? "null" : number(r.raw <= 0x7fffffff ? double(r.raw) : double(r.raw) - 4294967296.0)});
    } else if (c.name == "amd-topology") {
        const auto t = readAmdTopology(s, c.cpu); out.error = t.error;
        out.data.push_back({"identity", identity(t.identity)}); out.data.push_back({"firmware_mapping_verified", "false"});
        if (!out.error) {
            out.data.push_back({"apic_id", hex(t.apicId)}); out.data.push_back({"socket_id", number(t.socketId)});
            out.data.push_back({"ccd_in_socket", number(t.ccdInSocket)}); out.data.push_back({"core_in_ccd", number(t.coreInCcd)});
            out.data.push_back({"thread_in_core", number(t.threadInCore)});
        }
    } else if (c.name == "amd-umc-read") {
        const auto r = readAmdUmc(s, c.umc); out.error = r.error;
        out.data.push_back({"pci", pci(c.umc.bus, c.umc.device, c.umc.function)});
        out.data.push_back({"bank", number(c.umc.bank)}); out.data.push_back({"refresh_slot", number(c.umc.refreshSlot)});
        std::vector<std::string> registers, fields;
        // A failed bank read must not present an incomplete bank as a snapshot.
        if (!out.error) {
            for (const auto &v : r.registers) registers.push_back(object({{"offset", hex(v.offset)}, {"raw", hex(v.value)}}));
            for (const auto &v : r.decoded.values) {
                const auto &f = amdUmcFields().at(v.id);
                fields.push_back(object({{"id", number(v.id)}, {"group", quote(amdUmcGroupName(f.group))}, {"name", quote(f.name)},
                    {"offset", hex(v.offset)}, {"error", number(v.error)}, {"encoded", v.error ? "null" : number(v.encoded)}}));
            }
        }
        out.data.push_back({"registers", out.error ? "null" : array(registers)}); out.data.push_back({"fields", out.error ? "null" : array(fields)});
    }
    return out;
}
std::string spd(const std::string &path, const std::vector<std::uint8_t> &bytes, int &error) {
    SpdSnapshot decoded;
    if (!error) { decoded = decodeSpd(bytes); error = decoded.error; }
    std::vector<std::string> fields;
    for (const auto &field : decoded.fields) fields.push_back(object({{"name", quote(field.name)}, {"value", quote(field.value)}}));
    return object({{"path", quote(path)}, {"error", number(error)}, {"captured_bytes", number(double(bytes.size()))},
        {"memory_type", number(decoded.memoryType)}, {"crc_checked", boolean(decoded.crcChecked)},
        {"crc_valid", decoded.crcChecked ? boolean(decoded.crcValid) : "null"}, {"fields", array(fields)}});
}
Result offlineUmc(const Command &c) {
    Result out;
    out.data = {{"offline", "true"}, {"hardware_accessed", "false"}, {"origin_verified", "false"}, {"path", quote(c.path)}};
    auto load = [](const std::string &path) {
        std::vector<std::uint8_t> bytes; UmcCapture capture;
        capture.error = platform::readBoundedFile(path, bytes, 65536);
        if (!capture.error) capture = parseUmcCapture(std::string(bytes.begin(), bytes.end()));
        return capture;
    };
    const auto before = load(c.path); out.error = before.error;
    if (out.error) { out.data.push_back({"detail", quote(before.detail)}); return out; }
    out.data.push_back({"bank", number(before.target.bank)}); out.data.push_back({"refresh_slot", number(before.target.refreshSlot)});
    out.data.push_back({"cpu", before.hasCpu ? number(before.target.cpu) : "null"});
    out.data.push_back({"pci", before.hasPci ? pci(before.target.bus, before.target.device, before.target.function) : "null"});
    out.data.push_back({"complete", boolean(before.registers.size() == 56)});
    std::vector<std::string> fields;
    if (c.name == "amd-umc-decode") {
        std::vector<std::string> registers;
        for (const auto &r : before.registers) registers.push_back(object({{"offset", hex(r.offset)}, {"raw", hex(r.value)}}));
        out.data.push_back({"registers", array(registers)});
        for (const auto &v : before.decoded.values) {
            const auto &f = amdUmcFields().at(v.id);
            fields.push_back(object({{"id", number(v.id)}, {"group", quote(amdUmcGroupName(f.group))}, {"name", quote(f.name)},
                {"offset", hex(v.offset)}, {"error", number(v.error)}, {"encoded", v.error ? "null" : number(v.encoded)}}));
        }
    } else {
        out.data.push_back({"compare_path", quote(c.otherPath)});
        const auto after = load(c.otherPath); out.error = after.error;
        if (out.error) { out.data.push_back({"detail", quote(after.detail)}); return out; }
        const auto comparison = compareUmcCaptures(before, after); out.error = comparison.error;
        if (out.error) { out.data.push_back({"detail", quote("Both snapshots must use the same bank and refresh slot")}); return out; }
        out.data.push_back({"compared_complete", boolean(after.registers.size() == 56)});
        out.data.push_back({"physical_channel_verified", "false"});
        Fields counts;
        for (unsigned i = 0; i < comparison.counts.size(); ++i) counts.push_back({umcChangeName(UmcChange(i)), number(comparison.counts[i])});
        out.data.push_back({"counts", object(counts)});
        for (const auto &v : comparison.fields) {
            const auto &f = amdUmcFields().at(v.before.id);
            fields.push_back(object({{"id", number(f.id)}, {"group", quote(amdUmcGroupName(f.group))}, {"name", quote(f.name)},
                {"offset", hex(v.before.offset)}, {"state", quote(umcChangeName(v.state))},
                {"before_raw", v.before.error ? "null" : hex(v.before.raw)}, {"after_raw", v.after.error ? "null" : hex(v.after.raw)},
                {"before_encoded", v.before.error ? "null" : number(v.before.encoded)}, {"after_encoded", v.after.error ? "null" : number(v.after.encoded)},
                {"encoded_delta", v.before.error || v.after.error ? "null" : number(double(v.after.encoded) - double(v.before.encoded))}}));
        }
    }
    out.data.push_back({"fields", array(fields)}); return out;
}
Result execute(const Command &c, const BackendFactory &factory) {
    Result out;
    if (c.name == "diagnose") {
        const auto info = platform::systemInfo(); struct utsname uts{};
        out.error = info.affinityError;
        if (uname(&uts) && !out.error) out.error = -errno;
        std::vector<std::string> cpus;
        for (auto cpu : info.allowedCpus) cpus.push_back(number(cpu));
        out.data = {{"version", quote(OCTOOL_VERSION)}, {"kernel", quote(uts.release)}, {"architecture", quote(uts.machine)},
            {"cpu_model", quote(info.cpuModel)}, {"online_cpus", number(double(info.onlineCpus))}, {"allowed_cpus", array(cpus)},
            {"affinity_error", number(info.affinityError)}, {"module_loaded", boolean(info.moduleLoaded)},
            {"effective_uid", number(geteuid())}, {"register_access_tested", "false"}, {"display_required", "false"}};
    } else if (c.name == "inventory") {
        const auto native = platform::linuxInventoryNative(); std::vector<std::string> rows, devices;
        for (const auto &r : native.rows) {
            if (r.error && !out.error) out.error = r.error;
            rows.push_back(object({{"group", quote(r.group)}, {"name", quote(r.name)}, {"value", r.error ? "null" : quote(r.value)},
                {"unit", quote(r.unit)}, {"source", quote(r.source)}, {"status", quote(r.status)}, {"error", number(r.error)}}));
        }
        for (const auto &r : native.spd) { int error = r.error; devices.push_back(spd(r.path, r.bytes, error)); if (error && !out.error) out.error = error; }
        out.data = {{"rows", array(rows)}, {"spd", array(devices)}, {"partial", boolean(out.error != 0)}};
    } else if (c.name == "spd-decode") {
        std::vector<std::uint8_t> bytes; out.error = platform::readBoundedFile(c.path, bytes);
        out.data.push_back({"spd", spd(c.path, bytes, out.error)});
    } else if (c.name == "amd-umc-decode" || c.name == "amd-umc-diff") {
        out = offlineUmc(c);
    } else {
        HardwareService service(factory());
        const int error = service.transaction([&](HardwareSession &s) { out = hardware(c, s); return out.error; }, int(c.timeout));
        if (error) out.error = error;
    }
    return out;
}
const char *help = R"HELP(octool-cli: Linux x86_64, no Qt or display server required
Usage: octool-cli COMMAND [OPTIONS]

  diagnose                         OS/CPU affinity/driver presence, no device open
  inventory                        BIOS, hwmon and bound-driver DDR4/DDR5 SPD
  spd-decode --file PATH            Decode a bounded offline SPD file
  cpu --cpu N                      CPU identity
  register-read --space msr --cpu N --address A
  register-read --space mmio --address A --width 1|2|4|8
  register-read --space pci --address A --width 1|2|4 --bus B --device D --function F
  register-write [same target options] --value V --apply
  amd-pstates --cpu N               Family 1Ah P0..P7 (read only)
  intel-read --cpu N                RAPL, HWP and supported temperature readings
  intel-set --cpu N --field F --value V --apply
  intel-oc-read --cpu N --domain core|cache
  intel-vf-read --cpu N --domain core|cache [--point 1..15 [--for-edit]]
  intel-vf-set --cpu N --domain core|cache --point 1..15 --value MV --apply
  intel-turbo-read --cpu N --core-type p|e
  intel-uncore-read --cpu N
  intel-uncore-set --cpu N --minimum-ratio MIN --maximum-ratio MAX --apply
  intel-turbo-set --cpu N --core-type p|e --group 0..7 --value RATIO --apply
  intel-oc-set --cpu N --domain core|cache --field offset-mv|max-ratio --value V --apply
  intel-oc-set --cpu N --domain core|cache --field target-mv --value INTEGER_MV --mode adaptive|override --apply
  amd-smu-probe --cpu N --profile shimada|phoenix|gpt [PCI]
  amd-smu-read --cpu N --profile P --message 1|2 [PCI]
  amd-smu-send --cpu N --profile P --message M --arg0 A [--arg1 A ... --arg5 A] [PCI] --apply
  amd-curve-read --cpu N --ccd N --core N
  amd-topology --cpu N
  amd-umc-read --cpu N --bank N --refresh-slot N [PCI]
  amd-umc-decode --file SNAPSHOT     Decode GUI/CLI JSON offline, no privileges
  amd-umc-diff --file BEFORE --compare AFTER   Compare the same bank/refresh slot

PCI: --bus N --device N --function N (domain 0; defaults 0:0.0).
Intel fields: pl1/pl2 (W), pl1-window/pl2-window (s), pl1-enable/pl2-enable,
  pl1-clamp/pl2-clamp (0 or 1), hwp-min/hwp-max/hwp-desired/hwp-epp (0..255).
  hwp-window-us (0 = automatic; integer 1..1270000000 us, rounds down) requires
  CPUID activity-window support and HWP desired = 0. RAPL/HWP changes check full
  register readback; these are configured hints, not measured performance.
CPU commands require explicit --cpu; raw MMIO/PCI commands select their own target.
All hardware commands accept --timeout-ms 1..120000. PCI raw access is domain 0,
first 256 bytes only; all three BDF components and the access width are required.
Unsigned indices and raw register values accept decimal or 0x-prefixed hex.
Tuning --value uses decimal. Raw writes submit exactly once, without a readback;
they have no model/lock/bit-preservation checks and are intended for known registers.
Voltage target requires integer 1..2000 mV and an explicit mode; this is not a safe operating range.
Turbo groups use integer ratios 1..85, preserving active-core thresholds and other
groups. Active-group ratios must not increase as core counts increase. This B7
profile checks programmable capability, OC lock, stale values and full readback.
Results are JSON (optional --json); raw registers are exact hexadecimal strings.
Exit: 0 success, 2 invalid command/options, 3 failed operation or partial read.
Run diagnose first; choose a CPU from allowed_cpus. Device access may need sudo.
Firmware queries can write mailbox/index registers; settings require --apply.
CCD/core are firmware indices, not Linux CPU IDs; UMC values are raw encodings.
UMC offline commands accept regular JSON files up to 64 KiB, including partial
GUI snapshots. Missing fields stay null. Diff separates field and raw-only changes;
equal indices do not certify the same physical channel. No device is opened.
The shared core retains model/identity/lock checks. There are no automatic retries,
rollback, module loading, polkit dialogs or background service. Do not run another
tuning application concurrently; transaction locks are local to this process.
)HELP";
}
int run(const std::vector<std::string> &args, std::ostream &out, const BackendFactory &factory) {
    if (args.empty() || (args.size() == 1 && (args[0] == "--help" || args[0] == "help"))) { out << help; return 0; }
    if (args.size() == 1 && args[0] == "--version") { out << OCTOOL_VERSION << '\n'; return 0; }
    Result result; std::string detail; int code = 0;
    try {
        const auto command = parse(args); result = execute(command, factory);
        if (result.error) { code = 3; detail = std::strerror(-result.error); }
    } catch (const std::invalid_argument &error) {
        result.error = -EINVAL; detail = error.what(); code = 2;
    } catch (const std::exception &error) {
        result.error = -EIO; detail = error.what(); code = 3;
    }
    out << object({{"schema_version", "1"}, {"command", quote(args[0])}, {"ok", boolean(code == 0)},
        {"error", number(result.error)}, {"error_message", quote(detail)}, {"data", object(result.data)}}) << '\n';
    if (!out) return 3;
    return code;
}
} }
