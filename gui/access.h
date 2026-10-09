// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QString>
#include <memory>
#include "core/hardware.h"
#include "platform/hardware_factory.h"
#include <functional>

using Space = octool::core::Space;
using Request = octool::core::Request;
using Reply = octool::core::Reply;
using CpuIdReply = octool::core::CpuIdReply;
bool parseNumber(const QString &text, int base, quint64 maximum, quint64 &value);
QString validate(const Request &request);
QString targetText(const Request &request);
QString hexValue(quint64 value, int width);

class HardwareAccess {
public:
    using Authorizer = std::function<octool::platform::BackendConnection(const std::atomic<bool> &)>;
    HardwareAccess();
    explicit HardwareAccess(std::unique_ptr<octool::core::HardwareBackend> backend,
                            Authorizer authorizer = octool::platform::authorizeHardware);
    int authorize(const std::atomic<bool> &cancelled);
    Reply execute(const Request &request);
    CpuIdReply cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf = 0);
    int transaction(const std::function<int(octool::core::HardwareSession &)> &operation,
                    int timeoutMs = 10000, const std::atomic<bool> *cancelled = nullptr);
    QString backend(Space space) const;
private:
    octool::core::HardwareService service_;
    Authorizer authorizer_;
};
