# PR73 rollback to the measured readiness implementation

The user requested withdrawal of the pending-drain polling change after the
NVIDIA assay crashed during transition 26. Revert `cf1616728` and restore
the runtime source and tests from `c73bae9a7`, the last successfully measured
build. Retain the existing readiness correction and its conservative
handling of memory relief, mixed retries, partial mutation and recovery.

The interrupted run is `nvidia-2026-09-11T06-52-05-183Z`. It completed
25 transitions before PID 34484 crashed during DLSS HoshiPa to FSR3 HoshiPa,
pass 1, row 26. No later transitions or second pass completed. The exact
invalid-pointer mechanism is unproven. The rollback does not claim that a
six-frame delay itself guarantees safe resource ownership.

## Restored measured build

-   Compiled source: `c73bae9a776e67614bba84ee0cecf3d076de259f`.
-   Integration base: `ef7c366dd73989b2b87751c0ef975db7c6fd310f`.
-   Build ID: `d098079db31d3f43d3433ba4023e500774ff1895ee85ea648c726731bc68a31f`.
-   DLL SHA-256: `9a777935e69bbd3c4d7b6400c7291727d6bec15c65708495fbed6719d0b19581`.
-   DLL size: 28,173,824 bytes.
-   AIO: `CSX_AIO-3.19-VR-mainVR-ef7c366d-PR73-c73bae9a-DevBench-no-cache.7z`.
-   AIO SHA-256: `99415f28e954605af644d29d373172bafc5d5c42c5dde595cfec6b34364c3b0a`.
-   AIO size: 89,652,569 bytes.
-   Universal SE/AE/VR Release, DevBench enabled, no FOMOD or prebuilt cache.

This is **PR73 new** in the existing performance tables. Its NVIDIA run
`renderscale-tuning-nvidia-2026-09-11T05-18-34-649Z` completed 66/66
transitions, including rows 26 and 28 in both passes. Terminal render and
Task 2 results are 66 PASS / 0 FAIL / 0 INCONCLUSIVE; applicable full-history
health is MET/MET. Formal improvement versus PR66 remains INCONCLUSIVE.

The [existing comparison](pr73-readiness-nvidia-comparison-20260911.md) and
[canonical ledger](vr-render-scale-ledger.md) retain the measured
results. The preserved tested binary provides the restoration identity;
a new source revert commit does not retroactively change its Build ID.
No new performance measurement or release qualification is claimed.

## Rollback validation

-   Passed: the restored `src`, `tests`, `package` and `features` trees have
    no difference from `c73bae9a7`.
-   Passed: rebuilt `VRVendorRelatchPolicy` and
    `VRPresentationStretchTelemetryPolicy` with MSVC C++23, `/W4 /WX`.
-   Passed: both focused CTests, with a 15-second per-test timeout.
-   The existing successful NVIDIA run validates the preserved build;
    no new game run or full DLL rebuild was performed for this rollback.

Commands from the main repository worktree:

```powershell
pwsh ./tools/git.ps1 -C artifacts/pr73-nvidia-readiness-publication diff --exit-code c73bae9a7 -- src tests package features
pwsh ./tools/cmake.ps1 --build artifacts/pr73-pending-poll-20260911/controller-build --config Release --parallel 2
ctest --test-dir artifacts/pr73-pending-poll-20260911/controller-build -C Release --output-on-failure --no-tests=error --timeout 15
```
