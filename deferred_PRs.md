# Deferred PRs

This file tracks upstream PRs that are intentionally not picked into the
`cs-1.4.11-PL-VR` branch right now.

Keep this file updated when a PR is reviewed and deferred, skipped, or only
partially adapted.

## #1692 - HDR rendering

Status: deferred as-is.

Reason:
- Adds a large flat-screen HDR output pipeline, not a targeted VR fix.
- Upstream marks the new `HDR Display` feature as not VR-supported
  (`SupportsVR() == false`).
- Adds swapchain, present, UI composition, image-space, upscaling, and shader
  cache changes in areas that are already sensitive on this VR branch.
- Adds extra full-screen HDR/UI composite passes and full-resolution render
  targets when enabled, which would be expensive if accidentally active in VR.
- The original implementation was followed by multiple HDR/VR fixes upstream,
  so `#1692` alone is not a safe integration target.

If revisited:
- Treat HDR as a dedicated integration package, not as a simple cherry-pick.
- Keep HDR Display disabled/unsupported in VR unless the full VR render path is
  explicitly validated.
- Evaluate the required follow-up PRs together with the base feature:
  `#1988` water HDR TAA blend, `#2084` disable broken HDR ISTemporalAA in VR,
  `#2086` ISTemporalAA refactor/decompile, `#2098` VR desktop-window HDR fix,
  `#2126` ISHDR zero-average guard, `#2175` weather-editor HDR viewport guard,
  `#2186` false HDR display detection reduction, `#2187` exclusive-fullscreen
  HDR warning, `#2195` upscaling UI ghosting when HDR is unloaded, and `#2221`
  UI/black-screen fixes when HDR is unloaded.
- Re-check any non-PR HDR follow-up commits on the target upstream branch before
  implementation.

## #1708 - Exponential height fog

Status: deferred as-is.

Reason:
- Adds a new screen-space fog feature with per-pixel `exp2`, distance, and
  optional directional/cubemap work.
- VR doubles the screen-space cost across both eyes.
- The original implementation is enabled by default.
- Later upstream fixes changed important behavior, including disabling it by
  default and fixing a VR stereo mismatch.

If revisited:
- Bring it in only with the later VR/default-off fixes.
- Keep it optional for VR.
- Use a scoped commit subject such as
  `feat(fog): add exponential height fog (#1708)`.

## #1815 - Upscaling UI/background blur changes

Status: intentionally skipped.

Reason:
- Mainly affects UI/background blur behavior around upscaling.
- Does not improve normal in-game rendering for this VR branch.
- Touches low-level D3D12/UI render plumbing, so the risk is larger than the
  expected benefit.

If revisited:
- Only consider it if a specific UI/upscaling bug appears on this branch.

## #1837 - DLSS model preset selector

Status: intentionally skipped.

Reason:
- Adds DLSS model preset selection.
- This branch keeps the existing v1.4.11 upscaling behavior.
- Not needed for the current VR backport goal.

If revisited:
- Re-evaluate only if this branch intentionally updates DLSS behavior.

## #1859 - LLF settings removal

Status: not picked as-is.

Reason:
- Upstream removes broad LLF settings load/save behavior.
- This branch still has local LLF/placed-light settings that need persistence.
- The safe subset was adapted separately: remove only the unused
  `EnableContactShadows` setting.

If revisited:
- Do not apply the upstream PR directly unless LLF persistence has first been
  redesigned for this branch.

## #1874 - Volumetric shadows

Status: deferred as-is.

Reason:
- Adds a large core rendering feature, not a small fix.
- Adds shadow copy, downsample, and blur compute passes.
- Adds VSM shadow sampling into lighting, water, particles, effects, grass,
  distant trees, volumetric lighting, PBR, and hair paths.
- Original feature is effectively always loaded as core and has no user-facing
  off switch.
- VR is especially sensitive because added per-pixel shadow sampling runs over
  both eyes, and particles/effects are already fill-rate heavy.
- The PR predates later VR/HLSL fixes, so direct cherry-pick risk is high.

If revisited:
- Adapt behind an explicit setting, default off for VR.
- Scope shader defines to only shader types that need the feature.
- Skip the runtime shadow-copy work completely when disabled.
- Consider lower VR quality defaults.
- Use a scoped commit subject such as
  `feat(shadows): add volumetric shadows (#1874)`.

## #1885 - Shader compilation stop fix

Status: not picked as code.

Reason:
- The branch already has equivalent behavior covered by its current
  implementation.
- An empty marker commit records that the PR was reviewed and intentionally not
  cherry-picked as code.

If revisited:
- Re-check only if shader compilation stop behavior regresses.

## #1892 - EMAT fade/shadow tweaks

Status: deferred as-is.

Reason:
- Independent from `#1874` and `#1708`; not required by those features.
- Changes default `ExtendShadows` from `0` to `1`, which extends directional
  parallax shadows farther and can raise cost in EMAT-heavy scenes (especially
  in VR).
- Also changes EMAT shader behavior beyond the toggle:
  distance fade curve, parallax step scheduling, and soft-shadow shaping.
- These shader behavior changes can alter look/contrast and are not a clear
  win for this branch without targeted visual/perf validation.

If revisited:
- Prefer an adapted pick that keeps `ExtendShadows` default at `0`.
- Validate in VR with before/after captures in parallax-heavy scenes.
- If only chasing tiny ALU savings, consider a minimal local micro-change
  (for example `pow(x, 2.0)` to `x * x`) instead of the full PR.

## #1894 - Exclude HLSL tests from packaging

Status: already present in this branch.

Reason:
- Equivalent `#1894` packaging exclusions are already in `CMakeLists.txt` on
  this branch.
- Direct cherry-pick of `#1894` code commit is unnecessary.
- There is also an upstream no-op marker commit variant for branch histories.

## #1907 - Resolution based font

Status: deferred as-is.

