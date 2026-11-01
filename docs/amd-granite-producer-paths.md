# Granite Ridge: the eight remaining row producers

2026-11-01. Reference: ASUS ProArt X870E-CREATOR WIFI BIOS 2503,
SMU/MP5 98.84.0. The user's 9950X3D is unavailable and its installed BIOS is
unknown. This is REA-assisted static research and synthetic instruction replay,
not hardware acceptance. **This update adds producer evidence, not eight newly
verified physical units.** All eight rows retain `unit: "unverified"`.

Rows and float indices are zero based. The exact supported layouts are
620105/1828 and 620205/2452. Two-slot indices below are storage coordinates;
they are not physical CCD or OS core identities.

| Row | Two-slot indices | Recovered source | Still needed for a physical unit |
| --- | --- | --- | --- |
| Core 0 | 301..316 | Corrected model at `0x11a28 + 0x1bc * slot` | Model helper and coefficient scales |
| Core 4 | 365..380 | Counter delta times `100 / elapsed`, then 0.001 | Absolute timer frequency and counter event |
| Core 12 | 493..508 | A different counter delta times `100 / elapsed` | Absolute timer frequency and counter event; no 0.001 multiplier here |
| CCD 0 | 589..590 | Model at 0x12804 | Helper output and polynomial coefficient scales |
| CCD 1 | 591..592 | Corrected model at 0x12848 | Helper output and coefficient scales |
| CCD 2 | 593..594 | Configuration float at 0x8030 | An independently anchored physical unit for the command/configuration input |
| CCD 6 | 601..602 | Counter delta times `0.1 / elapsed` | Absolute timer frequency and counter event |
| CCD 8 | 605..606 | Reconstructed counter, reciprocal parameter scale, then direct parameter scale / 65536 | Counter encoding's physical meaning and absolute time scale |

## Models: core 0 and CCD 0/1

Core tail 0x2c83c..0x2c86c combines helper result `L`, model input `v`,
activity `d`, two coefficients `a,b`, and a shared bound `c`:

```text
dynamic = d * (v * (b + v*a))
total = L + dynamic
core0 = max(0, (c-v)*total) + total*v
```

The replay preserves each binary32 operation, including fused multiply-add;
this compact expression is not a replacement implementation. In particular,
`total * max(c,v)` is not equivalent for negative synthetic model results.
The tail also retains the previous row-0 source at core state +0x18.

CCD tail 0x2ccf0..0x2cd11 computes the analogous `total*v`. CCD tail
0x2cde9..0x2ce25 adds a nonnegative correction, using the configuration float
at 0x8030 as `v`. Its disabled branch retains the previous value. The proof
checks 216 parameter combinations across all three tails and three retained
CCD cases. Helper results are explicit fixtures; exponential/power helpers,
coefficient initialization and physical calibration are not emulated.
A product shaped like power is insufficient evidence for a watts label.

## CCD 2: configuration, not sensor readback

The missed alias was MP5 accessor 0x26820: it returns
`0x7820 + 0x4c10 * argument`. With argument zero, initializer 0x25814 stores
its converted byte at 0x8030. The byte is at 0x802a; the checked conversion is
one binary32 multiply-add with literal values:

```text
base = 0.4975340962409973
step = 0.0029591694474220276
configuration = float32(base + byte * step)
```

All 256 byte values are tested. They are arithmetic fixtures, not a supported
electrical range. This is only the initialization path. MP5 command 75h,
handler 0x2beb4, separately converts two 16-bit arguments by 0.001. A zero
upper argument selects the lower converted value. Later selection copies a
float and code into a configuration record.

The update handler 0x27b54 has separate increasing/decreasing-code branches.
The checked slices at 0x27c6f and 0x27fa4 copy record+12 to 0x8030 when
record byte+9 changes. An unchanged code can retain the old float even when
the new record contains a different float. There are 64 gate cases, including
16 decreases and 32 unchanged-code retentions. Calls that program peripherals,
the full transition sequence, and their success are outside this replay.

The initial byte conversion must not be applied to an already exported value:
the normal update path copies the incoming float instead. The 0.001 factor
and voltage-like constants alone do not independently prove volts. Neither
path is a sensor reading or an independent applied-setting getter.

## Core 4/12 and CCD 6: counter rates

