# Granite Ridge frequency models and CCD counter units

Reference: ASUS ProArt X870E-CREATOR WIFI BIOS 2503, SMU/MP5 98.84.0.
The 9950X3D machine is unavailable and its installed BIOS is unknown.
The new evidence uses REA-assisted static analysis and byte-checked instruction
replay with synthetic RAM. It is not hardware acceptance.

## Four additional row units

These names apply only to exact PM layouts 620105/1828 and 620205/2452.
Rows and indices below are zero based; indices refer to binary32 words.

| Storage row | Two-slot float indices | Unit | Interpretation |
| --- | --- | --- | --- |
| Core 3 | 349..364 | GHz | Averaged divider frequency model; disabled slots can retain old values |
| CCD 5 | 599..600 | GHz | Averaged common frequency model |
| CCD 9 | 607..608 | % | Quantized counter ratio; zero denominator retains the previous value |
| CCD 10 | 609..610 | GHz | Averaged fused frequency policy with a policy offset |

Frequency models and policies are not measured clocks, achieved frequencies,
or independent setting readback. Counter event identities are unresolved.
The decoder preserves stored values, including percentages above 100, without
scaling or clamping them. PM storage ordinals do not identify physical CCDs or
OS CPUs. Reference units do not authenticate the source of an imported capture.

## Frequency scale and divider model

The [earlier frequency anchor](amd-granite-performance-policy.md) traces the
documented MHz Fmax interface through input multiplication by 0.001 and the
stored getter's multiplication by 1000. MP5 0x119a0 therefore holds a GHz policy
scale. The new experiment rechecks that anchor for 13 finite inputs.

Initializer 0x295f8 populates the policy arrays from 0x119a0 and 0x119a4.
Selector 0x29468 takes minima/maxima and stores each selected value at
`0x11480 + 0x3c * local_slot`. The replay checks 64 selections, including a
lower policy greater than the upper policy. Other policy override paths are
not exhaustively executed.

Function 0x2a244 converts each enabled selected frequency to an integer code
with `max(trunc(float32(value * 200 + 0.0002)), 120)`. Its maximum code selects
the common divider. Multiplication by 1,000,000, integer division and a final
5e-9 multiplier produce the common GHz model at 0x12878. The caller copies
that float to 0x1287c, the CCD row 5 source.

Per-core integer dividers and a 6-bit interpolation code form a dimensionless
weight: `((64-fraction)/divider + fraction/(divider+1)) / 64`.
Multiplication by the common GHz value produces the core row 3 source at
`0x11a80 + 0x1bc * local_slot`. The proof preserves binary32 operation order;
the expression here describes the scale, not an alternative numeric algorithm.

The 384 model scenarios cover 768 active and 2,304 disabled slot cases,
divisors 1..4, both common-divider branches, integer/fractional paths, and
fraction-code endpoints 0 and 63. Disabled slot values remain unchanged.
Reciprocal results are explicit correctly rounded synthetic fixtures; actual
reciprocal-helper accuracy and peripheral programming are not executed.
Inputs up to 12 GHz exercise arithmetic; they are not a supported tuning range.

## CCD counter percentage

The tail at 0x2cc06 extracts two 18-bit counters from cached words at
0x128f4/0x128f8, subtracts their previous values with one 18-bit wrap, and writes
0x12894. Its formula is:

```text
float32(floor(delta_numerator * 12800 / delta_denominator) / 128)
```

The integer product fits 32 bits for the full 18-bit numerator range.
There is no clamp to 100. With a zero denominator, previous counter states
advance but the previous percentage remains. Separate enable/window gates
can skip the producer. The proof checks 300 ratio cases, including 102 wrapped
cases and 30 zero-denominator cases, plus eight gate/window cases. Cached
counter acquisition and actual event definitions remain outside this proof.

## CCD fused frequency policy

The normal SMU initialization path at 0x29bb0 and the 70h path at 0x29ef3
produce a policy offset by subtracting the fuse-derived GHz baseline from the
accepted GHz limit. The 24-byte record carries the limit at offset 8 and the
offset at 20. MP5 0x2ad20 receives it, runs 0x295f8, then writes
`float32(offset + fuse_code * 0.005f)` to 0x11440, the CCD row 10 source.

The proof covers 32 initial records and 160 replacement-record/fuse cases.
Fuse return values and interprocessor transport are explicit fixtures. The
alternate SMU lookup-table policy branch is not executed. This establishes a
frequency-policy unit, not a new runtime setter or a live Fmax measurement.

## Export, frontend behavior and remaining work

Nine replay scenarios accumulate 1, 2 or 4 samples, then execute normalization
and complete PM scatter for the one-slot layout and both two-slot ordinals.
All 99 new-unit output words preserve their expected averages without an
additional scale. Producer and export experiments use the same checked
addresses; peripheral timing, atomicity and freshness are not established.

GUI and CLI share the decoder. JSON marks model rows `frequency_model: true`,
the fused policy `fused_frequency_policy: true`, and all GHz rows
`measured_frequency: false`, `freshness_verified: false` and
`setting_readback_verified: false`. CCD row 9 carries
`counter_meaning_verified: false`. Import rebuilds labels and flags from raw
bytes; edited metadata does not grant trust.

There are now 13 core rows and six CCD rows with physical/dimensionless unit
evidence, plus two core rows and one CCD row identified only as averaged codes.
Core rows **0, 4, 12** and CCD rows **0, 1, 2, 6, 8** still have unknown units.
The [producer follow-up](amd-granite-producer-paths.md) verifies their arithmetic
tails, configuration updates and shared-parameter paths without promoting a
physical unit. It supersedes the discovery leads below where applicable.
Seven common candidate fields, the meanings of the three code rows, physical
PM-to-CCD/OS-CPU mapping and second-rail independent write/readback remain open.

The next static trace leads are recorded below. These are producer/consumer
locations, not additional verified units; the new executable proof does not
cover their complete upstream data paths.

| Unresolved row | MP5 source / candidate producer | Missing evidence |
| --- | --- | --- |
| Core 0 | `0x11a28 + 0x1bc * slot`; 0x2c798 stores at 0x2c86c | Model coefficient and input scales; a product alone does not establish watts |
| Core 4 / 12 | `0x11b0c / 0x11b10 + 0x1bc * slot`; stores at 0x2c569 / 0x2c5ea | Counter clock domains and scaling inputs; cannot inherit the row 3 model unit |
| CCD 0 | 0x12804; producer 0x2ccb8, store 0x2cd11 | Polynomial/table inputs and multiplier scale |
| CCD 1 | 0x12848; producer 0x2cdc8, store 0x2ce25 | Table output, correction term and source 0x8030 scale |
| CCD 2 | 0x8030, also consumed by 0x2cdc8 / 0x2ce2c | Incoming source identity and unit |
| CCD 6 | 0x128a8; store 0x2cb67 | Counter/window reference and the meaning of the 0.1 multiplier |
| CCD 8 | 0x12888 times 0x1302c divided by 65536 at PM export | Both source scales and the final multiplier; the counter producer alone is insufficient |

Reproduce with [the checked replay](../analysis/tools/firmware-granite-clock-models.py)
and the [REA workflow](../.github/workflows/rea-granite-study.yml).
See [validation](amd-granite-validation.md) for the latest completed cloud run.
