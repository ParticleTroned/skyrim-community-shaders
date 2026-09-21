> Historical report, privacy-filtered for this export. Statements about
> current process state and planned work describe the original session.
> See the bundle README for later findings and omitted source files.

# PR65 onward: relevance to the render-batch crash

Recent changes could introduce or expose this failure, but the crash dump
does not attribute it to a PR. Review priority below reflects changed code
and execution timing, not a measured probability or confirmed defect.

## Exact source boundary

The crashed DLL was built from
`5655a7102d02d6ab40b14888a52596c4c9e9f450`, producer Build ID
`fb078514153553f30d89552b2d469ebc41a372a82407cbe817abf15ef3ac664a`.
Physical DLL, manifest and AIO receipt were verified in
`runtime-dll-verification.json`.

Its ancestry includes PR65, PR75, PR66, PR67, PR68, PR69, PR70, PR71,
PR72, PR73 and PR74, followed by the standalone PBR and LLF corrections.
PR75 merged before PR66. Numeric PR ordering is not commit ordering.
The later PR76, PR77 and PR78 C++ changes are absent from this DLL.
That exclusion is specifically a DLL-source conclusion; it is not a fresh
hash inventory of every loose shader or asset provider.

No user source changes, packages or settings were modified during this
audit. The working tree contains unrelated shader/volumetric-lighting work.
Any comparison builds must use isolated checkouts and preserve those edits.

## Candidates and limitations

| Change                             | Relevant behavior                                                                                                               | Assessment                                                                                                                                                                                             |
| ---------------------------------- | ------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| PR73 `cf3834e18`                   | Changes submit serialization, native stereo-boundary service, provider-drain admission and release ownership.                   | First useful isolation target because it changes when resource replacement can execute. No direct release of the damaged accumulator was found in the reviewed changes.                                |
| PR65 `a4cb755c3`                   | Adds stereo submit identity/freshness checks and changes prepared-input reuse, invalidation and fallback decisions.             | Plausible indirect scheduling/path exposure. The accumulator and shadow wrapper implementations were not changed by this PR.                                                                           |
| PR70 `8f89c1f9d`                   | Links render-scale preference to upscaler selection and changes profile-resolution/admission behavior.                          | Could change which transitions occur with otherwise familiar settings; compare effective profiles in any replay. No direct accumulator ownership change identified.                                    |
| PR72 `ef7c366dd`                   | Reorders Skylighting occluder rejection before ancestry traversal; rejects NaN radii; preserves eligible pass emission.         | Close to rendering but a limited predicate/control-flow change. Lower direct lifetime relevance than ownership/scheduling changes.                                                                     |
| PR71 `113811754`                   | Changes shader-pack compatibility, source epochs, shader reuse and deferred cache I/O.                                          | Broad runtime/cache changes, but no accumulator/batch destruction path identified in the inspected diff. Requires compatible per-build caches in an A/B test.                                          |
| PR68 `f5d49d4a2`                   | Consolidates DevBench task admission/cancellation and result delivery.                                                          | Does not introduce the SKSE task queue or worker-affinity rebinding. The qualification COC path uses its separate RunOnMainThread implementation. The affinity log is not evidence of a new PR68 race. |
| PR69 `1afb9eca9`                   | Adds depth-culling recovery telemetry with bounded counters and reset coordination.                                             | No new scene-object ownership identified in the diff. Telemetry can affect timing, which is not proof of causation.                                                                                    |
| PR66 `ff93c9426`, PR75 `bf4ae54a7` | Eye-local encoder bounds and deferred FSR dispatch.                                                                             | Primarily GPU/input and vendor-eye behavior. The recovered first-transition target was DLSS profile K, quality 0, render scale off; no FSR fault was established.                                      |
| PR67 `358069e8f`, PR74 `ff281016e` | Version/build tooling.                                                                                                          | Low direct relevance to scene-object lifetime; build identity remains important for comparisons.                                                                                                       |
| PBR correction `b2021de91`         | Synchronizes material registries and material destruction bookkeeping.                                                          | Separate runtime change after PR73, so a pre-PR73 versus final-build comparison alone cannot blame PR73.                                                                                               |
| LLF correction `5655a7102`         | Activates repaired scene/shadow guards by correcting instruction checks and permitting independent Engine Fixes interior hooks. | Another post-PR73 change worth separating. Guards can change where an existing stale-object failure manifests; no release of the accumulator is added.                                                 |

The complete `FrameAnnotations.cpp`, `Deferred.cpp`,
`RuntimeThreadBinding.cpp` and `RuntimeThreadAffinity.cpp` have no diff
between the pre-PR65 parent and the crashed source. The CommonLib submodule
also has no diff across that boundary. The reviewed wrapper/affinity files
still match the current checkout, despite later unrelated work.

## Evidence against assuming a newly introduced lifetime defect

The parent of PR65 is `e03e4568a7a7043701526f9799e9da399c599bd0`.
Its LightLimitFix source already describes readable stale NiNode children
after cell teardown, reused shadow-camera storage on Windhelm/Dragonsreach
transitions, and stale BSLight/culling objects. Guard history includes
`c2a674fcb` (August 5) and `b829654ea` (August 12), before PR65's September 9
integration. This establishes an older class of lifetime problems, not
the identical `SkyrimVR+0x13480C8` fault signature on the older build.

The SKSE task-queue affinity behavior also predates PR65:
`df0e920f5` and `bd7a1318d` are August 22 changes. Seeing different task
thread IDs in the crash run does not identify a regression in PR68.

The post-load log reports release of 429 displaced engine-target references
at 00:33:59.049. Their container is
`std::vector<winrt::com_ptr<IUnknown>>`, used for graphics resources.
It is not a list of `BSShaderAccumulator` or `BSBatchRenderer` objects.
No direct causal link from that release to the scene-object corruption has
been demonstrated. The fault followed the second COC around 00:34:04.497.

PR73's history includes a failed experimental polling build `cf1616728`.
That run had a different execute access violation during an upscaler switch
and no matching dump. The polling change was withdrawn and the merged PR73
uses the later owned-drain implementation. Its historical failure must not
be treated as proof against the merged code or as this exact crash.
Prior successful upscaler-switch runs also do not prove COC scene-teardown
stability, because they exercise a different protocol.

## Controlled test that can answer attribution

1. Run the same saved game, modlist, exact route and five-second waits on
   the pre-PR65 source `e03e4568a`, preserving a matching DLL, PDB, assets,
   effective settings and compatible shader cache. Repeat full 25-COC runs;
   one successful run is not a clean baseline for an intermittent failure.
2. Compare against the already failing `5655a7102` build. If the identical
   fault occurs before PR65, these PRs are not necessary to produce it;
   they could still change its frequency. If the earlier build remains
   stable across matched repeats, bisect the actual ancestry toward the
   failing source, not PR numbers.
3. A focused PR73 pair is `ef7c366dd` versus `cf3834e18`. Keep the subsequent
   PBR/LLF corrections separate. An exact PR65 pair is `e03e4568a` versus
   `a4cb755c3`. Do not infer either PR's guilt from a comparison that also
   changes several intervening PRs.
4. Match failure signature, not just CTD count. If a suspect interval is
   found, trace creation/release/use of the captured accumulator/batch
   chain to prove the responsible ownership or ordering error.

No regression replay, rollback, build, deployment or live Ghidra session
was performed for this audit. Source history supports the candidate list;
causal attribution and a validated fix remain open.
