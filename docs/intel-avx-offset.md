# Intel AVX2 ratio offset recovery

The GUI **Intel → AVX2 offset** tab and CLI share the same core implementation.
The implemented platform profile is bare-metal Intel family 6, model B7, with
MSR, AVX and AVX2 capability. Other models fail before any MSR request. This is
a shared processor configuration accessed through the explicitly selected
logical CPU; it is not a physical-core selector.

```sh
octool-cli intel-avx-read --cpu 0
octool-cli intel-avx-set --cpu 0 --offset 7 --apply
```

Offsets are integer negative ratio offsets from 0 through 31 bins. Zero requests
no offset. The Intel Raptor Lake-S FSP describes this field and command 1B in
[FspmUpd.h](https://github.com/intel/FSP/blob/d901c9458288e1a0eb0ec9901efcd2ddfaa01cb8/RaptorLakeFspBinPkg/Client/RaptorLakeS/Include/FspmUpd.h).
The [ASUS Z790 BIOS manual](https://dlcdnets.asus.com/pub/ASUS/mb/LGA1700/ROG_MAXIMUS_Z790_DARK_HERO/E22758_ROG_Z790_Series_BIOS_Manual_Intel_14th_EM_WEB.pdf)
also describes this control. The deprecated AVX3 field is not exposed by this
profile. A configured offset does not establish a measured AVX2 frequency or
workload stability.

The original ELF has SHA256
`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`.
Three complete function bodies are preserved in the
[pinned fixture](../analysis/fixtures/legacy-intel-avx.json) and
[instruction audit](../analysis/legacy-intel-avx-static.json):

| Function | Observed behavior |
| --- | --- |
| `rd_avxoffset()` | Submit MSR 150 query 1A, poll busy, read bits 9:5 and 14:10 |
| `set_avxoffset(unsigned)` | Submit command 1B with bits 9:5; truncate to five bits and clear everything else |
| `set_avxoffset_390(unsigned)` | Copy the same truncated offset into both fields; clear everything else |

The [offline emulator](../analysis/tools/legacy-intel-avx.py) verifies 720
cases at synthetic MSR and vector boundaries. It demonstrates ignored wrapper
errors, ignored firmware status, and unbounded original polling. It does not
execute the original process or access hardware.

The reconstruction only modifies bits 9:5, preserving the other 27 bits. It
checks FLEX_RATIO's OC lock, rereads CPU identity, capabilities, lock and the
entire old configuration before one settings command, then verifies the full
new configuration and context. Stale snapshots, all nonzero firmware status
codes, access failures, timeouts and cancellation are failures. An unchanged
request performs no settings write. An uncertain write is not retried or rolled
back automatically.

Core tests cover all 32 values, every neighboring-bit mismatch, transport fault
positions, firmware errors, lock/context changes and interruption. GUI tests
cover stale results, confirmation, input validation and panel lifetime; CLI tests
check parsing before backend construction and machine-readable error evidence.
Cloud validation of this revision and physical-machine acceptance are recorded
separately from these synthetic guarantees.