Reason:
- UI/theme change only (theme JSON files and `ThemeManager` font behavior).
- Not required for current VR rendering/backport goals.
- Any UI scaling changes should be validated separately from rendering fixes.

If revisited:
- Pick together with a quick UI pass in VR menu and flat menu to confirm font
  size/readability behavior is acceptable.

## #1923 - VR resolution-based font sizing fix

Status: deferred as-is.

Reason:
- UI-only follow-up in `ThemeManager` for dynamic font size in VR.
- Changes default font-size source to VR overlay height when running in VR.
- Not required for current rendering/backport scope of this branch.
- Closely tied to the same font/UI path already deferred in `#1907`.

If revisited:
- Revisit together with `#1907` as one UI/font package.
- Validate readability and sizing consistency in both VR and non-VR menus.

## #1960 - Feature description text wrapping

Status: already picked on this branch.

Reason:
- This branch already contains commit `47669a76`
  (`fix(UI): feature description text wrapping (#1960)`), cherry-picked from
  upstream commit `b409a879`.
- The PR only changes `src/Menu/FeatureListRenderer.cpp` so feature
  descriptions use wrapped text instead of a single truncated line.
- No additional deferred action is needed unless this UI area is rewritten.

## #1961 - VR exponential height fog stereo mismatch

Status: deferred as not applicable on this branch right now.

Reason:
- `#1961` only patches `package/Shaders/ISSAOComposite.hlsl` inside the
  `EXP_HEIGHT_FOG` shader path.
- This branch does not currently include that exponential height fog path, so
  the fix has nothing to apply to.
- Direct pick now would be a no-op or conflict-prone without adding the base
  exponential fog implementation first.

If revisited:
- Revisit together with exponential height fog integration (`#1708` path).
- Apply the stereo UV conversion fix at the same time as the base feature.

## #1971 - PBR kD terms fix

Status: partially adapted.

Reason:
- Upstream `#1971` assumes the shadow-split light model introduced by `#1874`
  (`DirectContext.detailedShadow` / `softShadow` and
  `detailedLightColor` / `softLightColor` paths).
- This branch intentionally defers `#1874`, so those fields/paths are missing.
- Commit `398a1ceb` applies the kD correctness fixes while keeping this
  branch's current `context.lightColor` flow.

If revisited:
- If `#1874` is integrated later, re-compare this branch against upstream
  `#1971` and decide whether to switch to the detailed/soft light split.

## #1986 - Preserve vanilla water TAA when no upscaler is active

Status: already implemented locally.

Reason:
- This branch already contains equivalent logic in
  `src/Features/Upscaling.cpp` from local commit `755beeb0`
  (`fix(upscaling): preserve vanilla water TAA state when upscaler is none`).
- Direct cherry-pick of upstream `#1986` is unnecessary.

Follow-up on upstream:
- Upstream later modified the same area in `#1988`
  (`fix(water): hdr water taa blend`), which reworks `ConfigureTAA` behavior.
- Revisit `#1988` together with broader HDR/upscaling pipeline updates, not as
  an isolated pick.

## #1988 - HDR water TAA blend

Status: deferred as not applicable without HDR.

Reason:
- This PR is a follow-up to the HDR rendering path, not an independent VR fix.
- It changes water TAA history from the current log-encoded `float4` path with
  an alpha validity marker to a direct `R11G11B10_FLOAT` history path.
- This branch does not include `#1692` HDR rendering, so the current log-encoded
  water-history path remains the safer match for the branch.
- The upstream change also removes the no-upscaler early return in
  `Upscaling::ConfigureTAA()`, which would undo this branch's local `#1986`
  behavior that preserves vanilla water TAA when no upscaler is active.

If revisited:
- Revisit only as part of a full HDR integration package with `#1692`.
- Preserve or re-evaluate the local no-upscaler vanilla water TAA behavior at
  the same time.
- Validate water reflections/refraction with no upscaler, CS TAA, DLSS/FSR, and
  VR stereo before accepting the target-format change.

## #2002 - VR stereo reprojection

Status: deferred because this branch does not include the
`VRStereoOptimizations` feature line.

Reason:
- Adds a large experimental VR stereo reprojection/stencil-culling path, not a
  small compatibility fix.
- Adds the `VRStereoOptimizations` feature, new HLSL shaders, D3D11 stencil and
  render-target hooks, deferred-composite mode-texture plumbing, and POM depth
  correction data flow.
- Touches sensitive VR render ordering around deferred composite, stereo blend,
  motion vectors, depth/stencil state, and parallax/EMAT output.
- The first implementation was followed by multiple corrective upstream PRs, so
  `#2002` alone is not a safe integration target.
- Upstream later disabled the feature by default, which is a strong signal that
  it should not be enabled by default on this VR stability branch.
- Current performance expectation is uncertain: the path has an estimated fixed
  cost of about `0.6 ms`, and it is unclear whether saved Eye 1 shading reliably
  recovers that cost across real VR scenes.

Dependent/follow-up PRs:
- `#2061` removes the dead standalone reprojection compute path and routes the
  fill through the stereo blend overwrite path.
- `#2097` disables stereo reprojection and stereo blend by default and gates
  hooks/resources on the setting.
- `#2106` removes the fragile `Reflectance.w`/pixel-offset overload and replaces
  it with a dedicated POM offset texture plus render-target hook.
- `#2207` refactors `VRStereoOptimizations` settings loading and tooltip helpers.
- `#2209` names the stereo optimization D3D11 resources for RenderDoc/debugging.
- `#2217` adds Tracy profiling zones around stereo optimization dispatches.
- `#2218` consolidates the VR stereo settings UI and makes reprojection/debug
  controls part of the unified Stereo tab.

Related but not kept as a dependency:
- `#2150` also touched the deferred-composite/stereo optimization interface while
  optimizing deferred composite, but it was reverted by `#2232`; do not treat it
  as a required integration step.

