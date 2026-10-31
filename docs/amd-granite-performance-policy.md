# Granite Ridge performance policy values and OS CPU catalog

This study uses ASUS ProArt X870E-CREATOR WIFI BIOS 2503's pinned SMU/MP5
98.84.0. The user's 9950X3D system is unavailable and its installed BIOS is
unknown. All experiments below use synthetic memory; no setting was sent to
hardware. [Input provenance](validation/granite-creator-2503-input.json) and
[the complete research record](validation/amd-granite-research.json) identify
the reference and the REA/Ghidra runs.

## Two more PM units

For exact versions `0x620105` and `0x620205`, core rows 9 and 10 now display
**Core request policy A/B (averaged)** in **GHz**. The 9950X3D layout puts them
at float indices 445..460 and 461..476. They describe firmware policy values,
not measured clocks or confirmation that a frequency was applied. The shared
GUI/CLI decoder preserves the stored binary32 values without further scaling.

The unit combines two kinds of evidence:

* The [Granite Ridge reference implementation](https://github.com/Death4two/RyzenSMUDebug-LINUX/blob/a78069caaf73f6245c97d6c8bad5a1236b54f300/README.md)
  documents RSMU 70h input and 6Eh output in MHz. This is a command-unit
  cross-check, not the source of this PM layout or a measurement of our board.
* In the pinned firmware, 70h's handler at 0x29e54 converts input with
  `float32(argument) * 0.001f`. Its policy-selected float is stored at 0xaee4
  and record offset 8, sent by MP5 command 90h, and copied by 0x2ad20 into
  MP5 0x119a0. Command 32h's initialization record uses the same offset.
  The 6Eh getter at 0x2a7c8 reads 0xaee4, multiplies by 1000 and rounds.
  E7h's separate handler at 0x29fc4 also multiplies by 0.001, then sends the
  float bits through MP5 command 72h to 0x2b558, which stores at 0x119a0.

MP5 queue 1's dispatcher uses the table at 0x13298 with 0x97 entries; its
32h/90h/72h handlers are 0x2b33c/0x2b53c/0x2b558. The replay checks 13 finite
scale values, both record and direct-argument paths, the shared getter and
policy-array initialization. Mailbox, fuse and synchronization helpers are
explicit fixtures. The earlier 70h clipping policy and actual interprocessor
transport are not executed by this experiment. E7h is research evidence,
not a newly enabled application command or a validated operating range.

## Request producer and retained values

MP5 function 0x2f930 reads two words at
`0x1603038/0x160303c + 0x20000 * local_core_slot`. It treats their four bytes
as performance-request inputs. Their layout resembles AMD's CPPC Request
MSR, but the MMIO-to-MSR alias has not been established. The application does
not rename row 11 to EPP or claim that it belongs to an OS CPU.

The byte-checked path has these properties:

1. A zero word uses that thread's cached word. If **both current words are
   zero**, the function returns without updating either policy value or the
   fourth-byte code. The later PM accumulator can therefore average old data.
2. With two threads enabled, the first three bytes use pairwise maxima and
   the fourth uses the minimum. Optional policy bytes constrain those inputs.
3. Without a nonzero selected request, A uses the capped first code and B
   uses the lower-policy code. Each is multiplied by `1/166` and the float at
   0x119a0. A nonzero selected request makes both outputs equal. Clipping that
   selected code to zero takes the separate A/B branch again.
4. The lower-policy code is not capped at 166 or forced below the first code
   on this branch. Synthetic inputs can produce B greater than A. Neither
   frontend clamps, sorts, or silently swaps the two values.

The producer writes `0x11278 + 0x3c * local_core_slot + 0x1f4/0x204`.
Accumulation and final PM export add no unit conversion for these rows.
The new [instruction replay](../analysis/tools/firmware-granite-performance.py)
covers 32,768 combinations of local slot, all byte codes, thread count, policy
and missing-word masks, including 8,192 both-zero early returns. Separate
four-sample experiments verify 48 exported policy words across one-CCD and
both two-CCD storage ordinals. Zero and non-finite captures are handled by
the existing raw-value decoder; a unit label does not establish freshness.

JSON reports `request_policy_value: true`, `measured_frequency: false`,
`freshness_verified: false` and `setting_readback_verified: false`. Capture
import reconstructs these flags and units from raw bytes rather than trusting
editable labels in the file. The GUI identifies the rows as request policy.

## OS CPU to CPUID topology

```sh
octool-cli amd-topology --all-cpus --l3
octool-cli amd-topology --cpu 0 --l3
```

The first command reads the logical CPUs allowed to the process, preserving
sparse OS IDs and APIC IDs. `--cpu` and `--all-cpus` are mutually exclusive.
JSON includes each CPU's socket, CCD, core, thread, raw topology CPUID words,
and optional L3 geometry. An allowed set restricted by affinity or a container
is a subset of the machine; the tool does not invent omitted CPU entries.

In **AMD tuning**, select **Include L3 cache**, then **Read all available CPU
topology**. The table shows the same shared-core result and can be copied.
For the 9950X3D reference model, the Complex-level boundary identifies the
CPUID CCD; L3 capacity can distinguish cache configurations without assuming
which numbered CCD carries the extra cache.

Two complete sweeps must agree. Invalid/duplicate CPU IDs, duplicate APIC IDs,
incompatible CPU identities or topology boundaries, conflicting cache geometry,
access failures, changed CPUID results, and changes to the allowed CPU set
discard the entire catalog. Failed snapshots return an empty CPU array, not a
partially trusted map. Only CPUID is used; no SMU command or register write is
prepared. Tests cover sparse and interleaved OS IDs, 32/96 MiB cache fixtures,
every CPUID access failure in a two-CPU catalog, second-sweep drift and CLI
execution restricted to one allowed CPU.

This completes collection of **OS CPU → CPUID CCD/core/thread** associations.
It does not complete **PM storage slot → physical CCD/core → OS CPU** binding.
The request producer strengthens the local-slot-to-MMIO-window trace but does
not provide that missing identity link. `pm_slot_mapping_verified` and
`firmware_mapping_verified` remain false.

## Remaining boundaries

Twelve core rows now have physical or dimensionless units, and two further
core rows have the verified averaged-code representation. Core rows 0, 3, 4
and 12 still lack established units. Three CCD rows have established units,
one has the averaged-code representation, and eight remain unresolved.

The [independent channel override paths](amd-granite-channel-mirrors.md) and
[SVI telemetry paths](amd-granite-svi-telemetry.md) remain separate evidence.
Neither the software command mirrors nor this Fmax storage getter proves an
independent physical second-rail setter/getter pair. Channel-to-physical-rail
identity, device-side applied-value readback and hardware acceptance remain
unverified; this change enables no new voltage writer.
