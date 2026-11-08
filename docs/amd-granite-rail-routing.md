# Granite SVI startup status, scale modes and command-mirror provenance

Reference: ASUS ProArt X870E-CREATOR WIFI BIOS 2503, SMU 98.84.0, SHA-256
`5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596`.
The user's 9950X3D is unavailable and its installed BIOS is unknown.
This REA/Ghidra-assisted follow-up executes bounded, byte-checked instruction
slices with synthetic replies. It extends the existing
[channel mirrors](amd-granite-channel-mirrors.md) and
[SVI helpers](amd-granite-svi-telemetry.md); it does not enable a hardware writer.

## Startup also initializes the command mirrors

The startup call sequence contains `0x32ae8` immediately followed by `0x20a20`,
at instructions `0x1140/0x1143`. The former initializes firmware channel
addresses to 1, 2 and 3. Its three-iteration loop writes register 25h with 7,
then calls `0x32f10`. That helper reads SVI register 02h through `0x32bf8` and
extracts bits 7:5. The three mode bytes go to `0xc5b6`, `0xc5c2`, `0xc5ce`.
All 256 possible register bytes are covered for every channel, with 768 mode
stores. The address stores and mode loop execute as separate slices; unrelated
timing/peripheral initialization between them is outside this proof.

`0x20a20` then calls local-status helper `0x32d38` for each channel. This helper:

1. Uses the configured base and address for the selected firmware channel.
2. Writes that address to the local select/status port at offset `0x73014`.
3. Reads the same local port once and returns bits 14:6 as a nine-bit code.

The helper sends no SVI register-read packet, checks no completion bit and has
no polling loop. The 6,144 cases vary all 512 returned codes, three channels,
two address assignments and unrelated status bits, including completion bit
15 clear. These tests establish the returned bits, not their freshness or
their relationship to a device's applied voltage. Peripheral replies and
channel bases are fixtures; the tests never touch host registers.

The caller stores the returned code into both local code halfwords and
converts it into the same software float later maintained by direct writers:

| Firmware channel | Code halfwords | Software mirror | PM float index |
|---|---|---|---|
| 0 | `0x8c6c/0x8c6e` | `0x8c74` | 48 |
| 1 | `0x8c84/0x8c86` | `0x8c8c` | 53 |
| 2 | `0x8c9c/0x8c9e` | `0x8ca4` | 58 |

The conversion is `0.245 + code * 0.005`, except that channel 2 uses
`0.49 + code * 0.01` when its mode equals 1. This source of the mode byte is
newly connected to the earlier conversion proof. Other mode codes still use
the first conversion. The 4,096 initializer cases cover all eight modes and
all 512 codes for each channel, checking 12,288 stored floats. The selected
slice leaves override flags unchanged. Code zero retains the software offset;
the resulting number must not be described as a measured voltage.

Consequently, **command mirror** describes a software state with more than
one producer: a converted local status code at startup, and a locally
calculated command value after a later writer. Neither producer establishes
continuous device readback. The discovered-function inventory has one direct
caller of `0x32d38`, in startup; that incomplete inventory is not proof that
no indirect or undiscovered caller exists.

The startup path does not bind a channel to CPU, SoC, MISC or a second CPU rail.
Its addresses 1/2/3 do not identify PM CCD slots or physical board connections.
The existing PM averaging path is covered by the preceding channel-mirror
proof; this follow-up does not change its offsets, scale or averaging order.

## A separate guarded register wrapper

The image contains a pointer at `0x6f8c` to handler `0x330b0`. Unlike the
previously studied RSMU 91h wrapper, this handler rejects a request when byte
`0xc5aa` is nonzero. With that byte clear, it accepts only channel 0/1/2 and
register 20h..2Ch excluding 22h. It forwards a byte from argument bits 23:16
to `0x32cbc` while holding lock 2000h, then reports status 1. Rejections report
FFh before calling the lock or register writer.

For register 20h it also selects the channel and updates a local timing word:
argument data bits 3:0 supply both local nibbles 7:4 and 11:8, bit 4 supplies
bit 28, and bit 7 supplies bit 29. Other local bits are retained. The updated
word is copied into the channel's software configuration record. The 5,120
cases include 72 accepted requests, 5,048 rejections and six register-20
cache updates. The mailbox, lock and downstream register writer are explicit
fixtures here; the older helper proof separately covers packet construction.

This checks the handler's gates and local effects. It does not establish an
enabled host route to this table entry, the physical register contract, a
safe setting range, successful device application or a second-rail getter.
In particular, these gates must not be assumed to protect the separate 91h
wrapper. No new runtime command is allowed.

## Reproduce

```sh
python3 analysis/tools/firmware-granite-rail-routing.py \
  --smu build/firmware-input/smu-62-54-body.bin \
  --study build/firmware-evidence/smu/smu-study.json \
  --output build/firmware-evidence/granite-rail-routing.json
```

The proof pins the image hash and checks every executed instruction byte.
Register-window calls are explicit boundaries, not full processor emulation.
Controller replies, timing, installed-board wiring and electrical effects are
not validated. Physical rail identity, independent second-rail write/readback,
the remaining eight core/CCD units and PM-to-physical-core mapping remain
unresolved. Completed cloud results belong in the
[validation report](amd-granite-validation.md).
