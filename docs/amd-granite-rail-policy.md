# Granite automatic channel policies and override limits

The automatic consumers for firmware channels 1 and 2 have bounded request
policies and separate raise/lower phases. Those policy caps do **not**
constrain the explicit channel override helpers. This closes another part of
the control-flow analysis, without establishing physical rail identity or
independent device setting readback.

Reference SMU 98.84.0 body from ASUS ProArt X870E-CREATOR WIFI BIOS 2503:
SHA-256 `5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596`.
The user's 9950X3D is unavailable and its installed BIOS is unknown. The proof
uses retained REA/Ghidra static exports and original instruction bytes; it
does not claim another REA native-session evidence ID or hardware run.

## GRP-01: request arrays feed independent pending codes

The existing [channel override paths](amd-granite-channel-mirrors.md) call
the known direct writers, but another set of consumers calculates automatic
target codes:

| Firmware channel | Compute/raise phase | Lower phase | Current / pending code | Override flag |
|---|---|---|---|---|
| 1 | `0x20c10` | `0x20c90` | `0x8c84` / `0x8c86` | `0x8c88` |
| 2 | `0x20cb0` | `0x20d4c` | `0x8c9c` / `0x8c9e` | `0x8ca0` |

Channel 1 computes the nonnegative maximum of **14** float requests at
`0x8cc4..0x8cf8`, adds the base at `0x8c90`, then uses the original
subtract/scale/ceil sequence. A second, distinct 14-float array at
`0x8d5c..0x8d90` supplies its own nonnegative maximum stored at `0x8d94`;
it is not added into this target-code calculation. The postincrement
instruction ordering matters: the second array begins at `0x8d5c`, not the
preincrement address plus the displayed displacement.

Channel 2 takes the nonnegative maximum of four requests at
`0x8cfc..0x8d08` and adds the base at `0x8ca8`. Mode byte `0xc5ce == 1`
selects the `0.49 / 0.01` convention; other modes use `0.245 / 0.005`.
These are software-request/code conventions, not physical rail identities.
Request-slot numbers do not establish physical CPUs, CCDs or OS-core IDs.

The compute/raise phase always stores the pending code, then requests a
direct write only when pending exceeds current. The later lower phase
requests it when pending is below current. Both hold lock `0x2000` in the
inspected path. The direct writer honors the override flag and can leave the
current code and software mirror unchanged while a newly calculated
automatic pending code is retained separately. No device register is queried
by these policy consumers.

## GRP-02: automatic caps and their bypass

When byte `0xae11` is zero, channel 1 caps the automatic target to `0xd3`
(211). Channel 2 caps to `0x5b` (91) in mode 1 or `0xe7` (231) otherwise.
Using the previously established software mirror conversions, the nominal
values are 1.3 V for channel 1 and 1.4 V for channel 2. **These are firmware
automatic-policy limits, not established electrical safety ranges.**
Setting `0xae11` nonzero bypasses these caps. Its full provenance and runtime
permission meaning are not recovered here.

The override helpers `0x20de4` / `0x20e18` clear the corresponding override
flag, call the direct writer with the caller's original code, then set the
flag. They do not invoke the automatic-policy clamp above.

Six original-byte examples, all with `0xae11 == 0`, forward codes beyond
those automatic caps without alteration: channel 1 sends 212 or 511;
channel 2 sends 232 or 511 in ordinary mode and 92 or 511 in mode 1.
Only packet-service arguments are recorded; no controller packet is issued.
This is evidence against treating the automatic limit as protection provided
by a raw override command, not evidence that the larger values are safe.

## GRP-03: release allows later policy replacement

The release helpers `0x20e08` / `0x20e3c` only clear the flag. Three composed
examples then invoke the lower policy phase and observe that its pending
code is forwarded. Thus release does not restore a previous code immediately,
but a later policy phase can replace the override with the pending request.
Actual scheduling and hardware transitions are outside the experiment.

This is a connection between a recovered override/release protocol and its
automatic consumers. It is not a transactional restore operation, and the
stored pending/current codes remain distinct from device measurements.

## Reproduction and boundaries

```sh
python3 analysis/tools/firmware-granite-rail-policy.py \
  --smu build/firmware-input/smu-62-54-body.bin \
  --study build/firmware-evidence/smu/smu-study.json \
  --output build/firmware-evidence/granite-rail-policy.json
```

| Original-byte experiment | Cases |
|---|---:|
| Automatic consumers, all request positions, raise/lower phases | 2,112 |
| Explicit override exceeds automatic cap | 6 |
| Release followed by pending-policy phase | 3 |
| Total | 2,121 |

Every executed instruction is matched against the fixed binary. Finite
quarter-unit inputs give an independent integer oracle for the expected
ceil/code result; this is not a complete FPU or non-finite-input proof.
The cases cover all 14/4 request positions, both phases, the cap-bypass byte,
override flags and both channel-2 mode conventions. Current-code fixtures
include values outside the selected packet's nominal bit width; these are
synthetic local-state tests, not asserted target measurements.

Locks and `0x32d6c` packet submission are explicit service boundaries. The
experiment does not query registers, emulate device completion or model
electrical transitions. No new runtime writer is enabled. Channel 1/2 names
remain firmware-local: this study does not identify them as CPU/SOC/MISC or
as a second physical CPU rail. The missing contract remains physical-channel
binding, suitable parameter bounds for the explicit writer, and independent
device setting readback. The automatic caps cannot fill those gaps.

The generic channel step-override helper `0x20da4` was also located in a
separate bounded reference scan. The discovered direct-call inventory and
aligned initial pointer scan have no references to it. That absence does
not exclude an indirect route and does not justify exposing it; the new
proof does not execute that helper. Cloud execution and acceptance results
belong in the current [validation report](amd-granite-validation.md).
