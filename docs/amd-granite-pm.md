# Granite Ridge / 9950X3D PM capture

This profile is a separate implementation of the Granite Ridge PM transport and
stored table layout. It does not enable Shimada tuning commands on Granite Ridge.
GUI and CLI use the same core decoder and capture implementation.

The follow-up [field-unit and CCD identification study](amd-granite-semantics.md)
recovers four core-row units, adds optional L3 geometry to CPUID topology and
maps the exact two-slot native table to firmware target IDs 0/1.
The remaining unknown units and PM-to-physical-core association stay explicit.

The user's target is a Ryzen 9 9950X3D with an ASUS X870E Creator board, interpreted
as ProArt X870E-CREATOR WIFI. The installed BIOS version is unknown. ASUS's public
2503 capsule is an offline reference, not a claim about the installed firmware.
No motherboard firmware has been flashed or executed by this study.

## Use

```sh
octool-cli diagnose
sudo octool-cli amd-pm-native-read --cpu 0 --platform granite-ridge > granite-pm.json
octool-cli amd-pm-decode --file granite-pm.json
octool-cli amd-pm-decode --file table.bin --version 0x620205
```

Choose a CPU allowed by `diagnose`; 0 is only an example. In the GUI's AMD PM
panel, select **Granite Ridge 98.84.0**, choose the logical CPU, and click **Read
native PM table**. The existing installed-driver and offline import paths also
accept the two exact Granite versions. `Copy JSON snapshot` works across both
frontends. Import derives values from raw bytes and does not certify live metadata.

Native capture requires bare-metal AMD family 1Ah/model 44h, root PCI 1022:14d8 at
00:00.0, CPUID physical address width 32..52, and SMU **98.84.0**. It accepts only
`0x620105` / **1828 bytes** or `0x620205` / **2452 bytes**, with matching firmware
length. Other firmware versions are refused, including 0x621101/0x621201 variants.

The RSMU message/response/argument registers are 0x3b10524 / 0x3b10570 /
0x3b10a40..54. Commands are 2 (SMU version), 5 (table version and size), 4 (64-bit
address), 3 (transfer), then 5, 4, 2 again. Six argument words are zero for every
request. The address must be non-null, word aligned, and entirely inside the
physical address width. Identity, metadata and complete reads must all succeed;
changed metadata, access faults, cancellation and timeouts discard the buffer.
There is no automatic retry of a rejected firmware transfer.

Mailbox/index registers are written to request telemetry transfer; no tuning
setting is requested and no physical-memory write is issued. Transfer acceptance
and stable metadata do not prove freshness, atomicity or absence of another
mailbox client. `--bclk-mhz` is unavailable for this profile. Optional `--map-ccd`
accepts only the exact two-slot table and derives firmware IDs 0/1 from the
pinned initializer after the same metadata rechecks; it sends no extra query.
One-slot mapping is refused because the target cannot be inferred. The GUI's
**Map firmware CCD slots** option behaves identically.

## Stored layout and limits of interpretation

| Region | Layout |
| --- | --- |
| Common prefix | 0x4b4 / 1204 bytes |
| Core storage | 18 rows × 8 × N words, starts at 0x4b4 |
| CCD storage | 12 rows × N words, directly after core storage |
| Width N | 1 for 0x620105; 2 for 0x620205 |
| Complete inspector | 457 or 613 raw 32-bit words with binary32 interpretation |

N is a storage width. The pinned two-slot native path can identify firmware CCD
targets, but does not associate them with CPUID CCD IDs, cache versus frequency
CCDs, active cores or Linux CPU/APIC IDs. Numeric row labels deliberately
retain that distinction. Raw bits and byte offsets are included. Non-finite
binary32 interpretations retain their bits and return a partial result, not zero.
The complete word inspector makes no promise that every word is a live float sensor.

Eight common positions have **candidate** labels from the pinned public Granite
reference: FCLK/UCLK/MCLK at words 71/75/79, VDD_MISC 58, VDDCR_SOC 83,
CLDO_VDDG_IOD/CCD 259/261, CLDO_VDDP 269. Units remain unverified and no BCLK
correction is applied. These candidate positions are not evidence for rail setting
commands. Words 589/590 and 591/592 are not labeled as CCD power. The upstream
9950X3D study specifically rejected that interpretation. The temperature and
three residency rows now have static unit evidence; other physical units, physical
core mapping, second-rail write/readback and remaining tuning remain separate work.