If revisited:
- Treat `#2002` plus the follow-ups above as one dedicated VR reprojection
  integration package.
- Keep the feature default off unless current VR testing shows it is stable and
  worth the added render-state complexity.
- Measure net GPU frame time, not only isolated pass time; the integration only
  makes sense if reprojection savings consistently recover the roughly `0.6 ms`
  overhead.
- Reconcile the branch's existing stereo blend, SSGI stereo sync, dynamic
  resolution, terrain blending, and POM/EMAT paths before enabling stencil
  culling.
- Validate with OpenVR and OpenComposite, with and without upscaling, and with
  parallax/EMAT-heavy scenes before considering it shippable.

## #2009 - Weather editor full-screen background blur

Status: intentionally skipped.

Reason:
- Adds full-screen background blur/composite work for Weather Editor UI.
- This branch intentionally does not use background blur in VR or flat mode.
- Adds extra full-screen post-processing and render-target traffic for a UI
  effect, which is undesirable for VR frame-time stability.
- Touches sensitive shared menu/editor render paths with no direct in-game
  rendering benefit for this branch's scope.

If revisited:
- Revisit only if background blur is intentionally enabled as a branch feature.
- Keep VR default off unless profiling shows the cost is acceptable.

## #2012 - VR LLF cluster building/culling

Status: effectively covered; no additional implementation needed right now.

Reason:
- The critical VR fix from `#2012` is already present in this branch:
  right-eye clustered culling uses eye index `1` in
  `features/Light Limit Fix/Shaders/LightLimitFix/ClusterCullingCS.hlsl`.
- The remaining upstream C++ hunk only adds explicit `{"VR",""}` shader
  defines when compiling LLF cluster shaders.
- This branch's shader compiler already injects `VR` globally at compile time
  in VR mode (`Util::CompileShader`), so that extra LLF-local define plumbing
  is functionally redundant here.

If revisited:
- Keep as-is unless shader define injection policy changes.
- If compile policy is narrowed in future, re-apply the `#2012` LLF-local
  define wiring in `LightLimitFix::SetupResources()` and
  `LightLimitFix::ClearShaderCache()`.

## #2036 - UI background blur delay

Status: intentionally skipped as not applicable in current branch scope.

Reason:
- `#2036` is a targeted follow-up to background blur behavior in
  `src/Menu/BackgroundBlur.cpp` (switches from `WasActive` to `Active` to avoid
  a 1-frame delay when windows reappear).
- This branch currently keeps background blur out of scope (same rationale as
  deferred `#2009`), so this fix has no practical user-facing benefit right now.

If revisited:
- Revisit together with any future decision to enable background blur.
- If `#2009` (or equivalent blur path) is integrated later, `#2036` should be
  included as part of that package.

## #2053 - LL light compensate

Status: deferred as a side-branch integration, not part of current `dev`.

Reason:
- Upstream commit `50b94d2e` (`#2053`) is not contained in `upstream/dev` (or
  `origin/dev`) at this time.
- The commit is currently on side branches, notably:
  `upstream/effect11updated` and `upstream/enb-pp-updated-2`
  (also present on `upstream/ltm-nonsense`).
- It sits on the effect11/ENB line and bundles follow-up context from that
  branch family, so it is not a clean "stay-close-to-dev" pick for this branch.

If revisited:
- Re-evaluate when/if effect11/ENB work lands in upstream `dev`.
- Prefer picking the later dev-integrated form instead of this side-branch
  merge commit.

## #2055 - Update in-game links

Status: intentionally skipped; not picked because it does not apply cleanly to this fork.

Reason:
- Upstream `#2055` changes the upstream HomePage quick-link labels, wiki URL,
  and FAQ copy in `src/Menu/HomePageRenderer.cpp`.
- This fork has branch-specific HomePage/menu text and release-branch guidance,
  so taking the upstream wording directly would replace local UI intent rather
  than just bringing over an optimization.
- The PR is documentation/UI copy only; it is not required for runtime behavior.

If revisited:
- Manually compare the current fork HomePage links with upstream and only copy
  the external URL changes that still make sense for this fork.

## #2059 - Home page quick-links overflow

Status: intentionally skipped; not picked because it does not apply to this fork.

Reason:
- Upstream `#2059` targets quick-link/FAQ layout behavior in the upstream
  HomePage implementation.
- This fork uses a customized HomePage path where that upstream hunk does not
  apply, so cherry-pick resolved to no effective code change.
- The temporary empty marker commit was removed from history.

If revisited:
- Re-check only if this branch moves back to the upstream HomePage structure.

## #2061 - Remove dead reprojection code

Status: deferred as dependent on `#2002` (do not pick separately).

Reason:
- `#2061` only makes sense on top of the `#2002` VR stereo reprojection
  feature line (`VRStereoOptimizations` path).
- It removes/reworks the standalone reprojection compute route
  (`ReprojectionCS.hlsl`) and relies on the same stencil/mode-texture
  integration introduced in that package.
- Since `#2002` is deferred on this branch, `#2061` is also deferred.

If revisited:
- Revisit together with the full `#2002` integration package and its follow-up
  PR chain.

## #2076 - Volumetric shadows SharedShadowMap register

Status: not picked (not applicable as a branch delta right now).

Reason:
- `#2076` was merged into upstream `shadow-limit-fix` (not `upstream/dev`).
- The change is tied to volumetric-shadows integration context, while this
  branch intentionally defers volumetric shadows (`#1874`).
- The key register target from that PR is already aligned here:
  `package/Shaders/Common/ShadowSampling.hlsli` already binds
  `SharedShadowMap` at `register(t18)`.

If revisited:
- Re-check only if/when the deferred volumetric-shadows package (`#1874` line)
  is integrated.

## #2082 - Terrain blending transparency

Status: deferred as not applicable without `#2002`.

Reason:
- `#2082` fixes a transparency/regression path introduced by the stereo
  reprojection line.
