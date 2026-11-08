# Granite offline core-index diagnostic

The shared GUI/CLI diagnostic computes the reference firmware's PM storage
positions and compact core-command indexes from **explicit inputs**. It does
not collect masks from the machine or bind a PM slot to a physical CCD, APIC
ID or OS CPU. The reference is ASUS ProArt X870E-CREATOR WIFI BIOS 2503, SMU
98.84.0. The user's installed BIOS is unknown and target hardware is unavailable.

The core API is `diagnoseAmdGraniteMapping(AmdGraniteMappingInput)`, in
`gui/core/amd_granite_mapping.{h,cpp}`. It has no hardware/session dependency.
The CLI accepts the following inputs; the GUI's PM page offers the same fields:

```sh
octool-cli amd-granite-map --table-version 0x620205 --presence-mask 3 \
  --core-mask0 0 --core-mask1 0x81 --smt on
```

This is a **synthetic example**, not a capture of a 9950X3D. It produces two
PM CCD slots, 16 fixed PM core positions, one active command target and two
enabled core positions. PM core positions 8 and 15 refer to firmware target 1,
core-mask bits 0 and 7. Their diagnostic command indexes are respectively
0/1 and 2/3. The target/core/thread encodings are 32/33 and 46/47. The checked
command consumer ignores the thread bit, so each sibling pair aliases a core.
No hardware write command is generated.

All reported slot/rank/index numbers are zero based. The full JSON preserves
the supplied 32-bit inputs, emits all 64 remap entries (`null` for a hole), and
sets `input_provenance: caller-supplied`, `live_capture: false` and all runtime,
physical, APIC, OS binding and write-verification flags to false. Saving or
copying the result does not upgrade those claims. GUI input changes clear the
old table and disable copying until recalculation.

## Input source and bit treatment

These coordinates come from the existing [command-remap study](amd-granite-slot-binding.md)
and a new original-byte replay. They are **firmware coordinates**, not host
physical addresses or a supported generic register-read interface.

| Input | Reference firmware source | Treatment |
|---|---|---|
| Presence mask | SMU `0x1e22c(0x1df6,2)` | Two bits; SMU-local word `0xf885d3bc`, bits 23:22; a separate UEFI query reads SMN coordinate `0x5d3bc` at the same bit positions |
| Target 0 core-mask word | `0x1db30(0,0x203c003c)` → firmware transport coordinate `0x203c003c` | Store low 16 bits; enumerate bits 0–7 |
| Target 1 core-mask word | `0x1db30(1,0x203c003c)` → firmware transport coordinate `0x20bc003c` | Same treatment; do not compact the input target identity |
| SMT mode | First target whose stored low-16 mask is nonzero, selector `0x203c0030`, bit 10 | Bit clear means two threads, set means one; not queried separately for every target |
| PM table version | Explicit `0x620105` or `0x620205` | Respectively one or two storage slots; must match the presence-mask population |

The new fuse replay executes `0x1e22c` and its actual 64-bit shift helpers
`0x3f858`/`0x3f840`. It confirms that the helper reads the higher SMU-local word
`0xf885d3c0` before `0xf885d3bc`, but this two-bit field does not depend on the
higher word. Peripheral words are fixtures. The separate
[UEFI acquisition study](amd-granite-map-acquisition.md) traces the matching
SMN coordinate through a pinned protocol reader and its PCI index/data
operations. Runtime provider selection and actual register contents remain
unknown, so no host register is read by this diagnostic.

Core masks retain an important three-way distinction:

- Bits 16–31 are discarded by the initializer before any mask decision.
- Bits 8–15 do not enumerate additional cores, but a nonzero bit makes the
  target occupy a compact **command target rank**.
- Bits 0–7 enumerate fixed local PM core positions and compact command core
  ranks. Disabling a core leaves its PM position in place.

An absent target's supplied core-mask word is retained in the JSON but is
marked `input_consumed: false`; the firmware initializer does not read it.
Presence ranks include every present target, even when its mask is zero.
Command ranks include present targets with any nonzero low-16 mask. Thus
presence 3 with masks `[0x100,1]` gives target 1 command rank 1 despite target
0 producing no enumerated core. Treating the low byte as the complete active
target test would give an incorrect result.

Invalid versions, presence bits outside 0/1, no present targets, or a
version/presence-width mismatch return an error and no mappings. Arbitrary
32-bit core-mask inputs remain accepted so that ignored and non-enumerated
bits can be inspected rather than silently erased.

## Evidence and verification

The byte identity is pinned to SMU SHA-256
`5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596`.
REA-assisted Ghidra instruction evidence is read from `smu/smu-study.json`;
the source digest and every decoded instruction byte are checked against the
immutable body. The replay stops the initializer before its hardware-write
loop. No host CPUID, MMIO, mailbox or firmware installation occurs.

```sh
python3 analysis/tools/firmware-granite-map-inputs.py \
  --smu build/firmware-input/smu-62-54-body.bin \
  --study build/firmware-evidence/smu/smu-study.json \
  --output build/firmware-evidence/granite-map-inputs.json
```

The proof covers 292 raw presence-word cases, 108 boundary cases for extraction
across a 32-bit word, and 768 initializer cases with bits in every part of the
core-mask word, comparing 49,152 remap words. It generates 18 synthetic oracle
vectors in `gui/tests/amd_granite_mapping_vectors.h`; optional `--vectors PATH`
recreates that header. The generated header SHA-256 is
`f25bdee211b1846eb22f7b392b3ae1e95c7a51d47346a5e2042f3d4131c36821`.
These expected tables come from firmware instruction execution, separately
from the new C++ mapping implementation. `amd_granite_mapping_core_test --emit`
emits those saved vectors for GUI/CLI parity checks.

Nine core test groups cover those oracle tables, rejected mismatches, sparse
targets, empty present targets, all mask-bit classes, SMT sibling aliases,
every low-byte mask in either target, JSON trust boundaries, and non-default
process locales. Static and synthetic success establishes reference arithmetic
and input handling only. Runtime mask acquisition, installed-BIOS identity,
physical CCD/APIC/OS association and writable command access remain open.
