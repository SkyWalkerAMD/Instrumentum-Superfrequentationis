# Granite firmware command indexes and PM storage slots

Reference: ASUS ProArt X870E-CREATOR WIFI BIOS 2503, SMU 98.84.0.
The SMU body is pinned to SHA-256
`5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596`.
This extends the [startup/PM ordering study](amd-granite-semantics.md#firmware-storage-slot-to-ccd-target)
using REA-assisted Ghidra evidence and original-byte instruction replay.
The actual machine is unavailable; its installed BIOS is unknown.

A new path connects the firmware's per-target core masks to a compact command
index table and its consumers. It explains why a PM core slot, a compact
command index and an OS CPU/APIC ID must remain different coordinates.
It does **not** establish a physical CCD or OS CPU association.

## Mask acquisition and compact lists

Initializer `0x1e8ec` obtains two presence bits through fuse helper `0x1e22c`,
bit address `0x1df6`, width 2. For each present firmware target it calls
`0x1db30` with selector `0x203c003c`. The wrapper uses the reference base table
at `0x6828` and the selector's low 22 bits:

| Firmware target | Reference base | Core-mask register coordinate |
|---|---|---|
| 0 | `0x20000000` | `0x203c003c` |
| 1 | `0x20800000` | `0x20bc003c` |

The low 16 reply bits are stored at `0x8198 + 2*target`. The core enumeration
loop examines bits 0–7. Its compact active-target list includes a present
target only when that stored mask is nonzero. The first active target also
supplies selector `0x203c0030`; bit 10 clear selects two threads per core,
and bit 10 set selects one. The reference does not re-read that SMT setting
for every subsequent target.

The code creates these SMU-local records:

| Address | Contents |
|---|---|
| `0x6814` | Present-target mask |
| `0x6816` | Mask of present targets with a nonzero core mask |
| `0x81e0 + 4*n` | Compact enabled-core list: target at +0, original core-mask bit at +2 |
| `0x8220 + 4*n` | Compact thread list: target at +0, original core-mask bit at +2, thread at +3 |
| `0x8318 + 2*i`, `i < 64` | Compact command index to encoded target/core/thread; absent entries are `0xffff` |

All of these are reference firmware coordinates, not host physical addresses
or permissions to read them. The raw register replies, fuses and startup state
in the experiment are synthetic; the wrapper's transport callback returns
fixtures. Only the acquisition wrapper and arithmetic/list-building instructions
execute, with the initializer stopped before its final hardware write loop.

## Two encodings, neither a verified APIC contract

Helper `0x2e1b4` encodes six inputs: an SMT-mode flag, a one-bit domain,
a four-bit target, a one-bit subgroup, a three-bit core and a one-bit thread.
The initializer supplies domain/subgroup zero. In that path:

```text
SMT:     index = active_target_rank * 32 + enabled_core_rank * 2 + thread
         value = firmware_target   * 32 + original_core_bit * 2 + thread
non-SMT: index = active_target_rank * 16 + enabled_core_rank
         value = firmware_target   * 16 + original_core_bit
```

These expressions describe the checked encoding; the proof executes the
original helper instead of substituting the formulas. The general helper
masks its argument widths. Decoder `0x2e17c` returns domain, target, core and
thread; it has no subgroup output.

The PM initializer instead assigns its CCD storage rank to **every present
target**, regardless of its core mask. Each PM CCD slot reserves eight local
core positions. Disabling cores therefore compacts the command coordinates
without compacting PM core storage.

| Synthetic input, SMT enabled | Command mapping | PM storage consequence from the existing startup proof |
|---|---|---|
| Presence 2, target-1 mask `0x81` | Index 0/1 → target 1, core bit 0; index 2/3 → target 1, core bit 7 | One CCD storage slot belongs to firmware target 1; local core positions remain 0 and 7 |
| Presence 3, masks `[0,0xff]` | Index 0–15 → target 1 | Two CCD storage slots still belong to targets 0/1 |
| Presence 3, masks `[0xff,0xff]` | Valid indexes 0–15 and 32–47; indexes 16–31 remain absent | Two fixed eight-core storage groups |

The [published 9950X3D CPUID capture](amd-granite-published-topology.md)
has a CCD APIC shift of 4 and APIC IDs 0–31. The checked compact command
encoding has an SMT target shift of 5. This numeric mismatch rules out simply
using that capture's APIC IDs as command indexes in this reference path.
It does not prove the target's runtime masks, provider, physical numbering or
the meaning of every firmware command interface.

## Consumers and the remaining access boundary

Setter body `0x2a274` uses the input's upper 16 bits as the table index and
retains its low 16 bits as the payload. It rejects indexes above 63 or an
entry equal to `0xffff`. After `0x2e17c`, it checks a domain gate at `0x8b38`
and forwards `(decoded_core << 16) | payload` with MP5 message `0x3d` to the
decoded firmware target. The decoded thread bit is not used by the forwarded
request. Two SMT sibling indexes can therefore address the same core.

Getter-shaped body `0x2a314` uses the entire input as its bounded index and
forwards MP5 `0x3e` with the decoded core. Its returned payload is passed to
the mailbox helper. A public host-command route to that body has **not** been
verified. The setter pointer occurs at image offset `0x6d54`, but table
membership alone is not a tested host transport. No new native command or
write allowlist entry is added.

The consumers' transport and return values are fixtures. They establish
argument selection and rejection behavior, not hardware settings, units,
success of application or independent device readback. For initialized
domain-zero records, the checked consumers reject when byte `0x8b38` is zero
and proceed when it is nonzero; the wider meaning of that gate remains open.

## Reproduction and results

```sh
python3 analysis/tools/firmware-granite-slot-binding.py \
  --smu build/firmware-input/smu-62-54-body.bin \
  --study build/firmware-evidence/smu/smu-study.json \
  --output build/firmware-evidence/granite-slot-binding.json
```

The local replay checks:

- 3,840 encoder/decoder combinations, including discarded high argument bits.
- 1,534 initializer cases across presence masks 0–3, both SMT modes, sparse
  core sets and all 256 possible byte masks in each target position.
- 98,176 remap words and 14,568 compact thread records, plus three cases
  confirming that the register reply's upper 16 bits are discarded.
- 2,144 consumer cases: 152 forwarded requests and 1,992 rejections, including
  table holes, out-of-range selectors and the disabled gate.

The shared interpreter checks every executed instruction against the pinned
firmware bytes. No host CPUID, firmware service, MMIO or register I/O executes.
The existing startup-packet proof remains separately responsible for PM scatter
ordering; the new experiment does not authenticate a live capture or a runtime
APOB record. Physical CCD/OS-CPU binding, eight remaining row units and the
second rail's independent write/readback remain unresolved.
