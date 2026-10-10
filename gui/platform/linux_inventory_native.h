// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace octool { namespace platform {
struct NativeInventoryRow {
    std::string group, name, value, unit, source, status;
    int error = 0;
};
struct NativeSpdDevice {
    std::string path;
    std::vector<std::uint8_t> bytes;
    int error = 0;
};
struct NativeInventorySnapshot {
    std::vector<NativeInventoryRow> rows;
    std::vector<NativeSpdDevice> spd;
};
// Shared by the Qt adapter and CLI. Only reads existing kernel attributes and
// EEPROMs of bound ee1004/spd5118 drivers; never scans or creates I2C devices.
NativeInventorySnapshot linuxInventoryNative(const std::string &root = "/sys");
int readBoundedFile(const std::string &path, std::vector<std::uint8_t> &bytes);
} }
