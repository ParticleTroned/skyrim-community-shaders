# Open Shaders dev selective review

## Scope and checkpoint

The 2026-09-26 review resumes after the completed
[Open Shaders main review](open-shaders-main-sync.md). That review covered
all 26 first-parent entries after #688 through v2.15.0,
`0db03643c036de42077f8e9fa985e197c9604bb6`, including #729. Independent
parts of #715, #719 and #728 were implemented; #678 Procedural Sun remains
postponed. Earlier rejections and exclusions remain in force.

Initial working branch: `codex/open-shaders-dev-sync-after-2.15`, created
from `main-VR` HEAD `8fd0e7b8bf493ed14042ce5a45797cdb5c85a446`.
The user subsequently directed staying on `main-VR`; the primary worktree
was switched back there without modifying the SE worktrees. Existing user
changes were retained. The refreshed Open Shaders `main`
still points to v2.15.0. The pinned `dev` endpoint is
`af8134814a17073971628f59c132b196939889ce`, containing 56 first-parent
entries after that release. Review those entries in integration order,
oldest first, rather than sorting PR numbers. Inspect constituent changes
inside upstream-sync merges, including #758, for independent local value.
This is selective adaptation and does not establish merge ancestry.

After #738 the user directed that further work use a working branch while
the primary checkout remains on `main-VR` at `202777f6f9f79a99ba3fffa45f00cf02d0efac9b`.
The approved #730 changes were still uncommitted, so no commits needed to
be removed from `main-VR`. They were transferred with matching file hashes
to `codex/pr730-open-shaders-dev-sync`, based on that commit, in
`build/worktrees/pr730-open-shaders-dev-sync`. Subsequent review and ports
continue there. Unrelated primary-worktree changes were retained.

The user then specified that these ports will land atop
`feat/adaptive-balance-color`. Merge `a5e5ff76e` integrates its head
`f3bfe1f24f11a570ce6b74b9f25354a15f0ebe17` into the sync branch, retaining
both histories. During the port, another operation amended Color to
`41e91ef48a730e073480f5024b5ae11115125c99` and fast-forwarded `main-VR`
to it. This sync did not modify or reset either shared branch. A further
merge refreshes the sync branch to the amended Color head, preserving its
sign-aware grading fix and expanded HDR tests alongside #741's controls
and reflection checks. The primary checkout remains on `main-VR`.
The earlier `202777f6f` pin records the user-selected sync starting point;
the unrelated later fast-forward is retained.

Color follow-up `16891e61ccff08631cfafd8cd2e4944c663159ba` corrects the
preset contract marker to revision 5, which the runtime accepts. It is
merged into the sync branch as well. The atmosphere extension is additive:
retain revision 5 while preserving its updated source fingerprint and
regenerating the three compatibility markers. Source hashes identify
settings code changes; they do not require a contract revision bump.
`generate-unified-presets.ps1 -Check` verified all tiers. A JSON comparison
confirmed that only the revision marker changed, all three markers match
`PresetCompatibility::kSettingsContractRevision`, and the atmosphere
fingerprint is retained. Scoped pre-commit and diff checks passed; no
build or compiled/runtime test ran for the merge.

The user decides `i` or `r` for each presented candidate before a port.
Compare actual diffs with current local code, including equivalent or
better implementations, and preserve SE/AE and VR behavior. Inspect mixed
PRs for useful independent parts even when their main feature is excluded.

Exclude E11-only changes, EHF, translations, SLF, Kevdev's shared wind
system and upstream-specific UI. Retain Adaptive Balance instead of
Scene Manager. Grass Optimizations changes remain deferred. Retain the
earlier exclusions for Skyrim 1.7.99 support and upstream repository
housekeeping. Builds, shader compilation and compiled/runtime validation
are deferred until the end of the selected-port exercise, as explicitly
reconfirmed by the user. No push or publication has been requested.

## Initial review

-   #731, `1becfaf57db9a8e59016788a4132dc1c4114def2`,
    `ci(nexus): drop duplicate changelog reduction`: excluded upstream
    publishing housekeeping. Removes the plain-text changelog reduction
    helper and tests and adjusts the Nexus upload workflow; no runtime or
    shader changes.
-   #732, `5035905577f39eaad54766f7a9afe556929e0f0a`,
    `ci(release): promote workflow edits in two phases`: excluded upstream
    release housekeeping. Adds main/dev/hotfix promotion planning, workflow
    redispatch and related tests/documentation. Local release automation has
    a separate main-VR contract; no rendering change is included.

