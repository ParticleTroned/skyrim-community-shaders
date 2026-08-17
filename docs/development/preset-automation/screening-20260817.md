# First-pass feature screening — 2026-08-17

Status: accepted source evidence; sufficient for response-curve routing, not final preset selection

## Scope and identity

This screen used an AMD GPU in the native OpenVR → SteamVR null-HMD lane (`SVR-OVR-NULL`), Skyrim VR, Info logging, and the Release+DevBench bridge from commit `9218ec2e8`:

- DLL SHA-256: `03E8062D9401F03DBD0190E9EA119F3EAC8BD5D01FDF1433B1D0D5362D1E7B8D`;
- timing root: `D:\Games\Skyrim\MadGod2\overwrite\Root\CSX Baselines\preset-automation-screening`;
- visual root: `D:\Games\Skyrim\MadGod2\overwrite\Root\CSX Baselines\preset-automation-visual`.

The timing runner collected an A/B/A sequence with 120 unique, resolved, post-arm profiler samples in every phase. Capture was disabled during timing. The visual runner separately collected temporal A/B/A sequences from the production HMD submission path and required exact frame counts, zero failed or incomplete stereo pairs, zero backpressure drops, effective-state readback, and exact in-memory baseline restoration.

The paired aggregate estimate below is:

```text
((baseline-before mean + baseline-return mean) / 2) - ablated mean
```

It is a screening signal, not automatically the isolated feature cost. When enabled-to-enabled drift is comparable to the estimate, the result remains unresolved until repeated or supported by a direct timer.

## Aggregate timing screen

All values are milliseconds. Every listed capture passed its sample and restoration gates.

| Feature / anchor | Enabled A1 | Ablated | Enabled A2 | Paired GPU estimate | A1↔A2 drift | CPU estimate | First interpretation |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Light Limit Fix / Dragonsreach | 4.201199 | 4.114900 | 4.153481 | 0.062440 | 0.047718 | 0.009465 | Marginal aggregate signal; direct timers did not fall when ablated |
| Subsurface Scattering / Dragonsreach | 4.153023 | 4.138582 | 4.169973 | 0.022917 | 0.016950 | -0.003667 | Small plausible GPU cost; requires a stable close character anchor |
| Volumetric Lighting / Dragonsreach | 4.167072 | 4.102338 | 4.094452 | 0.028424 | 0.072620 | 0.006011 | Unresolved; drift dominates and the named passes were inactive |
| Grass Collision / Guardian Stones clear | 4.887518 | 4.796993 | 4.835108 | 0.064320 | 0.052410 | -0.007135 | Static null pose does not exercise collision work adequately |
| Grass Lighting / Guardian Stones clear | 4.872933 | 4.820662 | 4.853785 | 0.042697 | 0.019148 | 0.011605 | Small directionally consistent signal; no dedicated timer |
| Skylighting / Guardian Stones clear | 4.762863 | 4.569056 | 4.782621 | 0.203686 | 0.019757 | 0.188887 | Clear first-pass signal; intermittent probe/occlusion work matters |
| Screen Space Shadows / Guardian Stones clear | 4.772900 | 4.714448 | 4.786984 | 0.065495 | 0.014084 | 0.049778 | Plausible small cost, supported by direct per-eye timers |
| Terrain Blending / Guardian Stones clear | 4.769363 | 4.768208 | 4.840222 | 0.036585 | 0.070859 | -0.025085 | Aggregate unresolved; direct depth-blend pass is stable and small |
| Wetterness / Guardian Stones storm | 4.882512 | 4.818216 | 4.928432 | 0.087256 | 0.045920 | 0.056513 | Conditional cost is plausible but needs a repeat/response curve |

The only aggregate GPU difference clearly above baseline drift in this first set is Skylighting. That does not make the other features free: direct timers expose smaller work, some effects are intermittent, and the static scenes do not exercise every path.

## Direct timer cadence

`tools/analyze-profiler-ablation.py` sums matching top-level GPU timers per accepted sample and retains both the mean across all samples and the mean when non-zero. The cadence is important: averaging an intermittent update only over all frames hides the cost of the frames on which it runs.

| Feature | Enabled mean across all samples | Mean when active | Active fraction | Ablated mean | Interpretation |
| --- | ---: | ---: | ---: | ---: | --- |
| Subsurface Scattering | 0.0337–0.0356 | 0.0340–0.0356 | 99–100% | 0 | Small continuous Burley pass |
| Skylighting | 0.0727–0.0824 | 0.3488–0.3661 | 20.8–22.5% | 0 | Expensive intermittent occlusion/probe updates; tier intervals are meaningful |
| Screen Space Shadows | 0.0132–0.0136 | 0.0442–0.0481 | 27.5–30.8% | 0 | Small per-eye ray-march work with measured cadence |
| Terrain Blending | 0.02046–0.02059 | 0.02046–0.02059 | 100% | 0 | Stable continuous depth-blend floor; distance response still unmeasured |
| Grass Collision | 0.000001–0.000016 | 0.00004–0.00032 | 2.5–5% | 0 | Confirms that the static scene barely exercises the path |
| Light Limit Fix | 0.0422–0.0425 | 0.0422–0.0425 | 100% | 0.0423 | Its ablation does not remove the named bookkeeping passes; aggregate delta is not attributable to them |
| Volumetric Lighting | 0 | 0 | 0% | 0 | Dragonsreach did not activate these named passes during this run |

