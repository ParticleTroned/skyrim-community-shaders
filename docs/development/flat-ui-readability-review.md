# Flat UI readability: source review

Reviewed on 2026-09-29. This is a source comparison, not a rendered UI
comparison or a build/test result.

## Scope and references

-   Working branch: `codex/flat-ui-readability`.
-   Main-VR base: `dab1874a76fd39175dcefdc52110ba69d7284e12`.
-   Local `cs-1.7-PL-SE`: `734435e158c91438812ff96314645f2c69a8b8db`.
-   Cached `origin/cs-1.7-PL-SE`:
    `d438b51738da787f45c49013f5659a0a4326c963`.

Both SE references use the same font, style and search baselines and
switch geometry relevant to these corrections. The changes affect flat
SE/AE presentation. GPU load, shaders, feature defaults and VR presentation
are outside this change.

## Findings and corrections

| Finding                                                                                                  | Correction                                                                                                          |
| -------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| Automatic flat fonts were 27/36/54 px at 1080p/1440p/2160p, versus SE's 21/28/42 px.                     | Reuse the existing 21 px layout baseline for flat automatic sizing and the missing-resolution fallback.             |
| Reducing fonts alone left style geometry scaled against 27 px, making spacing about 22% tighter than SE. | Use the same 21 px baseline for flat style scaling; retain VR's 27 px baseline.                                     |
| Search controls used a 21 px reference instead of SE's 28 px reference.                                  | Restore the flat 1440p search baseline through the existing shared search-scale helper.                             |
| Switch tracks had little on/off contrast and a compact, squarer shape.                                   | Use theme accent/neutral blends, SE's frame-relative dimensions, rounded ends, 2 px knob padding and 1.5 px border. |
| Custom switch drawing bypassed ImGui's disabled opacity.                                                 | Apply `GetColorU32` to flat track, border and knob colours.                                                         |
| Invisible switches lacked the SE button's keyboard navigation.                                           | Enable ImGui's existing navigation handling for flat.                                                               |

The review also removed a redundant explicit focus-outline draw from the
draft: the pinned ImGui 1.92.6 `InvisibleButton` already renders it. See the
[upstream implementation](https://github.com/ocornut/imgui/blob/v1.92.6/imgui_widgets.cpp#L771-L791).

## Adversarial checks

-   **Scope:** VR retains its canvas-based font calculation, font/style
    baselines, search scale, switch dimensions, colours, opacity packing and
    interaction flags. SE and AE share the flat path.
-   **Correctness:** Font selection, fixed-size settings, font clamps and
    global scaling remain active. At default scale, the flat font/style and
    search formulas match the SE reference. Persisted settings are not reset.
-   **Robustness:** Theme-derived colours work with the inspected Default
    and Light palettes; disabled opacity is applied once. All current switch
    callers use automatic sizing and ImGui layout rather than a hardcoded
    reservation for the old width. Explicit caller sizes remain supported.
-   **DRY:** The implementation reuses `Util::kBaselineFontSize`,
    `GetUIScaleForBaseline`, `GetSearchUIScale`, `Color::Blend` and the shared
    `FeatureToggle`. It adds no separate flat renderer or theme copy. The
    search helper covers feature search, combo search and the colour filter.
-   **State ownership:** No additional ImGui style stack or DirectX resource
    mutation was introduced. The switch retains its existing balanced ID
    scope and uses ImGui for input handling.
-   **Existing checks:** The source-extraction boundaries and available
    declarations in `menu_frame_stability_test.cpp` remain compatible on
    inspection. That test was not executed.

No remaining blocking defect introduced by this diff was identified in
source review. Actual appearance and input behavior remain unverified.
The newer main-VR header arrangement, feature list and panel backgrounds
remain; this aligns typography, spacing and controls rather than promising
an identical screenshot of SE 1.7.

## Validation evidence

-   `pwsh ./tools/git.ps1 diff --check`: passed for the code changes.
-   Source comparison and caller/API review: completed as described above.
-   Builds, automated tests, game launches and visual checks: not run, as
    requested by the user.
