// SPDX-License-Identifier: GPL-2.0-only
#include "linux_inventory.h"
#include "linux_inventory_native.h"
#include <cstring>

namespace octool { namespace platform {
InventorySnapshot linuxInventory(const QString &root) {
    InventorySnapshot out;
    const auto native = linuxInventoryNative(root.toStdString());
    for (const auto &row : native.rows)
        out.rows.push_back({QString::fromStdString(row.group), QString::fromStdString(row.name),
            QString::fromStdString(row.value), QString::fromStdString(row.unit),
            QString::fromStdString(row.source), QString::fromStdString(row.status)});
    for (const auto &spd : native.spd) {
        SpdDevice device; device.path = QString::fromStdString(spd.path);
        if (spd.error) device.error = QString::fromLocal8Bit(std::strerror(-spd.error));
        else if (!spd.bytes.empty()) device.bytes = QByteArray(reinterpret_cast<const char *>(spd.bytes.data()), int(spd.bytes.size()));
        out.spd.push_back(device);
    }
    return out;
}
} }
