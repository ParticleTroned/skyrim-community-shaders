# main-vr-nr integration: adversarial review

Reviewed NR tip: `5a9be272d19a8b6c34d20615b026b7d21d8f5cf1`.
Imported main-VR source: `1a4262a7d12dc8e11cb5f12b48965744786beaaf`.
Original NR tip: `12d2bf8252693382aa874ce76a466352249e3b5d`.

The review covers the 50 imported main-VR commits, their final call sites,
and the conflict resolutions that combine them with NR. It follows the
[port review](open-shaders-dev-adversarial-review.md) and
[integration record](main-vr-nr-sync-20260927.md). One confirmed test
robustness issue is corrected below. No additional production defect was
confirmed; this is a code review, not an in-game qualification.

## Scope, correctness, robustness, reuse, and completeness

| Surface                          | Review result                                                                                                                                                                                                                                                                                           |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Accepted scope and ancestry      | All 50 incoming commits are ancestors of NR. Rejected or deferred E11-only, EHF, SLF, translation, upstream UI, wind, GO, and Scene Manager changes remain excluded. Visual controls belong to Adaptive Balance.                                                                                        |
| NR preservation                  | Dedicated NR production trees match the original NR tip. All three presets retain their NR settings, contract revision 8, and neutral parallax strength. The primary checkout and 22 pre-existing user files are unchanged.                                                                             |
| Stereo camera and temporal state | Corrected camera-origin reprojection remains inside the exact current/previous NR crop transforms. Successful-crop history, per-eye snapshots, and motion-vector scaling remain connected. DLSS budget warnings use the same success classification for return values and history publication.          |
| Depth and resource ownership     | Shared DLSS consumers use encoded typed depth; FSR retains its required copy path. Preparation checks the encoded resource identity instead of replacing it afterward. NR raw-depth helpers and crop callers remain intact.                                                                             |
| FOV curve and replay             | C++ and HLSL retain the NR 96-byte center-blend layout, with falloff at byte 76. Character-mask and full-image behavior survive. Effective falloff participates in history reset and retained-output keys; disabled and non-VR behavior stays neutral.                                                  |
| Adaptive Balance and materials   | Reviewed serialization, migrations, finite clamps, neutral values, layered controls, buffer layout, shader consumers, and profile persistence. The glare control scales existing engine glare; it does not restore absent glare. Parallax-strength version metadata matches the imported shader change. |
| Runtime and failure paths        | Reviewed weather hooks and localized SE/AE/VR handling, TruePBR/hair gates, shader-cache generation and publication guards, and frame-generation preparation, consumption, and reset behavior.                                                                                                          |
| Packaging and automation         | Optional NR runtime files participate in the shared install-time payload guard before staging reset. New settings retain their DevBench actions, validation, descriptions, and schema. Existing provider and fixture-admission policies remain intact.                                                  |
| DRY and validation               | Shared color, finite-clamp, reprojection, resource-naming, payload, and blend policies remain reused. The remaining duplicated GPU fixture waits are consolidated by the correction below.                                                                                                              |

## P3 corrected: GPU fixture readiness used the production deadline

The previous combined validation intermittently failed
`Left copy must finish before the delayed right copy is queued` in
`character_mask_bounds_gpu_test.cpp`. This assertion prepares the left
eye for a deliberately gated right-eye test. Its use of the production
50 ms readback budget made setup depend on WARP scheduling. Other positive
shader cases and old-marker retirement had the same setup dependency.

Reuse the fixture's existing two-second readiness allowance through one
bounded helper. It checks completion queries and, when supplied, the batch
fence without mapping or consuming staging data. Failed queries, failed
signals, device removal, and readiness timeout fail the fixture explicitly.
After setup, the test still calls the production reader with its unchanged
50 ms budget. Early-category cases now use that same production budget
instead of a two-second read deadline.

Closed GPU gates, pending nonblocking reads, expired shared deadlines,
stale fence values, failed signals, old-copy retirement, and unchanged
destinations retain their assertions. The two-probe fence negative control
still distinguishes a wrongly renewed per-eye deadline. No production
reader, shader, runtime deadline, or NR policy changes in this correction.

## Validation and limits

Commands run from `build/worktrees/main-vr-nr`:

```powershell
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target character_mask_bounds_gpu_test -- /m:1
& 'C:/Program Files/CMake/bin/ctest.exe' --test-dir build/ALL -C Release -R '^character_mask_bounds_shaders$' --repeat until-fail:10 --output-on-failure --no-tests=error --timeout 300 --output-junit ../../../../analysis/main-vr-nr-adversarial-20260927/bounds-repeat.xml
```

The target rebuild passed. All ten consecutive runs passed, with 312 WARP
cases per execution; total CTest wall time was 7.86 seconds. The pinned
clang-format 22.1.4 whole-file check also passed. Evidence is preserved in
the primary repository under
`build/analysis/main-vr-nr-adversarial-20260927/`: `build.log`,
`bounds-repeat.log`, `bounds-repeat.xml`, and `review-audit.json`.
CTest's JUnit file records the final iteration; the console log preserves
all ten results.

Scoped pre-commit checks pass for the test and both review records:
trailing whitespace, line endings, clang-format, and Prettier. YAML and
CMake formatting are skipped because no matching files changed. The exact
hook output is retained in `pre-commit.log`.

The preservation audit confirms no missing main-VR commits, unchanged NR
production code and preset defaults, and all 22 user-file hashes. The DLL
was not rebuilt. Its SHA-256 remains
`b6cbb61bdae86771e8541d8b73d8886faa68479a2cb6c976797059645a9d1f55`
at 31,014,912 bytes, matching the compiled producer in the integration
record. The earlier 202-test and separate 21-test passes remain evidence
for that producer, not new executions in this review. Their original
failed attempts are preserved; this correction addresses the documented
bounds-fixture timing sensitivity without rewriting those results.

Live SE/AE/VR, HMD rendering, NR inference, and render-scale qualification
were not run. No deployment or push was performed. The correction belongs
to `main-vr-nr`; the primary checkout remains on `main-VR`.
