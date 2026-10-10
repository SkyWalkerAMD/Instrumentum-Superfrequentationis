// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "core/hardware.h"
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace octool { namespace cli {
using BackendFactory = std::function<std::unique_ptr<core::HardwareBackend>()>;
// Parses and validates the complete command before invoking the factory.
// Exit status: 0 success, 2 usage, 3 operation failure (including partial reads).
int run(const std::vector<std::string> &args, std::ostream &out, const BackendFactory &factory);
} }
