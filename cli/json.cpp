// SPDX-License-Identifier: GPL-2.0-only
#include "json.h"
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace octool { namespace cli {
std::string quote(const std::string &value) {
    std::string out = "\"";
    const char *hex = "0123456789abcdef";
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto c = static_cast<unsigned char>(value[i]);
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else if (c < 128) out += char(c);
        else {
            // Keep valid UTF-8, replacing malformed filename/sysfs bytes so
            // stdout remains valid JSON even when the OS data is not UTF-8.
            const unsigned n = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
            bool valid = n && i + n <= value.size();
            for (unsigned j = 1; valid && j < n; ++j) {
                const auto next = static_cast<unsigned char>(value[i + j]);
                valid = next >= 0x80 && next <= 0xbf;
                if (j == 1) valid = valid && !(c == 0xe0 && next < 0xa0) && !(c == 0xed && next >= 0xa0) &&
                    !(c == 0xf0 && next < 0x90) && !(c == 0xf4 && next >= 0x90);
            }
            if (valid) { out.append(value, i, n); i += n - 1; }
            else out += "\\ufffd";
        }
    }
    return out + '"';
}
std::string number(double value) {
    if (!std::isfinite(value)) return "null";
    std::ostringstream out; out.imbue(std::locale::classic());
    out << std::setprecision(17) << value; return out.str();
}
std::string object(const Fields &fields) {
    std::string out = "{";
    for (const auto &field : fields) {
        if (out.size() > 1) out += ',';
        out += quote(field.first) + ':' + field.second;
    }
    return out + '}';
}
std::string array(const std::vector<std::string> &values) {
    std::string out = "[";
    for (const auto &value : values) { if (out.size() > 1) out += ','; out += value; }
    return out + ']';
}
} }
