# Shimada signed power adjustment

2026-10-26 续研：[Fmax 回读与解码器修正](amd-fmax-readback.md)已完成软件验证。新的指令分析追踪了 `35D28h` 的八槽计算、输出复制和 MMIO 消费路径；近似浮点语义、物理单位及核心映射尚未确认。

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
handler stores the argument at `13110h` and notifies its consumer. Caller
`35DC0h` passes base `12FACh+30h` to `35D28h`; its `+134h` signed load
reads that exact word, converts it to float and adds it to the `+4h` input.
The isolated Xtensa decoder extension now continues past the former stop at
`35D6Bh`; the downstream storage path is recorded below. Physical
units and downstream power-control effects remain unverified. This is a
shared setting; neither command contains a CCD/core selector.

`analysis/tools/firmware-adjustment-replay.py` verifies the source hash and
each executed instruction's bytes before replaying against synthetic RAM.
It checks one version query, all 256 possible stored bytes, and 1,572 setter
cases spanning both eligibility outcomes, three previous bytes, every signed
8-bit value and malformed/noncanonical words. Accepted writes are read back
through the original getter instructions. Mailbox helpers, eligibility and
the broadcast recorder are fixtures; MP5 and physical hardware are not emulated.

## MP5 consumer traced through the extended decoder

This trace uses the same vendor image, MP5 SHA-256
`b5e9c94ef3a27d55a95c1a0d703cc7041b734f9475d44b8eecd49a15794cdeaf`.
Instruction bytes and literal words were checked against the input body.
`G` below is the global base `12FACh`, loaded from literal `18960h`.

| Stage | Instruction evidence | Established data flow |
| --- | --- | --- |
| Mailbox storage | `36140h`: `s32i a10,a8,164h` | MP5 `99h` saves the signed word at `G+164h = 13110h` |
| Computation input | `35D33h`, `35D42h`, `35D48h` | `35D28h` receives `G+30h`, loads `+134h`, converts the signed word and adds it to the existing `+4h` float |
| Eight-slot calculation | `35D71h`–`35DB9h` | A hardware loop visits eight slots; inactive slots skip the store. Each active slot uses additional voltage/calibration inputs, a zero floor, approximate reciprocal and unsigned conversion, then stores to `G+44h+4*i` |
| Output copy | `35DFFh`–`35E09h` | A second eight-iteration hardware loop copies `G+44h+4*i` to `G+8h+4*i` |
| Register consumer | `359D5h`–`359DAh`, loop `359BDh`–`35A07h` | Selected slots write `G+8h+4*i` to the MMIO address `018FFE00h+314h+20000h*i`; the loop also saves a RAM copy |

These are slots in one MP5 context. Their correspondence to OS CPU IDs or CCD
IDs is not established. The two hardware loops must be read from instructions:
the decompiler output does not faithfully expand all iterations. `RECIP0.S`
and `UTRUNC.S` remain explicit opaque operations in Ghidra; this trace is not
a numerical emulation of the control algorithm. A conditional initializer at
`358BCh`/`358C2h` writes binary32 `20.0` to `G+34h`; that constant and the
voltage-dependent formula do not independently establish watts or another unit.

The [research record](validation/amd-fmax-readback-research.json) preserves the
instruction excerpts, source hashes and unresolved interpretation. The
[decoder changes](amd-fmax-readback.md#分析器修正) do not alter the firmware.

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

This does not complete dual rails or PM field naming. The extended SMU export
contains 1,385 discovered functions and MP5 634, with no selection truncation;
seven functions in each body still report bad-instruction truncation.
Missing references therefore do not prove that a consumer does not exist.
RSMU `61h` remains closed at runtime. Queries `6Ch` and `67h` remain research
candidates until their writers, units and scope are verified. Query `6Eh` is
now implemented as the version-gated [Fmax ceiling readback](amd-fmax-readback.md).
