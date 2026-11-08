# Granite local configuration C: host route, raw port and mirror widths

Reference: ASUS ProArt X870E-CREATOR WIFI BIOS 2503, SMU 98.84.0, SHA-256
`5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596`.
The user's 9950X3D is unavailable and its installed BIOS is unknown. This study
reuses REA/Ghidra's checked instruction inventory and replays selected original
instructions with synthetic RAM and scripted replies. It does not observe a
processor or establish a physical unit.

The [common configuration study](amd-granite-common-config.md) identified
PM word 269 as a direct copy of software float `0xc27c`. The new experiment
connects its host setter and demonstrates a second reason that this mirror
cannot be treated as applied-setting readback: the loop target and the byte
submitted to the local port have different widths.

## Host command 73h and the byte boundary

| Stage | Byte-checked behavior |
| --- | --- |
| RSMU table `0x6fa4 + 8*0x73` | Handler `0x1dca0`, flags `0x206` |
| Handler argument | Convert unsigned 32-bit argument to binary32 and multiply by bits `0x3a83126f`, approximately 0.001 |
| Quantizer `0x1ca44` | Truncate `(input - 0.4974510073661804) * 337.989013671875` to unsigned 32-bit, subject to binary32 rounding |
| Step loop `0x32220` | Compare and advance the complete 32-bit code toward that target |
| Local write | Pass only the advanced code's low eight bits to `0x1c9c4` with selector 0 |
| Stored byte `0xc271` | Store those low eight bits |
| Software float `0xc27c` | Convert the complete advanced code through `0x1ca2c`, without the byte truncation |
| Delay | Call `0x2f1c` with argument 25 after each step; its physical time unit is not established here |
| Host completion | Reply 1 after the loop; the selected handler and loop do not call a reader |

There is no payload-range guard in the selected handler and step function.
This is a claim about those functions, not the entire firmware or electrical
acceptance. The dispatcher and mailbox helper boundaries remain explicit
fixtures. A factor of 0.001 does not prove millivolts, volts or CLDO identity.

For an old byte of 255, the original instructions produce these examples:

| Host argument | Target code | Submitted byte | Software mirror | Conversion of submitted byte | Writes |
| --- | --- | --- | --- | --- | --- |
| 1252 | 255 | No write | Previous float retained | 1.2519134283 | 0 |
| 1255 | 256 | 0 | 1.2548720837 | 0.4974510074 | 1 |
| 1500 | 338 | 82 | 1.4974834919 | 0.7400624752 | 83 |
| 2000 | 507 | 251 | 1.9974998236 | 1.2400786877 | 252 |

All four cases return acknowledgement 1. The columns are unitless firmware
values. The last conversion is an analytical comparison, not a device reading.
The mismatch proves that a finite PM mirror alone cannot certify even the
submitted byte, much less an applied voltage. Equal-code retention remains a
separate behavior, including when a previous float does not match that byte.

The experiment covers 98 complete handler/loop cases, including eight
equal-code retentions, 42 targets above 255 and 17,797 synthetic port writes.
Five additional cases stop at the setter call site. Large arguments reach very
large quantized targets; their full loops are not executed. Arguments 0 and
497 reach negative quantizer inputs, whose FPU exception/result semantics
are deliberately not modeled. These observations are not safe input ranges.

## Local raw read is distinct from the setter

The three initialized local records have the following literal descriptors:

| Record | Target mask | Local port |
| --- | --- | --- |
| `0xc270` | `0x0002` | `0xf885a908` |
| `0xc284` | `0x0009` | `0xf885a90c` |
| `0xc298` | `0x0008` | `0xf885a90c` |

These are firmware-local addresses and masks. They are not independently
identified physical rails, host SMN addresses or a portable host register API.

Writer `0x1c9c4` submits `selector | code << 4 | target_mask << 12` and returns
without polling a completion word. The raw leaf itself does not mask the
selector or code to their intended widths; the step caller masks its code.

Reader `0x1c9ec` submits `(selector | 8) & 255` plus the lowest set target-mask
bit shifted by 12, polls bit 23 of that same port, and returns bits 31:24.
For record `0xc284`, the writer uses mask 9 while the reader selects mask 1.
The selected polling loop has no timeout. The stalled-response fixture is
stopped by the test's instruction budget, not a firmware timeout.

The replay covers 6,144 writes, 12,288 scripted reads, three oversized leaf
encodings and one stalled reader. It does not connect the raw reader to a
dedicated host getter. Neither a host write/read pair nor a physical
interpretation of that returned byte has been verified. The presence of a
local raw-read primitive therefore does not justify reporting the 73h reply
or PM word 269 as device readback.

## A separate initialization sequence narrows before conversion

Function `0x32298`, called from the selected `0x2531c` initialization paths,
uses a different update sequence. It quantizes a fixed intermediate float
with bits `0x3f8ccccd` (approximately 1.1) to code 203, then issues six writes:

1. Selector 4: old mode-4 byte with bit 3 set.
2. Selector 0: intermediate code 203.
3. Selector 3: old mode-3 byte with bit 6 cleared.
4. Selector 4: the value from step 1 with bits 1 and 2 cleared.
5. Selector 0: the requested code's low eight bits.
6. Selector 4: old mode-4 byte with bits 1, 2 and 3 cleared.

It then updates the stored bytes and converts the **truncated byte** into
`0xc27c`. In contrast to 73h's step loop, it always executes these six writes
and overwrites the mirror, including equal-byte cases. The 252 cases include
63 equal-byte rewrites and 108 oversized targets. The selected sequence
contains no raw read, completion poll or delay call. It is not evidence that
the writes take effect in the same order on a real peripheral.

## Reproduction and use

```sh
python3 analysis/tools/firmware-granite-local-config.py \
  --smu build/firmware-input/smu-62-54-body.bin \
  --study build/firmware-evidence/smu/smu-study.json \
  --output build/firmware-evidence/granite-local-config.json
```

The complete raw-reader replay executes the unsupported integer `NEG`
instruction as a separately byte-checked two's-complement operation between
two interpreter slices. It does not replace disassembly or alter the shared
interpreter. This operation agrees with QEMU v9.0.0's `translate_neg` in
[the Xtensa translator](https://github.com/qemu/qemu/blob/v9.0.0/target/xtensa/translate.c).
Floating experiments use finite, representable inputs; NaN, conversion
exceptions and device behavior remain outside this proof.

The shared GUI/CLI decoder can expose this route as reference protocol
metadata for PM word 269, including the 32-bit target, eight-bit submission,
possible mirror mismatch and unavailable applied readback. No physical unit,
safe electrical range, native write permission or second-rail support follows
from this evidence. The remaining eight row units and five configuration
units therefore remain unverified.
