# APOB enumeration through the UEFI APIC consumer

Reference: ASUS ProArt X870E-CREATOR WIFI BIOS 2503. REA 6.7.0 resolves the
APIC consumer, sorter and two topology providers recorded in the
[APOB study](amd-apob-topology.md). The new replay connects these previously
separate functions. It does not certify the target's installed BIOS, chosen
runtime provider, physical numbering, or PM-to-OS-core correspondence.

## What reaches the constructor

The original bytes of consumer `0x2d24` execute the following chain:

1. Locate the fabric, topology and proximity interfaces.
2. Obtain outer-group counts from provider slot `+0x20`.
3. Call sorter `0x54e0`, which uses `+0x40` to inspect stored IDs, and returns
   reordered **enumeration ordinals**.
4. Obtain subgroup, core and prefix-thread counts from slots `+0x28/30/38`.
5. Pass the ordinals and thread position to APIC constructor `0x526c`.
6. Serialize the returned APIC into 24-byte extended or 16-byte legacy
   processor-affinity-shaped records. The legacy path omits APICs >=255.

The constructor does not receive the APOB stored core IDs in this path. Reversed
and non-contiguous stored core IDs deliberately differ from their enumeration
positions in the tests. Stored outer IDs affect sorting, not the coordinate
values passed to the constructor. This distinction prevents treating an APOB
ID, a PM storage slot and an OS CPU number as interchangeable integers.

## Missing type-3 example

With stored outer IDs `[8,4]`, the sorter returns ordinals `[1,0]`. A synthetic
CPUID shape with types `[1,2,4,0]`, SMT shift 1, one subgroup and one two-thread
core per outer group yields the constructor inputs and outputs below:

| Six input coordinates | APIC returned |
|---|---|
| `(0,0,1,0,0,0)` | 1 |
| `(0,0,1,0,0,1)` | 2 |
| `(0,0,0,0,0,0)` | 0 |
| `(0,0,0,0,0,1)` | 1 |

The absent type-3 shift remains zero, so this combination produces a duplicate.
The replay deliberately combines synthetic inputs: it does **not** demonstrate
that these records, provider or caller configuration are used on the actual
9950X3D, or that its BIOS produces duplicate APICs. It rules out an unconditional
mapping formula based only on the recovered record layout.

## Reproduction and limits

[`firmware-granite-apic-enumeration.py`](../analysis/tools/firmware-granite-apic-enumeration.py)
runs **432 cases per provider, 864 total**, using pinned Unicorn 2.1.4 and
pefile 2024.8.26. Each provider produces 6,552 checked records. Shapes vary
outer/subgroup/core counts, non-prefix thread flags, SMT shifts, presence of a
type-3 level and both record encodings. Observable outputs match across the
two providers; their instruction counts differ and are not equated.

The sorter, getters, constructor, CPUID wrapper and consumer execute original
instruction bytes. Boot-service lookup, fabric counts, configuration, proximity
domains and CPUID responses are explicit fixtures. Writes are restricted to
stack and output memory; unexpected execution paths fail. No UEFI initializer,
host CPUID, MSR, physical-memory acquisition or hardware driver executes.

This is a recovered component of the mapping path. It supplies no checked edge
from SMU/MP5 PM slots to the runtime APOB record or to a physical/OS identity.
GUI and CLI retain separate PM and CPU-topology views and do not automatically
bind them. The eight remaining field units and second-rail write/readback are
also unfinished. See the [current validation report](amd-granite-validation.md).
