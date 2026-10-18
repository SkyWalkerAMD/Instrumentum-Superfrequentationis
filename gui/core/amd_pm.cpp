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
void field(AmdPmSnapshot &out, const char *name, const char *unit, unsigned offset, bool clock = false, bool original = false) {
    AmdPmField v; v.name = name; v.unit = unit; v.offset = offset; v.clock = clock;
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
    (original ? out.originalLimits : out.fields).push_back(v);
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
    return 0;
}
AmdPmSnapshot decodeAmdPm(const std::vector<std::uint8_t> &bytes, std::uint32_t version, const AmdPmOptions &options) {
    AmdPmSnapshot out; out.version = version; out.options = options;
    out.capturedBytes = bytes.size(); out.expectedBytes = amdPmTableSize(version);
    if (bytes.size() <= maximumBytes) out.raw = bytes;
    if (!validOptions(options)) { out.error = -EINVAL; out.detail = "Supplied BCLK must be positive and finite"; return out; }
    if (!out.expectedBytes) { out.error = -ENOTSUP; out.detail = "Unsupported exact PM table version"; return out; }
    if (bytes.size() != out.expectedBytes) { out.error = -EMSGSIZE; out.detail = "PM table length does not match its version"; return out; }
    // Independently implemented from the pinned layout facts in
    // docs/amd-pm-recovery.md; never infer per-core target IDs from this table.
    field(out, "FCLK", "MHz", 0x20c, true);
    field(out, "UCLK", "MHz", 0x21c, true);
    field(out, "MCLK", "MHz", 0x22c, true);
    field(out, "VDDCR_SOC", "V", 0x1a4);
    field(out, "CLDO_VDDP", "V", 0x5cc);
    field(out, "CLDO_VDDG_IOD", "V", 0x714);
    field(out, "CLDO_VDDG_CCD", "V", 0x71c);
    field(out, "VDD_MISC", "V", 0x1b8);
    const char *rails[] = {"VDD", "VDD1", "SoC", "Misc"};
    const char *names[] = {"requested voltage (VID)", "measured voltage", "current", "VRM temperature"};
    const char *units[] = {"V", "V", "A", "C"};
    const unsigned offsets[] = {0, 4, 8, 16};
    for (unsigned r = 0; r < 4; ++r) for (unsigned f = 0; f < 4; ++f)
        field(out, (std::string(rails[r]) + " " + names[f]).c_str(), units[f], 0x178 + 20 * r + offsets[f]);
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
        field(out, limits[i], "unverified", i < 16 ? i * 4 : 0x1c8 + (i-16)*4, false, true);
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
            (v.scaled ? number(v.scaledValue) : "null") + '}';
      }
    };
    appendFields(s.fields);
    out += "],\"original_limits_units_verified\":false,\"original_limits\":[";
    appendFields(s.originalLimits);
    return out + "]}";
}
AmdPmSnapshot parseAmdPmCapture(const std::string &input) {
    using capture_json::Json; using capture_json::check;
    AmdPmSnapshot out;
    try {
        const auto root = capture_json::Parser(input).parse();
        const Json *record = &root;
        if (root.has("schema_version")) {
            check(capture_json::uint(root.at("schema_version"), 1) == 1, "Unsupported CLI report version");
            const auto &command = root.at("command");
            check(command.kind == Json::String && (command.text == "amd-pm-read" || command.text == "amd-pm-decode"), "Not an AMD PM report");
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
