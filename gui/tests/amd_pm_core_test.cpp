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
        assert(!s.error && s.fields.size() == 22 && s.expectedBytes == lengths[i] && s.raw.size() == lengths[i]);
        assert(s.fields[0].value == 2000 && s.fields[1].value == 1800 && s.fields[2].value == 3600);
        assert(s.fields[3].value == 1.0625 && s.fields[4].value == 0.9375);
        assert(s.fields[5].value == 0.875 && s.fields[6].value == 1.25 && s.fields[8].value == 100 && s.fields[9].value == 70);
        assert(s.fields[11].value == 1.28125 && s.fields[12].value == 120 && s.fields[13].value == 72);
        assert(s.fields[15].value == s.fields[3].value && s.fields[16].value == 20 && s.fields[17].value == 60);
        assert(s.fields[19].value == s.fields[5].value && s.fields[20].value == 4 && s.fields[21].value == 50);
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
    assert(offsets.size() == 20);
    for (unsigned offset : offsets) for (std::uint32_t bits : {0x7f800000u,0xff800000u,0x7fc00000u,0x7f800001u,0xffbfffffu}) {
        auto b = base; pm_fixture::put(b, offset, bits); const auto s = decodeAmdPm(b, 0x730204);
        assert(s.error == -EDOM && s.fields.size() == 22);
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
    for (std::string bad : std::vector<std::string>{"", "{}", "null", "[]", encoded + "x", std::string(amdPmCaptureMaximumBytes+1, ' '), "{\"x\":\"\xed\xa0\x80\"}"})
        assert(parseAmdPmCapture(bad).error == -EINVAL);
    assert(parseAmdPmCapture(std::string(18, '[') + "0" + std::string(18, ']')).error == -EINVAL);
    // Sample full-array truncations and every byte near both document ends.
    // Exhaustive prefixes of a megabyte report make this quadratic without
    // adding coverage of JSON token types already tested by bounded_json.
    for (std::size_t i = 0; i < encoded.size(); ++i)
        if (i < 256 || i + 256 >= encoded.size() || i % 4096 == 0)
            assert(parseAmdPmCapture(encoded.substr(0, i)).error == -EINVAL);
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
    const char *limitNames[] = {"Fast PPT Limit","Fast PPT Value","TDC0 Limit","TDC0 Current Value",
        "TDC1 Limit","TDC1 Current Value","THM0 Limit","THM0 Current Value",
        "THM1 Limit","THM1 Current Value","FIT Limit","FIT Current Value",
        "VID0 Limit","VID0 Current Value","VID1 Limit","VID1 Current Value",
        "EDC_LIMIT_0","PEAK_TELEMETRY_CURRENT_0","PCC_LIMIT_0","PCC_RESIDENCY_0",
        "EDC_LIMIT_1","PEAK_TELEMETRY_CURRENT_1","PCC_LIMIT_1","PCC_RESIDENCY_1"};
    const unsigned limitOffsets[] = {0,4,8,12,16,20,24,28,32,36,40,44,48,52,56,60,
        456,460,464,468,472,476,480,484};
    const double limitValues[] = {400.5,350.25,100,63.5,150,74.25,95,82.5,80,71.25,1,0.25,1.5,1.25,1.375,1.125,
        200,180.5,4,0.75,250,220.25,5,0.5};
    for (unsigned i = 0; i < 8; ++i) {
        AmdPmOptions o; o.hasBclk = true; o.bclkMHz = 123.25;
        const auto s = decodeAmdPm(pm_fixture::bytes(lengths[i]),versions[i],o);
        assert(!s.error && s.originalLimits.size()==24 && s.fields.size()==22);
        for (unsigned n=0; n<24; ++n) {
            const auto &v=s.originalLimits[n];
            assert(v.name==limitNames[n] && v.offset==limitOffsets[n] && v.value==limitValues[n]);
            assert(v.unit=="unverified" && !v.clock && !v.scaled && !v.scaleError && !v.error);
        }
        const auto text=amdPmJson(s); assert(text.size()<amdPmCaptureMaximumBytes);
        assert(text.find("\"original_limits_units_verified\":false")!=std::string::npos);
        assert(amdPmJson(parseAmdPmCapture(text))==text);
    } ++groups;
    for (unsigned n=0; n<24; ++n) {
        for (unsigned bit=0; bit<32; ++bit) {
            auto b=base; const auto bits=UINT32_C(1)<<bit; pm_fixture::put(b,limitOffsets[n],bits);
            const auto s=decodeAmdPm(b,0x730204); assert(!s.error);
            assert(s.originalLimits[n].raw==bits && std::isfinite(s.originalLimits[n].value));
            for (unsigned j=0; j<24; ++j) if (j!=n) assert(s.originalLimits[j].value==limitValues[j]);
            for (unsigned j=0; j<s.fields.size(); ++j) assert(s.fields[j].raw==good.fields[j].raw);
        }
        for (auto bits : {UINT32_C(0),UINT32_C(0x80000000),UINT32_C(0xbf800000),UINT32_C(0x7f7fffff)}) {
            auto b=base; pm_fixture::put(b,limitOffsets[n],bits); const auto s=decodeAmdPm(b,0x730204);
            assert(!s.error && s.originalLimits[n].raw==bits);
            if (bits==0x80000000) assert(std::signbit(s.originalLimits[n].value));
        }
    } ++groups;
    for (unsigned n=0; n<24; ++n) for (auto bits : {UINT32_C(0x7f800000),UINT32_C(0xff800000),UINT32_C(0x7fc12345),UINT32_C(0x7f800001)}) {
        auto b=base; pm_fixture::put(b,limitOffsets[n],bits); const auto s=decodeAmdPm(b,0x730204);
        assert(s.error==-EDOM && s.originalLimits.size()==24 && s.fields.size()==22);
        assert(s.originalLimits[n].raw==bits && s.originalLimits[n].error==-EDOM);
        for (unsigned j=0; j<24; ++j) assert(s.originalLimits[j].error==(j==n?-EDOM:0));
        for (const auto &v : s.fields) assert(!v.error);
        const auto text=amdPmJson(s); assert(text.find("\"stored_value\":null")!=std::string::npos);
        const auto parsedLimits=parseAmdPmCapture(text); assert(parsedLimits.error==-EDOM && amdPmJson(parsedLimits)==text);
    } ++groups;
    auto current=amdPmJson(good); auto forged=current;
    replace(forged,"\"original_limits_units_verified\":false","\"original_limits_units_verified\":true");
    replace(forged,"\"unit\":\"unverified\"","\"unit\":\"W\"");
    replace(forged,"\"stored_value\":400.5","\"stored_value\":1");
    assert(amdPmJson(parseAmdPmCapture(forged))==current);
    const auto extension=current.find(",\"original_limits_units_verified\""); assert(extension!=std::string::npos);
    const auto old=current.substr(0,extension)+"}"; // Earlier schema-1 captures have no added array.
    assert(amdPmJson(parseAmdPmCapture(old))==current);
    const auto maximum=amdPmJson(decodeAmdPm(pm_fixture::bytes(11636),0x731004));
    assert(maximum.size()<amdPmCaptureMaximumBytes && !parseAmdPmCapture(maximum).error);
    ++groups;
    for (unsigned i=0; i<8; ++i) {
        pm_fixture::Driver d; d.files["pm_table_version"]=pm_fixture::le(versions[i],4);
        d.files["pm_table_size"]=pm_fixture::le(lengths[i],8); d.files["pm_table"]=pm_fixture::bytes(lengths[i]);
        pm_fixture::put(d.files["pm_table"],0x1dc,0x7f800001);
        const auto s=readAmdPmDriver(d.reader()); assert(s.snapshot.error==-EDOM && d.calls.size()==5);
        assert(s.snapshot.originalLimits[21].error==-EDOM && s.snapshot.fields[0].value==2000);
        d.calls.clear(); d.failCall=4; const auto failed=readAmdPmDriver(d.reader());
        assert(failed.snapshot.originalLimits.empty() && failed.snapshot.fields.empty() && failed.snapshot.raw.empty());
    } ++groups;
    const unsigned packageOffsets[] = {0x60,0x64,0x68,0x6c,0x70,0x74,0x78,
        0x40,0x44,0x48,0x4c,0x50,0x54,0x58,0x5c,0x7c,0x80,0x84,0x88,
        0x8c,0x90,0x94,0x98,0x9c,0xa0,0xa4,0xa8,0xac,0xb0,
        0xd0,0xd4,0xd8,0xdc,0xe0,0xe4,0xe8,0xec,0xf0,0xf4,0xf8,0xfc,
        0x100,0x104,0x108,0x10c,0x110,0x114,0x118,0x11c,0x120,0x124,0x128,0x12c};
    for (unsigned i=0; i<8; ++i) {
        AmdPmOptions o; o.hasBclk=true; o.bclkMHz=105;
        const auto s=decodeAmdPm(pm_fixture::bytes(lengths[i]),versions[i],o);
        assert(!s.error && s.originalPackage.size()==53);
        for (unsigned n=0; n<53; ++n) {
            const auto &f=s.originalPackage[n];
            const char *group=n<7?"power":n<15?"workload":n<19?"power_model":n<29?"voltage_policy":"limit_contributors";
            assert(f.offset==packageOffsets[n] && f.value==10.0+n/4.0 && f.group==group);
            assert(f.unit=="unverified" && !f.clock && !f.scaled && !f.scaleError && !f.error);
        }
        assert(s.originalPackage[0].name=="CPU Power" && s.originalPackage[6].name=="Socket Power");
        assert(s.originalPackage[15].name=="VDDCR_CPU_DYN_POWER_WC" && s.originalPackage[19].name=="FIT PID0");
        assert(s.originalPackage[29].name=="PPT0" && s.originalPackage[52].name=="PPT_APU1");
        const auto text=amdPmJson(s); assert(text.size()<amdPmCaptureMaximumBytes && amdPmJson(parseAmdPmCapture(text))==text);
        assert(text.find("\"original_package_units_verified\":false")!=std::string::npos);
        assert(text.find("\"original_package_targets_verified\":false")!=std::string::npos);
    } ++groups;
    for (unsigned n=0; n<53; ++n) for (unsigned bit=0; bit<32; ++bit) {
        auto b=base; const auto bits=UINT32_C(1)<<bit; pm_fixture::put(b,packageOffsets[n],bits);
        const auto s=decodeAmdPm(b,0x730204); assert(!s.error && s.originalPackage[n].raw==bits);
        if (bit==31) assert(std::signbit(s.originalPackage[n].value));
        for (unsigned j=0; j<53; ++j) if (j!=n) assert(s.originalPackage[j].value==10.0+j/4.0);
        for (unsigned j=0; j<24; ++j) {
            if (j<s.fields.size()) assert(s.fields[j].raw==good.fields[j].raw);
            assert(s.originalLimits[j].raw==good.originalLimits[j].raw);
        }
    } ++groups;
    for (unsigned n=0; n<53; ++n) for (auto bits : {UINT32_C(0x7f800000),UINT32_C(0xff800000),UINT32_C(0x7fc12345),UINT32_C(0x7f800001)}) {
        auto b=base; pm_fixture::put(b,packageOffsets[n],bits);
        const auto s=decodeAmdPm(b,0x730204); assert(s.error==-EDOM && s.originalPackage.size()==53);
        for (unsigned j=0; j<53; ++j) assert(s.originalPackage[j].error==(n==j?-EDOM:0));
        for (unsigned j=0; j<24; ++j) assert(!s.originalLimits[j].error);
        for (const auto &v : s.fields) assert(!v.error);
        const auto text=amdPmJson(s); assert(amdPmJson(parseAmdPmCapture(text))==text);
    } ++groups;
    auto packageForged=current;
    replace(packageForged,"\"original_package_units_verified\":false","\"original_package_units_verified\":true");
    replace(packageForged,"\"original_package_targets_verified\":false","\"original_package_targets_verified\":true");
    replace(packageForged,"\"name\":\"CPU Power\"","\"name\":\"Forged\"");
    replace(packageForged,"\"group\":\"power\"","\"group\":\"physical_cpu_0\"");
    assert(amdPmJson(parseAmdPmCapture(packageForged))==current);
    const auto packageStart=current.find(",\"original_package_units_verified\"");
    assert(packageStart!=std::string::npos && amdPmJson(parseAmdPmCapture(current.substr(0,packageStart)+"}"))==current);
    auto extreme=pm_fixture::bytes(11636);
    for (unsigned offset : packageOffsets) pm_fixture::put(extreme,offset,0xff7fffff);
    const auto extremeJson=amdPmJson(decodeAmdPm(extreme,0x731004));
    assert(extremeJson.size()<amdPmCaptureMaximumBytes && amdPmJson(parseAmdPmCapture(extremeJson))==extremeJson);
    assert(parseAmdPmCapture("{}").originalPackage.empty());
    assert(decodeAmdPm(base,0x730205).originalPackage.empty());
    assert(decodeAmdPm(std::vector<std::uint8_t>(0x130),0x730204).originalPackage.empty());
    ++groups;
    for (unsigned i=0; i<8; ++i) {
        pm_fixture::Driver d; d.files["pm_table_version"]=pm_fixture::le(versions[i],4);
        d.files["pm_table_size"]=pm_fixture::le(lengths[i],8); d.files["pm_table"]=pm_fixture::bytes(lengths[i]);
        pm_fixture::put(d.files["pm_table"],0x60,0x7f800001);
        const auto s=readAmdPmDriver(d.reader()); assert(s.snapshot.error==-EDOM && d.calls.size()==5);
        assert(s.snapshot.originalPackage[0].error==-EDOM && s.snapshot.originalPackage[1].value==10.25);
        assert(s.snapshot.originalLimits[0].value==400.5 && s.snapshot.fields[0].value==2000);
        d.calls.clear(); d.failCall=4;
        assert(readAmdPmDriver(d.reader()).snapshot.originalPackage.empty());
    } ++groups;
    const unsigned fabricOffsets[] = {0x154,0x158,0x15c,0x160,0x164,0x168,0x16c,0x170,0x174,
        0x24c,0x250,0x254,0x258,0x25c,0x260,0x264,0x268,0x26c,0x270,
        0x274,0x278,0x27c,0x280,0x284,0x288,0x28c,0x290};
    const char *fabricNames[] = {"FCLK_PCIE_UP_THRESHOLD","FCLK_PCIE_BANDWIDTH","FCLK_PCIE_DOWN_THRESHOLD",
        "FCLK_XGMI_UP_THRESHOLD","FCLK_XGMI_BANDWIDTH","FCLK_XGMI_DOWN_THRESHOLD",
        "FCLK_CXL_UP_THRESHOLD","FCLK_CXL_BANDWIDTH","FCLK_CXL_DOWN_THRESHOLD",
        "SMNCLK","SMNCLK_EFF","WAFLCLK","WAFLCLK_EFF","MP0CLK","MP0CLK_EFF",
        "MP1CLK","MP1CLK_EFF","MPIOCLK","MPIOCLK_EFF","ACLK","ACLK_EFF","SVI3CLK","SVI3CLK_EFF",
        "MPDMA_TF_CLK","MPDMA_TF_CLK_EFF","MPDMA_PM_CLK","MPDMA_PM_CLK_EFF"};
    for (unsigned i=0; i<8; ++i) {
        AmdPmOptions o; o.hasBclk=true; o.bclkMHz=105.5;
        const auto s=decodeAmdPm(pm_fixture::bytes(lengths[i]),versions[i],o);
        assert(!s.error && s.originalFabric.size()==27);
        for (unsigned n=0; n<27; ++n) {
            const auto &f=s.originalFabric[n];
            assert(f.name==fabricNames[n] && f.offset==fabricOffsets[n] && f.value==20+n/2.0);
            assert(f.group==(n<9?"link_bandwidth":"internal_clocks"));
            assert(f.unit=="unverified" && !f.clock && !f.scaled && !f.scaleError && !f.error);
        }
        const auto text=amdPmJson(s);
        assert(text.size()<amdPmCaptureMaximumBytes && amdPmJson(parseAmdPmCapture(text))==text);
    } ++groups;
    for (unsigned n=0; n<27; ++n) for (unsigned bit=0; bit<32; ++bit) {
        auto b=base; pm_fixture::put(b,fabricOffsets[n],UINT32_C(1)<<bit);
        const auto s=decodeAmdPm(b,0x730204); assert(!s.error && s.originalFabric[n].raw==(UINT32_C(1)<<bit));
        if (bit==31) assert(std::signbit(s.originalFabric[n].value));
        for (unsigned j=0; j<27; ++j) if (j!=n) assert(s.originalFabric[j].value==20+j/2.0);
        for (unsigned j=0; j<24; ++j) {
            if (j<s.fields.size()) assert(s.fields[j].raw==good.fields[j].raw);
            assert(s.originalLimits[j].raw==good.originalLimits[j].raw);
        }
        for (unsigned j=0; j<53; ++j) assert(s.originalPackage[j].raw==good.originalPackage[j].raw);
    } ++groups;
    for (unsigned n=0; n<27; ++n) for (auto bits : {UINT32_C(0x7f800000),UINT32_C(0xff800000),UINT32_C(0x7fc12345),UINT32_C(0x7f800001)}) {
        auto b=base; pm_fixture::put(b,fabricOffsets[n],bits);
        const auto s=decodeAmdPm(b,0x730204); assert(s.error==-EDOM && s.originalFabric.size()==27);
        for (unsigned j=0; j<27; ++j) assert(s.originalFabric[j].error==(n==j?-EDOM:0));
        for (const auto &f : s.fields) assert(!f.error);
        for (const auto &f : s.originalLimits) assert(!f.error);
        for (const auto &f : s.originalPackage) assert(!f.error);
        const auto text=amdPmJson(s); assert(amdPmJson(parseAmdPmCapture(text))==text);
    } ++groups;
    auto fabricForged=current;
    replace(fabricForged,"\"original_fabric_units_verified\":false","\"original_fabric_units_verified\":true");
    replace(fabricForged,"\"original_fabric_targets_verified\":false","\"original_fabric_targets_verified\":true");
    replace(fabricForged,"\"name\":\"SMNCLK\"","\"name\":\"Forged MHz\"");
    replace(fabricForged,"\"group\":\"internal_clocks\"","\"group\":\"physical_ccd_0\"");
    assert(amdPmJson(parseAmdPmCapture(fabricForged))==current);
    const auto fabricStart=current.find(",\"original_fabric_units_verified\"");
    assert(fabricStart!=std::string::npos && amdPmJson(parseAmdPmCapture(current.substr(0,fabricStart)+"}"))==current);
    // Largest supported table with every decoded float at maximum negative
    // magnitude, plus a supplied BCLK: the full current capture must fit.
    auto allExtreme=pm_fixture::bytes(11636);
    for (unsigned offset=0; offset<allExtreme.size(); offset+=4) pm_fixture::put(allExtreme,offset,0xff7fffff);
    AmdPmOptions hugeOptions; hugeOptions.hasBclk=true; hugeOptions.bclkMHz=123.25;
    const auto allExtremeJson=amdPmJson(decodeAmdPm(allExtreme,0x731004,hugeOptions));
    assert(allExtremeJson.size()<amdPmCaptureMaximumBytes && amdPmJson(parseAmdPmCapture(allExtremeJson))==allExtremeJson);
    assert(parseAmdPmCapture("{}").originalFabric.empty());
    assert(decodeAmdPm(base,0x730205).originalFabric.empty());
    assert(decodeAmdPm(std::vector<std::uint8_t>(0x294),0x730204).originalFabric.empty());
    ++groups;
    for (unsigned i=0; i<8; ++i) {
        pm_fixture::Driver d; d.files["pm_table_version"]=pm_fixture::le(versions[i],4);
        d.files["pm_table_size"]=pm_fixture::le(lengths[i],8); d.files["pm_table"]=pm_fixture::bytes(lengths[i]);
        pm_fixture::put(d.files["pm_table"],0x24c,0x7f800001);
        const auto s=readAmdPmDriver(d.reader()); assert(s.snapshot.error==-EDOM && d.calls.size()==5);
        assert(s.snapshot.originalFabric[9].error==-EDOM && s.snapshot.originalFabric[0].value==20);
        for (unsigned failed=1; failed<=5; ++failed) {
            d.calls.clear(); d.failCall=failed;
            const auto result=readAmdPmDriver(d.reader());
            assert(result.snapshot.originalFabric.empty() && result.snapshot.raw.empty());
        }
        d.calls.clear(); d.failCall=0; d.mutateAfter=4;
        d.mutatedVersion=versions[i]==0x730404?0x731004:0x730404;
        const auto moved=readAmdPmDriver(d.reader());
        assert(moved.snapshot.error==-EAGAIN && moved.snapshot.originalFabric.empty() && d.calls.size()==5);
    } ++groups;
    const char *arrayNames[] = {"GMI_FULL_WIDTH","GMI_HALF_WIDTH","GMI_QUARTER_WIDTH",
        "DIMM_POWER","DIMM_TEMP","PCIE_ALLOWED","PCIE_ACTIVE","PCIE_UP_THRESHOLD",
        "PCIE_DOWN_THRESHOLD","PCIE_IOHC_BANDWIDTH[4]"};
    const unsigned arrayWidths[] = {12,12,12,24,24,1,1,1,1,4};
    for (unsigned i=0; i<8; ++i) {
        AmdPmOptions o; o.hasBclk=true; o.bclkMHz=105.5;
        const auto s=decodeAmdPm(pm_fixture::bytes(lengths[i]),versions[i],o);
        assert(!s.error && s.originalArrays.size()==92);
        unsigned n=0;
        for (unsigned label=0; label<10; ++label) for (unsigned slot=0; slot<arrayWidths[label]; ++slot,++n) {
            const auto &f=s.originalArrays[n];
            assert(f.name==arrayNames[label] && f.slot==slot && f.slotCount==arrayWidths[label]);
            assert(f.offset==0x294+4*n && f.value==64+n);
            assert(f.group==(n<36?"gmi_storage":n<84?"dimm_storage":"pcie_storage"));
            assert(f.unit=="unverified" && !f.clock && !f.scaled && !f.error && !f.scaleError);
        }
        assert(n==92);
        const auto text=amdPmJson(s);
        assert(text.size()<amdPmCaptureMaximumBytes && amdPmJson(parseAmdPmCapture(text))==text);
        for (const auto *fields : {&s.fields,&s.originalLimits,&s.originalPackage,&s.originalFabric})
            for (const auto &f : *fields) assert(!f.slotCount);
    } ++groups;
    for (unsigned n=0; n<92; ++n) for (unsigned bit=0; bit<32; ++bit) {
        auto b=base; pm_fixture::put(b,0x294+4*n,UINT32_C(1)<<bit);
        const auto s=decodeAmdPm(b,0x730204);
        assert(!s.error && s.originalArrays[n].raw==(UINT32_C(1)<<bit));
        if (bit==31) assert(std::signbit(s.originalArrays[n].value));
        for (unsigned j=0; j<92; ++j) if (j!=n) assert(s.originalArrays[j].value==64+j);
        assert(s.fields[0].value==2000 && s.originalLimits[0].value==400.5);
        assert(s.originalPackage[0].value==10 && s.originalFabric.back().value==33);
    } ++groups;
    for (unsigned n=0; n<92; ++n)
        for (auto bits : {UINT32_C(0x7f800000),UINT32_C(0xff800000),UINT32_C(0x7fc12345),UINT32_C(0x7f800001)}) {
            auto b=base; pm_fixture::put(b,0x294+4*n,bits);
            const auto s=decodeAmdPm(b,0x730204);
            assert(s.error==-EDOM && s.originalArrays.size()==92);
            for (unsigned j=0; j<92; ++j) {
                const auto &f=s.originalArrays[j];
                assert(f.error==(j==n?-EDOM:0));
                if (j==n) assert(f.raw==bits); else assert(f.value==64+j);
            }
            for (const auto *fields : {&s.fields,&s.originalLimits,&s.originalPackage,&s.originalFabric})
                for (const auto &f : *fields) assert(!f.error);
            const auto text=amdPmJson(s); assert(amdPmJson(parseAmdPmCapture(text))==text);
        }
    ++groups;
    auto arrayForged=current;
    replace(arrayForged,"\"original_arrays_units_verified\":false","\"original_arrays_units_verified\":true");
    replace(arrayForged,"\"original_arrays_targets_verified\":false","\"original_arrays_targets_verified\":true");
    replace(arrayForged,"\"name\":\"DIMM_POWER\"","\"name\":\"Forged module power\"");
    replace(arrayForged,"\"slot\":0,\"slot_count\":12","\"slot\":999,\"slot_count\":1000");
    replace(arrayForged,"\"group\":\"dimm_storage\"","\"group\":\"physical_dimm_0\"");
    assert(amdPmJson(parseAmdPmCapture(arrayForged))==current);
    const auto arraysStart=current.find(",\"original_arrays_units_verified\"");
    assert(arraysStart!=std::string::npos);
    assert(amdPmJson(parseAmdPmCapture(current.substr(0,arraysStart)+"}"))==current);
    assert(parseAmdPmCapture("{}").originalArrays.empty());
    assert(decodeAmdPm(base,0x730205).originalArrays.empty());
    assert(decodeAmdPm(std::vector<std::uint8_t>(0x403),0x730204).originalArrays.empty());
    ++groups;
    // Expanded PM captures must survive both GUI and CLI size bounds, including
    // worst-case floats and the CLI envelope. Other formats remain at 64 KiB.
    assert(allExtremeJson.size()>65536);
    for (const auto &capture : {allExtremeJson,
        std::string("{\"schema_version\":1,\"command\":\"amd-pm-decode\",\"data\":{\"pm\":")+allExtremeJson+"}}"}) {
        assert(capture.size()<amdPmCaptureMaximumBytes);
        const auto padded=capture+std::string(amdPmCaptureMaximumBytes-capture.size(),' ');
        assert(amdPmJson(parseAmdPmCapture(padded))==allExtremeJson);
        assert(parseAmdPmCapture(padded+" ").error==-EINVAL);
    }
    std::string nodeBomb="{\"ignored\":[";
    for (unsigned i=0; i<65537; ++i) { if (i) nodeBomb+=','; nodeBomb+="null"; }
    nodeBomb+="]}";
    assert(parseAmdPmCapture(nodeBomb).detail=="JSON nesting or value limit exceeded");
    ++groups;
    for (unsigned i=0; i<8; ++i) {
        pm_fixture::Driver d; d.files["pm_table_version"]=pm_fixture::le(versions[i],4);
        d.files["pm_table_size"]=pm_fixture::le(lengths[i],8); d.files["pm_table"]=pm_fixture::bytes(lengths[i]);
        pm_fixture::put(d.files["pm_table"],0x384,0x7f800001);
        const auto s=readAmdPmDriver(d.reader()); assert(s.snapshot.error==-EDOM && d.calls.size()==5);
        assert(s.snapshot.originalArrays[60].error==-EDOM && s.snapshot.originalArrays[59].value==123);
        for (unsigned failed=1; failed<=5; ++failed) {
            d.calls.clear(); d.failCall=failed;
            const auto result=readAmdPmDriver(d.reader());
            assert(result.snapshot.originalArrays.empty() && result.snapshot.raw.empty());
        }
        d.calls.clear(); d.failCall=0; d.mutateAfter=4;
        d.mutatedVersion=versions[i]==0x730404?0x731004:0x730404;
        const auto moved=readAmdPmDriver(d.reader());
        assert(moved.snapshot.error==-EAGAIN && moved.snapshot.originalArrays.empty() && d.calls.size()==5);
    } ++groups;
    // Independent terminal offsets from the MP5 copy loops, including the
    // 6-slot case mishandled by the legacy constructor and all 16 slots.
    const unsigned ccdStarts[] = {0xab4,0xf34,0x13b4,0x1834,0x1cb4,0x2134,0x25b4,0x2a34};
    for (unsigned i=0; i<8; ++i) {
        auto b=pm_fixture::bytes(lengths[i]);
        for (unsigned off=0x634; off<lengths[i]; off+=4) pm_fixture::put(b,off,0x3f000000u+off);
        AmdPmOptions firmwareOptions; firmwareOptions.hasBclk=true; firmwareOptions.bclkMHz=105;
        const auto s=decodeAmdPm(b,versions[i],firmwareOptions); assert(!s.error);
        const unsigned n=2*(i+1);
        assert(s.firmwareCcdSlots==n && s.firmwareCores.size()==144*n && s.firmwareCcds.size()==13*n);
        assert(s.firmwareCores.front().offset==0x634 && s.firmwareCores.back().offset+4==ccdStarts[i]);
        assert(s.firmwareCcds.front().offset==ccdStarts[i] && s.firmwareCcds.back().offset+4==lengths[i]);
        std::set<unsigned> seen;
        for (const auto *rows : {&s.firmwareCores,&s.firmwareCcds}) for (const auto &f : *rows) {
            assert(seen.insert(f.offset).second && f.offset>=0x634 && f.offset+4<=lengths[i]);
            assert(f.raw==0x3f000000u+f.offset && f.unit=="unverified" && !f.error && !f.clock && !f.scaled);
            assert(f.row*f.slotCount+f.slot==unsigned(&f-&rows->front()));
        }
        for (const auto &f : s.fields) assert(f.offset<0x634 && f.name.find("VDDG")==std::string::npos);
        assert(seen.size()==(lengths[i]-0x634)/4);
        auto text=amdPmJson(s);
        auto forged=text; replace(forged,"\"firmware_arrays_labels_verified\":false","\"firmware_arrays_labels_verified\":true");
        assert(amdPmJson(parseAmdPmCapture(forged))==text);
    } ++groups;
    for (unsigned off : {0x634u,0x714u,0x71cu,0x2a30u,0x2a34u,0x2d70u}) {
        auto b=pm_fixture::bytes(11636); pm_fixture::put(b,off,0x7f800001);
        const auto s=decodeAmdPm(b,0x731004); assert(s.error==-EDOM && s.fields.size()==22);
        unsigned bad=0;
        for (const auto *rows : {&s.firmwareCores,&s.firmwareCcds}) for (const auto &f : *rows) {
            if (f.error) { ++bad; assert(f.offset==off && f.raw==0x7f800001); }
        }
        assert(bad==1 && amdPmJson(parseAmdPmCapture(amdPmJson(s)))==amdPmJson(s));
    } ++groups;
    auto largest=pm_fixture::bytes(11636);
    for (unsigned off=0x634; off<11636; off+=4) pm_fixture::put(largest,off,0xff7fffff);
    const auto largeText=amdPmJson(decodeAmdPm(largest,0x731004));
    assert(largeText.size()<amdPmCaptureMaximumBytes && !parseAmdPmCapture(largeText).error);
    assert(decodeAmdPm(largest,0x731005).firmwareCores.empty());
    largest.pop_back(); assert(decodeAmdPm(largest,0x731004).firmwareCcds.empty());
    ++groups;
    std::cout << groups << " AMD PM scenario groups passed\n";
}