- That issue origin (`#2002`) is intentionally deferred on this branch.
- Since the introducing path is not present here, this fix is not applicable as
  a branch delta right now.

If revisited:
- Revisit together with the `#2002` integration package.

## #2084 - Disable broken HDR ISTemporalAA in VR

Status: deferred as not applicable without HDR.

Reason:
- `#2084` is an HDR follow-up (`BSImagespaceShaderISTemporalAA`) targeting the
  HDR imagespace path in VR.
- This branch intentionally does not include HDR integration (`#1692`), so the
  underlying HDR path is not active here.
- Therefore there is no current branch delta to take from `#2084` on its own.

If revisited:
- Revisit only as part of a full HDR package (`#1692` + follow-ups).

## #2086 - ISTemporalAA decompile/refactor

Status: deferred; not a safe standalone pick on this branch.

Reason:
- `#2086` comes from the HDR follow-up line and performs a large rewrite of
  `ISTemporalAA` plus shared shader helpers.
- Even though parts are not strictly `#ifdef HDR_OUTPUT`, it changes core TAA
  behavior broadly (VR/SE paths), so taking it without the rest of the HDR
  package is high regression risk.
- This branch intentionally does not include HDR base integration (`#1692`),
  so the original HDR-driven motivation/path is still out of scope here.

If revisited:
- Revisit as part of the full HDR package (`#1692`) together with related
  `ISTemporalAA`/HDR VR follow-ups (`#2084`, `#2098`, `#2126`, and later fixes).

## #2090 - HDR sun rewrite

Status: deferred as not applicable without HDR.

Reason:
- `#2090` adds the HDR sun shader include and rewires sky/HDRDisplay sun output
  behavior.
- This branch intentionally does not include the HDR base integration (`#1692`),
  so the target `HDRDisplay` path is absent/out of scope.
- Taking the sky hunk alone would mix HDR-specific shader behavior into the
  non-HDR branch without the matching HDR pipeline.

If revisited:
- Revisit only as part of a full HDR package (`#1692` and follow-ups).

## #2098 - VR desktop-window HDR fix

Status: deferred as not applicable without HDR.

Reason:
- `#2098` patches `HDRDisplay` + Present/HDR compositing flow in VR.
- This branch does not include the HDR base feature (`#1692`) and does not
  contain `src/Features/HDRDisplay.cpp/.h`, so the target path is absent here.
- Therefore it is not an applicable standalone pick on this branch.

If revisited:
- Revisit only as part of the full HDR integration package (`#1692` and
  follow-up fixes).

## #2089 - Shader compile thread priority

Status: picked in this branch (tracking note; not deferred).

Reason:
- `#2089` is independent of the deferred `#2002` stereo-reprojection line.
- It sets shader compilation worker threads to `THREAD_PRIORITY_BELOW_NORMAL`
  to reduce scheduling contention during compilation.

## #2097 - Disable stereo optimizations by default

Status: deferred as dependent on `#2002` (do not pick separately).

Reason:
- `#2097` is explicitly in the `VRStereoOptimizations` path (stereo mode
  defaults, hook gating, stereo blend defaults, resource/setup gating).
- That path is part of the deferred stereo reprojection package rooted at
  `#2002`.
- Since `#2002` is not integrated on this branch, `#2097` is not a standalone
  branch delta to take right now.

If revisited:
- Revisit together with the full `#2002` integration package.

## #2106 - Remove pixeloffset overload

Status: deferred as dependent on `#2002` (do not pick separately).

Reason:
- `#2106` is a direct follow-up in the same `VRStereoOptimizations`/stereo
  reprojection line introduced by `#2002`.
- It rewires how reprojection reads POM data (removes
  `Reflectance.w` pixel-offset overloading, adds a dedicated POM offset
  texture/UAV/SRV path, and adds render-target hook/resource plumbing).
- Those touched paths only matter when the deferred stereo reprojection package
  is present; without `#2002`, this is not a standalone branch delta.

If revisited:
- Revisit together with the full `#2002` integration package and its follow-up
  chain.

## #2107 - Engine Fixes VR 7.x compatibility

Status: picked in this branch (tracking note; not deferred).

Reason:
- `#2107` is already present on this branch as commit `ddfbb802`
  (`fix(VR): allow for Engine Fixes VR 7.x (NG) (#2107)`).
- No additional cherry-pick is needed.

## #2126 - ISHDR zero-average guard

Status: deferred as not applicable without the newer HDR `ISHDR` path.

Reason:
- `#2126` guards `contrastedColorModified` against `avgValue.x == 0` in the
  newer HDR contrast block.
- This branch's current `package/Shaders/ISHDR.hlsl` does not contain that
  `contrastedColorModified` code path, so the exact upstream fix has nothing to
  patch here right now.
- Current branch code already guards the active divide in this shader
  (`avgValue.y / avgValue.x`) with a non-zero check.
- Direct pick now is likely conflict-prone and low value until the newer HDR
  `ISHDR` path is integrated.

If revisited:
- Revisit together with HDR package integration (`#1692` line) and
  `ISTemporalAA`/HDR follow-ups (`#2086`, `#2084`, `#2098`).
- Apply this guard immediately when the `contrastedColorModified` path is
  introduced.

## #2128 - VR overlay rendering desync

Status: review note added (not picked yet).

Reason:
- User note: branch owner suspects `#2128` is related to the deferred `#2002`
  stereo-reprojection line.
- Current technical read: `#2128` only changes
  `src/Features/VR/InSceneOverlay.cpp` (eye/head/world transform order for the
  in-scene overlay path), while `#2002` is the separate
  `VRStereoOptimizations`/stereo-reprojection package.

If revisited:
- Re-check behavior in both OpenVR and OpenComposite overlay modes.
- If taken, prefer as an independent VR overlay fix unless new evidence shows a
  direct dependency on `#2002`.

## #2131 - Exponential fog default off

Status: deferred as dependent on deferred base feature `#1708`.

