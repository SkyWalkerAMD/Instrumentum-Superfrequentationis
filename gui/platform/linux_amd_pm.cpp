// SPDX-License-Identifier: GPL-2.0-only
#include "linux_amd_pm.h"
#include "linux_inventory_native.h"
#include <cerrno>
namespace octool { namespace platform {
core::AmdPmReader linuxAmdPmReader(const std::string &root) {
    return [root](const std::string &leaf, std::vector<std::uint8_t> &bytes, std::size_t maximum) {
        bytes.clear();
        if (root.empty() || root.find('\0') != std::string::npos ||
            (leaf != "pm_table_version" && leaf != "pm_table_size" && leaf != "pm_table")) return -EINVAL;
        return readBoundedFile(root + '/' + leaf, bytes, maximum);
    };
}
} }
