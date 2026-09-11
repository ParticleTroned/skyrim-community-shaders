# PR73 pending-drain polling

An immutable public-API settings transition can encounter a healthy pending
GPU drain after memory relief has invalidated shared presentation resources.
The old retry path waited six frames before polling again because faster
polling also required permission to skip the later settling guard.

Polling admission is now independent of presentation admission. A proven
pending drain before provider release can be checked on the next frame, even
when memory relief requires the ordinary settling guard. Such retries retain
their Backend classification and never gain readiness-only promotion credit.
Repeated pending results remain eligible for polling on consecutive frames.

The existing six-frame settling requirement, three coherent stereo frames,
next-cycle promotion, exact request ownership, resource retirement, memory
admission, mutation, device-loss, quarantine and recovery protections remain.
Unidentified reset/retirement waits retain their existing cadence. SE and AE
behavior is unchanged because both call sites are inside the VR relatch path.

The focused policy tests cover repeated Backend pending results, memory-relief
exclusion from promotion, default rejection, stale ownership, actual mutation,
failures, quarantine, recovery and counter saturation. The adjacent stretch
accounting policy also passes under MSVC C++23 with /W4 /WX /permissive-.

The existing measurements in
[the canonical ledger](vr-render-scale-comparison-ledger.csv) and
[PR73 versus PR66 report](pr73-readiness-nvidia-comparison-20260911.md)
predate this implementation. They remain unchanged and are not evidence of
its performance. Actual savings depend on when each GPU drain becomes ready;
a shorter polling interval does not waive the fence or guarantee a five-frame
improvement. Runtime validation must also check retry and cleanup churn.

## Build validation

The manual-test AIO was compiled from clean source
`cf16167283cf70b74aedab348e740b23f0e6a49d`, with all SE/AE/VR runtime
targets enabled, Release configuration and `DEVBENCH_BRIDGE=ON`.

-   Build ID: `a8d2e6a5f759ff30246fe77851f7cf73d01880d431019284482b0f05ee12afc0`.
-   DLL SHA-256: `402ffc0bb96e83fb29726ac5c016e07400846aaae9fecbd5035387825fc21701`, 28,173,824 bytes.
-   Archive: `CSX_AIO-3.19-VR-mainVR-ef7c366d-PR73-cf161672-DevBench-no-cache.7z`, 89,618,126 bytes.
-   Archive SHA-256: `19ffb951093d1344dc903f60303c02f3afc146aa5bb64985dd10babce91daebd`.
-   Passed: both focused CTest policies and all 163 shader assertions.
-   Passed: universal DLL link and DevBench-disabled Upscaling syntax check.
-   Passed: staged/archived manifest verification, `7z t`, and inspection of
    all 435 archive entries. No prebuilt shader cache or FOMOD is included.
-   Passed: pinned clang-format checks on changed C++ lines and the complete
    changed header/tests, remaining scoped hooks and `git diff --check`.
    Unrelated historical formatting in Upscaling.cpp was preserved.

The initial combined-target build returned MSB1011 after creating the archive.
A single explicit CommunityShaders target then completed successfully, with
the same Build ID and DLL hash. Archive integrity and identity checks passed
independently. The existing FidelityFX CMP0116 configure warning remains.

Build commands ran from `build/pr73-rebase-20260910`:

```powershell
pwsh ./tools/cmake.ps1 --build build/AIO-PR73-DevBench --config Release --target CommunityShaders --parallel 2
python ../../artifacts/pr73-pending-poll-20260911/verify-and-deliver.py
```

Local evidence is retained in `artifacts/pr73-pending-poll-20260911/` and the
archive's adjacent receipt and SHA-256 files. The prior DLL, PDB, libraries,
manifest and delivered archive are preserved. Deployment and game/runtime
tests were not performed: the user requested an archive for manual testing
and explicitly prohibited automatic deployment/runtime testing here.
The separate render-scale PR qualification remains unrun.
