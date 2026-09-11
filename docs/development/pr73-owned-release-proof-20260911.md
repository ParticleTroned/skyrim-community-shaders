# PR73: owned drain completion and presentation release

## Why

Measured source `269bded159c66f4d8b1a45a836078dfa6cdf3241` records an
owned drain's initial Pending result as a Backend retry. That truthful
history prevents the existing readiness-only promotion exception, even
after the exact owned transaction completes. The six-frame settling rule
did not change. The admission coupling adds conservative settling to
otherwise successful transitions.

The retained run is
`artifacts/renderscale-tuning/nv-pr73-269bded1-mtwz6pez`. It completed
66 transitions with no counted runtime failures. All 18 owned drains
were observed Ready one frame after Pending, in 45.33-56.79 ms. These
observations do not identify the exact GPU completion instant or prove
every initially pending fence had the same cause.

PR66 `a09e1cc77` has 19 retries across 17 transitions. Successful PR73
`c73bae9a7` has 19 across 19; `269bded15` has 30 across 30. Rows 15-19
and 26 use the owned path and conservative guard in both current passes.
Some older outliers improved while previously fast cases regressed;
retry counts alone are not a performance score. Row 25 improved only
in pass 1, so it must remain in the regression comparison.

Row 20 is separate: its non-owned Backend retry waited six frames,
303.47/280.64 ms, before readmission. Its retained blockers include
provider deferral and cleanup debt. This correction does not change that
path. Row 24's existing PreMutationReadiness exception also remains intact.

See the [complete retained comparison](nvidia-renderscale-tuning-pr73-269bded15-20260911.md)
and [crash investigation](pr73-pending-drain-crash-investigation-20260911.md).
The earlier `cf1616728` whole-transaction retry acceleration remains
reverted; its exact memory-corruption mechanism is unproven.

## What changed

An operation-specific completion receipt can preserve proof-driven
release after one observed owned Backend wait. It never reclassifies,
subtracts or suppresses a retry. The ordinary readiness-only policy and
the six-frame fallback are unchanged.

The receipt separates four requirements:

1. Old-provider drain tickets match the request, epoch, source and target
   generations, required provider set, device/context, runtime queue and
   fence, provider revisions and ticket serials. Cancellation followed by
   same-epoch reissue cannot reuse a previous ticket identity.
2. The exact reset completes in the same Apply invocation. Partial reset,
   failed creation, requeue and exceptions cannot publish its local
   receipt. Engine reconciliation and retirement accounting must succeed.
   Every outstanding intermediate serial must belong to this operation;
   displaced engine references require proven accounting with no restored,
   poisoned or changed-unproven ownership. Prior/foreign cleanup debt,
   failed admission and sticky cleanup-fence failures deny the exception.
3. The exact applied target is physically published and the provider
   generation is prepared. Newly created provider revisions and runtime
   identities are separate from the consumed old-provider ticket. DLSS
   viewport preparation remains a pre-dispatch requirement in the existing
   promotion path; successful vendor output is not required beforehand.
4. Both coherent eyes and the existing consecutive stable cycles remain
   mandatory. Publication still waits for the next compositor cycle.
   The receipt is revalidated before preparation, after preparation and
   before final promotion; any changed evidence restores conservative
   settling without renewing its start frame.

Successful reset cancels the old live tickets. Their consumed receipt is
historical evidence for this transaction, never a readiness ticket for
new provider resources. Owned work and presentation use the existing
native presentation mutex; destructive work stays after both matching
native eye callbacks have returned.

Pending retirement of exactly detached old references and an exact-owner
RapidRelatch trim may remain cleanup-only work after healthy admission
and target publication. Their existing tails, fences and service paths
continue unchanged. This is not a blanket memory-relief exemption:
pressure/recovery trim obligations, prior debt, mixed retry histories,
counter saturation, quarantine and failure retain the guard.

## Reporting contract

Additive events distinguish old-ticket consumption, target publication,
provider preparation and eligibility for the guard exception. Eligibility
does not itself enable vendor dispatch. Per-fence observations record
issue and first observed completion beside existing End/Signal and
completion checks; they add no GPU query, poll, Flush or dispatch.
The blocking-cleanup timestamp records when eligibility verifies the
remaining ownership obligations. It does not claim that cleanup-only
retirement or trim fences have completed; those retain separate evidence.

