// SPDX-License-Identifier: GPL-2.0-only
#include "access.h"
#include "platform/hardware_factory.h"
#include <QRegularExpression>
#include <utility>

CpuIdReply HardwareAccess::cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf)
{
    return service_.cpuid(cpu, leaf, subleaf);
}

bool parseNumber(const QString &text, int base, quint64 maximum, quint64 &value)
{
    QString digits = text.trimmed();
    if (base == 16 && digits.startsWith("0x", Qt::CaseInsensitive)) digits.remove(0, 2);
    const QRegularExpression allowed(base == 16 ? "^[0-9a-fA-F]+$" : "^[0-9]+$");
    if (!allowed.match(digits).hasMatch()) return false;
    bool ok = false;
    quint64 parsed = digits.toULongLong(&ok, base);
    if (!ok || parsed > maximum) return false;
    value = parsed;
    return true;
}

QString validate(const Request &r)
{
    using Error = octool::core::ValidationError;
    switch (octool::core::validateRequest(r)) {
    case Error::None: return {};
    case Error::InvalidSpace: return "Unknown register address space.";
    case Error::InvalidWidth:
        return "Width must be 8, 16, 32 or 64 bits.";
    case Error::InvalidMsr:
        return "MSR index must fit 32 bits; the value is 64 bits.";
    case Error::InvalidPci:
        return "PCI supports domain 0000, bus 00-FF, device 00-1F, function 0-7 and offsets 00-FF.";
    case Error::UnalignedAddress:
        return "Address / offset must be aligned to the selected width.";
    case Error::AddressOverflow:
        return "Address range overflows 64 bits.";
    case Error::ValueOverflow:
        return "Value does not fit the selected width; it will not be truncated.";
    }
    return "Invalid register request.";
}

QString hexValue(quint64 value, int width)
{ return "0x" + QString::number(value, 16).toUpper().rightJustified(width * 2, '0'); }

QString targetText(const Request &r)
{
    if (r.space == Space::Msr)
        return QString("MSR %1 on logical CPU %2").arg(hexValue(r.address, 4)).arg(r.cpu);
    if (r.space == Space::Memory) return "MMIO " + hexValue(r.address, 8);
    return QString("PCI 0000:%1:%2.%3 + %4")
        .arg(r.bus, 2, 16, QChar('0')).arg(r.device, 2, 16, QChar('0'))
        .arg(r.function).arg(hexValue(r.address, 1));
}

HardwareAccess::HardwareAccess() : service_(octool::platform::makeHardwareBackend()) {}
HardwareAccess::HardwareAccess(std::unique_ptr<octool::core::HardwareBackend> backend)
    : service_(std::move(backend)) {}

QString HardwareAccess::backend(Space space) const
{
    switch (service_.backend(space)) {
    case octool::core::Backend::Module: return "module";
    case octool::core::Backend::Direct: return "direct";
    default: return "none";
    }
}

Reply HardwareAccess::execute(const Request &r)
{
    return service_.execute(r);
}
