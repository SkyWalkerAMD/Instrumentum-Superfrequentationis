# APOB topology record recovery

The GUI and CLI share a bounded offline decoder for an **APOB group 3,
type 3 record**, including its complete 48-byte header (16-byte fields and
32-byte HMAC). Complete APOB containers with exactly one such record are also
accepted; see the [handoff and container follow-up](amd-apob-handoff.md). This is a recovered
component of the firmware topology path. It does not connect PM storage slots
to physical CCDs, OS CPUs or firmware tuning targets.

The [APIC consumer follow-up](amd-apob-apic-enumeration.md) now replays the
sorter, provider getters and APIC constructor together. The selected caller
passes enumeration ordinals, not stored IDs; runtime PM/physical binding
remains unverified.

In AMD tuning, choose **Open firmware topology file…**. In the CLI:

```sh
octool-cli amd-topology-decode --file topology-record.bin
```

Input is a raw record or complete APOB container, not a BIOS image, a CPUID
snapshot or a PM table. No hardware backend is opened. There is no automatic
physical-memory acquisition. A single-record file must be 456 bytes through
1 MiB, have group/type 3/3, and declare exactly its file length at offset 12. Extension
bytes beyond the known span are counted, not interpreted. Matching these
checks does not authenticate the file's firmware origin or applicability.

The two interfaces show enumeration ordinals, stored identifiers and both raw
thread flags. Enumeration stops at the first `0xff` identifier at each level.
Thread count means the initial nonzero flag prefix: `[0,1]` counts as zero.
It is not the number of online OS CPUs. Repeated identifiers within a parent
are reported without renumbering or claiming unique identity. Parsing errors
discard the decoded groups; the GUI also clears prior results and saved
tuning snapshots. Decoding never prepares a hardware command.

## Pinned firmware evidence

Reference: ProArt X870E-CREATOR WIFI BIOS 2503. The installed BIOS and runtime
provider on the user's 9950X3D remain unknown. REA 6.7.0 / Ghidra 12.1.4 studies
two providers of `D20482F8-7ECF-4A40-8C11-D326D8153806`:

| Provider module | SHA-256 | Installer | Count helpers | Ordinal to stored-ID helper | APOB selector |
| --- | --- | --- | --- | --- | --- |
| `9521A9E1-759B-48F6-96A1-EB7B8997243E` | `8ea4e45882f053775e87943b20b092d2086787a9500a120fb9f35af62d424295` | `0x13e4` | `0x1d28/1d78/1de4/1e7c` | `0x1f40` | `0x2bb0` |
| `EC052FF7-E33B-484B-BD4D-8C7E4DC9572D` | `c4828555b14a4d8b8e84c7f94f2258b941a778a553cbb7666aac603b251a56b4` | `0x1454` | `0x1d9c/1dec/1e58/1ef0` | `0x1fb4` | `0x2b20` |

Offsets are file offsets/RVAs in these pinned PE images (image base zero).
Installers populate an allocated function table at runtime. Default REA
function discovery missed some of these indirect entries. The study now
passes 15 explicitly recorded analyst seeds per provider through REA's native
`REA_GHIDRA_SEED_FILE` support, based on the installer stores. The inputs are
hash checked; this does not patch the binary or the analysis engine. Seed
addresses and tool calls are retained in each study's evidence.

The selector checks the APOB signature `0x424f5041`, container version at least
5, then selects a unique group 3/type 3 record with the requested instance.
The returned address points to the record header, not the payload. This is
static evidence extended by the 502-case handoff replay; the installer has not been executed. The
application does not copy the firmware's unbounded container walking logic.

| Record offset/stride | Recovered read behavior |
| --- | --- |
| `0x00/0x04/0x08/0x0c` | Group, type, instance, total record bytes (little-endian DWORDs) |
| `0x10..0x2f` | HMAC bytes in the matching public AMD header; not verified |
| `0x30 + g*0x33`, `g < 8` | Group's stored ID |
| Group base `+1+s*0x19`, `s < 2` | Subgroup's stored ID |
| Subgroup base `+1+c*3`, `c < 8` | Core entry's stored ID and two thread flags |

The protocol's `+0x40` helper translates a contiguous prefix of input
coordinate pointers to stored IDs. Missing intermediate pointers are invalid.
The APIC consumer module `9FB8AC55-D266-4F86-AC43-BF2BA74F5A7A`, helper
`0x54e0`, sorts original enumeration ordinals by the stored-ID order
`[0,4,8,12,2,6,10,14,3,7,11,15,1,5,9,13]`. With stored IDs `[8,4]`, its
result is ordinals `[1,0]`, not the IDs themselves. Duplicate/unsupported IDs
can yield ambiguous or incomplete output; this order is not used to invent
a physical map in the application.

Protocol slot `+0x48` (`0x2118` / `0x2178`) is **not an inverse topology
lookup**. Its body reads then writes a register through another interface,
using an address derived from a stored ID. It is outside the emulator's
allowed execution range and is not exposed as a product command.
The [register-target follow-up](amd-apob-register-targets.md) now executes
these two bodies in a separate bounded replay with scripted register services.
It proves that A uses the group ID while B uses the subgroup ID, and that
both use the first core's flag prefix for the bit stride. GUI/CLI expose
candidate-route ambiguity as an offline diagnostic; neither provider nor a
hardware write is selected.

## Replay and limits

`analysis/tools/firmware-granite-topology-protocol.py` uses pinned Unicorn
2.1.4 and pefile 2024.8.26. Each provider has 2,674 synthetic instruction
replays: 1,473 count cases, 1,024 translations, 41 invalid pointer/boundary
cases and 136 ordering cases (5,348 total). Only selected getters and the
ordering helper execute in emulated memory. The diagnostic callback records
its argument and returns; actual firmware assertion/log behavior is not
emulated. Unexpected branches and writes outside stack/output memory fail.

The application has independent parser tests for every truncation, maximum
span, sparse/duplicate IDs, prefix flags, extension bounds and byte mutations;
CLI tests assert no hardware backend is created. GUI tests cover the same
record and clearing of failed results. See [completed validation](amd-granite-validation.md)
for the final tested commits and cloud outcomes.

Still unverified: which provider executes on the target, the runtime record
producer and physical binding of its stored IDs, the link to PM slots and OS
CPUs, eight remaining field units, and independent second-rail write/readback.
