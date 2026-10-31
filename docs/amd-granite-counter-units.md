# Granite Ridge counter units and final PM export

This study uses the pinned ASUS ProArt X870E-CREATOR WIFI 2503 / SMU and MP5
98.84.0 reference from [the input study](amd-granite-pm.md). The user's 9950X3D
machine is unavailable, its installed BIOS is unknown, and no real counter or
voltage measurement was taken. Both GUI and CLI share the corrected decoder.

## Corrected CCD row 11

The earlier study stopped normalization at MP5 0x2f5af, before the final scatter.
It correctly found a ratio at 0x19cfc but incorrectly carried that unit into the
PM output. Instructions 0x2f5cc / 0x2f5cf / 0x2f5d8 load accumulator +0x26c,
multiply by the still-live 100.0 constant, and store it before export. This runs
once before the 18-row copy loop, not once for each row.

**CCD row 11 is percent, not ratio.** A producer ratio of 0.25 becomes 25 in
the PM table. OCTool displays stored 25 as 25%, without multiplying or dividing
it again. The underlying event remains unidentified, so this is not labeled
CPU utilization, L3 temperature or CCD power. Old imported captures have their
labels and units rebuilt from raw bytes; stored values remain unchanged.

The current evidence supersedes the old ratio label in the historical reports
for commits 091c1b4 and edc8a7d. Those immutable validation records are retained.

## Five additional core rows

Rows and storage slots are zero based. The base state is
`0x11a18 + 0x1bc * firmware_core_slot`.

| Core row | Unit | State offset | Traced behavior |
| --- | --- | --- | --- |
| 13 | ratio | +0x108 | First bounded partition of the C0 candidate fraction |
| 14 | ratio | +0x104 | Bounded remainder after subtracting that partition |
| 15 | % | +0xe8 | Counter A times 100 divided by the reference counter delta; capped at 100 |
| 16 | % | +0xec | Counter B with the same denominator and cap |
| 17 | % | +0xdc | `float32(floor(delta_num * 12800 / delta_den) / 128)`; no 100% cap |

Rows 13/14 pass through a policy-selected combination of bounded counter
fractions before partitioning the C0 candidate. The policy and event identities
remain unknown; the application uses neutral counter names. Rows 15/16 follow
the zero-reciprocal branch when their reference delta is zero. Row 17 instead
retains its previous value when its denominator is zero, while updating the
numerator baseline. These behaviors do not establish live sample validity.

Row 17 uses two 18-bit counters. Wrapping deltas add 0x40000. Integer scaling
has a 1/128-percentage-point step before binary32 rounding; large results can
round more coarsely. Values over 100 remain visible, rather than being silently
clamped by the application. This is a counter ratio, not a verified load metric.

The shared accumulation loop averages these five rows without further scaling.
JSON includes `counter_meaning_verified: false` and `freshness_verified: false`
for these rows and CCD row 11. GUI status likewise keeps counter identity
unverified. Raw bits, non-finite values and firmware slots remain available.

## Reproducible evidence

[`firmware-granite-counters.py`](../analysis/tools/firmware-granite-counters.py)
checks the exact image hash and every executed instruction byte. It covers:

- 18 complete normalization/export cases, 2,808 output words, one/two CCD
  storage slots and sample counts 0/1/2/4/8/16. No floating instruction between
  the reciprocal return and the final PM writes is skipped. A zero sample
  count follows the firmware's factor-one path; that is not a valid-sample claim.
- 750 event-percentage cases, including 225 wrapping-counter cases and 150
  zero-denominator cases.
- 384 partition cases across both policy branches and three thresholds.
- 300 quantized-percentage cases, including 102 wrapping cases and 30 cases
  that retain the previous value on a zero denominator.
- Eight source-to-accumulator-to-export cases checking 40 newly interpreted
  core words through four sample accumulations each.

Reciprocal instructions/helpers are explicit exact-factor fixtures. Their
approximation error, floating-point exception behavior and zero-time behavior
outside the selected slices are not emulated. MMIO output addresses are keys
in a Python dictionary. Physical CCD/APIC/OS CPU mapping is not established.

The existing [semantics replay](../analysis/tools/firmware-granite-semantics.py)
also now follows the entire export path, including the CCD percentage multiply.
GUI, portable-core and CLI regressions verify the units, unmodified raw values,
values above 100, old-label import, non-finite words and unverified metadata.
See [the batch verification report](amd-granite-validation.md) for actual run
results and the remaining hardware boundaries.
