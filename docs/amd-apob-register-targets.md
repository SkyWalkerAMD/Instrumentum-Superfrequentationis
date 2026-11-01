# APOB register target candidates

The pinned ASUS ProArt X870E-CREATOR WIFI BIOS 2503 contains two providers of
the topology protocol studied in [APOB topology recovery](amd-apob-topology.md).
Their slot `+0x48` is a register read/modify/write operation, not an inverse
lookup from PM slots to physical CPUs. A separate original-byte replay now
covers that slot without executing any hardware or firmware services.

## The providers differ

Offsets are file offsets/RVAs in the previously hash-pinned provider images.

| Provider | Entry / service object offset | Register selector |
| --- | --- | --- |
| A (`9521A9E1-759B-48F6-96A1-EB7B8997243E`) | `0x2118` / `+0x68` | `uint32(0x203c0020 OR (group_stored_id << 23))` |
| B (`EC052FF7-E33B-484B-BD4D-8C7E4DC9572D`) | `0x2178` / `+0x70` | `uint32(0x20bc0020 OR (subgroup_stored_id << 22))` |

Both bodies count the initial nonzero prefix of bytes at record `+0x33/+0x34`.
That is the first core entry's flag pair, not the selected core's thread count.
The selected bit is `(initial_prefix * core_ordinal + thread_argument) & 31`.
The selected core's **stored ID does not determine the bit**. The callback
receives the socket argument, the address and a pointer to a DWORD. Successful
reads are followed by `old_value OR (1 << bit)` and a write callback.

This proves neither the register's physical effect nor which provider is
active on the user's machine. The callback objects, their read payloads and
EFI status values are fixtures. Diagnostics are explicitly disabled in the
emulated image; firmware initialization, assertion handlers and devices do
not run. A read error prevents the write; a warning status does not. The
write's status is returned without reading back the register afterward.

## CPU eligibility narrows the reference path

Following the PE entry into `0x72c` reveals different CPU gates before either
provider's installer. REA exports the gate and CPUID wrapper for both images,
plus B's comparison helper `0x960`. Original-instruction replay supplies
synthetic `CPUID 0x80000001` results at the actual `cpuid` instruction `0x395`:

| Provider | EAX condition | EBX condition | Accepted / rejected boundary |
| --- | --- | --- | --- |
| A | `(EAX & 0x0fff0f00) == 0x00b40f00` | `EBX[31:28] == 0` | `0x785` / `0x897` |
| B | `(EAX & 0x0fff0ff0)` is `0x00b60f80` or `0x00b60f00` | `EBX[31:28] == 0` | `0x779` / `0x88b` |

The project's 9950X3D fixture signature `0x00b40f40`, with EBX's high nibble
zero, passes A and rejects B. A ignores the low model nibble as well as
stepping; B keeps the low model nibble and tries two signatures. Both ignore
the low 28 EBX bits. This supports prioritizing A's **group-based reference
selector** for that input, rather than treating the two entry gates as
equivalent. It does not prove that the user's installed BIOS executed A,
that its dependencies allowed protocol installation, or what the register
does. No target-machine CPUID capture is available; both offline candidate routes
remain visible and neither becomes an executable target.

The gate replay covers **16,636 cases**: every model byte and package nibble
with endpoint steppings, every single-bit change around three signatures,
ignored-register variation and explicit matching/nonmatching examples.
Only gate/helper instructions and stack writes are allowed. Execution stops
at the branch boundary before any firmware service or installer. Host CPUID
does not run, and no protocol installation, HMAC check, PM binding or
physical mapping is inferred from passing the gate.

## Ambiguity that matters to mapping

- A uses a group ID while B uses a subgroup ID. A record that is unique under
  one candidate route can have duplicate targets under the other.
- The address calculation uses OR. Group IDs 0 and 64 both produce
  `0x203c0020` under A; subgroup IDs 0 and 2 both produce `0x20bc0020` under B.
  Distinct stored identifiers alone therefore do not prove distinct targets.
- If the first prefix is zero, later nonzero thread flags can still pass the
  selected entry check. Different core ordinals then select the same bit.
- The firmware slot does not explicitly bound the thread argument to 0/1.
  In the replay, argument 2 can pass by reading the next core's ID byte.
  The product only derives candidates from the two actual flag positions;
  it does not reproduce this unchecked indexing or expose a write interface.

The grid checks both profiles with 1,600 routing cases each, plus nine status
combinations, eleven invalid cases and two unchecked-thread cases per profile:
**3,244 original-instruction replays**. Core IDs are deliberately unrelated to
ordinals; first-entry flags and selected-entry flags vary independently.
Original instructions are restricted to each 364-byte selector body, with
explicit callbacks at two emulator-only addresses. Unexpected calls and
writes outside the emulated stack fail the experiment.

## GUI and CLI

`amd-topology-decode --file record.bin` and **Open firmware topology file…**
use the same bounded offline decoder. It now reports the reference initial
flag prefix, entries with a different prefix and duplicate address/bit pairs
for both providers. GUI details and JSON retain the candidate coordinates;
neither provider is selected automatically. Existing raw IDs and flags are
unchanged, including non-prefix flags.

JSON's `reference_register_routes` contains both candidate addresses and a
bit for each nonzero raw flag 0/1. An invalid record/container returns null
for this diagnostic and no groups. Coordinates are record ordinals, not OS
CPU numbers; the addresses are reference register selectors, not host physical
addresses. `runtime_provider_verified`, `register_effect_verified` and
`runtime_write_enabled` remain false. No hardware backend is opened.

Tests cover the sparse record, prefix mismatches, core-ID independence,
distinct-ID address aliases, the maximum 256 candidates, every record-byte
mutation and failed/container imports. GUI tests also verify prior results
are cleared and command arguments remain unchanged. Cloud results are linked
from [the validation report](amd-granite-validation.md).

```sh
PYTHONPATH=build/firmware-tools/python python3 analysis/tools/firmware-granite-register-targets.py \
  --provider-a build/firmware-input/uefi-granite-topology-provider-a.bin \
  --provider-b build/firmware-input/uefi-granite-topology-provider-b.bin \
  --output build/firmware-evidence/granite-register-targets.json

PYTHONPATH=build/firmware-tools/python python3 analysis/tools/firmware-granite-provider-gates.py \
  --provider-a build/firmware-input/uefi-granite-topology-provider-a.bin \
  --provider-b build/firmware-input/uefi-granite-topology-provider-b.bin \
  --output build/firmware-evidence/granite-provider-gates.json
```

No new physical units or physical/OS/PM binding are established. The remaining
core 0/4/12 and CCD 0/1/2/6/8 units, independent second-rail write/readback,
runtime provider and installed BIOS remain unresolved. This narrows the
coordinate problem without promoting reference selectors into tuning targets.
