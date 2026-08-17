# First quality-profile curves — 2026-08-17

Status: accepted source evidence; enough to constrain provisional tier choices for these three features, but not enough to qualify a complete preset

## Scope and method

These runs extend the first AMD screen in the native OpenVR → SteamVR null-HMD lane (`SVR-OVR-NULL`). They use Skyrim VR at the fixed Guardian Stones pose, Info logging, and the Release+DevBench bridge built from commit `dbad76d12`:

- DLL SHA-256: `35D76E9B86EA8928E56CA52C83727B102AF7A10597394A4967E7B3728315453D`;
- timing root: `D:\Games\Skyrim\MadGod2\overwrite\Root\CSX Baselines\preset-automation-curves`;
- visual root: `D:\Games\Skyrim\MadGod2\overwrite\Root\CSX Baselines\preset-automation-visual-curves`.

Every accepted timing sweep collected a Quality baseline, Performance/Balanced/Quality phases, and a restored Quality baseline with 120 unique resolved profiler samples per phase. Every accepted visual sweep captured the same five phases as 30 combined-stereo BMP pairs at a four-compositor-cycle cadence, with exact frame counts and zero backpressure or incomplete-pair drops. Each feature was measured in both PBQ and reverse QBP order. Capture and profiling were kept separate.

The first Skylighting visual PBQ attempt (`...pbq-r01`) is retained as rejected evidence. Its first phase succeeded, but the next capture path exceeded Windows' effective path limit. The runner now uses short feature/phase labels; `...pbq-r02` is the accepted replacement.

`tools/analyze-visual-profile-curve.py` forms a temporal median for each phase, interpolates the two restored-Quality baselines at each phase's compositor-token position, and compares the drift-corrected profile residuals. “Stable region” below restricts the result to pixels with at most 2 luma units of baseline-return drift and temporal frame delta. This improves attribution but cannot turn non-linear foliage or weather animation into a controlled signal; forward/reverse direction agreement remains a validity check.

## Effective profile definitions

These are the values actually exposed by the typed runtime control. They are session-only and reversible; they do not edit configuration files.

### Skylighting

| Tier | Probe field | Grid quality | Probe update | Occlusion update | Stable slices | Minimum specular visibility |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Performance | 10,240 | 0 | 16 | 8 | 8 | 0.05 |
| Balanced | 12,970.667 | 1 | 13 | 6 | 11 | 0.10 |
| Quality | 15,701.333 | 2 | 9 | 5 | 13 | 0.10 |

Changing tier recreates probe resources and resets temporal history. The control declares a five-second settle gate.

### Screen Space Shadows

| Tier | VR reference samples | Cull distance |
| --- | ---: | ---: |
| Performance | 16 | 20,480 |
| Balanced | 30 | 20,480 |
| Quality | 44 | unlimited (`0`) |

Changing tier releases and recompiles the per-eye ray-march variants. The control becomes ready only when both eye variants match the requested sample count; all three variants are now present in the retained Info shader cache.

### Wetness

| Tier | Requested fade | Effective fade | Drop chance | FX range | Grid size | Interval | Ripple life | Splash life |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Performance | 5,000 | 7,002.801 | 0.60 | 700 | 3.60 | 0.65 | 0.22 | 4.5 |
| Balanced | 7,500 | 7,500 | 0.70 | 1,000 | 3.25 | 0.58 | 0.26 | 5.2 |
| Quality | 10,000 | 10,000 | 0.80 | 1,400 | 3.00 | 0.50 | 0.30 | 6.0 |

The Performance fade request is clamped by the existing 100-metre minimum. The API reports both requested and normalized values, so no result here is labelled as a measured 5,000-unit cutoff. Changing tier resets the temporal weather state and uses a five-second settle gate.

## Timing response

All values are milliseconds. Aggregate means retain scene drift; direct named timers are preferred where available.

### Skylighting

