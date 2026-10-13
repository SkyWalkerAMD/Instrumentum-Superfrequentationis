# Intel package thermal events

Intel Controls and `octool-cli intel-read --cpu N` now expose six package event
pairs on family 6 models B7, 8F and CF when CPUID.06H:EAX[6] reports package
thermal management. Each pair has a current status and a history bit:

| Event | Current bit | History bit |
| --- | --- | --- |
| Thermal event | 0 | 1 |
| External PROCHOT | 2 | 3 |
| Critical temperature | 4 | 5 |
| Thermal threshold 1 | 6 | 7 |
| Thermal threshold 2 | 8 | 9 |
| Package power limit | 10 | 11 |

All twelve fields come from one read of `IA32_PACKAGE_THERM_STATUS` (1B1).
The GUI displays Active/Inactive and Recorded/Not recorded; JSON preserves
numeric 0/1, the complete raw word and per-field errors. These fields are
read-only. History indicates an event since reset or its last clear, not its
duration or timestamp. The implementation does not clear package history or
change interrupt thresholds.

Failed register reads produce unavailable fields. Event flags do not depend on
successful temperature conversion: missing TjMax or an invalid digital sensor
does not turn a successfully read event into zero. Reserved bits 12..15 are
not decoded as current or cross-domain limits.

The original `get_hedt_clipping_log()` reads 1B1 and returns fourteen raw flags,
including reserved bits 12/13, while ignoring read errors. Its full instructions
are already pinned in the [limit helper fixture](../analysis/fixtures/legacy-intel-limits.json)
and [274-case emulator](../analysis/tools/legacy-intel-limits.py). This restores
the supported meaning of that reader, independently of the B7-only 64F limit
history clear operation.

Definitions were checked against [Intel SDM Volume 4 revision 085, table 2-2,
printed page 2-23](https://cdrdv2-public.intel.com/835765/335592-sdm-vol-4.pdf)
and [Volume 3B revision 090, section 17.9, printed pages 17-45/46](https://cdrdv2-public.intel.com/874250/253669-090-sdm-vol-3b.pdf).
Volume 3B explicitly identifies bit 9 as threshold **2** history; this resolves
the repeated threshold-1 label in that Volume 4 table. The source PDF SHA256s
are `de47347b8d3827abf4ac1e8c9e72379e6c74f47403ad8976cdb27f38d2e172b9`
and `66379da848a8ef5c2336679f43fa62ea931de0573a453cbcdedecd630ab877e2`.

Tests cover all 64 input bits on all three profiles, unsupported capability,
independent sensor failures, access errors, read-only GUI state, clipboard
output and structured CLI results. No test claims real hardware acceptance.
