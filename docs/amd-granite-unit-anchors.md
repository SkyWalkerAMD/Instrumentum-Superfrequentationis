# Granite MP5 timer conversions and the unresolved unit anchor

Reference: ASUS ProArt X870E-CREATOR WIFI BIOS 2503, MP5 98.84.0,
SHA-256 `fb5d96c17b909cc4f681523ccc931796e27c7457ba5ad637a3d4fce1c78ce556`.
The target machine is unavailable. This extends the static timing lead in
[the producer study](amd-granite-producer-paths.md) with original-instruction
replay; it does **not** establish a timer frequency or a new physical unit.

The new [bounded proof](../analysis/tools/firmware-granite-unit-anchors.py)
executes the integer conversion helpers, call argument preparation, timer
reader and deadline comparisons. Timer words and register replies are explicit
fixtures. MMIO accesses operate only on synthetic data. All addresses below
belong to the MP5-local reference address space.

## Integer conversion helpers

`0x32748` multiplies two unsigned 32-bit inputs and returns the 64-bit product.
`0x32440` divides an unsigned 64-bit numerator by a supplied denominator.
The proof executes both helpers' original instructions, including shifts,
16-bit partial products, carries, normalization and quotient corrections.
It does not replace these calls with supplied arithmetic results.

The 1,124 multiply cases include a ten-value boundary grid and deterministic
random inputs. The 2,058 divide cases cover the two denominators actually used
here, 1,000,000 and 1,000,000,000, with boundary and deterministic 64-bit inputs.
Each result is compared with independent integer arithmetic. Other divisors
and division by zero are outside this proof.

The separate bounded integer interpreter keeps its own `SAR` semantics:
`SSL` writes `32-(input & 31)`, `SSR` writes `input & 31`, and `SRC` shifts the
concatenated register pair. This follows the primary
[QEMU Xtensa translator](https://github.com/qemu/qemu/blob/v9.0.0/target/xtensa/translate.c).
It does not change the shared floating-point replay interpreter.

## Timer words and polling deadlines

Reader `0x22c8` saves interrupt state, reads the low word at `0x32001c0`, reads
the high word at `0x32001c4`, and restores interrupt state. The checked code
does not retry the pair. Twenty fixtures verify the order and returned words.
**Peripheral latching and atomicity are unknown**: this does not demonstrate
that real reads tear, or that interrupt masking makes the pair coherent.

Poll helper `0x305bc` converts its fourth argument as follows:

```text
ticks = floor((uint32(argument) * 100000000 + 999999999) / 1000000000)
deadline = uint64(initial_timer + ticks)
```

It reads the masked register while the new timer is **less than or equal to**
that deadline. A timer strictly above the deadline ends the loop before the
next register read. An argument of zero disables the deadline check; it does
not mean an immediate timeout. Both paths retain the masked expected bits
in the integer result register; there is no separately assigned timeout status
in this checked function. A caller must not treat that return as device success.

The 156 deadline cases vary 13 arguments, four starting counters and three
positions relative to the computed deadline. They include 52 equal-deadline
reads, 44 expired cases without a register read and 24 wrapping sums. A further
three cases follow mismatching replies through the inclusive deadline into
timeout, and one zero-argument case polls three times until a match. One
boundary experiment shows that a 64-bit-wrapped deadline can already compare
below the initial counter; the helper uses absolute unsigned ordering rather
than wrapping elapsed-time subtraction. This is a synthetic boundary finding,
not a claim that an ordinary hardware session reaches the 64-bit wrap.

## The other delay conversion narrows to 32 bits

Slice `0x29002..0x29015` loads the word at `0xe448`, multiplies it by
100,000,000 and divides by 1,000,000. The subsequent call to delay helper
`0x22e4` receives only the low 32-bit result:

```text
ticks = low32(uint32(input) * 100)
```

Nine boundary fixtures include 42,949,672 and 42,949,673, the last input before
and the first input after truncation begins, plus the maximum unsigned input.
The original delay loop uses unsigned 32-bit elapsed subtraction, so the
fixtures also cross the low-word wrap. This is an arithmetic boundary, not
an accepted user tuning range or an assertion about the input's physical unit.

## Remaining physical evidence

The constants are consistent with a nominal 100 MHz timer and arguments in
microseconds or nanoseconds. No independently identified peripheral clock or
documented argument contract binds those units here. The official
[AMD Model 44h PPR](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/57896.zip)
defines a 100 MHz reference clock, but it does not identify this MP5 timer.
The elapsed counter's event inputs remain unverified as well.

Therefore core rows **0/4/12** and CCD rows **0/1/2/6/8** retain their existing
unverified physical units in GUI and CLI. These additional proofs do not change
the decoder's scaling, authenticate a capture's BIOS, establish physical core
mapping, or authorize a hardware write.

```sh
python3 analysis/tools/firmware-granite-unit-anchors.py \
  --mp5 build/firmware-input/mp5-62-54-body.bin \
  --study build/firmware-evidence/mp5/smu-study.json \
  --output build/firmware-evidence/granite-unit-anchors.json
```
