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

### #754: improved grass transparency, rejected

[Open Shaders #754](https://github.com/alandtse/open-shaders/pull/754),
`faa83083e7d18be1808bd1af96b26d0a1683fa64`, is titled
`feat(grass): add improved transparency`.

User decision: **r**. It adds hashed alpha coverage to reduce blocky distant
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
No code was implemented or built.

### #753: distance haze controls, excluded

[Open Shaders #753](https://github.com/alandtse/open-shaders/pull/753),
`2cedaa9783abcafe226b77a7ae45fc6cd1f8d366`, is titled
`feat(fog): add distance haze controls`.

Adds horizontal distance haze to EHF, including bounded opacity and
distance settings, shader blending, weather-color handling and fog-history
invalidation. The shared HLSL edit changes only the EHF settings layout.
The CSUtility fog-intensity and IBL color changes are consumers inside EHF;
neither modifies an independent Adaptive Balance or vanilla fog path.
All other changes are EHF settings/UI and translations. Excluded under the
EHF rule after inspecting the nontranslation diff.

### #755: frame-generation input readiness, accepted SE/AE port

[Open Shaders #755](https://github.com/alandtse/open-shaders/pull/755),
`6cd47558880cc94247e77c61c4b2fa0a4cdead42`, is titled
`fix(framegen): prevent loading transition flash`.

User decision: **i, adapted to the local SE/AE FidelityFX path**. The upstream
description identifies previous-scene LOD models flashing as loading ends.
The fix separates permission to prepare frame-generation inputs from a
latched successful-preparation result, retains that result through
presentation, and clears it when the frame's wrapped buffers are cleared.
Both required shaders must be available before any input copy begins.

Before this port, local `ShouldUseFrameGenerationThisFrame` recomputed settings, pause
and main/loading-menu state at both post-processing and presentation.
It also has stronger local runtime-ready and Reflex-quarantine gates.
Those gates do not establish that the current frame's depth and motion
inputs were prepared: a menu closing between the two calls can change
the decision from false to true without a new copy.

Local `CopySharedD3D12Resources` already checked both shaders, but returned
`void` and copies motion vectors before that check. Missing shaders can
therefore leave stale depth without preventing the later FidelityFX
prepare dispatch. No prepared-input latch or equivalent freshness check
was found in the copy, caller, swap-chain or FidelityFX presentation path.
The local provider's failure containment handles API errors, not this
missing current-frame input contract.

The port separates `ShouldPrepareFrameGeneration` from the latched
`ShouldUseFrameGenerationThisFrame`. `PrepareFrameGenerationInputs` first
invalidates the old result and publishes success only after both copies.
The copy checks required resources and both shaders before moving either
input. Presentation keeps the prepared frame's menu decision while still
checking the local runtime, enabled setting, provider readiness and Reflex
quarantine. Normal base-frame and UI presentation remain unchanged.

All four post-processing preparation sites use that helper; the hook also
invalidates readiness before its earliest possible return. Present and
Present1 share a scope-exit invalidation guard, so successful, failed and
exceptional exits cannot leave consumed inputs ready. Test presents and
retryable presents retain the pending inputs and existing UI contents.
The guard runs after the frame limiter, retaining its current-frame rate
decision. Resource reset/recreation and the shared FidelityFX context-reset
boundary invalidate readiness before mutation; the latter covers both
resize entry points and provider teardown. There is no local
`ClearWrappedBuffers` method to copy from upstream.

The Streamline Reflex change was not ported: it supports upstream's DLSS-G
path, whereas local FidelityFX frame generation deliberately disables
Reflex and quarantines generation if disabling fails. Preserve that policy.
`IsFrameGenerationDx12PathActive` explicitly excludes VR, so this port
benefits SE/AE on the main-VR codebase without enabling VR frame generation
or changing the VR render-scale and compositor paths.

Added `FrameGenerationInputs`, which extracts the actual production copy,
preparation and decision methods, readiness member, and both Present
entry points with their shared implementation. Simulated engine, D3D and
provider boundaries cover loading/pause transitions, allow-in-menu settings,
missing shaders/resources, all live safety gates including VR, input
consumption, test/retryable presents, frame-limiter ordering, provider and
present failures, exceptions, and Reflex-disable failure. These are
controller fixtures, not a real GPU or in-game visual test.

The settings-owner source fingerprint and three generated preset markers
are refreshed; compatible revision 5 and all graphics settings are retained.

Validation:

-   `python tests/extract_frame_generation_inputs.py --source-dir . --output-dir ../../analysis/open-shaders-dev-review-20260926/pr755-extracted`
    passed without compiling. A separate source audit confirmed all four
    preparation sites, invalidation before the early post-processing return,
    resource reset/recreation, and both resize paths' shared context reset.
-   `pwsh ./tools/generate-unified-presets.ps1 -Check` passed for all tiers.
    JSON comparison with the parent confirmed that the only preset change
    is `Preset Compatibility/settingsContract/sourceTreeSha256`; revision 5
    and every graphics setting are unchanged.
-   Scoped pre-commit and `git diff --check` passed. Root CMake Gersemi is
    skipped for the previously documented unrelated baseline formatting;
    the added test block passes separately with its enclosing indentation,
    with the expected warning for the custom `add_controller_test` command.
-   `FrameGenerationInputs` execution, DLL builds and SE/AE/VR runtime
    validation remain deferred by user instruction. No claim is made that
    the loading flash has been reproduced or visually verified locally.

### #752: GO projection and buffer limits, deferred

[Open Shaders #752](https://github.com/alandtse/open-shaders/pull/752),
`01b4a5bd5d65c58a113c25feae1c94dc6ca90f4b`, is titled
`fix(go): correct projection and buffer limits`.

Separates nominal-pixel Hi-Z projection from render-pixel LOD thresholds,
corrects flat GO vertex projection, and bounds GO bucket allocations using
their largest per-eye record. The shared `RunGrass.hlsl` hunk is inside
`GRASS_OPTIMIZATIONS`; it does not change the ordinary grass path. All other
changes are GO code, buffer layout and feature version. Deferred under
the GO rule, with no independent non-GO hunk to port.

### #750: forced-weather sky refresh, accepted SE/AE/VR port

[Open Shaders #750](https://github.com/alandtse/open-shaders/pull/750),
`3e56f31abe16cc3e62cf17e94f5e8aca5a6b7654`, is titled
`fix(weather): refresh sky when forcing weather`.

User decision: **i, adapted to local weather controls and hooks**. Its shared
renderer correction clears cached cloud render passes after `ForceWeather`
resets blending, and releases the outgoing attached or pending aurora/sky
model so the newly selected weather can load its own. It is useful outside
upstream UI and Scene Manager: local WeatherPicker, weather API preview,
editor refresh and lock enforcement all force weather.

The local paths previously called the engine or its lock-hook trampoline
directly. No matching cloud-pass invalidation or model-handle release
existed in those paths or `Utils/Game.cpp`. The existing lock guards,
batched editor refresh and weather-service owner-thread checks solve
different problems and remain intact.

`EditorWindow::ForceWeather` now centralizes the native call and refresh.
It preserves the active lock and uses the saved entry/trampoline when
available, avoiding re-entry through the detour and duplicate cleanup.
WeatherPicker instant selection, API preview, editor refresh, lock repair
and the external ForceWeather hook use that boundary. Ordinary blended
`SetWeather` transitions retain their previous behavior.

`Util::RefreshForcedWeatherSky` detaches the outgoing aurora root, releases
its attached or pending model request, and invalidates each cloud's sky
shader render passes. Null sky, root, cloud and shader-property cases are
guarded. AE clears the handle and releases its entry through ID 15443;
SE/VR use the engine's resetting release through ID 25746. The local
CommonLib handle has no public releasing reset method.

The loader and user requirements now require **VR Address Library 0.269.0**,
up from 0.207.0. The published
[0.269.0 release](https://github.com/alandtse/skyrim_vr_address_library/releases/tag/v0.269.0)
includes the reset mapping from
[address-library #226](https://github.com/alandtse/skyrim_vr_address_library/pull/226).
Its database change maps ID 25746 from SE `0x1403b8d30` to VR `0x1403c8b50`,
with matching handle replacement/release logic in the published analysis.
This verifies the dependency and mapping, not local binary execution.

The existing DevBench weather action and its preview schema description
now document the refresh; field names, enums and ABI remain unchanged.
No Scene Manager, upstream layout or translation changes were ported.
The settings-owner fingerprint and three generated preset markers are
refreshed while retaining compatible revision 5 and every graphics setting.

Added `ForcedWeather`, extracting the production model-release, refresh,
force-routing and lock-hook methods. Simulated engine boundaries cover
SE/AE/VR release selection, attached/pending/absent models, first and last
cloud layers, null and unrelated properties, pre-install and failed-install
fallbacks, installed hooks, direct and console calls, active locks, drift
repair and unchanged normal weather transitions. These fixtures do not
validate real engine ABI, GPU rendering or in-game visual behavior.

Validation:

-   `python tests/extract_forced_weather.py --source-dir . --output-dir ../../analysis/open-shaders-dev-review-20260926/pr750-extracted`
    passed without compiling. Source inspection confirmed that all three
    direct callers use the common force boundary.
-   The DevBench descriptor parses as JSON. Comparing it with the parent
    after removing description fields confirms no structural schema change.
-   `pwsh ./tools/generate-unified-presets.ps1 -Check` passed for all tiers.
    JSON comparison with the parent confirms the sole preset difference is
    `Preset Compatibility/settingsContract/sourceTreeSha256`.
-   Scoped pre-commit and `git diff --check` passed. Root CMake Gersemi is
    skipped for the previously documented unrelated baseline formatting;
    the new test block passes an isolated Gersemi check with its enclosing
    indentation and the expected custom-command warning.
-   `ForcedWeather` compilation/execution, DLL builds, shader compilation
    and SE/AE/VR runtime validation remain deferred by user instruction.

### #757: revert improved grass transparency, no local action

[Open Shaders #757](https://github.com/alandtse/open-shaders/pull/757),
`f74c58d42d4ff283409a847f3a780b99e870c3f0`, is titled
`revert: "feat(grass): add improved transparency"`.

This exactly reverses #754 across its 14 files, as verified during the
#754 review. The user rejected that feature and it was never ported;
there is no corresponding local implementation to remove. No code change.

### #756: DLSS camera transforms, accepted SE/AE/VR port

[Open Shaders #756](https://github.com/alandtse/open-shaders/pull/756),
`7f4672b9e1e4b2b86a0dda50ac611480d27583a7`, is titled
`fix(upscaling): correct DLSS camera transforms`.

User decision: **i, adapted to the local frozen stereo camera history**. The
upstream change derives projection from the inverse view and engine view
projection, then derives reprojection from the current and previous engine
matrices with the camera-relative origin shift. It replaces shared mutable
camera history so DLSS and frame generation cannot consume each other's
same-frame updates. Its math is also relevant to ordinary DLSS.

Before this port, local `Streamline::CheckFrameConstants` took projection
directly from `GetCameraProjUnjittered` or its captured equivalent. The
SE/AE path used `recalculateCameraMatrices`. VR already consumed captured
current and previous view projections, but multiplied inverse-current by
previous without translating between their camera-relative origins. The
snapshot captures both positions; its history repair and cut detection do
not bake that translation into the matrices. No equivalent reprojection
helper was found in the codebase.

The local immutable per-eye snapshot, frame identity checks, history reset
handling and foveated viewport corrections are stronger ownership and crop
contracts than the upstream patch alone. The port preserves them: all five
inputs (inverse view, current/previous view projection and current/previous
position) select the same frozen per-eye snapshot when present, with the
existing cached-engine fallback. `UpscalingCamera::BuildReprojection`
contains the upstream row-vector arithmetic without mutable history.
Streamline copies the four results through `std::bit_cast`, then applies
the existing VR crop corrections. No upstream DLSS-G is added.

Both crop blocks are unchanged, including projection scaling, FOV/pinhole
adjustment and clip-space conjugation. The snapshot/frame-token guards,
jitter, reset, motion-vector scaling, cache signature and subsequent
dispatch code are unchanged. Snapshot history repair continues to supply
matching previous matrices and origins on reset or retained-history frames.
No new resource, setting, schema, shader or preset change is introduced.

Added the `CameraReprojection` controller target using the project's
existing DirectXTK dependency. It exercises the production helper with the
upstream captured AE matrices, rotating/translating cameras and asymmetric
projection changes. Additional cases use the actual snapshot publication
and history-repair policy to cover independent eye origins, repeated
producer visits, reset seeding and retained adjacent history. The captured
AE fixture is upstream evidence, not a new local capture.

Validation:

-   `python ../../analysis/open-shaders-dev-review-20260926/audit-pr756.py`
    passed. It compares helper arithmetic with the pinned upstream commit,
    verifies that all five inputs select the same per-eye snapshot, and
    checks exact preservation of both crop blocks, frame guards and all
    code from jitter selection through the remaining dispatch/cache paths.
-   `pwsh ./tools/generate-unified-presets.ps1 -Check` passed for all tiers;
    this port does not alter settings-owner sources or their fingerprint.
-   Scoped pre-commit and `git diff --check` passed. Root CMake Gersemi is
    skipped for the previously documented unrelated baseline formatting;
    the isolated added test block passes with its enclosing indentation
    and the expected custom-command warning.
-   `CameraReprojection` compilation/execution, DLL builds and SE/AE/VR
    runtime validation remain deferred by user instruction. The required
    VR render-scale qualification also remains pending for the final
    validation stage. No visual, performance or qualification pass is claimed,
    and there are no new measurements to enter into a numbered ledger.

### #759: per-hunk upstream-sync policy, excluded

[Open Shaders #759](https://github.com/alandtse/open-shaders/pull/759),
`2fc5aab776b497228ff49a1ed248c68bdf787e57`, is titled
`docs(sync): resolve sync conflicts per-hunk`.

Changes `.gitattributes` and Open Shaders' upstream-sync documentation,
including conflict capture and per-hunk fork-preservation rules. There is
no renderer, shader or runtime change. Excluded as upstream repository
housekeeping; local AGENTS policy and the user's selective-sync instructions
remain authoritative.

### #760: grass flutter and ambient wind tuning, excluded

[Open Shaders #760](https://github.com/alandtse/open-shaders/pull/760),
`f58cc4f61dbfa3a5e1663c56d0a17320a47c62db`, is titled
`feat(wind): grass flutter and ambient tuning`.

Changes Kevdev's wind response, spring bending, flutter, tuning and UI.
The shared State/permutation edits only carry `EnableGrassWindSpringBend`;
the `RunGrass.hlsl` edit consumes that flag and the new wind displacement.
No independent ordinary-grass correction exists in those shared hunks.
Excluded under the shared wind and translation/UI rules.

### #751: post-processing pipeline, accepted early stale-job rejection

[Open Shaders #751](https://github.com/alandtse/open-shaders/pull/751),
`224ec11a466264851143c969af1d3be2c2f2b205`, is titled
`feat(post-processing): update pipeline`.

Adds a linked Cinematic Camera, converts several post-processing stages to
fullscreen raster passes, updates FFT glare/lens flare, corrects ACEScg
white-point and grading/HDR handling, and expands standalone asynchronous
shader compilation to vertex/pixel stages with generation-aware lifetimes.
This is not E11-only; the shared changes were inspected independently.

User decision: **i only for the early stale shader-job rejection**, adapted
to the existing `IsTaskStale` helper. Before this port,
`ProcessCompilationSet` checked its stop token before naming the task and
preparing compilation, but did not check its generation there.
`CompileShader` eventually rejects an obsolete
generation through `ClaimCompilation`, after descriptor resolution, macro
capture, path/key construction and compatibility lookup. The upstream
entry guard avoids that preparatory work when a queued job is already
obsolete. Retain the local generation-aware claim/publication, counter and
disk-write protections and the scope-exit dispatch-slot release. This is
an earlier exit, not evidence of a current cache-corruption defect or a
measured performance gain.

The rest of this PR is omitted from the approved limited port:

-   Local code has no upstream PostProcessing, HDRDisplay, CinematicCamera,
    ACEScg/OpenDRT or FFT glare pipeline. Adaptive Balance's color, bloom
    and tonemap paths are different implementations; their existence does
    not mean the upstream camera/FFT features are already implemented.
    Importing that subsystem would be a separate renderer feature project.
-   The ISHDR gamut/tint and explicit-luminance changes correct the ACEScg
    path. Local luminance already uses fixed sRGB coefficients and has no
    AP1 conversion. In the local working gamut, the upstream tint helper
    reduces to the multiplication already present. Preserve our stereo
    bloom boundaries, bloom blending, guarded Reinhard division and
    Adaptive Balance color composition.
-   The shared camera helpers and disable callback serve the new physical
    camera. The enlarged `FullscreenPassScope` and standalone async queue
    are absent locally; their fixes do not repair our synchronous
    `Util::CompileShader`/`LazyShader` path. Local shader compilation status
    already counts failed work as terminal through `IsCompiling`, also
    used by the shader API. Do not add unused parallel infrastructure.
-   Upstream UI and translations remain excluded.

The sole source change adds `IsTaskStale(task.GetGeneration())` to the
worker's entry guard. The existing dispatch-slot scope guard is established
first, so this exit still releases the slot. The helper uses an acquire
load of the generation; existing later guards continue to handle a reset
that races with this check. The shared path applies to SE, AE and VR.
No shader, resource, setting, preset or DevBench contract changes.

Validation:

-   `python ../../analysis/open-shaders-dev-review-20260926/audit-pr751.py`
    passed. It verifies the exact one-line source delta, dispatch-slot
    cleanup before the guard, task naming after it, and reuse of the
    existing generation helper with all later protections unchanged.
-   Scoped pre-commit and `git diff --check` passed.
-   Builds, shader compilation, compiled tests and runtime validation
    remain deferred by user instruction. No performance gain is claimed.

### #761 and #763: upstream-sync documentation, excluded

[Open Shaders #761](https://github.com/alandtse/open-shaders/pull/761),
`9e1672d32a3945aca46f049f90c369767c967ec6`, is titled
`docs(sync): add trace-intent, build-config rules`.
[Open Shaders #763](https://github.com/alandtse/open-shaders/pull/763),
`4f4bf9df3fca2605cdc839ae7d33c8836f071da5`, is titled
`docs(sync): require builds`.

Both change only upstream's sync workflow document. They cover historical
intent, cross-platform build policy and requiring real Linux ClangCL builds.
Excluded as upstream housekeeping. They do not change the user's explicit
instruction to defer builds until the end of this selective sync.

### #758: upstream merge, accepted hair/True PBR correction

[Open Shaders #758](https://github.com/alandtse/open-shaders/pull/758),
`25b96de6becb67a5754a07a11debce45cad2d89e`, is titled
`chore(sync): merge upstream/dev as of b4e7b08ec4`.

Reviewed all 44 files in the actual first-parent merge diff, plus the
constituent upstream commits. Repeated commits in the secondary ancestry
do not by themselves represent new changes in the resulting tree.

User decision: **i, limited to the Hair Specular/True PBR permutation fix** from
[Community Shaders #2738](https://github.com/community-shaders/skyrim-community-shaders/pull/2738),
`25161eb4f32deed80028d01cbbead75c4d9e4f07`, titled
`fix(shaders): compile skin and hair under TRUE_PBR`.

Local lighting defines can combine `HAIR`, `CS_HAIR` and `TRUE_PBR`.
Before this port, `Lighting.hlsl` included Hair Specular and wrote
`material.Shininess` under the first two flags alone. However,
`MaterialProperties` omits `Shininess` under True PBR, and `DirectContext` selects its PBR
fields instead of `hairShadow`, which the included hair functions use.
This was an inconsistent shader permutation visible in the source; no new
local compilation or runtime failure was observed during this review.

Ported the upstream `CS_HAIR_SHADING` gate, requiring the two hair flags and
excluding True PBR, consistently through `LightingCommon.hlsli`,
`LightingEval.hlsli` and `Lighting.hlsl`. The port preserves generic hair
semantics, the existing True PBR Marschner hair path, VR lighting/foveated behavior
and Adaptive Balance. The upstream Skin subsystem is absent locally, so
its new `CS_SKIN_SHADING` gate has no applicable local feature to fix.
This limited shared shader correction applies to SE, AE and VR.

Other merge changes are already covered or outside the agreed scope:

-   Terrain Shadows already has Z blur, half-texel sampling, finite step
    math, clamped integer height interpolation, independent height bounds,
    guarded history reads, lit out-of-map samples and a zero-width guard.
    Preserve local bias/softening choices; no visual superiority is claimed.
-   Shader enable indexing already uses a bounded `ShaderEnabled` helper.
    SSS already validates its keyword lookup and object chain. Shader-cache
    wakeups already lock, exchange and notify; disk checks handle errors.
    Cache ABI validation avoids the upstream null version-string compare.
-   Weather texture lookup already uses game resources with validated
    paths. The missing-texture vanilla material guard already exists in
    `Hooks.cpp`; True PBR has deterministic fallback bindings.
-   Unified Water already defers release with an explicit VR relocation
    and checks collection membership before dereferencing orphan pointers.
    Skylighting already rejects small occluders before later pass work.
-   Frame-generation input invalidation is already covered by the local
    lifecycle and #755. FidelityFX output directories are already isolated
    per build/configuration. The upstream SDK pin change adds cross-compile
    and optional-effect build support, not new FSR image math; retain the
    local SDK fork and its dispatch-size/FSR ports.
-   E11 setting/IBL changes, upstream UI, translations and upload metadata
    remain excluded. HDRDisplay's enable guard has no local subsystem.
-   The font-path change replaces physical containment with lexical
    containment for upstream UI compatibility. Local containment is shared
    by fonts, themes and the shader DevBench export write boundary. Do not
    weaken that shared boundary to import an excluded font UI change.

The source delta adds that shared gate and replaces 14 conditions: the
context field, direct/indirect evaluations, feature include, tint/flow-map,
tangent, material, normal, shadow, ambient and vertex-color branches.
Every Hair Specular use is under the same condition. Generic `HAIR`
branches stay intact, including the default vertex tint for PBR hair.
No shader body, C++ code, resource, setting, preset or DevBench contract
changes. No upstream Skin, UI or other excluded subsystem was added.

Validation:

-   `python ../../analysis/open-shaders-dev-review-20260926/audit-pr758.py`
    passed. It verifies the gate against the pinned upstream source, the
    exact 14 condition replacements, include order, balanced conditional
    nesting and coverage of every Hair Specular reference. Its Boolean
    check covers all 16 hair/feature/PBR/VR flag combinations. All shader
    bodies, generic hair behavior and local stereo/Adaptive Balance code
    are preserved by exact source comparison.
-   `git diff --check` and scoped pre-commit passed with `clang-format`
    skipped. The initial formatter pass requested only the pre-existing
    continuation indentation of `applyMeshTV` in `Lighting.hlsl`; that
    unrelated change was restored. The 14 changed conditions and added
    gate needed no formatting corrections; both shared headers passed the
    separate scoped clang-format check.
-   This is a source audit, not compiled shader validation. Builds, shader
    compilation and SE/AE/VR runtime checks remain deferred by request.
    Final shader validation must include Hair Specular on/off with ordinary
    and True PBR hair, including VR, back-lighting and deferred permutations.

### #764: unreadable disk-cache timestamp, rejected

[Open Shaders #764](https://github.com/alandtse/open-shaders/pull/764),
`800af745ca6195ec830324ac865039501a06b010`, is titled
`fix: treat unreadable disk-cache timestamp as miss`.

User decision: **r: the affected timestamp acceptance path is absent locally**.
The entire upstream source change sets `diskCacheOutdated = true` when
reading the cached blob's timestamp fails, preventing reuse of a blob whose
freshness could not be established.

Both `main-VR` and this sync branch already initialize that variable to
`true`. Loose-cache reuse requires a manifest digest matching the combined
source-content and compile-state hashes; missing or unverifiable digests
remain a miss. There is no disk-cache timestamp fallback to repair.
Managed packs likewise require a source-derived identity and compatible
content/compile-state metadata; missing identity or read errors produce a
miss. This protects a wider validity contract than timestamps alone.

No #764 code change was made. The decision is based on the complete pinned
diff and local read-path inspection, not a new runtime cache-failure test.

### #762: E11 weather-cache type changes, excluded

[Open Shaders #762](https://github.com/alandtse/open-shaders/pull/762),
`a6291d6f5913cf531a8c11ce4929cc4f5e9fcb5a`, is titled
`fix: reset weather cache on setting type change`.

The entire change is in `Features/Effects11/SettingManager.cpp`. When a
setting is re-registered with a different variant type, it resets that
setting's live and saved weather-cache entries to the new default. There
is no local E11 setting registry or equivalent variant cache to repair,
and no independent shared weather hunk. Excluded under the E11 rule.

### #768: Horizon Fix / EHF integration, excluded

[Open Shaders #768](https://github.com/alandtse/open-shaders/pull/768),
`35efe1ac168e79ba82add3d7758b8c7795f450b4`, is titled
`feat(horizonfix): ehf integration`.

Reviewed all six changed files, including shared shader data, feature
buffer construction, sky and image-space shaders. The new plugin export,
far-water-distance buffer value and expanded feature defines exclusively
extend EHF's sky fog distance to meet Horizon Fix water, including
reflections. The existing water rendering path receives no independent
correction. Excluded under the EHF rule; no buffer or plugin ABI additions.

### #766: weather/time scrubbing, accepted SE/AE/VR adaptation

[Open Shaders #766](https://github.com/alandtse/open-shaders/pull/766),
`9abc4063d463a1a098e84f0d43746e337038683b`, is titled
`fix(weather): stabilize time scrubbing`.

User decision: **i, adapt both runtime corrections to the local controls**:

-   The approved #750 port called `DoClearRenderPasses()` from
    `Util::RefreshForcedWeatherSky`. Upstream replaces immediate cloud-pass
    clearing with `lastRenderPassState = INT32_MAX`, leaving existing passes
    available to the current render queue until normal accumulation rebuilds
    them. This is a safety correction to our pending #750 implementation,
    not an E11 or UI-only change. Retain the local native-call routing,
    weather-lock hooks and SE/AE/VR aurora-model release paths.
-   Our `EditorWindow::DrawGameHourSlider` wrote the calendar hour directly
    without aligning `Sky::lastWeatherUpdate`. With a weather lock active,
    a backward edit can look like a midnight wrap and expire the override;
    `MaintainWeatherLock` then repairs it by forcing weather again. Adapt
    upstream's timer alignment before publishing the edited hour, only
    while a lock is active. Both the menu bar and Weather Picker use this
    shared slider, so one local correction covers both existing controls.
    Preserve unlocked edits and the current lock/pause semantics; do not
    import upstream's environment-control UI or automatic scrub locks.

CommonLib exposes both fields in shared runtime layouts and already uses
the same render-pass-state sentinel in `BSShaderProperty`. No new address
relocation, setting or resource is needed. These fixes apply to SE, AE and
VR; the current source review does not establish a runtime crash or pass.

Implemented both corrections. The cloud helper now marks the existing
property dirty without clearing its passes. The shared hour slider edits
a temporary value, rejects non-finite or out-of-range input, and aligns
the timer before publishing a valid change when both sky and an active
weather lock exist. Missing sky still permits valid hour edits; missing
calendar/hour retains the existing failure return. Idle sliders and
unlocked edits do not alter the weather timer. UI labels, ranges, layout,
pause state and weather-lock policy remain unchanged.

Extended the existing `ForcedWeather` controller fixture and extractor to
use the actual edited clock function alongside native refresh/lock routing.
Its cloud model now owns render-pass objects and models immediate clearing
as releasing that ownership. Weak references represent passes borrowed by
the draw queue, checking they survive refresh and retire only at the next
accumulation, which rebuilds once. Existing SE/AE/VR native-route and model
release cases remain. New clock cases model override expiry across backward,
forward and midnight edits, verify no repair reload, and cover inactive
locks, missing lock targets, idle sliders, invalid values, missing game
objects and both cached/singleton access paths.

Validation:

-   `python tests/extract_forced_weather.py --source-dir . --output-dir ../../analysis/open-shaders-dev-review-20260926/pr766-forced-weather`
    passed, generating the real production functions for the existing target.
-   `python ../../analysis/open-shaders-dev-review-20260926/audit-pr766.py`
    passed. It verifies the exact helper delta, preserves native routing,
    locks, model release and all other editor code, checks timer-before-hour
    ordering and input guards, and checks extraction and Python syntax.
-   `pwsh ./tools/generate-unified-presets.ps1 -Check` passed for all three
    tiers. No preset or settings-owner fingerprint changed.
-   Scoped pre-commit and `git diff --check` passed.
-   Controller compilation/execution, DLL builds, shader compilation and
    SE/AE/VR runtime validation remain deferred by user instruction. The
    fixture's modeled behavior is not a runtime engine-validation result.

### #781: PR compile-record workflow, excluded

[Open Shaders #781](https://github.com/alandtse/open-shaders/pull/781),
`e313d8748452fb811ee03d91f98188018499b7f6`, is titled
`ci(shadercache): skip compile record on PRs`.

The sole workflow hunk runs upstream's cache compile-record command only
when cache artifacts or compiled shaders will be uploaded. Shader validation
itself is unchanged. Excluded as upstream CI/publishing housekeeping; no
independent runtime cache fix.

### #775: nasal foveated presets, rejected

[Open Shaders #775](https://github.com/alandtse/open-shaders/pull/775),
`86edd27e4cd14c347a9bb33b20464b7de1fd0944`, is titled
`feat(upscaling): nasal 60/70 foveated presets`.

User decision: **r: upstream preset convenience, no missing renderer fix**.
The entire diff adds two names and two preset entries to Open Shaders'
`FoveatedRender` controller. They select square crops covering 60% or 70%
of each eye's width/height, shifted toward the nose with explicit mirrored
right-eye UVs. No shader math, resource handling, dispatch or default
selection changes.

Local foveation uses `FoveatedRegionPlan`, continuous center coverage from
0.25 to 1.0, horizontal expansion and independent per-eye offset controls.
Those settings already feed the runtime region plan and history checks.
The local Subrect controller serves previews/crops and is not the upstream
stereo foveated preset selector. Its preset type has no `rightUV` field.

The exact named shortcuts are absent. Similar size/bias choices can use the
existing local controls, but our mask/feathering and resolved eye offsets
are different, so no pixel-identical result or performance advantage is
claimed. Porting this diff would add upstream-specific preset UI/data,
which is excluded; no independent part warrants changing our renderer.
No #775 code was changed.

### #772: guarded calls, accepted diagnostics-only adaptation

[Open Shaders #772](https://github.com/alandtse/open-shaders/pull/772),
`cb90974a3db3d5f0090d9ef9ea973e4b9a072313`, is titled
`refactor(utils): add SEH-guarded call helper`.

The upstream change replaces two FidelityFX dispatch wrappers with a
shared callable helper that catches Windows structured exceptions and
returns their codes. It adds those codes to runtime-upscaler and host FSR3
fault logs, suppresses repeated host messages, and shares a vendor constant.
Reviewed the entire five-file diff and both local dispatch paths.

User decision: **i, only the exception-code diagnostics**, adapted to the
existing wrappers. Both `main-VR` and this sync branch already guard host
FSR3 and runtime-FSR dispatch with `__try`/`__except`. Local code also guards
provider creation, destruction, configuration, queries and frame-generation
dispatch. A missing crash guard is not the issue.

Local fault handling retains indeterminate runtime contexts/resources and
quarantines host contexts together with shared scratch ownership. It has
explicit lifecycle failure handling, stereo dispatch rules and an existing
`fsrDispatchCrashLogged` latch. Preserve those contracts, the current SEH
filter, module-entry checks, result codes and log-latch reset policy.
The local recovery implementation covers more ownership cases than the
upstream refactor; a generic helper alone would not improve that behavior.

The independently useful missing part is the Windows exception code.
Current logs name the failed eye and quarantine action but omit that code.
Capture it in `DispatchRuntimeUpscalerProtected` and
`DispatchHostFsr3UpscaleProtected`, initialize the output on every call and
include it in their existing fault messages. This gives SE/AE/VR failure
reports a concrete exception identifier without changing dispatch success,
fallback or retirement decisions. It does not make a failing provider
recoverable or establish a new runtime stability result.

Do not import the generic `SehGuard` files, replace unrelated guards, or
copy upstream's separate `call_once` logging mechanism. Vendor-constant
deduplication is incidental cleanup; the named Streamline constant used
upstream is not present locally. Update the existing extracted eye-dispatch
test stub if the approved helper signature changes, and preserve its
current fallback/quarantine cases. Compiled validation stays deferred.

Implemented a 32-bit exception-code output in both existing wrappers. Each
call initializes it to zero; the existing SEH handler captures
`GetExceptionCode()`. The two existing fault logs include that value as
eight hexadecimal digits alongside the failed eye. No exception code is
used to select a dispatch, recovery or resource-retirement outcome.

Updated only the existing eye-dispatch fixture's host-wrapper mock
signature and output. Its fallback, quarantine and stereo cases remain
unchanged; this is not a new SEH execution test.

Validation:

-   `python ../../analysis/open-shaders-dev-review-20260926/audit-pr772.py`
    passed. Comparing against `b12b3d01d`, the full production file differs
    only in the two diagnostic outputs, their call arguments and fault-log
    text. The audit also checks the mock-only fixture delta and generated
    host call. All other source, including return codes, module-entry
    checks, SEH filters, quarantine, stereo batching and log-latch policy,
    remains identical.
-   `pwsh ./tools/cmake.ps1 -D PROJECT_ROOT=. -D OUTPUT_DIRECTORY=../../analysis/open-shaders-dev-review-20260926/pr772-fsr-eye-dispatch -P tests/extract_fsr_eye_dispatch.cmake`
    passed, generating five headers without compilation. Initial combined
    `-DNAME=value` invocations passed truncated paths through PowerShell;
    separating `-D` from its value corrected the invocation.
-   `pwsh ./tools/generate-unified-presets.ps1 -Check` passed for all three
    tiers; no preset or settings-owner fingerprint changed.
-   Scoped pre-commit and `git diff --check` passed.
-   Builds, controller execution, shader compilation, SEH fault injection
    and SE/AE/VR runtime validation remain deferred by user instruction.

### #777: D3D12 interop setup, accepted naming-only adaptation

[Open Shaders #777](https://github.com/alandtse/open-shaders/pull/777),
`58426662760c4a4617e2586d48353371c23703d9`, is titled
`refactor(upscaling): harden D3D12 interop setup`.

Reviewed all four changed files against the local swap-chain, shared
resource and runtime-FSR lifecycle paths. Upstream centralizes shared
fences, protects temporary COM objects and NT handles during construction,
polls device removal during a CPU fence wait, and adds debug names to the
command queue, both shared-fence pairs and wrapped D3D12 resources.

User decision: **i, resource debug naming only**, adapted to our existing
ownership and lifecycle code:

-   Local `WrappedResource` already creates textures and views in temporary
    `winrt::com_ptr` owners and publishes only after success. Its members
    also retain COM ownership, and imported handles distinguish borrowed
    handles from owned `winrt::handle` temporaries. Both shared-fence setup
    paths already own their NT handles with `winrt::handle`. The upstream
    constructor/handle leak corrections are already covered.
-   Local runtime-FSR teardown and command-context acquisition return
    explicit lifecycle results and poll completion without upstream's
    five-second CPU wait. They detect `UINT64_MAX` fence completion as
    device removal and preserve pending or indeterminate ownership. The
    upstream helper's initial completed-value comparison does not handle
    that sentinel separately. Keep the local device-loss classification,
    retirement proof, fence counters and stereo submission order.
-   The command queue, interop fences and wrapped resource creation paths
    lack these names locally. Add stable names for the queue, both fence
    pairs and D3D12 resource aliases. Carry descriptive resource/eye names
    through the existing wrappers and use `Util::SetResourceName` for
    their owned D3D11 textures/views and fences. Preserve existing source
    texture names when importing borrowed textures. This makes GPU captures
    and fault investigation easier across SE, AE and VR; no rendering,
    performance or stability improvement is claimed from naming alone.

Implemented names for the command queue and both D3D11/D3D12 fence pairs.
Both `WrappedResource` constructors now require a name. The owning path
uses `Util::SetResourceName` for the D3D11 texture and its requested views,
with the existing SRV/UAV/RTV suffixes; the import path names the D3D12
alias without renaming the retained D3D11 source. All production callers
provide names, including recreation paths, per-eye runtime staging/output
and direct guide imports. Names are assigned during creation/import,
after the existing reuse checks, rather than on every dispatch.

Retained all COM/handle ownership, resource descriptors, publication order,
fence counters, waits, pending/retirement decisions and SE/AE/VR routing.
No `SharedFence` migration or new graphics allocation was introduced.
Updated the two existing extracted fixtures only for the name parameters,
required includes and naming-call mocks; their cases remain unchanged.

Validation:

-   `python ../../analysis/open-shaders-dev-review-20260926/audit-pr777.py`
    passed. Removing only the explicit naming additions reproduces the
    complete baseline `d78bb69d4` implementations, ownership header and
    fixture bodies. The audit checks that imported D3D11 sources are not
    renamed and that the generated headers contain the new signatures.
-   `pwsh ./tools/cmake.ps1 -D PROJECT_ROOT=. -D OUTPUT_DIRECTORY=../../analysis/open-shaders-dev-review-20260926/pr777-guide-interop -P tests/extract_fsr_shared_guide_interop.cmake`
    passed; script-only generation of the existing interop fixture.
-   `pwsh ./tools/cmake.ps1 -D PROJECT_ROOT=. -D OUTPUT_DIRECTORY=../../analysis/open-shaders-dev-review-20260926/pr777-save-reuse -P tests/extract_fsr_save_reuse.cmake`
    passed; script-only generation of the existing save-reuse fixture.
-   `pwsh ./tools/generate-unified-presets.ps1 -Check` passed for all three
    tiers. No preset, setting or settings-owner fingerprint changed.
-   Scoped pre-commit and `git diff --check` passed.
-   DLL/controller builds, test execution, shader compilation and GPU or
    SE/AE/VR runtime validation remain deferred by user instruction.

### #770: DLSS VRAM-budget warning, accepted local adaptation

[Open Shaders #770](https://github.com/alandtse/open-shaders/pull/770),
`3a1976b37de7f54ecb86bd9955adf5697fd4ed01`, is titled
`fix(upscaling): accept DLSS VRAM-budget warning`.

User decision: **i, adapt successful-warning handling and throttled warning
logs while retaining local diagnostics and stereo/lifecycle safeguards**.
Reviewed the complete two-file diff, current `main-VR` and sync-branch
`EvaluateDLSS`, its local callers and DevBench trace accounting.

Streamline's `slEvaluateFeatureInternal` first evaluates the feature and
restores state. Only when the result is already `eOk` can a zero remaining
VRAM budget replace it with `eWarnOutOfVRAM`; a real error is retained.
Verified this in the local SDK source (submodule pin
`2122257e0fce486f91b385aa63b9a09b0a34b363`) and the
[NVIDIA v2.14.1 implementation](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/source/plugins/sl.common/commonInterface.cpp#L544),
matching the runtime version selected by `Streamline-Runtime.cmake`.
The warning therefore does not invalidate an otherwise successful output.

Our `EvaluateDLSS` still logs every non-`eOk` result as an error and returns
false. `DispatchVendorEyeRegion` maps that to `Failed`; the main foveated
path can then reset history and dispatch full-frame DLSS. Full-eye VR
callers can replace the valid output with stretch fallback. The same
success check also affects SE/AE. These are concrete code paths, not a
measured occurrence or a quantified performance claim.

Accept exactly `eOk` and `eWarnOutOfVRAM` as completed evaluations, keeping
actual errors as failures. Report the budget warning with bounded per-eye,
per-instance logging. Local error logs already retain viewport, resource
dimensions and result changes, so preserve those details instead of
replacing them with upstream's shorter generic error message. Keep all
existing admission, constants, options, relatch and stereo checks.

The local DevBench trace also counts every nonzero evaluation result as
an evaluation failure. Adapt that classification consistently while
retaining the raw warning result in each trace record and documenting the
counter meaning. This avoids a successful warning becoming a false health
failure. Actual allocation errors or device loss must remain failures.
No settings, shader or memory-budget policy change is needed.

Implemented one shared `DLSSResultPolicy::IsEvaluationSuccessful` classifier
for dispatch completion and DevBench evaluation failure accounting. It
accepts only `eOk` and `eWarnOutOfVRAM`. Raw trace codes, constants handling,
prior pinned errors and all other trace processing remain intact. The
existing detailed error logs and admission/dispatch/lifecycle paths are
unchanged; the new warning branch avoids the error log and returns success.

Budget warnings log immediately, then at most once per 300 frames for each
eye and Streamline instance. Flat runtimes use eye slot zero. Successful
interposer initialization resets this cadence. Optional frame values keep
frame zero and `UINT32_MAX` valid, and unsigned subtraction preserves the
interval across counter wrap. Logging includes frame, viewport, result and
VR eye information, with a safe frame-zero fallback if state is missing.

Updated the registered DevBench description, action schema description and
qualification documentation to distinguish successful budget warnings from
evaluation failures. Memory, timing, constants and other health criteria
remain unchanged. No shader, setting, resource or memory-budget policy was
changed, and no measured performance or stability result is claimed.

Added `DLSSResultPolicy` to the existing controller-test group, using the
real Streamline SDK result definitions. The fixture covers ordinary success,
the budget warning, every current SDK error, unknown result values, first
and repeated warnings, eye/instance isolation, bounded indices, reset and
frame-counter wrap. Its compilation and execution are deferred.

Validation:

-   `python ../../analysis/open-shaders-dev-review-20260926/audit-pr770.py`
    passed. Reversing only the classification, warning and reset additions
    reproduces the complete `5197a2329` Streamline implementation. It also
    verifies shared classifier use, raw-result retention, consistent tool
    documentation and registration of the regression fixture.
-   `pwsh ./tools/cmake.ps1 -D PROJECT_ROOT=. -P tests/vr_render_scale_devbench_contract_test.cmake`
    passed; script-only inspection reported a coherent DevBench contract.
-   `pwsh ./tools/generate-unified-presets.ps1 -Check` passed for all three
    tiers; no preset or settings-owner fingerprint changed.
-   Whitespace, line-ending and documentation hooks and `git diff --check`
    passed. Clang-format passed for the changed implementation, bridge,
    policy and test files. Initial formatting also changed unrelated
    `Streamline.h` and `CMakeLists.txt` baseline sections; those changes
    were restored. Their task additions matched the formatter output, but
    whole-file clang-format for that header and gersemi for CMake were
    skipped afterward to preserve the baseline. Gersemi also reported its
    existing unknown-custom-command warnings.
-   DLL/controller builds, compiled tests, shader compilation and SE/AE/VR
    runtime validation, including render-scale qualification, remain
    deferred until the end by user instruction. No measurement ledger was
    created because this port produced no runtime measurements.

### #771: shared encoder selection, rejected

[Open Shaders #771](https://github.com/alandtse/open-shaders/pull/771),
`48b826dc77252c70f8ce0910aa0fed648208933c`, is titled
`refactor(upscaling): share encode shader selection`.

User decision: **r: the functional corrections are already covered locally**.
Reviewed all three changed files, both local encoder entry paths, cache
accesses/reset and settings validation, including current `main-VR`.

Upstream removes a second encoder selector in its foveated Preprocess,
centralizes the four input SRVs, separates method/output cache slots, and
adds missing-input checks to its main encoding pass. It also replaces a
literal method count in settings loading with the enum count.

Local main and submit-stage encoding already call the same
`GetEncodeTexturesCS`; there is no second foveated selector populating the
same cache slot. FSR typed-depth encoding already has its own lazy shader
slot, selected from the active runtime method and depth requirements, and
`ClearShaderCache` resets it alongside all method slots. Both encoder entry
paths reject missing TAA-mask, normal, motion-vector or depth views before
binding/dispatch, with additional local resource and lifecycle checks.

`SanitizeUpscalingSettings` already clamps method values to the actual enum
limits, including the separate no-DLSS selection. The remaining two-axis
cache conversion and common SRV collector would be organizational changes;
the fifth method slot is unused capacity, not a missing permutation or an
out-of-bounds access in the reviewed paths. No independently useful safety
or rendering fix remains to port, including partial hunks.

No #771 code was changed.

### #774: placeholder crop preset, rejected

[Open Shaders #774](https://github.com/alandtse/open-shaders/pull/774),
`e58588d8b6be6fc20f527c87468a7f27a92e78b7`, is titled
`fix(subrect): replace placeholder crop preset`.

User decision: **r: local preset reconciliation already covers the stuck
placeholder, with different crop-preservation semantics**. Reviewed both
production files and all three added regression cases. Checked the shared
utility and its actual local screenshot host rather than excluding this
solely because upstream's caller is foveated UI.

Upstream settings loading can create a synthetic `Full Frame` preset before
the host seeds defaults. `MaterializeNewDefaults` previously left it at
index zero ahead of the seeds. This change tracks placeholder provenance
and a complete explicit crop quartet, then replaces the synthetic list
with the seeds and selects the first seed when no explicit crop was loaded.
Persisted preset lists and explicit crops retain their upstream behavior.

Local `Controller` has no `MaterializeNewDefaults`. Its
`SeedDefaultPresets` and `LoadSettings` both call `ReconcileSeededDefaults`,
which handles an empty list or the sole legacy full-frame placeholder.
`ScreenshotFeature::PostPostLoad` seeds Left Eye, Right Eye and Both Eyes
(Side-by-Side) through that path. The full-frame placeholder is replaced
by those real presets; a retained full-frame crop maps to the matching
Both Eyes preset, while a custom crop remains custom. A never-loaded,
empty controller selects the first seed. Existing non-placeholder lists
are preserved, apart from the established legacy stereo-label migration.

This is not identical to upstream's choice to select the first seed after
an empty settings load: local reconciliation preserves the current crop
instead. That difference is deliberate in the local screenshot controller
and does not leave the placeholder stuck in front of the defaults. The
same implementation is present in current `main-VR`; local foveation does
not use this controller. No missing independent rendering or crop-state
fix was identified, and no #774 code was changed.

### #776: skip runtime downloads, accepted local adaptation

[Open Shaders #776](https://github.com/alandtse/open-shaders/pull/776),
`0677f487a8e39ca38173a2751bfe5e02807cdebc`, is titled
`build(cmake): add SKIP_RUNTIME_DOWNLOADS option`.

User decision: **i: adapt the opt-in DLL-only build workflow to local download,
packaging and deployment contracts**. Reviewed all seven changed files,
the local runtime modules, verified downloader, CMake staging/install paths
and both cleanup scripts. This is developer tooling with no direct shader,
UI or SE/AE/VR rendering change.

Upstream adds an OFF-by-default option that skips FidelityFX downloads and
Streamline archive download/extraction. It separates expected runtime paths
from files actually available, allowing configuration and DLL-only builds
without those payloads. Missing payloads still block installation/package
creation, with an early configure failure when AIO zip output is requested.
Runtime-path preservation prevents skipped DLLs from being treated as stale
files during staging or shader deployment. Other build dependencies must
still be available; this is not a complete offline-dependency mode.

Locally, `csx_download_verified_asset` already reuses an existing file when
its SHA-256 matches. Streamline also avoids extraction when its stamp
matches the pinned archive. These cover populated-cache offline use, but
the runtime modules are included unconditionally and a missing payload
still requires a successful download. No equivalent skip option exists;
the same downloader/runtime-module implementations are in current
`main-VR`. Thus the missing workflow has independent local value.

Adaptation must retain hash verification, binary-tree runtime staging,
the pinned Streamline version and its shipped notices/licenses. Local
packaging uses explicit file lists, without upstream's `FeaturePackaging`
registry or separate Streamline D3D12 directory, so a direct port does not
fit. Normal builds must retain their current behavior, and package/install
paths must fail clearly when required payloads are absent.

The local deployment manifest restricts stale deletion to previously owned,
unmodified content, which must be preserved. That alone does not protect an
owned runtime DLL omitted from a new skipped payload: it could still be
classified as stale. The adaptation therefore needs runtime preservation
while retaining the existing ownership and content checks, without
upstream's global destination-mirroring deletion behavior.

Implemented `SKIP_RUNTIME_DOWNLOADS`, OFF by default. Normal mode delegates
to the existing verified downloader. Skip mode only reads cached assets:
FidelityFX DLL hashes must match, and Streamline requires a verified archive
plus the matching extraction stamp before staging existing SDK files.
No download, archive extraction or invalid-cache removal occurs in skip
mode. The six Streamline DLLs and five original notices remain mandatory
expected payloads. Missing or unverified entries are omitted only from
staging inputs and build dependencies, never from required install paths.

Both automatic zip modes reject incomplete payloads at configure time.
An install guard runs for every component before dependency installs or
the existing AIO reset. It rejects missing runtime components and full
installs, while allowing SKSE-only installation. The guard also changes
when skip mode changes and is a dependency of staging, deployment and
runtime-bearing package outputs. Reconfiguration therefore refreshes the
relevant rules even when the available file lists stay empty.

Both AIO and shader staging cleanup retain the two runtime directories in
skip mode. Manifest-based deployment leaves their destination contents
untouched, including when source runtime files exist but differ. It keeps
ownership only for previously owned files whose hashes still match; user
modifications retain the existing preservation/release behavior. Disabling
skip mode restores ordinary copy and stale-file cleanup. No other shader
deployment ownership rules or runtime pins changed.

Added the developer guide and `RuntimeDownloadPolicy` controller-test
registration. Its script-only fixtures cover valid cache reuse, invalid
cache preservation, missing payloads, ordinary verified local-file download,
stale Streamline extraction/payload rejection, package-policy failures,
install ordering and SKSE-only installation, both cleanup modes, runtime
overwrite prevention and ownership across skip-to-normal transitions.

Validation:

-   `pwsh ./tools/cmake.ps1 -D PROJECT_ROOT=. -D TEST_ROOT=../../analysis/open-shaders-dev-review-20260926/pr776-policy -P tests/runtime_download_policy_test.cmake`
    passed after the final code changes. Evidence fixtures are under
    `pr776-policy/fa893923ac31`. The missing-payload warning is expected.
    Only script execution and a language-free install fixture ran: no
    project/DLL target, dependency download or game deployment was invoked.
-   `python ../../analysis/open-shaders-dev-review-20260926/format-pr776.py C:/src/skyrim-community-shaders/.git/pre-commit-cache/repou5304rxj/py_env-python3/Scripts/gersemi.exe --check`
    passed for changed ranges in existing CMake files and all new CMake
    files. Whole-file gersemi is skipped in scoped hooks to preserve the
    known unrelated baseline formatting; changed ranges are checked directly.
-   Scoped whitespace, line-ending and documentation hooks and
    `git diff --check` passed.
-   Full project configuration, DLL builds, compiled tests, real package
    validation, shader compilation and SE/AE/VR runtime checks remain
    deferred until the end by user instruction.

### #769: typed per-eye foveated depth, accepted

[Open Shaders #769](https://github.com/alandtse/open-shaders/pull/769),
`aeba846179bad44a7560a583a8f019f1e2cf0c42`, is titled
`fix(upscaling): typed per-eye foveated depth`.

The user selected **i: adapt the missing DLSS depth conversion; retain the
existing FSR conversion and local stereo/lifecycle contracts**. Reviewed the complete
seven-file diff, PR metadata and both local main/submit depth producers,
their foveated consumers, resource formats and the encode shader. The
relevant local depth code is also present in current `main-VR`.

Upstream replaces boxed copies from engine depth-stencil into per-eye and
foveated crop textures with a compute pass. It reads the typed depth SRV,
writes an R32_FLOAT UAV with explicit X/Y source offsets, checks bounds
and resources, and returns failure when conversion cannot run. It also
passes one resolved depth SRV through preparation and HMD-mask clearing.

Before this port, local FSR already wrote native depth values to per-eye R32_FLOAT textures
through `EncodeTexturesCS` with `DEPTH_OUTPUT`. Main and submit encoding
support X/Y offsets and foveated regions. The separate upstream foveated
module does not exist locally, and duplicating its FSR conversion is not
needed. Local DLSS, however, took depth from `vrIntermediateDepth`:
`PreparePerEyeInputs` and `EncodeSubmitStageVRInputs` filled it using boxed
`CopySubresourceRegion` calls from the engine's main depth texture. The
foveated center then crops that intermediate into its own depth input.

Those DLSS intermediates used the R24G8 format family instead of
upstream's R32 allocation, but format compatibility does not settle the
source-copy restriction. Microsoft's
[CopySubresourceRegion contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copysubresourceregion)
requires a whole-subresource copy for depth-stencil buffers, with zero
destination offsets and a null source box. The reviewed producers pass
eye/region boxes instead. This is an API-contract concern in local code,
not a runtime measurement or proof of a particular visible artifact.

Implemented the missing conversion through the existing guide encoder:

-   VR DLSS adds `DEPTH_OUTPUT` to its existing cached shader permutation.
    Both main-pass and submit-stage encoding bind the per-eye R32_FLOAT
    depth UAV at `u3`. No extra shader, constant buffer or dispatch is
    introduced. The existing X/Y source and output offsets cover each eye
    and foveated region. Depth remains in the engine's native range; it is
    neither linearized nor inverted.
-   `vrIntermediateDepth` now has named R32_FLOAT SRV/UAV storage. Resource
    compatibility and readiness checks require the new format and views.
    Existing foveated center resources inherit the typed format, making
    their region copies R32_FLOAT-to-R32_FLOAT.
-   FSR retains its existing `vrIntermediateLinearDepth` encoder output.
    When periphery TAA also needs the shared depth input, copy the encoded
    typed region. Submit encoding unbinds its depth UAV before that copy.
    DLSS no longer performs either boxed copy from engine depth-stencil.
-   Both VR producers validate the depth SRV's resource identity, readable
    format, single-mip/array/sample shape and stereo source extent. Typed
    destinations must cover the dispatch and provide resource/SRV/UAV.
    Invalid inputs return through the existing failure/fallback paths.
-   `PreparePerEyeInputs` consumes the already encoded resource set and
    returns failure if it is incompatible. Allocation remains before
    encoding, so preparation cannot discard freshly encoded depth or other
    guides. Reviewed all three consumers: main foveated dispatch,
    Streamline full-eye dispatch and FidelityFX split-eye dispatch.
-   Existing source ownership, input freshness proofs, eye masks, crop
    bounds, resource retirement, fallback re-encoding and cleanup remain.
    Flat SE/AE DLSS keeps its original permutation; flat FSR's typed-depth
    selection is unchanged. No settings or DevBench action changes.

Added `VRDepthEncodeShader`, a D3D11 WARP test using the production encoder
and extracted production validation helpers. It covers native D24-to-float
values, both eyes, nonzero X/Y regions, compact output offsets, untouched
pixels outside dispatch, source/target rejection and `u3` reflection. Its
18 GPU cases span VR DLSS, VR FSR and flat FSR; a separate reflection check
requires flat DLSS to omit the depth output. This fixture is registered
but has **not been compiled or run**. Both runtime shader matrices also
include the new DLSS depth-output permutation.

Validation:

-   `pwsh ./tools/cmake.ps1 -D PROJECT_ROOT=. -D OUTPUT_DIRECTORY=../../analysis/open-shaders-dev-review-20260926/pr769-depth-encode -P tests/extract_vr_depth_encode.cmake`
    passed; this only extracted source headers.
-   `pwsh ./tools/cmake.ps1 -D PROJECT_ROOT=. -P tests/vr_submit_input_freshness_contract_test.cmake`
    and `pwsh ./tools/cmake.ps1 -D PROJECT_ROOT=. -P tests/vr_render_scale_devbench_contract_test.cmake`
    passed as script-only source contracts.
-   `python ../../analysis/open-shaders-dev-review-20260926/audit-pr769.py`
    passed. It checks both bindings and typed-only region copies, resource
    preparation, unchanged submit freshness/cleanup and flat/fallback code,
    extracted-header ordering and unchanged preset values/revision.
-   `generate-unified-presets.ps1 -Check` initially failed because the source
    fingerprint hashes the edited Upscaling files. After reviewing the
    unchanged settings schema, refreshed the policy fingerprint and ran
    the generator and `-Check`: all three tiers passed. Only their source
    markers and derived report hashes changed; revision 5 is retained.
-   Changed-range clang-format 22.1.4 and gersemi checks, full formatting of
    new files, scoped whitespace/YAML/documentation hooks and
    `git diff --check` passed. Whole-file clang-format and gersemi hooks
    are skipped to preserve unrelated baseline formatting; the relevant
    formatting is checked directly with `format-pr769.py --check` in the
    local audit folder.
-   Builds, shader compilation, the new GPU fixture, existing compiled
    tests and runtime checks remain deferred by user instruction. This is
    an API-contract correction, with no claimed visual or performance
    result. Physical-HMD qualification remains pending; the iteration
    record reflects implementation only, with no new measurement ledger.

The later #779 review overlaps standard per-eye depth handling and must
account for this shared conversion. Direct use of the whole native depth
resource by the existing left-eye DLSS path was not changed here.

### #773: publish scene exposure to features, rejected

[Open Shaders #773](https://github.com/alandtse/open-shaders/pull/773),
`495642ba406a87f0ce49082a54ecfebe03ad04cc`, is titled
`refactor: publish scene exposure to features`.

The user selected **r: no local exposure producer or consumer for this contract**.
Reviewed the complete ten-file diff, PR metadata, local feature lookup,
Adaptive Balance and the upscaler exposure setup. This is not Scene Manager
UI and is not E11-only; the decision is based on the code dependency.

Upstream publishes Post Processing's active histogram adaptation buffer,
luminance clamp range and exposure compensation through `Feature`.
Composite and the published CPU/HLSL helpers share the same exposure
formula. Publication stops when that pipeline is unavailable, bypassed or
does not own tonemapping. This prepares the later #780 RCAS exposure work;
#773 itself does not change the rendered composite.

CSX has no `PostProcessing`, `HistogramAutoExposure`, adaptation SRV or
consumer of `Feature::SceneExposure`. Adaptive Balance is implemented in
`AdaptiveBrightness`: `ApplyProfile` adjusts separate lighting multipliers
and gamma offsets, while `GetCommonBufferData` publishes those controls.
These spatially different lighting adjustments cannot be represented as
the single scene-wide exposure expected by this interface. They are not
an existing equivalent histogram implementation.

Local DLSS enables vendor auto exposure with pre-exposure 1; FSR likewise
enables vendor auto exposure and passes no external exposure resource.
Adding a default-false feature API and unused shared formula would provide
no runtime benefit. The remaining generic `FindLoadedFeature` refactor only
replaces a working early-exit lookup, with no independent correction.
Reassess useful independent #780 normalization changes when its turn
arrives; do not create a synthetic exposure from Adaptive Balance here.
No #773 code was imported.

### #782: stale VR menu backdrop with FSR, rejected

[Open Shaders #782](https://github.com/alandtse/open-shaders/pull/782),
`c9ffae711ae06ff933ba4d317475b4ed8c7040cb`, is titled
`fix(upscaling): stale VR menu backdrop with FSR`.

The user selected **r: the upstream copy path is absent and local routing already
guards DLSS output**. Reviewed the complete one-line diff, PR metadata,
upstream menu-blit caller, and local main/foveated/submit sharpening and
copy-back paths. This is a rendering correction, not an upstream UI-only
exclusion.

Upstream `PerfMode::MaybeBlitMenuBG` calls `Upscale`, then copies
`refraTempTex` into `testTexture` when the DLSS sharpening redirect predicate
is true. That predicate previously checked resources and saved sharpening
settings without requiring the active method to be DLSS. With FSR active,
the copy could replace fresh FSR output with an unwritten or stale DLSS
intermediate. The patch adds `GetUpscaleMethod() == UpscaleMethod::kDLSS`.

Local code has no `IsPerfModeSharpenRedirectActive`, `MaybeBlitMenuBG`,
`refraTempTex` or `testTexture` path. Its `PerfModeState` manages the render
scale boot contract rather than that upstream menu bridge. The analogous
local routing is already guarded:

-   `RefreshRuntimeResolutionPlan` selects the sharpener target only for
    `plan.upscaleMethod == kDLSS`; main foveated routing likewise requires
    DLSS before choosing `sharpenerTexture`.
-   Submit-stage sharpening requires `upscaleMethod == kDLSS` as well as
    its existing lifecycle and settings checks.
-   `Upscale` clears `dlssUpscaleOutputInSharpenerTexture` on entry.
    Streamline or the successful DLSS foveated path publishes it only when
    coherent output is in that texture. `ApplySharpening` returns before
    any copy or dispatch unless that producer flag is set. A saved DLSS
    sharpening setting alone therefore cannot authorize the FSR copy-back
    described in this PR.

An exact source comparison confirmed that all six reviewed routing and
producer-guard regions match the current primary `main-VR` checkout.
There is no additional hunk to adapt for VR or SE/AE. This source review
does not claim that every menu symptom is solved or reuse upstream runtime
results as local evidence. No build, shader compilation or runtime test
ran; no #782 code was imported.

### #788: disable height fog in the world map, excluded

[Open Shaders #788](https://github.com/alandtse/open-shaders/pull/788),
`2ee662790a4a69b361e437505903d4722d26990e`, is titled
`fix(fog): disable height fog in the world map`.

Excluded under the standing EHF rule. The complete diff changes only
`ExponentialHeightFog.cpp`: its common-buffer enable flag is cleared while
the map is open, and its volumetric prepass takes the existing resource
release path. There are no shared renderer, map-menu or non-EHF changes
to extract. Local source has no Exponential Height Fog feature. No code
was imported.

### #787: effect brightness control, adapted

[Open Shaders #787](https://github.com/alandtse/open-shaders/pull/787),
`5f00b928359780c2e32607712a5fb1f99ecf38b2`, is titled
`feat(utility): add effect brightness control`.

The user selected **i: adapt the weather-colour controls into Adaptive
Balance**. The non-translation code changes are independent of OS Utility
UI, E11 and Scene Manager. The existing Effects multiplier scales
directional and point-light contributions; it does not replace weather
Effect Lighting colour. The earlier #741 shader-side sky-static multiplier
also acted at a different stage and on a narrower material predicate.

Implemented scope:

-   Added Weather Effect Brightness, default 1 and range 0-2, to Global,
    time/interior profiles and replacement/layered locations. Persistence,
    sanitization, neutral defaults and profile interpolation include it.
    Each layer's detailed Lighting gate applies; the existing Effects
    lighting control remains independent.
-   Adaptive Balance owns a post-colour-update detour using upstream's
    `REL::RelocationID(25686, 26233)`. The established universal relocation
    maps SE/VR to the first ID and AE to the second. CommonLib exposes the
    same live `Sky::skyColor` layout and colour indices. The hook scales
    Effect Lighting and Sky Statics after the original engine update,
    without weather-record mutation or a separate render pass. It does
    not import upstream's broader Linear Lighting/weather callback rewrite.
-   A focused helper restores our previous output before the engine update
    only when the live sky identity and colour still match. It retains
    later external edits, does not dereference saved owners, and avoids
    accumulating scales when an update leaves the colour unchanged.
    Neutral/disabled, unavailable and measurement-bypassed states restore
    native colours on the next update. Non-finite factors use neutral;
    finite factors are bounded and non-finite colour results are skipped.
    Profile-resolution exceptions retain the fresh native result and log
    a bounded warning. Failed detour attachment is logged and reported as
    unavailable; the other Adaptive Balance controls remain usable.
-   Sky Static Brightness now scales its weather colour. Removed its final
    `Effect.hlsl` multiplication and replaced the old C++/HLSL float with
    zeroed padding, retaining the 80-byte buffer and transparency offset.
    Transparency, material classification, blend handling and VR rules
    remain unchanged. Non-neutral brightness can look different because
    its scope and order relative to colour conversion have changed.
    Bumped the Adaptive Brightness feature version to invalidate affected
    shader cache entries.
-   Extended `set_adaptive_balance_visuals`, its description/schema and
    configured/effective status with `effectBrightness`.
    `adaptiveBalanceWeatherColorsAvailable` exposes hook installation
    through `communityshaders.menu`. Documented next-update timing and the distinction
    between requested values and an applied weather update.
-   Added production-hook/helper regression cases for repeated updates,
    weather and external edits, zero/neutral recovery, runtime gates,
    missing weather, failure recovery, ownership and finite bounds.
    Composition tests cover defaults, interpolation and location layers;
    DevBench validation includes the new field and shader reflection
    expects the retained padding slot. These C++ tests are not executed.

Validation is limited to source extraction, descriptor/range/persistence
and buffer-layout audits, changed-line formatting, scoped pre-commit,
unified-preset generation/check and `git diff --check`. The initial preset
check correctly detected the changed source fingerprint; after reviewing
the additive neutral-default field, refreshed the fingerprint and all
three generated markers while retaining contract revision 5 and existing
preset values. No build, shader compilation, deployment, compiled test or
runtime test ran. SE/AE/VR relocation availability, update timing, disabling
and weather transitions still require validation at the end of the sync.

### #779: typed right-eye DLSS depth, rejected

[Open Shaders #779](https://github.com/alandtse/open-shaders/pull/779),
`72518c269d9b0d52b4cbe62ca5ddab7043b7f84c`, is titled
`fix(upscaling): typed right-eye DLSS depth`.

The user selected **r: already covered on the sync branch by the accepted #769
port**, `658087a45`. Reviewed the complete three-file diff, PR description,
local encoder selection/allocation/bindings, preparation, standard DLSS
evaluation and submit-stage encoding. This is not an exclusion: it is a
useful correction already included in an earlier approved port.

Upstream removes the boxed copy from the engine's depth-stencil into the
right-eye DLSS intermediate. It enables the existing typed-depth encoder
for VR DLSS, writes native non-linear depth to R32_FLOAT, and tags that
texture for eye 1. The standard direct left eye keeps the native combined
depth resource. Its helper centralizes the upstream shader-selection and
UAV-binding policy; allocation, readiness and naming changes support that
same replacement.

The sync branch already has the complete relevant behavior:

-   `GetEncodeTexturesCS` selects `DEPTH_OUTPUT` for VR DLSS. Both main
    and submit-stage encoders bind the validated R32_FLOAT
    `vrIntermediateDepth[eye]` UAV at u3. SE/AE DLSS keeps its existing
    permutation, and FSR keeps its own typed output.
-   `PreparePerEyeInputs` consumes the already-encoded DLSS depth. Its
    remaining depth copy is typed R32_FLOAT FSR output to typed R32_FLOAT
    periphery input, not a boxed copy from the native depth-stencil.
-   Standard Streamline right-eye evaluation already receives
    `vrIntermediateDepth[1]`; isolated-eye fallback and foveated paths use
    the same per-eye native-depth values. The standard direct left-eye
    path keeps the engine resource, matching #779's intended behavior.
-   Local code deliberately retains separate typed DLSS/periphery and
    FSR resources for its larger lifecycle and foveated contracts.
    Importing upstream's single-resource deletion/rename would not add a
    missing correction. The local port also validates source/target
    identity, format and bounds and retains its failure/fallback gates.

This coverage belongs to the working branch; it does not claim #769 has
landed on primary `main-VR`. The upstream PR's integration tests are not
local evidence. Local compilation and runtime qualification remain pending.
No #779 code was imported.

### #780: normalize RCAS by scene exposure, partially adapted

[Open Shaders #780](https://github.com/alandtse/open-shaders/pull/780),
`9b56e59dbbadd30140e9b9f832e1c82bc465babd`, is titled
`fix(upscaling): normalize RCAS by scene exposure`.

The user selected **i, partial: take only the independent RCAS buffer debug name**.
Reviewed the complete three-file diff, PR metadata, the #773 exposure
formula, local fixed/motion-adaptive RCAS, shared dispatch/bindings,
constant-buffer wrapper and vendor exposure setup. Before this port, the
reviewed RCAS, motion-dispatch, binding, buffer-wrapper and feature-interface
files were identical between the sync branch and current `main-VR`.

The main upstream change multiplies all five RCAS samples by the published
scene exposure before computing contrast, then divides the output RGB by
that exposure. This addresses brightness-dependent sharpening in raw HDR
before tonemapping. Its new constant-buffer fields and t1 adaptation SRV
carry the #773 producer's luminance range, compensation and adapted value.
With no producer, the exposure factor stays one.

The visual correction is not implemented locally, but its required input
is absent. Neither branch supplies `Feature::FindSceneExposure`, an adapted
luminance buffer, `HistogramAutoExposure` or the Post Processing producer.
Adaptive Balance changes separate lighting components and cannot supply
this single scene-wide exposure. DLSS uses vendor auto exposure with
pre-exposure 1; FSR also enables vendor auto exposure and supplies no
external exposure resource. Those settings do not publish the required
adaptation buffer to our standalone RCAS pass. Importing an always-neutral
interface would not enable this correction.

There are additional local integration differences: motion-adaptive RCAS
already binds its motion texture at t1, and its constant buffer extends the
existing 16-byte fixed configuration. Upstream's t1 binding and 32-byte
configuration cannot simply replace those contracts. A future exposure
implementation would need a real producer plus coordinated fixed/motion
paths, resource slots and state restoration. The current code is not
claimed to provide equivalent exposure normalization.

Implemented the independent hunk: pass `"Upscaling::RCASConfig"`
to the existing `ConstantBuffer` constructor. The fixed RCAS buffer was
unnamed in both branches; the wrapper only calls the established
`Util::SetResourceName` path when given a name. The motion-adaptive buffer
already has its own distinct name. This identifies the fixed
and fallback configuration in graphics captures for SE, AE and VR, without
changing sharpening, resources, shader layout or settings. This is
a diagnostic-only port, with no visual or performance improvement claimed.

The remaining hunks all belong to the unavailable exposure path and were
not imported. The complete production diff is one constructor argument.
Verified the existing wrapper reaches `Util::SetResourceName` and
`ID3D11DeviceChild::SetPrivateData`, with no resource descriptor change.
Scoped pre-commit and `git diff --check` passed. No new test is needed for
this debug label; no build, shader compilation, compiled test, deployment
or runtime validation ran, as instructed.

### #783: frozen VR menu with foveated DLSS, rejected

[Open Shaders #783](https://github.com/alandtse/open-shaders/pull/783),
`fa8af0c3cd0212feae886a44477f949448a5e673`, is titled
`fix(upscaling): frozen VR menu with foveated DLSS`.

The user selected **r: the route-selection correction is already covered locally**.
Reviewed the complete two-file diff and PR metadata, then traced local
main-pass producers, menu/fallback routing, sharpening finalization and
the separate submit-stage path. This is a renderer correction, not an
upstream UI-only exclusion.

Upstream chose its separate foveated sharpening path using
`Bridge::IsRouteActive()`, which describes configuration. When a menu
skipped foveated evaluation, standard DLSS wrote its sharpening intermediate
but the foveated finalizer ran instead. The presented backdrop could retain
the previous gameplay frame. The patch records the route's actual result
in `routeHandledThisFrame`, resets it on each upscale, and uses it to select
the matching finalizer. Both DLSS and FSR publish their route result.

Local code has no such foveated/standard finalizer switch or upstream
`refraTempTex`/`testTexture` presentation pair:

-   `PerformUpscaling` calls `Upscale`, which clears
    `dlssUpscaleOutputInSharpenerTexture` before its early-return checks.
    Successful foveated DLSS publishes this flag only when its output
    actually targets `sharpenerTexture`. Failure falls through to the
    standard vendor route, with full-eye re-encoding when necessary.
-   Standard Streamline DLSS publishes the same output flag only after
    successful evaluation or a successfully presented stretch fallback.
    FSR cannot leave a previous DLSS flag active after `Upscale` resets it.
-   Both DLSS postprocessing call sites use the same `ApplySharpening`.
    It requires the producer flag before using the intermediate, then
    sharpens into the current main target or copies the coherent output
    there when sharpening is disabled or unavailable. A saved foveation
    setting cannot select a different, mismatched finalizer.
-   Submit-stage presentation takes its own return path before main-pass
    sharpening. Its per-eye finalizer uses the selected current output
    and copies that output if the optional sharpener fails. It does not
    invoke the absent upstream finalizer switch.

Exact source comparisons confirmed that sharpener routing, output reset,
foveated result/fallback handling, `PerformUpscaling`, `ApplySharpening`
and `Streamline::Upscale` match current primary `main-VR`. Both primary and
sync have the same two common-finalizer call sites. No additional hunk
provides a missing SE/AE or VR correction. This source conclusion does
not establish that all menu-freeze symptoms are solved or reuse upstream
runtime tests as local evidence. No #783 code was imported; builds
and runtime checks remain deferred.

### #778: optional FOV blend curve, accepted partial port

[Open Shaders #778](https://github.com/alandtse/open-shaders/pull/778),
`13999735d5e574b02ca2a3ace8651ec73605242c`, is titled
`feat(upscaling): oval foveated edge mask`.

Recommend **i, partial: adapt the falloff curve to our existing FOV blend**.
Reviewed the complete five-file diff and PR description, separating its
shader/settings changes from the excluded translations and upstream UI.
Compared the local mask, CPU region/tile planning, centre compositors,
periphery TAA, mask visualization and shared shader-detail consumers.

Upstream adds an optional ellipse to its rectangular subrect composite and
a falloff exponent from 0.5 to 2, default 1. It estimates pixel distance
from the ellipse edge, leaves the background untouched outside it, and
applies the exponent before feather smoothstep or dither blending. Vendor
input/output regions remain rectangular. This adds no render pass or
resource; its constant-buffer fields reuse upstream padding.

Local code already rounds the FOV boundary using the power-4 superellipse
in `Common/FoveatedMask.hlsli`, with horizontal expansion, per-eye offsets
and smooth feathering. `FoveatedCenterBlendCS` already leaves zero-weight
pixels untouched, and `FoveatedSpatialCompositeCS` selects periphery there.
There is no corresponding local dither-leak correction to import.

This is not identical to upstream's power-2 ellipse or pixel-distance
feathering. Our normalized-distance feather has different semantics, and
the current rounded shape is not claimed to be universally better. It is
already integrated with the local FOV contract: CPU tile distances and
the inscribed centre rectangle use the same power 4, periphery dispatch
skips that centre rectangle, and TAA/history and shader-detail consumers
share the mask. A shader-only ellipse replacement would make those
ownership and coverage assumptions disagree. An ellipse option would
require coordinated changes beyond upstream's standalone compositor.

The independent falloff control is missing. Our current blend is a fixed
smoothstep; the existing Center Blend/TAA Transition setting changes band
width, not the distribution of centre/periphery weight inside that band.
The useful partial scope is:

-   Add an optional 0.5-2 falloff curve, neutral 1, to the existing local
    upscaling FOV controls, persistence, finite-value sanitization and
    DevBench action/schema/status. Preserve the exact current calculation
    at 1 and retain old-config defaults.
-   Reshape only the upscaler's centre/periphery blend inside its current
    transition band. Keep the power-4 shape, eye offsets, width settings,
    zero/full-weight boundaries and CPU dispatch/underlay geometry.
    Keep unrelated shader-detail feature controls on their current curve.
-   Carry the setting consistently through main and submit-stage centre
    blending, spatial composition and matching periphery-TAA weights.
    Include it in applicable settings snapshots/history invalidation so
    cached or reused output cannot silently retain the old curve.
    Reuse existing passes; keep C++/HLSL buffer contracts synchronized.
-   Leave the ellipse shape, rectangle selector, upstream dither mode,
    translations and upstream UI out. The change would tune the visible
    seam, with no claimed vendor workload reduction or measured speedup.

At review time the shared mask, CPU planning header and all four reviewed
upscaler shaders matched current `main-VR` exactly. The CPU mask/tile-distance and
feather-sanitization regions also match. Neither branch has a configurable
falloff exponent. Foveated vendor composition remains VR-only, with SE/AE
behavior retained. If accepted, add focused neutral/bounds/stereo and
temporal coverage, and record pending render-scale qualification. Builds,
shader compilation and runtime validation remain deferred until the sync
ends.

The user accepted the partial port as an **in-game FOV checkbox** for A/B
testing. [FOV blend curve](fov-blend-curve.md) documents the implemented
controls, saved defaults, DevBench contract and deferred runtime checks.
Checkbox off retains the saved exponent and passes neutral 1 to the
shader. Center blend/spatial buffers reuse padding; periphery TAA appends
one float4 with matching size/offset assertions. All five upscaler blend
weight call sites use the focused helper, while shared shader-detail
consumers retain their existing code. Effective changes reset history and
invalidate frame state; finalized submit-eye reuse compares the exponent.
The curve does not enter the FSR allocation/lifecycle key.

The existing `FovSettings` test now extracts the production setter, UI,
getter and DevBench validator. `FoveatedBlendCurveShader` adds production
shader compilation/reflection and WARP checks of neutral, bounded,
directional and mirrored-eye weights. These fixtures are added, not run.
Source checks passed; runtime qualification remains explicitly pending in
the VR render-scale iteration record. Upscaling's feature version is
2-6-0. The three unified presets retain contract revision 5 and update only
their source fingerprint; old settings still default to the disabled curve.

Validation without builds:

-   `python tests/extract_fov_settings.py --source-dir . --output-dir ../../analysis/open-shaders-dev-review-20260926/pr778-fov-settings`
-   `python ../../analysis/open-shaders-dev-review-20260926/audit-pr778.py`
-   Changed-line clang-format 22.1.4 and gersemi checks via
    `format-pr778.py --check`; scoped remaining pre-commit hooks.
-   `pwsh ./tools/generate-unified-presets.ps1 -Check` and
    `pwsh ./tools/git.ps1 diff --check`.
-   No build, shader compilation, compiled test, deployment or runtime
    validation ran. No measured performance claim is made.

### #784: sharpen foveated DLSS in PerfMode, rejected

[Open Shaders #784](https://github.com/alandtse/open-shaders/pull/784),
`779efdc1a0ce5b631491d0cd206cc4574006234a`,
`fix(upscaling): sharpen foveated DLSS in PerfMode`.

The user selected **r**, already covered by the local output-routing design.
Read the complete two-file diff and PR description, then followed local
main and submit outputs through sharpening and failure finalization.

Open Shaders' foveated PerfMode route wrote the presented `testTexture`,
while its separate foveated sharpener operated on `kMAIN`. The fix redirects
all foveated output writes to `refraTempTex` when sharpening is active and
uses `ApplySharpening` to resolve into `testTexture`. The non-PerfMode route
retains its other finalizer. The bug concerns the image being sharpened,
not the sharpening filter itself; there is no independent shader hunk.

Local code has neither that PerfMode texture pair nor its split
`FoveatedRenderImpl::Postprocess` finalizer:

-   Main foveated DLSS chooses `sharpenerTexture` before dispatch when
    sharpening is requested and publishes the actual-output flag only on
    success. Both postprocess sites use `ApplySharpening`, which consumes
    that exact intermediate and writes the main output. Disabled or failed
    sharpening retains the ready vendor output through the existing copy.
-   Submit-stage DLSS chooses its per-eye sharpening intermediate before
    foveated dispatch/composition. `finalizeSubmitStageEyeOutput` sharpens
    that selected output into the submitted `vrIntermediateColorOut`, with
    a copy fallback if the optional sharpener fails. With sharpening off,
    the output already targets the final eye texture.
-   FSR does not enter the DLSS sharpener. The existing reset and
    actual-producer flag keep skipped/fallback routes from sharpening stale
    output, as reviewed for #783.

Source comparisons against primary `main-VR` confirm these routing and
finalization regions are already present there. #778 changes curve
composition/reuse, not the sharpening ownership. Importing #784's
upstream-specific dispatch selector or texture redirect would not add a
missing local behavior. Its reported upstream runtime results are not
local validation. No #784 code is imported.

### #789: prevent highlight outlines, rejected

[Open Shaders #789](https://github.com/alandtse/open-shaders/pull/789),
`af8134814a17073971628f59c132b196939889ce`,
`fix(post-processing): prevent highlight outlines`.

The user selected **r**, the affected highlight-grading controls and pass
are absent locally. This is not a claim that the upstream fix is already
implemented, or that local rendering cannot produce outlines for other
reasons.

Read the complete three-file diff and PR description. Upstream changes
`ShadowsMidtonesHighlights` in its Post Processing color-grading shader,
adds a focused `Highlights::Apply` helper and adds seven shader tests.
Previously, a strong reduction in highlight gain or a negative highlight
offset could make brighter input become darker across the midtone-to-
highlight transition. The PR uses the integrated smoothstep mask for
reduced gain and limits negative offsets by available gained brightness.
Neutral and boosted controls retain their existing calculation; tests
cover brightness ordering, HDR headroom/slope, narrow ranges and offsets.
Its reported symptom is a dark outline around procedural sun and cloud
silver lining. The correction is shared grading math, so it was evaluated
independently of the postponed Procedural Sun feature.

Searched local runtime and shader sources for the affected function,
gain/offset fields, transition thresholds and helper, then inspected
Adaptive Balance, Linear Lighting, `ISHDR.hlsl` and `DisplayMapping.hlsli`.
Neither the sync branch nor current primary `main-VR`
(`041e5dc0a573da84727fdf059607fc0e8ce287b1`) has that Post Processing pass or
its shadow/midtone/highlight gain and offset controls.

Adaptive Balance's existing `AdaptiveBalanceColor::Apply` uses a
luminance-based power curve for contrast, constrained to [0.5,2], plus
saturation. Its color helper matches primary `main-VR` exactly. The local
HDR/display path applies its own tonemapping, bloom, cinematic settings
and Adaptive Balance, without the upstream three-zone interpolation.
The sync branch's cloud/weather brightness controls also do not interpolate
highlight gain or subtract a luminance-dependent highlight offset.

There is therefore no corresponding calculation to replace, including
partially inside Adaptive Balance. Importing the helper alone would add
unused code; adding three-zone grading would be a separate feature rather
than applying this correction. No #789 code is imported.
This is the final entry in the pinned 56-entry dev review range, not a
claim that the live upstream branch has no newer work.

Validation: read-only source comparison, PR metadata and complete diff
review; scoped documentation hooks and `git diff --check`. No build,
shader compilation, compiled test, deployment or runtime validation ran.

## Pinned review complete

The user rejected #789, completing decisions for all 56 first-parent
entries from v2.15.0 through the pinned dev endpoint
`af8134814a17073971628f59c132b196939889ce`. The entry without a PR number in
its commit subject, `25b96de6b`, is covered by the #758 merge review.
No candidate remains awaiting a decision. Accepted ports are on
`codex/pr730-open-shaders-dev-sync`, including the requested Adaptive
Balance base and the optional FOV curve. The primary checkout remains
on `main-VR`; this review does not merge or publish the working branch.

The user's deferred end-of-sync build and compiled validation are recorded
in [the final validation report](open-shaders-dev-validation.md).
Earlier per-port validation notes describe the checks available
when those decisions were made; they are not final runtime qualification.

## Initial review verification

-   Refreshed only Open Shaders `dev` and `main` with `--no-tags`; pinned
    the resulting review range and compared #733's complete shader diff.
-   Read #733 metadata through the GitHub CLI and checked local sky shader
    permutations and both extra-flag definitions.
-   `pwsh ./tools/dev-doctor.ps1 -Network`: zero failures after running
    outside the restricted sandbox; one existing public-HTTPS remote warning.
    Remote configuration was retained.
-   No build, shader compilation, deployment or runtime validation ran
    during that initial review. End-of-sync compiled results are recorded
    in the [final validation report](open-shaders-dev-validation.md).

## Integration into main-VR (2026-09-27)

At the user's direction, merge all 40 commits reachable from reviewed sync
head `d93d49668f83e045e9fef177bc6d49b8496950ad` but absent from the requested
`main-VR` head `2bbecfce6d483fc33b788cd9984022997673be90`. The requested head
is the first parent, and the sync head is the second parent. Original
commits and authorship remain intact, including both corrections from the
[adversarial review](open-shaders-dev-adversarial-review.md).

Resolve the overlapping Adaptive Balance histories with the 80-byte
atmosphere layout and the existing water-profile persistence fix. Retain
main-VR's material parallax strength, shader-cache packaging and public
release changes. DevBench exposes both material parallax strength and the
optional FOV blend curve. Regenerate all three preset fingerprints from
the combined settings sources, retaining contract revision 5 and neutral
material parallax strength. A staged-tree audit confirmed that all 30
main-only paths and 82 sync-only paths retain their respective contents.
All 22 pre-existing modified or untracked local files retained their bytes.

`pwsh ./tools/validate-local.ps1 -OutputDirectory build/analysis/open-shaders-main-integration-20260927/validation`
passed on the resolved merge tree before creating the merge commit:

-   Universal ALL Release DLL, with SE/AE/VR and DevBench enabled.
-   All 162 registered CTest groups executed and passed; none missing,
    disabled, unbuilt or skipped.
-   Preset regression suite, generated preset check, diff check, manifest
    verification and unchanged before/after source snapshots passed.
-   Scoped pre-commit passed. Changed C++/HLSL integration lines passed
    clang-format separately; whole-file clang-format and gersemi hooks
    were skipped to preserve the already-reviewed surrounding formatting.

The producer source is the requested base above plus the resolved merge,
with dirty digest
`3ce360314a1240caa7a4f33a0993050c0f8af2ec554857e6514db87db86e31e3`.
Build ID:
`31e3a8011a753698eef02c6d54f7c9865766574c367716ac89dab21a8c577f08`.
The DLL is 29,109,760 bytes, SHA-256
`cd75e56f91123d0485558bc30d2512fd82f43eb13ed2c0eaaa8ba9095e84a43c`.
This integration record is the only tracked change after that validation.
No game deployment, SE/AE/VR runtime test, headset qualification or push
was performed. The primary checkout remains on `main-VR`.

## Review resumed through Open Shaders 2.17.0 (2026-09-29)

Resume after #789 at `af8134814a17073971628f59c132b196939889ce`.
The fetched Open Shaders `main` and `dev` both point to
`74f95a4d7f52c5f61edf6267ebca813a94bbc71a`, release 2.17.0.
This range contains 27 merged PRs and two release commits. Review PRs in
first-parent integration order and retain the existing exclusions, adding
upstream Neural Rendering because CSX maintains its own on `main-vr-nr`.
Inspect mixed PRs for useful changes outside excluded features.

The user directed that accepted ports go directly onto `main-VR`.
Use its existing checkout at `build/worktrees/main-vr-feature-metadata`,
starting at `d53ba287d69dec693a107fbd834e36df1ef95b72`.
Keep builds deferred until the end and obtain each `i` or `r` decision
before implementing that candidate.

### #793: shared PerfMode output check, rejected

[Open Shaders #793](https://github.com/alandtse/open-shaders/pull/793),
`9810152c2a38f54c76a8a2184b70672b2c9f9657`,
`refactor(upscaling): share PerfMode output check`.

The user rejected this PR. It extracts the repeated upstream PerfMode
hook-active and test-texture check into `IsPresentingTestTexture()`.
CSX uses a different presentation pipeline and has no `GetTestTexture()`
or `IsPresentingTestTexture()` call sites. The helper and its callers
provide no independent local change. No runtime code was imported.

Evidence: reviewed the complete upstream diff and searched the local
source for both symbols. No build, shader compilation or runtime test
ran for this decision.

### #792: shared compute buffer binding, accepted partial port

[Open Shaders #792](https://github.com/alandtse/open-shaders/pull/792),
`6419c39b7d1fa728603ab406f06667937aec36e2`,
`refactor: share SharedData compute CB binding`.

The user accepted the proposed partial port. Replace the remaining
manual shared-buffer binding in `DynamicCubemaps::UpdateCubemap()` with
`Util::BindSharedDataConstantBuffersForCS(context)`. The helper already
binds shared and feature data at compute slots b5 and b6, in that order,
and handles missing context, state or buffers. Its header is already
included. This preserves the normal binding behavior for SE, AE and VR
without changing resource ownership, dispatches or shader permutations.

Do not introduce upstream's parallel `State::BindSharedDataCS()` helper.
Other applicable callers already use our centralized binding utilities;
upstream EHF, wind and PostProcessing callers remain outside this port.

Validation: source comparison confirmed the same buffer order, slots and
count. A full source search leaves the direct compute-slot b5 binding only
inside the shared helper. The following checks passed:

-   `pwsh ./tools/pre-commit.ps1 run --files src/Features/DynamicCubemaps.cpp docs/development/open-shaders-dev-sync.md`
    passed whitespace, line-ending, clang-format and Prettier hooks;
    YAML and CMake hooks had no applicable files.
-   `pwsh ./tools/git.ps1 diff --check` passed.
-   `pwsh ./tools/dev-doctor.ps1 -Network` reported zero failures and
    zero warnings. The initial sandboxed hook attempt could not write
    its Git cache database; the approved retry passed.

Builds, shader compilation and runtime validation remain deferred.

### #790: foveated rendering in pause menus, rejected

[Open Shaders #790](https://github.com/alandtse/open-shaders/pull/790),
`5a2421dcbfdafaf9639d6927bd13871c01d1349d`,
`fix(upscaling): run foveated route in pause menus`.

The user rejected this PR. Upstream replaces its blanket pause-menu
restriction with a main/loading-menu restriction in the FoveatedRender
route. CSX uses different menu presentation, input-freshness, transition
and submit-ownership contracts. Substituting the upstream predicate
would not preserve those contracts. This rejection does not claim that
every paused-menu scenario has been runtime-verified locally.

No implementation was imported. The recommendation rests on the
upstream diff and the local foveated dispatch and menu-presentation gates
reviewed during the 2.17 screening. No build or runtime test ran.

### #791: periphery history continuity, accepted adaptation

[Open Shaders #791](https://github.com/alandtse/open-shaders/pull/791),
`061cda2d1a8f635d3bef1bed80f3eba376371bd9`,
`fix(upscaling): reseed periphery history on resume`.

The user accepted an adaptation to CSX's existing per-eye implementation.
Replace the history-valid flag with a committed producer record, reusing
the existing temporal snapshot adjacency and resource-contract checks.
Main-pass history follows engine frames. Submit-stage history follows
the immutable producer snapshot, preserving compositor-cycle identity
when desktop Present advances the observed engine frame between eyes.

Both dispatch routes reseed history after a skipped producer or a changed
generation, method or input/output extent. Existing reset requests still
force reseeding. Publish the record only where both eye histories already
commit; failed or incomplete pairs leave the last complete producer
unchanged. All four resource-reset paths clear the record. No shaders,
GPU allocations, menu gates, settings or SE/AE dispatch paths change.

The existing `VRSubmitTemporalSnapshot` test target now includes committed
history cases for skipped and incomplete producers, contract changes,
peer-eye checks across Present, repeated producers, cycle wrap, invalid
keys and resource reset. Compiled execution is deferred with the build.
The implementation and qualification limits are recorded in the
[history continuity report](periphery-taa-history-continuity.md).

Validation completed without a build:

-   Scoped pre-commit whitespace, line-ending and Prettier hooks passed
    for the eight changed files. Clang-format 22.1.4 passed separately on
    changed lines in `Upscaling.cpp`/`.h` and on the complete policy and
    two test files; the whole-file upscaling format hook was skipped to
    preserve surrounding code. YAML/CMake hooks had no applicable files.
-   `pwsh ./tools/cmake.ps1 -D PROJECT_ROOT=C:/src/skyrim-community-shaders/build/worktrees/main-vr-feature-metadata -D OUTPUT_DIRECTORY=C:/src/skyrim-community-shaders/build/analysis/open-shaders-217-review-20260929/pr791-extraction -P tests/extract_foveated_save_reuse.cmake`
    passed source-fixture extraction. Separate `-D` value arguments avoid
    the joined-option parsing that truncated paths in earlier attempts.
-   `pwsh ./tools/git.ps1 diff --check` passed. A full source/test search
    found no remaining references to the replaced history-valid flag.

The existing resource-reuse fixture now uses the production history
record and checks both preservation and invalidation. Neither controller
target was compiled or executed; runtime and physical-HMD qualification
remain pending. No measured ledger or Build ID was created.

### #785: NMLFF compatibility conflict, accepted

[Open Shaders #785](https://github.com/alandtse/open-shaders/pull/785),
`d628f69960c956fc30b6bdddb2d83f50c8f5315a`,
`fix(compat): identify NMLFF conflict`.

The user accepted this port. Add `NativeMeshLightFlickerFix.dll` to the
existing incompatible-plugin list with upstream's explanation that Light
Limit Fix supersedes it and both replace the same lighting hooks. CSX's
LLF already owns lighting setup, and the list lacked this conflict.

Reuse the existing startup probe and diagnostic path for SE, AE and VR.
A detected incompatible plugin prevents CSX hook/feature initialization;
the conflict is not merely an informational notice. Do not add a second
loader check, change detection semantics or alter LLF rendering.
Source review confirmed that the existing consumer displays the supplied
reason. Builds and runtime validation remain deferred.

Validation: `pwsh ./tools/pre-commit.ps1 run --files src/Compatibility.h docs/development/open-shaders-dev-sync.md`
passed whitespace, line-ending, clang-format and Prettier hooks; YAML and
CMake hooks had no applicable files. `git diff --check` passed.

### #796: Grass Optimizations release metadata, deferred

[Open Shaders #796](https://github.com/alandtse/open-shaders/pull/796),
`c4172f49a374f083b164b3753eff01201f0c937b`,
`chore(grass): release Grass Optimizations`.

The complete diff only removes `Beta = True` from the Grass Optimizations
feature manifest. It falls under the standing GO deferral and contains
no useful change outside GO. No code or metadata is imported.

### #797: dynamic near-clip default, rejected

[Open Shaders #797](https://github.com/alandtse/open-shaders/pull/797),
`e210f6c2d0c58b7a56bb8e17a8b461f5c741a7f1`,
`chore(vr): disable dynamic near clip by default`.

The user rejected this PR. It changes upstream's `DynamicNearClip`
default from enabled to disabled and updates the corresponding example.
CSX `main-VR` has neither that near-clip controller nor its setting, so
there is no applicable local default to change. No code was imported.

### #767: tiered wind sampling API, excluded

[Open Shaders #767](https://github.com/alandtse/open-shaders/pull/767),
`3aa48d435daff1e28cf3892a03f5f1b46dd5a392`,
`feat(wind): add tiered wind sampling API`.

This falls under the standing exclusion of the new wind system. Its
shared public API edits expose that wind sampling; the implementation
and callers belong to the excluded system. The screening found no
independent applicable fix outside it. No code was imported.

Both decisions use the source comparison and upstream diffs from the
pinned 2.17 screening. No build or runtime test ran.

### #794: grass motion-vector alpha, accepted partial port

[Open Shaders #794](https://github.com/alandtse/open-shaders/pull/794),
`2ac27bd9906035e53f8f06dbf863763daa4db660`,
`feat(wind): refine grass flutter and gusts`.

The user accepted only the independent motion-vector alpha correction.
Both `RunGrass.hlsl` non-depth output layouts now declare `MotionVectors`
as `float4`, and both producers write the existing velocity in XY with
Z = 0 and alpha = 1. This supplies an explicit source alpha to the
deferred motion-vector blend state, which uses `SRC_ALPHA` and
`INV_SRC_ALPHA` whenever blending is enabled.

Cover both Grass Lighting and `RenderBasicGrass`, including the
runtime-disabled Grass Lighting path. Preserve the current/previous
position calculation, per-eye index, depth-only permutations and
render-target assignments across SE, AE and VR. No wind sampling,
flutter, gust, wind-history, GO or upstream UI changes are imported.

Validation: `pwsh ./tools/pre-commit.ps1 run --files package/Shaders/RunGrass.hlsl docs/development/open-shaders-dev-sync.md`
passed whitespace, line-ending, clang-format and Prettier hooks; YAML
and CMake hooks had no applicable files. `git diff --check` passed.
A source equality audit confirmed exactly two output declarations and
two writes changed, with every other shader byte unchanged after newline
normalization. Source review checked the shared basic-grass fallback,
both runtime branches and the existing source-alpha blend state. No
build, shader compilation or SE/AE/VR runtime validation ran.

The intervening upstream 2.16.0 release commit is bookkeeping only;
CSX release automation continues to own the local project version.

### #801: stale Streamline DX12 payloads, rejected

[Open Shaders #801](https://github.com/alandtse/open-shaders/pull/801),
`45a78d7aa364918a231d65bbc2df73646498e202`,
`build(upscaling): drop stale Streamline DX12 DLLs`.

The user rejected this PR. It removes six outdated tracked Streamline
DX12 DLLs and changes upstream's `dlssg-repro` tool to consume downloaded
runtime payloads. Those files and that tool are absent from `main-VR`,
which already stages downloaded runtimes through its build workflow.
No code, DLL or packaging change was imported.

### #802: editor and overlay input isolation, excluded

[Open Shaders #802](https://github.com/alandtse/open-shaders/pull/802),
`46036999dbd534ade24c11e15a0199b7aa56557a`,
`fix(ui): isolate editor and overlay input`.

The complete diff changes upstream's editor/overlay input handling,
including its preview-flying mode. It falls under the standing OS UI
exclusion. No implementation was imported.

These decisions use the complete diffs and source comparison from the
pinned 2.17 screening. No build or runtime test ran.

### #803: ambient effect lighting, accepted adapted port

[Open Shaders #803](https://github.com/alandtse/open-shaders/pull/803),
`4ec595f3f0310e05b242c0227f34eeabad093c7f`,
`feat(utility): add ambient lighting toggle`.

The user accepted the optional lighting mode in Adaptive Balance. Add a
default-off global `useAmbientEffectLighting` switch under
`Global > Lighting` and DevBench `set_adaptive_balance_visuals`. Saved settings,
global/full presets, performance-state capture and configured/effective
status include it. Existing master, loaded, menu, player-cell and performance
measurement gates neutralize the shader switch while retaining its selection.

In-world lit effects and classified or flagged sky statics can replace
weather lighting with ambient/IBL plus half-strength directional light.
Reuse local lighting-space, ambient balance, sky occlusion and shadow helpers;
retain material colours, lighting influence, point lights, fog and transparency.
The non-lit sky path keeps the upstream terrain/cloud shadow ray rather than
adding a scene shadow-mask dependency. VR uses eye-relative positions and
stable shadow noise. Existing bounded effect/sky-static brightness profile
composition drives the new mode without consuming the adjusted weather colours.

Reuse three padding slots in the existing 80-byte Adaptive Balance buffer;
all prior live-field offsets remain unchanged. Add the engine's existing
sky-object descriptor bit to the shader flags. No E11, EHF, upstream UI,
Scene Manager, wind, GO or NR implementation is imported. Unified presets
explicitly retain false and refresh their source fingerprints, preserving
compatible settings-contract revision 5.

Validation: production controller-fixture extraction, registered DevBench
JSON/schema and source-layout checks, and the unified preset generator
regression suite passed. Scoped pre-commit hooks and changed-line
clang-format 22.1.4 checks passed. Added controller gate/brightness/boolean
cases and updated shader-layout assertions; these compiled tests have not
run. DLL builds, shader compilation and SE/AE/VR runtime qualification
remain deferred to the final sync build by user instruction. Runtime
appearance and cost have not been measured.

### #804: grass profiler zone lifetime, rejected

[Open Shaders #804](https://github.com/alandtse/open-shaders/pull/804),
`2eedd983b3d315ea95fa08a9d89d00634f521c50`,
`fix(profiling): fix grass zone stack corruption`.

The user rejected this PR. Upstream's member-held grass profiler scope
spans multiple calls and violates Tracy's CPU-zone nesting requirements.
`main-VR` has neither `grassGpuPass` nor `UpdateGrassGpuPass`; its
`ScopedGpuPass` instances use local scopes. The new upstream `Spanning`
mode has no local consumer. No implementation was imported.

### #807: upstream post-processing output and presets, rejected

[Open Shaders #807](https://github.com/alandtse/open-shaders/pull/807),
`416f70f6d90593a893eaf1e51e2710b3747f3cdd`,
`fix(post-processing): retain VR output and presets`.

The user rejected this PR. Its resource sizing, full-resolution output
handoff and preset retention fixes belong to upstream's PostProcessing
pipeline. The shared Feature/Upscaling interface changes connect that
pipeline to upstream's provided-input PerfMode path. Neither that
PostProcessing implementation nor those provider interfaces exists in
`main-VR`; local render-scale/post-processing paths are separate. No
independent Adaptive Balance fix was found, and no code was imported.

### #810: grass directional-shadow availability, accepted partial port

[Open Shaders #810](https://github.com/alandtse/open-shaders/pull/810),
`c9e628dc5c96199c71d7a2a12fb7002ac69807a9`,
`fix(grass): restore PBR directional shadows`.

The user accepted the independent availability check for both local grass
paths. Replace four blanket interior exclusions in `RunGrass.hlsl` with
the existing `ShadowSampling::HasDirectionalShadows()` helper: shadow-mask
selection and the directional-detail block in both `RenderBasicGrass`
and Grass Lighting. Runtime-disabled Grass Lighting also uses the basic
path. The CPU already publishes this flag for exteriors and active
Interior Sun; ordinary interiors retain unshadowed directional light.

Preserve local shadow-mask sampling, scattering predicates, per-eye
positions and the existing world-shadow/caustics logic. No upstream
PBR-specific directional-shadow replacement or GO branch is imported.
No resources, settings or runtime-specific code paths are added.

Validation: a source comparison against the parent revision confirmed
exactly the four intended guard substitutions and no other shader edits.
The existing CPU-to-shader availability contract and basic-path fallback
were checked. Scoped pre-commit hooks and `git diff --check` passed.
Shader compilation, DLL builds and SE/AE/VR visual validation remain
deferred until the end of sync by user instruction.

### #809: Scene Manager tonemapping catalog, excluded

[Open Shaders #809](https://github.com/alandtse/open-shaders/pull/809),
`69c87160dd514c663e1dfa868876c1866841be99`,
`feat(scene-manager): expose tonemapping settings`.

This only exposes existing settings through the Scene Manager catalog,
policy and catalog tests. It adds no tonemapping math or independent
Adaptive Balance control. Excluded under the standing Scene Manager rule.

### #808: Scene Manager feature filter, excluded

[Open Shaders #808](https://github.com/alandtse/open-shaders/pull/808),
`b6f8ed809f37d8ea5d67339aff57acdf457330cf`,
`feat(scene-manager): filter scenes by feature`.

The scene filtering, translations and searchable-combo scroll restoration
support upstream's editor/Scene Manager UI. No independent renderer fix
was found in the shared UI helper changes. Excluded under the standing
Scene Manager, upstream UI and translation rules.

### #806: Tracy protocol 83, accepted adapted tooling port

[Open Shaders #806](https://github.com/alandtse/open-shaders/pull/806),
`ddeacbda6bc97cc3229f43d678e8200e824e4938`,
`build(deps): bump tracy vcpkg pin for protocol v83`.

The user accepted the dependency update with accurate version metadata and
matched capture/viewer tooling. Pin the overlay to
`a8db9bd8445343ee171439b9479c0a594183bf62` and its verified archive SHA-512;
both version declarations use `0.14.2-a8db9bd8`. The pinned source reports
version 0.14.2 and protocol 83, replacing protocol 82. The 21-commit range
also changes two client source files, so this is not a server-only update.

The existing overlay declared CLI/viewer features but neither applied its
tools patch nor selected its build options. Connect those features and
refresh the patch against the pinned source. A separate tools directory
owns shared dependencies with statistics and optional GUI support before
adding the individual applications. This prevents missing GUI targets,
duplicate common targets and profiler-only compiler flags leaking into
the client build. Copy the Release tools, including the capture daemon,
to the standard vcpkg tools directory and clean their package-bin copies.
Debug builds retain only the client.

The opt-in `tracy-tools` manifest feature selects both CLI and viewer tools;
the existing `ALL-TRACY` preset selects it alongside instrumentation.
Production presets remain unchanged. README and architecture instructions
describe the matched source, protocol and executable locations. Existing
installed tools/DLLs have not been replaced or rebuilt during this sync.

Validation: downloaded-source SHA-512 and patch application checks passed.
Version/preset checks and a non-building CMake audit exercised the real
port with vcpkg feature mapping for core-only, CLI-only, GUI-only and
combined selections. All four selected the expected Release tools and
disabled Debug tools. A vcpkg `install --dry-run` resolved the complete
matched configuration to `0.14.2-a8db9bd8` without building packages.
Scoped pre-commit hooks and `git diff --check` passed. DLL/tool builds,
live protocol compatibility and capture validation remain deferred until
the final build by user instruction.

### #814: upstream release-stage UI helpers, excluded

[Open Shaders #814](https://github.com/alandtse/open-shaders/pull/814),
`bd821a447b9c69b1826c31f71a978017941345e5`,
`feat(ui): add reusable release-stage tag helpers`.

This moves upstream feature-menu release-stage labels and colours into
shared UI helpers. No renderer behavior changes. Excluded under the
standing upstream UI rule.

### #811: pre-upscale depth of field and motion blur, rejected

[Open Shaders #811](https://github.com/alandtse/open-shaders/pull/811),
`951220acc4c190dc478c9f10cbaa4c05fedba429`,
`perf(post-processing): DoF and blur pre-upscale`.

The user selected **r**. All six changed files belong to upstream's custom
PostProcessing pipeline, including its DoF and MotionBlur components and
input-provider ownership. These components are absent from main-VR. No
independent correction applies to Adaptive Balance. No code imported.

### #815: destroyed grass-shape captures, deferred

[Open Shaders #815](https://github.com/alandtse/open-shaders/pull/815),
`404024aea2c2376343febf2fcc1084d837d2f98b`,
`fix(grass): drop captures of destroyed shapes`.

The change removes pending captures of destroyed shapes in GrassBucketStore.
Deferred under the standing Grass Optimizations rule.

### #723: upstream neural rendering, excluded

[Open Shaders #723](https://github.com/alandtse/open-shaders/pull/723),
`386bb211f8969dafe2a4a7cf641c5c4570fd4bcd`,
`feat(upscaling): add DLSS neural rendering`.

Excluded under the user's kevdev NR rule; CSX maintains its own implementation
on main-vr-nr. Shared colour, string, hashing, fence, swap-chain and upscaling
hunks were inspected separately from the new feature. They provide NR
integration, supporting utilities or behavior-preserving refactors; no
independently justified local correction was selected.

### #817: GO VR grass placement, deferred

[Open Shaders #817](https://github.com/alandtse/open-shaders/pull/817),
`8444f3c628feaa51e3de0415f83c29269ca08e04`,
`fix(grass-optimizations): VR grass placement`.

The RunGrass change corrects projection in GO's instanced vertex path.
The local shader does not use this InstanceExtras/instanceID branch.
Deferred under the standing Grass Optimizations rule.

### #816: GO runtime enable setting, deferred

[Open Shaders #816](https://github.com/alandtse/open-shaders/pull/816),
`b0f16fd4c292704c4f09591241eeaf5d687f8c7f`,
`feat(grass): add runtime Enabled setting`.

Runtime enable/disable, hook and bucket lifecycle changes belong to
GrassOptimizations, with accompanying translation updates. Deferred under
the standing Grass Optimizations rule.

### #821: conditional Scene Manager components, excluded

[Open Shaders #821](https://github.com/alandtse/open-shaders/pull/821),
`9f0bb80448dcd4e8ef4468b529f719451417cf1f`,
`fix(scene): discover conditional components`.

Only the scene-settings catalog generator and its tests change. This makes
conditional upstream PostProcessing components discoverable by Scene Manager;
it provides no independent Adaptive Balance correction. Excluded under the
standing Scene Manager rule.

### #819: celestial terrain-shadow direction, accepted partial port

[Open Shaders #819](https://github.com/alandtse/open-shaders/pull/819),
`ad261d852dd719c0f1c6a71b3c65cf7286b22366`,
`feat(sky): occlude sunset lighting and glare`.

The user accepted the terrain-shadow portion. Sky Sync now retains the
active caster's apparent direction before shadow elevation limits, then
exposes its normalized world-space direction. Capture follows time-jump
correction and precedes lighting and the end-of-frame caster handoff, so
terrain shadows follow the light actually used by the current fade phase.
The local fade-out/fade-in model, lighting intensity and horizon locks
remain intact; upstream's blended-direction transition is not imported.

The direction is cleared by ShadowFader reset and when there is no caster.
The accessor rejects unloaded/disabled Sky Sync, missing sky roots, zero
length and nonfinite directions. Existing invalid-sky, disabled-feature,
interior and worldspace lifecycle resets cover the retained direction.
Terrain Shadows falls back to its existing engine-light direction and
hemisphere correction whenever no valid celestial direction is available.
Only the fallback retains that correction; a valid celestial direction
keeps its actual horizon position.

Direction validation, sudden-change detection and shadow updates all use
the selected direction. This preserves immediate full refresh on caster
handoffs or other large direction changes, including changes hidden by
the engine light's elevation limit. The shared C++ path applies to SE, AE
and VR without new runtime-specific branches, settings or GPU resources.

The glare shader and vertex-stage depth binding are deliberately deferred.
They modulate a glare draw already being produced and do not establish
working glare in either VR or SE. The prior #733 decision remains intact.

Validation: the source audit verified unchanged fade/caster/calendar
calculations apart from direction capture and elevation-lock ordering,
capture before the current-frame handoff, reset and validity gates, and
the selected direction reaching both discontinuity detection and dispatch.
Scoped pre-commit hooks and `git diff --check` passed. No DLL build,
compiled tests or runtime rendering validation were run, per the user's
end-of-sync build instruction. Sunrise/sunset, sun/moon handoffs and
disable/re-enable behavior still require final runtime validation.

### #820: ACEScg skin tinting, rejected

[Open Shaders #820](https://github.com/alandtse/open-shaders/pull/820),
`1bdf8aab35a7ca60b69e5051e8df93c9ea0464ee`,
`fix(lighting): correct ACEScg skin tinting`.

The user selected **r** after correction of the initial recommendation.
Upstream converts face colour from AP1 to linear sRGB for gamma-space
face tinting, then restores its working gamut. Local Color::Diffuse only
performs gamma conversion and diffuse scaling; it never transforms this
base colour into AP1. Both local FACEGEN paths already use sRGB primaries.
No ACEScg/AP1 working-gamut implementation or independent local correction
was found. No code imported.

### Pinned 2.17 review complete

All 27 PR decisions are resolved: eight accepted ports, eight rejections
and eleven standing exclusions or deferrals. Release-only bookkeeping
ends the range at `74f95a4d7f52c5f61edf6267ebca813a94bbc71a` (2.17.0).
This is the pinned endpoint, not a claim about subsequent live upstream
changes. Accepted ports are on main-VR.

The user interrupted the preliminary final build and requested an
[adversarial review of all eight ports](open-shaders-217-adversarial-review.md)
before any further build. Configure completed and compilation/linking
began, but no successful final build or completed tests are claimed.
Three separate follow-up corrections cover Tracy revision headers,
zero-contribution ambient-effect work and periphery dispatch failure
propagation. Each commit body identifies its original implementation;
shader/DLL/tool builds and runtime/performance qualification remain pending.
