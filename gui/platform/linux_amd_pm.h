// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "core/amd_pm.h"
namespace octool { namespace platform {
// Root is injectable for filesystem fixtures; the application uses the fixed
// sysfs location. Does not load a driver or issue direct register/SMU writes.
core::AmdPmReader linuxAmdPmReader(const std::string &root = "/sys/kernel/ryzen_smu_drv");
} }
