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
