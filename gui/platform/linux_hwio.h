// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "hardware_factory.h"
#include "../../port/hal/octool_hwio.h"

namespace octool {
namespace platform {

// Takes ownership, including an injected test transport. A null handle stays
// unavailable; it must never trigger a second implicit hwio_open().
std::unique_ptr<core::HardwareBackend> makeLinuxHardwareBackend(hwio_t *handle);

} // namespace platform
} // namespace octool
