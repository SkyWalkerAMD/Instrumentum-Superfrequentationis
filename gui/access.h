// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QString>
#include <QMutex>
#include <memory>
#include "../port/hal/octool_hwio.h"

enum class Space { Msr, Memory, Pci };
struct Request {
    Space space = Space::Msr;
    bool write = false;
    unsigned cpu = 0;
    quint64 address = 0, value = 0;
    unsigned bus = 0, device = 0, function = 0;
    int width = 8;
};
struct Reply { int error = 0; quint64 value = 0; };
bool parseNumber(const QString &text, int base, quint64 maximum, quint64 &value);
QString validate(const Request &request);
QString targetText(const Request &request);
QString hexValue(quint64 value, int width);

class HardwareAccess {
public:
    explicit HardwareAccess(hwio_t *handle = nullptr);
    ~HardwareAccess();
    Reply execute(const Request &request);
    QString backend(Space space) const;
private:
    hwio_t *handle_;
    mutable QMutex mutex_;
};
