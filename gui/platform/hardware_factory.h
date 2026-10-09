// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../core/hardware.h"

namespace octool {
namespace platform {

// Implemented by the OS adapter selected at build time.
std::unique_ptr<core::HardwareBackend> makeHardwareBackend();

} // namespace platform
} // namespace octool
