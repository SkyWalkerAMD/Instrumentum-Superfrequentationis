// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QString>
#include <QByteArray>
#include <QVector>

namespace octool { namespace platform {
struct InventoryRow { QString group, name, value, unit, source, status; };
struct SpdDevice { QString path; QByteArray bytes; QString error; };
struct InventorySnapshot { QVector<InventoryRow> rows; QVector<SpdDevice> spd; };
// root is injectable for fixture tests; production always uses /sys.
InventorySnapshot linuxInventory(const QString &root = QStringLiteral("/sys"));
} }
