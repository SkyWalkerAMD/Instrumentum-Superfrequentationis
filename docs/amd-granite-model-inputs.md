# Granite model helper arithmetic and CCD configuration quantization

Reference: ProArt X870E-CREATOR WIFI BIOS 2503, MP5 98.84.0. The input
SHA-256 is `fb5d96c17b909cc4f681523ccc931796e27c7457ba5ad637a3d4fce1c78ce556`.
Addresses below are MP5-local coordinates, not host addresses or physical core
identities. This extends the [producer paths](amd-granite-producer-paths.md)
and [transfer paths](amd-granite-transfer-paths.md).

The new replay computes the exponential/power helper results from byte-checked
instructions and the reference table. It connects those results to core row 0
and CCD rows 0/1 without supplying arbitrary model-helper outputs. Calibration
coefficients and inputs are synthetic; **no new physical unit is established**.
GUI tooltips and CLI `producer_note` use the same decoder descriptions. Raw PM
values, unit labels and hardware-write permissions do not change.

## Exponential table and power interpolation

`0x24c20` selects a row in the 41-entry table at `0x18660`, using
`min(trunc(float32(abs(x) + 0.5)), 40)`. Each 24-byte row contains its integer
center followed by the coefficients corresponding to `exp(center)` divided by
`1, 2, 6, 24, 120`. The first coefficient is both the constant term and the
linear coefficient: the loop's postincrement load reads it again. Five terms
in the remainder are accumulated in instruction order. Negative arguments
invoke reciprocal helper `0x327b0` on the positive result.

The table index is capped; the result is not clamped. Inputs beyond 40.5 use
the last polynomial outside its nearest-center interval. The 1,453 finite
fixtures include table centers, binary32 neighbors of all half-integer
boundaries, and values beyond the cap. Within `abs(x) <= 40.5`, the largest
sampled relative difference from host `exp` is about `3.355e-5`. That is a
sampled comparison, not a bound over all arguments or an implementation of
the firmware reciprocal approximation.

`0x24c70` multiplies the base `n-1` times for `n = trunc(abs(exponent))`,
starting with the base, then linearly interpolates toward the next power.
Its signed `LOOPGTZ` skips the loop when `n=0`; in particular, exponent zero
returns the base, not one. Negative exponents invoke the reciprocal helper.
The 136 fixtures cover eight positive bases and 17 exponents, including eight
zero-exponent cases. Substitution with host `pow` would change behavior.

## Model helpers and their callers

Let `E` denote the firmware exponential above and `P` its power interpolation.
The following formulas describe the paths, with binary32 rounding retained at
the actual instruction boundaries in the replay:

| Entry | Arithmetic | Relevant caller |
|---|---|---|
| `0x32684` | `scale * c2 * E((c4+c3*v)*(t-c5) + v*(c0+c1*v))` | CCD row 0 at `0x2cccd`, CCD row 1 at `0x2cde6` |
| `0x2bfe8` | Combine the preceding exponential model evaluated at `v` and at a second input, using `activity` and `(1-activity)*(1-suppression)` | Core row 0 at `0x2c7ff`; suppression from `0x12e24` |
| `0x326bc` | `P(v,c3) * c0 * E(c1/(t+c2))` | Core auxiliary state at `0x2c7e8`, CCD auxiliary state at `0x2cce7` |

The last helper writes parallel state: core record `+0x1c` and CCD address
`0x12808`. These values do **not** feed the row-0 expression in the checked
slices. Sharing a producer function does not establish a direct dataflow edge.

There are 162 simple-model, 162 blended-model and 24 power-temperature fixtures.
A further 288 compositions execute the caller argument loads, computed helper
results and output stores, checking 864 row values plus both auxiliary states.
This covers different activity, temperature, suppression, ceiling and synthetic
coefficient choices. It also preserves the CCD 1 fused multiply-add ordering.
Calls cross explicit `f0` result boundaries; full register-window execution,
the entire scheduler and physical coefficient calibration are not emulated.

## CCD row 2 uses two different conversions

The previously checked initializer converts byte `0x802a` into float `0x8030`
using base `0.4975340962409973` and step `0.0029591694474220276`.
The later command path does not use that conversion to reconstruct the float.

MP5 command `0x75` (`0x2beb4`) expands each unsigned 16-bit half by binary32
`0.001`; a zero upper half reuses the lower half. It stores the two original
floats at `0x119ac/0x119b0`. Helper `0x24984` then quantizes each separately:

```
offset = -0.4974510073661804  // bits 0xbefeb1e6
gain   = 337.989013671875    // bits 0x43a8fe98
code   = low8(trunc(float32(float32(value + offset) * gain)))
```

For the tested nonnegative, representable conversion results, the caller
stores only the low byte; it does not clamp the result to 255. The 80 command
fixtures include 47 such truncations across the two results. This is arithmetic
evidence, not a supported electrical range. Negative/invalid unsigned float
conversions and floating exceptions remain outside the replay.

The selector at `0x2a494` chooses the second pair only when `float[0x11428]`
is strictly greater than threshold `float[0x119b4]`; equality selects the first
pair. It writes the selected code and original float to `0x11725/0x11728`.
There are 320 selector fixtures. The increase/decrease gates at
`0x27c6f/0x27fa4` compare the byte against the previous record's byte. All 320
equal-code cases retain the previous `0x8030`, including when an incoming
float differs. Thus applying the initializer's byte-to-float formula to every
later exported value would be incorrect. This remains a configuration mirror,
not a sensor value or independent hardware setting readback.

The [host routing and consumer follow-up](amd-granite-config-routing.md) connects
RSMU 72h to this MP5 75h path. The two inputs are broadcast alternatives selected
by a local threshold, not separately addressed rails. Host acknowledgement does
not validate a target success code or provide device readback.

## Reproduction and limits

Run after the pinned input extraction and Granite instruction study:

```
python3 analysis/tools/firmware-granite-model-inputs.py \
  --mp5 build/firmware-input/mp5-62-54-body.bin \
  --study build/firmware-evidence/mp5/smu-study.json \
  --output build/firmware-evidence/granite-model-inputs.json
```

The REA workflow includes this replay along with the previous 13 proofs. REA
6.7.0 analyzes the two UEFI transport modules; the Xtensa slices use the pinned
Ghidra decoder and explicit replay. REA does not execute these Xtensa models.
The report separately records 2,428 reciprocal result fixtures. Reciprocal
refinement, floating exceptions, peripheral programming, live timing and device
effects are not reproduced. The finite Python arithmetic model is not a
complete binary32 FPU emulator.

The generic instruction interpretation is cross-checked against QEMU 9.0:
[Xtensa translation](https://github.com/qemu/qemu/blob/v9.0.0/target/xtensa/translate.c)
defines the four `CONST.S` values and the signed `LOOPGTZ` condition;
[floating helpers](https://github.com/qemu/qemu/blob/v9.0.0/target/xtensa/fpu_helper.c)
implement `MSUB.S` as accumulator minus product. This supports the operation
selection, not a claim of complete AMD FPU equivalence or physical units.

Eight rows still lack physical units: core **0/4/12**, CCD **0/1/2/6/8**.
PM-to-physical-CCD/OS-core binding and the second rail's physical identity,
independent write and device readback remain unresolved. The user's machine
is unavailable and its installed BIOS is unknown. See the latest
[completed validation](amd-granite-validation.md); local replay alone is not a
passed cloud or hardware result.
