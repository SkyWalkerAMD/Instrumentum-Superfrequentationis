# Granite model aggregates and the SVI calibration boundary

Reference: ASUS ProArt X870E-CREATOR WIFI BIOS 2503, SMU/MP5 98.84.0.
The user's 9950X3D remains unavailable and its installed BIOS is unknown.
This is a byte-checked synthetic instruction replay, not a hardware capture.
It establishes **no additional physical units**.

The [existing consumer study](amd-granite-config-routing.md) starts the model
reduction at SMU `0x2800d` with its earlier additive inputs supplied as fixtures.
The new [calibration-boundary proof](../analysis/tools/firmware-granite-unit-calibration.py)
executes the preceding instructions from `0x27ed4` and continues through the
three output stores, stopping before `0x28064`. This removes the earlier
arbitrary starting values of floating registers `f8/f9` in that reduction.
It does not recover the calibration of their source fields.

## Independent telemetry and model inputs

The replay composes three existing instruction paths rather than replacing
them with assumed results:

1. SMU `0x27e41..0x27ed0` converts synthetic SVI raw codes into voltage,
   current and power. The existing [telemetry evidence](amd-granite-svi-telemetry.md)
   anchors this power in watts, without assigning a physical rail name.
2. MP5 `0x2ce90..0x2cf35` aggregates independently varied core-row-0 and
   CCD-row-0/1 sources. An explicit 0xe4-byte copy transfers each record to
   `0xa1c0 + 0xe4*firmware_target`. The copy is a fixture, not DMA completion.
3. SMU `0x27ed4..0x28064` consumes those records and its separate local
   additive inputs. Original loop instructions are followed; the decompiler's
   incomplete loop rendering is not used as execution evidence.

At `0x27fa9..0x27fbb`, the SMU sums three SVI power sources at `0xa62c`,
`0xa644` and `0xa65c`. The result enters `0xa388`, stored at `0x27fee`.
The core/CCD model sum instead enters `0xa38c`, stored at `0x2805e`.
The adjacent alternative composite is stored at `0xa390` at `0x28061`.

The table gives the change in each composite for a one-unit increase in the
selected source, with every other source held at zero in the experiment.
The one-unit source value is a synthetic arithmetic coordinate unless its
unit is independently anchored; it is not a supported operating adjustment.

| Independently varied source | `0xa388` | `0xa38c` | `0xa390` |
|---|---:|---:|---:|
| Each of three converted SVI power values | 1 | 0 | 0 |
| Each core-row-0 source, either firmware target | 0 | 2 | 0 |
| Each CCD-row-0 or CCD-row-1 source, either target | 0 | 1 | 0 |
| Alternate aggregate offset +0x1c or +0x20, either target | 0 | 0 | 1 |
| Local `0xa4f4` or `0xa704` | 1 | 1 | 1 |
| Local `0xa6fc` | 1 | 1 | 0 |
| Local `0xa520` or `0xa548` | 1 | 0 | 0 |
| Local `0xa470` or `0xa528` | 0 | 1 | 0 |
| Local `0xa4a0` or `0xa530` | 0 | 0 | 1 |
| Local `0xa554` | 0 | 1 | 1 |
| Selected lookup intercept, with slope zero | 0 | 0 | 1 |

The factor two for core row 0 follows the aggregate's core sum appearing in
both record +0 and record +4. It is not a statement that physical core power
is double-counted. Likewise, shared additive fields do not prove that all
three composites or the exported model rows have the same calibrated units.
The upstream scales and purpose of those shared fields remain unresolved.

All three SVI power inputs can change `0xa388` without changing `0xa38c` in
this checked prefix. The earlier study independently showed that the later
feedback selection uses `0xa388` or `0xa390`, not `0xa38c`. A nearby watt-valued
input therefore still does not supply a direct unit anchor for core row 0 or
CCD rows 0/1. No GUI/CLI unit label or scaling is changed by this proof.

## Coverage and explicit boundaries

There are 1,368 cases: 38 independent source positions, amplitudes 0.25/1/8,
all four target-presence masks and three lookup inputs. The three lookup
inputs exercise the low/equal/high sides of the synthetic threshold table.
Dyadic values make the checked additions exact; the experiment is not a bound
over exceptional or arbitrary binary32 values.

The cache-maintenance call to `0x3534` is an explicit boundary. Each case
checks its arguments `(0xa1c0, 0x1c8)` but does not simulate caches, coherence
or DMA. The reciprocal instruction at `0x27f4f` receives an exact `0.5`
fixture for its supplied denominator 2. Its hardware approximation and FPU
exceptions remain outside the replay. Other additive model fields and the
alternate aggregate producers are independent fixtures, not discovered
coefficient units. The 623,808 copied record bytes are synthetic.

The pinned firmware bodies have these SHA-256 values:

| Input | SHA-256 |
|---|---|
| SMU | `5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596` |
| MP5 | `fb5d96c17b909cc4f681523ccc931796e27c7457ba5ad637a3d4fce1c78ce556` |

The original instructions come from retained REA/Ghidra study run
`42432173976`. Both new consumer-prefix entries already exist in that study;
no new engine entry or speculative function boundary is required. The report
also records both study-file digests. These local addresses and digests are
evidence coordinates, not fabricated REA Evidence IDs.

```sh
python3 analysis/tools/firmware-granite-unit-calibration.py \
  --smu build/firmware-input/smu-62-54-body.bin \
  --smu-study build/firmware-evidence/smu/smu-study.json \
  --mp5 build/firmware-input/mp5-62-54-body.bin \
  --mp5-study build/firmware-evidence/mp5/smu-study.json \
  --output build/firmware-evidence/granite-unit-calibration.json
```

The local Python 3.14 replay passed with trace SHA-256
`162980c5c57c94e2371364dcc3e6416e10d0201ecca1bed644f6da8eef1e7d6b`.
Cloud validation is a separate result and is not claimed here.
Core rows **0/4/12**, CCD rows **0/1/2/6/8**, and common configuration words
**259/260/261/262/269** retain unverified physical units. This experiment
specifically advances the three model rows' consumer evidence; it does not
resolve the remaining counter clocks/events, configuration calibration,
physical CCD/OS mapping, or independent second-rail write/readback.
