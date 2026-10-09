// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <string>
#include <vector>

namespace octool {
namespace platform {
struct SystemInfo {
    std::string cpuModel;
    long onlineCpus = 0;
    std::vector<unsigned> allowedCpus;
    int affinityError = 0;
    bool moduleLoaded = false, helperInstalled = false;
};
SystemInfo systemInfo();
} // namespace platform
} // namespace octool
