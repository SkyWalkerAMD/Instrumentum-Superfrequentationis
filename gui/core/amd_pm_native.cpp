// SPDX-License-Identifier: GPL-2.0-only
#include "amd_pm_native.h"
#include "amd_rsmu.h"
#include <array>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace octool { namespace core {
namespace {
struct Packet {
    int error = 0;
    std::uint32_t response = 0;
    std::array<std::uint32_t, 2> words{};
    bool attempted = false;
};
// Separate closed transport: the existing tuning command allowlist is unchanged.
Packet exchange(HardwareSession &s, unsigned command) {
    Packet out;
    if (command < 3 || command > 5) { out.error = -EINVAL; return out; }
    auto r = detail::rsmuWait(s); if ((out.error = r.error)) return out;
    r = detail::rsmuIndirect(s, 0x3b10970, true, 0); if ((out.error = r.error)) return out;
    for (unsigned i = 0; i < 6; ++i) {
        // CPU4 GetDramAddress takes {1,1,0,0,0,0}; version/transfer take zeros.
        r = detail::rsmuIndirect(s, 0x3b10a40 + 4*i, true, command == 4 && i < 2 ? 1 : 0);
        if ((out.error = r.error)) return out;
    }
    out.attempted = true;
    r = detail::rsmuIndirect(s, 0x3b10924, true, command); if ((out.error = r.error)) return out;
    r = detail::rsmuWait(s); if ((out.error = r.error)) return out;
    out.response = std::uint32_t(r.value);
    if (out.response != 1) { out.error = -EIO; return out; }
    if (command != 3) for (unsigned i = 0; i < 2; ++i) {
        r = detail::rsmuIndirect(s, 0x3b10a40 + 4*i, false);
        if ((out.error = r.error)) return out;
        out.words[i] = std::uint32_t(r.value);
    }
    out.error = s.checkpoint(); return out;
}
int identity(HardwareSession &s, const SmuTarget &t, unsigned &bits) {
    if (t.profile != SmuProfile::Shimada || t.bus || t.device || t.function) return -ENOTSUP;
    if (const int e = s.checkpoint()) return e;
    const auto p = probeSmu(s, t);
    if (p.error) return p.error;
    if (p.identity.model != 8) return -ENOTSUP;
    const auto features = s.cpuid(t.cpu, 1);
    if (features.error) return features.error;
    if (features.words[2] & 0x80000000u) return -ENOTSUP;
    const auto max = s.cpuid(t.cpu, 0x80000000);
    if (max.error) return max.error;
    if (max.words[0] < 0x80000008) return -ENOTSUP;
    const auto a = s.cpuid(t.cpu, 0x80000008);
    if (a.error) return a.error;
    bits = a.words[0] & 255u;
    if (bits < 32 || bits > 52) return -ENOTSUP;
    return s.checkpoint();
}
std::string word(std::uint64_t v, int width) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "\"0x" << std::hex << std::setw(width) << std::setfill('0') << v << '"'; return out.str();
}
const char *yes(bool b) { return b ? "true" : "false"; }
}
const char *amdPmNativePhaseName(AmdPmNativePhase p) {
    switch (p) {
    case AmdPmNativePhase::Validate: return "validate";
    case AmdPmNativePhase::Version: return "version";
    case AmdPmNativePhase::Address: return "address";
    case AmdPmNativePhase::Transfer: return "transfer";
    case AmdPmNativePhase::Read: return "read";
    case AmdPmNativePhase::Recheck: return "recheck";
    case AmdPmNativePhase::Decode: return "decode";
    case AmdPmNativePhase::Complete: return "complete";
    }
    return "invalid";
}
AmdPmNativeRead readShimadaPmNative(HardwareSession &s, const SmuTarget &t, const AmdPmOptions &opt) {
    AmdPmNativeRead out; out.cpu = t.cpu;
    const auto fail = [&](int e) {
        out.error = e; out.snapshot = AmdPmSnapshot(); out.snapshot.error = e;
        out.snapshot.detail = "Native PM capture stopped during " + std::string(amdPmNativePhaseName(out.phase)) + "; no buffer published";
        return out;
    };
    if (opt.hasBclk && (!std::isfinite(opt.bclkMHz) || opt.bclkMHz <= 0)) return fail(-EINVAL);
    if (const int e = identity(s, t, out.physicalAddressBits)) return fail(e);
    out.phase = AmdPmNativePhase::Version;
    auto p = exchange(s, 5); out.response = p.response;
    if (p.error) return fail(p.error);
    out.version = p.words[0]; out.reportedBytes = p.words[1];
    const auto size = amdPmTableSize(out.version);
    if (!size) return fail(-ENOTSUP);
    if (out.reportedBytes != size) return fail(-EMSGSIZE);
    out.metadataValid = true;
    out.phase = AmdPmNativePhase::Address;
    p = exchange(s, 4); out.response = p.response;
    if (p.error) return fail(p.error);
    out.address = std::uint64_t(p.words[0]) | (std::uint64_t(p.words[1]) << 32);
    const auto maximum = (std::uint64_t(1) << out.physicalAddressBits) - 1;
    // Require a non-null, word-aligned range entirely within the selected
    // CPU's physical-address width. Never strip high bits or truncate to 32 bits.
    if (out.address < 4096 || (out.address & 3u) || out.address > maximum || size - 1 > maximum - out.address) return fail(-ERANGE);
    out.addressValid = true;
    unsigned bits = 0;
    if (const int e = identity(s, t, bits)) return fail(e);
    if (bits != out.physicalAddressBits) return fail(-EAGAIN);
    out.phase = AmdPmNativePhase::Transfer;
    p = exchange(s, 3); out.response = p.response;
    out.transferAttempted = p.attempted; out.transferAccepted = p.response == 1;
    if (p.error) return fail(p.error);
    out.phase = AmdPmNativePhase::Read;
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t i = 0; i < size; i += 4) {
        if (const int e = s.checkpoint()) return fail(e);
        Request request; request.space = Space::Memory; request.width = 4; request.address = out.address + i;
        const auto r = s.execute(request); if (r.error) return fail(r.error);
        for (unsigned j = 0; j < 4; ++j) bytes[i+j] = std::uint8_t(r.value >> (8*j));
        out.bytesRead += 4;
    }
    out.phase = AmdPmNativePhase::Recheck;
    if (const int e = identity(s, t, bits)) return fail(e);
    if (bits != out.physicalAddressBits) return fail(-EAGAIN);
    p = exchange(s, 5); out.response = p.response;
    if (p.error) return fail(p.error);
    if (p.words[0] != out.version || p.words[1] != out.reportedBytes) return fail(-EAGAIN);
    p = exchange(s, 4); out.response = p.response;
    if (p.error) return fail(p.error);
    if ((std::uint64_t(p.words[0]) | (std::uint64_t(p.words[1]) << 32)) != out.address) return fail(-EAGAIN);
    if (const int e = s.checkpoint()) return fail(e);
    out.metadataRechecked = true; out.phase = AmdPmNativePhase::Decode;
    out.snapshot = decodeAmdPm(bytes, out.version, opt); out.error = out.snapshot.error;
    // Non-finite fields remain partial captures, as with file/driver decoding.
    if (!out.error) out.phase = AmdPmNativePhase::Complete;
    return out;
}
std::string amdPmNativeJson(const AmdPmNativeRead &r) {
    return "{\"cpu\":" + std::to_string(r.cpu) + ",\"phase\":\"" + amdPmNativePhaseName(r.phase) +
        "\",\"error\":" + std::to_string(r.error) + ",\"physical_address_bits\":" + std::to_string(r.physicalAddressBits) +
        ",\"version\":" + word(r.version,8) + ",\"reported_bytes\":" + std::to_string(r.reportedBytes) +
        ",\"address\":" + (r.addressValid ? word(r.address,16) : "null") +
        ",\"metadata_valid\":" + yes(r.metadataValid) + ",\"address_valid\":" + yes(r.addressValid) +
        ",\"transfer_attempted\":" + yes(r.transferAttempted) + ",\"transfer_accepted\":" + yes(r.transferAccepted) +
        ",\"response\":" + word(r.response,8) + ",\"bytes_read\":" + std::to_string(r.bytesRead) +
        ",\"metadata_rechecked\":" + yes(r.metadataRechecked) + ",\"origin_verified\":false,\"atomic_snapshot_guaranteed\":false,"
        "\"freshness_verified\":false,\"physical_mapping_verified\":false,\"tuning_settings_requested\":false}";
}
} }