The reporter keeps release stages separate from drain attempts, which
close at Applied. It retains denied, revoked and unconsumed operations,
validates exact certificate identities and reports absent endpoints
explicitly. Existing request-queued preparation observations provide a
separate request-to-admission origin from qualification dispatch.

## Validation and remaining runtime work

The focused controller suite passed 17/17 tests, including production
drain helpers, actual final publication/Arm assembly, promotion with
DevBench ON/OFF, and the native callback-order hook. The standalone
retirement/retry policy runs eight adversarial test groups. Production
Upscaling.cpp compiled to an object with SE/AE/VR and DevBench ON.
Universal DevBench-OFF syntax checks also passed for Upscaling.cpp,
FidelityFX.cpp, Streamline.cpp, VRRenderScaleDevBenchBridge.cpp and
InSceneOverlay.cpp, with source hashes stable throughout each check.

Local evidence is retained under
`artifacts/pr73-owned-release-proof-validation-20260911/`, including
`controllers-formatted-tests.log`, its JUnit XML, native-boundary results and
compiler responses/source hashes. These tests use scripted provider and
device outcomes; they do not execute a real destructive GPU transaction.

No corrected runtime measurement or visual qualification is claimed.
The comparison must retain both passes of the complete 33-transition
matrix against measured `269bded15`, with PR66 as historical reference.
PR73's exact overall integration parent is
`ef7c366dd73989b2b87751c0ef975db7c6fd310f`. `bc077786d` is only the parent
of the owned-drain change and cannot substitute for that overall base.

The operator confirms the same modlist and conditions and controls
deployment, MO2, game startup and settings. This follow-up does not launch
or deploy to that session. Live lifecycle cases and the required
`csx-render-scale-pr-v1` qualification remain necessary before release
readiness can be claimed.

No runtime rows are added by this offline correction. The
[canonical comparison ledger](vr-render-scale-comparison-ledger.csv)
remains the durable measurement record, with all historical cells and the
latest complete local evidence preserved. The previously documented
GitHub file-size limitation remains; report links do not replace ledger
coverage or turn unavailable runtime evidence into a pass.

## Final clean-build evidence

The compiled correction is `554e484e3957178e2d144bf35266bfdcc0948642`. Its source
remained clean and unchanged throughout both universal Release builds and
the complete controller run. This documentation follow-up does not change
that compiled source identity.

| Build                    | Compiled source                            | Build ID                                                           | DLL SHA-256                                                        | DLL bytes |
| ------------------------ | ------------------------------------------ | ------------------------------------------------------------------ | ------------------------------------------------------------------ | --------: |
| Correction, DevBench ON  | `554e484e3957178e2d144bf35266bfdcc0948642` | `e7e9fa2d13f28c6727b9567741511a418c54113c190db2616bfb2c2ad1158d96` | `5bb50a859ccb62f00d20669a9ad24b6b704dc076054e86dfa8af9dda9db8bfac` |  28632576 |
| Measured PR73            | `269bded159c66f4d8b1a45a836078dfa6cdf3241` | `9b8b7f17af7ec7012eb194ad945b45756db313352ef6b2ec84d88a50bef9247b` | `086b2207bd4b615af5f7a1bb4f567bb0dba8e1c1ec5e5498cd440c623682380a` |  28184576 |
| PR integration parent    | `ef7c366dd73989b2b87751c0ef975db7c6fd310f` | `fdce0c5b3cb548e590bba2732f4bc6939b4872a19f379b7fd6ef7d8572ae4a0f` | `2972cfc3516b5b321ef1fdf9c364b2ee28ffc44a53d97ddc0386eb6d5abfe26f` |  28154368 |
| Correction, DevBench OFF | `554e484e3957178e2d144bf35266bfdcc0948642` | `3d4291f873029ecf0d60dffb47b766d5580d640ab36c16a01ed4c537e77e337a` | `f42fea9246cf828ed889f2bc04a619d6c2664657276a00f19ec792f49b6f4b37` |  23656960 |

