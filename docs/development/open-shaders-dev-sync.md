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

## Verification

-   Refreshed only Open Shaders `dev` and `main` with `--no-tags`; pinned
    the resulting review range and compared #733's complete shader diff.
-   Read #733 metadata through the GitHub CLI and checked local sky shader
    permutations and both extra-flag definitions.
-   `pwsh ./tools/dev-doctor.ps1 -Network`: zero failures after running
    outside the restricted sandbox; one existing public-HTTPS remote warning.
    Remote configuration was retained.
-   No build, shader compilation, deployment or runtime validation ran.
