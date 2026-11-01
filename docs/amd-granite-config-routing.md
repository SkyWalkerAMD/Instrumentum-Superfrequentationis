# Granite CCD configuration routing and model consumers

Reference: ASUS ProArt X870E-CREATOR WIFI BIOS 2503, SMU/MP5 98.84.0.
The [pinned input manifest](validation/granite-creator-2503-input.json) identifies
both images. The user's 9950X3D is unavailable and its installed BIOS is unknown.
All results below are byte-checked instruction replays on synthetic memory.
They do not establish new physical units, an OS-core binding or a second rail.

## Two configuration inputs are alternatives

The earlier [model-input study](amd-granite-model-inputs.md) traced MP5 command
75h to the CCD-row-2 source at `0x8030`. The new replay connects the host-side
route and distinguishes its payload from two independently addressed rails.

| Stage | Checked behavior |
| --- | --- |
| RSMU table `0x6fa4 + 8*0x72` | Handler `0x1dcc0`, flags `0x206` |
| Handler `0x1dcc0` | Forward the unchanged 32-bit argument with MP5 command `0x75` to broadcaster `0x1d690` |
| Broadcaster | For each of the two bits in presence mask `ushort[0x6814]`, clear completion, write the same payload, then write command 75h |
| Target addressing | Payload at `0xf8010998 + (target << 20)`; command at `0xf8010900 + (target << 20)` in the reference's local transport address space |
| Completion | After submitting to all present targets, wait for each completion to become nonzero; no comparison with success code 1 |
| Host reply | Return 1 once those waits finish; no target value is returned |
| MP5 table `0x13298 + 8*0x75` | Handler `0x2beb4`, flags 6; argument helper `0x11b4` |
| MP5 input storage | Expand each unsigned 16-bit half by 0.001; a zero upper half reuses the lower half; store at `0x119ac/0x119b0` and quantize separately |
| MP5 acknowledgement | Helper `0x2e834` writes completion 1 immediately after input storage, before later selection/transition processing |
| Selection `0x2a494` | Use the second pair only when `float[0x11428] > float[0x119b4]`; equality selects the first |
| CCD row 2 | Later transition gates retain the previous `0x8030` when the quantized code is unchanged, even if the incoming float differs |

The active-target mask at `0x6816` is not read by this broadcaster. Present and
active masks are deliberately varied independently in the replay. Sparse
presence routes to the original target number, not a compact storage ordinal.
These numbers still do not identify a CPUID CCD, Linux CPU or physical rail.

The replay covers 1,344 dispatch cases, including 756 present-target cases with
a non-success nonzero completion and a host reply of 1. Empty presence also
replies 1. Two stalled-target cases exhaust the replay's instruction budget:
both commands were already submitted, and the selected firmware loop contains
no timeout. The instruction budget belongs to the test, not the firmware.
Lock helpers and completion words are explicit fixtures, not synchronization
or peripheral emulation.

There are 378 paired-target selector cases, 756 selected values and 756 retained
row values. Each target can select a different alternative from the same
payload because its selector is local. This is not independent addressing of
two rails. The conversion coefficients still lack an independent physical-unit
anchor; the 0.001 scale alone is insufficient to label the values volts.

## GUI and CLI

Both frontends use the shared PM decoder. CCD row 2 now appears as **Selected
configuration; unit unverified** in the GUI. Its tooltip explains broadcast,
threshold selection, retained values and the acknowledgement limitation.
CLI/JSON adds `configuration_protocol` to this row only, with reference command
IDs, input role, target-selection rule and explicit false values for independent
rail addressing, device readback and runtime write enablement.

This describes the pinned reference. It neither certifies the firmware that
produced an imported file nor requests a write. Import rebuilds all derived
fields from raw table bytes, discarding forged protocol, rail, target and
readback claims. Raw values, units and the existing native-read command allowlist
are unchanged. The shared-core, GUI and CLI regressions cover both exact table
sizes and forged metadata.

## Model aggregate is not the adjacent feedback stream

The [transfer study](amd-granite-transfer-paths.md) already traced core row 0 and
CCD rows 0/1 through aggregate `0x12a2c` and descriptor `0x15960`. A second new
replay connects those values to selected SMU consumer instructions:

| Location | Contribution from the three uncalibrated rows |
| --- | --- |
| MP5 aggregate +0 | Sum of eight core-row-0 values |
| MP5 aggregate +4 | That sum plus CCD row 0 and CCD row 1 |
| SMU target base | `0xa1c0 + 0xe4*firmware_target` |
| SMU composite `0xa38c` | Initial other contributors plus both of the preceding aggregate entries from each target |
| Adjacent composite `0xa390` | A separate stream from aggregate offsets +0x1c/+0x20 and other contributors |
| Checked feedback selection | Selects between `0xa388` and `0xa390` when the policy byte equals 1, then applies gain/bias; does not select `0xa38c` |

The 128 paired-target cases and 3,072 feedback-selection cases deliberately
vary the two streams independently. They preserve the actual addition order,
including the two appearances of the core sum. This is a result of the selected
instructions, not a claim that the hardware double-counts measured core power.
The active mask filters a later reduction; it does not filter the first
two-target composite.

