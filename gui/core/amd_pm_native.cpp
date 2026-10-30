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
    bool attempted = false, absent = false;
};
// Separate closed transport: the existing tuning command allowlist is unchanged.
Reply wait(HardwareSession &s, bool granite) {
    for (unsigned poll = 0; poll < 100; ++poll) {
        const auto r = detail::rsmuIndirect(s,granite ? 0x3b10570 : 0x3b10970,false);
        if (r.error || r.value) return r;
        detail::rsmuDelay();
    }
    Reply r; r.error = -ETIMEDOUT; return r;
}
Packet exchange(HardwareSession &s, unsigned command, unsigned target = 0, bool granite = false) {
    Packet out;
    if (granite && command == 0xa5) { out.error = -ENOTSUP; return out; }
    if ((command != 2 && command != 3 && command != 4 && command != 5 && command != 0xa5) ||
        (command == 0xa5 ? target > 15 : target != 0)) { out.error = -EINVAL; return out; }
    auto r = wait(s,granite); if ((out.error = r.error)) return out;
    r = detail::rsmuIndirect(s, granite ? 0x3b10570 : 0x3b10970, true, 0); if ((out.error = r.error)) return out;
    for (unsigned i = 0; i < 6; ++i) {
        // Shimada address query uses {1,1,0,0,0,0}; Granite metadata and transfer use zeros.
        const unsigned arg = !granite && command == 4 && i < 2 ? 1 : command == 0xa5 && i == 0 ? target : 0;
        r = detail::rsmuIndirect(s, 0x3b10a40 + 4*i, true, arg);
        if ((out.error = r.error)) return out;
    }
    out.attempted = true;
    r = detail::rsmuIndirect(s, granite ? 0x3b10524 : 0x3b10924, true, command); if ((out.error = r.error)) return out;
    r = wait(s,granite); if ((out.error = r.error)) return out;
    out.response = std::uint32_t(r.value);
    if (command == 0xa5 && out.response == 0xfe) {
        // Only this pinned command's completed FE response means absent.
        // Transport faults, timeouts and other firmware errors never do.
        out.error = s.checkpoint(); out.absent = !out.error; return out;
    }
    if (out.response != 1) { out.error = -EIO; return out; }
    const unsigned words = command == 3 ? 0 : command == 4 || command == 5 ? 2 : 1;
    for (unsigned i = 0; i < words; ++i) {
        r = detail::rsmuIndirect(s, 0x3b10a40 + 4*i, false);
        if ((out.error = r.error)) return out;
        out.words[i] = std::uint32_t(r.value);
    }
    out.error = s.checkpoint(); return out;
}
int presentMask(HardwareSession &s, unsigned &mask, unsigned &queried, std::uint32_t &response) {
    mask = queried = 0;
    for (unsigned target = 0; target < 16; ++target) {
        const auto p = exchange(s,0xa5,target); response = p.response;
        if (p.error) return p.error;
        ++queried;
        if (!p.absent) mask |= 1u << target;
        // A5's packed clock result is deliberately not decoded as rail state.
    }
    return 0;
}
std::vector<unsigned> targetIds(unsigned mask) {
    std::vector<unsigned> out;
    for (unsigned target = 0; target < 16; ++target) if (mask & (1u << target)) out.push_back(target);
    return out;
}
int identity(HardwareSession &s, const SmuTarget &t, unsigned &bits, bool granite = false) {
    if ((!granite && t.profile != SmuProfile::Shimada) || t.bus || t.device || t.function) return -ENOTSUP;
    if (const int e = s.checkpoint()) return e;
    if (granite) {
        const auto cpu = identifyCpu(s,t.cpu);
        if (cpu.error) return cpu.error;
        if (!cpu.amd || cpu.family != 0x1a || cpu.model != 0x44) return -ENOTSUP;
        Request request; request.space = Space::Pci; request.width = 4;
        const auto pci = s.execute(request);
        if (pci.error) return pci.error;
        if (pci.value != 0x14d81022) return -ENODEV;
    } else {
        const auto p = probeSmu(s,t);
        if (p.error) return p.error;
        if (p.identity.model != 8) return -ENOTSUP;
    }
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
    case AmdPmNativePhase::Topology: return "topology";
    case AmdPmNativePhase::Address: return "address";
    case AmdPmNativePhase::Transfer: return "transfer";
    case AmdPmNativePhase::Read: return "read";
    case AmdPmNativePhase::Recheck: return "recheck";
    case AmdPmNativePhase::TopologyRecheck: return "topology-recheck";
    case AmdPmNativePhase::Decode: return "decode";
    case AmdPmNativePhase::Complete: return "complete";
    }
    return "invalid";
}
static AmdPmNativeRead capture(HardwareSession &s, const SmuTarget &t, const AmdPmOptions &opt, bool mapCcds, bool granite) {
    AmdPmNativeRead out; out.cpu = t.cpu; out.mappingRequested = mapCcds; out.graniteRidge = granite;
    const auto fail = [&](int e) {
        out.error = e; out.snapshot = AmdPmSnapshot(); out.snapshot.error = e;
        out.mappingValid = out.mappingRechecked = false; out.firmwareCcdIds.clear();
        out.snapshot.detail = "Native PM capture stopped during " + std::string(amdPmNativePhaseName(out.phase)) + "; no buffer published";
        return out;
    };
    const auto packet = [&](unsigned command) { return exchange(s,command,0,granite); };
    if (opt.hasBclk && (!std::isfinite(opt.bclkMHz) || opt.bclkMHz <= 0)) return fail(-EINVAL);
    if (granite && (opt.hasBclk || mapCcds)) return fail(-ENOTSUP);
    if (const int e = identity(s, t, out.physicalAddressBits, granite)) return fail(e);
    out.phase = AmdPmNativePhase::Version;
    Packet p;
    if (granite) {
        p = packet(2); out.response = p.response;
        if (p.error) return fail(p.error);
        out.smuVersion = p.words[0];
        if (out.smuVersion != 0x00625400) return fail(-ENOTSUP);
    }
    p = packet(5); out.response = p.response;
    if (p.error) return fail(p.error);
    out.version = p.words[0]; out.reportedBytes = p.words[1];
    const auto size = amdPmTableSize(out.version);
    if (!size || granite != isGranitePm(out.version)) return fail(-ENOTSUP);
    if (out.reportedBytes != size) return fail(-EMSGSIZE);
    out.metadataValid = true;
    if (mapCcds) {
        out.phase = AmdPmNativePhase::Topology;
        p = packet(2); out.response = p.response;
        if (p.error) return fail(p.error);
        out.smuVersion = p.words[0];
        if (out.smuVersion != 0x00732e00) return fail(-ENOTSUP);
        if (const int e = presentMask(s, out.presentMaskBefore, out.queriedBefore, out.response)) return fail(e);
        if (targetIds(out.presentMaskBefore).size() != (size - 0x634) / 0x274) return fail(-EMSGSIZE);
    }
    out.phase = AmdPmNativePhase::Address;
    p = packet(4); out.response = p.response;
    if (p.error) return fail(p.error);
    out.address = std::uint64_t(p.words[0]) | (std::uint64_t(p.words[1]) << 32);
    const auto maximum = (std::uint64_t(1) << out.physicalAddressBits) - 1;
    // Require a non-null, word-aligned range entirely within the selected
    // CPU's physical-address width. Never strip high bits or truncate to 32 bits.
    if (out.address < 4096 || (out.address & 3u) || out.address > maximum || size - 1 > maximum - out.address) return fail(-ERANGE);
    out.addressValid = true;
    unsigned bits = 0;
    if (const int e = identity(s, t, bits, granite)) return fail(e);
    if (bits != out.physicalAddressBits) return fail(-EAGAIN);
    out.phase = AmdPmNativePhase::Transfer;
    p = packet(3); out.response = p.response;
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
    if (const int e = identity(s, t, bits, granite)) return fail(e);
    if (bits != out.physicalAddressBits) return fail(-EAGAIN);
    p = packet(5); out.response = p.response;
    if (p.error) return fail(p.error);
    if (p.words[0] != out.version || p.words[1] != out.reportedBytes) return fail(-EAGAIN);
    p = packet(4); out.response = p.response;
    if (p.error) return fail(p.error);
    if ((std::uint64_t(p.words[0]) | (std::uint64_t(p.words[1]) << 32)) != out.address) return fail(-EAGAIN);
    if (mapCcds) {
        out.phase = AmdPmNativePhase::TopologyRecheck;
        p = packet(2); out.response = p.response;
        if (p.error) return fail(p.error);
        if (p.words[0] != out.smuVersion) return fail(-EAGAIN);
        if (const int e = presentMask(s, out.presentMaskAfter, out.queriedAfter, out.response)) return fail(e);
        if (out.presentMaskAfter != out.presentMaskBefore) return fail(-EAGAIN);
        if (const int e = identity(s, t, bits, granite)) return fail(e);
        if (bits != out.physicalAddressBits) return fail(-EAGAIN);
        p = packet(2); out.response = p.response;
        if (p.error) return fail(p.error);
        if (p.words[0] != out.smuVersion) return fail(-EAGAIN);
    }
    if (granite) {
        p = packet(2); out.response = p.response;
        if (p.error) return fail(p.error);
        if (p.words[0] != out.smuVersion) return fail(-EAGAIN);
        if (const int e = identity(s,t,bits,granite)) return fail(e);
        if (bits != out.physicalAddressBits) return fail(-EAGAIN);
    }
    if (const int e = s.checkpoint()) return fail(e);
    if (mapCcds) {
        out.firmwareCcdIds = targetIds(out.presentMaskBefore);
        out.mappingValid = out.mappingRechecked = true;
    }
    out.metadataRechecked = true; out.phase = AmdPmNativePhase::Decode;
    out.snapshot = decodeAmdPm(bytes, out.version, opt); out.error = out.snapshot.error;
    // Non-finite fields remain partial captures, as with file/driver decoding.
    if (!out.error) out.phase = AmdPmNativePhase::Complete;
    return out;
}
AmdPmNativeRead readShimadaPmNative(HardwareSession &s, const SmuTarget &t, const AmdPmOptions &o, bool map) {
    return capture(s,t,o,map,false);
}
AmdPmNativeRead readGranitePmNative(HardwareSession &s, unsigned cpu, const AmdPmOptions &o) {
    SmuTarget t; t.cpu = cpu;
    return capture(s,t,o,false,true);
}
std::string amdPmNativeJson(const AmdPmNativeRead &r) {
    std::string ids = "[";
    if (r.mappingValid) for (std::size_t i = 0; i < r.firmwareCcdIds.size(); ++i)
        ids += (i ? "," : "") + std::to_string(r.firmwareCcdIds[i]);
    ids += "]";
    return std::string("{\"platform\":\"") + (r.graniteRidge ? "granite-ridge" : "shimada") + "\",\"smu_version\":" + word(r.smuVersion,8) + ",\"cpu\":" + std::to_string(r.cpu) + ",\"phase\":\"" + amdPmNativePhaseName(r.phase) +
        "\",\"error\":" + std::to_string(r.error) + ",\"physical_address_bits\":" + std::to_string(r.physicalAddressBits) +
        ",\"version\":" + word(r.version,8) + ",\"reported_bytes\":" + std::to_string(r.reportedBytes) +
        ",\"address\":" + (r.addressValid ? word(r.address,16) : "null") +
        ",\"metadata_valid\":" + yes(r.metadataValid) + ",\"address_valid\":" + yes(r.addressValid) +
        ",\"transfer_attempted\":" + yes(r.transferAttempted) + ",\"transfer_accepted\":" + yes(r.transferAccepted) +
        ",\"response\":" + word(r.response,8) + ",\"bytes_read\":" + std::to_string(r.bytesRead) +
        ",\"metadata_rechecked\":" + yes(r.metadataRechecked) + ",\"origin_verified\":false,\"atomic_snapshot_guaranteed\":false,"
        "\"ccd_mapping\":{\"requested\":" + yes(r.mappingRequested) + ",\"valid\":" + yes(r.mappingValid) +
        ",\"rechecked\":" + yes(r.mappingRechecked) + ",\"smu_version\":" + word(r.smuVersion,8) +
        ",\"present_mask_before\":" + word(r.presentMaskBefore,4) + ",\"present_mask_after\":" + word(r.presentMaskAfter,4) +
        ",\"queried_before\":" + std::to_string(r.queriedBefore) + ",\"queried_after\":" + std::to_string(r.queriedAfter) +
        ",\"firmware_ccd_ids_by_slot\":" + ids + ",\"active_core_mask_verified\":false,\"os_cpu_mapping_verified\":false},"
        "\"freshness_verified\":false,\"physical_mapping_verified\":false,\"tuning_settings_requested\":false}";
}
} }