The manual-test archive is in the repository's `dist` directory:

`CSX_AIO-3.19-VR-mainVR-ef7c366d-PR73-554e484e3-DevBench-no-cache.7z`

-   Archive SHA-256: `5e7391d27df85326521f78791c8079b38e6995699dd07326fd0356ea66c5f46d`.
-   Archive size: 89120629 bytes;
    435 entries.
-   DevBench enabled; universal SE/AE/VR; no FOMOD and no bundled shader cache.
-   Archive integrity, staged/archived DLL and PDB identity, manifest source,
    Build ID, DLL hash and size verification all passed.
-   Automatic deployment remained disabled. The archive was copied only to
    repository `dist`; no MO2, game or profile action was performed.

The exact integration-parent archive and the retained measured PR73
archive are also preserved in `dist`. Canonical manifest identity differs
from both references only in source commit. Compiler environment and
production Release compile/link settings match. Configure arguments differ
only in build-directory path and `BUILD_CONTROLLER_TESTS=ON`, which adds
separate test targets and does not change production compile/link settings.

Commands ran from the isolated correction worktree:

```powershell
pwsh ./tools/cmake.ps1 --build build/ALL-DevBench-ON --config Release --target Package-AIO-Manual --parallel 2 -- /verbosity:minimal
pwsh ./tools/cmake.ps1 --build build/ALL-DevBench-OFF --config Release --target CommunityShaders --parallel 2 -- /verbosity:minimal
pwsh ./tools/cmake.ps1 --build build/ALL-DevBench-OFF --config Release --target controller_tests --parallel 2 -- /verbosity:minimal
ctest --test-dir build/ALL-DevBench-OFF -C Release -L ControllerTests --output-on-failure --no-tests=error --timeout 60
```

The complete CI controller target compiled and **75/75 tests passed**, with
zero failures or skips, including all 17 focused tests and both promotion
DevBench variants. Package shader tests passed **163 assertions in one
test case**. The final full controller run supersedes the earlier focused
run for exact-source validation. The existing third-party hde64 and CMake
warnings remain in the logs.

Recorded elapsed times: ON configure 26.95 s,
ON package build 780.28 s, OFF DLL build
602.30 s, controller build 74.28 s,
and controller execution 3.34 s.

CI fixture repair `e74c5c427` resolves the FSR extraction-harness failures.
Tooling repair `3d6cb1232` restores inherited MSVC state after its fake
environment fixture. Fresh and initialized shell probes passed locally;
GitHub run `34619505605` passed its previously failing environment step.
That observation is distinct from a full current-head CI pass: jobs still
running at reporting time are not counted as passed.

The reporter is integrated into local automation `dev` at
`9e3a0a9e5b9f55675a49d825c86c96e437f5e54e`. Four reporting suites and
source/package CLI parity passed. Independent negative checks confirm
that Failure ends owned-promotion attribution, contradictory eligibility
claims are rejected, and every required guard-obligation bit is checked.
Raw counters, exact uint64 identities and full-precision timing data remain
intact; display milliseconds use two decimals. Installed plugin files were
not changed. New runs must select the maintained finalizer explicitly:

```text
--toolkit-root C:/src/skyrim-vr-automation --finalize-candidate <request.json>
```

Durable local receipts under
`artifacts/pr73-owned-release-proof-validation-20260911/` include
`build-receipt.json`, `build-OFF-receipt.json`,
`controllers-OFF-ci-receipt.json`, `controllers-OFF-ci-tests.xml`,
`candidate-production-recipe-comparison.json`,
`owned-release-reporting-followup-validation.json` and
`owned-release-reporting-integration.json`. Scoped whitespace, line-ending
and Markdown hooks passed. Changed C++ ranges passed pinned clang-format
22.1.4; changed CMake blocks passed the pinned formatter. Unrelated legacy
formatting was preserved.

Matched live 66-transition comparisons, real failure/recovery scenarios,
and `csx-render-scale-pr-v1` visual qualification remain **not run** for
this correction. Offline builds do not establish a performance gain or
release readiness, and they add no fabricated runtime rows to the ledger.
