# Intel energy counters, HWP capabilities and history

GUI **Intel → Power / performance** and `octool-cli intel-read --cpu N` now show
energy counter values in joules, their unit and wrap range. These are read-only
counter snapshots, not instantaneous watts or lifetime energy totals. No
sampling, wrap accumulation, package aggregation or settings write is performed.

| Profile | Counters accessed |
| --- | --- |
| Intel 06_B7, bare metal with MSR capability | Package `611h`, package-wide CPU cores (PP0) `639h` |
| Intel 06_8F / 06_CF, same capability checks | Package `611h`, DRAM `619h` |

The energy exponent is `MSR 606h[12:8]`; counters use their low 32 bits.
The full raw MSR remains visible. Zero is a counter value, not proof that a
domain is absent. A failed read produces an error and unavailable value, not zero.
DRAM readings require firmware configuration of RAPL mode 0; this program does
not enable it or establish that the firmware's measurement is functioning.

The model and unit selection follows the pinned Linux v6.12
[RAPL perf driver](https://github.com/torvalds/linux/blob/v6.12/arch/x86/events/rapl.c)
and [powercap driver](https://github.com/torvalds/linux/blob/v6.12/drivers/powercap/intel_rapl_common.c).
SPR/EMR use the unit register for DRAM, unlike older server profiles with the
15.3 µJ override. [Intel SDM Volume 4](https://cdrdv2-public.intel.com/835765/335592-sdm-vol-4.pdf),
Table 2-52, describes the newer DRAM unit as approximately 61 µJ and requires BIOS
mode 0. Sources and reviewed PDF pages are [pinned here](validation/intel-telemetry-sources.json).

The existing HWP capability display now includes guaranteed and most efficient
performance levels, completing all four capability bytes in `IA32_HWP_CAPABILITIES`.
These are performance indices. They are neither clock ratios nor MHz, and are
shown only with the architectural HWP capability. Read failure suppresses all
four values. The byte definitions are in SDM Table 2-2, PDF page 63.

`IA32_HWP_STATUS` (`777h`) now provides two read-only history rows: guaranteed
performance changed (bit 0), and excursion to minimum (bit 2). These require
`CPUID.06H:EAX[7]`, including on Intel models outside the RAPL profile list;
notification bit 8 is not required. Reserved bits are retained in the raw value
without interpretation. GUI values are `Recorded` / `Not recorded`; CLI values
are 0 / 1 with `hwp_status_is_history: true`. Read failure produces unavailable
values. Reading never clears history, enables interrupts or changes HWP.
The fields are not current performance or a diagnosis of an active limit.
SDM Table 2-2, PDF page 65, defines their history semantics.

Original `Rd_HWP_STATUS_Tuner()` is preserved as a
[complete fixture](../analysis/fixtures/legacy-intel-hwp-status.json), with
[138 offline cases](../analysis/tools/legacy-intel-hwp-status.py). It returns the
low 32 bits, ignores wrapper errors and leaves the output stack slot
uninitialized on failed reads. The restored reader retains all raw bits and
reports failure explicitly.

Original `intel_get_energy_unit()` and `get_current_energy()` are preserved in a
[pinned fixture](../analysis/fixtures/legacy-intel-energy-counters.json) with
[complete instruction evidence](../analysis/legacy-intel-energy-counters-static.json).
The latter reads PP0, despite its generic name. The
[400-case replay](../analysis/tools/legacy-intel-energy-counters.py) verifies field
boundaries and ignored wrapper failures. Package/DRAM applicability and the
additional HWP display use the primary documentation above; no claim of a
complete original power-monitoring implementation is made.

Regression coverage includes all unit exponents, zero and maximal counters,
reserved high bits, model/domain separation, identity/capability failure,
independent counter faults, every HWP byte value and GUI/CLI unavailable states.
Actual power accuracy, counter continuity across resets and physical hardware
acceptance remain outside synthetic verification.