## Reproducible evidence

Public inputs and fixed checksums:

| Input | SHA-256 |
| --- | --- |
| ASUS 2503 ZIP | fc7211637882a09ccf01059d4ee1681c0a9876e2aff58d57949d903664012d9d |
| ASUS CAP | f34be92fa4577c976b299495d29f66e437839165cfecf2856d030487880b20a9 |
| SMU 98.84.0, 262144 bytes | 5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596 |
| MP5 98.84.0, 262144 bytes | fb5d96c17b909cc4f681523ccc931796e27c7457ba5ad637a3d4fce1c78ce556 |

`analysis/tools/firmware-granite-input.py` checks archive, capsule, duplicate PSP
payloads and decompressed bodies. `rea-granite-study.yml` uses pinned REA 6.7.0,
Ghidra 12.1.4 and UEFIExtract A76; it never launches vendor executables. REA resolves
both UEFI entry points and the transport helpers at file offsets 0xbb4/0xbf4 in
the separately hash-pinned PE modules. Static SMU and MP5 discovery is independent
of Shimada offsets.

`firmware-granite-proof.py` checks instruction bytes against both pinned bodies,
replays the MP5 integer scatter slice 0x2f5af..0x2f657 for all three N/ordinal
combinations, and verifies exact, nonoverlapping table coverage. It also replays
15 metadata combinations, four 64-bit addresses, and the firmware version query.
The metadata mailbox helpers are explicitly stubbed; this is a synthetic proof
of handlers and storage, not a live mailbox or timing test. Floating-point
normalization is excluded from this integer-only proof. The separate semantics
experiment verifies the corrected postincrement profile and selected unit paths;
reciprocal accuracy is not emulated. Decompiler pseudocode alone loses Xtensa
hardware loops. The topology experiment also replays the formerly missed
large-frame initializer and follows its packet through the PM copy loops.

Source references:

- [ASUS ProArt X870E-CREATOR WIFI BIOS](https://www.asus.com/de/supportonly/proart%20x870e-creator%20wifi/helpdesk_bios/)
- [Pinned ryzen_smu transport and version definitions](https://github.com/amkillam/ryzen_smu/blob/d2983668300dd2a598e5a7dc40e71ce0678cc270/smu.c)
- [Pinned Granite Ridge 9950X3D research](https://github.com/Kyworn/gnr-smu/blob/def79d66d5431dfc4b6fc773f0b99fb99c8e378d/docs/architectures/granite_ridge/9950X3D.md)

### Remaining rails protocol research

The separately replayed 61h handler at 0x2a644 extracts selector bits 16:9 and
code bits 8:0. It requires byte[0xae68] != 0 and selector < byte[0xae67], clamps
the code to byte[0x13549] when that cap is nonzero, and stores one halfword at
0xaf28 + 2 × selector. `firmware-granite-rails.py` checks 21,432 synthetic cases,
including all 512 input codes, the second slot, disabled/range failures, clamp
boundaries and ignored upper bits. Another 18,432 cases connect that storage
to a shared-floor consumer slice at 0x2100c. Its first/last selection and
`0.245 + code * 0.005` conversion do not establish independent physical rails,
the encoded hardware-request path or a paired second-rail getter.

Granite's A5 handler reads and then discards the caller's argument. Its packed
output comes from two fixed indexed float sources under 0x9d20; it cannot be
used as the Shimada per-CCD presence query. The 6Eh handler reads a fixed float at
0xaee4, distinct from 61h's selected halfword array. Neither query establishes a
second-rail setter/readback pair. The GUI and CLI therefore do not enable a
Granite 61h writer or label either query as verified rail voltage readback.

Synthetic core, CLI and GUI tests cover layout boundaries, imports, non-finite
words, profile gates, metadata changes, partial reads and cancellation. The
sanitizer suite includes Granite decoding. Distribution packaging and portable
core builds are cloud checks; neither those nor VM tests replace a test on the
user's 9950X3D and installed BIOS. See the accompanying validation report for run
IDs and actual outcomes.
