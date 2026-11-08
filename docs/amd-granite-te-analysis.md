# Granite IA32 TE inputs and analysis-only PE wrappers

The early core-status producer and consumer are IA32 TE images. Their original
bytes remain the inputs for [the HOB instruction replay](amd-granite-map-binding.md).
REA/Ghidra instead receives separately hashed PE32 wrappers that preserve every
file-backed section byte, section RVA, section flag, ImageBase and entry RVA.
Neither wrapper is a recovered original PE file or an installable firmware image.
The pipeline never executes either firmware image on a processor or device.

The fixed Ghidra 12.1.4 stock loader inventory and FileFormats inventory were
checked for native TE support. Its
[PE loader](https://github.com/NationalSecurityAgency/ghidra/blob/Ghidra_12.1.4_build/Ghidra/Features/Base/src/main/java/ghidra/app/util/opinion/PeLoader.java)
delegates to
[PortableExecutable](https://github.com/NationalSecurityAgency/ghidra/blob/Ghidra_12.1.4_build/Ghidra/Features/Base/src/main/java/ghidra/app/util/bin/format/pe/PortableExecutable.java),
which requires a DOS header before parsing the PE header; the original `VZ`
TE headers do not qualify. No TE loader was present in those stock inventories.
This is the reason for a deterministic analysis wrapper, not a claim that no
third-party Ghidra extension can ever handle TE.

| Module | Original TE bytes | Original TE SHA-256 | Derived PE32 bytes | Derived PE32 SHA-256 |
|---|---:|---|---:|---|
| `core-status-producer` | 33,584 | `7464250e2d6f8e3268af5da403997d57c86485ef86b25a23bc2b4af6abc65f1f` | 33,984 | `ecf94dafb435b1058bf5351960b18910674170bf066e148a9bc73616232c0992` |
| `core-status-consumer` | 20,736 | `c76d2ac61c511c113406297113d999f9cf7f1827c3b4413029907d5f7966ecb6` | 21,120 | `c821f23f2ac30abfb9a4b6a93ab2920b9b15a19cab8be836e1409c77b1c635a9` |

## Exact address preservation

Producer ImageBase is `0x09ac6280`, stripped delta is `0x190`; consumer ImageBase
is `0x09b07988`, stripped delta is `0x180`. For these pinned sections only,
the original PE section raw position equals its RVA. The TE file position is
`RVA - delta`, while the preserved virtual address is `ImageBase + RVA`.
These are distinct coordinates. The wrapper validates that equality for each
section instead of assuming that every TE image uses this layout.

| Function | Original RVA | Original TE file offset | Wrapper file offset | Preserved virtual address |
|---|---:|---:|---:|---:|
| Producer module entry | `0x3c0` | `0x230` | `0x3c0` | `0x09ac6640` |
| Producer CPU/model gate | `0x409` | `0x279` | `0x409` | `0x09ac6689` |
| Complete HOB producer | `0x66d` | `0x4dd` | `0x66d` | `0x09ac68ed` |
| Consumer module entry | `0x399` | `0x219` | `0x399` | `0x09b07d21` |
| HOB consumer | `0x1f7b` | `0x1dfb` | `0x1f7b` | `0x09b09903` |
| Consumer GUID lookup | `0x434a` | `0x41ca` | `0x434a` | `0x09b0bcd2` |

The producer profile additionally seeds RVA `0x997`, `0x4789`, `0x4917`,
`0x492a` and `0x4a21`. All eight producer and three consumer seeds sent to
Ghidra are full virtual addresses. REA queries use wrapper file offsets and
must resolve both the mapped address and exact procedure start to the expected
original virtual address. The returned REA Evidence IDs refer to the derived
PE input. Each session retains the original TE hash and complete derivation
report; this does not manufacture REA evidence for the original TE input.

The wrapper synthesizes DOS, COFF and PE32 headers and uses file/section
alignment `0x20`. It retains the TE relocation and debug directory RVAs and
their underlying section bytes but does not apply relocations or rebase the
image. It omits `IMAGE_FILE_EXECUTABLE_IMAGE`, retains the EFI subsystem, and
uses an `.analysis-pe.bin` filename. Section execute flags are preserved for
static disassembly. Original TE files remain separate and unchanged.

The tool rejects unsupported machine/subsystem, truncated or overlapping
sections, auxiliary COFF tables, unexpected overlays, unsupported raw/RVA
layouts, invalid directories and addresses, and headers overlapping section
bytes. The pinned pipeline additionally requires both exact original and
derived hashes. Generic parser acceptance alone is not firmware authenticity.

## Reproducible checks and evidence limits

`analysis/tests/test_firmware_te_pe.py` has twelve test groups. Ten exercise
literal offsets, malformed images, metadata/byte/address tampering and identity
rejection. The other two use the real original TE images and independent
`pefile 2024.8.26` parsing of the derived PE. They compare every file-backed
byte and RVA mapping across all six sections, both ImageBases, both entry
RVAs and all eleven requested procedure locations. Without explicit original
inputs those two integration groups are skipped; the cloud workflow supplies
both and expects no skips.

```sh
PYTHONPATH=build/firmware-tools/python python3 analysis/tests/test_firmware_te_pe.py \
  --producer build/firmware-input/uefi-granite-core-status-producer.bin \
  --consumer build/firmware-input/uefi-granite-core-status-consumer.bin \
  --output build/firmware-evidence/granite-te-wrapper-tests.json
```

The workflow now requests nine UEFI REA sessions, including the two derived
IA32 inputs, and forty-six separate instruction-replay proofs. The wrapper
unit/integration report and its two derivation reports are validation artifacts,
not three additional firmware proofs. Source extraction, byte/address
equivalence, static decompilation and synthetic instruction replay remain
distinct evidence classes. None verifies the user's installed BIOS, physical
CCD/OS-core binding, independent rails device protocol or live hardware effect.