The descriptor copy, initial other contributors and the alternate producer
stream are explicit fixtures. Cache maintenance, DMA completion and the full
scheduler are not emulated. A nearby power-limit consumer does not by itself
prove that the eight exported rows have calibrated power/frequency units.

## UEFI APIC construction is a separate coordinate space

The pinned module `9FB8AC55-D266-4F86-AC43-BF2BA74F5A7A` is 34,368 bytes,
SHA-256 `78e0fae1bbe0c4a35d6edaf1e50f0b1c5572066fdf63f9da5b8b59517d93b813`.
REA resolves constructor `0x526c`, CPUID wrapper `0x3a0` and entry `0x4c4`
from exact file offsets. Its caller inventory identifies `0x1844`, `0x2094`
and `0x2d24`, while explicitly warning that two call references into the
function body lack resolved caller entries. It is not a complete call graph.

The constructor reads leaf 0Bh and four subleaves of 80000026h. For synthetic
level types 1, 2, 4, 0, with eight core positions per complex, its result is:

```
(coordinate0 << type4_shift) + coordinate2
  + (coordinate3 << type2_shift) + (coordinate4 << type1_shift)
  + (coordinate5 if leaf0B_shift == 1 else 0)
```

The absent type-3 shift remains zero. A nonzero coordinate2 is accepted by this
selected function, so `(0,0,1,0,0,0)` and `(0,0,0,0,0,1)` both produce APIC ID 1
in the SMT-enabled fixture. In the same fixture `(0,0,0,1,0,0)` produces 16.
These coordinates are numbered arguments, not established physical CCD/core
names. The examples demonstrate why blindly substituting a PM CCD slot is
insufficient; they do not demonstrate incorrect BIOS behavior on valid inputs.

The 256 local instruction replays use Unicorn 2.1.4 and pefile 2024.8.26 with
synthetic CPUID words. Only the constructor and wrapper are permitted to run;
unknown branches/calls fail. No UEFI entry point or hardware instruction is
executed on the host. The proof covers both SMT shifts 0/1, but not execution
of the constructor's callers or their runtime data. The existing GUI/CLI
CPUID catalog remains separate from PM storage IDs and retains unverified PM
binding. Runtime coordinate provenance still needs to be established.

REA's three caller bodies narrow the next dependency: these coordinates are
obtained through firmware protocol enumeration, not read from the PM table.
The selected bodies locate protocol GUIDs
`2ADCD53F-4C94-40BB-BBA5-7DEBFF8A13AC` (data at `0x7320`) and
`D20482F8-7ECF-4A40-8C11-D326D8153806` (data at `0x7370`).
Their provider implementations are now partially recovered in the
[APOB protocol study](amd-apob-topology.md); physical identifier meanings
and runtime model applicability remain unverified.

| Caller | Static observation; not an executed caller proof |
| --- | --- |
| `0x1844` | Six nested enumeration coordinates reach the constructor; its result is appended to a per-outer-coordinate APIC list, with a 512-entry storage guard |
| `0x2094` | The same enumeration feeds the constructor and leaf `8000001Dh` cache queries; resulting records contain the APIC and a separate running-ordinal bitmap |
| `0x2d24` | Coordinate 2 comes through an intermediate list populated by helper `0x54e0`, rather than being the list index; the constructor's result is stored in processor-affinity-shaped records |

The last helper makes an indirect call through protocol offset `+0x40`.
Its fixed ordering data and interface results are not a PM-to-physical map.
REA's decompilation also omits some inferred output arguments on indirect
calls, so these observations are leads for the protocol provider study, not
validated C declarations. None of the three recovered caller bodies supplies
a checked link from MP5/SMU PM storage to those enumerated coordinates. Only
the constructor itself has the 256-case instruction replay described above.

## Reproduce

After the pinned extraction and instruction study:

```sh
for proof in config-route consumers; do
  python3 analysis/tools/firmware-granite-$proof.py \
    --smu build/firmware-input/smu-62-54-body.bin \
    --mp5 build/firmware-input/mp5-62-54-body.bin \
    --smu-study build/firmware-evidence/smu/smu-study.json \
    --mp5-study build/firmware-evidence/mp5/smu-study.json \
    --output build/firmware-evidence/granite-$proof.json
done
PYTHONPATH=build/firmware-tools/python python3 analysis/tools/firmware-granite-apic.py \
  --binary build/firmware-input/uefi-granite-topology.bin \
  --output build/firmware-evidence/granite-apic.json
```

The three proofs are included in the REA workflow. The subsequent APOB
provider/getter replay brings the replay count to 19.
REA 6.7.0 analyzes two UEFI transport modules, the topology module and its two
protocol providers; the pinned Ghidra Xtensa
decoder supplies instruction evidence for the bounded Python replays. REA does
not execute the Xtensa firmware. See [completed validation](amd-granite-validation.md)
for the commits and cloud results actually verified.

Remaining physical units: core **0/4/12**, CCD **0/1/2/6/8**. The counter paths
still need event identities and an independently anchored time base; the model
paths need coefficient calibration. PM-to-physical-CCD/OS-core mapping and the
second rail's physical identity, independent write and device readback remain
unresolved. No new hardware write is enabled by this work.
