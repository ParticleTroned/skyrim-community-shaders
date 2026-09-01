# PR73 owned-drain parent and controller validation

The FSR controller harness repair is published as `e74c5c427`. The exact
parent of runtime change `269bded15` is now built with matching canonical
build inputs. The live comparison and unattended visual qualification have
not run for this exact pair; no performance or visual pass is claimed.

## CI harness repair

The [failed C++ job](https://github.com/ParticleTroned/skyrim-community-shaders/actions/runs/34583680720/job/103212976458)
built the DLL but rejected the extracted FSR test fixture. Its
`runtimeResult` member hid production locals under `/W4 /WX` (`C4458`,
then `C2220`), and the fixture lacked `InvalidateFSRRelatchDrain` (`C3861`).

The fixture now uses `runtimeDispatchResult` and observes drain
invalidation before host submission. Twelve combinations cover both eyes,
ordinary host and runtime fallback, and successful, failed and faulted
dispatch. Deferred admission retains its proof. Production code and the
six-frame settling guard are unchanged.

The extracted `FSREyeDispatch` target built with MSVC 19.51, Release x64,
C++23, `/EHsc /MP /W4 /WX`. All 12 focused controller tests and required
file-scoped hooks passed. Exact commands, source/header hashes and logs
are retained in
`artifacts/pr73-owned-drain-20260911/ci-fsr-harness/validation.json`.
The [new CI run](https://github.com/ParticleTroned/skyrim-community-shaders/actions/runs/34614517817)
was still running when this repair was recorded; local validation does not
establish a completed remote CI result.

## Drain failure and recovery controller tests

Commit `fa778f63d` adds `VRRelatchDrainController`, executing the production boundary
controller, drain cancellation and readiness predicates, and shared-cleanup
branch. Eight scenario groups cover:

-   Ordinary ownership admission and rejection of recovery/unowned requests.
-   One poll per frame while Pending, and revocation at the six-frame
    deadline while retaining the original conservative retry schedule.
-   Ready results before and at the deadline, exact queued-retry matching,
    and consumption of the early-admission bypass only once.
-   Failed FSR/DLSS polling, device-loss responses, and rejection of owned
    commit after failure.
-   Epoch, source/target generation and recovery cancellation of both
    provider tickets.
-   Device, runtime fence/queue and provider revision changes invalidating
    a previously ready proof.
-   Cleanup backpressure, one completed cleanup per owned operation, and
    fresh cleanup ownership after cancellation.
-   Restoration of boundary scope when downstream execution throws.

Final MSVC configure/build and CTest passed with
`/EHsc /MP /W4 /WX /permissive-`; the CTest has a 15-second timeout. The source remained
unchanged throughout final validation. Exact commands, source hashes,
coverage and exclusions are in
`artifacts/pr73-owned-drain-20260911/drain-controller-validation/result.json`.
An independent read-only review found no blocking issues.

The new test and extractor passed their scoped hooks. The new CMake
registration matches Gersemi output exactly; whole-file Gersemi also
rewrites 50 unrelated baseline regions. Those existing regions were
preserved after verifying that baseline and candidate formatter output
are identical outside the new registration and dependency.
The commit hook normalized CMake line endings. Its whole-file Gersemi
step was skipped only after the separate scoped formatting proof above;
the other applicable commit hooks passed.

Provider polling, device-loss responses and downstream full relatch
execution are scripted dependencies. These tests do not execute real
D3D fence polling, GPU resource lifetime, the full apply implementation,
or post-apply visual settling. They supplement the successful runtime
measurements without substituting for hardware failure qualification.

## Exact compiled pair

| Identity    | Exact parent                                                       | Owned-drain candidate                                              |
| ----------- | ------------------------------------------------------------------ | ------------------------------------------------------------------ |
| Source      | `bc077786db08637eec8b4c3f718e971e58700a60`                         | `269bded159c66f4d8b1a45a836078dfa6cdf3241`                         |
| Build ID    | `0786ca167c161b413ace0cc8ef7d3a76907ef7b36ac4763c8bc3ed9ebb39cfd1` | `9b8b7f17af7ec7012eb194ad945b45756db313352ef6b2ec84d88a50bef9247b` |
| DLL SHA-256 | `47fe6e5f0283ad05d5bd8b9c8ddfe4e22b801dd091bca81050600b9fd2a8e116` | `086b2207bd4b615af5f7a1bb4f567bb0dba8e1c1ec5e5498cd440c623682380a` |
| DLL bytes   | 28,173,824                                                         | 28,184,576                                                         |
| AIO SHA-256 | `98da44a37262cb0d6b3b0101655e4f2679a54800599720ceffb2c36a0aca0526` | `614edd3a3ca1f217103b87d29de2ea95621192608dcc2f6d9e539dd440817e5f` |
| AIO bytes   | 89,057,225                                                         | 89,639,416                                                         |

Both sources are clean. All canonical manifest fields except the source
identity match, including toolchain, compiler hash, Windows SDK,
dependencies, build options and shader-cache ABI. Both use Release,
DevBench ON, SE/AE/VR ON and Tracy OFF. Both packages omit FOMOD and
prebuilt shader caches; automatic deployment is disabled. Matching cache
ABI does not establish an identical warmed runtime cache.

The parent archive is
`build/pr73-parent-bc077786d/dist/CSX_AIO-3.19-VR-mainVR-ef7c366d-PR73-parent-bc077786d-DevBench-no-cache.7z`.
Its receipt is
`build/pr73-parent-bc077786d-evidence-20260911/build-receipt.json`.
The candidate archive remains
`dist/CSX_AIO-3.19-VR-mainVR-ef7c366d-PR73-269bded1-DevBench-no-cache.7z`,
with its original receipt under `artifacts/pr73-owned-drain-20260911`.

Parent validation passed archive integrity, all 435 entries,
staged/archive DLL, PDB and manifest equality, clean source identity and
the canonical input comparison. Its build ran 163 shader assertions in
one test case successfully. Existing warnings remain: hde64 `C4701` and
FidelityFX's deprecated CMake `CMP0116` policy.

The retained `c73bae9a7` control differs from `bc077786d` only in six
documentation files. It supplies equivalent runtime source, but its
different compiled identity cannot substitute for the requested exact
parent. Its earlier measurements remain in the
[existing run comparison](nvidia-renderscale-tuning-pr73-269bded15-20260911.md)
and [canonical ledger](vr-render-scale-ledger.md).

## Live comparison status

The operator confirms the same modlist and identical conditions, and
retains exclusive control of deployment, MO2, Skyrim startup and settings.
This follow-up performed one read-only MO2 inspection and no game,
deployment, profile or runtime mutation. The parent archive is prepared
for the operator's manually started session.

The focused routes are rows 15-20 and 26 in both passes of the retained
33-transition order:

| Row | Route                                 |
| --- | ------------------------------------- |
| 15  | FSR3 HoshiPa to Ultra Quality         |
| 16  | FSR3 Ultra Quality to Quality         |
| 17  | FSR3 Quality to Balanced              |
| 18  | FSR3 Balanced to Performance          |
| 19  | FSR3 Performance to Ultra Performance |
| 20  | FSR3 Ultra Performance to native AA   |
| 26  | DLSS HoshiPa to FSR3 HoshiPa          |

No exact-parent runtime timing or `csx-render-scale-pr-v1` visual result
exists yet. The existing candidate run contains 18 one-frame drains and
does not demonstrate timeout, invalidation or device-loss recovery on the
GPU. The user's condition confirmation is retained separately from live
build, settings, driver and capture evidence. No new runtime timing cells
were added to the ledger by this build and offline-test follow-up.

## Reporter activation

Maintained automation `dev` commit
`c900f176eadcf6bc35cc4c8ffb64e564156fc367` handles the six drain/commit event
names and preserves valid additive names without discarding known timing
evidence. The existing 66-transition run was repaired from its unchanged
journal, with complete retry reporting and all 18 owned-drain intervals.

The installed plugin `0.9.0+codex.20260911050803` still has the older
parser. The installed live worker does not finalize reports. Finalizing
each new run with `--toolkit-root C:/src/skyrim-vr-automation` and
`--finalize-candidate <request.json>` selects the corrected parser and
avoids the gap without reinstalling the plugin. Toolkit selection alone
does not rebuild an existing summary. The local reporting workflow in
`docs/development/vr-render-scale-comparison-reporting.md` preserves source,
raw-input and output hashes; installation and host reload remain separate
from the completed source repair.
