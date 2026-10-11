// SPDX-License-Identifier: GPL-2.0-only
#include "umc_capture.h"
#include "bounded_json.h"
#include <cerrno>
#include <map>
#include <stdexcept>
#include <utility>

namespace octool { namespace core {
namespace {
using capture_json::check;
using capture_json::Json;
using capture_json::Parser;
using capture_json::uint;
using capture_json::word;
using capture_json::hexDigit;
using capture_json::equals;

std::string snapshot(const UmcCapture &c) {
    std::string out = "{\"format\":\"octool-amd-umc-v1\",\"bank\":" + std::to_string(c.target.bank) +
        ",\"refresh_slot\":" + std::to_string(c.target.refreshSlot) + ",\"registers\":[";
    for (const auto &r : c.registers) {
        if (out.back() != '[') out += ',';
        out += "{\"offset\":" + std::to_string(r.offset) + ",\"value\":" + std::to_string(r.value) + '}';
    }
    out += ']';
    if (c.hasCpu) out += ",\"cpu\":" + std::to_string(c.target.cpu);
    if (c.hasPci) out += ",\"bus\":" + std::to_string(c.target.bus) + ",\"device\":" + std::to_string(c.target.device) + ",\"function\":" + std::to_string(c.target.function);
    return out + ",\"origin\":\"Offline import; platform identity and capture origin unverified\"}\n";
}
}
UmcCapture parseUmcCapture(const std::string &json) {
    UmcCapture c;
    try {
        const auto root = Parser(json).parse(); check(root.kind == Json::Object, "Expected snapshot object");
        const bool report = root.has("schema_version"); const Json *data = &root; bool complete = false;
        if (report) {
            check(!root.has("format"), "Ambiguous snapshot format");
            check(uint(root.at("schema_version"), 1) == 1 && equals(root.at("ok"), Json::Boolean, "true") &&
                uint(root.at("error"), 0) == 0, "Only successful schema v1 reports can be imported");
            complete = equals(root.at("command"), Json::String, "amd-umc-read");
            check(complete || equals(root.at("command"), Json::String, "amd-umc-decode"), "Not a UMC capture report");
            data = &root.at("data"); check(data->kind == Json::Object, "Invalid report data");
        } else check(equals(root.at("format"), Json::String, "octool-amd-umc-v1"), "Unknown UMC snapshot format");
        c.target.bank = uint(data->at("bank"), 22); c.target.refreshSlot = uint(data->at("refresh_slot"), 15);
        if (data->has("cpu") && data->at("cpu").kind != Json::Null) {
            c.target.cpu = uint(data->at("cpu"), UINT32_MAX); c.hasCpu = true;
        }
        const Json *pci = data;
        if (report) {
            if (data->has("pci") && data->at("pci").kind != Json::Null) {
                pci = &data->at("pci"); check(pci->kind == Json::Object, "Invalid PCI metadata");
                uint(pci->at("domain"), 0); c.hasPci = true;
            }
        } else c.hasPci = data->has("bus") || data->has("device") || data->has("function");
        if (c.hasPci) {
            c.target.bus = uint(pci->at("bus"), 255); c.target.device = uint(pci->at("device"), 31); c.target.function = uint(pci->at("function"), 7);
        }
        check(!complete || (c.hasCpu && c.hasPci), "Read report is missing target metadata");
        const auto &registers = data->at("registers");
        check(registers.kind == Json::Array && !registers.items.empty() && registers.items.size() <= 56 &&
            (!complete || registers.items.size() == 56), "Invalid register count");
        for (const auto &item : registers.items) {
            UmcRegister r; r.offset = report ? word(item.at("offset")) : uint(item.at("offset"), UINT32_MAX);
            r.value = report ? word(item.at("raw")) : uint(item.at("value"), UINT32_MAX); c.registers.push_back(r);
        }
        c.decoded = decodeAmdUmc(c.registers, c.target.refreshSlot);
        check(!c.decoded.error, "Duplicate or unrecognized register offset for selected refresh slot");
        c.snapshotJson = report ? snapshot(c) : json; c.error = 0;
    } catch (const std::invalid_argument &e) {
        c = UmcCapture{}; c.error = -EINVAL; c.detail = e.what();
    }
    return c;
}
const char *umcChangeName(UmcChange state) {
    switch (state) {
    case UmcChange::Unchanged: return "unchanged"; case UmcChange::Changed: return "changed";
    case UmcChange::RawOnly: return "raw_only"; case UmcChange::MissingBefore: return "missing_before";
    case UmcChange::MissingAfter: return "missing_after"; case UmcChange::MissingBoth: return "missing_both";
    }
    return "unknown";
}
UmcComparison compareUmcCaptures(const UmcCapture &before, const UmcCapture &after) {
    UmcComparison out;
    if (before.error || after.error || before.registers.empty() || after.registers.empty()) { out.error = -EINVAL; return out; }
    if (before.target.bank != after.target.bank || before.target.refreshSlot != after.target.refreshSlot) { out.error = -EXDEV; return out; }
    const auto b = decodeAmdUmc(before.registers, before.target.refreshSlot), a = decodeAmdUmc(after.registers, after.target.refreshSlot);
    if (before.target.bank > 22 || b.error || a.error) { out.error = -EINVAL; return out; }
    for (std::size_t i = 0; i < b.values.size(); ++i) {
        UmcFieldChange change; change.before = b.values[i]; change.after = a.values[i];
        change.state = change.before.error ? (change.after.error ? UmcChange::MissingBoth : UmcChange::MissingBefore) :
            change.after.error ? UmcChange::MissingAfter : change.before.encoded != change.after.encoded ? UmcChange::Changed :
            change.before.raw != change.after.raw ? UmcChange::RawOnly : UmcChange::Unchanged;
        ++out.counts[std::size_t(change.state)]; out.fields.push_back(change);
    }
    return out;
}
} }
