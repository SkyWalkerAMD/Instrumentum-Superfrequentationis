# Intel performance limit reasons

The **Intel → Limit reasons** tab and the following CLI commands share a core:

```sh
octool-cli intel-limits-read --cpu 0
octool-cli intel-limits-clear --cpu 0 --reason thermal --apply
octool-cli intel-limits-clear --cpu 0 --reason all --apply
```

This profile covers **Intel 06_B7**, bare metal with MSR support. It reads the
package `MSR_CORE_PERF_LIMIT_REASONS` register 64F through the selected logical
CPU. Current status is a momentary observation; history reports events since
the corresponding log was last cleared. Neither is a continuous measurement of
performance, power or frequency. An inactive status does not prove the system
was unconstrained between reads.

The model list and bit definitions are from Intel's
[SDM Volume 4, 335592-085US, October 2024](https://cdrdv2-public.intel.com/835765/335592-sdm-vol-4.pdf),
Table 2-39, printed pages 2-354 and 2-362–2-364 (PDF pages 364 and 372–374).
The downloaded reference SHA-256 is
`de47347b8d3827abf4ac1e8c9e72379e6c74f47403ad8976cdb27f38d2e172b9`.
These pages were rendered and the table columns inspected. Table 2-39 supersedes
older generation tables where the same address occurs; descriptions from a
different processor's register 64F or 690 must not be substituted.

| CLI reason | Status bit | History bit | Meaning |
| --- | --- | --- | --- |
| `prochot` | 0 | 16 | External PROCHOT |
| `thermal` | 1 | 17 | Thermal event |
| `residency` | 4 | 20 | Residency state regulation |
| `ratl` | 5 | 21 | Running average thermal limit |
| `vr-thermal` | 6 | 22 | Voltage regulator temperature |
| `vr-current` | 7 | 23 | Voltage regulator thermal design current |
| `other` | 8 | 24 | Electrical or other constraint |
| `pl1` | 10 | 26 | Package/platform long-duration power limit |
| `pl2` | 11 | 27 | Package/platform short-duration power limit |
| `turbo` | 12 | 28 | Maximum turbo ratio |
| `transition` | 13 | 29 | Turbo transition attenuation |

History flags are **write-zero-to-clear** (W0C). Clearing a log does not remove
the active constraint or modify protection settings. The GUI requires an
explicit confirmation; the CLI requires `--apply` and one reason or `all`.
Before the single write, identity, history and reserved bits must still match
the snapshot. Known live status bits may change. Selected log bits are written
as zero; **all unselected known log bits are written as one**, including those
that were zero in the snapshot. This preserves a newly arrived unselected event
between read and write. Reserved bits retain their read values. If the selected
history is already clear, no write is made.

Readback checks identity, reserved bits, preservation of unselected recorded
events, and selected history. Selected flags still set return `EAGAIN`, with
`still_set_mask` and the readback available; an ongoing event may reassert
immediately. This is not reported as success and does not cause a retry.
Snapshots and hardware events are not atomic. Other tools may also clear logs;
the in-process transaction lock cannot coordinate with them.

The [pinned original functions](../analysis/fixtures/legacy-intel-limits.json),
[instruction audit](../analysis/legacy-intel-limits-static.json) and
[offline emulator](../analysis/tools/legacy-intel-limits.py) cover 274 scenarios:
`get_clipping_log()` returns 31 raw booleans from 64F, including reserved bits;
`clear_clipping_log()` writes an all-zero word without checks or readback.
The separate `get_hedt_clipping_log()` reads 14 bits from **1B1**, not 64F.
Both original readers suppress wrapper failures. The reconstructed profile
exposes only documented B7 meanings and propagates errors. The separate [package-event reader](intel-package-events.md) restores six documented
status/history pairs from the server helper's 1B1 register on B7/8F/CF. It does
not turn this 64F history-clear profile into a server control.

Core tests cover all documented bit positions, each stale or corrupted bit,
W0C semantics, new events, immediate reassertion, faults, cancellation and
deadlines. GUI tests also cover confirmation, selection changes and asynchronous
lifetime; CLI tests independently decode each JSON bit. These synthetic tests
do not constitute acceptance of real hardware behavior or Secure Boot.
