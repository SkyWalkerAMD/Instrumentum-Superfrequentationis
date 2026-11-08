# Granite UEFI query and reference SMN acquisition

A newly traced UEFI query supplies a concrete acquisition path for the
presence field behind the [offline mapping diagnostic](amd-granite-mapping-diagnostic.md).
It reads SMN coordinate `0x5d3bc`, bits 23:22, matching the independently replayed
SMU fuse helper's two-bit field at SMU-local `0xf885d3bc`. A second immutable
UEFI module implements the called read service through PCI index/data offsets
`0xb8`/`0xbc`. This is a static and synthetic path, not a live mask capture.

Reference firmware is ASUS ProArt X870E-CREATOR WIFI BIOS 2503. The user's
installed BIOS and runtime protocol selection are unknown; the machine is
unavailable. There is no new native read or write allowlist.

## Pinned artifacts and protocol chain

| Artifact | Capsule file GUID | SHA-256 |
|---|---|---|
| Topology provider A | `9521A9E1-759B-48F6-96A1-EB7B8997243E` | `8ea4e45882f053775e87943b20b092d2086787a9500a120fb9f35af62d424295` |
| Register service, 38,752-byte PE | `AC991242-FBB0-42FA-AEF4-AA9746FD68E1` | `61a6f2850ea3cd207264d4c831dbaff253a53137eefcc5a3890e254698c10963` |

Provider A's entry `0x72c` is already covered by the model/package gate study.
Its retained REA/Ghidra pseudocode Evidence ID is
`ev_444bc9991726274d218d7a0093093f3b1617bd88a7236bd1247407737fb1f917`.
After the accepted gate, `0x7ab..0x7d1` passes GUID
`5c278ea8-cf6b-4626-a7d6-7cfd23f30719` and table `0x3498` to the firmware's
protocol installer. Table slot `+0x10` points to the newly examined `0x1250`.

That query obtains GUID `264272b3-81d1-453a-8503-8989f4382e68` and calls its
`+0x20` member with `(service, socket 0, register_coordinate, output_pointer)`.
The new register-service module passes this GUID and table `0x8020` to the
installer at `0xc45..0xc67`; table `+0x20` points to `0x428c`.
The reproduction executes both argument-building slices with a recording
installation callback. It does not execute a real firmware installer or
assert that either installation succeeded.

The register-service installer publishes the table before its later
`0xb40f00` policy checks. Those later checks do **not** prove a model-specific
installation gate. Two other capsule modules also contain install sites for
this read GUID: `50578334-FCFD-4DCA-B3AF-47DD76E946CF` and
`AD82C06C-34C0-44F0-BA98-801C3CA7592F`. Finding several candidates cannot select
the active provider. The new module is therefore a pinned reference read
implementation, not an authenticated runtime service.

## Query outputs and error boundaries

The checked `0x1250` body performs these operations:

1. Read `0x5d3bc`, shift right by 22, and consider only bits 0 and 1.
2. For each set bit, read `0x304a03dc | (target << 25)`, giving coordinates
   `0x304a03dc` and `0x324a03dc`. Set an output-mask bit when that word's low
   byte is not `0xff`.
3. Read target 0 again if output-mask bit 0 is set; otherwise read target 1,
   **including when no target passed the prior test**.
4. Return that final word's low byte, constant 8, and bit 8 through optional
   output pointers, together with the output mask from step 2.

The low byte has not been equated to the SMU initializer's enabled-core mask
at `0x203c003c`/`0x20bc003c`. The one-bit output has not been authenticated as
the diagnostic's SMT input; its relationship to SMU selector `0x203c0030`
bit 10 remains open. No complemented byte or ordinal is automatically fed
into the mapping diagnostic. Firmware target indexes are not claimed to be
physical CCDs, APIC groups or OS CPU numbers.

The query rejects when its mask, byte and bit output pointers are all null;
providing only the constant-8 output pointer does not satisfy that check. A
nonzero LocateProtocol result becomes `EFI_DEVICE_ERROR`. In contrast, the
query **does not check the read service's returned status**. Four synthetic
descriptor-lookup failures demonstrate that the query can return success
after no SMN data read. Scratch RAM is explicitly zero in those fixtures; it
is not a hardware fallback value. A future collector must validate reads
itself rather than copy this unchecked-success behavior.

## Read service and reproduction

`0x428c` asks helper `0x411c` for the selected socket descriptor. The helper's
descriptor and status are fixtures in this experiment. On success, the reader
uses `(descriptor_u32_at_0xe & 0x0ff00000) | 0xb8` as its PCI index coordinate,
then executes `0x52cc`, `0x53f0`, `0x5240` and `0x5198` on synthetic memory:

```text
read old PCI index
write requested SMN coordinate to PCI index
read PCI data
restore old PCI index
```

The proof runs the actual load/store instructions against in-memory PCI
windows. Its write hook allows only the index address, stack and result
buffers; a data-register write fails the proof. Four bus coordinates and
three prior-index values verify masking, data offset `+4`, and index restore.
Peripheral replies, protocol binding and socket-descriptor lookup remain
supplied fixtures. No host memory mapping, device access or CPUID is performed.

```sh
python3 analysis/tools/firmware-granite-map-acquisition.py \
  --provider-a build/firmware-input/uefi-granite-topology-provider-a.bin \
  --register-service build/firmware-input/uefi-granite-register-service.bin \
  --output build/firmware-evidence/granite-map-acquisition.json
```

The script uses the existing pinned `unicorn==2.1.4` and `pefile==2024.8.26`
dependencies. It checks both artifact digests, mapped PE bytes and every
executed instruction; 304 query cases execute 215 distinct source instructions,
824 synthetic data reads and 1,648 synthetic index writes. Separate publication
slices record the two GUID/table pairs. The trace hash is
`57d463abf646e7a5b571904f9fcd1fc97987d91f52774bb87060d9a50bf1ca05`.

The new REA profile `granite_register_service` retains function evidence for
`0xbec`, `0x428c`, `0x411c`, `0x52cc`, `0x5198`, `0x5240` and `0x53f0`;
provider A's study adds `0x1250`. Static publication evidence and synthetic
read results establish an inspectable acquisition candidate. Runtime provider
selection, matching core-mask semantics and physical/OS identity remain open.
