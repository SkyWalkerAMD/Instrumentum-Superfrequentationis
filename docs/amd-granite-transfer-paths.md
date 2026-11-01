# Granite Ridge producer addresses and descriptor paths

The ASUS ProArt X870E-CREATOR WIFI 2503 MP5 98.84.0 reference now has a
byte-checked path from the remaining PM producers to their local RAM and
counter-register coordinates. GUI tooltips and CLI JSON expose the same
reference information. **No additional physical unit is verified by this work.**

The target 9950X3D machine is unavailable and its installed BIOS is unknown.
All replay inputs are synthetic; no firmware, driver or register operation is
executed on hardware. See [the pinned inputs](amd-granite-pm.md) and
[the preceding producer study](amd-granite-producer-paths.md).

## Shared GUI/CLI reference coordinates

| PM row | Producer RAM address | Counter register addresses |
|---|---|---|
| Core 0 | `0x11a28 + 0x1bc * local_slot` | Model output, no counter address asserted |
| Core 4 | `0x11b0c + 0x1bc * local_slot` | `0x1600134 + 0x20000 * local_slot` |
| Core 12 | `0x11b10 + 0x1bc * local_slot` | `0x160013c + 0x20000 * local_slot` |
| CCD 0 | `0x12804` | Model output |
| CCD 1 | `0x12848` | Model output |
| CCD 2 | `0x8030` | Configuration mirror |
| CCD 6 | `0x128a8` | `0x187f134` |
| CCD 8 | `0x12888` | `0x187f170`, `0x187f174`, `0x187f17c`: low word, high word, reconstruction tag |

Every address belongs to an individual MP5's reference address space. Core
`local_slot` is the storage slot modulo eight: core storage slots 0 and 8 in
the two-CCD table have the same local coordinates in different MP5 instances.
These are not host physical addresses, a register-read permission, or
CPUID/APIC/OS identities. The capture does not authenticate the reference BIOS.

JSON adds `reference_address_space: mp5_local`, `producer_source_address`,
`counter_source_addresses` and `reference_addresses_are_host_addresses: false`
only for these eight rows. Import reconstructs them from the version and raw
capture; forged addresses and address-space metadata are ignored. GUI tooltips
show the same coordinates without converting stored values or assigning
unknown units. Existing physical-unit and freshness flags remain false.

## Recovered transfer helper

Static discovery previously missed MP5 `0x317b8`: an earlier speculative entry
claimed a byte inside its actual `ENTRY` instruction. `GraniteSeed.java` now
seeds this direct-call target first, only for the pinned MP5 hash and expected
entry bytes. The recovered instructions contain a three-iteration loop; the
decompiler omits that loop in its C output. The proof follows instructions.

`0x317b8` reads three descriptor-list pointers. For each nonzero pointer it
stores the list address at `0x3270e00 + 0x1000 * channel + 0x208`, writes command
`0x79` at `+0x200`, then records pending state 2 and the list address in RAM.
Zero pointers skip that channel. Eight pointer-presence combinations check
24 synthetic MMIO writes. Submission is not completion; interrupt-mask
instructions are recorded but concurrency is outside this experiment.

Initializer `0x2d18c` supplies lists `0x15c00`, `0x15d80`, `0x15f00` for counter
acquisition, and `0x16080`, `0x16100`, `0x16180` for integrator outputs.
`0x30350` prepares even and odd core lists separately. It marks disabled core
descriptors, moves the list pointer to the first active descriptor and sets the
last active descriptor's termination bit. Empty or globally disabled groups
clear their pointers. The third list has its own `0x3040c` gate.

The replay covers 2,048 preparation cases: all 256 core-enable masks, both
global-enable states, both parities, and one/three descriptors per core. It
checks 3,584 disabled markers and 960 termination markers, plus four third-list
cases. This validates preparation bytes and control flow, not undocumented
controller execution semantics.

The per-core descriptor declares a 0x78-byte span from
`0x160010c + 0x20000 * local_slot` to core state `+0x138`. The row-4 and row-12
inputs at state `+0x160`/`+0x168` therefore correspond to offsets `+0x28`/`+0x30`
in that span. CCD row 8 uses the 16-byte descriptor at `0x15f20`.
The [previous arithmetic replay](../analysis/tools/firmware-granite-producers.py)
continues from producer inputs through averaging and PM export.

## Model aggregation and integration

Thirty-two synthetic model sets replay `0x2ce90..0x2cf35`:

| MP5 RAM | Contents |
|---|---|
| `0x12a2c` | Sum of eight core-row-0 producers |
| `0x12a30` | That sum plus CCD rows 0 and 1 |
| `0x12a6c` | That sum plus CCD row 0 |
| `0x12a70` | CCD row 1 |

Descriptor `0x15960` declares a 0xe4-byte transfer starting at `0x12a2c`.
Startup packet offset `+0x34` supplies SMU aggregate address
`0xa1c0 + 0xe4 * firmware_target`. The 75-case topology replay now checks that
pointer, including a single present target 1: the destination uses the firmware
target, not its compact storage ordinal. Two MP5 patch cases check local
destination aliases `0x130a1c0` and `0x130a2a4`. DMA completion and the SMU
consumer are not emulated.

Core-row-0 output also enters an integer integrator at state `+0xe0/+0xe4`.
The image's initial coefficient is binary32 `0x4283126f` (about 65.536).
The replay checks 128 cases, including 48 low-word carries and high-word wrap.
Eight descriptors route the two words toward `0x19010c0 + 0x20000 * local_slot`.
The coefficient resembles an energy-unit conversion, but neither the update
period in seconds nor this endpoint's MSR identity is established. It therefore
does not certify joules or watts, and the software does not apply that factor
to PM values.

## Reproduction and limits

Run [`firmware-granite-transfers.py`](../analysis/tools/firmware-granite-transfers.py)
with the pinned MP5 body and corrected study:

```sh
python3 analysis/tools/firmware-granite-transfers.py \
  --mp5 build/firmware-input/mp5-62-54-body.bin \
  --study build/firmware-evidence/mp5/smu-study.json \
  --output build/firmware-evidence/granite-transfers.json
```

The REA workflow runs this alongside twelve existing firmware replays.
REA 6.7.0 analyzes two UEFI transports; the pinned Ghidra Xtensa extension and
instruction replays handle SMU/MP5. Neither tool's inferred C is treated as
recovered original source or a substitute for instruction checks.

The callback registration is another boundary for the timebase investigation.
MP5 `0x2d7d8` writes callback pointers into `0x19c10`; the model/integration
callback `0x2c798` occupies slot 6. Dispatcher `0x2d78c` walks 21 callbacks and
records the difference of `0x32001c0` around each call. That difference measures
callback execution duration, not its invocation period. `0x2d7b8` queues the
dispatcher through `0x1e6c`, and its own pointer occurs in the initial command
table at `0x13300`. These static observations do not establish a periodic
one-millisecond update or identify the integrator's destination as an energy
MSR; neither assumption is used by the replay or decoder.

The Windows reference `Tool.exe` has SHA-256
`04a6a6043b9b653737a3e7030ded0cd3f67289575f6959ef8096b0b1a2a1c609`.
At virtual address `0x14007f757`, `cmp eax,0x620205` is followed by `jb` at
`0x14007f75c`: this is a threshold comparison, not an exact-version match.
Its surrounding constructor handles multiple layouts, so its
power/voltage/frequency labels have not been adopted as unit evidence for these
rows. This executable was inspected statically, not executed. Absolute counter
timebase, event identities, model coefficient units,
physical CCD/core mapping and independent second-rail write/readback remain
unverified. No new hardware write is enabled.
