# Granite Ridge four-state PM policies

The shared GUI/CLI decoder now exposes all **20 common DPM fields** for the
98.84.0 reference. Four previously unverified state-0 candidates gain a
qualified interpretation; 16 other state/share fields become visible. This
brings the common view from 23 to **39 fields**, with no change to raw table
size. These are software policies and sampling shares. They do not establish
the eight unresolved core/CCD units, a physical target map or a second-rail
write/readback protocol.

In the GUI AMD PM panel, open a snapshot or a supported native capture and
choose **Granite Ridge DPM policies**. The CLI's existing `amd-pm-decode` and
`amd-pm-native-read --platform granite-ridge` commands emit the same fields.
Import recalculates labels, units, state numbers and evidence flags from raw
bytes. Metadata supplied by a capture cannot certify a measurement or physical
identity. The existing common view and full raw-word view remain available.

| PM word indices | Four exported values | Unit | Interpretation |
| --- | --- | --- | --- |
| 67–70 | DPM state 0–3 sample shares | % | State sample count × 100 / total samples; not elapsed-time residency |
| 71–74 | Clock policy A | MHz, nominal reference | FCLK identity remains a candidate |
| 75–78 | Clock policy B | MHz, nominal reference | UCLK identity remains a candidate |
| 79–82 | Clock policy C | MHz, nominal reference | MCLK identity remains a candidate; derived from rounded B |
| 83–86 | Voltage policy | V | Software table interpolation or override; physical rail unverified |

All four states are exported, including states with zero sampling share.
Clock/voltage policy values are copied directly, not averaged. A state index
does not identify a CCD, core, SVI channel, currently selected policy, or an
enabled state. Zero voltage override retains the previous policy. The share
denominator counts sampling invocations even when an invalid state contributes
to none of the four numerators. Scheduling gates do not establish a fixed
sampling period; see [the scheduling study](amd-granite-scheduling.md).

The decoder preserves already exported binary32 values without conversion,
BCLK scaling or clamping. Non-finite values retain their raw bits and produce
a field error. Negative, fractional and over-100 shares are not silently
modified. The input's contents do not prove freshness or installed firmware.

## Original-byte evidence

Reference: ASUS ProArt X870E-CREATOR WIFI BIOS **2503**, SMU **98.84.0**. The
SMU body SHA-256 is
`5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596`.
The installed BIOS and target hardware are unavailable. Firmware bytes are
never executed on hardware. REA/Ghidra exports are checked byte-for-byte
against this pinned body before the bounded interpreter uses an instruction.

All addresses below belong to the reference SMU-local address space, not host
physical memory. The base is `0x96a0`; each policy state has stride `0x50`.

| Original instruction range / function | Checked operation |
| --- | --- |
| `0x25090..0x25130` | Decode two packed 24-bit inputs; multiply by 100 / 65536 and divide by the decoded divider; store clock policies at base + `0x228/0x22c/0x230` + state stride |
| `0x1cb1c` | Decode all 256 divider codes: code/4, (2×code−64)/4, (4×code−256)/4, or 128 in the four consecutive ranges 0–64, 65–96, 97–126, 127–255 |
| `0x2531c..0x253ae` | Build two six-point tables: six-bit frequency inputs × 100 and nine-bit voltage inputs × 0.005 + 0.245 |
| `0x24db0` | Search and interpolate a table, retaining the endpoint minimum behavior |
| `0x25130..0x25169` | Recompute a sentinel policy as the maximum of the two interpolated voltages; store at base + `0x23c` + stride |
| `0x24fbc` | Nonzero 16-bit override sets 0.245 + code × 0.005; zero leaves the policy unchanged |
| `0x2cd41..0x2cd4f`, `0x2ce0b..0x2ce13` | Increment a valid state's counter and the common invocation counter |
| `0x2ce80..0x2cfac`, `0x2d00c..0x2d061` | Calculate 100 / count; export four shares and all 16 clock/voltage policies |
| `0x2d13a..0x2d14e` | Generic pointer copy leaves these 20 directly stored words untouched |

Policy C rounds B, keeps the low 16 bits, multiplies by two for configuration
mode zero (otherwise one), and again keeps the low 16 bits. It is not another
frequency counter. The firmware's constant 100 and fixed-point representation,
together with the reference-clock convention, support **nominal MHz policy**
units. They do not identify a physical clock or measure the board's BCLK.
AMD PPR 57896 revision 3.00 page 31 defines REFCLK as 100 MHz;
[AMD's original document archive](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/57896.zip)
has SHA-256 `5ffaa95d1d4438afa79f8fb025f712efb29bfcbb0d0711d85df60c5151da7288`.
The archive hash is recorded independently in the validation evidence; it is
not an assertion that MP5 timer `0x32001c0` uses that clock. The volt encoding
also agrees with the SVI3 convention in
[Richtek RT3674FE, page 80](https://www.richtek.com/assets/product_file/RT3674FE/RT3674FE_DS-00.pdf).
Neither document establishes the physical destination of this software policy.

`analysis/tools/firmware-granite-dpm.py` checks 1,152 clock-policy cases, all
256 divider codes, 744 synthetic fuse returns, 620 interpolations, 128 override
cases (16 zero-code retentions), and seven complete sampling/export cases
(140 exported words). Both fuse tables, the sentinel caller and final pointer
copy are included. Reciprocal results, fuse inputs, finite rounding inputs and
configuration are explicit fixtures; divider-zero reciprocal, arbitrary float
exceptions, peripheral effects and later auxiliary calibration are outside
the proof. A large override-code fixture is not an electrical operating range.

The interpreter now distinguishes a taken branch to Xtensa LEND from sequential
arrival at LEND. The interpolation helper jumps out of its search loop; it
must not repeat the interpolation. This follows `gen_brcond`, `gen_jumpi` and
`gen_jumpi_check_loop_end` in
[QEMU 9.0's Xtensa translator](https://github.com/qemu/qemu/blob/v9.0.0/target/xtensa/translate.c).
The new proof exercises this distinction. All previous original-byte proofs
are rerun after this change and compared against their published output.

## Remaining work

Core rows **0/4/12** and CCD rows **0/1/2/6/8** still lack physical units.
PM slots are not bound to physical CCDs or OS CPU IDs. The voltage policy above
does not enable independent second-rail writing or device readback. The three
other common candidates at indices **259/261/269** remain unverified. See
[current validation](amd-granite-validation.md) for the tested source commit,
cloud matrix, replay comparisons and hardware limitations.
