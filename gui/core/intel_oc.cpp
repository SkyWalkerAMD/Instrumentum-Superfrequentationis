// SPDX-License-Identifier: GPL-2.0-only
#include "intel_oc.h"
#include "intel_tvb.h"
#include "intel_avx.h"
#include "intel_bclk_voltage.h"
#include "intel_iccmax.h"
#include <cerrno>
#include <chrono>
#include <cmath>
#include <thread>

namespace octool { namespace core {
namespace {
const std::uint64_t busy = UINT64_C(1) << 63;
const std::uint64_t ocLock = UINT64_C(1) << 20;
bool validDomain(IntelOcDomain domain) {
    return domain == IntelOcDomain::Core || domain == IntelOcDomain::Cache;
}
int waitReady(HardwareSession &s, unsigned cpu, std::uint64_t &value) {
    // Both a poll bound and the enclosing transaction deadline apply.
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        const auto r = s.execute(msrRequest(cpu, 0x150));
        if (r.error) return r.error;
        if (!(r.value & busy)) { value = r.value; return 0; }
        if (const int error = s.checkpoint()) return error;
        if (attempt != 99) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return -ETIMEDOUT;
}
IntelOcResponse mailboxCommand(HardwareSession &s, unsigned cpu, unsigned command,
                               std::uint32_t data, unsigned parameter1, unsigned parameter2) {
    IntelOcResponse out;
    std::uint64_t value = 0;
    if ((out.error = waitReady(s, cpu, value))) return out;
    Request request = msrRequest(cpu, 0x150);
    request.write = true;
    request.value = busy | (std::uint64_t(parameter1) << 40) |
                    (std::uint64_t(parameter2) << 48) |
                    (std::uint64_t(command) << 32) | data;
    if ((out.error = s.checkpoint())) return out;
    out.commandAttempted = true;
    if ((out.error = s.execute(request).error)) return out;
    if ((out.error = waitReady(s, cpu, value))) return out;
    if ((out.error = s.checkpoint())) return out;
    // Decode the very sample which observed completion, not an extra read
    // that could observe a different command or return to the busy state.
    out.completed = true;
    out.firmwareStatus = unsigned((value >> 32) & 255);
    if (out.firmwareStatus) { out.error = -EIO; return out; }
    out.data = std::uint32_t(value);
    return out;
}
IntelOcResponse exchangeCommand(HardwareSession &s, unsigned cpu, IntelOcDomain domain,
                                unsigned command, std::uint32_t data, unsigned point = 0) {
    return mailboxCommand(s, cpu, command, data, unsigned(domain), point);
}
IntelOcResponse exchange(HardwareSession &s, unsigned cpu, IntelOcDomain domain,
                         bool write, std::uint32_t data, unsigned point = 0) {
    return exchangeCommand(s, cpu, domain, write ? 0x11 : 0x10, data, point);
}
}
bool hasIntelOcProfile(const CpuIdentity &id) {
    // Raptor Lake-S client path. Never infer this from the board/product name.
    return !id.error && id.intel && id.family == 6 && id.model == 0xb7;
}
bool hasIntelIccMaxProfile(const CpuIdentity &id) {
    if (id.error || !id.intel || id.family != 6) return false;
    if (id.model == 0x97)
        return id.stepping == 0 || id.stepping == 1 || id.stepping == 2 || id.stepping == 4 || id.stepping == 5;
    return id.model == 0x9a && id.stepping >= 1 && id.stepping <= 4;
}
double intelIccMaxAmps(std::uint32_t data) { return double(data & 0x7ffu) / 4.0; }
bool intelIccMaxModeBit31(std::uint32_t data) { return (data & UINT32_C(0x80000000)) != 0; }
namespace {
bool iccMaxValid(const IntelIccMaxSnapshot &v) {
    return v.valid && !v.error && hasIntelIccMaxProfile(v.identity) &&
        v.topology.commandAttempted && v.topology.completed && !v.topology.error && !v.topology.firmwareStatus &&
        !(v.topology.data & 0x1000u) && v.address == ((v.topology.data >> 8) & 15) &&
        v.response.commandAttempted && v.response.completed && !v.response.error && !v.response.firmwareStatus;
}
int iccMaxContext(HardwareSession &s, unsigned cpu, CpuIdentity &identity, std::uint64_t &flexRatio) {
    identity = identifyCpu(s, cpu);
    if (identity.error) return identity.error;
    if (!hasIntelIccMaxProfile(identity)) return -ENOTSUP;
    const auto features = s.cpuid(cpu, 1);
    if (features.error) return features.error;
    if (features.words[0] != identity.signature) return -EAGAIN;
    if (!(features.words[3] & (1u << 5)) || (features.words[2] & (1u << 31))) return -ENOTSUP;
    const auto flex = s.execute(msrRequest(cpu, 0x194));
    if (flex.error) return flex.error;
    flexRatio = flex.value;
    return 0;
}
bool sameIccMaxContext(const IntelIccMaxSnapshot &a, const IntelIccMaxSnapshot &b) {
    return a.identity.signature == b.identity.signature && a.flexRatio == b.flexRatio &&
        a.topology.data == b.topology.data && a.address == b.address;
}
}
bool intelIccMaxEditable(const IntelIccMaxSnapshot &v) {
    return iccMaxValid(v) && !(v.flexRatio & ocLock) && !intelIccMaxModeBit31(v.response.data) &&
        (v.response.data & 0x7ffu) >= 4;
}
int encodeIntelIccMax(double amps, std::uint32_t previous, std::uint32_t &encoded) {
    if (!std::isfinite(amps) || amps < 1 || amps > 511.75 || std::floor(amps * 4) != amps * 4) return -ERANGE;
    encoded = (previous & ~UINT32_C(0x7ff)) | std::uint32_t(amps * 4);
    return 0;
}
IntelIccMaxSnapshot readIntelIccMax(HardwareSession &s, unsigned cpu) {
    IntelIccMaxSnapshot out; out.cpu = cpu;
    if ((out.error = iccMaxContext(s, cpu, out.identity, out.flexRatio))) return out;
    out.topology = mailboxCommand(s, cpu, 4, 0, 0, 0);
    if ((out.error = out.topology.error)) return out;
    // Active-low SVID flag. Do not use the other rail descriptors as fallbacks.
    if (out.topology.data & 0x1000u) { out.error = -ENOTSUP; return out; }
    out.address = (out.topology.data >> 8) & 15;
    out.response = mailboxCommand(s, cpu, 0x16, 0, out.address, 0);
    if ((out.error = out.response.error)) return out;
    const auto topologyAfter = mailboxCommand(s, cpu, 4, 0, 0, 0);
    if ((out.error = topologyAfter.error)) return out;
    if (topologyAfter.data != out.topology.data) { out.error = -EAGAIN; return out; }
    CpuIdentity after; std::uint64_t flex = 0;
    if ((out.error = iccMaxContext(s, cpu, after, flex))) return out;
    if (after.signature != out.identity.signature || flex != out.flexRatio) { out.error = -EAGAIN; return out; }
    if ((out.error = s.checkpoint())) return out;
    out.valid = true; return out;
}
IntelIccMaxUpdate applyIntelIccMax(HardwareSession &s, const IntelIccMaxSnapshot &old, double amps) {
    IntelIccMaxUpdate out;
    if (!iccMaxValid(old)) { out.error = -EINVAL; return out; }
    if (!intelIccMaxEditable(old)) { out.error = -EPERM; return out; }
    if ((out.error = encodeIntelIccMax(amps, old.response.data, out.expected))) return out;
    out.expectedValid = true;
    const auto current = readIntelIccMax(s, old.cpu);
    if ((out.error = current.error)) return out;
    if (!sameIccMaxContext(current, old) || current.response.data != old.response.data) { out.error = -EAGAIN; return out; }
    if (!intelIccMaxEditable(current)) { out.error = -EPERM; return out; }
    if (out.expected == current.response.data) out.after = current;
    else {
        out.response = mailboxCommand(s, old.cpu, 0x17, out.expected, current.address, 0);
        out.writeAttempted = out.response.commandAttempted;
        if ((out.error = out.response.error)) return out;
        out.after = readIntelIccMax(s, old.cpu);
    }
    if ((out.error = out.after.error)) return out;
    if (!sameIccMaxContext(out.after, old) || out.after.response.data != out.expected) { out.error = -EIO; return out; }
    if ((out.error = s.checkpoint())) return out;
    out.verified = true; out.unchanged = !out.writeAttempted; return out;
}
double intelOcOffsetMillivolts(std::uint32_t data) {
    int code = int(data >> 21);
    if (code & 1024) code -= 2048;
    return double(code) * (1000.0 / 1024.0);
}
double intelOcTargetMillivolts(std::uint32_t data) {
    return double((data >> 8) & 4095) * (1000.0 / 1024.0);
}
IntelOcVoltageMode intelOcVoltageMode(std::uint32_t data) {
    return (data & (1u << 20)) ? IntelOcVoltageMode::Override : IntelOcVoltageMode::Adaptive;
}
int encodeIntelOcVoltage(unsigned millivolts, IntelOcVoltageMode mode,
                         std::uint32_t previous, std::uint32_t &encoded) {
    if (mode != IntelOcVoltageMode::Adaptive && mode != IntelOcVoltageMode::Override) return -EINVAL;
    if (!millivolts || millivolts > 2000) return -ERANGE;
    const unsigned code = (millivolts * 1024u + 500u) / 1000u;
    encoded = (previous & UINT32_C(0xffe000ff)) | (code << 8) | (unsigned(mode) << 20);
    return 0;
}
int encodeIntelOcOffset(double millivolts, std::uint32_t previous, std::uint32_t &encoded) {
    // Bounds are representation limits, not recommended operating voltages.
    if (!std::isfinite(millivolts) || millivolts < -1000.0 ||
        millivolts > 1023.0 * (1000.0 / 1024.0)) return -ERANGE;
    const int code = int(std::round(millivolts * (1024.0 / 1000.0)));
    encoded = (previous & UINT32_C(0x1fffff)) | ((std::uint32_t(code) & 2047) << 21);
    return 0;
}
int encodeIntelOcRatio(unsigned ratio, std::uint32_t previous, std::uint32_t &encoded) {
    if (!ratio || ratio > 85) return -ERANGE;
    encoded = (previous & UINT32_C(0xffffff00)) | ratio;
    return 0;
}
IntelOcSnapshot readIntelOc(HardwareSession &s, unsigned cpu, IntelOcDomain domain) {
    IntelOcSnapshot out; out.cpu = cpu; out.domain = domain;
    if (!validDomain(domain)) { out.error = -EINVAL; return out; }
    out.identity = identifyCpu(s, cpu);
    if ((out.error = out.identity.error)) return out;
    if (!hasIntelOcProfile(out.identity)) { out.error = -ENOTSUP; return out; }
    const auto features = s.cpuid(cpu, 1);
    if ((out.error = features.error)) return out;
    if (!(features.words[3] & (1u << 5))) { out.error = -ENOTSUP; return out; }
    const auto flex = s.execute(msrRequest(cpu, 0x194));
    if ((out.error = flex.error)) return out;
    out.flexRatio = flex.value; out.locked = (flex.value & ocLock) != 0;
    out.response = exchange(s, cpu, domain, false, 0);
    if ((out.error = out.response.error)) return out;
    out.valid = true;
    return out;
}
IntelVfSnapshot readIntelVf(HardwareSession &s, unsigned cpu, IntelOcDomain domain, unsigned point) {
    IntelVfSnapshot out; out.cpu = cpu; out.domain = domain; out.selectedPoint = point;
    if (!validDomain(domain) || point > 15) { out.error = -EINVAL; return out; }
    out.identity = identifyCpu(s, cpu);
    if ((out.error = out.identity.error)) return out;
    if (!hasIntelOcProfile(out.identity)) { out.error = -ENOTSUP; return out; }
    const auto features = s.cpuid(cpu, 1);
    if ((out.error = features.error)) return out;
    if (!(features.words[3] & (1u << 5))) { out.error = -ENOTSUP; return out; }
    const unsigned first = point ? point : 1, last = point ? point : 15;
    for (unsigned index = first; index <= last; ++index) {
        IntelVfPoint row; row.point = index;
        row.response = exchange(s, cpu, domain, false, 0, index);
        out.points.push_back(row);
        if (row.response.error) {
            if (!out.error) out.error = row.response.error;
            // A firmware rejection is local to this point. Preserve its
            // status and inspect the next candidate; never invent a point
            // count from a generic error or silently discard later points.
            if (!row.response.completed) { out.error = row.response.error; return out; }
        }
    }
    out.scanCompleted = true;
    return out;
}
namespace {
bool responseValid(const IntelOcResponse &r) {
    return !r.error && r.completed && !r.firmwareStatus;
}
bool vfSnapshotValid(const IntelVfEditSnapshot &s) {
    return !s.error && s.valid && s.point >= 1 && s.point <= 15 && validDomain(s.domain) &&
        hasIntelOcProfile(s.identity) && responseValid(s.control) &&
        responseValid(s.legacy) && responseValid(s.value);
}
bool sameVfContext(const IntelVfEditSnapshot &a, const IntelVfEditSnapshot &b) {
    return a.identity.signature == b.identity.signature && a.flexRatio == b.flexRatio &&
        a.control.data == b.control.data && a.legacy.data == b.legacy.data;
}
}
bool intelVfDomainDefault(const IntelVfEditSnapshot &s) {
    // Conservative profile policy: adaptive, zero target, zero domain offset.
    // Do not silently clear another voltage setting to activate point editing.
    return (s.legacy.data & UINT32_C(0xffffff00)) == 0;
}
bool intelVfEditable(const IntelVfEditSnapshot &s) {
    return vfSnapshotValid(s) && !(s.flexRatio & ocLock) && intelVfDomainDefault(s) &&
        (s.domain != IntelOcDomain::Core || !(s.control.data & 8));
}
IntelVfEditSnapshot readIntelVfEdit(HardwareSession &s, unsigned cpu, IntelOcDomain domain, unsigned point) {
    IntelVfEditSnapshot out; out.cpu = cpu; out.domain = domain; out.point = point;
    if (!validDomain(domain) || !point || point > 15) { out.error = -EINVAL; return out; }
    out.identity = identifyCpu(s, cpu);
    if ((out.error = out.identity.error)) return out;
    if (!hasIntelOcProfile(out.identity)) { out.error = -ENOTSUP; return out; }
    const auto features = s.cpuid(cpu, 1);
    if ((out.error = features.error)) return out;
    if (!(features.words[3] & (1u << 5))) { out.error = -ENOTSUP; return out; }
    const auto flex = s.execute(msrRequest(cpu, 0x194));
    if ((out.error = flex.error)) return out;
    out.flexRatio = flex.value;
    out.control = exchangeCommand(s, cpu, IntelOcDomain::Core, 0x14, 0);
    if ((out.error = out.control.error)) return out;
    out.legacy = exchange(s, cpu, domain, false, 0);
    if ((out.error = out.legacy.error)) return out;
    out.value = exchange(s, cpu, domain, false, 0, point);
    if ((out.error = out.value.error)) return out;
    if ((out.error = s.checkpoint())) return out;
    out.valid = true; return out;
}
IntelVfUpdate applyIntelVfOffset(HardwareSession &s, const IntelVfEditSnapshot &old, double millivolts) {
    IntelVfUpdate out;
    if (!vfSnapshotValid(old)) { out.error = -EINVAL; return out; }
    if ((out.error = encodeIntelOcOffset(millivolts, old.value.data, out.expected))) return out;
    out.submitted = out.expected & UINT32_C(0xffe00000);
    if (!intelVfEditable(old)) { out.error = -EPERM; return out; }
    out.stage = IntelOcStage::Preflight;
    const auto fresh = readIntelVfEdit(s, old.cpu, old.domain, old.point);
    if ((out.error = fresh.error)) return out;
    if (fresh.identity.signature != old.identity.signature) { out.error = -ENODEV; return out; }
    if (!intelVfEditable(fresh)) { out.error = -EPERM; return out; }
    out.stage = IntelOcStage::Compare;
    if (!sameVfContext(old, fresh) || old.value.data != fresh.value.data) { out.error = -EAGAIN; return out; }
    if (out.expected == fresh.value.data) out.readback = fresh;
    else {
        out.stage = IntelOcStage::Write;
        out.response = exchange(s, old.cpu, old.domain, true, out.submitted, old.point);
        out.writeAttempted = out.response.commandAttempted;
        if ((out.error = out.response.error)) return out;
        out.stage = IntelOcStage::Verify;
        out.readback = readIntelVfEdit(s, old.cpu, old.domain, old.point);
    }
    if ((out.error = out.readback.error)) return out;
    if (out.readback.identity.signature != old.identity.signature) { out.error = -ENODEV; return out; }
    if (!sameVfContext(old, out.readback) || !intelVfEditable(out.readback)) { out.error = -EAGAIN; return out; }
    if (out.readback.value.data != out.expected) { out.error = -EIO; return out; }
    if ((out.error = s.checkpoint())) return out;
    out.verified = true; out.unchanged = !out.writeAttempted; out.stage = IntelOcStage::Complete;
    return out;
}
namespace {
IntelOcUpdate applyEncoded(HardwareSession &s, const IntelOcSnapshot &old,
                          int encodingError, std::uint32_t encoded) {
    IntelOcUpdate out;
    if (old.error || !old.valid || !old.response.completed || old.response.error ||
        old.response.firmwareStatus || !hasIntelOcProfile(old.identity) || !validDomain(old.domain)) {
        out.error = -EINVAL; return out;
    }
    if ((out.error = encodingError)) return out;
    out.submitted = encoded;
    if (old.locked) { out.error = -EPERM; return out; }
    out.stage = IntelOcStage::Preflight;
    const auto fresh = readIntelOc(s, old.cpu, old.domain);
    out.response = fresh.response;
    if ((out.error = fresh.error)) return out;
    if (fresh.identity.signature != old.identity.signature) { out.error = -ENODEV; return out; }
    if (fresh.locked) { out.error = -EPERM; return out; }
    out.stage = IntelOcStage::Compare;
    if (fresh.response.data != old.response.data) { out.error = -EAGAIN; return out; }
    if (out.submitted == fresh.response.data) {
        out.readback = fresh; out.verified = true; out.stage = IntelOcStage::Complete; return out;
    }
    out.stage = IntelOcStage::Write;
    out.response = exchange(s, old.cpu, old.domain, true, out.submitted);
    out.writeAttempted = out.response.commandAttempted;
    if ((out.error = out.response.error)) return out;
    out.stage = IntelOcStage::Verify;
    out.readback = readIntelOc(s, old.cpu, old.domain);
    out.response = out.readback.response;
    if ((out.error = out.readback.error)) return out;
    if (out.readback.identity.signature != old.identity.signature) { out.error = -ENODEV; return out; }
    if (out.readback.response.data != out.submitted) { out.error = -EIO; return out; }
    out.verified = true; out.stage = IntelOcStage::Complete;
    return out;
}
}
IntelOcUpdate applyIntelOcOffset(HardwareSession &s, const IntelOcSnapshot &old, double millivolts) {
    std::uint32_t encoded = 0;
    const int error = encodeIntelOcOffset(millivolts, old.response.data, encoded);
    return applyEncoded(s, old, error, encoded);
}
IntelOcUpdate applyIntelOcRatio(HardwareSession &s, const IntelOcSnapshot &old, unsigned ratio) {
    std::uint32_t encoded = 0;
    const int error = encodeIntelOcRatio(ratio, old.response.data, encoded);
    return applyEncoded(s, old, error, encoded);
}
IntelOcUpdate applyIntelOcVoltage(HardwareSession &s, const IntelOcSnapshot &old,
                                 unsigned millivolts, IntelOcVoltageMode mode) {
    std::uint32_t encoded = 0;
    const int error = encodeIntelOcVoltage(millivolts, mode, old.response.data, encoded);
    return applyEncoded(s, old, error, encoded);
}
namespace {
bool tvbSnapshotValid(const IntelTvbSnapshot &s) {
    return !s.error && s.valid && hasIntelOcProfile(s.identity) &&
        responseValid(s.response) && s.response.commandAttempted;
}
bool sameTvbContext(const IntelTvbSnapshot &a, const IntelTvbSnapshot &b) {
    return a.identity.signature == b.identity.signature && a.flexRatio == b.flexRatio;
}
}
bool intelTvbEditable(const IntelTvbSnapshot &s) {
    return tvbSnapshotValid(s) && !(s.flexRatio & ocLock);
}
bool intelTvbRatioClipping(std::uint32_t data) { return !(data & 4u); }
bool intelTvbVoltageOptimization(std::uint32_t data) { return !(data & 8u); }
bool intelRingDownBin(std::uint32_t data) { return !(data & 2u); }
int encodeIntelTvb(IntelTvbField field, unsigned enabled, std::uint32_t previous, std::uint32_t &encoded) {
    std::uint32_t mask = 0;
    switch (field) {
    case IntelTvbField::RatioClipping: mask = 4u; break;
    case IntelTvbField::VoltageOptimization: mask = 8u; break;
    case IntelTvbField::RingDownBin: mask = 2u; break;
    default: return -EINVAL;
    }
    if (enabled > 1) return -ERANGE;
    encoded = (previous & ~mask) | (enabled ? 0u : mask);
    return 0;
}
IntelTvbSnapshot readIntelTvb(HardwareSession &s, unsigned cpu) {
    IntelTvbSnapshot out; out.cpu = cpu;
    out.identity = identifyCpu(s, cpu);
    if ((out.error = out.identity.error)) return out;
    if (!hasIntelOcProfile(out.identity)) { out.error = -ENOTSUP; return out; }
    const auto features = s.cpuid(cpu, 1);
    if ((out.error = features.error)) return out;
    if (!(features.words[3] & (1u << 5)) || (features.words[2] & (1u << 31))) {
        out.error = -ENOTSUP; return out;
    }
    const auto flex = s.execute(msrRequest(cpu, 0x194));
    if ((out.error = flex.error)) return out;
    out.flexRatio = flex.value;
    out.response = exchangeCommand(s, cpu, IntelOcDomain::Core, 0x18, 0);
    if ((out.error = out.response.error)) return out;
    if ((out.error = s.checkpoint())) return out;
    out.valid = true;
    return out;
}
IntelTvbUpdate applyIntelTvb(HardwareSession &s, const IntelTvbSnapshot &old, IntelTvbField field, unsigned enabled) {
    IntelTvbUpdate out;
    if (!tvbSnapshotValid(old)) { out.error = -EINVAL; return out; }
    if ((out.error = encodeIntelTvb(field, enabled, old.response.data, out.expected))) return out;
    if (!intelTvbEditable(old)) { out.error = -EPERM; return out; }
    out.stage = IntelOcStage::Preflight;
    const auto fresh = readIntelTvb(s, old.cpu);
    out.response = fresh.response;
    if ((out.error = fresh.error)) return out;
    if (fresh.identity.signature != old.identity.signature) { out.error = -ENODEV; return out; }
    if (!intelTvbEditable(fresh)) { out.error = -EPERM; return out; }
    out.stage = IntelOcStage::Compare;
    if (!sameTvbContext(old, fresh) || old.response.data != fresh.response.data) { out.error = -EAGAIN; return out; }
    if (out.expected == fresh.response.data) out.readback = fresh;
    else {
        out.stage = IntelOcStage::Write;
        out.response = exchangeCommand(s, old.cpu, IntelOcDomain::Core, 0x19, out.expected);
        out.writeAttempted = out.response.commandAttempted;
        if ((out.error = out.response.error)) return out;
        out.stage = IntelOcStage::Verify;
        out.readback = readIntelTvb(s, old.cpu);
    }
    if ((out.error = out.readback.error)) return out;
    if (out.readback.identity.signature != old.identity.signature) { out.error = -ENODEV; return out; }
    if (!sameTvbContext(old, out.readback) || !intelTvbEditable(out.readback)) { out.error = -EAGAIN; return out; }
    if (out.readback.response.data != out.expected) { out.error = -EIO; return out; }
    if ((out.error = s.checkpoint())) return out;
    out.verified = true; out.unchanged = !out.writeAttempted; out.stage = IntelOcStage::Complete;
    return out;
}
namespace {
bool avxSnapshotValid(const IntelAvxSnapshot &s) {
    return !s.error && s.valid && hasIntelOcProfile(s.identity) &&
        responseValid(s.response) && s.response.commandAttempted;
}
}
unsigned intelAvx2Offset(std::uint32_t word) { return (word >> 5) & 31u; }
int encodeIntelAvx2Offset(unsigned bins, std::uint32_t previous, std::uint32_t &encoded) {
    if (bins > 31) return -ERANGE;
    encoded = (previous & ~UINT32_C(0x3e0)) | (bins << 5);
    return 0;
}
bool intelAvxEditable(const IntelAvxSnapshot &s) {
    return avxSnapshotValid(s) && !(s.flexRatio & ocLock);
}
IntelAvxSnapshot readIntelAvx(HardwareSession &s, unsigned cpu) {
    IntelAvxSnapshot out; out.cpu = cpu; out.identity = identifyCpu(s, cpu);
    if ((out.error = out.identity.error)) return out;
    if (!hasIntelOcProfile(out.identity) || out.identity.maximum < 7) { out.error = -ENOTSUP; return out; }
    const auto features = s.cpuid(cpu, 1);
    if ((out.error = features.error)) return out;
    if (features.words[0] != out.identity.signature) { out.error = -ENODEV; return out; }
    if (!(features.words[3] & (1u << 5)) || !(features.words[2] & (1u << 28)) ||
        (features.words[2] & (1u << 31))) { out.error = -ENOTSUP; return out; }
    const auto extended = s.cpuid(cpu, 7);
    if ((out.error = extended.error)) return out;
    if (!(extended.words[1] & (1u << 5))) { out.error = -ENOTSUP; return out; }
    const auto flex = s.execute(msrRequest(cpu, 0x194));
    if ((out.error = flex.error)) return out;
    out.flexRatio = flex.value;
    out.response = mailboxCommand(s, cpu, 0x1a, 0, 0, 0);
    if ((out.error = out.response.error)) return out;
    if ((out.error = s.checkpoint())) return out;
    out.valid = true; return out;
}
IntelAvxUpdate applyIntelAvx(HardwareSession &s, const IntelAvxSnapshot &old, unsigned bins) {
    IntelAvxUpdate out;
    if (!avxSnapshotValid(old)) { out.error = -EINVAL; return out; }
    if ((out.error = encodeIntelAvx2Offset(bins, old.response.data, out.expected))) return out;
    out.expectedValid = true;
    if (!intelAvxEditable(old)) { out.error = -EPERM; return out; }
    out.stage = IntelOcStage::Preflight;
    const auto fresh = readIntelAvx(s, old.cpu); out.response = fresh.response;
    if ((out.error = fresh.error)) return out;
    if (fresh.identity.signature != old.identity.signature) { out.error = -ENODEV; return out; }
    if (!intelAvxEditable(fresh)) { out.error = -EPERM; return out; }
    out.stage = IntelOcStage::Compare;
    if (fresh.flexRatio != old.flexRatio || fresh.response.data != old.response.data) { out.error = -EAGAIN; return out; }
    if (out.expected == fresh.response.data) out.after = fresh;
    else {
        out.stage = IntelOcStage::Write;
        out.response = mailboxCommand(s, old.cpu, 0x1b, out.expected, 0, 0);
        out.writeAttempted = out.response.commandAttempted;
        if ((out.error = out.response.error)) return out;
        out.stage = IntelOcStage::Verify; out.after = readIntelAvx(s, old.cpu);
    }
    if ((out.error = out.after.error)) return out;
    if (out.after.identity.signature != old.identity.signature) { out.error = -ENODEV; return out; }
    if (out.after.flexRatio != old.flexRatio || !intelAvxEditable(out.after)) { out.error = -EAGAIN; return out; }
    if (out.after.response.data != out.expected) { out.error = -EIO; return out; }
    if ((out.error = s.checkpoint())) return out;
    out.verified = true; out.unchanged = !out.writeAttempted; out.stage = IntelOcStage::Complete;
    return out;
}
namespace {
bool bclkVoltageSnapshotValid(const IntelBclkVoltageSnapshot &s) {
    return !s.error && s.valid && hasIntelOcProfile(s.identity) &&
        responseValid(s.response) && s.response.commandAttempted;
}
}
bool intelBclkVoltageEnabled(std::uint32_t word) { return (word & 4u) != 0; }
int encodeIntelBclkVoltage(unsigned enabled, std::uint32_t previous, std::uint32_t &encoded) {
    if (enabled > 1) return -ERANGE;
    encoded = (previous & ~UINT32_C(4)) | (enabled << 2);
    return 0;
}
bool intelBclkVoltageEditable(const IntelBclkVoltageSnapshot &s) {
    return bclkVoltageSnapshotValid(s) && !(s.flexRatio & ocLock);
}
IntelBclkVoltageSnapshot readIntelBclkVoltage(HardwareSession &s, unsigned cpu) {
    IntelBclkVoltageSnapshot out; out.cpu = cpu; out.identity = identifyCpu(s, cpu);
    if ((out.error = out.identity.error)) return out;
    if (!hasIntelOcProfile(out.identity) || out.identity.maximum < 1) { out.error = -ENOTSUP; return out; }
    const auto features = s.cpuid(cpu, 1);
    if ((out.error = features.error)) return out;
    if (features.words[0] != out.identity.signature) { out.error = -ENODEV; return out; }
    if (!(features.words[3] & (1u << 5)) ||
        (features.words[2] & (1u << 31))) { out.error = -ENOTSUP; return out; }
    const auto flex = s.execute(msrRequest(cpu, 0x194));
    if ((out.error = flex.error)) return out;
    out.flexRatio = flex.value;
    out.response = mailboxCommand(s, cpu, 0x14, 0, 0, 0);
    if ((out.error = out.response.error)) return out;
    if ((out.error = s.checkpoint())) return out;
    out.valid = true; return out;
}
IntelBclkVoltageUpdate applyIntelBclkVoltage(HardwareSession &s, const IntelBclkVoltageSnapshot &old, unsigned enabled) {
    IntelBclkVoltageUpdate out;
    if (!bclkVoltageSnapshotValid(old)) { out.error = -EINVAL; return out; }
    if ((out.error = encodeIntelBclkVoltage(enabled, old.response.data, out.expected))) return out;
    out.expectedValid = true;
    if (!intelBclkVoltageEditable(old)) { out.error = -EPERM; return out; }
    out.stage = IntelOcStage::Preflight;
    const auto fresh = readIntelBclkVoltage(s, old.cpu); out.response = fresh.response;
    if ((out.error = fresh.error)) return out;
    if (fresh.identity.signature != old.identity.signature) { out.error = -ENODEV; return out; }
    if (!intelBclkVoltageEditable(fresh)) { out.error = -EPERM; return out; }
    out.stage = IntelOcStage::Compare;
    if (fresh.flexRatio != old.flexRatio || fresh.response.data != old.response.data) { out.error = -EAGAIN; return out; }
    if (out.expected == fresh.response.data) out.after = fresh;
    else {
        out.stage = IntelOcStage::Write;
        out.response = mailboxCommand(s, old.cpu, 0x15, out.expected, 0, 0);
        out.writeAttempted = out.response.commandAttempted;
        if ((out.error = out.response.error)) return out;
        out.stage = IntelOcStage::Verify; out.after = readIntelBclkVoltage(s, old.cpu);
    }
    if ((out.error = out.after.error)) return out;
    if (out.after.identity.signature != old.identity.signature) { out.error = -ENODEV; return out; }
    if (out.after.flexRatio != old.flexRatio || !intelBclkVoltageEditable(out.after)) { out.error = -EAGAIN; return out; }
    if (out.after.response.data != out.expected) { out.error = -EIO; return out; }
    if ((out.error = s.checkpoint())) return out;
    out.verified = true; out.unchanged = !out.writeAttempted; out.stage = IntelOcStage::Complete;
    return out;
}
IntelTvbThresholds decodeIntelTvbThresholds(std::uint32_t data) {
    IntelTvbThresholds out;
    out.temperatureA = (data >> 8) & 255; out.downBinsA = data & 255;
    out.temperatureB = (data >> 16) & 255; out.downBinsB = data >> 24;
    return out;
}
bool intelTvbTableRowValid(const IntelTvbTableRow &row) {
    return row.activeCores >= 1 && row.activeCores <= 8 && row.response.commandAttempted && responseValid(row.response);
}
IntelTvbTableSnapshot readIntelTvbTable(HardwareSession &s, unsigned cpu, unsigned activeCores) {
    IntelTvbTableSnapshot out; out.cpu = cpu; out.selectedActiveCores = activeCores;
    if (activeCores > 8) { out.error = -EINVAL; return out; }
    out.identity = identifyCpu(s, cpu);
    if ((out.error = out.identity.error)) return out;
    if (!hasIntelOcProfile(out.identity)) { out.error = -ENOTSUP; return out; }
    const auto features = s.cpuid(cpu, 1);
    if ((out.error = features.error)) return out;
    if (!(features.words[3] & (1u << 5)) || (features.words[2] & (1u << 31))) {
        out.error = -ENOTSUP; return out;
    }
    const unsigned first = activeCores ? activeCores : 1, last = activeCores ? activeCores : 8;
    for (unsigned candidate = first; candidate <= last; ++candidate) {
        IntelTvbTableRow row; row.activeCores = candidate;
        row.response = mailboxCommand(s, cpu, 0x24, 0, candidate, 0);
        out.rows.push_back(row);
        if (row.response.error) {
            if (!out.error) out.error = row.response.error;
            // Firmware rejections are local to a candidate, not a reliable
            // end-of-table marker. Transport/cancellation faults stop the scan.
            if (!row.response.completed) { out.error = row.response.error; return out; }
        }
    }
    if (const int error = s.checkpoint()) { out.error = error; return out; }
    out.scanCompleted = true;
    return out;
}
bool intelTvbTableValuesEditable(std::uint32_t data) {
    const auto fields = decodeIntelTvbThresholds(data);
    return fields.temperatureA <= 100 && fields.temperatureB <= 100 &&
        fields.downBinsA <= 10 && fields.downBinsB <= 10;
}
namespace {
bool tvbTableEditValid(const IntelTvbTableEditSnapshot &s) {
    return !s.error && s.valid && s.activeCores >= 1 && s.activeCores <= 8 &&
        tvbSnapshotValid(s.configuration) && s.value.commandAttempted && responseValid(s.value);
}
}
bool intelTvbTableEditable(const IntelTvbTableEditSnapshot &s) {
    return tvbTableEditValid(s) && intelTvbEditable(s.configuration) && intelTvbTableValuesEditable(s.value.data);
}
int encodeIntelTvbTable(IntelTvbTableField field, unsigned value,
                       std::uint32_t previous, std::uint32_t &encoded) {
    unsigned shift = 0, maximum = 0;
    switch (field) {
    case IntelTvbTableField::TemperatureA: shift = 8; maximum = 100; break;
    case IntelTvbTableField::DownBinsA: shift = 0; maximum = 10; break;
    case IntelTvbTableField::TemperatureB: shift = 16; maximum = 100; break;
    case IntelTvbTableField::DownBinsB: shift = 24; maximum = 10; break;
    default: return -EINVAL;
    }
    if (value > maximum || !intelTvbTableValuesEditable(previous)) return -ERANGE;
    encoded = (previous & ~(UINT32_C(255) << shift)) | (std::uint32_t(value) << shift);
    return 0;
}
IntelTvbTableEditSnapshot readIntelTvbTableEdit(HardwareSession &s, unsigned cpu, unsigned activeCores) {
    IntelTvbTableEditSnapshot out; out.activeCores = activeCores; out.configuration.cpu = cpu;
    if (activeCores < 1 || activeCores > 8) { out.error = -EINVAL; return out; }
    out.configuration = readIntelTvb(s, cpu);
    if ((out.error = out.configuration.error)) return out;
    out.value = mailboxCommand(s, cpu, 0x24, 0, activeCores, 0);
    if ((out.error = out.value.error)) return out;
    if ((out.error = s.checkpoint())) return out;
    out.valid = true; return out;
}
IntelTvbTableUpdate applyIntelTvbTable(HardwareSession &s, const IntelTvbTableEditSnapshot &old,
                                     IntelTvbTableField field, unsigned value) {
    IntelTvbTableUpdate out;
    if (!tvbTableEditValid(old)) { out.error = -EINVAL; return out; }
    if ((out.error = encodeIntelTvbTable(field, value, old.value.data, out.expected))) return out;
    if (!intelTvbTableEditable(old)) { out.error = -EPERM; return out; }
    out.stage = IntelOcStage::Preflight;
    const auto fresh = readIntelTvbTableEdit(s, old.configuration.cpu, old.activeCores);
    if ((out.error = fresh.error)) {
        out.response = fresh.configuration.error ? fresh.configuration.response : fresh.value;
        return out;
    }
    if (fresh.configuration.identity.signature != old.configuration.identity.signature) { out.error = -ENODEV; return out; }
    if (!intelTvbEditable(fresh.configuration)) { out.error = -EPERM; return out; }
    out.stage = IntelOcStage::Compare;
    if (!sameTvbContext(old.configuration, fresh.configuration) ||
        old.configuration.response.data != fresh.configuration.response.data || old.value.data != fresh.value.data) {
        out.error = -EAGAIN; return out;
    }
    if (out.expected == fresh.value.data) out.readback = fresh;
    else {
        out.stage = IntelOcStage::Write;
        out.response = mailboxCommand(s, old.configuration.cpu, 0x25, out.expected, old.activeCores, 0);
        out.writeAttempted = out.response.commandAttempted;
        if ((out.error = out.response.error)) return out;
        out.stage = IntelOcStage::Verify;
        out.readback = readIntelTvbTableEdit(s, old.configuration.cpu, old.activeCores);
    }
    if ((out.error = out.readback.error)) return out;
    if (out.readback.configuration.identity.signature != old.configuration.identity.signature) { out.error = -ENODEV; return out; }
    if (!sameTvbContext(old.configuration, out.readback.configuration) ||
        old.configuration.response.data != out.readback.configuration.response.data ||
        !intelTvbEditable(out.readback.configuration)) { out.error = -EAGAIN; return out; }
    if (out.readback.value.data != out.expected) { out.error = -EIO; return out; }
    if ((out.error = s.checkpoint())) return out;
    out.verified = true; out.unchanged = !out.writeAttempted; out.stage = IntelOcStage::Complete;
    return out;
}
} }
