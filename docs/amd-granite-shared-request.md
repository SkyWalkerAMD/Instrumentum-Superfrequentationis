# Granite shared voltage request and its limits

Reference: ASUS ProArt X870E-CREATOR WIFI BIOS 2503, SMU 98.84.0. The installed
BIOS of the user's 9950X3D is unknown and the target is unavailable. All replay
inputs and peripheral responses below are synthetic. This extends the
[61h research](amd-granite-pm.md#remaining-rails-protocol-research), without
enabling a voltage write or claiming a second physical rail.

## Newly decoded common field

Both GUI and CLI now decode **PM float index 19 / byte offset 0x4c** as
**Shared voltage request mirror (averaged)**, in **V**, for exact versions
620105/1828 and 620205/2452. It comes from SMU-local `0x8c34`, through
accumulator `0x1534c` and the common output pointer table. The export averages
the stored floats without another conversion.

This is a software request. It is distinct from the channel-0 command mirror
at index 48 and SVI voltage telemetry at index 49. The GUI fixture deliberately
shows 1.5 V, 1.125 V and 1.25 V respectively. Their difference is preserved.
No value is clamped, converted back to a VID, or used to prepare a command.

The existing commands acquire or decode the additional field automatically:

```sh
octool-cli amd-pm-decode --file table.bin --version 0x620205
```

In the GUI, open the same PM file or use the existing native-read path and
select the common fields. The status says **Shared request mirror; not
readback**, with a tooltip explaining rounding, the shared path and retention.
JSON includes `software_request_mirror: true`, `measurement: false`,
`firmware_consumer_channel: 0` and the reference SMU-local source address.
Physical rail, freshness, setting readback, independent rail addressing and
runtime write flags remain false. Import rebuilds these fields from raw bytes.

## Complete encoding path

SMU function `0x2100c` has two different paths. Define binary32 constants
`base = 0.245`, `step = 0.005`, `gain = 200`, `epsilon = -0.00001`.
The following expressions summarize the computation; the replay retains each
actual binary32 instruction and the multiply-add ordering.

| Condition | Request value and code |
|---|---|
| `byte[0xae68] != 0` | Select the first entry of `0xaf28` when `byte[0xa19c] == 0`, otherwise the last configured entry. Convert with `base + code*step`, then take the maximum with float `0xb9a8`. |
| Enabled path code | `max(0, ceil(epsilon + (request-base)*gain))`; cap at 255 only when `byte[0xae11] == 0`. The float retains the pre-quantization request even when the code is capped. |
| `byte[0xae68] == 0` | Combine the shared input, minimum and two offsets, apply the bound, then use the same ceiling conversion. This path ignores the eight-bit cap and reconstructs its stored float with `base + encoded*step`. |

The shared float goes to `0x8c30`, and caller `0x210c4` copies it to `0x8c34`.
The encoded halfword goes to `0x8c6e`. The checked direct writer `0x20fb0`
passes that code to packet helper `0x32d6c` with **firmware channel 0**.
When the override flag at `0x8c70` is nonzero, the writer retains its old code
and command mirror. Successful scripted completion also does not return a
voltage payload: the caller calculates its software mirror from the code.

The scale agrees with the previously pinned manufacturer's
[SVI3 voltage representation, page 80](https://www.richtek.com/assets/product_file/RT3674FE/RT3674FE_DS-00.pdf#page=80).
That supports software volts, not identification of the controller or rail on
the ASUS board, a safe operating range, or an applied-voltage measurement.
The two 61h storage entries therefore still do not establish independently
addressed physical rails or a matching device getter.

## Executed evidence

[`firmware-granite-shared-request.py`](../analysis/tools/firmware-granite-shared-request.py)
checks the pinned image and every executed instruction byte. It includes:

- 24,576 handler-to-selection-to-code-to-channel cases: 1,024 capped cases,
  12,288 locked writers and 12,826 cases where request and command mirror differ.
- 160 disabled-path combinations and 6,144 adjacent binary32 boundary cases.
- Four samples carried through accumulation and export at indices 19/48.
  A deliberately invalid sentinel is retained by one locked fixture; that
  arithmetic result is not a plausible measured voltage.

The finite CEIL.S model uses upward rounding, consistent with
[QEMU's Xtensa instruction mapping](https://github.com/qemu/qemu/blob/v9.0.0/target/xtensa/translate.c).
NaN, overflow, floating exceptions, complete scheduling and device effects are
outside the replay. Register-window calls execute as explicit slice boundaries;
the packet helper runs separately with scripted readiness/completion bits.
This is not a complete firmware or peripheral emulator.

The eight **core 0/4/12 and CCD 0/1/2/6/8** units remain unresolved. This update
adds one common software-voltage field, not one of those eight rows. PM-to-
physical-CCD/OS-core binding and independent second-rail write/device readback
also remain unresolved. See [completed validation](amd-granite-validation.md).
