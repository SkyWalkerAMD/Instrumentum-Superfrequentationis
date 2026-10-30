# Granite Ridge field units and CCD identification

This update uses the ASUS ProArt X870E-CREATOR WIFI 2503 reference capsule and
its pinned SMU/MP5 98.84.0 bodies from [the input study](amd-granite-pm.md).
It does not identify the BIOS installed on the user's 9950X3D system.

## Recovered units

The shared GUI/CLI decoder interprets these rows for exact PM versions 0x620105
and 0x620205. Row numbers and storage slots are zero based.

| Core row | Display | Unit | Static data path |
| --- | --- | --- | --- |
| 1 | Core voltage model, averaged | V | VID-derived voltage axis, interpolated by the per-core model; not measured voltage |
| 2 | Core temperature, sensor maximum, averaged | deg C | 12-bit sensor code / 8 - 49; maximum of three selected sensors; accumulation and normalization |
| 5 | C0 residency candidate, averaged | % | Fraction at per-core state +0x70, accumulated and multiplied by 100 after normalization |
| 6 | CC1 residency candidate, averaged | % | Fraction at +0x74, same normalization |
| 7 | CC6 residency candidate, averaged | % | Fraction at +0x78, same normalization |

| CCD row | Display | Unit | Static data path |
| --- | --- | --- | --- |
| 3 | CCD sensor maximum A, averaged | deg C | Maximum of eight selected converted sensors, stored at 0x1280c |
| 4 | CCD sensor maximum B, conditional, averaged | deg C | Maximum of converted sensors 45..52 if byte[0x7829] is nonzero, stored at 0x12810 |
| 11 | MP5 interval counter ratio, averaged | ratio | Accumulated ticks divided by elapsed ticks, stored at 0x19cfc |

