# Intel ICCmax / VR topology recovery evidence

This is protocol research, **not an enabled GUI/CLI current-limit control**.
The [research record](validation/intel-iccmax-research.json) separates local
instruction replay from the cloud-tested production build `bc30c4f`.

Nine complete original functions are pinned in the
[ICCmax fixture](../analysis/fixtures/legacy-intel-iccmax.json) and
[VR topology fixture](../analysis/fixtures/legacy-intel-vr-topology.json), with
[ICCmax instruction audit](../analysis/legacy-intel-iccmax-static.json) and
[topology instruction audit](../analysis/legacy-intel-vr-topology-static.json).
All were extracted from original ELF SHA256
`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`.

| Original path | Established behavior | Remaining interpretation |
| --- | --- | --- |
| `rd_iccmax()` / `is_iccmax_dis()` | Query 16 through MSR 150; return low 10 bits / bit 31 | CPU/firmware applicability and unit |
| `iccmax()` | Query 16, submit 17 with data OR `800003ff`, then write MSR 601 = `1fff`; no verification | Safe replacement must not copy the extra write blindly |
| Indexed `set_iccmax()` | Topology-selected command address, low 11-bit replacement, preserving other data bits | Exact rail mapping and firmware contract |
| `unlimit_iccmax()` / `maxout_iccmax()` | Indexed bit-31 change / first-three-entry sweep, ignoring topology flags | Rail validity, supported models and lock behavior |
| `get_vr_iccmax_unlimited()` | Three raw 11-bit values, appending three bit-31 flags to the caller's vector | Unit and model binding; still returns payload on busy/error |
| `get_vr_topology()` | Query 04; extracts addresses 11:8, 16:13, 28:25 and flags 12, 17, 29 | Flag meaning/polarity and named supply domains |

The indexed helper's command word is `(address & 31) << 8 | command`; its
static instructions do not set the busy/submit bit used by the scalar helper.
It cannot be treated as interchangeable with the scalar protocol. A ten-bit
reader also cannot justify a generic eleven-bit decoder.

The [local emulator](../analysis/tools/legacy-intel-iccmax.py) executed 2,863
cases across all eight ICCmax/topology functions, including real nested calls
to the original topology decoder. Cases cover bit patterns, integer boundaries,
negative/out-of-range domain indexes, delayed completion, wrapper/firmware
errors, permanently busy responses and vector allocation/bit-word boundaries.
It confirms ignored errors, indefinite polling in topology/setters and the
extra 601 write. The indexed reader polls at most 201 additional times after
the first read, but still returns data when busy remains set. For permanently
busy synthetic replies, read counts are 203/202/202 across its three queries;
the first query has an extra final read. Completion polling never compares
the written value with a fresh command-16 readback.

Only allowlisted original instructions and synthetic MSR/container boundaries
run; the original process and hardware are never started. The ninth pinned
function is the separate maximum non-turbo ratio getter, already covered by
[644 replay cases](../analysis/tools/legacy-intel-platform.py) in the tested
production snapshot. None of this determines physical current units or which
named power rail belongs to each address.

Replay after installing the pinned emulation requirements:

```sh
PYTHONPATH=build/reference-tools python3 analysis/tools/legacy-intel-iccmax.py --output build/iccmax-emulation.json
```

Completion requires model-specific protocol and rail evidence before adding a
bounded shared core, explicit units/ranges, GUI/CLI controls, lock checks and
verified readback. The existing raw-register tool is not counted as restoration
of these named controls.