Reason:
- `#2131` only changes `ExponentialHeightFog::Settings.enabled` default from
  `1` to `0`.
- This branch does not currently include exponential height fog (`#1708`), so
  this default-toggle fix has no active target here.
- It is effectively a follow-up policy fix for the deferred fog feature.

If revisited:
- Apply together with exponential height fog integration (`#1708`).
- Keep default off in VR unless explicitly validated and enabled by users.

## #2140 - LLF null light guard

Status: intentionally not picked (already covered in this branch).

Reason:
- Upstream `#2140` adds null guards for `bsLight`/`niLight`, dense strict-light
  packing, and unsigned shadow-mask bit writes in LLF setup.
- This branch already has equivalent or stricter protection in
  `src/Features/LightLimitFix.cpp`:
  `a_pass`/`sceneLights` guards, null checks in strict/shadow loops, dense
  `outIndex` packing, and `1u << maskIndex` handling.
- The branch LLF path also includes additional local logic (placed-light
  intensity handling and particle-light robustness), so a direct cherry-pick
  would add conflict/regression risk without a net functional gain.

If revisited:
- Re-check only if LLF null-light crashes or strict-light packing regressions
  appear.
- Prefer minimal local fixes over a direct upstream cherry-pick to preserve
  branch-specific particle-light behavior.

## #2142 - Skip wasted VR overlay rendering in game

Status: partially covered; keep as targeted local adaptation (do not raw cherry-pick).

Reason:
- This branch already has the stricter auto-hide/welcome predicate centralized
  via `ShouldShowAutoHideOverlay()` and already uses it in `DrawOverlay()` and
  `SubmitOverlayFrame()`.
- Remaining gap: `RenderInSceneOverlay()` still uses the older broad
  `settings.kAutoHideSeconds > 0` visibility condition and performs some work
  before the visibility/attach checks.

How to implement safely on this branch:
- Reuse existing `ShouldShowAutoHideOverlay()` (no new predicate helper needed).
- In `src/Features/VR/InSceneOverlay.cpp`, add an early top-of-function return
  before perf annotation/init work when all overlays are effectively inactive:
  require one of `menu enabled`, `overlayVisible`, or
  `ShouldShowAutoHideOverlay()` to proceed.
- Include `attachMode != None` and `menuTexture` availability in that same
  early gate.
- Remove redundant later checks that become unnecessary after the early gate.
- Keep current branch-specific VR/OpenComposite behavior and existing overlay
  positioning logic unchanged.

Expected result:
- Prevents wasted per-eye in-scene overlay rendering during normal gameplay.
- Low-risk perf/stability improvement, no interaction with LLF/particle-light
  systems.

## #2150 - DeferredComposite optimization

Status: deferred because this branch does not include
`VRStereoOptimizations`; linked to deferred `#2002` line and not kept as a
stable target.

Reason:
- `#2150` rewires DeferredComposite and directly touches
  `VRStereoOptimizations`/stencil-culling integration points.
- It depends on the same stereo-reprojection/culling feature family rooted at
  `#2002`, which is intentionally deferred on this branch.
- Upstream later reverted `#2150` with `#2232`, so this specific form is not a
  stable endpoint to integrate.

If revisited:
- Revisit only as part of a full `#2002` package decision.
- Base on the post-`#2232` upstream state, not on raw `#2150`.

## #2232 - Revert DeferredComposite #2150

Status: intentionally not picked on this branch.

Reason:
- `#2232` is an upstream revert of `#2150` (`revert: "perf: optimise
  DeferredComposite (#2150)"`).
- This branch already deferred `#2150` because it is tied to the deferred
  `#2002` stereo/deferred-composite line.
- Since the target change (`#2150`) is not present here, taking `#2232` would
  be a no-op at best and a risky mismatch against branch-local DeferredComposite
  edits at worst.

If revisited:
- Keep using post-`#2232` upstream as the reference baseline when/if the full
  `#2002` package is evaluated.

## #2156 - Screen Space Ray Tracing branch snapshot

Status: deferred as-is (do not pick into this branch).

Reason:
- Upstream `pull/2156/head` points to an older SSRT feature branch snapshot
  (`d0544c46`), not a small incremental fix in the current PR stream.
- It introduces a large new `ScreenSpaceRayTracing` feature package (specular +
  diffuse raymarching, denoiser passes, optional SHARC path), plus broad
  deferred/lighting integration changes.
- Scope is large (`upstream/dev...pr-2156`: ~26 files, ~3558 insertions), so
  integration risk is high on this VR-tuned branch.
- VR cost risk is high: multiple additional full-screen compute passes
  (depth prepass + Hi-Z + raymarch + temporal/variance/spatial filtering), all
  on top of existing SSGI/SSS/shadow workload.
- The branch history explicitly includes a VR-enable commit marked
  "haven't test at all", which is not an acceptable stability baseline for this
  branch.

If revisited:
- Treat as a dedicated feature-integration project, not a cherry-pick.
- Start from current upstream dev state for SSRT (or a later stabilized PR),
  not from `pull/2156/head`.
- Require targeted VR profiling and regression validation before enablement.

## #2163 - Terrain shadows half-res update path

Status: intentionally not picked.

Reason:
- `#2163` (`7661ba7e`) changes Terrain Shadows to use a half-resolution
  heightmap/update path for faster updates.
- Upstream `dev` quickly reverted this change in `#2172`
  (`b77e9e13`), which indicates the original optimization was not kept as the
  stable direction.
- While it can reduce Terrain Shadows update cost, it also lowers shadow detail
  and carries regression risk in this branch's Terrain Shadows path.

If revisited:
- Do not use raw `#2163` as the base.
- Revisit only via the post-`#2172` upstream state (or a later stabilized
  Terrain Shadows optimization set).

## #2170 - Add old Reflex DLL to incompatible list

Status: conceptually already implemented on this branch.

