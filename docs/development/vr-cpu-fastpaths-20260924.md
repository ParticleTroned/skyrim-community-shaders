# Production CPU fast paths

The integration carries the three CPU optimizations from `fecd92a75` onto
main-VR `8ade30b8e`, after adversarial review of scope, correctness,
robustness and stability. Main-VR now pins the locally available FidelityFX
`e65b253063`; the unavailable SDK maintenance update remains deferred.
No diagnostic scene logging, shader changes or graphics-quality changes are
included. No production frame-time improvement is claimed without a matched
game comparison.

## Review findings and corrections

No reproducible crash or hang regression was identified in `fecd92a75`.
Review did identify incomplete validation of the material-admission contract:
the existing hook tests mocked the particle and terrain helpers, so a missing
invalidation in their actual implementations could pass those tests.

The integration compiles and tests the actual `CheckParticleLights` and
`OnRenderPassImmediately` bodies. Particle eligibility now runs once in the
caller, which invalidates admission before entering effect configuration.
That configuration helper is private and cannot clear the invalidation flag.
This hardens the callback boundary without adding checks to ordinary draws;
it also removes duplicate pass/geometry/property checks and flag resets.
The original implementation set the flag before `GetMaterial`; no observed
crash is attributed to its previous placement.

The shadow tests additionally exercise 25,000 deterministic mixed operations
against independent expected membership, plus 32 interleaved renderers with
cross-thread destruction and recreation. This extends coverage of intrusive
bucket-list unlinking and positive/negative renderer-cache collisions. The
shadow and adapter production implementations remain identical to `fecd92a75`.

The earlier integration notes incorrectly described the missing SDK pin as
a current build blocker after the main-VR history update. Those notes are
updated here; no dependency substitution is added by this integration.

## Adapter and provider selection

Ordinary VR draws with no pending physical render-target change return
before provider normalization. Drain cleanup and provider/device completion
callbacks remain. Pending changes normalize late-discovered portable boot
profiles before physical application.

Selection queries the existing device-owned adapter cache only when
unresolved DLSS capability needs vendor identity. A different device needs
a fresh identity; failed queries remain retryable. Provider decisions still
use current settings and capabilities. No physical transition, vendor
fallback, resource-lifetime or render-scale policy is removed.

## Shadow ownership

Only occupied buckets participate in pruning and clearing. Empty pooled
buckets remain reusable without adding traversal work. Registration skips
native emptiness probes for buckets without owned records, and duplicates
at the table capacity boundary cannot trigger growth.

Eight thread-local weak renderer entries cache successful lookups and
misses. Registry creation and destruction invalidate the revision, including
negative entries on other threads. Weak references do not retain retired
pools, and cache collisions still compare the full renderer address.

Native completion combines pruning, reader retirement and collection under
one lock. Native drawing and owner destruction remain outside that lock.
Owned pass copies, geometry/property references, duplicate suppression,
registration/reset serialization, allocation-failure handling and nested-reader
protection remain. Cleanup follows native bucket lifetime rather than frame
count or a rendering-range guess. No arbitrary bucket cap is introduced.

## Material admission

The ordinary immediate route reuses successful VR material admission across
read-only particle and terrain routing. Effect processing, terrain depth
reset and caught particle exceptions invalidate it. Second terrain draws
and queued-pass replay always validate afresh. The admitted draw body is
private to Hooks.cpp; validity is never cached across draws or frames.

Future callbacks or state-changing work added to the read-only routing must
invalidate admission before execution. RTTI identity reads retain their
existing read-only contract. SE/AE still bypass the VR material guard;
malformed-pointer/index handling and custom PBR ownership remain unchanged.

## Validation

Ten focused tests passed in Release with MSVC 19.51.36252.0, `/W4 /WX /EHsc`:

-   ShadowBatchSubmissions, ShadowBatchHooks and ShadowBatchInstall.
-   NativeLightingMaterialGuard and RenderPassAdmissionRouting.
-   UpscalingProviderSelectionPolicy and VRVendorRelatchPolicy.
-   FSRRuntimeLifecyclePolicy and ShaderCacheDisablePolicy.
-   NvidiaPipelineContract.

Tests retain one material probe for ordinary admitted draws, fresh checks
after callbacks/replay, unchanged flat-runtime dispatch, lazy adapter query
counts and late capability resolution. Actual hook and routing bodies are
extracted from production source; engine/graphics objects and effect work
are mocked. Tests do not establish real engine concurrency or visual fidelity.

The warmed shadow hook benchmark passed at 64, 512 and 4,096 submissions,
with zero allocations in each measured warmed loop. This checks allocation
reuse, not game performance. Sparse pruning checks occupied-bucket counts,
and duplicate-at-capacity tests verify no allocation.

The focused driver, test results and benchmark output are preserved locally
under `build/cpu-review-20260924/`. The full project registers all ten tests.

The clean implementation commit `40d1dd0470d7fc754fbb3faf762de08c2385257a`
built successfully as a universal SE/AE/VR Release DLL, with DevBench and
Tracy disabled and clean provenance required. The ten tests also passed
through the full project's CMake registration. Its verified producer identity:

-   Build ID: `ee7997037f7091f348c545ea9e73684d2a7d0656d1de2ac7437d696c2b13b249`.
-   DLL SHA-256: `5bf33e7bfb12251669b73023f91aea961aeefdd411f64454a99eb273d192c127`.
-   DLL size: 23,473,664 bytes.
-   DLL and adjacent manifest: `build/cpu-reviewed/Release/`.

`ShadowBatchSubmissions`, `ShadowBatchHooks` and `RenderPassAdmissionRouting`
also passed under MSVC AddressSanitizer with no reported memory errors.
The full-project test results are in `production-ctest.xml`, sanitizer
results in `asan.xml`, and compilation in `build-production.log` under the
evidence directory. The existing `tools/build_provenance.py verify` command
verified the manifest and actual DLL. These records refer to the compiled
implementation commit; subsequent validation documentation changes no code.

Scoped whitespace, clang-format and Prettier checks passed. Gersemi 0.26.1
passed on the changed CMake section using `--line-ranges 1905-1966` and on
the complete extraction script. Whole-file CMake formatting was excluded
because it would rewrite unrelated legacy sections. The build retained the
existing MinHook `hde64.c` C4701 warning and FidelityFX CMP0116 deprecation
warning; neither was introduced by the optimizations.

Long gameplay, cell transitions, save/load, visual comparison and render-scale
runtime qualification have not run. No new runtime measurement ledger is
published, and the unit/benchmark results are not production frame timings.
