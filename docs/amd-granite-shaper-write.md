# Granite Curve Shaper saved-offset writes

The fixed Granite 98.84.0 firmware supports a narrower, verifiable contract:
**write one tier's three saved offsets and compare the complete saved table**.
Reading the hidden baseline is not necessary to establish those saved bytes.
It remains necessary to predict the combined downstream values, and neither
saved-table readback nor the host acknowledgment proves successful device
application.

Reference input: ASUS ProArt X870E-CREATOR WIFI BIOS 2503 / SMU 98.84.0,
SHA-256 `5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596`.
The user's 9950X3D is unavailable and its installed BIOS is unknown.
This study uses retained REA/Ghidra research artifacts and byte-checked
Xtensa slices. It does not claim a new REA native-session evidence ID, a
target-machine capture, or new electrical limits.

## GSW-01: the saved-state contract does not require baseline contents

The initial RSMU table at `0x6fa4` contains `A6h -> 0x2abcc, flags 6` and
`84h -> 0x2ac14, flags 6`. The existing [Curve Shaper study](amd-granite-curve-shaper.md)
establishes the three-byte tier write, the saved-table getter and downstream
baseline addition. The new proof composes their actual instruction paths.

The setter's argument is:

`tier | uint8(low) << 8 | uint8(medium) << 16 | uint8(high) << 24`

Here tier is `0..4`; the three values are signed counts, not millivolts.
Canonical application arguments keep bit 7 clear. The firmware ignores bit 7
in this host handler; it is not a host deferred-commit option.

The handler requires `0x2c910(2)`, equivalent to capability bit 2 in the
version-specific capability query. The handler has no manual-OC check.
An application may additionally require manual OC off as a conservative
product rule; that restriction must not be described as an A6 firmware gate.

At `0x2abfc..0x2ac04` the selected tier's bytes are saved to
`0x13993..0x139a1`. The helper `0x2ab74` then visits all 15 saved bytes:

1. It sign-extends the byte, clamps to `[-50,30]`, and stores it back.
2. It adds the corresponding signed baseline byte at `0x13984..0x13992`.
3. It packs the sum into a byte and forwards it, without replacing the saved
   offset with that combined value.

The getter at `0x2ac14` reads only the saved offset region. Its five words
contain exact low-byte tags `0..4` and the three corresponding signed8
offsets in the higher bytes. It does not add the baseline during reads.

The new proof enumerates all 65,536 baseline/runtime raw-byte pairs through
the actual clamp/add/store slice `0x2ab9a..0x2abae`. The clamp preserves the
runtime byte **if and only if** its signed value is in `[-50,30]`.
It then exercises every allowed integer in each of the 15 cells with two
valid prior tables and three different baseline fixtures: 7,290 complete
setter/getter compositions and 87,480 unchanged nonselected-cell checks.
The saved result is independent of the baseline fixture.

## GSW-02: why the whole prior table must be checked

Each accepted A6 call normalizes every saved cell. Checking only the selected
tier cannot prevent changing another tier whose current byte is `31` or
`-51`. Requiring **all 15 current saved values** to be within `[-50,30]`
before writing makes that normalization an identity operation for unselected
tiers, under the tested unchanged-input condition.

The caller must compare all five raw words with the expected prior snapshot,
including tier tags, and compare all five words again after writing with a
copy of that snapshot in which only the selected three bytes were replaced.
A matching selected tier alone is insufficient. A malformed, partial, stale,
or out-of-range prior table must not prepare a write.

This is not a firmware atomic compare-and-swap. Sixty synthetic interleaving
examples alter one cell after a hypothetical valid prior snapshot; 48 show
an unselected out-of-range cell being normalized by the later A6. The mailbox
command has no expected-table parameter and cannot reject that race.
Version and capability checks do not detect another writer that preserves
those values. Application session serialization and full pre/post checks are
useful safeguards, but cannot make a multiword firmware query or the whole
operation atomic relative to other firmware or host writers.

## GSW-03: matching saved values do not authenticate application

