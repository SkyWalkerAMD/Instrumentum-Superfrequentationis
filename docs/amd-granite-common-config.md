# Granite common configuration mirrors and fixed firmware targets

Reference: ASUS ProArt X870E-CREATOR WIFI BIOS 2503, SMU 98.84.0, SHA-256
`5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596`.
The user's machine is unavailable and its installed BIOS is unknown. These
findings use REA/Ghidra evidence and bounded original-instruction replays with
synthetic inputs; no device access or physical acceptance is claimed.

GUI and CLI now expose five common configuration values. The earlier
CLDO_VDDG_IOD/CCD and CLDO_VDDP labels were candidates, not verified physical
identities. They are replaced by source-based names, and the missing target-1
positions are added. Common fields increase from 39 to 41.

| PM float index | Shared label | SMU-local source | Export |
|---|---|---|---|
| 259 | Firmware target 0 configuration A | `0xa55c` | Averaged |
| 260 | Firmware target 1 configuration A | `0xa564` | Averaged |
| 261 | Firmware target 0 configuration B | `0xa56c` | Averaged |
| 262 | Firmware target 1 configuration B | `0xa574` | Averaged |
| 269 | Local configuration C mirror | `0xc27c` | Direct copy |

All five units remain **unverified**. Firmware targets 0/1 are fixed source
positions, including in a one-slot PM layout. They are not compact PM CCD
slots, verified physical CCD identities, OS CPU numbers or two addressed rails.
The [command remap study](amd-granite-slot-binding.md) separately shows why
per-core command indexes cannot be copied from PM or APIC coordinates.

## Target acquisition and threshold selection

SMU `0x2960c` prepares two three-byte records per firmware target:

- A reads selectors `0x1895a816/0x1895a818` for target 0 and
  `0x18a5a816/0x18a5a818` for target 1 through `0x2b70`. It stores each reply's
  bits 15:8 and the second reply's low three bits.
- B obtains MP5 message 76h and stores the reply's low three bytes, without
  applying the three-bit mask to its third byte.

These are transport arguments and synthetic reply checks, not a proof of
physical register identity or device success. The ten acquisition cases vary
both targets and unrelated high bits.

Helper `0x29678` converts the first or second byte using binary32 constant
`0x3bd999ba` (approximately 0.006640640087425709). It chooses the second byte
only when reference frequency `float[0x96a0]` is strictly greater than
`1600 - 50 * third_byte`; equality selects the first byte. The scale resembles
a voltage encoding, but that resemblance does not establish volts or CLDO
identity. The proof checks 6,144 selection cases and all 256 possible tag bytes.

Caller `0x27dc4..0x27df7` updates the two sources for a target only when the
sample counter is nonzero and its presence-mask bit is set. In contrast,
`0x2ccad..0x2cd14` accumulates both fixed targets regardless of presence.
Normalization `0x2ce9a..0x2cef0` divides by sample count; pointer copy
`0x2d13a..0x2d14e` exports the four results. The 36 full export cases check 144
words for 1/2/4 samples, all presence masks and three reference frequencies.
An absent target can therefore contribute an old value. These fields do not
carry the target-presence mask; their existence or finiteness cannot establish
that the corresponding target is present or fresh.

## The local mirror uses a different update path

Initializer `0x321e0` reads three local configuration records, calls conversion
`0x1ca2c`, and stores their converted codes. For `0xc27c`, conversion is one
binary32 multiply-add using base bits `0x3efeb1e6` and step bits `0x3b41e659`.
All 256 codes and 15 initialized records are checked. The raw local reads are
explicit fixtures; physical units and electrical ranges remain unverified.

Step setter `0x32220` quantizes its input through `0x1ca44`, moves the current
code toward the target one code at a time, calls local writer `0x1c9c4`, and
updates the software float. It invokes the delay helper with argument 25 after
each step; no time unit or applied-device effect is asserted. It does not call
the raw reader. Equal old/new codes skip the loop and retain the previous
float. Twenty-five transitions include ten decreasing paths, five equal-code
retentions and 3,052 synthetic writer calls. This retention statement is
specific to the step setter: the separate `0x32298` path also writes this
mirror and is not covered by the transition experiment.

The PM pointer table directly copies `0xc27c` to word 269 using `LSI`/`SSI`
floating-register loads and stores, with no intervening FP arithmetic. Five
raw-bit fixtures include negative zero, subnormal and signalling-NaN bits. Firmware
does not average or reconvert this word on that path. The shared application
decoder also preserves the stored value and classifies non-finite bits without
changing them.

This copy experiment uses opaque 32-bit register words rather than Python
floating values. The first cloud run exposed a host-runtime difference:
converting a signalling NaN through Python's float can quiet its bits on Linux.
That is not part of the firmware's load/store operation. The dedicated replay
rejects arithmetic instructions and avoids host floating conversion entirely;
the general numerical interpreter still makes no NaN/FPU-exception claim.

## Shared GUI and CLI behavior

The GUI adds **Granite Ridge configuration mirrors** to the PM display menu.
The same five fields appear in `amd-pm-decode` and native PM capture JSON.
Producer notes distinguish averaged target values from the direct local
mirror; tooltips identify SMU-local reference addresses. JSON records the
firmware target and configuration kind only for A/B, and explicitly leaves
presence, physical unit, physical identity, freshness and readback unverified.

Import reconstructs this metadata from supported layout/raw bytes. Forged
units, producer addresses, target numbers, presence flags, mapping claims and
setter-readback claims are ignored. Both layouts receive tests for all five
values and exceptional float encodings; no BCLK scaling or clamping is applied.
No write command or hardware-access permission is added.

The [rails follow-up](amd-granite-rail-routing.md) also clarifies the existing
three command mirrors' startup origin in local controller status bits. The
[timer follow-up](amd-granite-unit-anchors.md) establishes integer deadline
behavior but cannot anchor absolute Hz or the remaining eight physical units.

## Replay and instruction interpretation

```sh
python3 analysis/tools/firmware-granite-common-config.py \
  --smu build/firmware-input/smu-62-54-body.bin \
  --study build/firmware-evidence/smu/smu-study.json \
  --output build/firmware-evidence/granite-common-config.json
```

The decreasing setter revealed a text-rendering trap: Ghidra prints instruction
bytes `7c f5` as `movi.n a5,0xff`, while the encoded immediate is -1.
The bounded interpreter now decodes MOVI.N from checked bytes, with 2,048
additional synthetic ISA cases covering all 128 encodings and 16 registers.
The asymmetric -32..95 range and encoding follow Cadence's
[Xtensa ISA summary, MOVI.N](https://www.cadence.com/content/dam/cadence-www/global/en_US/documents/tools/silicon-solutions/compute-ip/isa-summary.pdf).
This does not turn the bounded interpreter into a full CPU/FPU emulator.
Completed local/cloud comparisons and publication are recorded in
[validation](amd-granite-validation.md).