Grass Lighting and Wetterness expose no dedicated matching profiler timer in this build. They require aggregate paired measurement, shader/code accounting, or new instrumentation.

## Temporal visual screen

The accepted outdoor visual runs use 60 combined stereo BMP frames per phase at one accepted pair every four compositor cycles. The earlier Skylighting `r01` attempt is retained as rejected evidence because its off phase reported one backpressure drop; `r02` is the accepted rerun. The Dragonsreach Volumetric Lighting run used the previously calibrated three-cycle cadence and passed.

`tools/analyze-visual-ablation.py` forms a temporal median for each phase, compares the off median with the average of both enabled medians, and separately measures enabled-return drift. Metrics below are the range across left and right eyes in 8-bit luma units. “Above drift” is the fraction whose effect exceeds both 2 luma and twice the local enabled-return drift.

| Feature / anchor | Mean absolute effect | Enabled-return drift | Off minus enabled | Pixels above drift | Visual essence in this scene |
| --- | ---: | ---: | ---: | ---: | --- |
| Skylighting / clear day | 5.43–5.44 | 3.30–3.31 | +0.95–1.01 | 26.7–26.9% | Broad, low-frequency redistribution of ambient light across shaded terrain and vegetation |
| Screen Space Shadows / clear day | 6.06 | 2.56–2.58 | +1.01–1.10 | 32.2–32.5% | Local contact/relief shadow around foliage, stone detail, intersections, and creases; sky remains unchanged |
| Wetterness / storm | 9.40–9.42 | 2.05–2.10 | +7.28–7.34 | 53.2–53.5% | Strong weather-conditioned darkening and material-response change across exposed stone, earth, bark, and foliage |
| Volumetric Lighting / Dragonsreach | 0.261–0.264 | 0.191–0.196 | -0.087 to -0.099 | 0.74–0.81% | Negligible in this anchor; no basis for a tier decision from this scene |

Wetterness is the strongest perceptual change in this screen and is intentionally weather-conditional. Screen Space Shadows contributes comparatively localized depth cues. Skylighting affects a much broader spatial field and also carries the largest measured scheduling cost. Volumetric Lighting needs a different anchor with an unquestionably active ray/fog path; a global disable decision from this null result would be invalid.

## Artifact hashes

Each path is `<timing root>/<run id>/ablation-profiler-raw.json`.

| Run id | SHA-256 |
| --- | --- |
| `20260817-amd-svr-ovr-null-dragonsreach-info-lightlimitfix-ablation-r01` | `09BB52E9936FEC9B27BA83C69F065F3C490A0A87F9CA2585F9576CEA50DBEFD2` |
| `20260817-amd-svr-ovr-null-dragonsreach-info-sss-subsurface-ablation-r01` | `9AB8502DAC3FF23C6699003155BEFE6EF9DE9D4E5036D4C742FE370B5A7F762C` |
| `20260817-amd-svr-ovr-null-dragonsreach-info-volumetriclighting-ablation-r01` | `AD2A1269E3757AE58D6F2FEF2E0CBB158A33AE14FF5CF2B0B4960108A237FF07` |
| `20260817-amd-svr-ovr-null-guardian-clear-info-grasscollision-ablation-r01` | `D87D5B7ECAAB6E9DC2B50A851623BC1EBA3DBED5D36B84686C519CAA4193C9C0` |
| `20260817-amd-svr-ovr-null-guardian-clear-info-grasslighting-ablation-r01` | `4DE6FA0680A198B920A984803F5CCC811D796457CE66849F8F22D70625EC07A7` |
| `20260817-amd-svr-ovr-null-guardian-clear-info-skylighting-ablation-r01` | `DC4E98EC786A3A41E204A0A43BAB3056D136D72F7F63634A20DCB3CB026FAAA0` |
| `20260817-amd-svr-ovr-null-guardian-clear-info-sss-ablation-r01` | `E0C0DCDD8C5FA7E4FCC68F0AD90632BF36CFA7970CD39A1A7C71DB8C14D0C945` |
| `20260817-amd-svr-ovr-null-guardian-clear-info-terrainblending-ablation-r01` | `D81270AC74C752CB8B82084F4007FEE9283C0C3101A05332AB3FD97269704372` |
| `20260817-amd-svr-ovr-null-guardian-storm-info-wetterness-ablation-r01` | `DA0C565408F9716D3B36D4146A5033040CC13E35E18866B2C2BE8AF1EA3E6BA0` |

## Routing decisions

This screen supports the following next work without pretending that presets are already solved:

1. Measure inherited low/default/high response curves for Skylighting, Screen Space Shadows, and Wetterness first.
2. Preserve Skylighting update cadence and tail behavior, not only its amortized mean.
3. Capture separate eyes for the leading stereo-sensitive candidates after combined-stereo triage.
4. Exercise Grass Collision with a reproducible movement/input sequence.
5. Measure Terrain Blending at distances that cross its inherited cull thresholds; its continuous pass floor alone does not measure distance scaling.
6. Use a close, stationary character/material anchor for Subsurface Scattering.
7. Find a demonstrably active volumetric-ray/fog anchor before repeating Volumetric Lighting.
8. Add focused pairwise tests where broad ambient work, local shadowing, wet material response, IBL, or volumetric effects can overlap.

No result here justifies separate AMD and NVIDIA shader-policy trees. The inherited census still supports one tier intent with narrow provider/capability selection; NVIDIA hardware validation remains a later portability gate.