The helper always broadcasts five MP5 `47h` packets and then the separate
commit packet `0x00000080`. It repeats all five tiers even when one tier was
edited, and even when the requested saved values already equal the old ones.
Fifteen identical-value cases retain the table and still send six packets.
The application should detect an identical requested tier after comparing
the current snapshot and return without issuing A6.

The new composition executes original broadcaster `0x1d760` for each of the
six packets, with explicitly scripted completion words. It selects firmware
targets using `0x6816`; neighboring `0x6814` is deliberately different in
the test. Of 160 completed routing cases, 90 use non-success nonzero target
statuses and still obtain host ACK 1 with matching saved values. Forty have
no selected target yet still store the offsets and acknowledge the host.
No MP5 execution or physical delivery is implied by these fixtures.

Twelve stalled-target cases stop the actual completion polling at a bounded
instruction limit, once per packet and target. The selected saved bytes have
already changed although there is no host ACK. The test inspects synthetic
RAM offline afterward; it does not assert that a busy real mailbox would
accept a getter. Therefore an error, timeout or cancellation after message
submission must preserve an honest `write_attempted` state and must not be
reported as proof that the table stayed unchanged.

Even within the allowed saved-offset range, a baseline can cause signed8
wrap in the forwarded byte: `127 + 30` becomes `0x9d`, decoded as `-99` by
the downstream handler. The raw-pair enumeration has 1,740 such wrapped
combinations for legal offsets. The baseline's actual values and provenance
remain unknown. This does not invalidate saved-offset readback; it prevents
claiming a verified effective offset, voltage, safe electrical range or
successful application from that readback.

## Product boundary supported by this evidence

A shared GUI/CLI implementation may expose this saved-offset operation with
the following contract:

- Require the exact Granite profile, CPU/PCI identity, nonvirtual target and
  firmware `0x00625400`, and require the supported capability bit.
- Accept one tier `0..4`, three integers `-50..30`, a valid complete prior
  five-word table and its full capability word. Reject noncanonical or
  out-of-range input before submitting a setter.
- Read a fresh complete table with version/capability/identity checks, match
  the complete expected snapshot and capability word, and require all 15
  current signed offsets in range. Recheck identity/version/capabilities
  before submitting the single A6 call.
- Skip the setter when the selected saved values are identical.
- Following an accepted write, perform one complete 84 read with the same
  identity/version/capability checks and compare all five words with the
  expected result. Never substitute a software-generated expected table for
  a missing or partial reply.
- Distinguish host acceptance, saved-table match and unknown application.
  After an attempted write, errors are not rollback or unchanged-state
  guarantees. Do not automatically retry, roll back, or switch mailboxes.

The outcome should be described as **saved offsets read back and matched;
device application remains unverified**. This command does request downstream
application, so it must not be presented as an inert local-cache edit. This
study supplies no baseline getter, no physical frequency/temperature anchors,
no independent rail readback and no permission to expose raw MP5 packets.

## Reproduce and coverage

```sh
python3 analysis/tools/firmware-granite-shaper-write.py \
  --smu build/firmware-input/smu-62-54-body.bin \
  --study build/firmware-evidence/smu/smu-study.json \
  --output build/firmware-evidence/granite-shaper-write.json
```

| Original-byte experiment | Cases |
|---|---:|
| Every raw baseline/runtime byte pair | 65,536 |
| Allowed integers at every cell, complete setter/readback | 7,290 |
| Explicit competing prior-table mutation | 60 |
| Six-packet routing and nonzero completion outcomes | 160 |
| Stalled packet/target boundaries | 12 |
| Identical saved-value writes still broadcast | 15 |
| Total | 73,073 |

The proof pins the SMU hash and checks each executed instruction byte.
Its signed8 arithmetic oracle uses `struct.unpack('<b', ...)`; the ISA SEXT
decoder and raw instruction identities were independently checked in the
preceding Curve Shaper study. Fuse replies, locks, initial RAM, completion
responses and hypothetical interleaving are explicit fixtures. No physical
mailbox, MP5 target, VRM or OS memory mapping is accessed. Cloud run and
application test results belong in the validation report after they complete.