The stable reverse aggregate sweep measured Quality `4.932104`, Balanced `4.867290`, and Performance `4.826713`. Averaging corresponding profile phases across both directions gives approximately Performance `4.8346`, Balanced `4.8640`, and Quality `4.9976`: about `0.0294` from Performance to Balanced and `0.1336` from Balanced to Quality. The forward baseline itself drifted by `0.2010`, so the averaged aggregate curve is directional evidence rather than an isolated cost claim.

The direct Skylighting timer makes the Quality step clearer:

| Tier | Mean across both sweeps | Mean when active | Observed active fraction |
| --- | ---: | ---: | ---: |
| Performance | 0.0141 | 0.2596 | 5.45% |
| Balanced | 0.0126 | 0.3368 | 3.75% |
| Quality | 0.0652 | 0.3557 | 18.3% |

Performance and Balanced cadence estimates are sparse in 120 samples and should not be over-ranked from the amortized means. Quality is nevertheless materially more frequent and more expensive overall. This is a genuine policy lever rather than a cosmetic micro-setting.

### Screen Space Shadows

Aggregate frame means were too noisy to rank the tiers. The named per-eye timers were cleaner and produced these averages across PBQ and QBP:

| Tier | Mean across all samples | Mean when active | Active fraction |
| --- | ---: | ---: | ---: |
| Performance | 0.0143 | 0.0427 | 33.4% |
| Balanced | 0.0188 | 0.0563 | 33.4% |
| Quality | 0.0197 | 0.0594 | 33.3% |

Performance is a repeatable saving. Balanced and Quality are close at this near-field anchor; their important distinction is therefore coverage, especially Quality's unlimited range, rather than a large near-field timing step.

### Wetness

Wetness has no dedicated profiler timer in this build. The forward aggregate sweep drifted strongly; the reverse sweep measured Quality `4.934706`, Balanced `4.988966`, and Performance `4.965021` around a stable `4.976384` → `4.968415` baseline. No tier ordering is resolved within background noise. Until dedicated instrumentation exists, tier selection must be driven by visible range/density, weather-conditioned temporal behavior, and interaction evidence rather than a claimed GPU saving.

## Visual response

The table reports the range across forward/reverse sweeps and both eyes for mean absolute, drift-corrected pairwise separation inside stable regions. Units are 8-bit luma.

| Feature | Performance ↔ Balanced | Balanced ↔ Quality | Performance ↔ Quality | Directional interpretation |
| --- | ---: | ---: | ---: | --- |
| Skylighting | 0.74–1.00 | 0.75–1.06 | 1.14–1.21 | Local redistribution is repeatable, but signed whole-scene ordering reverses or collapses with sweep order; do not describe tiers as simply brighter/darker |
| Screen Space Shadows | 0.79–1.30 | 0.80–0.97 | 1.11–1.16 | Direction agrees: Performance is modestly brighter/less shadowed than Balanced, and Balanced is modestly brighter/less shadowed than Quality |
| Wetness | 0.87–0.95 | 0.93–0.94 | 0.97–0.98 | Absolute separation repeats, but signed ordering conflicts under reversing storm evolution; visible range and rain-density changes need a purpose-built distance/temporal anchor |

Left/right values track closely in every sweep, with typical mean differences below `0.05` luma. That is reassuring symmetry evidence for this fixed combined-stereo view, not a complete stereo-correctness qualification. The leading candidates still require separate-eye inspection under head movement and at depth discontinuities.

Representative full frames support the quantitative reading:

- Skylighting tier changes are subtle at a glance and appear as fine ambient redistribution over shaded foliage, stone, and terrain rather than a new scene-wide look.
- Screen Space Shadows Performance slightly relaxes contact/relief shading; higher tiers retain more fine shadow structure and Quality removes the inherited distance cutoff.
- Wetness itself remains a large storm-conditioned material effect, but the three tier variants look close from this static near-field viewpoint. The inherited tier differences primarily change precipitation density/lifetime and effect range, which this composition does not isolate well.

## Provisional policy constraints

These curves support constraints, not yet a complete preset:

