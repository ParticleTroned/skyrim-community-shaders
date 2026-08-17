# Controlled tier-motion screen — 2026-08-17

## Scope and provenance

This screen extends the first accepted null-HMD player-translation proof to four settings that materially affect the provisional tiers. It uses the AMD native OpenVR → SteamVR null-HMD lane (`SVR-OVR-NULL`), Info logging, the fixed Guardian Stones anchors, and the Release+DevBench DLL from commit `d0682bf57` with SHA-256 `1287E6DE6A0795A1E63FCCDE17CFCC86D463B0D9FF2438866C5DCB0207240CF4`. The generalized capture harness is commit `f0349acb5`.

Every accepted phase:

- reconstructed and read back the declared cell, worldspace, time, weather, position, and submitted-view yaw;
- captured 30 separate-eye BMP pairs at one pair per three compositor cycles;
- scheduled one typed Papyrus player translation after ten requested capture samples and aligned analysis to the observed image event;
- required exact effective control readback and readiness; and
- restored the original feature snapshot, player position, HUD, and timescale.

The stable campaign locator is `M/<run id>` beneath `L:\CSX Preset Automation\Sessions\2026-08-17\CSX Baselines`. `M` is intentionally short because the first full-label capture produced a 272-character temporary-manifest path and failed before saving any frame. A shorter independent D: NVMe staging path succeeded. The stopped-runtime finaliser then moved 6.814 GiB / 740 accepted-run files to L:. D: staging, SkyrimVR, and MO2 overwrite retained no session evidence.

## Results

The tail is the final five aligned captured samples. `Effect/drift` is the mean-absolute baseline-versus-ablation residual divided by the baseline-before versus baseline-return residual. Recovery is the first three-sample window within 10% of the final tail magnitude; it describes convergence of this abrupt position step, not a physical-HMD reprojection bound.

| Run | Controlled comparison | Motion | Event frames B/A/B | Recovery | Tail effect L/R | Tail drift L/R | Effect/drift L/R |
| --- | --- | ---: | --- | ---: | --- | --- | --- |
| `sky-gc-qp-m1` | Skylighting Quality / Performance / Quality | +32 X, clear 14:00 | 7 / 10 / 7 | 5 samples, 166.7 ms | 3.747 / 3.719 | 7.138 / 7.069 | 0.525 / 0.526 |
| `sss-gc-range-m1` | SSS cull 0 / 20,480 / 0; reference samples held at 44, compiled 33/33 | +64 X, clear 14:00 | 7 / 9 / 9 | 3 samples, 100.0 ms | 5.197 / 5.165 | 3.483 / 3.512 | 1.492 / 1.471 |
| `wet-gs-qp-m1` | Wetterness Quality / Performance / Quality | +32 X, storm 14:00 | 7 / 6 / 6 | 2 samples, 66.7 ms | 4.997 / 4.984 | 6.877 / 6.872 | 0.727 / 0.725 |
| `vl-gf-hl-m1` | Volumetric Lighting exterior High / Low / High; interior held High | +32 X, fog 07:00 | 9 / 9 / 9 | 14 samples, 466.7 ms | 2.886 / 2.874 | 3.680 / 3.645 | 0.784 / 0.788 |

Artifact identities:

| Run | Run-record SHA-256 | Analysis SHA-256 |
| --- | --- | --- |
| `sky-gc-qp-m1` | `61550EF9BE0CDB222DE3FBF48B01D7D1A013E3A57D33076441FBC09C74814C24` | `40484F4FE54C85EECD3B6F20946421F81A650CA88DDE04B77EFB19622A1A6CAC` |
| `sss-gc-range-m1` | `BC4F5B4EB66584EAA6C8B9FD08C2A3B82261DB960ECA81B56324311B64BF9FC7` | `383ED9FD6799BE84497F9FE09424570AB12F42ED2CBF62FF985A5C2E78F71E67` |
| `wet-gs-qp-m1` | `B29A074FB4B153B95AA07E2268F1401D66771301F9D5A9D16997CBACA7DF53E9` | `38C2456DEFB037F638CCDE1EEF51850965EA842FAE0C605E18687A10DE6B3A8E` |
| `vl-gf-hl-m1` | `2694E4108E1021CA6D53FCD5A7612BF8D2702269AE02F07EE21960E95D0E7E8C` | `7E861CAFDF1D26CA2CE03C46367264BA700E5DB9C3801A340B5C16F5C3219677` |

## Interpretation

Skylighting reached the same settled response in both eyes without a gross persistent trail, but the Quality-versus-Performance residual was only about half the returning-Quality drift. This qualifies one abrupt translation and supports retaining the measured cost-based gradient; it does not establish a stable perceptual ordering or continuous-view stability.

SSS range is the only comparison whose settled residual exceeded baseline drift. Unlimited range was locally darker than the 20,480-unit state by `-0.150/-0.183` signed mean luma while mean-absolute response was `5.197/5.165`; about 52% of sampled pixels exceeded one luma in the tail. Both eyes agreed closely and converged within about 100 ms. This strengthens the Quality unlimited-range candidate. It still does not locate scene geometry in the 19,280–20,480-unit fade band, and the position step is not continuous rotation/disocclusion.

Wetterness converged rapidly and showed close eye agreement, with no evidence of a gross persistent state trail after the controlled step. Returning-Quality storm/material drift remained larger than the tier residual, so the earlier paired static storm evidence remains the basis for perceptual tier separation. Continuous precipitation, a known near/far material target, and physical-head motion remain open.

Volumetric Lighting required about 467 ms to enter its final tail in both eyes, substantially longer than the other three comparisons. The High-versus-Low tail remained below High-return drift and had approximately zero signed whole-frame difference, so this is a temporal caution rather than evidence of a visible High advantage. A reproducible strong-shaft/dense-fog target and continuous or rotating motion remain required.

## Policy consequence

No tier value changes are justified. Retain the existing Skylighting, SSS, Wetterness, and Volumetric Lighting gradients. Raise confidence in SSS unlimited range as a real moving-scene distinction, record bounded null-HMD step recovery for all four systems, and narrow the remaining work to known-distance targets, continuous/rotating motion, physical HMD, alternate runtimes, and NVIDIA portability.