Reason:
- Upstream `#2170` adds `Data/SKSE/Plugins/NVIDIA_Reflex.dll` to the
  incompatible plugin blacklist in `src/XSEPlugin.cpp`.
- This branch already has that protection (plus `TAASharpen.dll`) from local
  commit `ab8f12cb` (`fix(xse): block Reflex and TAA Sharpen plugins`).
- Cherry-picking `#2170` now would be redundant and likely an empty/no-op pick.

## #2175 - Weather editor HDR viewport guard

Status: deferred as not applicable without HDR Display.

Reason:
- `#2175` disables Weather Editor viewport when `HDR Display` is enabled.
- This branch does not include HDR feature integration (`#1692`), and does not
  have `src/Features/HDRDisplay.h` or `globals::features::hdrDisplay`.
- Because the dependency path is absent, this PR has no applicable branch delta
  right now.

If revisited:
- Revisit together with HDR integration (`#1692` package).
- Keep this guard in the HDR package so Weather Editor viewport and blur state
  stay safe when HDR is active.

## #2178 - DeferredComposite texture rubberbanding

Status: deferred as not applicable on current branch path.

Reason:
- `#2178` targets the DeferredComposite texture path centered on
  `package/Shaders/DeferredCompositePS.hlsl`.
- This branch does not have that shader file/path (`DeferredCompositePS.hlsl`);
  it currently uses `package/Shaders/DeferredCompositeCS.hlsl`.
- The PR also removes Normal/NormalsSwap render-target hook plumbing tied to
  that same deferred-composite texture flow; taking only partial pieces here is
  not a safe standalone change.
- Practical dependency context: this sits on the newer DeferredComposite line
  associated with the deferred stereo/deferred-composite package (`#2002` /
  follow-up `#2150` family), which this branch intentionally defers.

If revisited:
- Revisit together with the full `#2002` package decision (and post-`#2232`
  DeferredComposite state), not as an isolated cherry-pick.

## #2186 - Reduce false HDR display detections

Status: deferred as not applicable without HDR Display.

Reason:
- `#2186` patches `src/Features/HDRDisplay.cpp/.h` to improve HDR monitor
  detection behavior.
- This branch intentionally does not include the HDR base integration
  (`#1692`) and does not contain `HDRDisplay` feature files in the current
  tree.
- Because that dependency path is absent, there is no standalone branch delta
  to apply from `#2186` right now.

If revisited:
- Revisit only as part of the full HDR package (`#1692` line).
- Integrate together with adjacent HDR detection/unload follow-ups
  (`#2187`, `#2195`, `#2221`) rather than as an isolated cherry-pick.

## #2187 - Warn when HDR runs in exclusive fullscreen

Status: deferred as not applicable without HDR Display.

Reason:
- `#2187` modifies `src/Features/HDRDisplay.cpp/.h` and `src/Hooks.cpp` to
  surface an exclusive-fullscreen HDR warning in the HDR UI flow.
- This branch intentionally does not include the HDR base integration
  (`#1692`) and does not have the `HDRDisplay` feature path in the working
  tree.
- Without that HDR feature path, the PR is not a meaningful standalone delta
  for this branch right now.

If revisited:
- Revisit only as part of the full HDR package (`#1692` line).
- Integrate with related HDR detection/unload follow-ups (`#2186`, `#2195`,
  `#2221`) so UI messaging and runtime behavior stay consistent.

## #2188 - CI release-branch config fix

Status: picked in this branch (tracking note; not deferred).

Reason:
- Implemented via cherry-pick commit `11f9f6e8`.
- This is CI/release automation scope only (`release.yaml`, `.releaserc.js`);
  no runtime rendering/VR behavior changes.

## #2193 - Reduce copy operations

Status: picked in this branch (tracking note; not deferred).

Reason:
- Implemented as an adapted branch integration (not raw cherry-pick) to match
  local `TerrainBlending` and `Upscaling/Streamline` deltas.
- Removes redundant copy paths and keeps intended behavior for VR and menu
  contexts.

Validation note:
- Terrain Blending behavior must be confirmed in-game after this change
  (depth blend and pass rendering still correct in interior/exterior scenes).

## #2195 - Upscaling UI ghosting when HDR is unloaded

Status: not applicable as an HDR delta on this branch; intent already covered.

Reason:
- Upstream `#2195` only removes the `hdrDisplay.loaded` guard and always calls
  `hdrDisplay.SetUIBuffer()` when `d3d12SwapChainActive` is true.
- This branch intentionally does not include the HDR package (`#1692` line),
  so the HDRDisplay integration path is not the active branch target.
- Current branch `Upscaling::PostDisplay()` already performs the equivalent
  intended behavior via `if (d3d12SwapChainActive) SetUIBuffer();`.

If revisited:
- Re-check only as part of full HDR integration (`#1692` package) and related
  unload/detection follow-ups (`#2186`, `#2187`, `#2221`).

## #2198 - Skip VSM shadows for grass SSS

Status: intentionally not picked; current branch is already conceptually aligned.

Reason:
- Upstream `#2198` targets an older grass shader path with VSM-specific symbols
  (`dirSoftShadow`, `dirVSMDetailedShadow`, `GetLightingShadow`) that this
  branch no longer has.
- This branch's current `RunGrass.hlsl` already uses the non-VSM directional
  shadow flow for grass SSS, so the intended behavior is already present.
- Direct patch application does not match current file context.

If revisited:
- Re-check only if grass/VSM shadow logic is reworked again in a later shader
  refactor.

## #2221 - HDR unload UI/blackscreen fixes

Status: partially implemented on this branch (non-HDR subset only).

Implemented:
- Took only the non-HDR swapchain safety subset in
  `src/Features/Upscaling.cpp`:
  force `DXGI_SWAP_EFFECT_FLIP_DISCARD` `BufferCount >= 2`, and normalize
  swapchain `_SRGB` formats to `_UNORM`.

