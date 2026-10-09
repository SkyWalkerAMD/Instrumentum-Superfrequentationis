// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../core/hardware.h"
#include <atomic>

namespace octool {
namespace platform {

// Implemented by the OS adapter selected at build time.
std::unique_ptr<core::HardwareBackend> makeHardwareBackend();
struct BackendConnection {
    std::unique_ptr<core::HardwareBackend> backend;
    int error = 0;
};
BackendConnection authorizeHardware(const std::atomic<bool> &cancelled);

} // namespace platform
} // namespace octool
