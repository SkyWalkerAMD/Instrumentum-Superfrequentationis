# Shimada signed power adjustment

The original ELF calls RSMU `A8h` from
`AMD_PM_LOG::get_percore_power_offset()` at file offset `239100h`, with zero
input. Its name does **not** establish a PM-buffer offset or a per-core
selector. REA study `power_adjustment` also rechecks the mailbox wrapper and
rails caller. The executable is never run.

The pinned ASUS 1317 SMU body, SHA-256
`a2f3724ccaef1d67db04afb553816f111dc86207480b28d7998cc15fa92ea7d7`, supplies
independent integer-instruction evidence:

| RSMU command | Handler | Observed operation |
| --- | --- | --- |
| `02h` | `23A0Ch` | Return literal `00732E00h` from `171D0h` |
| `A8h` | `2C2E0h` | Load byte `B395h`, sign-extend to 32 bits, reply 1 |
| `A7h` | `2C2A4h` | Read signed argument; require helper `321F8h(2)`; accept −10…5; store byte `B395h`; broadcast MP5 `99h` with the full signed word; reply 1 |

Eligibility rejection returns `FDh`; an out-of-range argument returns `FFh`.
Neither rejection stores the byte or calls the broadcast helper. The MP5
handler stores the argument at `13110h` and notifies its consumer. Physical
units and downstream power-control effects remain unverified. This is a
shared setting; neither command contains a CCD/core selector.

`analysis/tools/firmware-adjustment-replay.py` verifies the source hash and
each executed instruction's bytes before replaying against synthetic RAM.
It checks one version query, all 256 possible stored bytes, and 1,572 setter
cases spanning both eligibility outcomes, three previous bytes, every signed
8-bit value and malformed/noncanonical words. Accepted writes are read back
through the original getter instructions. Mailbox helpers, eligibility and
the broadcast recorder are fixtures; MP5 and physical hardware are not emulated.

## Shared GUI/CLI implementation

Both frontends use `readShimadaPowerAdjustment` and
`setShimadaPowerAdjustment`. Access requires family `1Ah`, model `08h`, PCI
`1022:153a`, domain/BDF `0000:00:00.0`, and RSMU version **115.46.0**. Other
versions stop after the version query without sending A7/A8.

Reads expose the raw word and signed counts. Only canonical sign-extended
8-bit words decode: `FFFFFFFFh` means −1; `000000FFh` is invalid. Counts are
not presented as watts, millivolts or table offsets.

Writes require explicit −10…5 counts, the previous raw word and the complete
capability word. The application additionally requires capability bit 2 and
manual OC disabled. It compares both values, rechecks capabilities and identity,
sends A7 once, then reads the version, capabilities and A8 again. A matching
stored value is verified; a mismatched value, changed identity/capability,
rejected reply, I/O failure or cancellation stays an error. An unchanged value
sends no setting. There is no retry, implicit mode change or rollback. A
matching A8 reply does not establish a physical effect.

```sh
octool-cli amd-power-adjustment-read --cpu 0
```

An explicit edit uses `state.raw` and `state.capabilities_raw` from that read:

```sh
octool-cli amd-power-adjustment-set --cpu 0 --counts -1 \
  --expect-raw 0x00000000 --expect-caps 0x00000004 --apply
```

The example's previous values must match the actual read. GUI controls are in
AMD Tuning under “Shared power adjustment”; read before each edit. Both
frontends distinguish acceptance, stored-value verification and unmeasured
physical effects.

## Remaining evidence gaps

This does not complete dual rails or PM field naming. The expanded SMU export
contains 1,385 discovered functions and MP5 637, with no selection truncation;
some individual functions still encounter unsupported Xtensa instructions.
Missing references therefore do not prove that a consumer does not exist.
RSMU `61h` remains closed at runtime. Queries `6Ch`, `6Eh` and `67h` remain
research candidates until their writers, units and scope are verified.
