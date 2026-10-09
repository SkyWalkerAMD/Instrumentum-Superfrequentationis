// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "system_info.h"
#include <functional>

namespace octool {
namespace platform {
std::string parseLinuxCpuModel(const std::string &cpuinfo);
// Capture/pin/restore this thread with a dynamically sized Linux CPU set.
int onLinuxCpu(unsigned cpu, const std::function<int()> &operation);
} // namespace platform
} // namespace octool