## #733: vanilla sun glare, rejected

[Open Shaders #733](https://github.com/alandtse/open-shaders/pull/733),
`1c0d36c350b7363c1462bde69ea20fe07824a220`, is titled
`fix(sky): restore vanilla sun glare`.

The user selected **r: affected path absent locally**. Its entire diff changes
one preprocessor branch in `package/Shaders/Sky.hlsl` from `#else` to
`#elif !defined(DITHER) || !defined(TEX)`. This prevents OS's extra
`IsSun` scene-depth test from forcing vanilla glare opacity to zero in
the `DITHER` + `TEX` permutation, preserving the engine visibility fade.

Current CSX has neither that fallback depth-test branch nor `IsSun` in
its C++/HLSL extra shader flags. Its sole depth-based alpha override in
this shader is confined to `CLOUD_SHADOWS && CLOUDS && !DEFERRED`.
The local glare paths retain `baseColor.w * input.Color.w` for both VR
and non-VR permutations. Thus this is not a missing local correction,
and there is no independent hunk to port. This conclusion is based on
source inspection, not a new visual or runtime test.

The user rejected #733 after the SE investigation below and clarification
that absence of this bug does not establish working glare in either
runtime. No #733 implementation was made. Runtime verification of the
halo, weather sprites, occlusion and upscaling remains deferred.

## #733 follow-up: earlier SE glare experiments

The user confirms that testers judged the earlier attempted fix not to
work. Treat that attempt as unsuccessful, regardless of successful capture
receipts or diagnostic session names. No new runtime test was performed.

The local `cs-1.7-PL-SE` ref is `734435e158c91438812ff96314645f2c69a8b8db`;
the cached `origin/cs-1.7-PL-SE` ref is
`d438b51738da787f45c49013f5659a0a4326c963`. Both retain the offending
`IsSun` depth-test branch. The directory
`build/worktrees/cs-1.7-PL-SE` is actually checked out on `main-dlss5`
at `adc58f7ee33353c27870c0e2b2d4cdede08da851`, rather than the similarly
named branch. Its nested `Gogh-se`, `Vincent-se` and
`Gogh-se-lensflare-test` worktrees, and the separate
`build/worktrees/os-sky-sync-weather-lens-flare` worktree, also retain
that branch without #733's condition.

In the SE sources, `State::UpdateSkyShaderPermutation` marks both
`SO_SUN` and `SO_SUN_GLARE` as `IsSun`. `Sky.hlsl` then applies the extra
scene-depth comparison to glare and can replace its engine-provided
opacity with zero. The SunGlare shader technique uses `DITHER` + `TEX`.
#733's one-line conditional excludes precisely that permutation from the
fallback depth check, while retaining the check for other permutations.
This is a concrete applicable difference in the SE branch, even without
importing procedural sun or EHF.

The diagnostic worktree is on `diagnostic/gogh-se-lensflare` at
`deed87bae7402e174df0f8788d9721ec3232e2f2`, with substantial uncommitted
changes. Its experiment targets another rendering path:

-   `LensFlareCompatibility.cpp` hooks image-space rendering and replaces
    `BGSLensFlareVisibilityPass`, temporarily locks dynamic resolution and
    rebinds its per-frame buffer. The added `LensFlare.hlsl` adjusts depth
    sample coordinates for dynamic resolution.
-   Classic weather lens-flare draws are redirected to a separate texture
    for composition after DLSS/NR, with a fallback composition path.
-   Shader-cache changes register the visibility pass and resolve its
    original shader source name. Hooks add dispatch-scope/null checks and
    revise the IBLF initialization hook's relocation, write width and return
    contract. These are separate issues, not part of #733.
-   `SkySync.cpp` changes add diagnostics around suppressing/restoring
    weather lens-flare records. They do not remove the sky shader's glare
    depth test. `Sky.hlsl` itself is not among the worktree's modified files.

The weather's `BGSLensFlare` sprites, the sky's `SO_SUN_GLARE` halo and
image-based lens flares are distinct paths. Restoring one does not prove
the others work. Earlier main-VR history also contains a separate March
glare-intensity experiment: `c9b6ec3107acb86789e6bcfa7807d58b7737056a`
removed weather/glare-scale overrides and their occlusion gate. It did
not implement #733's shader condition.

September 2-3 evidence remains under the SE worktree's
`devbench-evidence`. All five post-NR capture receipts report
`inconclusive: true` with native fallback/format/HUD limitations. One
on-confirm image was visually inspected; an off image could not be
decoded by the image tool, so no visual A/B success is claimed. Session
completion does not supersede the testers' failure report. No crash stack
establishing the cause of those earlier failures was located in the
inspected evidence; do not attribute the crashes to the shader condition.

Revised distinction: #733 is a relevant candidate for the old SE branch,
and was absent from its failed diagnostic worktree. It is still not a
direct patch for current `main-VR`, whose shared SE/AE/VR sky shader lacks
the offending fallback entirely. This does not establish that glare works
in current `main-VR`; any remaining symptom needs its own diagnosis. The
user rejected the direct port, and no experiment has been imported.

## Review after #733

-   #734, `43a1629573c40133fcd3d039954e21cb64cca1bc`,
    `fix(fog): preserve sky with vanilla fog enabled`: excluded EHF-only
    correction. Its single condition restricts the EHF-plus-vanilla-fog
    composition branch to geometry depth. Local `ISSAOComposite.hlsl`
    already restricts vanilla fog to `depth < 0.999999` and has no EHF
    composition branch. No independent local change remains.
-   #735, `f537f4b9cf6753015877cecfe840abda41d67ddc`,
    `fix(sun): prevent procedural sun clipping`: deferred with #678.
    Billboard expansion, fixed occlusion-query coverage, radius metadata,
    vertex permutation binding and previous-frame positions serve the
    absent Procedural Sun feature. Without that feature, the current and
    previous input positions are identical; there is no separate motion
    vector fix to extract.
-   #736, `cc7ec97c5ab6704d5f9836548be36ede3093e7ff`,
    `fix(fog): remove double opacity weighting`: excluded EHF-only shader
    correction. Both edits are inside its absent fog helper.
-   #737, `0d5e5893b0e824de3f37bb112f27f8af426d7a7f`,
    `fix(fog): correct height fog on effect meshes`: excluded EHF-only
    correction after inspecting the full conditional context. Both hunks
    change `EXP_HEIGHT_FOG` paths; ordinary effect-fog branches are unchanged.

## #738: sky composition, approved and implemented

[Open Shaders #738](https://github.com/alandtse/open-shaders/pull/738),
`a0eafffe2143464e9fd23099f73789c9dbf38990`, is titled
`fix(ll): correct sky composition` and authored by Dlizzio
`<77717521+Dlizzio@users.noreply.github.com>`.

The user selected **i, adapted partial port**. With Linear Lighting enabled,
compose authored weather tint, sky texture and additive offset before
applying the sky colour transform. Blend cloud textures before that
transform and include the authored horizon multiplier in the composition.

Before this port, `Sky.hlsl` transformed `PParams.yyy`, sampled textures
and vertex colours separately, blended transformed cloud samples, and
added transformed offsets afterward. Local `Color::Sky` applies a
per-channel gamma power; there was no equivalent composition helper.
Although multiplication of
nonnegative colours commutes with that power, addition and interpolation
do not. Thus the useful local difference survives without upstream's
ACEScg, HDR sun, Cloud Relight, E11 or Procedural Sun paths. This is a
source-level correctness rationale, not measured visual validation.

The implementation adds a pixel-shader-local `ComposeSkyColor` helper and
uses `Color::UseLinearLightingColorAdjustments` to select authored
composition. LL keeps sampled textures and `PParams.yyy` in their authored
form until texture interpolation, tint multiplication and offset addition
are complete. The horizon factor is likewise included before adjustment.
The moon mask still receives one sky adjustment, and its alpha test is
unchanged. No shared colour function or shader-buffer layout changes.

The non-LL path retains its separately adjusted textures, tint and offset,
including Adaptive Balance adjustments. Final brightness/saturation,
SE/AE's centred dither and VR's dither-free path remain. The vertex shader,
stereo coordinates, alpha/occlusion behavior and motion vectors are
unchanged. Excluded/absent feature handling is not imported. LL is disabled
by default, so the correction applies when explicitly enabled.

This is a colour-composition change; it does not establish working glare
visibility or resolve the deferred runtime verification from #733.

The review observed `main-VR` at
`4730e3029fa5e116e8d22741ef31866cedbaa66b` after another local change to
the shader-include test; that unrelated work was retained. After #738,
resume with #730, `fd6350ca7e82a52a8f578c8aaa62def53f051f72`,
`fix(cache): handle shared bytecode per variant`.

### Validation for #738

-   Source review checked textured and untextured sky, cloud interpolation,
    sun glare, moon masks and horizon fade with LL enabled and disabled.
    Reviewed both VR and non-VR branches and retained the local dither and
    Adaptive Balance contracts.
-   `pwsh ./tools/git.ps1 diff --check -- package/Shaders/Sky.hlsl`: passed.
-   `pwsh ./tools/pre-commit.ps1 run --files package/Shaders/Sky.hlsl docs/development/open-shaders-dev-sync.md`:
    passed whitespace, line-ending, clang-format and Markdown checks;
    YAML and CMake hooks skipped because no matching files changed.
-   C++ builds, shader compilation, compiled tests and runtime visual checks
    are deferred until the end by user instruction. No DXBC equivalence,
    visual improvement or runtime pass is claimed.

## #730: shared bytecode per variant, accepted partial port

[Open Shaders #730](https://github.com/alandtse/open-shaders/pull/730),
`fd6350ca7e82a52a8f578c8aaa62def53f051f72`, is titled
`fix(cache): handle shared bytecode per variant`.

User decision: **i, adapted partial port**. `GetShaderString(..., true)`
intentionally excludes the descriptor, allowing variants with identical
defines to share compiled bytecode. Runtime shader maps and task IDs retain
the descriptor. Baseline `CompilationSet::Add` nonetheless refused a task
when `GetCompletedShader(task)` finds shared bytecode. A runtime-map miss
can therefore remain unresolved even though the existing worker path can
reuse that bytecode to construct the missing descriptor's shader object.

Baseline `capturedShaders` and `clearedThisCaptureCycle` also used the shared
string key. Capturing multiple descriptors with that key retains only one
descriptor and disk path, leaving other captured runtime variants out of
the scoped clear. Preserve each full task identity, release each eligible
runtime variant, and invalidate shared bytecode once per capture cycle.
Adapt the pending-state check and invalidation atomically to avoid erasing
a concurrent compilation claim.

The upstream tracking-outside-developer-mode correction is already covered
for vertex, pixel and compute shaders. Local tracking additionally keeps
normal captures out of the persistent developer map. Retain that behavior,
render-thread capture rebinding, per-task in-flight checks, generation and
deferred-eviction protection, and synchronized managed/disk invalidation.
These safeguards make a wholesale replacement of the cache files unsuitable.

The source-level failure paths are present in the shared SE/AE/VR cache;
no runtime reproduction or performance measurement has been performed.

Implemented queue admission without the shared-bytecode veto, retaining
queued/in-progress/processed task deduplication and generation assignment.
Capture records are keyed by full task identity and initialized from each
observed descriptor, including in developer mode where the persistent
display record may describe a different variant. Normal gameplay still
does not populate that persistent map.

Scoped clearing releases each eligible captured runtime variant and
forgets its task ID, while tracking shared-bytecode invalidation separately
across both capture windows. A new cycle resets both sets. The pending
check and bytecode erase share one map lock; active per-task workers and
deferred hot-reload evictions are skipped. Runtime resource release is
separate from shared bytecode removal, and the existing synchronized disk
and managed-pack invalidation path remains unchanged. No settings or
DevBench contract changes are introduced.

Added `ShaderCacheVariants` controller coverage extracting the production
task declarations/identity, queue admission, capture state/tracking and
eviction methods. Engine/D3D resources, disk deletion and scheduler
completion boundaries are stubbed. Cases cover shared-bytecode queue
admission, task deduplication and generation, multiple captured descriptors
in both developer modes, off-thread exclusion, both capture windows,
per-cycle bytecode invalidation, pending/in-flight/deferred protection,
shader stage/type identity, per-variant disk paths and feature clear scope.
These tests do not establish actual D3D creation or concurrency safety.

The next entry is #635, `405b59488fb8cfdb52a489047cb6fc2afe42f646`,
`feat(fog): match vanilla weather visibility`.

### Validation for #730

-   Reviewed the shared SE/AE/VR queue, `ClaimCompilation` cache-hit path,
    vertex/pixel/compute runtime insertion, scoped clearing and disk-cache
    invalidation. No runtime-specific code or shader resources were added.
-   `pwsh ./tools/cmake.ps1 -D PROJECT_ROOT=. -D OUTPUT_DIRECTORY=../../analysis/open-shaders-dev-review-20260926/pr730-extracted -P tests/extract_shader_cache_variants.cmake`:
    passed, producing all nine source headers; no configure or compilation. Two earlier
    invocations using combined `-Dname=C:/...` arguments were rejected
    because PowerShell split the drive-qualified values.
-   `pwsh ./tools/git.ps1 diff --cached --check`: passed.
-   Scoped pre-commit checks passed whitespace, line endings, clang-format
    and Markdown checks. The first full invocation failed because Gersemi
    reformatted existing unrelated root CMake code; those edits were
    discarded, keeping only the one test-registration include. The rerun
    used `SKIP=gersemi` for that baseline limitation. The two new CMake
    scripts separately passed installed Gersemi `--check`, with warnings
    that it does not recognize the repository's custom CMake functions.
-   Builds, compiled controller tests, shader compilation and SE/AE/VR
    runtime validation remain deferred until the end by user instruction.

## Review after #730

-   #635, `405b59488fb8cfdb52a489047cb6fc2afe42f646`,
    `feat(fog): match vanilla weather visibility`: excluded EHF. Shared
    shader changes add EHF weather parameters or adjust EHF-enabled
    composition; the ordinary vanilla-fog branches remain unchanged.
    Its generic control-discovery improvements belong to the upstream
    Scene Manager settings-catalog generator, which this branch does not
    use. No independent local port identified.
-   #744, `175f2f5343e77c3cb754b24fd1ecd1e5d6246731`,
    `refactor(ui): organize upscaling settings in tabs`: excluded upstream
    UI/translation work. Changes are confined to settings drawing methods
    and their declarations, plus translated labels; no rendering/backend
    implementation changes.

### #742: S3D rock texture exclusions, rejected

[Open Shaders #742](https://github.com/alandtse/open-shaders/pull/742),
`90650df7b73e45b0bd21a1a9ee65ae3a0ef2893f`, is titled
`fix(terrain): blacklist S3D rock textures`.

Recommend **r**. Its only change adds `pbr/landscape/trees/`,
`landscape/mountains/s3drocks/` and `pbr/landscape/mountains/s3drocks/`
to the default JSON exclusions for the #727 mesh-texture rule loader.
The user rejected that loader earlier, and this branch has neither it nor
the JSON file. Importing the file alone would have no effect.

Current `TerrainVariation::DataLoaded` gathers actual landscape diffuse
textures and seasonal swaps; `IsLandscapeDiffusePath` requires membership
when those records are available. `UpdateMeshPermutation` additionally
rejects tree/foliage/material cases. This avoids upstream's broad automatic
mountain-directory admission in normal operation. It is not the same as an
explicit S3D blacklist: when landscape records are unavailable, local
directory fallback can still admit an S3D path. No local failure requiring
that named exception has been demonstrated, so retain the existing #727
decision instead of adding mod-specific rules preemptively.

User decision: **r**. No #742 code has been implemented.

### #740: Scene Manager feature availability, excluded

`ff75ed18411c3a97fce5bad0efe4203d070e87c4`,
`chore(scene-manager): update feature availability`, only changes Scene
Manager availability rules, nested feature UI disabling, and associated
tests. No independent renderer correction is present. Retain Adaptive
Balance and exclude this PR under the user's Scene Manager/UI rules.

### #741: atmosphere controls, accepted adapted port

[Open Shaders #741](https://github.com/alandtse/open-shaders/pull/741),
`47f5e45630f5b5ea62cead6865bbc5ccdeddf720`, is titled
`feat(utility): expand atmosphere controls`.

User decision: **i, adapted partial port into Adaptive Balance**.
Implementation commit: `c0dce9cc4`; the subsequent Color-base merge retains
the updated Color grading fix and tests.
The shader changes add cloud-specific brightness, saturation and gamma;
vanilla fog opacity scaling; sky-static effect brightness/transparency;
and a sun-glare intensity multiplier. These have independent uses in
current SE/AE and VR shaders without EHF or Scene Manager.

Code-level comparison at working-branch HEAD `35743a486`:

-   `Sky.hlsl` and `Color::Sky` currently apply the same sky brightness,
    saturation and gamma to clouds and other sky passes. The existing
    cloud permutations already expose `CLOUDS`; separate cloud controls
    are missing. Keep #738's authored composition and the VR/non-VR
    dither behavior while separating the adjustments.
-   `Color::FogAlpha` currently exposes a gamma curve through LL/Adaptive
    Balance, but has no opacity multiplier. Gamma reshaping is not an
    equivalent independent strength control, especially at full opacity.
    Its callers cover opaque composite fog, effects, lighting and water.
-   `Effect.hlsl` has no dedicated sky-static brightness/transparency
    controls. The upstream effect-permutation/GrayscaleToAlpha predicate
    can be adapted without importing its shadow-relighting call. Preserve
    local effect multipliers, alpha testing, motion-vector outputs and
    additive/multiplicative blend behavior.
-   The sun-glare technique is already identified as `DITHER` plus `TEX`
    in `ShaderCache.cpp`. A multiplier can be applied to both local sky
    branches, including the separate VR path. It only scales glare that
    is already drawn; it cannot restore missing glare, fix weather lens
    flare visibility or change the rejected #733 outcome.
-   Local Volumetric Lighting already offers `ShaftIntensity`, `Opacity`,
    saturation and custom colour through its runtime godray profile.
    `ShaftIntensity` scales a copied engine descriptor before rendering;
    upstream `vlIntensity` instead scales the final gamma-adjusted shader
    result. These are not mathematically identical under nonlinear gamma,
    but no missing brightness-control capability justifies a second
    competing control. Retain the existing local implementation.

Implemented port scope: cloud brightness/saturation/gamma, vanilla fog
intensity, sky-static brightness/transparency and sun-glare intensity,
integrated into the existing Adaptive Balance global/profile/location
composition and DevBench interface. Do not import upstream CS Utility
ownership, page/override UI, translations, the EHF fog-gamma exception,
Scene Manager composition or the additional VL multiplier. Local CS Utility
remains responsible for DOF utilities. Existing saved sky/cloud appearance
is preserved when new cloud fields are absent, and disabling Adaptive
Balance restores neutral outputs. Settings boundaries and the DevBench
schema cover every new control. C++/HLSL layouts are synchronized: Color
keeps offsets 40/44; six appended atmosphere values extend Adaptive Balance
to 80 bytes. Cloud gamma uses Linear Lighting padding at offset 104.

The upstream PR description was verified through GitHub CLI and its full
non-translation code diff was compared with the local sources. The port
preserves Color grading and composition from `feat/adaptive-balance-color`,
#738's sky composition, VR's separate sky path, SE/AE dithering, preview
exclusion, and the local effect alpha/gamma and blend rules. Transparency
composes through remaining opacity so neutral layers do not erase a fade.

Cloud migration runs before root and feature-scoped settings merges and
on direct profile imports. It copies only absent cloud fields from numeric
sky values; explicitly saved cloud values win. The feature shader version
advances to 1-11-0 for the changed shared buffer contract. Unified preset
compatibility metadata is refreshed without retuning the three presets.
The Color follow-up retains runtime-compatible contract revision 5 for
these additive settings; the atmosphere source fingerprint remains updated.

Regression coverage is added to the extracted production Adaptive Balance
test for atmosphere composition, migration and DevBench validation. Existing
Color and ambient reflection tests are updated for the expanded layout.
No build, compiled test, shader compilation, deployment or runtime validation
has run for this port. Glare visibility remains unverified in SE/AE/VR.

Source validation for #741:

-   `python tests/extract_adaptive_balance_toggle.py --source-dir . --output-dir ../../analysis/open-shaders-dev-review-20260926/pr741-extracted`
    passed; generated the production-code test inputs without compiling them.
-   `pwsh ./tools/generate-unified-presets.ps1 -Check` verified all three tiers.
    A JSON comparison with the parent commit confirmed that only their
    `Preset Compatibility` metadata changed.
-   Python parsing of the registered DevBench descriptor confirmed all 17
    numeric bounds match the production validator, with the separate boolean
    lighting gate and complete setter coverage.
-   Scoped pre-commit source checks and `git diff --check` passed. Root CMake Gersemi is skipped for the previously documented
    unrelated baseline formatting; its edited test block passed Gersemi separately, with the expected warning
    for the custom `add_controller_test` command.
-   Builds and compiled/runtime validation remain deferred, including the
    new regression cases and production SE/AE/VR shader permutations.

### #745: Light Limit Fix UI, excluded

`76e0b2c348f301103b35aebb3f92fc1055381622`,
`refactor(llf): reorganize feature UI`, reorganizes LLF/SLF settings into
tabs, adjusts shadow tables and budget bars, and scales overlay dimensions.
The `ShadowRenderer.cpp` changes are confined to `DrawOverlay`: window
placement, resizing constraints, and the collapsed-window Begin/End path.
No shadow rendering or independent lighting correction is included.
Excluded under the upstream-specific UI, SLF and translation rules.

### #743: RTTI exception recovery, accepted

Implementation commit: `7f20a64d7`.

[Open Shaders #743](https://github.com/alandtse/open-shaders/pull/743),
`568888306be0c15f8b6db8df821014e19612e1d9`, is titled
`fix(llf): allow RTTI exception recovery`.

User decision: **i**. Removed `noexcept` from the three shared
point-light classification helpers in `src/Utils/PointLightFlags.h`,
matching the complete upstream code change. The underlying
CommonLib `skyrim_cast` calls engine RTTI and is not declared `noexcept`.
An escaping C++ exception must not be converted into termination before
reaching the existing caller recovery boundary.

The local strict-light loop calls `SetEngineLightFlags`, which delegates
to `SetPointLightTypeFlags`, inside the existing MSVC `__try`/`__except`
boundary; recovery clears strict-light data. Its retained-light snapshot
reduces lifetime hazards but does not replace the exception contract.
Adaptive Balance also uses `GetVanillaPointLightFlags` when LLF is not
providing classification. This is shared SE/AE/VR code, independent of SLF,
E11 and UI. Preserve the nonthrowing bit-mask helpers and existing recovery
behavior; remove only the three incorrect exception specifications.

Verified the PR body through GitHub CLI and compared the complete header
diff and both local consumers. Only the three declarations change;
nonthrowing bit-mask helpers, light flags and both consumers are retained.
Scoped pre-commit and `git diff --check` passed for this port. Builds, compiled tests and runtime validation remain deferred by the
user. No exception-recovery runtime result is claimed.

### #746: stale scene-light recovery, accepted Adaptive Balance port

[Open Shaders #746](https://github.com/alandtse/open-shaders/pull/746),
`d24e23ada4097cd1ec111ba387de5c9ec8244ad9`, is titled
`fix(csutility): recover from stale scene lights`.

User decision: **i, adapted into Adaptive Balance**. Its only upstream change guards
the vanilla point-light classification loop with MSVC structured exception
handling. Locally, the matching function is
`AdaptiveBrightness::UpdateVanillaPointLightData`: before the port it
dereferenced raw scene-light entries and called the RTTI helper without
a recovery guard.
The Lighting hook calls it when LLF is unloaded; the Water hook also uses
it. LLF's retained-light snapshot and strict-light recovery do not protect
this separate loop. The change is applicable to shared SE/AE/VR code and
is independent of E11, SLF, upstream UI and Scene Manager.

The guard now covers both call sites in that common callee, including
the raw light dereference before RTTI. It stops on the first fault and
uploads neutral zero classification flags, preserving the existing local
Inverse Square Lighting enabled-state mask, count bounds, registers and
buffer update/binding. Upstream retains successfully classified prefix
entries because it initializes the buffer only before the loop; the local
adaptation clears the whole classification buffer on recovery, as
local strict-light recovery already does for its own data. Zero is the
existing shader fallback when classification data is unavailable.

Recovery logs one warning per process through an atomic gate, without
flooding the render log on repeated faults. The first failed read aborts
the loop; buffer upload and binding still run afterward. No scene-light
ownership, API contract, shader, Color grading or render-scale code changes.
The source fingerprint for generated presets is refreshed because the
settings-owner inventory includes this implementation file; compatible
revision 5 and every graphics setting remain unchanged.

Added `AdaptiveBalancePointLights`, an MSVC controller test that extracts
the actual production method, buffer layout and limits. Its fixture injects
an access-violation exception during the light read and a C++ exception
during classification after one successful entry. It checks full-batch
clearing, stopping before later entries, upload/binding to both registers,
next-call recovery, bounded warnings, ISL masking, count bounds and empty
inputs. Engine classification and D3D upload are simulated; this is not an
in-game RTTI or rendering test. Both Adaptive Balance test targets share a
single extraction dependency to avoid concurrent generation of headers.

Validation:

-   `python tests/extract_adaptive_balance_toggle.py --source-dir . --output-dir ../../analysis/open-shaders-dev-review-20260926/pr746-extracted`
    passed, producing both existing and new test headers without compiling.
-   `pwsh ./tools/generate-unified-presets.ps1 -Check` passed for all tiers.
    A JSON comparison with the parent confirmed that only their settings-source
    fingerprint changed, and every marker still matches runtime revision 5.
-   Scoped pre-commit and `git diff --check` passed. Root CMake Gersemi remains
    skipped for the previously documented unrelated baseline formatting;
    the edited test block passed Gersemi separately, with the expected warning
    for the custom `add_controller_test` command.
-   The new `AdaptiveBalancePointLights` compiled fault-injection test, DLL
    build and SE/AE/VR runtime validation remain deferred to the end of the
    sync by user instruction. No recovery test execution is claimed.

### #748: Effects11 location crash, excluded

`8253d0a4cc7238185a1901f174a091dab5182019`,
`fix(effects11): avoid null-cell location crash`, changes only
`Effects11/ENBHelper.cpp`. It guards the E11 location cache's
`GetCurrentLocation()` call with a parent-cell check and clears that cache
when the cell is absent. No shared utility or independent non-E11 change
is present. Excluded under the E11-only rule.

### #747: DLSS-G buffer count, rejected

[Open Shaders #747](https://github.com/alandtse/open-shaders/pull/747),
`bf52e305a62168f00ef54506ca51f28fac71c109`, is titled
`fix(upscaling): scale DLSS-G buffers to multiplier`.

User decision: **r**. It sizes the direct DLSS-G swap chain and allocator/fence
arrays for multi-frame generation, then allows resizing that chain with
its live buffer count. The local branch has no `CreateSwapChainDirect`,
`useDLSSG`, cached DLSS-G frame multiplier or Streamline DLSS-G feature
binding. Its DX12 swap chain is the FidelityFX provider's two-buffer path.
The upstream FidelityFX path also retains two buffers, so widening local
arrays or relaxing their count contract adds no applicable correction.

The mixed resize changes were inspected separately: local
`ResolveBackendBufferCount` already translates the public one-buffer
contract (including zero/preserve requests) into two backend buffers.
`ResizeBuffers` and `ResizeBuffers1` share that policy, restore buffers and
frame-generation context on failure, and refresh them after success.
Upstream's direct-DLSS-G count check would not preserve that local proxy
contract. No independent resize fix from this PR is missing locally.

Verified the full two-file diff, GitHub PR description, local creation and
resize paths, and the absence of direct DLSS-G bindings. No #747 code has
been implemented.

### #749: unsupported scene controls, excluded

[Open Shaders #749](https://github.com/alandtse/open-shaders/pull/749),
`91b07ad39f6c513a6b173d64274ede31d7c14418`, is titled
`feat(scene): gray out unsupported controls`.

The catalog generator identifies navigation checkboxes; scene-editing UI
hooks distinguish those controls from settings and disable unsupported or
ambiguous controls. The policy change permits the Wind Tree Meshes setting
in Scene Manager. The remaining changes test that catalog and UI policy.
Excluded under the Scene Manager, upstream-specific UI and wind rules.
There is no independent renderer or Adaptive Balance correction to port.

### #754: improved grass transparency, recommendation awaiting decision

[Open Shaders #754](https://github.com/alandtse/open-shaders/pull/754),
`faa83083e7d18be1808bd1af96b26d0a1683fa64`, is titled
`feat(grass): add improved transparency`.

Recommend **r**. It adds hashed alpha coverage to reduce blocky distant
grass, with a default-enabled Grass Lighting option. The shader uses
position derivatives and texture mip level to vary the alpha threshold
consistently across depth and color passes. Its changes cover ordinary
grass and VR as well as GO, so this is not a GO-only exclusion.

Local Grass Lighting has no alpha-coverage setting, helper or position
interpolator; the grass shader still uses hard alpha rejection. This
feature is therefore absent, rather than already implemented or replaced
by a demonstrated superior equivalent. A port would need to respect the
different local settings layout and enabled-state contract.

However, upstream explicitly reverted the feature in
[Open Shaders #757](https://github.com/alandtse/open-shaders/pull/757),
`f74c58d42d4ff283409a847f3a780b99e870c3f0`, later in the same pinned range.
An exact comparison of added/deleted lines confirms that the revert
reverses every changed line across all 14 files. The pinned `dev` endpoint
contains neither `EnableAlphaCoverage` nor `GrassAlphaCoverage`. The
intervening #752 GO projection change remains separate and is deferred
under the GO rule; the whole grass file is not otherwise unchanged.

The revert's description gives no reason beyond reverting #754, so no
particular crash, visual defect or performance regression is inferred.
Rejecting avoids reintroducing a feature removed from the upstream target;
no independent fix outside the alpha-coverage feature was found in #754.
No code was implemented or built. Await the user's `i` or `r`.

## Verification

-   Refreshed only Open Shaders `dev` and `main` with `--no-tags`; pinned
    the resulting review range and compared #733's complete shader diff.
-   Read #733 metadata through the GitHub CLI and checked local sky shader
    permutations and both extra-flag definitions.
-   `pwsh ./tools/dev-doctor.ps1 -Network`: zero failures after running
    outside the restricted sandbox; one existing public-HTTPS remote warning.
    Remote configuration was retained.
-   No build, shader compilation, deployment or runtime validation ran.
