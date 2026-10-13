# Intel thermal controls

The **Intel → Thermal limit** tab and `intel-thermal-read` / `intel-thermal-set`
commands use the same core. The profile covers Intel family 6 models **B7, 8F,
CF**, with MSR and DTS capability, on bare metal. Platform identity is checked
before register access. In particular, this profile does not infer Granite
Rapids support from a motherboard name.

```sh
octool-cli intel-thermal-read --cpu 0
octool-cli intel-thermal-set --cpu 0 --offset-degrees 10 --apply
```

The panel displays default TCC activation temperature (TjMax), the configured
offset, and `TjMax - offset` in degrees Celsius. The permitted input range is
0..63 C with a positive resulting threshold. Zero requests the default
activation threshold. An MSR readback verifies configuration; it does not measure
the onset of throttling under load.

The implementation follows the model-specific masks in
[Linux v6.12 intel_tcc.c](https://github.com/torvalds/linux/blob/v6.12/drivers/thermal/intel/intel_tcc.c)
and the capability check in
[processor_thermal_device.c](https://github.com/torvalds/linux/blob/v6.12/drivers/thermal/intel/int340x_thermal/processor_thermal_device.c):

| Register | Field | Meaning |
| --- | --- | --- |
| `PLATFORM_INFO` CE | bit 30 | TCC offset is programmable |
| `TEMPERATURE_TARGET` 1A2 | bits 23:16 | Default TCC activation temperature |
| `TEMPERATURE_TARGET` 1A2 | bits 29:24 | Offset below the default temperature |
| `TEMPERATURE_TARGET` 1A2 | bit 31 | Offset lock |

A zero TjMax is unavailable data. Locked or non-programmable configurations
remain readable. Set operations compare full old target and platform words,
check identity and capabilities again, perform at most one masked write and
verify full readback. The other 58 target bits, including clamp, time window and
lock, are preserved. Errors do not trigger retries or automatic rollback.

The original `Get_TCC_Activation_Temperature()` reads bits 23:16 of 1A2 and
ignores its MSR wrapper result. Despite its name, it returns the **default**
temperature. Its [complete pinned function](../analysis/fixtures/legacy-intel-thermal.json),
[instruction audit](../analysis/legacy-intel-thermal-static.json), and
[offline emulator](../analysis/tools/legacy-intel-thermal.py) cover 644 cases
including every possible target byte and wrapper failures. The distinct original `Get_TJMAX_OFFSET()` uses the server BIOS
mailbox 607/608 or a GNR PCIe path; this reconstruction does not claim to restore
that separate protocol by substituting the client MSR operation.

The existing Intel Controls view also exposes default TjMax and the package
digital sensor when CPUID reports it. Core digital readout uses eight bits on
the documented Tiger/Alder/Raptor Lake profiles; SPR/EMR uses seven core bits
and eight package bits. A zero TjMax, invalid sensor-valid bit, or access error
is unavailable data rather than a temperature. Temperature conversion subtracts
the sensor delta from TjMax, not from the configurable TCC threshold.

Tests cover the complete offset range on all three profiles, every bit of both
old and returned register words, context and lock changes, all transport fault
positions, deadlines, cancellation, GUI lifetime and structured CLI failures.
Hardware behavior and Secure Boot remain separate acceptance work.
