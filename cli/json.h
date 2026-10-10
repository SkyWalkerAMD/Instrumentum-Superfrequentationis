// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace octool { namespace cli {
using Fields = std::vector<std::pair<std::string, std::string>>;
std::string quote(const std::string &value);
std::string number(double value);
std::string object(const Fields &fields);
std::string array(const std::vector<std::string> &values);
inline std::string boolean(bool value) { return value ? "true" : "false"; }
} }
