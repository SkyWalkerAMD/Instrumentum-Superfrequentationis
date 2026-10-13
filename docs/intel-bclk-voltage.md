# BCLK-aware adaptive voltage

GUI **Intel → BCLK voltage** and CLI share this read/change implementation:

```sh
octool-cli intel-bclk-voltage-read --cpu 0
octool-cli intel-bclk-voltage-set --cpu 0 --enabled 1 --apply
```

The supported profile is bare-metal Intel family 6, model B7 (Raptor Lake-S),
with MSR capability. Enable makes the CPU voltage/frequency curve account for
BCLK. Disable clears that setting. This controls shared processor configuration
through the selected logical CPU; it neither changes BCLK itself nor measures
the resulting voltage. Other models fail before register access.

Intel's pinned [Raptor Lake-S FSP policy](https://github.com/intel/FSP/blob/d901c9458288e1a0eb0ec9901efcd2ddfaa01cb8/RaptorLakeFspBinPkg/Client/RaptorLakeS/Include/FspmUpd.h)
defines `BclkAdaptiveVoltage` and its positive enable polarity. The policy
structure offset `0x03C5` is not a runtime register address. Runtime protocol
comes from the two complete original functions in the
[pinned fixture](../analysis/fixtures/legacy-intel-bclk-voltage.json) and
[instruction audit](../analysis/legacy-intel-bclk-voltage-static.json):

| Original function | Address / size | Observed protocol |
| --- | --- | --- |
| `bclk_aware()` | `0x373C80` / 150 bytes | MSR `150`, query `80000014`, data bit 2 |
| `wr_bclk_aware(bool)` | `0x373B90` / 230 bytes | Query `14`, replace bit 2, submit `80000015`, poll completion |

The original ELF SHA256 is
`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`.
[926 offline cases](../analysis/tools/legacy-intel-bclk-voltage.py) execute its
allowlisted instructions with synthetic MSR boundaries. Both functions ignore
wrapper errors and firmware status, can poll without a bound and perform an
extra read after query completion. The setter does not independently query and
compare the result. An ABI-invalid bool can alter neighboring bits.

The reconstruction accepts exactly 0 or 1, preserves all other 31 bits, checks
the OC lock, and compares CPU identity, the full FLEX_RATIO value and the full
old control word before one settings write. It verifies the full resulting
word and context afterwards. Query/set errors, stale state, cancellation,
deadlines and nonzero firmware statuses fail explicitly. An unchanged request
does not issue a settings command. Uncertain writes are not retried or rolled
back. Query commands still require a mailbox write, even for a read operation.

Core tests cover both states, every neighboring-bit mismatch, firmware statuses,
all transport fault positions, locked/unsupported systems and interruption.
GUI tests cover confirmation, target changes, stale results and closing the
panel during I/O. CLI tests check invalid arguments before backend creation and
JSON nulls for failed snapshots. Cloud build evidence and physical hardware
acceptance are distinct: synthetic verification does not establish firmware
support or voltage behavior on a physical machine.
