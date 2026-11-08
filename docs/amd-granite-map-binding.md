# Granite early core-status HOB and independent enablement masks

The reference firmware reads the disabled-fuse byte and the enabled-core
register independently, then records a three-state result for each local core.
This supplies a concrete producer and consumer for a **core-status GUID HOB**.
It does not supply a PM-to-APOB, physical CCD, APIC or OS-CPU binding.

The source remains ASUS ProArt X870E-CREATOR WIFI BIOS 2503. The user's
9950X3D is unavailable and its installed BIOS is unknown. All register values,
CPUID replies and firmware service outcomes below are synthetic inputs.

## Immutable IA32 inputs and address conventions

| Role | Capsule file GUID | Bytes | SHA-256 |
|---|---|---:|---|
| Early mask/HOB producer | `F3FDA9D8-4EE5-44D8-8E49-AFAA77B5FC51` | 33584 | `7464250e2d6f8e3268af5da403997d57c86485ef86b25a23bc2b4af6abc65f1f` |
| Core-status consumer | `C8F6D366-DDF1-4960-9D57-82F2655A1294` | 20736 | `c76d2ac61c511c113406297113d999f9cf7f1827c3b4413029907d5f7966ecb6` |

These are **IA32 TE images**, machine `0x14c`, rather than the AMD64 PE
modules used by the earlier topology protocol. The producer image base is
`0x09ac6280`; its original section RVA minus stripped-file offset is `0x190`.
The consumer base is `0x09b07988` and its delta is `0x180`. Addresses below
are RVAs; add the relevant image base for virtual addresses. Reading these
bytes as x86-64 incorrectly interprets IA32 INC instructions as prefixes.

The proof maps each original section at its TE virtual address and verifies
every executed instruction against those bytes. An analysis PE wrapper, if
used for REA/Ghidra discovery, is a derived analysis input and does not replace
either original TE identity. Claim IDs below are local research identifiers;
they are not invented REA Evidence IDs.

## GMB-01: a model/package-gated producer reads both masks

Entry `0x409` supplies CPUID leaf `0x80000001` through wrapper `0x4a21`.
Before initialization it requires `(EAX & 0x0fff0f00) == 0x00b40f00` and
`EBX[31:28] == 0`. Signature `0x00b40f40` with package nibble zero passes
the checked branch. This agrees with provider A's prior eligibility gate;
it does not prove installation or execution on the user's machine.

After other initialization and service calls, the accepted entry calls
`0x66d` at `0x520`. Those intervening initialization paths are not executed
by this experiment. The separate gate replay stops at `0x470` or `0x657`.

The complete `0x66d` body reads the following coordinates:

| Data | Coordinate | Used bits |
|---|---|---|
| Target presence | `0x5d3bc` | 23:22 |
| Disabled fuse word, target `t` | `0x304a03dc OR (t << 25)` | low byte and bit 8 |
| Control word, target `t` | `0x203c0030 OR (t << 23)` | inverse of bit 8 |
| Enabled-core word, target `t` | `0x203c003c OR (t << 23)` | low byte |

Only present targets receive the three target-specific reads. The target
loop is fixed to two entries. `0x997` calls `0x4789`, with PCI index coordinate
`(socket_byte << 20) OR 0xb8`. The actual `0x4917/0x492a` instructions use
the reference PCI window, read the previous index, select the requested
coordinate, read data at index `+4`, then restore the old index. This proof
uses in-memory PCI registers and permits index writes only, never data writes.

The disabled coordinate is the same one used by the earlier
[provider-A query](amd-granite-map-acquisition.md). The enabled coordinates
are `0x203c003c` and `0x20bc003c`, matching the checked
[SMU command-remap initializer](amd-granite-slot-binding.md). This is a
checked coordinate connection; the values are still independent inputs.

## GMB-02: complete GUID HOB layout and three-state construction

The producer requests a type-4 HOB of size `0x20c`, writes GUID
`0cdfa7da-0d0b-41f0-b98b-0f6359c0857f` at HOB `+8`, and copies exactly
`0x1f4` bytes of zero-initialized local payload to HOB `+0x18`.
The allocator's memory and common header are fixtures; the GUID stores and
full payload construction/copy execute original instructions.

| Payload offset | Encoding and recovered meaning |
|---|---|
| `+0x00` | DWORD target count, fixed 2 |
| `+0x04` | DWORD local core count per target, fixed 8 |
| `+0x08` | DWORD OR of bit 8 from present targets' fuse words, 0 or 1 |
| `+0x0c` | DWORD two-bit presence mask |
| `+0x10 + 4*t` | DWORD containing disabled-fuse low byte |
| `+0x40 + 4*t` | DWORD containing enabled-register low byte |
| `+0x70` | DWORD OR of inverse control-word bit 8 across present targets |
| `+0x74 + 4*(8*t+c)` | DWORD state, for target `t=0..1`, core bit `c=0..7` |

