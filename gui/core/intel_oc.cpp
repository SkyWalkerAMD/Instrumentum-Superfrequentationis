// SPDX-License-Identifier: GPL-2.0-only
#include "intel_oc.h"
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
IntelOcResponse exchange(HardwareSession &s, unsigned cpu, IntelOcDomain domain,
                         bool write, std::uint32_t data, unsigned point = 0) {
    IntelOcResponse out;
    std::uint64_t value = 0;
    if ((out.error = waitReady(s, cpu, value))) return out;
    Request request = msrRequest(cpu, 0x150);
    request.write = true;
    request.value = busy | (std::uint64_t(unsigned(domain)) << 40) |
                    (std::uint64_t(point) << 48) |
                    (std::uint64_t(write ? 0x11 : 0x10) << 32) | data;
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
}
bool hasIntelOcProfile(const CpuIdentity &id) {
    // Raptor Lake-S client path. Never infer this from the board/product name.
    return !id.error && id.intel && id.family == 6 && id.model == 0xb7;
}
double intelOcOffsetMillivolts(std::uint32_t data) {
    int code = int(data >> 21);
    if (code & 1024) code -= 2048;
    return double(code) * (1000.0 / 1024.0);
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
} }
