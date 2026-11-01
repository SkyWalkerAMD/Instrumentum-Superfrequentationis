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
```

Both proofs are included in the REA workflow, bringing the replay count to 17.
REA 6.7.0 analyzes the two UEFI transport modules; the pinned Ghidra Xtensa
decoder supplies instruction evidence for the bounded Python replays. REA does
not execute the Xtensa firmware. See [completed validation](amd-granite-validation.md)
for the commits and cloud results actually verified.

Remaining physical units: core **0/4/12**, CCD **0/1/2/6/8**. The counter paths
still need event identities and an independently anchored time base; the model
paths need coefficient calibration. PM-to-physical-CCD/OS-core mapping and the
second rail's physical identity, independent write and device readback remain
unresolved. No new hardware write is enabled by this work.
