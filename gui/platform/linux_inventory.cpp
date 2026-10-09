// SPDX-License-Identifier: GPL-2.0-only
#include "linux_inventory.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace octool { namespace platform {
namespace {
QString readText(const QString &path, QString &error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { error = f.errorString(); return {}; }
    const auto data = f.read(4097);
    if (data.size() > 4096 || f.error() != QFileDevice::NoError) { error = "Read failed or attribute too long"; return {}; }
    return QString::fromUtf8(data).trimmed();
}
}
InventorySnapshot linuxInventory(const QString &root) {
    InventorySnapshot out;
    const QString dmi = root + "/class/dmi/id/";
    for (const char *key : {"board_vendor", "board_name", "board_version", "bios_vendor", "bios_version", "bios_date", "product_name"}) {
        QString error; const auto value = readText(dmi+key, error);
        out.rows.push_back({"Motherboard / BIOS", QString::fromLatin1(key), value, "", dmi+key, error.isEmpty() ? "Read" : error});
    }
    const QRegularExpression pattern("^(temp|in|curr|power|fan|freq)([0-9]+)_(input|average)$");
    const auto devices = QDir(root + "/class/hwmon").entryList({"hwmon*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto &device : devices) {
        const QString dir = root + "/class/hwmon/" + device + "/";
        QString nameError; QString name = readText(dir+"name", nameError); if (name.isEmpty()) name = device;
        const auto files = QDir(dir).entryList(QDir::Files | QDir::System, QDir::Name);
        for (const auto &file : files) {
            const auto match = pattern.match(file); if (!match.hasMatch()) continue;
            if (out.rows.size() >= 4096) { out.rows.push_back({"Inventory", "Limit", "", "", dir, "Too many sensor attributes"}); return out; }
            const QString kind = match.captured(1), base = kind + match.captured(2);
            QString labelError, error; QString label = readText(dir+base+"_label", labelError);
            if (label.isEmpty()) label = base;
            if (match.captured(3) == "average") label += " (average)";
            const QString text = readText(dir+file, error);
            bool ok = false; const qlonglong raw = text.toLongLong(&ok, 10);
            QString status = error.isEmpty() ? (ok ? "Read" : "Invalid numeric attribute") : error;
            for (const char *suffix : {"_fault", "_alarm"}) {
                const QString path = dir+base+suffix;
                if (!QFileInfo::exists(path)) continue;
                QString flagError; const QString flag = readText(path, flagError);
                if (!flagError.isEmpty()) status += "; " + QString::fromLatin1(suffix+1) + " unavailable";
                else if (flag == "1") status += "; " + QString::fromLatin1(suffix+1);
            }
            const double divisor = kind == "power" ? 1000000.0 : kind == "fan" || kind == "freq" ? 1.0 : 1000.0;
            const QString unit = kind == "temp" ? "C" : kind == "in" ? "V" : kind == "curr" ? "A" : kind == "power" ? "W" : kind == "fan" ? "RPM" : "Hz";
            out.rows.push_back({name, label, ok && error.isEmpty() ? QString::number(double(raw)/divisor, 'g', 12) : "", unit, dir+file, status});
        }
    }
    if (devices.isEmpty()) out.rows.push_back({"Sensors", "hwmon", "", "", root+"/class/hwmon", "No kernel sensor driver exposed"});
    QSet<QString> seen;
    for (const char *driver : {"ee1004", "spd5118"}) {
        const QString dir = root + "/bus/i2c/drivers/" + driver;
        const auto entries = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        const QRegularExpression devicePattern("^[0-9]+-00[0-9a-fA-F]{2}$");
        for (const auto &entry : entries) {
            if (!devicePattern.match(entry).hasMatch()) continue;
            const QString path = dir+"/"+entry+"/eeprom";
            const QString canonical = QFileInfo(path).canonicalFilePath();
            if (!canonical.isEmpty() && seen.contains(canonical)) continue;
            seen.insert(canonical);
            SpdDevice spd; spd.path = path; QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) spd.error = f.errorString();
            else {
                spd.bytes = f.read(4097);
                if (f.error() != QFileDevice::NoError || spd.bytes.size() > 4096) { spd.error = "EEPROM read failed or too large"; spd.bytes.clear(); }
            }
            out.spd.push_back(spd);
            if (out.spd.size() >= 128) return out;
        }
    }
    return out;
}
} }
