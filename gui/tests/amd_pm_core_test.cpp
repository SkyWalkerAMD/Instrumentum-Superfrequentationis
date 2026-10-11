// SPDX-License-Identifier: GPL-2.0-only
#include "amd_pm.h"
#include "amd_pm_fixture.h"
#include <cassert>
#include <cerrno>
#include <cmath>
#include <iostream>
#include <limits>
#include <locale>
#include <set>
using namespace octool::core;
static void replace(std::string &s, const std::string &before, const std::string &after) {
    const auto n = s.find(before); assert(n != std::string::npos); s.replace(n, before.size(), after);
}
int main() {
    unsigned groups = 0;
    const std::uint32_t versions[] = {0x730204,0x730404,0x730604,0x730804,0x730a04,0x730c04,0x730e04,0x731004};
    const unsigned lengths[] = {2844,4100,5356,6612,7868,9124,10380,11636};
    assert(amdPmLayouts().size() == 8);
    for (unsigned i = 0; i < 8; ++i) {
        assert(amdPmLayouts()[i].version == versions[i] && amdPmTableSize(versions[i]) == lengths[i]);
        const auto s = decodeAmdPm(pm_fixture::bytes(lengths[i]), versions[i]);
        assert(!s.error && s.fields.size() == 24 && s.expectedBytes == lengths[i] && s.raw.size() == lengths[i]);
        assert(s.fields[0].value == 2000 && s.fields[1].value == 1800 && s.fields[2].value == 3600);
        assert(s.fields[3].value == 1.0625 && s.fields[4].value == 0.9375 && s.fields[5].value == 0.8125 && s.fields[6].value == 0.78125);
        assert(s.fields[7].value == 0.875 && s.fields[8].value == 1.25 && s.fields[10].value == 100 && s.fields[11].value == 70);
        assert(s.fields[13].value == 1.28125 && s.fields[14].value == 120 && s.fields[15].value == 72);
        assert(s.fields[17].value == s.fields[3].value && s.fields[18].value == 20 && s.fields[19].value == 60);
        assert(s.fields[21].value == s.fields[7].value && s.fields[22].value == 4 && s.fields[23].value == 50);
        for (const auto &v : s.fields) assert(!v.error && !v.scaled && !v.scaleError);
    } ++groups;
    for (unsigned i = 0; i < 8; ++i) for (unsigned length = 0; length <= lengths[i] + 1; ++length) {
        if (length == lengths[i]) continue;
        const auto s = decodeAmdPm(std::vector<std::uint8_t>(length), versions[i]);
        assert(s.error == -EMSGSIZE && s.fields.empty() && s.capturedBytes == length);
    } ++groups;
    for (std::uint32_t v : {0u,0x730u,0x730205u,0x731204u,0xffffffffu}) {
        const auto s = decodeAmdPm(pm_fixture::bytes(), v);
        assert(s.error == -ENOTSUP && s.fields.empty() && s.expectedBytes == 0);
    } ++groups;
    const auto base = pm_fixture::bytes(); const auto good = decodeAmdPm(base, 0x730204);
    std::set<unsigned> offsets; for (const auto &f : good.fields) offsets.insert(f.offset);
    assert(offsets.size() == 22);
    for (unsigned offset : offsets) for (std::uint32_t bits : {0x7f800000u,0xff800000u,0x7fc00000u,0x7f800001u,0xffbfffffu}) {
        auto b = base; pm_fixture::put(b, offset, bits); const auto s = decodeAmdPm(b, 0x730204);
        assert(s.error == -EDOM && s.fields.size() == 24);
        for (const auto &f : s.fields) assert(f.offset == offset ? f.error == -EDOM && f.raw == bits : f.error == 0);
        assert(amdPmJson(s).find("nan") == std::string::npos);
    } ++groups;
    for (unsigned offset : offsets) for (std::uint32_t bits : {0u,0x80000000u,1u,0x807fffffu,0x00800000u,0x7f7fffffu,0xff7fffffu,0xbf800000u}) {
        auto b = base; pm_fixture::put(b, offset, bits); const auto s = decodeAmdPm(b, 0x730204);
        assert(!s.error);
        for (const auto &f : s.fields) if (f.offset == offset) {
            assert(f.raw == bits && std::isfinite(f.value));
            if (bits == 0x80000000u) assert(std::signbit(f.value));
        }
    } ++groups;
    AmdPmOptions options; options.hasBclk = true; options.bclkMHz = 102.5;
    const auto scaled = decodeAmdPm(base, 0x730204, options);
    assert(!scaled.error && scaled.fields[0].value == 2000 && scaled.fields[0].scaledValue == 2050);
    assert(scaled.fields[1].scaledValue == 1845 && scaled.fields[2].scaledValue == 3690);
    for (std::size_t i = 3; i < scaled.fields.size(); ++i) assert(!scaled.fields[i].scaled && !scaled.fields[i].clock);
    ++groups;
    for (double bclk : {0.0,-100.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        options.bclkMHz = bclk; const auto s = decodeAmdPm(base, 0x730204, options);
        assert(s.error == -EINVAL && s.fields.empty());
        pm_fixture::Driver d; assert(readAmdPmDriver(d.reader(), options).snapshot.error == -EINVAL && d.calls.empty());
    } ++groups;
    options.bclkMHz = std::numeric_limits<double>::max();
    const auto overflow = decodeAmdPm(base, 0x730204, options);
    assert(overflow.error == -ERANGE && overflow.fields[0].scaleError == -ERANGE && !overflow.fields[0].scaled);
    assert(!overflow.fields[0].error && overflow.fields[0].value == 2000 && !overflow.fields[3].error);
    ++groups;
    const auto encoded = amdPmJson(scaled);
    const auto parsed = parseAmdPmCapture(encoded);
    assert(!parsed.error && parsed.raw == base && amdPmJson(parsed) == encoded && parsed.options.bclkMHz == 102.5);
    assert(!parseAmdPmCapture("\xef\xbb\xbf" + encoded).error);
    ++groups;
    for (const char *command : {"amd-pm-read", "amd-pm-decode"}) {
        const std::string report = std::string("{\"schema_version\":1,\"ok\":false,\"command\":\"") + command + "\",\"data\":{\"pm\":" + encoded + "}}";
        assert(parseAmdPmCapture(report).raw == base);
    } ++groups;
    auto edited = encoded; replace(edited, "\"stored_value\":2000", "\"stored_value\":99999");
    replace(edited, "\"scaled_clock_mhz\":2050", "\"scaled_clock_mhz\":1");
    assert(amdPmJson(parseAmdPmCapture(edited)) == encoded); ++groups;
    for (const auto &replacement : std::vector<std::pair<std::string,std::string>>{
        {"\"schema\":1", "\"schema\":1.00000001"}, {"\"schema\":1", "\"schema\":2"},
        {"\"schema\":1", "\"schema\":1,\"schema\":1"}, {"\"captured_bytes\":2844", "\"captured_bytes\":2845"},
        {"\"bclk_mhz\":102.5", "\"bclk_mhz\":-1"}, {"\"bclk_mhz\":102.5", "\"bclk_mhz\":1e999"},
        {"\"raw_hex\":\"", "\"raw_hex\":\"Z"}, {"\"version\":\"0x00730204\"", "\"version\":7537156"}}) {
        auto s = encoded; replace(s, replacement.first, replacement.second);
        assert(parseAmdPmCapture(s).error == -EINVAL && parseAmdPmCapture(s).fields.empty());
    } ++groups;
    for (std::string bad : std::vector<std::string>{"", "{}", "null", "[]", encoded + "x", std::string(65537, ' '), "{\"x\":\"\xed\xa0\x80\"}"})
        assert(parseAmdPmCapture(bad).error == -EINVAL);
    assert(parseAmdPmCapture(std::string(18, '[') + "0" + std::string(18, ']')).error == -EINVAL);
    for (std::size_t i = 0; i < encoded.size(); ++i) assert(parseAmdPmCapture(encoded.substr(0, i)).error == -EINVAL);
    ++groups;
    struct Comma : std::numpunct<char> { char do_decimal_point() const override { return ','; } };
    const auto previous = std::locale(); std::locale::global(std::locale(previous, new Comma));
    assert(amdPmJson(scaled) == encoded && amdPmJson(parseAmdPmCapture(encoded)) == encoded);
    std::locale::global(previous); ++groups;
    for (unsigned width : {4u,8u}) {
        pm_fixture::Driver d; d.files["pm_table_size"] = pm_fixture::le(2844, width);
        const auto s = readAmdPmDriver(d.reader()); assert(!s.snapshot.error && s.tableReadAttempted && s.snapshot.raw == base);
        assert((d.calls == std::vector<std::string>{"pm_table_version","pm_table_size","pm_table","pm_table_version","pm_table_size"}));
    } ++groups;
    for (unsigned call = 1; call <= 5; ++call) {
        pm_fixture::Driver d; d.failCall = call; const auto s = readAmdPmDriver(d.reader());
        assert(s.snapshot.error == -EACCES && s.snapshot.fields.empty() && s.snapshot.raw.empty() && d.calls.size() == call);
        assert(s.tableReadAttempted == (call >= 3));
    } ++groups;
    for (unsigned size = 0; size <= 9; ++size) {
        pm_fixture::Driver d; d.files["pm_table_version"].resize(size);
        if (size != 4) assert(readAmdPmDriver(d.reader()).snapshot.error && d.calls.size() == 1);
        pm_fixture::Driver e; e.files["pm_table_size"].resize(size);
        if (size != 4 && size != 8) assert(readAmdPmDriver(e.reader()).snapshot.error && e.calls.size() == 2);
    } ++groups;
    for (unsigned variant = 0; variant < 3; ++variant) {
        pm_fixture::Driver d;
        if (variant == 0) d.files["pm_table_version"] = pm_fixture::le(0x730205, 4);
        if (variant == 1) d.files["pm_table_size"] = pm_fixture::le(2843, 8);
        if (variant == 2) d.files["pm_table_size"] = pm_fixture::le(0x100000b1cULL, 8);
        const auto s = readAmdPmDriver(d.reader()); assert(s.snapshot.error && !s.tableReadAttempted && d.calls.size() == 2);
    } ++groups;
    pm_fixture::Driver changed; changed.mutateAfter = 4;
    const auto change = readAmdPmDriver(changed.reader()); assert(change.snapshot.error == -EAGAIN && change.snapshot.raw.empty() && change.snapshot.fields.empty());
    pm_fixture::Driver shortTable; shortTable.files["pm_table"].pop_back();
    assert(readAmdPmDriver(shortTable.reader()).snapshot.error == -EMSGSIZE);
    pm_fixture::Driver longTable; longTable.files["pm_table"].push_back(0);
    assert(readAmdPmDriver(longTable.reader()).snapshot.error == -EFBIG && longTable.calls.size() == 3);
    assert(readAmdPmDriver({}).snapshot.error == -EINVAL); ++groups;
    std::cout << groups << " AMD PM scenario groups passed\n";
}
