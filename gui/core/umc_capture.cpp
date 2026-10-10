// SPDX-License-Identifier: GPL-2.0-only
#include "umc_capture.h"
#include <cerrno>
#include <map>
#include <stdexcept>
#include <utility>

namespace octool { namespace core {
namespace {
void check(bool valid, const char *message) {
    if (!valid) throw std::invalid_argument(message);
}
struct Json {
    enum Kind { Null, Boolean, Number, String, Array, Object } kind = Null;
    std::string text;
    std::vector<Json> items;
    std::map<std::string, Json> members;
    const Json &at(const char *key) const {
        const auto it = members.find(key);
        check(kind == Object && it != members.end(), "Missing snapshot member");
        return it->second;
    }
    bool has(const char *key) const { return members.count(key) != 0; }
};
unsigned hexDigit(char c) {
    return c >= '0' && c <= '9' ? unsigned(c - '0') : c >= 'a' && c <= 'f' ? unsigned(c - 'a' + 10) :
        c >= 'A' && c <= 'F' ? unsigned(c - 'A' + 10) : 16;
}
void utf8(std::string &out, unsigned cp) {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xc0 | (cp >> 6)); out += char(0x80 | (cp & 63)); }
    else if (cp < 0x10000) {
        out += char(0xe0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 63)); out += char(0x80 | (cp & 63));
    } else {
        out += char(0xf0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 63));
        out += char(0x80 | ((cp >> 6) & 63)); out += char(0x80 | (cp & 63));
    }
}
class Parser {
    const std::string &input;
    std::size_t pos = 0, nodes = 0;
    char peek() const { return pos < input.size() ? input[pos] : '\0'; }
    char take() { check(pos < input.size(), "Truncated JSON"); return input[pos++]; }
    void space() { while (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n') ++pos; }
    bool digit() const { return peek() >= '0' && peek() <= '9'; }
    unsigned codeUnit() {
        unsigned n = 0;
        for (unsigned i = 0; i < 4; ++i) { const auto d = hexDigit(take()); check(d < 16, "Invalid Unicode escape"); n = (n << 4) | d; }
        return n;
    }
    std::string string() {
        check(take() == '"', "Expected JSON string"); std::string out;
        for (;;) {
            const auto c = static_cast<unsigned char>(take());
            if (c == '"') return out;
            check(c >= 0x20, "Unescaped control character");
            if (c == '\\') {
                const char e = take();
                switch (e) {
                case '"': case '\\': case '/': out += e; break;
                case 'b': out += '\b'; break; case 'f': out += '\f'; break;
                case 'n': out += '\n'; break; case 'r': out += '\r'; break; case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = codeUnit();
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        check(take() == '\\' && take() == 'u', "Missing low surrogate");
                        const auto low = codeUnit(); check(low >= 0xdc00 && low <= 0xdfff, "Invalid low surrogate");
                        cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                    } else check(cp < 0xdc00 || cp > 0xdfff, "Unpaired low surrogate");
                    utf8(out, cp); break;
                }
                default: check(false, "Invalid string escape");
                }
            } else if (c < 0x80) out += char(c);
            else {
                unsigned extra = 0, cp = 0, minimum = 0;
                if (c >= 0xc2 && c <= 0xdf) { extra = 1; cp = c & 31; minimum = 0x80; }
                else if (c >= 0xe0 && c <= 0xef) { extra = 2; cp = c & 15; minimum = 0x800; }
                else if (c >= 0xf0 && c <= 0xf4) { extra = 3; cp = c & 7; minimum = 0x10000; }
                else check(false, "Invalid UTF-8");
                for (unsigned i = 0; i < extra; ++i) {
                    const auto next = static_cast<unsigned char>(take());
                    check((next & 0xc0) == 0x80, "Invalid UTF-8 continuation"); cp = (cp << 6) | (next & 63);
                }
                check(cp >= minimum && cp <= 0x10ffff && (cp < 0xd800 || cp > 0xdfff), "Invalid UTF-8 code point");
                utf8(out, cp);
            }
        }
    }
    Json value(unsigned depth) {
        check(depth <= 16 && ++nodes <= 8192, "JSON nesting or value limit exceeded"); space(); Json v;
        const char c = peek();
        if (c == '{' || c == '[') {
            ++pos; v.kind = c == '{' ? Json::Object : Json::Array;
            const char close = c == '{' ? '}' : ']'; space();
            if (peek() == close) { ++pos; return v; }
            for (;;) {
                space();
                if (c == '{') {
                    const auto key = string(); space(); check(take() == ':', "Expected colon");
                    auto child = value(depth + 1);
                    check(v.members.emplace(key, std::move(child)).second, "Duplicate JSON member");
                } else v.items.push_back(value(depth + 1));
                space(); const char end = take(); if (end == close) return v;
                check(end == ',', "Expected comma or closing bracket");
            }
        }
        if (c == '"') { v.kind = Json::String; v.text = string(); return v; }
        if (c == '-' || digit()) {
            const auto start = pos; v.kind = Json::Number;
            if (peek() == '-') ++pos;
            check(digit(), "Invalid JSON number");
            if (peek() == '0') ++pos; else while (digit()) ++pos;
            if (peek() == '.') { ++pos; check(digit(), "Missing fraction"); while (digit()) ++pos; }
            if (peek() == 'e' || peek() == 'E') {
                ++pos; if (peek() == '+' || peek() == '-') ++pos;
                check(digit(), "Missing exponent"); while (digit()) ++pos;
            }
            v.text = input.substr(start, pos - start); return v;
        }
        for (const auto *literal : {"true", "false", "null"}) {
            const std::string word(literal);
            if (input.compare(pos, word.size(), word) == 0) {
                pos += word.size(); v.kind = word == "null" ? Json::Null : Json::Boolean; v.text = word; return v;
            }
        }
        check(false, "Invalid JSON value"); return v;
    }
public:
    explicit Parser(const std::string &text) : input(text) {}
    Json parse() {
        check(!input.empty() && input.size() <= 65536, "Snapshot must contain 1..65536 bytes");
        if (input.compare(0, 3, "\xef\xbb\xbf") == 0) pos = 3;
        auto out = value(0); space(); check(pos == input.size(), "Trailing data after JSON"); return out;
    }
};
// Exact decimal arithmetic: never round a fractional or oversized input to a
// valid register word (e.g. 4294967295.00000001 must remain invalid).
unsigned uint(const Json &v, unsigned maximum) {
    check(v.kind == Json::Number && !v.text.empty() && v.text[0] != '-', "Expected unsigned integer");
    std::string digits; int fractional = 0, exponent = 0; bool fraction = false;
    std::size_t i = 0;
    for (; i < v.text.size() && v.text[i] != 'e' && v.text[i] != 'E'; ++i) {
        if (v.text[i] == '.') fraction = true;
        else { digits += v.text[i]; if (fraction) ++fractional; }
    }
    if (i < v.text.size()) {
        ++i; bool negative = false;
        if (v.text[i] == '+' || v.text[i] == '-') { negative = v.text[i] == '-'; ++i; }
        for (; i < v.text.size(); ++i) {
            check(exponent <= 6553, "Integer exponent out of range"); exponent = exponent * 10 + (v.text[i] - '0');
        }
        if (negative) exponent = -exponent;
    }
    const auto first = digits.find_first_not_of('0');
    if (first == std::string::npos) return 0;
    digits.erase(0, first); int scale = exponent - fractional;
    while (scale < 0 && !digits.empty() && digits.back() == '0') { digits.pop_back(); ++scale; }
    check(scale >= 0 && scale <= 10 && digits.size() + std::size_t(scale) <= 10, "Integer out of range or fractional");
    digits.append(std::size_t(scale), '0'); unsigned n = 0;
    for (char c : digits) {
        const unsigned d = unsigned(c - '0');
        check(d <= maximum && n <= (maximum - d) / 10, "Integer out of range"); n = n * 10 + d;
    }
    return n;
}
unsigned word(const Json &v) {
    check(v.kind == Json::String && v.text.size() == 10 && v.text.compare(0, 2, "0x") == 0, "Expected 32-bit hex word");
    unsigned out = 0;
    for (std::size_t i = 2; i < 10; ++i) {
        const auto d = hexDigit(v.text[i]); check(d < 16, "Invalid hex word"); out = (out << 4) | d;
    }
    return out;
}
bool equals(const Json &v, Json::Kind kind, const char *text) { return v.kind == kind && v.text == text; }
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
