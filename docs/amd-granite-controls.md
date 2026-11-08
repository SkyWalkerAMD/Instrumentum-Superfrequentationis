# Granite Ridge controls and offline mapping

The GUI and CLI share the same Granite Ridge control implementation. It accepts
AMD family 1Ah / model 44h, PCI 1022:14d8 at domain 0 / BDF 0, no hypervisor,
and **exact SMU 98.84.0**. The reference is ASUS ProArt X870E-CREATOR WIFI BIOS
2503. The user's 9950X3D is unavailable and its installed BIOS is unknown;
synthetic tests and cross-platform builds do not establish machine acceptance.

| Feature | Interface | What a successful result establishes |
| --- | --- | --- |
| Capability flags | `amd-pbo-capabilities` | Version-checked firmware flags; full word retained |
| PBO Scalar | `amd-pbo-scalar-read`, `amd-pbo-scalar-set` | Exact stored binary32 policy readback; clamping is a verification failure |
| PPT / TDC / EDC / temperature limit | `amd-pbo-limit-set` | Acknowledgement only; effective limit remains unknown |
| Manual OC mode | `amd-oc-mode-set` | Requested mode flag readback; no automatic frequency, voltage or Scalar setting |
| All-core Curve Optimizer | `amd-curve-all-set` | Acknowledgement only; per-target application and current margins remain unknown |
| Fmax ceiling | `amd-pbo-fmax-read`, `amd-pbo-fmax-set` | Stored ceiling readback, as in the preceding release |
| PM / command index diagnostic | `amd-granite-map` | Reference mapping from explicit caller inputs; no live capture or OS binding |

Select **Granite Ridge** in the GUI's AMD SMU panel. Capability reads populate
the supported controls. Scalar has a separate read step. Changes to the CPU,
platform or requested values invalidate a pending confirmation. Each mutator
rechecks identity, version and the complete expected capability word before
submitting one setting. Scalar and mode then read back their respective stored
policy or flag. Acknowledged limits and all-core CO are never displayed as
verified measurements. Failed operations do not retry or roll back silently.

In the CLI, specify `--platform granite-ridge` explicitly. Omitting this option
preserves the existing Shimada behavior; the program never probes another
mailbox by trying writes. Start with reads:

```sh
octool-cli amd-pbo-capabilities --cpu 0 --platform granite-ridge
octool-cli amd-pbo-scalar-read --cpu 0 --platform granite-ridge
octool-cli amd-pbo-fmax-read --cpu 0 --platform granite-ridge
```

Use an allowed CPU from `octool-cli diagnose`. The write interfaces are:

```text
amd-pbo-scalar-set --cpu N --platform granite-ridge --scalar N --expect-raw RAW --expect-caps RAW --apply
amd-pbo-limit-set --cpu N --platform granite-ridge --limit ppt|tdc|edc|temperature --value N --expect-caps RAW --apply
amd-oc-mode-set --cpu N --platform granite-ridge --enabled 0|1 --expect-caps RAW --apply
amd-curve-all-set --cpu N --platform granite-ridge --margin N --expect-caps RAW --apply
```

Expected words must come from the current read, not from this document. Scalar
accepts integer multipliers 1–10. Limit input bounds are 1–2000 W/A or 1–100 C;
these are application bounds, not fuse limits or electrical recommendations.
Granite all-core CO accepts **−50 to +30 counts**, whereas Shimada retains its
existing −50 to +50 interface. Counts are not millivolts. PBO/CO require bit 2
and manual OC off; limits use bit 1 and manual OC off. Enabling manual OC needs
bit 0. Disabling or an unchanged mode request does not require that enable bit.

Scalar `6Dh` returns zero while manual OC is enabled; this does not mean the
stored Scalar was erased. Setter `5Bh` can acknowledge a clamped value. CO's
internal broadcast accepts any nonzero target completion, so its host ACK
cannot establish that every target applied the margin. Setting a limit does
not establish its effective value. There is no automatic mode change.

## Mapping diagnostic

The PM panel's Granite mapping dialog and the CLI both require an explicit
table version, presence mask, two core masks and SMT selection. For example,
this is a **synthetic** sparse-input diagnostic, with no device access:

```sh
octool-cli amd-granite-map --version 0x620105 --presence-mask 2 --core-mask0 0xffffffff --core-mask1 0x81 --smt on
```

The result keeps eight PM core slots for the one present firmware target.
Only two cores are enabled. Their command indexes are 0/1 and 2/3, encoding
32/33 and 46/47. The absent target's mask is retained as unconsumed input.
Neither sequence is an OS CPU ID. The original-byte oracle checks 18 cases,
including sparse masks, high mask bits, absent targets and SMT off.

The diagnostic rejects a table-width/presence conflict. It does not obtain
these masks from PM floats or silently infer them from CPUID. It is independent
of the existing Shimada `--map-ccd` path. Physical CCD identity, V-Cache binding,
APIC/OS association and live runtime provider selection remain unresolved.

## Evidence and remaining work

- [Scalar](amd-granite-pbo-scalar.md), [four limits](amd-granite-pbo-limits.md),
  [mode](amd-granite-oc-mode.md), [all-core CO](amd-granite-curve-all.md).
- [Mapping inputs and diagnostics](amd-granite-mapping-diagnostic.md).
- [Reference UEFI acquisition](amd-granite-map-acquisition.md) traces the
  presence query through a selected SMN service. Provider selection and equality
  between its fuse byte and the SMU initializer's core mask are still unverified.
- [Host mailbox routing](amd-granite-host-routing.md): MP1 command 70 and
  RSMU command 70 are different table entries; a neighboring table is not an
  extension of RSMU's valid range.
- [Local configuration C](amd-granite-local-config.md): a 32-bit step target
  can produce a software mirror that differs from the submitted low byte.
  This metadata is shared by GUI, CLI and capture re-decoding; it is not device
  readback and the 73h hardware writer remains closed.

Eight core/CCD row units, physical PM-to-OS mapping, independent second-rail
write/readback and additional Granite tuning commands remain unverified.
Manual OC frequency and arbitrary Granite SMU writes remain unavailable.
Build, package, screenshot and replay results are recorded separately in the
[current validation report](amd-granite-validation.md).