Not implemented:
- `src/Features/HDRDisplay.cpp/.h` changes and HDR/FG UI routing logic from
  upstream `#2221`.

Reason:
- This branch intentionally does not include the HDR base integration
  (`#1692`), so the HDRDisplay path is out of scope.

## #2219 - SSS resolution-independent sample count

Status: partially implemented in this branch (shader-only subset).

Implemented:
- Took VR shader stability/correctness changes only in:
  `features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/RaymarchCS.hlsl`
  and
  `features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/bend_sss_gpu.hlsli`.
- Specifically:
  VR-only `UsePrecisionOffset = false`, corrected UV scaling order for
  `coord/coord_with_offset` with `DynamicRes`, explicit SBS per-eye UV handling,
  and `depth_thickness_scale` floor guard to avoid near-zero division behavior.

Intentionally not implemented:
- The C++ VR sample-count policy change in
  `src/Features/ScreenSpaceShadows.cpp` from `#2219` was not taken.
- Branch keeps current VR scaling/tuning path (`VRBaseSamplesAtReference` and
  existing dynamic sample logic) to avoid changing SSS performance envelope.

## #2229 - Exponential Height Fog vanilla fade toggle

Status: deferred as not applicable without Exponential Height Fog.

Reason:
- `#2229` adds an Exponential Height Fog setting and weather variable for
  respecting vanilla fog fade, extends the EHF shared settings buffer, and
  threads that behavior through the EHF shader.
- It also changes EHF integration points in `ISSAOComposite.hlsl` and
  `Water.hlsl`, so the PR is not an isolated UI toggle.
- This branch intentionally does not include Exponential Height Fog yet, so
  taking this PR would import feature-specific shader and buffer layout changes
  outside the current branch scope.

If revisited:
- Revisit only as part of a deliberate Exponential Height Fog integration.
- Apply together with the later EHF follow-ups, including `#2301`, so the
  shared buffer layout, water fog behavior, and vanilla-fade behavior are
  reviewed as one package.

## #2235 - Typed UAV loads and logging

Status: picked in this branch (adapted conflict resolution).

Validation note:
- Terrain Blending must be checked in-game after this pick to confirm depth
  blending still behaves correctly in interior/exterior scenes (no depth
  mismatch, flicker, or blend regression).

## #2244 - FidelityFX optiscaler / FSR 3.1.4 vendor update

Status: deferred; do not pick as-is.

Reason:
- Upstream `#2244` changes the FidelityFX submodule to
  `alandtse/FidelityFX-SDK-DX11` on `optiscaler-build`, updates packaged
  FidelityFX DLLs, deletes packaged PDBs, and switches host FSR creation to
  `ffxGetDeviceDX11_Fsr31`.
- This branch already uses a fork-specific FidelityFX submodule:
  `ParticleTroned/FidelityFX-SDK-DX11` on `fsr-3.1.5-dx11`.
- The branch also has later local FSR work for host FSR 3.1.5, runtime FSR4
  provider loading, runtime upscaler fallback/validation, and VR-safe
  per-eye handling.
- Picking `#2244` directly would risk downgrading/replacing that fork-specific
  FSR 3.1.5/runtime-FSR4 stack and would churn binary vendor files without a
  clear branch benefit.

If revisited:
- Reconcile the FidelityFX vendor strategy explicitly instead of cherry-picking
  this PR: compare the optiscaler-build changes against the current
  `fsr-3.1.5-dx11` branch and only port missing build/runtime fixes that still
  apply.

## #2271 - Restore UAV bind on vanilla normals targets

Status: not picked because the regression it fixes is not present on this
branch.

Reason:
- Upstream `#2271` restores `CreateRenderTarget_Normals` and
  `CreateRenderTarget_NormalsSwap` after `#2178` removed those hooks.
- This branch did not pick `#2178`, so the two normal-target hooks were never
  removed here.
- The branch also did not pick `#2150`, the DeferredComposite optimization that
  made the `#2178` hook removal look valid on upstream's temporary path.
- Current branch code still uses `DeferredCompositeCS`, still binds
  `normals.UAV`, and still installs both normal-target hooks, so the intended
  `#2271` fix is already covered by existing branch state.

If revisited:
- Only reconsider if a future DeferredComposite integration imports the
  `#2178`/`#2150` texture path or otherwise removes the normal-target UAV bind
  hooks.

## #2274 - HDR background blur fix

Status: deferred as not applicable without HDR/background blur integration.

Reason:
- `#2274` patches HDRDisplay, Upscaling/DX12 swapchain, BackgroundBlur, and
  shared state around HDR + background blur composition.
- This branch intentionally does not include the HDR base integration (`#1692`)
  and keeps background blur out of current VR scope.
- Taking this PR without HDR would either be a no-op or pull in HDR/blur
  plumbing that is intentionally excluded from this branch.

If revisited:
- Revisit only as part of a full HDR integration package, together with any
  deliberate decision to enable or validate background blur on this branch.

## #2286 - TruePBR aggressive MATO clearing fix

Status: already implemented in this branch.

Reason:
- `#2286` removes the fallback path that aggressively cleared PBR MATO data
  from geometries when no matching PBR config was found.
- This branch already has equivalent behavior in `src/TruePBR.cpp`; attempting
  to cherry-pick upstream `#2286` produced an empty pick.
- No marker commit was made because there was no remaining code delta to apply.

If revisited:
- Re-check only if TruePBR MATO ownership or fallback clearing behavior changes.

## #2287 - HDR Display Nexus metadata

Status: deferred as not applicable without HDR.

Reason:
- `#2287` only updates HDR Display feature metadata/config.
- This branch intentionally does not include the HDR Display feature, so there
  is no active feature package for the metadata to describe.

If revisited:
- Revisit only if HDR Display is integrated as a branch feature.

## #2289 - HDR peak nits slider maximum

Status: deferred as not applicable without HDR.

