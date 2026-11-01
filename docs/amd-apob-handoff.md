# APOB handoff, complete-container import and public layout comparison

Reference: ASUS ProArt X870E-CREATOR WIFI BIOS 2503. The actual 9950X3D BIOS
and selected runtime provider remain unknown. This extends the
[topology protocol study](amd-apob-topology.md), without binding a PM slot to
a physical CCD or OS CPU and without enabling a voltage command.

## HOB to APOB record

REA 6.7.0 resolves the additional lookup functions in both pinned providers:

| Stage | Provider A | Provider B | Observed behavior |
|---|---|---|---|
| Cached HOB list | `0x96c` | `0x9b4` | Return cached list; a missing cache invokes a separate UEFI lookup |
| GUID HOB lookup | `0x99c` | `0x9e4` | Walk HOB lengths; match type 4 and the 16-byte GUID |
| APOB descriptor | `0x2ac0` | `0x2a30` | Find `30b174f3-7712-4cca-bd13-d0b8a8801997`; return HOB address +24 |
| Record selection | `0x2bb0` | `0x2b20` | Use descriptor byte +0 and pointer +8; check APOB header and select the requested instance |

The descriptor has a size field at +4 in AMD's public structure. The selected
firmware path does not use it to bound the container. Instead it reads the
container's own size at +8. The replay independently varies the descriptor size
between 0 and `0xffffffff` without changing the selected record.

`firmware-granite-apob-handoff.py` runs **251 cases per provider, 502 total**
against the original instruction bytes in Unicorn 2.1.4. Each set includes
216 valid selections, 28 instance/duplicate cases, six unavailable/error
cases and one bounded zero-length-record stall. It varies record position,
header extension length, instance and version. Duplicate matching instances
return EFI_UNSUPPORTED; absent matches return EFI_NOT_FOUND. A zero-length
nonmatching record does not advance and reaches the research instruction
budget. This is not a firmware timeout.

The HOB list, descriptor and records are synthetic. The list is already cached;
UEFI initialization does not execute. Diagnostic callbacks and assertion
reporting are disabled fixtures. Only the selected lookup functions may run,
and writes are confined to stack/output memory. No host I/O instruction,
hardware acquisition, record producer or HMAC verification executes.

## Complete APOB import in both interfaces

GUI **Open firmware topology file…** and CLI
`amd-topology-decode --file apob.bin` now accept either the original single
record or a complete APOB container. Both use the same decoder and the same
1 MiB bounded regular-file reader. The container is recognized by its APOB
signature and must meet all of these conditions:

- Version at least 5; declared size equals the entire file.
- First entry begins at or after the 16-byte container header and within the file.
- Every record has its complete 48-byte header, a length of at least 48,
  and a length within the remaining file, including records after the topology.
- Exactly one group 3/type 3 record exists. Multiple instances are rejected
  instead of silently selecting a socket/die; a caller can extract an intended
  single record separately. The selected record still needs the full 456-byte
  reference getter span.

The decoder walks to the declared end before exposing groups. Invalid trailing
records therefore discard a previously encountered valid topology. Unknown
records and container header extensions are skipped by their validated bounds.
The application does not reproduce the firmware's unchecked length loop or
fixed 256-pointer collection buffer.

JSON preserves the original record schema and adds `input_bytes`,
`record_header_bytes: 48`, `record_hmac_verified: false` and optional `container`
metadata (version, count and selected offset). Container decoding changes
`source` to `offline-apob-container`; failed decoding returns no groups and no
selected container metadata. GUI shows the same location/count before the
unchanged identifiers and flags. Neither frontend prepares hardware commands.

## Public AMD layout evidence and its boundary

The [source manifest](validation/amd-apob-public-layouts.json) pins all four
files by commit, Git blob hash and SHA-256; downloaded bytes were checked
against the corresponding public trees.

AMD's [Genoa APOB definitions](https://github.com/openSIL/openSIL/blob/9648171d730771cc3574d7d4d13581d22d163319/xUSL/Include/ApobCmn.h)
define the type header as four DWORDs followed by a 32-byte HMAC. Thus the
complete record header is **48 bytes**, correcting the earlier wording that
called only the first 16 bytes the header. Those 32 bytes were already skipped
by the application; they are not newly treated as trusted data.

The same source names the nested bytes `PhysCcdNumber`, `PhysComplexNumber`,
`PhysCoreNumber` and `IsThreadEnabled`. Its 2-complex/8-core/2-thread nested
strides are 51, 25 and 3 bytes, matching the reference provider's accesses.
[Its lookup implementation](https://github.com/openSIL/openSIL/blob/9648171d730771cc3574d7d4d13581d22d163319/xUSL/Apob.c)
uses group/type 3/3 and `(socket << 8) | die` for an instance.
These independently support the record format and the intended names of these
stored fields. This is a cross-platform comparison, not proof of the target
machine's physical numbering or its PM-to-APOB correspondence.

The differences matter: Genoa declares 12 CCD entries while the pinned ASUS
getters stop at eight. AMD's [Turin definitions](https://github.com/openSIL/openSIL/blob/9de7d45669af452c8563acb82ced47ba15a7f61a/xUSL/APOB/BRH/Apob-BRH.h)
declare 16 CCDs, one complex per CCD and 16 core entries per complex. A common
group/type or HMAC header is therefore insufficient to choose a full topology
layout for arbitrary processors. The product retains its explicit ASUS
reference layout and does not rename unverified stored IDs into OS identities.

REA also resolves a provider of `2ADCD53F-4C94-40BB-BBA5-7DEBFF8A13AC`, module
`CC8A8528-23F3-427E-AF41-C91E3297B622` (SHA-256
`4ec3a9dd5c115f221349071bc5aceb3cc0901190808dc3fea4e72ae80801cea0`).
Its count helpers `0x5338/0x5364` obtain fields from the register reader
`0x4a38`, including function 4 offsets `0x180/0x184`. This is static evidence,
not an executed register proof; it does not supply an MP5 PM-slot link.

The eight units (core 0/4/12, CCD 0/1/2/6/8), target runtime record provenance,
PM/physical/OS binding and independent second-rail write/device readback remain
unresolved. HMAC presence, successful parsing and mailbox acknowledgement do
not establish any of them.
