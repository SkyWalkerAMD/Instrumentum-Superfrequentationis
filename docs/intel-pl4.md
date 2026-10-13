# Intel Raptor Lake PL4

GUI **Intel → Power / performance → Package PL4 (peak power)** and CLI
`intel-read` / `intel-set --field pl4` share the same implementation. The profile
is limited to Intel family 6 model B7, with MSR support, consistent CPU identity
and no hypervisor. PL4 uses MSR `601h`; it is a separate package power control
from the still-unresolved ICCmax mailbox rail settings.

```sh
octool-cli intel-read --cpu 0
# Example only; choose a limit appropriate to the target platform.
octool-cli intel-set --cpu 0 --field pl4 --value 100 --apply
```

The field is `601h[12:0]`. Its power unit is `2^(-606h[3:0]) W`, matching
Linux v6.12's [Raptor Lake profile](https://github.com/torvalds/linux/blob/v6.12/drivers/powercap/intel_rapl_msr.c)
and [PL4 mask/unit selection](https://github.com/torvalds/linux/blob/v6.12/drivers/powercap/intel_rapl_common.c).
The accepted encoded range is 1..8191; nonintegral values round down. With power
exponent 3 this is 0.125..1023.875 W. The GUI and CLI display an existing zero
value but do not offer a zero/disable operation or assign it a verified meaning.

Before writing, the reader obtains a fresh full register and unit snapshot and
compares them with the user's snapshot. Bit 31 is treated as a lock. A single
write replaces only bits 12:0, preserving bits 13..63. A fresh read verifies the
entire register, unit and identity; an unchanged setting does not produce a
write. Errors, cancellation, timeout and differing readback remain failures,
without automatic retry or rollback. Verified readback is not measured power
or physical stability acceptance.

There is a documented source inconsistency. [Intel SDM Volume 4](https://cdrdv2-public.intel.com/835765/335592-sdm-vol-4.pdf)
Table 2-45 describes PL4 as watts but its field text says 0.125 A. Table 2-46
includes B7 among the processors inheriting that register. This implementation
uses the model-specific Linux power interpretation and power-unit register,
not an amperes conversion. SDM's bit-31 lock is also honored even though Linux's
generic MSR PL4 path does not expose a lock primitive. The pin record is
[intel-telemetry-sources.json](validation/intel-telemetry-sources.json).

Four complete original entries and their actual bit-mask helper are in the
[fixture](../analysis/fixtures/legacy-intel-pl4.json) and
[instruction audit](../analysis/legacy-intel-pl4-static.json). The
[612-case offline replay](../analysis/tools/legacy-intel-pl4.py) establishes:

- Both getters mask 13 bits and **multiply by 8.0**. The original floating-point
  constant bytes are retained; that return value is not used as watts here.
- The no-argument setter clears 14 bits and inserts `1fff`, also clearing bit 13.
- The integer setter replaces 14 bits; negative/oversized values saturate to
  16383, allowing the reserved bit 13 to be set.
- Both setters ignore wrapper errors and locks and do not read back.
- A separate PCU/PCI getter exists; this implementation does not enable that
  transport or use its input as a validated CPU/package selector.

Core regressions cover every power exponent, preserved-bit and full-readback
mutations, locks, stale units, unsupported models, failed I/O, timeout and
cancellation. GUI tests cover confirmation, displayed/copyable values, no-op,
locked/failed/unsupported states and failed verification. CLI JSON checks use
the same core and distinguish unavailable, attempted, unchanged and verified
states. Physical effects remain untested.
