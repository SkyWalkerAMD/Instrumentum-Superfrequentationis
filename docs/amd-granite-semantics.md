# Granite Ridge field units and CCD identification

This update uses the ASUS ProArt X870E-CREATOR WIFI 2503 reference capsule and
its pinned SMU/MP5 98.84.0 bodies from [the input study](amd-granite-pm.md).
It does not identify the BIOS installed on the user's 9950X3D system.

## Recovered units

The shared GUI/CLI decoder interprets these rows for exact PM versions 0x620105
and 0x620205. Row numbers and storage slots are zero based.

| Core row | Display | Unit | Static data path |
| --- | --- | --- | --- |
| 2 | Core temperature, sensor maximum, averaged | deg C | 12-bit sensor code / 8 - 49; maximum of three selected sensors; accumulation and normalization |
| 5 | C0 residency candidate, averaged | % | Fraction at per-core state +0x70, accumulated and multiplied by 100 after normalization |
| 6 | CC1 residency candidate, averaged | % | Fraction at +0x74, same normalization |
| 7 | CC6 residency candidate, averaged | % | Fraction at +0x78, same normalization |

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

CPUID IDs do not yet establish the PM row-to-physical-core association. Native
Granite `--map-ccd` remains unavailable. No tuning target is inferred from L3 size.

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