1. Skylighting Quality is not a free default. Balanced is the present provisional centre tier; Performance should use the longer update cadence/smaller field, while Quality may retain the costly high-frequency probe policy if later temporal and scene-coverage checks justify it.
2. Screen Space Shadows Performance should use the 16-sample capped-range variant. Balanced's 30 samples are a credible centre point. Quality's 44 samples plus unlimited range should be retained only if distance/edge stability tests show visible value; its near-field cost over Balanced is small here.
3. Wetness tiers should remain visually enabled. Performance's requested 5,000-unit fade is not currently realizable because of the sanitizer minimum. No performance claim should be attached to the current Wetness tier curve until dedicated timing or a stronger aggregate design exists.
4. None of these results requires separate AMD and NVIDIA shader-tier policies. Hardware portability remains a later gate; only narrow capability/provider overrides are in scope.
5. Next evidence should target pairwise composition (Skylighting × Screen Space Shadows, Wetness × ambient/IBL), SSS distance boundaries, Wetness range/precipitation motion, and separate-eye/head-motion stability.

## Artifact hashes

Timing hashes refer to `<timing root>/<run id>/profile-curve-profiler-raw.json`; visual hashes refer to `<visual root>/<run id>/visual-profile-curve-run.json`.

| Run id | SHA-256 |
| --- | --- |
| `20260817-amd-svr-ovr-null-guardian-clear-info-skylighting-curve-pbq-r01` | `33965F74FCDAB1391304309AF093AD46405E394BA39F909167B070A2FF06AD95` |
| `20260817-amd-svr-ovr-null-guardian-clear-info-skylighting-curve-qbp-r01` | `BE6CBC5401A1BA0C78FFD291A8FF3D0C7BAC2DC93828F48F78326D554AA26F50` |
| `20260817-amd-svr-ovr-null-guardian-clear-info-skylighting-visual-curve-pbq-r02` | `9E3A6771B61AA07490995FE289B4F9794B4209213C9661B28B6775D086F0BE1E` |
| `20260817-amd-svr-ovr-null-guardian-clear-info-skylighting-visual-curve-qbp-r01` | `EEAA7654120BF095DD272AFEF439A7A96AACEF6E9F97C27CC89B5A732F2161C5` |
| `20260817-amd-svr-ovr-null-guardian-clear-info-sss-curve-pbq-r01` | `AD6F52003FB2D61E118C854F1E0CA0B4ED4352EF90203595533D3ADB60FBC60F` |
| `20260817-amd-svr-ovr-null-guardian-clear-info-sss-curve-qbp-r01` | `00446D34A9E4EC0A38673D9E816C1F3687F232182E29EF9FE5AE231DC3EE9FA0` |
| `20260817-amd-svr-ovr-null-guardian-clear-info-sss-visual-curve-pbq-r01` | `A4538B0CB7BD6A9A63930D3E66E777406414806C258D52C2DAF387C789E1951A` |
| `20260817-amd-svr-ovr-null-guardian-clear-info-sss-visual-curve-qbp-r01` | `2AFCA65FC1F50F3415BF474C86A022289F3656564886D7806B8A422FBCD41A77` |
| `20260817-amd-svr-ovr-null-guardian-storm-info-wetterness-curve-pbq-r01` | `0BE1473BF2ABEB40B1C65F8D7309F3FE1FB9646DCAE4C602AD9C41EFBEC20C49` |
| `20260817-amd-svr-ovr-null-guardian-storm-info-wetterness-curve-qbp-r01` | `A17D4E6207E03B4F3ACF16C965CEDF07C11B85954E39D353A4485CCBFB0423F2` |
| `20260817-amd-svr-ovr-null-guardian-storm-info-wetterness-visual-curve-pbq-r01` | `E1EB8BBE6C3DF8D1C668332469EA4BD8B31358B130E405A5CF38AD6084331441` |
| `20260817-amd-svr-ovr-null-guardian-storm-info-wetterness-visual-curve-qbp-r01` | `EDFFD3BFB5B82C5E6524A906626A306B15AA76032912084AA763E96F58DB9AE6` |
