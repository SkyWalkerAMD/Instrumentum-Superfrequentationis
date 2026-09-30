// SPDX-License-Identifier: GPL-2.0-only
#include "access.h"
#include <QMutexLocker>
#include <QRegularExpression>
#include <cerrno>
#include <limits>
#include <sched.h>

CpuIdReply HardwareAccess::cpuid(unsigned cpu, uint32_t leaf, uint32_t subleaf)
{
    QMutexLocker lock(&mutex_);
    CpuIdReply reply;
    if (!handle_) { reply.error = -ENOMEM; return reply; }
    if (hwio_backend_for(handle_, HWIO_FAM_CPU) == HWIO_BE_MODULE) {
        reply.error = hwio_cpuid(handle_, cpu, leaf, subleaf, reply.words);
        return reply;
    }
    // Direct HAL CPUID runs on the calling CPU. Pin only this worker thread,
    // and restore its affinity before returning it to Qt's thread pool.
    if (cpu >= CPU_SETSIZE) { reply.error = -ERANGE; return reply; }
    cpu_set_t previous, selected;
    if (sched_getaffinity(0, sizeof(previous), &previous)) {
        reply.error = -errno; return reply;
    }
    CPU_ZERO(&selected); CPU_SET(cpu, &selected);
    if (sched_setaffinity(0, sizeof(selected), &selected)) {
        reply.error = -errno; return reply;
    }
    reply.error = hwio_cpuid(handle_, cpu, leaf, subleaf, reply.words);
    if (sched_setaffinity(0, sizeof(previous), &previous)) reply.error = -errno;
    return reply;
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
    if (r.width != 1 && r.width != 2 && r.width != 4 && r.width != 8)
        return "Width must be 8, 16, 32 or 64 bits.";
    if (r.space == Space::Msr && (r.width != 8 || r.address > UINT32_MAX))
        return "MSR index must fit 32 bits; the value is 64 bits.";
    if (r.space == Space::Pci && (r.width > 4 || r.bus > 255 || r.device > 31 ||
        r.function > 7 || r.address > 255 || r.address + r.width > 256))
        return "PCI supports domain 0000, bus 00-FF, device 00-1F, function 0-7 and offsets 00-FF.";
    if (r.space != Space::Msr && r.address % quint64(r.width))
        return "Address / offset must be aligned to the selected width.";
    if (r.address > std::numeric_limits<quint64>::max() - quint64(r.width - 1))
        return "Address range overflows 64 bits.";
    if (r.write && r.width < 8 && r.value >= (quint64(1) << (r.width * 8)))
        return "Value does not fit the selected width; it will not be truncated.";
    return {};
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

HardwareAccess::HardwareAccess(hwio_t *handle) : handle_(handle ? handle : hwio_open(nullptr)) {}
HardwareAccess::~HardwareAccess() { hwio_close(handle_); }

QString HardwareAccess::backend(Space space) const
{
    QMutexLocker guard(&mutex_);
    return hwio_backend_str(hwio_backend_for(handle_, space == Space::Msr ? HWIO_FAM_MSR :
                                           space == Space::Memory ? HWIO_FAM_MMIO : HWIO_FAM_PCI));
}

Reply HardwareAccess::execute(const Request &r)
{
    Reply result;
    if (!validate(r).isEmpty()) { result.error = -EINVAL; return result; }
    QMutexLocker guard(&mutex_);
    if (!handle_) { result.error = -ENOMEM; return result; }
    uint64_t value = 0;
    uint32_t pci = 0;
    switch (r.space) {
    case Space::Msr:
        result.error = r.write ? hwio_wrmsr(handle_, r.cpu, uint32_t(r.address), r.value) :
            hwio_rdmsr(handle_, r.cpu, uint32_t(r.address), &value);
        break;
    case Space::Memory:
        result.error = r.write ? hwio_mem_write(handle_, r.address, r.width, r.value) :
            hwio_mem_read(handle_, r.address, r.width, &value);
        break;
    case Space::Pci:
        result.error = r.write ? hwio_pci_write(handle_, r.bus, r.device, r.function,
                                              r.address, r.width, uint32_t(r.value)) :
            hwio_pci_read(handle_, r.bus, r.device, r.function, r.address, r.width, &pci);
        value = pci;
        break;
    }
    if (!result.error) result.value = r.write ? r.value : value;
    return result;
}
