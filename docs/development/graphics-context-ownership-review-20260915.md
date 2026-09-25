# Graphics ownership correction: adversarial review

This review covers the correction following diagnostics commit
`7d2195273c220b9e7d5f3ce7c059a8cff355dfed` on `main-VR`.
It retains the loading-menu transaction guard and removes CSX's permanent
per-call D3D11 protection. It is not a full revert of PR93.

## Evidence and scope

PR93 merge `5adb39d62981f855fd57af77c45bd57cf9b8a233` addressed real
synchronization gaps, as described in the supplied originating-machine
handover. Its reported 70 successful COCs belong to its original tested
source, not this correction. The local five-build investigation identified
new D3D11 locking cost at PR93. Earlier DLSS CPU/GPU regressions remain
separate; sampled CPU execution is not interchangeable with fpsVR CPU time.
No new runtime measurements are published by this implementation review.

## Source-to-test findings

| Concern                                   | Production path and correction                                                                                                                                                               | Verification / limit                                                                                                                                                                                                                                                                    |
| ----------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Permanent API locking                     | Both device-creation paths, RenderDoc bypass, proxy candidate and renderer initialization call the same read-only validation helper. Device flags still exclude SINGLETHREADED.              | WARP regression failed before correction; creation, output cleanup, unsupported contexts and external flag preservation tests pass. No source call to SetMultithreadProtected remains.                                                                                                  |
| Conflicting temporary owners              | Screenshot and flowmap never save/restore the shared API flag. They use the native renderer owner.                                                                                           | Concurrent production RAII transactions preserve WARP viewport state with API protection off. Another module's enabled flag is preserved.                                                                                                                                               |
| Stale context admitted by cached identity | Auxiliary access now compares its context with the native renderer's published context, rather than the cached globals pointer. Readiness uses that same binding.                            | New production-header test first failed on a mismatched context; current, replaced, unpublished and absent renderer cases are covered.                                                                                                                                                  |
| Loading-message deadlock or lost clear    | Existing byte-validated VR guard remains try-only and reentrant. Its callback and pending-state contract are unchanged; it shares the RAII primitive.                                        | Loading clear tests cover rejection, contention, recursion and exception release. A new real readback test defers a competing loading clear and then executes the preserved pending work.                                                                                               |
| Worker lock order                         | Screenshot staging and worker Map use try-only ownership. The worker removes its job from the queue before acquiring the renderer; no queue/lifecycle lock is acquired by the copy callback. | Existing 500 ms Map retry bound is unchanged. Success, HRESULT copy failure and exception release unmap before unlocking. No lock is held while sleeping or encoding.                                                                                                                   |
| Flowmap state and I/O                     | Private deferred recording remains private; ExecuteCommandList(TRUE) and DirectXTex capture acquire the renderer separately.                                                                 | WARP tests exercise command-list state restoration under concurrent renderer ownership. DataLoaded's synchronous flowmap call holds no CSX UI/submit lock; disk loading/encoding/publication are outside ownership. Native startup and actual regeneration still need runtime exercise. |
| Native / third-party coverage             | Native rendering must honor its renderer critical section; auxiliary capture cannot safely use a foreign context.                                                                            | Unit tests prove helper semantics, not every game/mod call path. Present, stereo submission, screenshot during loading, renderer lifetime and third-party writers require exact-build runtime coverage. No speculative submit locks or scheduling changes were added.                   |
| Readiness claims                          | Raw API flag remains visible; apiReady means context eligibility. Aggregate readiness also requires the native owner and applicable loading guard.                                           | The DevBench description and ownership document describe implementation readiness, not proven stability or performance.                                                                                                                                                                 |
| Settings contract drift                   | The generator hashes complete settings-owner source files, including ScreenshotFeature and Upscaling. Review found no settings/default/schema change.                                        | Refresh the source fingerprint and generated provenance together. Preset rendering values remain unchanged; only embedded contract metadata changes.                                                                                                                                    |

The engine renderer and its critical section retain native lifetime ownership.
The helpers hold no global COM reference and do not create an independent
mutex that native rendering could bypass. The screenshot's COM references
retain its device/context; they do not certify native renderer lifetime at
process teardown. Forced teardown and replacement remain runtime evidence
requirements, not results inferred from an isolated WARP fixture.

## Validation

The initial regression was built with:

```powershell
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target d3d_context_protection_test -- /m:1
ctest --test-dir build/ALL -C Release -R '^D3DContextProtection$' --output-on-failure --no-tests=error
```

It failed before removing automatic protection. The published-context
regression separately failed before its correction. Original failure output
is retained under `build/cpu-burst-diagnostics/pr93-regression-failing-test.log`
and `pr93-review-regression-failure.log`.

Final validation uses:

```powershell
pwsh ./tools/validate-local.ps1 -OutputDirectory build/validation/pr93-ownership-reviewed-20260915
```

This explicitly builds the main DLL, controller_tests and shader_tests,
saves the CTest inventory, runs complete CTest with a 300-second per-test
limit, tests the preset generator and its -Check contract, checks the build
manifest and records source identity and git diff --check. Results: **passed** in 218.109 seconds. All 124 discovered tests were built
and run: 124 passed, zero failed, skipped, disabled, missing or not built.
CTest took 53.83 seconds; the shader test took 45.57 seconds. Main DLL,
controller and shader targets built; preset tests, generated-preset check,
manifest verification and `git diff --check` passed. The complete record,
commands, inventory and logs are in
`build/validation/pr93-ownership-reviewed-20260915/summary.json`.

Configuration: ALL / CSmain, universal Release, DevBench ON, CMake 4.4.1,
MSVC executable version 19.51.36256.0 (installed under toolset directory
14.52.36615). The validated working-tree Build ID is
`ae8ac2a805a295cbc50ca590dff70c7c45b147e05c18e6ebfdcc638a8085fdfe`;
DLL SHA-256 is
`b71327247fdd6c571d8a2c5f07aeae7458faeebf52c5c6f42a1b960d68512c9d`
(28,884,480 bytes). This is the reviewed source before committing, not a
claimed clean post-commit runtime build. Final edits after that validation
only record its results in this document.

Scoped pre-commit whitespace, line-ending, clang-format and prettier checks
passed. For `Upscaling.cpp`, clang-format 22.1.4 was checked specifically at
changed ranges 15689:15697, 15760:15768 and 15833:15840 with `--dry-run
--Werror --style=file`; unrelated legacy formatting was preserved. Other
changed C++ files passed the complete-file hook. The commit hook skips only
that already checked formatter to avoid rewriting unrelated legacy code.
No CMake source is part of this ownership commit.

## Remaining runtime evidence

Run the exact corrected build through 20 alternating COCs with the retained
loading guard and actual stereo screenshot readback. Check every destination,
terminal generation, both eyes, retirement and active stretch at stop.
Exercise flowmap regeneration and its failure fallback. Repeat matched
performance lanes with equal tracing and warm shader state. SE/AE runtime
coverage remains outstanding. Neither the original 70 COCs nor helper tests
certify this candidate. The physical-HMD qualification matrix and its
20 COC / 25 menu counts are unchanged.