Residency fractions and percentage scaling are traced; the individual state
names remain candidates. The independently maintained
[9950X3D observations](https://github.com/Kyworn/gnr-smu/blob/def79d66d5431dfc4b6fc773f0b99fb99c8e378d/docs/architectures/granite_ridge/9950X3D.md)
support the temperature row and residency names. They are not measurements of
the user's machine. The sensor conversion also matches AMD's temperature scale
used in [Linux k10temp](https://github.com/torvalds/linux/blob/v6.17/drivers/hwmon/k10temp.c).

These values already contain firmware normalization. The application neither
divides temperatures by eight again nor multiplies stored percentages by 100.
`unit_evidence` records `granite-mp5-98.84.0-static`; the capture itself does not
authenticate firmware, sensor validity, freshness or active core identity.
Import recomputes labels and units from raw bytes. Other row units remain unknown.

The byte-checked experiment is
[`firmware-granite-semantics.py`](../analysis/tools/firmware-granite-semantics.py).
It checks all 4096 raw sensor codes across 64 batches, 512 selected-core maxima,
36 bounded counter-fraction cases, four sample accumulations and 156 normalized
words. Reciprocal refinement is an explicit exact-factor fixture, not an
emulation of floating-point exception behavior or error bounds.

Three additional invalid-mask fixtures check 128 retained sensor values. An
invalid sensor response leaves the previous converted float in place, and the
per-core maximum can still use that retained value. The reported temperature
unit therefore does not imply that a sample is fresh or currently valid.

### CCD temperatures, model voltage and interval ratio

The extended temperature slice ends at MP5 0x2c19d, including both CCD maxima.
Row 3 initially selects sensors `[13,14,15,16,25,26,27,28]` through the bytes at
0x12864. Row 4 uses sensors 45..52 only when enabled; otherwise the previous
float remains. In 384 synthetic cases, 192 disabled cases retain that float.
These runs also check 3,072 core maxima using the image's initial selectors.
Runtime selector overrides and freshness are not established. Neither CCD row
is named L3 temperature: sensor coordinates have not been physically identified.

Core row 1 is copied from 0xf040 + 0x520 * core slot. Function 0x29658 writes
that field using the voltage axis at 0x19ac0+0x34 and its step at +4. Initializer
0x29b14 builds 16 points from a 9-bit code and a 5-bit step code; helper 0x24ae4
converts the former with `0.245 + code * 0.005`, and the step is `code * 0.005`.
The conversion matches the volt scale in the manufacturer's
[SVI3 controller documentation, page 80](https://www.richtek.com/assets/product_file/RT3674FE/RT3674FE_DS-00.pdf#page=80).
This is a scale reference, not identification of the controller on the ASUS board.
The helper returns 0.245 for code zero: the experiment does not import an
electrical off-code rule into this model. It checks 512 conversions, 2,048
16-point grids (step codes 0/1/16/31), and 60 bounded interpolation cases.
Fuse reads, model index selection and approximate reciprocal accuracy remain
outside the replay. The PM value is a model output, not a voltage sensor,
verified applied setting, or second-rail getter.

CCD row 11 comes from 0x31b50 through 0x32214 and 0x2d778. The numerator is an
accumulated tick count, plus an active interval when flagged; the denominator
is elapsed ticks. The experiment checks 54 cases, including 18 wrapping clock
intervals, then checks the caller stores into the PM producer source. Exact
reciprocal fixtures exclude zero elapsed time and do not establish reciprocal
accuracy. The flag's physical meaning is unresolved, so the value is not called
CPU utilization and is neither multiplied by 100 nor clamped to [0,1].

All four newly interpreted rows pass through the same accumulation and averaging
path without additional scaling. The synthetic normalization experiment checks
40 core values and three CCD values. Both frontends use these shared units;
unknown rows remain unverified, and imports rebuild interpretation from raw bytes.

## Decoder correction

The pinned
[configuration-specific Xtensa ISA](https://github.com/qemu/qemu/blob/d73149283205645625bfa9fe029cfb68e4c4a9ae/target/xtensa/core-de233_fpu/xtensa-modules.c.inc)
assigns `0x8003/0xc003/0x180000/0x580000` to LSIP/SSIP/LSXP/SSXP.
[QEMU's implementation](https://github.com/qemu/qemu/blob/d73149283205645625bfa9fe029cfb68e4c4a9ae/target/xtensa/translate.c)
accesses the original base and then advances it. Stock Ghidra assigns these
encodings the older FPU's preincrement semantics. Granite's producer loads an
accumulator at +0 and stores it with SSIP +4; preincrement would overwrite the
next accumulator, cascading values through the table.

`SmuFpPatch.py --granite-postincrement` selects the matching static research
profile. It changes no firmware bytes and leaves existing Shimada studies on
their prior profile. The compiled SLEIGH self-test covers 12,240 cases, including
768 new address-update cases and indexed register-alias cases. Zero-overhead
loops still require raw instruction replay: decompiler pseudocode alone loses
loop bodies.

## Selected CPU, CCD and L3 cache

```sh
octool-cli diagnose
octool-cli amd-topology --cpu 0 --l3
```

Choose an allowed CPU from `diagnose`. In AMD tuning, select **Include L3 cache**
and click **Read CPU topology**. Both frontends read CPUID Fn80000026 IDs and
Fn8000001D L3 geometry on the selected logical CPU, retaining sparse APIC IDs.
The output shows capacity in bytes/MiB and cache sharing ID. This lets a user
compare the reported L3 capacities of the two CCDs without assuming that CPU 0,
CCD 0 or firmware slot 0 is the cache-equipped CCD.

Geometry uses (ways+1) × (partitions+1) × (line size+1) × (sets+1) from the encoded
CPUID fields, with checked 64-bit arithmetic. Sharing capacity is not the count
of online processors. Bounded enumeration, missing L3, inconsistent domains,
faults, changed topology and changed cache geometry discard the L3 result.
This path performs CPUID only and sends no SMU or register command. See
[AMD topology references](amd-topology-recovery.md) for the CPUID contract.

CPUID IDs do not yet establish the PM row-to-physical-core association. No tuning
target is inferred from L3 size.

The model-specific [AMD PPR 57896 Rev 3.00](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/57896.zip)
corrects an earlier assumption borrowed from Model 02h. Model 44h's page 120
marks topology type 3 reserved; Figure 21 on page 34 shows one Complex per CCD.
For Family 1Ah / Model 44h only, the decoder therefore uses the type-2 Complex
shift for the CPUID CCD boundary and records `ccd_level_type: 2`. Other models
still require the explicit type-3 level. Missing Complex or a reserved type 3
on Model 44h fails closed. The corrected fixture contains Core/Complex/Socket
levels, and tests cover sparse IDs, disabled SMT, missing levels and wrong models.
This fixes OS CPU-to-CPUID CCD parsing, not the still-missing firmware PM binding.
[Source hashes and inspected pages](validation/amd-granite-topology-source.json)
pin the document; page 110's CoreId field is a logical core identifier, not a
contract for MP5 table indexing.

### Initial temperature sensor selectors

The pinned MP5 image contains these three bytes at
`0x11a18 + 0x1bc * core_slot + 0x84..0x86`. The temperature consumer reads
those selectors and stores their maximum at per-core state +0x28.

| Core storage slot | Initial sensor indices (decimal) |
| --- | --- |
| 0 | 1, 2, 3 |
| 1 | 33, 34, 35 |
| 2 | 4, 5, 6 |
| 3 | 36, 37, 38 |
| 4 | 7, 8, 9 |
| 5 | 39, 40, 41 |
| 6 | 10, 11, 12 |
| 7 | 42, 43, 44 |

A separate local Python replay used these image bytes, 64 rotations of distinct
valid sensor values and the same byte-checked instruction slice. All 512 core
maxima matched. This supplements the cloud study; it is not another cloud job
or a hardware measurement. [The local result](validation/amd-granite-initial-selectors.json)
records the input hash and deterministic trace.

These are initial image data. Runtime overrides have not been ruled out, and
the sensor indices do not identify CPUID/APIC or OS cores. No live core label,
active-core mask or tuning target is inferred from them. To reproduce the local
inspection and replay using the extracted, hash-pinned study inputs:

```python
import hashlib, json, runpy, struct
from pathlib import Path

body = Path("build/granite-study-input/mp5-62-54-body.bin").read_bytes()
assert hashlib.sha256(body).hexdigest() == "fb5d96c17b909cc4f681523ccc931796e27c7457ba5ad637a3d4fce1c78ce556"
study = json.loads(Path("build/granite-study-41023307189/mp5/smu-study.json").read_text())
assert study["source_sha256"] == hashlib.sha256(body).hexdigest()
code = {int(i["address"], 16): i for f in study["functions"] for i in f["instructions"]}
Slice = runpy.run_path("analysis/tools/firmware-granite-semantics.py")["Slice"]
selectors = [list(body[0x11a18+c*0x1bc+0x84:0x11a18+c*0x1bc+0x87]) for c in range(8)]
trace = hashlib.sha256()
for rotation in range(64):
    vm = Slice(body, code)
    values = [((i+rotation) % 64)/8 for i in range(64)]
    for i in range(64):
        vm.put(0x12b44+4*i, ((392+(i+rotation) % 64) << 11) | (1 << 10))
        vm.putf(0x12c44+4*i, -999)
    for c, indices in enumerate(selectors):
        for j, index in enumerate(indices):
            vm.put(0x11a18+c*0x1bc+0x84+j, index, 1)
    vm.run(0x2c0e3, 0x2c15f)
    for c, indices in enumerate(selectors):
        expected = max(values[i] for i in indices)
        actual = vm.getf(0x11a18+c*0x1bc+0x28)
        assert actual == expected
        trace.update(struct.pack("<IIff", rotation, c, expected, actual))
assert trace.hexdigest() == "f394b054ff73ce955cbf08d960a6f4e30756bf20bb697cb876ef57410807d13b"
```

## Firmware storage slot to CCD target

The corrected entry scanner also includes frames of 128 bytes and above.
The formerly missed indirect target at SMU 0x1d7ec initializes the MP5 packet.
Its byte at +0 is the firmware target, +0x18 is its storage rank among present
targets, +0x11 is the number of present targets, and +0x3c is the PM destination.
Byte +2 is a separate active-target rank. Disabling cores does not compact the
storage rank. The instruction copy loops, not the incomplete decompiler output,
establish these fields.

[`firmware-granite-topology.py`](../analysis/tools/firmware-granite-topology.py)
replays 75 synthetic presence/core-mask combinations, 100 complete 112-byte
startup packets and 15,600 PM copy words. Within this firmware's two-target
domain, width two necessarily means firmware IDs `[0, 1]`. Width one may mean
either target; the runtime cannot choose between them without more evidence.

```sh
octool-cli amd-pm-native-read --cpu 0 --platform granite-ridge --map-ccd
```

GUI **Map firmware CCD slots** and CLI share this optional behavior. It accepts
only the existing exact 1Ah/44h, PCI 1022:14d8, SMU 98.84.0 gates with table
0x620205/2452. Identity, firmware, length and address rechecks must complete
before IDs are published. A one-slot table is rejected before transfer when
mapping is requested; ordinary one-slot capture remains available without it.
No A5 query or tuning request is added. JSON distinguishes the inferred mask
from queried presence masks with `source: granite-two-slot-initializer` and
`presence_mask_queried: false`.

These are firmware target IDs, not an established association with CPUID CCD
IDs, physical core numbers or OS CPUs. GUI labels explicitly retain **core slot**.
Imported snapshots do not authenticate this mapping. Actual installed BIOS and
target-machine acceptance remain outstanding.

## Rails boundary

RSMU 0x61 stores a clamped code in an indexed halfword array. A separate consumer
at SMU 0x2100c selects the first or last configured entry according to a policy
flag, applies `0.245 + code * 0.005`, and combines it with a shared voltage floor.
This is evidence that the second stored entry can affect shared policy, not a
proof of two independently readable physical rails. RSMU 0x6e still reads a
different fixed float; 0xa5 is not the missing indexed readback. Actual second
rail writes remain disabled pending the remaining protocol and hardware checks.

The byte-checked rails experiment now joins 61h storage to this consumer on
synthetic RAM: 18,432 cases vary all 512 input codes, selectors 0/1, configured
counts 1/2/4, first/last policy, caps and shared inputs. It checks rejected
selectors, unselected entries, 1,536 selected-second-entry cases and three
disabled branches. Of these synthetic selected-second cases, 718 exceed the
seeded shared input. The float at 0x8c30 and the caller's mirror at 0x8c34 agree.
The opaque `ceil.s` conversion and encoded hardware-request path are deliberately
not executed; the independent float store is replayed separately. These are
code-path tests, not voltage measurements or proof of an independent rail getter.

The downstream callers at 0x20bc8/0x20bec compare the requested code at 0x8c6e
with the software current code at 0x8c6c. If an increase/decrease is needed, both
pass **channel 0** to 0x20e4c. The direct path at 0x20fb0 also passes channel 0
to the hardware helper 0x32d6c. The new caller experiment checks 5,120 comparisons,
512 direct cases and 512 locked cases with hardware calls explicitly skipped.
Its subsequent updates of 0x8c6c/0x8c74 are software mirrors, not hardware readback.
These slices start from seeded encoded codes and do not execute the opaque ceil
conversion. The 61h array selector is therefore not an independent SVI channel
selector in these paths. Other channels exist, but their physical identity and
relationship to a second CPU rail remain unproved; runtime writes stay disabled.