Reason:
- `#2289` only changes HDRDisplay UI behavior by increasing the maximum peak
  nits slider value to 10000.
- This branch intentionally does not include the HDR Display feature, so there
  is no active HDR UI for this change to apply to.

If revisited:
- Revisit only if HDR Display is integrated as a branch feature.

## #2291 - Upscaling slider toggle replacement

Status: intentionally skipped.

Reason:
- `#2291` changes the Upscaling UI toggle controls from sliders to replacement
  controls.
- This branch's current Upscaling UI is clearer/better for the branch's
  combined VR, foveated rendering, frame generation, Reflex, and runtime FSR
  settings.
- Taking the PR would churn UI behavior without improving the current branch
  goal of preserving stable VR-focused upscaling controls.

If revisited:
- Re-evaluate only as part of a deliberate Upscaling UI redesign, not as a
  mechanical cherry-pick.

## #2297 - ISHDR bloom gate for legacy weather mods

Status: deferred as not applicable to this branch's current ISHDR path.

Reason:
- `#2297` gates the newer upstream ISHDR bloom expression so SDR keeps the
  legacy hard cutoff (`Param.x - blendedColor`) while HDR keeps the newer
  soft-saturation form.
- This branch does not include the HDR Display/ISHDR path that introduced the
  soft-saturation expression.
- The current active branch code in `package/Shaders/ISHDR.hlsl` already uses
  the legacy SDR cutoff:
  `blendedColor += saturate(Param.x - blendedColor) * bloomColor;`.
- Directly picking `#2297` would target missing HDR-era context rather than
  add a useful branch delta.

If revisited:
- Revisit only if the newer HDR/ISHDR pipeline is integrated.
- Preserve the legacy SDR cutoff for non-HDR weather compatibility when that
  path is added.

## #2301 - Exponential Height Fog inscattering color settings

Status: deferred as not applicable without Exponential Height Fog.

Reason:
- `#2301` is an Exponential Height Fog feature update: it separates fog
  inscattering color from vanilla fog color, replaces the directional
  inscattering exponent with Henyey-Greenstein anisotropy, and adds sunlight
  attenuation plus a disable-vanilla-fog control.
- It also changes shared shader data and EHF integration points in
  `Effect.hlsl`, `ISSAOComposite.hlsl`, `Lighting.hlsl`, and `Water.hlsl`.
- This branch intentionally does not include Exponential Height Fog yet, so
  picking this PR would import feature plumbing and shader behavior outside the
  current branch scope.

If revisited:
- Revisit only as part of a deliberate Exponential Height Fog integration.
- Audit the shared buffer layout plus water/fog interactions against this
  branch's VR and water changes instead of applying it as a mechanical
  cherry-pick.

## #2309 - Upscaling depth copy condition revert

Status: intentionally not picked; branch already reversed this path.

Reason:
- Upstream `#2309` reverts the depth-copy condition in
  `Upscaling::UpscaleDepth()` so `kMAIN -> kMAIN_COPY` is refreshed
  unconditionally before the depth upscale pass.
- This branch already tried the equivalent unconditional refresh in local
  commit `912ea27a` (`fix(upscaling): refresh scene depth before upscale pass`)
  and then intentionally reversed it with `2a95b1d0`
  (`Revert "fix(upscaling): refresh scene depth before upscale pass"`).
- Current branch code keeps the branch-preserving behavior: during active depth
  upscaling it only refreshes `depthCopy` in menu/non-3D contexts where the
  engine may skip the normal copy, while the full-resolution underwater-mask
  path still refreshes the depth source when that path is active.
- Picking `#2309` would undo that local reversal and restore the unconditional
  copy behavior this branch already backed out.

If revisited:
- Re-test map menu depth, pause/main/loading menus, underwater mask behavior,
  and VR depth/stencil propagation before changing this copy policy again.

## #2313 - duplicate/unmerged TruePBR MATO cbuffer PR head

Status: not picked because it is not merged into `upstream/dev`.

Reason:
- Current `upstream/dev` has no merged commit carrying `#2313`.
- The PR head exists at `upstream/pr/2313`, but its head commit is another
  `fix(truepbr): apply MATO rbg scalars through cbuffer (#2310)` revision and
  is not contained in `upstream/dev`.
- Per branch rule, only PRs merged into `dev` are eligible for picking.

If revisited:
- Re-check only if `#2313` is later merged into `dev`; otherwise use the
  actual merged `#2310` commit for the MATO cbuffer fix.

## #2319 - Interior Sun volumetric shadows compatibility

Status: deferred as not applicable without Volumetric Shadows.

Reason:
- `#2319` is explicitly a Volumetric Shadows compatibility fix for Interior Sun.
- It changes `VolumetricShadows.cpp/.h`, Volumetric Shadows feature metadata,
  shared shader data, `ShadowSampling.hlsli`, `Effect.hlsl`, `Lighting.hlsl`,
  and state plumbing for volumetric-shadow parameters.
- This branch intentionally does not include Volumetric Shadows yet, so picking
  this PR would import feature integration code outside the current branch
  scope.

If revisited:
- Revisit only as part of a deliberate Volumetric Shadows integration.
- Re-audit Interior Sun and shadow-sampling interactions against this branch's
  existing water, VR, and lighting changes when that feature is added.

## #2326 - unmerged duplicate Volumetric Shadows compatibility PR head

Status: not picked because it is not merged into `upstream/dev`.

Reason:
- Current `upstream/dev` has no merged commit carrying `#2326`, and upstream
  exposes only `refs/pull/2326/head`, not a merge ref.
- The PR head currently points at `fix(interior sun): volumetric shadows
  compatibility (#2319)`.
- That underlying change is already deferred above because this branch does not
  include Volumetric Shadows yet.

If revisited:
- Re-check only if `#2326` is later merged into `dev`.
- Otherwise treat it together with `#2319` as part of a deliberate Volumetric
  Shadows integration.
