# Intel maximum non-turbo ratio

The Intel Controls panel and `octool-cli intel-read --cpu N` expose **Maximum
non-turbo ratio** on Intel family 6 models B7, 8F and CF. The value is read-only;
it is not added to the editable controls. Other CPU models do not acquire an
extra register read for this feature.

The value is bits 15:8 of `MSR_PLATFORM_INFO` (CE). It describes a fixed platform
ratio, not the current core frequency or the maximum turbo ratio. No MHz or
bus-clock estimate is inferred. A zero field or failed MSR access is reported
as unavailable, with a null JSON value; it never becomes a measured zero ratio.

The register definition follows Intel's
[SDM Volume 4, revision 085](https://cdrdv2-public.intel.com/835765/335592-sdm-vol-4.pdf),
the model-specific `MSR_PLATFORM_INFO` tables. The original
`MAX_NON_TURBO_LIM_RATIO()` performs the same extraction but ignores its MSR
wrapper's status. The [pinned complete function](../analysis/fixtures/legacy-intel-platform.json),
[instruction audit](../analysis/legacy-intel-platform-static.json), and
[offline emulator](../analysis/tools/legacy-intel-platform.py) cover 644
synthetic cases, including every ratio byte and failed reads with untouched
output arguments. These reproduce original instructions without loading the
original executable or accessing hardware.

Core tests cover all 256 values on each of the three profiles, access errors,
unlisted CPUs and preservation of read-only status. GUI and CLI tests cover
the same data path. Intel Controls also discards asynchronous results when the
selected CPU changes, rechecks the target after its confirmation dialog, and
retains the backend safely if a pending panel is closed.
