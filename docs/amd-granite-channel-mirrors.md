# Granite Ridge command mirrors and stored codes

The reference is ASUS ProArt X870E-CREATOR WIFI BIOS 2503, SMU/MP5 98.84.0.
The installed BIOS and physical behavior of the user's 9950X3D remain unknown.
These findings extend the [unit and topology study](amd-granite-semantics.md)
and [SVI register helper evidence](amd-granite-svi-telemetry.md).

## Three software voltage mirrors

| PM float index | Firmware channel | Source address | Accumulator | Unit |
| --- | --- | --- | --- | --- |
| 48 | 0 | 0x8c74 | 0x153c0 | V |
| 53 | 1 | 0x8c8c | 0x153d4 | V |
| 58 | 2 | 0x8ca4 | 0x153e8 | V |

The direct writer updates a software float after its packet helper finishes.
The float is `0.245 + VID * 0.005`; channel 2 instead uses
`0.49 + VID * 0.01` only when the byte at `0xc5ce` is one. The common
accumulator and output pointer tables average these floats into PM indices
48/53/58 without another scale conversion. This establishes volts as the
software representation, not a verified physical voltage or electrical range.

GUI and CLI expose these as **command mirrors**, distinct from the twelve
[SVI telemetry fields](amd-granite-svi-telemetry.md). The previous `VDD_MISC
candidate` at index 58 is replaced, without adding a duplicate offset.
JSON includes `software_command_mirror: true`, `measurement: false`, and false
values for physical rail identity, freshness and setting readback verification.
Imported labels and trust flags are rebuilt from the raw bytes.

## Separate override routes, unresolved physical rail identity

| Firmware channel | Override command / handler | Direct writer | Release command / handler | Override flag |
| --- | --- | --- | --- | --- |
| 1 | 0x11 / 0x2be90 | 0x20f10 | 0x12 / 0x2bea4 | 0x8c88 |
| 2 | 0x13 / 0x2beb4 | 0x20f3c | 0x14 / 0x2bec8 | 0x8ca0 |

The RSMU dispatch table at `0x6fa4` has eight bytes per command. Each
override wrapper takes lock 0x2000, clears its channel flag, calls its direct
writer, sets the flag and releases the lock. An ordinary direct writer skips
its update when the flag is already set. Release clears only the flag; it does
not restore the previous VID or read a peripheral voltage.

The packet helper at `0x32d6c` selects the configured channel address and emits
`address | (VID << 6) | 0x8000`. Its completion polling is not a voltage getter.
The selected path forwards oversized VID arguments without range validation;
the 0..511 test grid is not a safe operating voltage range. Code zero's
electrical meaning is also unresolved.

This provides separate firmware channel routes, but does not identify which
channel is CPU, SOC, MISC or a second physical rail. It also does not establish
device-side effects or independent setting readback. Application write
allowlists are unchanged; no new voltage command is enabled. The existing
0x61 shared-floor path is a separate protocol and remains denied at runtime.

[`firmware-granite-channels.py`](../analysis/tools/firmware-granite-channels.py)
checks the pinned binary and disassembly bytes, 64 wrapper/release cases,
12,288 writer cases (6,144 locked), nine oversized arguments, and a four-sample
writer-to-mirror-to-PM export. Mailbox argument and lock helpers are stubs.
The packet helper is replayed separately, with scripted MMIO and completion
responses, then execution resumes in the caller's own register window.
This is bounded instruction replay, not a full processor or peripheral emulator.

## Three integer-code representations

| PM row | Source | Stored interpretation |
| --- | --- | --- |
| Core 8 | byte at `0xeb28 + 0x520 * local_slot + 2` | Averaged 8-bit code A |
| Core 11 | byte at `0x15884 + 0x2c + local_slot` | Averaged 8-bit code B |
| CCD 7 | upper 16 bits of word at `0x12f30` | Averaged 16-bit code |

The MP5 accumulation and complete export path convert these to floats and
average them without further scaling. Their means can be fractional; the
application does not round them to integers. `code` describes their storage
representation. The meaning, physical unit and freshness of each code remain
unverified. They are not presented as OS P-states, frequency, temperature or
core identifiers. JSON includes `code_meaning_verified: false`.

[`firmware-granite-codes.py`](../analysis/tools/firmware-granite-codes.py)
checks 768 four-sample cases and 13,056 output words, all byte starting values,
both PM widths, both two-slot ordinals, wrapping source codes and varying
discarded lower CCD bits. Fractional output is checked without rounding.
Reciprocal behavior uses the same explicit exact-factor fixtures as the
[counter replay](amd-granite-counter-units.md).

## Storage coordinates are not OS CPU coordinates

Core JSON now exposes `firmware_ccd_storage_slot = slot / 8` and
`firmware_local_core_slot = slot % 8`. GUI labels show the same decomposition.
The complete scatter replay verifies the corresponding output positions for
one and two CCD slots. Native two-slot reads may additionally show the already
verified firmware target IDs [0,1]; single-slot target selection remains
ambiguous. Neither the integer codes nor this decomposition bind a PM slot to
a physical CCD, APIC ID or OS CPU. The global target-verification flag stays
false, and imports cannot establish live topology trust.

The current represented rows are twelve core and three CCD rows with traced
physical/dimensionless units, plus two core and one CCD code rows. These
code representations do not establish fully identified physical metrics.
The common view has seven unresolved candidates, three command mirrors and
twelve telemetry fields. See [the current validation report](amd-granite-validation.md)
for tested builds, reproducible outputs and remaining hardware limitations.

## Follow-up on the scale for core rows 9/10

Additional disassembly inspection, separate from the executable proofs above,
locates their sources at `0x11278 + 0x3c * local_slot + 0x1f4/0x204`.
Producer `0x2f930` reads two packed words at
`0x1602e00 + 0x20000 * local_slot + 0x238/0x23c`, retains previous words when
individual reads are zero, combines their bytes and applies policy bounds.
Its floating paths scale selected codes by approximately 1/166 and the float
at `0x119a0`. Initializer `0x2ad20` copies that float from offset 8 of an incoming
record; handler `0x2b33c` obtains a 24-byte record before initialization.

The [request-policy follow-up](amd-granite-performance-policy.md) now traces
the record back to the Fmax MHz-to-GHz conversion, checks its SMU and MP5
handlers, and replays the request arithmetic and PM averaging. Rows 9/10 are
therefore displayed as GHz policy values. This does not establish measured
frequency, independent setting readback, freshness or an OS core binding.
