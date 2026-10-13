# Original Intel perfbias naming and target-mask findings

The original `is_intel_perfbias`, `en_intel_perfbias` and `dis_intel_perfbias`
entries access **MSR 48h bit 1**. That is STIBP, not the energy/performance bias
MSR `1B0h`. [Intel SDM Volume 4](https://cdrdv2-public.intel.com/835765/335592-sdm-vol-4.pdf),
Table 2-2, PDF page 16, identifies the register as `IA32_SPEC_CTRL` and its bit 1
as STIBP, enumerated by `CPUID.07H.0:EDX[27]`.

The [pinned complete functions](../analysis/fixtures/legacy-intel-perfbias.json),
[static instructions](../analysis/legacy-intel-perfbias-static.json) and
[668 offline replay cases](../analysis/tools/legacy-intel-perfbias.py) establish:

- The reader extracts bit 1. The setters clear/set that bit after one MSR read.
- Setters copy the sampled full register to every target. They do not read each
  target's current register before writing.
- The 64-bit result of `get_Processor_Mask` passes through `EAX` to `ECX`, losing
  bits 32..63 before the `WrmsrTx` boundary. Synthetic masks containing only a
  high bit become zero.
- Read/write return codes are ignored. If a failing reader leaves its outputs
  untouched, setters use zero-initialized data, potentially clearing unrelated
  settings in the requested writes. There is no post-write verification.

The emulation runs the three complete original bodies while replacing only
MSR wrappers, topology/mask methods and delay with synthetic boundaries. It does
not execute the original topology enumerator or establish actual CPU routing.
It is additional local research after production commit `39dc2e5`, and is not
part of that commit's cloud-test counts.

These names must not be used to introduce an energy/performance-bias control.
The restored GUI and CLI do not perform these broadcast writes. A future
explicit STIBP feature would require capability, target and kernel-ownership
handling under its real name; EPB would require its own architectural contract.
The existing raw-register command is not counted as restoration of either.

Source ELF SHA256:
`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`.
Fixture SHA256:
`8ac1cf69f6d2082f29cc0c07a544403c3c0b5ca1682007386bbb447b26155b35`.
The SDM PDF is pinned by [the telemetry source record](validation/intel-telemetry-sources.json).
