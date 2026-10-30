# Granite Ridge SVI telemetry and PM units

This study uses the hash-pinned ASUS ProArt X870E-CREATOR WIFI 2503 SMU 98.84.0
body from [the input study](amd-granite-pm.md). It does not identify the installed
BIOS or the voltage controller on the user's unavailable 9950X3D system.

The shared GUI/CLI decoder now exposes 12 additional common fields for exact
table versions 0x620105 and 0x620205. Indices and firmware channels are zero based.

| Firmware channel | Voltage (V) | Current (A) | Power (W) | Temperature (deg C) |
| --- | --- | --- | --- | --- |
| 0 | word 49 | word 50 | word 51 | word 52 |
| 1 | word 54 | word 55 | word 56 | word 57 |
| 2 | word 59 | word 60 | word 61 | word 62 |

Each value is already averaged by firmware. No application scaling, rounding
to an integer, BCLK correction or clamping is applied. These are channel
telemetry values, not per-CCD power, physical rail names or setting readback.
The existing candidate at word 58 comes from a different software source
(0x8ca4); it must not be confused with measured voltage at word 59 (0xa654).

Use the existing native capture or offline decode commands:

```sh
octool-cli amd-pm-native-read --cpu 0 --platform granite-ridge
octool-cli amd-pm-decode --file granite-pm.json
```

Choose an allowed CPU from `diagnose`; native capture retains its firmware and
platform guards. The GUI common-fields view shows the same values and units.
JSON records `unit_evidence: granite-smu-98.84.0-static`, the firmware channel,
and false flags for capture authentication, physical rail identity, freshness
and setting readback. Import recomputes this metadata from raw bytes, including
when a JSON file forges channel names, units or derived values.

## Byte-checked data path

[`firmware-granite-telemetry.py`](../analysis/tools/firmware-granite-telemetry.py)
checks instruction bytes against the pinned body and replays bounded slices:

1. SMU 0x32ee4 reads SVI register 09h through 0x32bf8. Bits 5:3 select the
   current coefficient: mode 0 returns zero; other modes return
   `2**(mode-1) / 32`. The initializer at 0x28d51 stores three coefficients at
   0xa620 + 24 * channel. All 256 byte replies and 768 stores are exercised.
2. The readout loop at 0x32e88 issues three request kinds per channel. A reply
   with bit 26 set supplies bits 25:10 to the raw cache at 0xa668 + 16 * channel.
   Otherwise it retains that field and increments the channel error counter.
   The replay covers all 512 validity masks twice, 9,216 payload checks,
   4,608 retained values and counter wrapping. Polling is outside this slice;
   the MMIO replies are explicit fixtures.
3. Converter 0x27e41..0x27ed0 writes four floats at 0xa624 + 24 * channel:
   voltage code times 0.005, current code times its coefficient clamped at zero,
   their product, and the low temperature byte minus 40. Channel 2 uses 0.01
   instead of 0.005 only when byte[0xc598+0x36] equals 1. A separate status byte
   tests temperature payload bit 9. The 49,152 channel cases cover all low-byte
   temperatures, eight scale modes, four third-channel mode bytes and both
   status states; 256 evenly spaced uint16 inputs are a synthetic grid, not a
   claim about valid electrical ranges. Three negative-coefficient fixtures
   additionally verify the consumer's zero clamp.
4. The 140-entry pointer table at 0xb100 feeds accumulator 0x15320 through
   0x2cc6e..0x2cc91. The common output table at 0xb340 has 301 entries and routes
   those accumulators to the PM indices above through 0x2d13a..0x2d14e. Four
   synthetic samples and an exact reciprocal fixture of 1/4 verify all 12
   values reach the output without another scale conversion. Approximate
   reciprocal refinement is not emulated.

The manufacturer's [SVI3 controller reference](https://www.richtek.com/assets/product_file/RT3674FE/RT3674FE_DS-00.pdf)
identifies register 09h bits 5:3 as output-current scale (page 36), volts as
5 mV per reporting code and temperature as the byte minus 40 (page 84).
This corroborates the units and register purpose, not the ASUS controller model.
The channel-2 alternative voltage scale is established by firmware, not inferred
from that controller's default scale.

## Validity and remaining work

A failed read may leave a mix of older and newer raw fields. The converter and
averaging loop still consume those cached values; a finite number does not prove
freshness. Mode 0 also makes current and power zero without proving zero load.
The twelve common PM fields carry neither the reply-valid bits nor the channel
error counters. Both frontends therefore report freshness as unverified.

Channel addresses initially become 1, 2 and 3 in 0x32ae8; their physical rail
identity and runtime board overrides remain unresolved. Register commands 90h
and 91h are still research-only: byte marshaling and telemetry units do not
establish an independent second-rail setter/readback pair, transaction timing or
the board's register contract. No new tuning write is enabled. PM-to-CPUID/OS
core association is also still separate from these channel measurements.

The follow-up [register-helper replay](../analysis/tools/firmware-granite-registers.py)
executes 0x32bf8 and 0x32cbc with scripted MMIO responses: 6,912 read and 6,912
write cases cover all register bytes, channels 0/1/2, three reply delays, varied
returned bytes and three written byte values. It checks the address mask,
complete write sequence, read payload extraction and packet formats
`address | register << 6 | 0x02000000` and
`address | register << 6 | data << 14 | 0x01000000`.
Eleven stalled readiness gates exhaust the bounded research instruction budget;
they do not produce a firmware timeout. Device side effects and readiness/error
bit semantics beyond the observed branches remain unverified. A completion bit
does not establish that a voltage changed. No real register command is issued.
