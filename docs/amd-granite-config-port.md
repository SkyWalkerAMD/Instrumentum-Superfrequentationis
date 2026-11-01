# Granite MP5 configuration ports and retained PM values

Reference: ASUS ProArt X870E-CREATOR WIFI BIOS 2503, MP5 98.84.0, SHA-256
`fb5d96c17b909cc4f681523ccc931796e27c7457ba5ad637a3d4fce1c78ce556`.
This continues the [configuration-route study](amd-granite-config-routing.md)
using the pinned REA/Ghidra evidence and bounded original-instruction replay.
No target hardware executes these experiments. Timer values and peripheral
replies are explicit fixtures; all addresses below are MP5-local addresses.

## Local writer and raw reader

| Entry | Byte-checked behavior | Limit |
| --- | --- | --- |
| `0x255d0` | Write `uint32(code << 4) \| (selector & 7)` to `0x18c0064`; write mask, then zero to `0x18c006c` | No device completion polling or returned device payload |
| `0x22e4` | After each of those three writes, wait for three raw ticks of timer `0x32001c0`, using unsigned subtraction | Tick frequency and real elapsed time remain unverified |
| `0x255a8` / `0x255bc` | Forward to that writer with masks `0xff` / `0x30000` | The first argument is overwritten in the writer; it does not select another rail |
| `0x25604` | Write `8 \| (selector & 7)` to `0x1bc0064`, write mask to `0x1bc006c`, poll bit 8 at `0x18c0068`, clear the trigger and return bits 7:0 | Raw byte only; no verified host getter route, physical unit or write/read pairing |

The writer covers 6,144 selector/code/mask combinations, 16 wrapper cases and
three oversized encodings. The real delay instructions run against scripted
normal and wrapping timers. The reader covers 6,144 selector/reply/delay
combinations and verifies that unrelated status bits are not returned. A
stopped timer and a missing reply flag each exhaust the test's bounded
instruction budget. That budget is not a firmware timeout; the checked raw
reader loop has no timeout.

The 0..255 grid is an arithmetic experiment, not a safe electrical range.
The raw reader's presence does not establish that it reads back a setting
written by this writer. The discovered-function inventory has no direct
caller for `0x25604`; discovery is incomplete and cannot rule out an indirect
route. A reachable host command, addressing meaning, physical units and
device behavior are all still needed before exposing such a getter.

## A changed code can retain the previous float

The previous replay covered the ordinary increasing/decreasing-code gates in
transition `0x27b54`. This follow-up executes its other branch, including
accessor `0x26820` and the complete 216-byte record copy:

| State at `0x7820` | Stored code `0x802a` | Exported float `0x8030` |
| --- | --- | --- |
| Byte +0 is zero | Unchanged | Unchanged |
| Byte +0 is nonzero and either byte +3 or +4 is nonzero | Incoming record byte +9 is copied | Previous float is retained, including when the code changes |

The alternate branch copies the incoming record and updates enabled core
state, but does not execute a local configuration-port write or raw read.
The 126 cases vary the two branch bytes, enable state, old/new code and core
mask; 45 cases retain the old float despite a changed stored code. The
meanings of the two branch bytes have not been established. No sleep-state,
physical CCD or rail name is assigned to them.

CCD row 2 is an average of this software source. A host acknowledgement,
changed code or copied record therefore cannot independently demonstrate
that a new setting was applied or measured. GUI and CLI share this finding:
the GUI shows retention explicitly, JSON reports the unverified readback
boundary, and capture imports recompute these facts from the supported
layout instead of trusting supplied metadata.

## Reproduce

```sh
python3 analysis/tools/firmware-granite-config-port.py \
  --mp5 build/firmware-input/mp5-62-54-body.bin \
  --study build/firmware-evidence/mp5/smu-study.json \
  --output build/firmware-evidence/granite-config-port.json
```

The REA workflow runs this proof after the existing configuration-route
replay. [Validation evidence](amd-granite-validation.md) identifies completed
cloud runs. The replay pins the input hash and checks each executed instruction
against its original bytes; unexpected calls, addresses or reply consumption
fail the experiment. Synthetic MMIO records do not touch host hardware.

No new physical unit is assigned. Core rows **0/4/12**, CCD rows **0/1/2/6/8**,
PM-slot to physical-CCD/OS-core binding, and independent second-rail write and
device readback remain unresolved. The native command allowlist is unchanged.
