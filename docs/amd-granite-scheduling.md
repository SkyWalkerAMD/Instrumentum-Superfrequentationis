# Granite model scheduling and the missing time-unit anchor

Reference: ASUS ProArt X870E-CREATOR WIFI BIOS 2503, SMU/MP5 98.84.0,
identified by the [pinned inputs](validation/granite-creator-2503-input.json).
This follows the [producer/integrator study](amd-granite-transfer-paths.md).
It does **not** establish watts, joules or an absolute model-update period.

The model integrator's factor near 65.536 is insufficient to assign an energy
unit: its destination's MSR alias and update interval remain unverified.
REA/Ghidra instruction discovery now exposes another relevant scheduling
boundary. `firmware-granite-scheduling.py` checks the original instruction bytes
against synthetic RAM, queue returns, completion ports and lock services.

## Enqueue can be skipped

SMU `0x11e8` loads inhibit byte `0x82f9` and pending byte `0x82f8`. Only when
inhibit is zero and pending is below two does it call `0x2810` with dispatcher
`0x1dcf4`, priority 4 and the remaining four arguments zero. A zero queue return
increments pending; other returns do not. With inhibit zero and pending at
least two, it skips the queue call and increments `0x81cc` with 32-bit wrap.
The tail service `0x25f4` is called in all of these paths; that service itself
is scripted, not executed.

The 8,192 cases cover all 256 pending byte values, four inhibit bytes, four
queue returns and two skipped-counter values. They include 16 queue attempts,
four accepted cases, 2,032 pending-gate skips and 6,144 inhibited cases. These
are fixture counts, not measurements of how frequently the gates occur.

## Acknowledgement precedes model queuing

Static registration associates SMU callback slot 1 with `0x1ddd0`, which calls
`0x1d62c` with MP5 command `0x0d`. The checked broadcaster visits each set bit
in the two-target presence mask, clears its completion word, writes the command
and then waits for nonzero completion. It does not compare with success code 1.
Forty-eight cases include 27 with a non-success nonzero response; active and
presence masks deliberately differ. The decompiler omits the loop in its C
view, so these conclusions follow the instruction replay.

MP5 table entry `0x13300` points to `0x2d7b8`. That handler first executes the
completion-1 store helper `0x2e834`, then calls queue helper `0x1e6c` with
dispatcher `0x2d78c`, priority 4 and arguments `[0,0,1,0]`. It returns without
checking the queue result. Six scripted return values, including nonzero ones,
preserve the already-written completion value 1. The scheduler and model
callback do not execute in this experiment.

The [earlier callback study](amd-granite-transfer-paths.md) places model callback
`0x2c798` at MP5 slot 6. This connects the request to its intended queue, not
to a completed update. The per-callback timer differences in both dispatchers
measure execution duration; they do not independently establish the interval
between model invocations.

## Limits

The new 8,246 cases prove selected enqueue, broadcast and acknowledgement
behavior. They do not emulate interrupts, concurrency, scheduler deadlines or
peripheral clocks. No fixed one-millisecond invocation guarantee follows from
these slices, and no counter-event or energy-MSR identity has been established.
All eight unresolved PM rows retain their existing units and semantics in the
shared GUI/CLI decoder. No setting write or new physical map is enabled.
