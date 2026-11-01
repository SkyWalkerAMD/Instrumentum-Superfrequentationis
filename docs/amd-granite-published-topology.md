# 9950X3D published CPUID and the recovered APIC consumer

The topology decoder previously rejected every Family 1Ah / Model 44h CPU
that exposed a type-3 level. This followed the reserved label in PPR 57896
Rev 3.00, but conflicts with a
[published 9950X3D capture](https://github.com/InstLatx64/InstLatx64/blob/895c35aba50508c64cd38d8892fe98b0954142d7/AuthenticAMD/AuthenticAMD0B40F40_K20_GraniteRidge_06_CPUID.txt).
That capture reports both Complex and CCD levels with identical boundaries.

The source is InstLatx64's capture contributed by westlake, committed on
2025-03-17. It identifies an ASUS ROG Strix X670E-E Gaming WiFi, BIOS 2904,
and signature `00B40F40`. It is **not** a capture from the user's X870E Creator.
Its SHA-256 is `3f4f76ee0440b141b1a8f180ccaa7b0e2662607df622320cfab0b00e468d3279`.

| Fn80000026 level | APIC shift | Logical processor count |
|---|---:|---:|
| Core (1) | 1 | 2 |
| Complex (2) | 4 | 16 |
| CCD (3) | 4 | 16 |
| Socket (4) | 5 | 32 |

All 32 captured CPUs report this shape and distinct APIC IDs 0–31. Fn8000001D
reports 96 MiB L3 for capture indices 0–15 and 32 MiB for indices 16–31.
The capture's Windows logical CPU indices are not assumed to be Linux CPU IDs.

## Shared GUI and CLI behavior

For this model, type 3 is accepted when its shift and logical count both
equal type 2. Both raw levels are retained. JSON reports `ccd_level_type: 3`
and `redundant_ccd_level: true`; the GUI indicates that the boundaries agree.
If type 3 is absent, the existing model-specific Complex-level fallback remains.
Missing Complex or conflicting boundaries/counts fail without returning an
identity map. Other processor models keep their existing rules.

Use `octool-cli amd-topology --all-cpus --l3`, or the AMD tuning page's
**Read all available CPU topology** action. CPUID topology remains separate from
firmware tuning targets and PM slots. No setting command is prepared or sent.

The shared test fixture preserves the captured signature, four topology levels
and cache words. Its backend identity scaffolding, OS CPU selection and final
zero-count subleaves are synthetic: the public dump omits terminating subleaves.
Tests deliberately use sparse, reversed OS CPU numbering to avoid relying on
the capture's numbering. Conflicting levels and failed reads discard results.

## Connection to REA evidence

[`firmware-granite-published-topology.py`](../analysis/tools/firmware-granite-published-topology.py)
checks the source hash and generated C++ fixture, then supplies the captured
Fn0000000B and four Fn80000026 replies to the
[recovered ASUS 2503 APIC consumer](amd-apob-apic-enumeration.md).
The consumer, sorter, provider getters and constructor execute original bytes
identified by REA. Across two providers, 32 captured inputs, four stored-ID
orders and two record encodings, **512 cases / 16,384 output records** produce
the expected APIC set without duplicates.

This joins a captured CPUID shape to a recovered firmware component under
**synthetic APOB and service inputs**. It does not verify the installed BIOS,
runtime provider, actual APOB contents, physical CCD labels or PM-to-OS binding.
The earlier missing-type-3 collision example remains valid for its synthetic
inputs; it is not a description of this published capture.

The eight pending core/CCD row units and independent second-rail write/device
readback remain unresolved. See the [validation report](amd-granite-validation.md).