Core acquisition 0x2c1f4..0x2c200 subtracts the previous timer word from
the current value at 0x32001c0 and stores the uint32 difference at state+0xb4.
The producer accumulates elapsed values. At 0x2c4d1, the reciprocal elapsed
factor is multiplied by 100. Core row 4 uses the uint32 delta of state+0x160
against +0x110, then multiplies by 0.001. Core row 12 uses +0x168 against
+0x114 without that final multiplier.

The proof checks 72 rate cases (24 wrapping), three timer-delta cases,
six gate cases that zero both outputs, and one overflow branch that clears
its carry flag while retaining old outputs. Reciprocal values are explicit
fixtures. Zero elapsed and approximate reciprocal/FPU behavior are not modeled.

CCD row 6 subtracts the previous word at 0x128a4 from the word at 0x128a0.
Its arithmetic tail is `float32(float32(delta * 0.1f) / elapsed)`. Acquisition
0x2d59c connects this word to MMIO 0x187f134 and the elapsed state to the same
timer; that peripheral acquisition is not executed. Calling this GHz would
require both a clock-event identity and a timer-frequency anchor.

Additional static timing lead (not an extra replay result): MP5 helper
0x22e4 waits for a raw uint32 difference at 0x32001c0. At 0x29002..0x29015,
its argument is formed from word[0xe448] multiplied by 100000000 through
0x32748, then divided by 1000000 through 0x32440. Poll helper 0x305bc uses
the paired timer reader 0x22c8 and a rounded-up conversion with 100000000
and 1000000000. This is consistent with a 100 MHz timer and microsecond /
nanosecond arguments, but those input units and the peripheral clock binding
are still inferred. The [AMD Model 44h PPR](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/57896.zip)
defines a 100 MHz reference clock; that definition alone does not identify
this MMIO timer or its counter events. These leads therefore do not change
the decoder's unit labels or the verified-unit count.

## CCD 8: shared parameter and averaging order

Let `K` be float[0x7cac]. Initialization 0x2d18c writes
`float32(signed32(1 << shift) * reciprocal(K))` to 0x12e18. Initializer
0x2d818 writes `float32(K * 0.05f)` to 0x1302c for the first state block.
The same helper also initializes a second block; it is not another PM slot.

At 0x2cb5f..0x2cba5 the producer reconstructs a 32-bit counter from low/high
words and a six-bit tag. A tag exponent outside 0..31 falls back to bit zero.
It subtracts the previous reconstructed value with uint32 wrap, multiplies by
the scale at 0x12e18 and 100/elapsed, and writes 0x12888.
The proof checks 288 combinations, including shifts 1/8/16/31, tag limits,
counter wrap and a signed shift-31 conversion. Shift 31 is a boundary fixture,
not a supported tuning setting.

PM accumulation 0x2f4df additionally multiplies by 0x1302c and by 1/65536.
Ignoring all rounding, the two K factors cancel and the combined scale is
`signed32(1 << shift) * 5 / 65536`. This cancellation **does not identify the
physical quantity** and must not replace the firmware's operation order.
The full export experiment changes the multiplier between samples: it proves
that the average contains the average of the products, not the product of
separately averaged operands. Nine one/two-slot export cases check all 261
affected words for 1/2/4 samples.

## Shared GUI and CLI behavior

These eight rows now have descriptive model/configuration/counter names and
`producer_evidence`, `producer_kind`, `producer_note`. They deliberately have
no `unit_evidence`; `physical_unit_verified`, `physical_measurement_verified`,
`freshness_verified`, `setting_readback_verified` and `capture_firmware_verified`
remain false. GUI tooltips expose the same notes; the unit column stays
Unverified. Raw floats, negative values, subnormals and raw invalid bits are
preserved without applying another conversion. Import rebuilds this metadata
from raw bytes and ignores edited labels, notes and trust flags.

Reproduce with [firmware-granite-producers.py](../analysis/tools/firmware-granite-producers.py)
and the [REA workflow](../.github/workflows/rea-granite-study.yml).
There remain 13 core and six CCD rows with physical/dimensionless unit evidence,
plus two core and one CCD code rows. The eight rows above, seven common
candidates, physical PM-to-CCD/OS-CPU binding and independent second-rail
write/readback are still incomplete. See [validation](amd-granite-validation.md)
for the latest completed batch; pending work is not a passed cloud result.
