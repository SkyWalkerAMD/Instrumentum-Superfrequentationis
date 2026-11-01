# Reference APOB to OS CPU comparison

GUI and CLI now share a read-only comparison between an imported APOB topology
and the CPUs available to the process. It joins **reference APIC candidates**
to CPUID APIC IDs, preserving OS CPU numbers. It does not establish that a PM
slot belongs to a physical CCD/core.

In the AMD panel, open a firmware topology file, optionally select L3 cache,
then choose **Compare file with OS CPUs**. It uses the bytes already loaded;
loading a different or invalid file clears the previous selection. CLI uses
the same bounded record/container format:

```sh
octool-cli amd-topology --all-cpus --apob topology.bin --l3
```

`amd-topology-decode --file topology.bin` remains offline. The comparison
reads CPUID only: no register requests, tuning arguments or firmware commands.

## Scope and results

The comparator accepts the published Model 44h, SMT2, single-socket topology:
types 1/2/3/4, shifts 1/4/4/5, logical capacities 2/16/16/32. **The actual
type-3 leaf must exist.** The ordinary topology reader's Complex-level
fallback is not used to invent a missing constructor input.

Records require instance 0, one or two groups, one subgroup per group, unique
stored IDs within each parent, and prefix thread flags. Unsupported group
IDs, duplicate identities, empty records and malformed containers are
rejected. A CPU set contains 1–32 distinct OS CPU IDs. These are reference
scope restrictions, not claims that other real configurations are invalid.

Fn80000001 EAX/EBX must meet the recovered provider-A entry predicate; Fn0B
subleaf 0 must match the SMT boundary. The comparator reads the CPU catalog
twice, brackets it with constructor/provider inputs, compares signature/APIC
across leaves, and rejects changes. GUI/CLI also compare the allowed CPU set
before/after. Failures discard all candidate/CPU match results.

Results include original positions and stored IDs, thread, reference APIC,
matching OS CPU or `null`, and a rechecked catalog with optional L3 geometry.
Missing APICs and OS CPUs with no reference candidate are reported separately.
Affinity restrictions can legitimately give partial results; no omitted CPU
is synthesized. `complete_reference_match` means only that these APIC sets
agree. Origin/HMAC, installed provider, physical targets and PM binding remain
explicitly unverified even when it is true.

## REA and original-byte oracle

Reference: ASUS ProArt X870E-CREATOR WIFI BIOS 2503. REA resolves consumer
`0x2d24`, sorter `0x54e0`, constructor `0x526c` and D20482F8 provider getters.
The constructor receives **enumeration positions**, after sorting by stored
group ID, not stored IDs as APIC fields. With the accepted shape the result
is `group_ordinal * 16 + core_ordinal * 2 + thread`.

`analysis/tools/firmware-granite-apob-comparison.py` executes both providers,
sorter, consumer and constructor in pinned Unicorn 2.1.4. CPUID comes from
the [pinned public capture](amd-granite-published-topology.md), which is from
**another board/BIOS**. APOB and firmware services remain synthetic. Four
group orders, three core-prefix lengths and two thread-prefix lengths produce
48 original-byte replays / 624 records. The providers agree on 24 cases / 312
rows, generated into a C++ oracle and checked against the shared comparator.
Cloud REA regenerates/checks that oracle independently of the product formula.

Tests also cover reverse/sparse OS numbering, 96/32 MiB L3 geometry, missing
APICs, unmatched CPUs, malformed records, unsupported/duplicate CPUID,
changes between reads, and every CPUID failure position. GUI tests verify
stale-result clearing and unchanged tuning arguments. CLI process tests parse
JSON and check syntax/file failures before backend creation.

Still incomplete: physical units for core rows 0/4/12 and CCD rows
0/1/2/6/8, runtime PM-slot to physical CCD/OS CPU binding, and independent
second-rail write plus device readback. This comparator is an intermediate
association, not completion of those items. Target hardware is unavailable
and its installed BIOS has not been confirmed.
