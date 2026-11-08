# Granite Curve Shaper stored offsets and downstream interpolation

The product exposes saved offsets as a read-only table. Scope is ASUS ProArt
X870E-CREATOR WIFI BIOS 2503, SMU/MP5 98.84.0. The user's 9950X3D is unavailable
and its installed BIOS is unknown. No transport, fuse or physical behavior was
observed on that machine.

## Retained evidence and identity

The original instructions come from the retained REA/Ghidra workflow artifact
`rea-granite-study`, run `42432173976`, artifact `13598640780`, archive SHA-256
`9a06b1d89da37c67d8327457a33ecffdf3e5a6543ceba84da8eef5d4237e69d9`.
Function exports are under `build/granite-study-42432173976/{smu,mp5}`.
Those exports do not contain individual REA Evidence IDs for these Xtensa
functions; the stable identifiers below are local claim IDs, not invented
REA Evidence IDs. Xtensa functions are identified here by component, address
and checked raw bytes. The separate UEFI REA records do not supply Xtensa
function evidence IDs.

| Input | SHA-256 |
|---|---|
| SMU body | `5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596` |
| MP5 body | `fb5d96c17b909cc4f681523ccc931796e27c7457ba5ad637a3d4fce1c78ce556` |

[`firmware-granite-curve-shaper.py`](../analysis/tools/firmware-granite-curve-shaper.py) checks every exported instruction against
these body bytes before running bounded synthetic slices. It uses the existing
limited interpreter, not a full Xtensa/FPU emulator. Fuse replies, mailboxes,
transport completions and reciprocal results are explicit fixtures. A synthetic
result is not hardware acceptance.

## CS-01: five-word getter returns saved runtime offsets

RSMU table `0x6fa4`, selector `0x84`, flags `6`, resolves to `0x2ac14`.
The instruction loops copy 15 bytes from `0x13993..0x139a1` into five words:

`word[tier] = tier | byte[3*tier] << 8 | byte[3*tier+1] << 16 | byte[3*tier+2] << 24`

Here `tier=0..4`. `0x15ac` writes mailbox arguments 0..4, while argument 5 is
untouched. The handler acknowledges `1`; it has no capability or OC-mode query.
It neither clamps nor adds a baseline during reads. These are saved runtime
offsets in the SMU, not reads from MP5, a VRM or an applied-voltage sensor.
Before the apply helper has normalized them, the stored bytes can represent
any signed8 value. A reader must preserve all `-128..127` values.

## CS-02: one-tier write normalizes and forwards the entire table

RSMU `0xa6`, flags `6`, resolves to `0x2abcc`. It extracts the low seven bits
of argument 0 as tier, rejects tiers >=5, and requires `0x2c910(2)` permission.
The permission is the same fuse/platform capability exposed by `0x6f` bit 2.
The handler itself has no manual-OC check. Rejection acknowledges `0xff`.
Argument bit 7 is ignored here; it is not a deferred-commit flag.

On acceptance, the handler overwrites exactly three saved bytes for the
selected tier, then calls `0x2ab74`. That helper:

1. Interprets **all 15** saved bytes as signed8 and clamps each to `-50..30`.
2. Stores those clamped bytes back, including tiers not selected by the user.
3. Adds the corresponding signed8 baseline at `0x13984..0x13992`.
4. Packs each sum back into one byte, discarding carry/sign extension.
5. Broadcasts five MP5 `0x47` packets, then a sixth packet `0x00000080`.

For example, an unselected saved value `31` changes to `30`; `-51` changes to
`-50`. A synthetic baseline `127` plus runtime offset `30` is packed as `0x9d`
and interpreted downstream as `-99`. The baseline's actual target values,
provenance and effective bounds remain unresolved. An offset getter alone
does not reveal or validate those baseline values.

The broadcast helper `0x1d760` is already checked by the all-core CO study:
it uses target mask `0x6816`, not neighboring `0x6814`, and treats any nonzero
completion as finished. A failed nonzero downstream status is not propagated
as a failed host acknowledgement. Therefore host ACK and matching saved
offsets cannot establish a successful MP5/device application.

## CS-03: MP5 storage is a 3-by-5 float matrix

MP5 table `0x13298`, message `0x47`, flags `2`, resolves to `0x2a048`.
For packets with bit 7 clear, three packed signed8 values are converted exactly
to binary32 and stored at:

`0x19bd4 + 4*tier + 20*column`, with `tier=0..4`, `column=0..2`.

Only those three cells change on that packet. This handler does not range-check
the tier; the host-facing handler is the demonstrated range gate. It must not
be offered as an unrestricted raw MP5 writer.

Bit 7 set instead calls `0x29b14` followed by `0x29f9c` and acknowledges through
`0x2e834`. The input's remaining bits do not select individual cells on that
branch. The proof models those commit calls as boundaries during the packet
tests and separately executes the relevant `0x29b14` construction slices below.
Full `0x29f9c` per-core cache rebuild and physical effects remain outside this
proof's execution coverage.

## CS-04: five frequency anchors and three temperature anchors

The beginning of `0x29b14` reads signed8 fuse values at `0x267d`, `0x2685`,
`0x268d` and stores three float anchors at `0x19b34 + 4*column`.
The loop at `0x29bd9..0x29c57` reads five 6-bit fuse values at
`0x25c9 + 6*tier` and multiplies each by binary32 `0.1`, storing frequency
coordinates at `0x19b48 + 4*tier`.