There is storage for twelve target masks and twelve groups of eight states,
but this producer writes only the first two. The other ten targets' mask and
state spaces remain zero from initialization. They are unused storage, not
ten additional detected targets.

For each present target the original branches assign:

| Fuse bit | Enabled bit | State |
|---:|---:|---:|
| 1 | either | 0: excluded by fuse mask |
| 0 | 1 | 1: enabled by register |
| 0 | 0 | 2: not enabled by register |

For an absent target the producer instead writes disabled mask `0xff`,
enabled mask zero and eight zero states. These are absence fill values,
not observations that eight physical cores are fuse-disabled. Consumers of
the imported record must preserve the separate presence flag.

Independent masks matter. With disabled byte zero and enabled byte zero,
the producer records eight state-2 positions; replacing the enabled byte
with the complement of the disabled byte would falsely yield eight state-1
positions. A set bit in both masks yields state zero, retaining fuse priority.
Synthetic overlap cases establish branch behavior, not a claim that a target
machine actually has contradictory register contents.

## GMB-03: a separate consumer uses this exact HOB

Consumer `0x1f7b` locates the same GUID through `0x434a`. The experiment
replays the original consumer through `0x20ee`, supplying a synthetic HOB
lookup and IDTR/services pointer. It copies the two target/core limits,
presence and each present target's three-state cells into its own record.
It caps this branch at two targets and eight cores. Absent targets' output
core-state bytes are left untouched by this slice.

Subsequent original code at `0x2144..0x218b` chooses printable tokens for the
three states: state 0 uses `" x "`, state 1 uses `" 1 "` and state 2 uses
`" 0 "`, alongside `"CCD%d :"`. These literal strings and branch sites are
static byte evidence; the formatting/service paths are outside this replay.
The module contains AOD setup labels. Neither that label nor `CCD%d` supplies
a physical numbering contract. This is not the earlier group-3/type-3 APOB
record, which has a different producer and layout.

The prior 64-bit topology consumer's `0x1655..0x16ed` also interprets
provider A's selected low byte as disabled positions, calculating
`popcount(active_targets) * (8-popcount(selected_byte))`. It reuses one
selected byte for all targets. That prior slice is corroborating consumer
evidence, not a source of per-target enabled masks or physical identities.

## Why this HOB is insufficient for a live binding

The SMU mapper stores the enabled word's **low sixteen bits** and lets bits
8..15 affect whether a target occupies a compact command rank. This HOB
retains only the low eight bits. A synthetic register word `0x100` becomes
zero in the HOB even though the SMU mapper treats its low sixteen bits as
nonzero. The HOB therefore cannot reconstruct all mapper inputs.

The HOB's control flag uses **bit 8** and is aggregated across present
targets. The mapper reads **bit 10** from the first active target for its
SMT decision. Neither aggregation is a substitute for that input. No flag
in this HOB is promoted to a verified SMT value.

An imported HOB can consequently explain recorded fuse exclusion, enabled
register bits and three-state consistency. It cannot authenticate its own
origin, select a runtime provider, identify physical cores, or join PM slots
to APOB, APIC or OS CPU IDs. No native HOB extraction or register collection
is added by this study.

## Strict offline import contract

The shared decoder accepts only a complete 524-byte GUID HOB. It checks
type 4, declared length 524, the exact GUID, target/core counts 2/8,
presence bits 0/1 only, low-byte masks, flags 0/1, exact three-state
consistency and zero unused target storage. Requiring the common-header
reserved DWORD to be zero is an application envelope check; it is not
an observed allocator behavior in the synthetic replay.
With no present target, both aggregate flags must also be zero; neither
target loop ran. With a present target the two flags remain independent.

Mask overlaps are diagnostic information and remain visible. They do not
justify deleting raw bits or guessing a physical target. All provenance,
live capture, runtime provider and PM/physical/APIC/OS-binding trust flags
remain false. Invalid records publish no target table. Loading a record
must not prepare any tuning command or overwrite a verified live identity.

## Reproduction

```sh
python3 analysis/tools/firmware-granite-map-binding.py \
  --producer build/firmware-input/uefi-granite-core-status-producer.bin \
  --consumer build/firmware-input/uefi-granite-core-status-consumer.bin \
  --output build/firmware-evidence/granite-map-binding.json \
  --example-hob build/firmware-evidence/granite-core-status-example.hob \
  --vectors build/firmware-evidence/granite-core-status-vectors.h
```

The input filenames above designate the **original TE files**. Dependencies
are pinned Unicorn 2.1.4; only standard-library parsing is used for TE mapping.
The default run includes 5,384 producer cases, 5,380 composed consumer slices
and 8,256 CPU-gate cases. It varies every disabled-byte encoding, independent
enabled-bit bases, present/absent targets, high bits, both control bits and
allocation failures. Original instruction counts, data/index read counts,
gate acceptance, trace hashes and four independently generated C++ oracle
vectors are retained in the JSON report. All cases are bounded synthetic
execution, including the success of the HOB service fixtures.
