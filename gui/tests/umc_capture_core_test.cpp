// SPDX-License-Identifier: GPL-2.0-only
#include "umc_capture.h"
#include <cassert>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cerrno>
using namespace octool::core;
namespace {
const std::string minimal = R"({"format":"octool-amd-umc-v1","bank":0,"refresh_slot":0,"registers":[{"offset":516,"value":34}]})";
std::string replace(std::string text, const std::string &old, const std::string &value) {
    const auto p = text.find(old); assert(p != std::string::npos); return text.replace(p, old.size(), value);
}
std::string gui(unsigned slot, bool complete = true) {
    std::string out = "{\"format\":\"octool-amd-umc-v1\",\"bank\":22,\"refresh_slot\":" + std::to_string(slot) + ",\"registers\":[";
    for (auto offset : amdUmcOffsets(slot)) {
        if (out.back() != '[') out += ',';
        out += "{\"offset\":" + std::to_string(offset) + ",\"value\":4294967295}";
        if (!complete) break;
    }
    return out + "]}";
}
std::string report(const char *command, unsigned slot, bool complete = true) {
    std::ostringstream out;
    out << "{\"schema_version\":1,\"command\":\"" << command << "\",\"ok\":true,\"error\":0,\"data\":{\"cpu\":130,\"pci\":{\"domain\":0,\"bus\":2,\"device\":3,\"function\":1},\"bank\":22,\"refresh_slot\":" << slot << ",\"registers\":[";
    bool first = true;
    for (auto offset : amdUmcOffsets(slot)) {
        if (!first) out << ',';
        first = false;
        out << "{\"offset\":\"0x" << std::hex << std::setw(8) << std::setfill('0') << offset << "\",\"raw\":\"0xffffffff\"}";
        if (!complete) break;
    }
    out << "],\"fields\":[{\"id\":32,\"encoded\":-999}]}}"; return out.str();
}
void snapshots() {
    auto c = parseUmcCapture(minimal);
    assert(!c.error && !c.hasCpu && !c.hasPci && c.decoded.values.size() == 212);
    assert(!c.decoded.values[32].error && c.decoded.values[32].encoded == 34);
    assert(c.decoded.values[203].error == -ENODATA);
    assert(!parseUmcCapture(c.snapshotJson).error);
    for (unsigned slot = 0; slot < 16; ++slot) {
        c = parseUmcCapture(gui(slot)); assert(!c.error && c.registers.size() == 56);
        for (const auto &v : c.decoded.values) assert(!v.error);
    }
    c = parseUmcCapture(replace(minimal, "\"bank\":0", "\"cpu\":4294967295,\"bus\":255,\"device\":31,\"function\":7,\"bank\":0"));
    assert(!c.error && c.hasCpu && c.hasPci && c.target.cpu == UINT32_MAX && c.target.bus == 255);
}
void reports() {
    for (unsigned slot = 0; slot < 16; ++slot) {
        const auto c = parseUmcCapture(report("amd-umc-read", slot));
        assert(!c.error && c.hasCpu && c.hasPci && c.target.cpu == 130 && c.target.bus == 2);
        assert(c.registers.size() == 56 && c.decoded.values[32].encoded != unsigned(-999));
        const auto reloaded = parseUmcCapture(c.snapshotJson);
        assert(!reloaded.error && reloaded.registers.size() == 56 && reloaded.target.refreshSlot == slot);
    }
    assert(parseUmcCapture(report("amd-umc-read", 0, false)).error);
    const auto partial = parseUmcCapture(report("amd-umc-decode", 0, false));
    assert(!partial.error && partial.registers.size() == 1);
    const auto r = report("amd-umc-read", 0);
    for (const auto &bad : {replace(r, "true", "false"), replace(r, "\"error\":0", "\"error\":-5"),
        replace(r, "\"schema_version\":1", "\"schema_version\":2"), replace(r, "amd-umc-read", "amd-umc-diff"),
        replace(r, "\"cpu\":130", "\"cpu\":null"), replace(r, "\"domain\":0", "\"domain\":1"),
        replace(r, "0xffffffff", "0x100000000"), replace(r, "\"0xffffffff\"", "4294967295"),
        replace(r, "\"data\":", "\"format\":\"octool-amd-umc-v1\",\"data\":"),
        replace(r, "0xffffffff", "0x+fffffff")}) assert(parseUmcCapture(bad).error);
}
void numbers() {
    for (const auto &n : {"4294967295", "4294967295.0", "4.294967295e9", "429496729500e-2"}) {
        const auto c = parseUmcCapture(replace(minimal, "\"value\":34", std::string("\"value\":") + n));
        assert(!c.error && c.registers[0].value == UINT32_MAX);
    }
    for (const auto &n : {"-1", "0.5", "1e-99999", "1e99999", "4294967296", "4294967295.00000001", "1.00000000000000001",
        "18446744073709551616", "NaN", "Infinity", "0x22", "+34", "034", "34.", ".34", "1e", "1e+", "true", "null", "\"34\""})
        assert(parseUmcCapture(replace(minimal, "\"value\":34", std::string("\"value\":") + n)).error);
    assert(parseUmcCapture(replace(minimal, "\"bank\":0", "\"bank\":23")).error);
    assert(parseUmcCapture(replace(minimal, "\"refresh_slot\":0", "\"refresh_slot\":16")).error);
    assert(parseUmcCapture(replace(minimal, "\"bank\":0", "\"bus\":2,\"bank\":0")).error);
}
void jsonBoundaries() {
    for (const auto &bad : {std::string("{}"), minimal + "x", minimal + minimal, replace(minimal, "\"bank\":0", "\"bank\":0,\"bank\":1"),
        replace(minimal, "\"bank\":0", "\"bank\":0,\"b\\u0061nk\":1"),
        replace(minimal, "\"bank\":0", "\"extra\":{\"x\":0,\"x\":1},\"bank\":0"),
        replace(minimal, "\"bank\":0", "\"extra\":[0,],\"bank\":0"),
        replace(minimal, "516", "517"), replace(minimal, "34}]", "34},{\"offset\":516,\"value\":35}]"),
        replace(minimal, "\"value\":34", "\"value\":34,"), replace(minimal, "\"bank\":0", "bank:0")}) {
        const auto c = parseUmcCapture(bad); assert(c.error && c.registers.empty() && c.decoded.values.empty() && c.snapshotJson.empty());
    }
    auto full = minimal + std::string(65536 - minimal.size(), ' ');
    assert(!parseUmcCapture(full).error && parseUmcCapture(full + ' ').error);
    assert(!parseUmcCapture("\xef\xbb\xbf" + minimal).error);
    assert(parseUmcCapture(minimal + '\0').error);
    const auto deep = replace(minimal, "\"bank\":0", "\"extra\":" + std::string(17, '[') + '0' + std::string(17, ']') + ",\"bank\":0");
    assert(parseUmcCapture(deep).error);
    std::string many = "[0"; for (unsigned i = 0; i < 8192; ++i) many += ",0";
    assert(parseUmcCapture(replace(minimal, "\"bank\":0", "\"extra\":" + many + "],\"bank\":0")).error);
}
void unicode() {
    for (const auto &value : {std::string("\xe5\x86\x85\xe5\xad\x98"), std::string("\\u5185\\u5b58"), std::string("\\ud83d\\ude00"), std::string("\\u0000")})
        assert(!parseUmcCapture(replace(minimal, "\"bank\":0", "\"note\":\"" + value + "\",\"bank\":0")).error);
    for (const auto &value : {std::string("\\ud800"), std::string("\\udc00"), std::string("\\ud800\\u0061"), std::string("\\x00"),
        std::string("\xc0\xaf"), std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"), std::string("\xe5\x20\x20"), std::string("\n")})
        assert(parseUmcCapture(replace(minimal, "\"bank\":0", "\"note\":\"" + value + "\",\"bank\":0")).error);
}
void comparisons() {
    const auto before = parseUmcCapture(minimal), same = parseUmcCapture(minimal);
    auto diff = compareUmcCaptures(before, same);
    assert(!diff.error && diff.fields.size() == 212 && diff.fields[32].state == UmcChange::Unchanged);
    assert(diff.fields[203].state == UmcChange::MissingBoth);
    const auto after = parseUmcCapture(replace(minimal, "\"value\":34", "\"value\":35"));
    diff = compareUmcCaptures(before, after); assert(diff.fields[32].state == UmcChange::Changed);
    const auto raw = parseUmcCapture(replace(minimal, "\"value\":34", "\"value\":2147483682"));
    diff = compareUmcCaptures(before, raw); assert(diff.fields[32].state == UmcChange::RawOnly);
    const auto other = parseUmcCapture(replace(minimal, "\"offset\":516", "\"offset\":4624"));
    diff = compareUmcCaptures(before, other);
    assert(diff.fields[32].state == UmcChange::MissingAfter && diff.fields[203].state == UmcChange::MissingBefore);
    unsigned total = 0; for (auto n : diff.counts) total += n; assert(total == 212);
    const auto reverse = compareUmcCaptures(other, before);
    assert(reverse.counts[3] == diff.counts[4] && reverse.counts[4] == diff.counts[3]);
    for (unsigned i = 0; i < 6; ++i) assert(std::string(umcChangeName(UmcChange(i))) != "unknown");
}
void comparisonGuards() {
    const auto before = parseUmcCapture(minimal);
    assert(compareUmcCaptures(before, parseUmcCapture(replace(minimal, "\"bank\":0", "\"bank\":1"))).error == -EXDEV);
    assert(compareUmcCaptures(before, parseUmcCapture(replace(minimal, "\"refresh_slot\":0", "\"refresh_slot\":1"))).error == -EXDEV);
    assert(compareUmcCaptures(before, UmcCapture{}).error);
    auto forged = before; forged.registers.push_back(forged.registers[0]);
    assert(compareUmcCaptures(before, forged).error);
    forged = before; forged.decoded.values.clear();
    assert(!compareUmcCaptures(before, forged).error); // Always re-decode raw evidence.
    forged.target.cpu = 999; forged.target.bus = 255;
    assert(!compareUmcCaptures(before, forged).error); // OS numbering is not a physical mapping.
}
void truncationAndMutation() {
    for (const auto &valid : {minimal, report("amd-umc-read", 15)}) {
        for (std::size_t size = 0; size < valid.size(); ++size) assert(parseUmcCapture(valid.substr(0, size)).error);
        for (std::size_t i = 0; i < valid.size(); ++i) {
            auto bad = valid; bad[i] = '\0'; assert(parseUmcCapture(bad).error);
        }
    }
}
}
int main() {
    snapshots(); reports(); numbers(); jsonBoundaries(); unicode(); comparisons(); comparisonGuards(); truncationAndMutation();
    std::cout << "8 UMC capture and comparison scenario groups passed\n";
}