The same loop constructs each adjustment value from a signed10 fuse plus the
signed8 combined baseline/runtime count saved by message `0x47`. The three
signed10 fuse rows start at `0x25e7`, `0x2619`, `0x264b`, with a 10-bit step per
tier. Result matrix addresses are `0x19b5c + 20*column + 4*tier`.
No volts/millivolts conversion is present on these input/storage paths.

The names and order are corroborated by pinned primary implementation sources:
[Zen4Settings](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Settings/Zen4Settings.cs)
names the five frequency tiers minimum/low/medium/high/maximum;
[SetCurveShaperMargin](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/SetCurveShaperMargin.cs)
packs the low/medium/high margins and applies `-50..30` input bounds;
[GetCurveShaperMargin](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/Smu/Commands/GetCurveShaperMargin.cs)
uses the getter. These sources agree with the retained bytes but are not proof
that a different SMU version uses the same protocol. Their bit-7 setter choice
does not alter this exact SMU handler's ignored-bit behavior.

AMD's [Curve Shaper guide](https://docs.amd.com/r/en-US/68886-ryzen-master-user-guide/Curve-Shaper)
describes temperature/frequency bands, and ASUS's
[X870E BIOS manual](https://dlcdnets.asus.com/pub/ASUS/mb/13MANUAL/E25062_ROG_X870E_Series_BIOS_Manual_EM_WEB.pdf?model=ROG+CROSSHAIR+X870E+HERO)
describes overlapping bands and count magnitudes. The latter is a different
ASUS board family; it supports terminology, not this board's target acceptance.
The actual fuse values are unavailable. Fixed claims such as a particular GHz
threshold or `-5/50/90` temperature anchors are therefore not justified here.

## CS-05: adjacent points interpolate and can extrapolate

The decompiled pseudocode for `0x297a0` is empty, despite its 86 original
instructions. Replaying those instructions is essential. They interpolate
across the frequency anchors, then across two adjacent temperature anchors,
and add a separate per-core base-curve term. The test neutralizes that separate
term rather than pretending to have reconstructed every electrical model.

The exact dyadic fixtures show that a frequency below the first anchor uses
its value, while a frequency above the last anchor extrapolates the final
segment. Temperatures outside the first/last anchor likewise extrapolate.
These are overlapping interpolation controls, not 15 isolated switches.
Tests use exact dyadic reciprocal results at `0x327b0`; they do not emulate
the processor's approximation algorithm, rounding flags or exceptional values.

## Original OCTool reference and implementation decision

ASCII and UTF-16LE string scans found existing Curve Optimizer and Boost Curve
names, but no Curve Shaper or tier/margin terminology in the two supplied
original binaries:

| Original input | SHA-256 |
|---|---|
| Linux `build/input-audit/octool` | `44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10` |
| Windows `Tool.exe` | `04a6a6043b9b653737a3e7030ded0cd3f67289575f6959ef8096b0b1a2a1c609` |

Linux's symbol table includes `AMD_PBO::set_curve_opt`, `get_co`, `set_fmax`
and `set_core01_rails`; matching the word `shaper` returned font-shaping code.
This limited negative result does not prove the original lacks the feature,
and a new implementation must not be counted as an exact original UI recovery
without a corresponding original call path.

The shared core provides an exact-version **read-only saved Curve Shaper offset
table**, labelled in counts with its SMU-storage provenance. GUI and CLI use
the same decoder. The reader validates low-byte tier tags `0..4`, retains all
signed8 counts, and ignores the unused sixth word. The sequence
`02/6f/84/6f/02` brackets the read with complete capability and exact-version
comparisons. A capability or OC-state change invalidates the read; a stable
missing bit 2 or active OC mode does not prevent this read-only query because
the getter does not check either. CPU/PCI target checks remain required.
Reading saved offsets does not imply physical application or matching BIOS
baseline. The raw five words remain available alongside the decoded counts.

Do not enable the writer solely from its command name. Any subsequent typed
writer needs an explicit whole-table effect policy, stale table/capability and
version checks, a clearly scoped stored-offset readback result, and resolution
or accurate presentation of the separate baseline and downstream failure issue.
The product does not enable `0xa6` or raw MP5 `0x47` writes.

## Reproduction and coverage

```sh
python3 analysis/tools/firmware-granite-curve-shaper.py \
  --smu build/firmware-input/smu-62-54-body.bin \
  --smu-study build/firmware-evidence/smu/smu-study.json \
  --mp5 build/firmware-input/mp5-62-54-body.bin \
  --mp5-study build/firmware-evidence/mp5/smu-study.json \
  --output build/firmware-evidence/granite-curve-shaper.json
```

Passed locally with Python 3.14:

| Check | Cases |
|---|---:|
| Four original SEXT-7 instructions, all low bytes and three high-bit patterns | 3,072 |
| Getter, every signed8 encoding in each of 15 cells | 256 |
| MP5 storage, every signed8 encoding at each tier | 1,280 |
| MP5 commit branch patterns | 5 |
| Host setter, permission/range/bit7/table effects and composed MP5 storage | 2,560 |
| Original signed fuse helper, complete 8/10-bit input sets | 1,280 |
| Three temperature fuse anchors, all signed8 encodings | 256 |
| Frequency and 3-by-5 fused adjustment model construction | 1,024 |
| Exact dyadic two-axis interpolation, every cell basis and boundary queries | 28,611 |

The setter matrix includes 1,760 rejects, 400 accepted cases changing an
unselected tier and 340 accepted cases with signed8 baseline-addition wrap.
These are deliberately adversarial synthetic values, not observations of the
user's BIOS configuration. Trace hashes are retained in the JSON report.
