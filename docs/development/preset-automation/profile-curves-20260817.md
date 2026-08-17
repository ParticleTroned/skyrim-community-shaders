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

## Wetness independent-parameter follow-up

Commit `ab359c4d9` added bounded, reversible Wetterness
`qualityParameters` at Info logging. The runtime rejects invalid bounds,
reports shader-derived coverage and event-opportunity values, resets owned
temporal state, waits five seconds, and restores the exact original state. No
shader variant is compiled. At the Quality values, readback reports a
`142.8 m` material fade, raindrop full-strength/cutoff radii of approximately
`1,980`/`2,425` game units, and a nominal cell-time opportunity rate of
`0.17778` per square world unit per second.

Four accepted Guardian Stones storm runs separated the inherited seven-field
cluster into two two-factor screens. Every phase reapplied and verified
`GuardianStones`, `Tamriel`, `14:00`, `SkyrimStormRain`, and camera yaw
`-1.550000906`; IBL remained active. Range runs captured 30 separate-eye BMP
pairs per state at a six-cycle cadence. Temporal runs captured 60 pairs per
state at a four-cycle cadence. All 900 pairs completed with zero failed,
incomplete, or backpressure drops, and every run restored its original
in-memory state.

Stable-region mean absolute luma effects across both orders and eyes were:

| Independent factor | Other factor on | Other factor off | Interaction |
| --- | ---: | ---: | ---: |
| Material wetness fade, `7,003` → `10,000` | `0.46`–`0.51` | `0.51`–`0.64` | `0.74`–`0.79` with raindrop range |
| Raindrop range, `700` → `1,400` | `0.43`–`0.46` | `0.57`–`0.64` | same screen |
| Opportunity density, `0.0712` → `0.1778` nominal rate | `0.69`–`0.86` | `0.67`–`0.78` | `1.01`–`1.04` with persistence |
| Splash/ripple persistence, `4.5/0.22` → `6.0/0.30` | `0.78`–`0.82` | `0.84`–`0.95` | same screen |

The absolute magnitudes and left/right response repeat. Signed whole-scene
direction and raw consecutive-frame-delta ratios do not: for example, the
low-density/high-persistence state measured `0.44`–`0.45×` the both-high
frame-delta baseline when density changed first, but `1.33`–`1.34×` when
persistence changed first. Storm and foliage evolution therefore still
dominate a simple motion-energy ordering. The correct conclusion is that the
controls visibly redistribute rain events and coverage; it is not that the
lower tier has proved more temporally stable or cheaper.

Representative lossless frames preserve the large wet/dark material response
in both states. Differences appear primarily as rain-streak/surface-event
distribution and subtle stone/ground coverage rather than a wholesale scene
brightness change. The inherited Wetness gradient remains suitable for a
provisional perceptual tier, but no GPU-saving claim is attached until a named
timer or equivalent attributable instrumentation exists.

Evidence root:
`D:\Games\Skyrim\MadGod2\overwrite\Root\CSX Baselines\preset-automation-wetness-factorial`

| Paired-order run | Manifest SHA-256 | Primary analysis SHA-256 |
| --- | --- | --- |
| ranges, material-first | `5A5CEBFA7DB9E64DFE767692A579E19BC78C3985DC30CC4D574AEDAC52FFFEF4` | `0CEF7F5795DFA365B1E99E71AC06A8EAA27417F2DD36B9C752B2CBABC50106A7` |
| ranges, raindrop-first | `7F4D76E72A9D2DEB3E59E4431DC56DA6C1EB3DFDAA8BA148713479CFE1375984` | `BA213ED4D97C6C0CD4C17A28D695D06D237145BFC13A67477D6938098867CC13` |
| temporal, density-first | `1C6857EAAD3A8AA2A3F72CE1E206D78C564F99BD6E36A96537B278DAECF929D1` | `F7F7AA12D435BD8104966BEDCFD5DB4440A4DF8A8C7657525DB484776E1E60DB` |
| temporal, persistence-first | `D90A394DA23C7A7E523747D134B4CBA10FB34858E361A8181F4488F65DCDE280` | `9B9706F083F46A5A85E814F04610896145525EF63862056A3D72EAEE557D3FDD` |

The temporal-analysis hashes are
`81AAF0718A2DF39946F62B97A6AD8A459B75E7A879A8B219CD68E7E1983674ED`
and
`9AB1DE3537B4343CA21ED659BCE3258126539939CA5FB9185CBE7C62CACA8C77`.

## SSS distance and separate-eye follow-up

The first distance follow-up added separate-eye output and a 3 × 3 stable-region spatial grid to the profile-curve tooling. Combined-stereo plus separate-eye BMP capture at the earlier four-cycle cadence caused two backpressure drops in the first Windhelm run, so that run is rejected. A six-cycle cadence completed all later runs with 30 frames per phase and zero failed, incomplete-pair, or backpressure drops.

Windhelm's fixed entry pose faces a near gate and is unsuitable for deciding the value of Quality's unlimited range. Its accepted forward/reverse pair also failed the directional validity check: the signed Balanced-versus-Quality result changed direction and the mean absolute separation expanded from roughly `0.52` to `1.55` luma. It remains useful negative evidence about anchor selection, not a distance-policy result.

The new `exterior-whiterun-multidepth-clear-day` anchor supplies foreground ground, tree and horse detail, middle-distance city geometry, distant mountains, and open sky. Its accepted Balanced→Quality and Quality→Balanced sweeps agree in both eyes:

| Sweep | Stable mean absolute B↔Q separation, left/right | Signed B−Q, left/right | Reading |
| --- | ---: | ---: | --- |
| Balanced→Quality | `0.90` / `0.88` | `+0.39` / `+0.42` | Quality is locally darker/more shadowed |
| Quality→Balanced | `0.74` / `0.74` | `-0.69` / `-0.72` when evaluated Q−B | Same physical direction after order reversal |

The spatial grid localizes the largest stable difference to the bottom/foreground row (`1.25`–`1.72` luma). The open-sky upper-right cell is about `0.10`, with upper-centre and upper-left cells around `0.39`–`0.43` and `0.77`–`0.80`. Left and right grids are nearly identical at this fixed pose.

This confirms a repeatable local/fine-shadow improvement from Balanced to Quality and provides additional fixed-pose eye-symmetry evidence. It does **not** demonstrate that unlimited SSS distance is valuable: the observed response is foreground-dominant, while sample count and cull distance change together. A cutoff decision still requires either a known-distance target with equal sample count or a control that separates sample-count and range factors, followed by moving-view/disocclusion inspection.

That Whiterun conclusion is retained as historical evidence but the scene is no
longer a validated anchor. A fresh `coc WhiterunExterior01` repeatedly produced
position `[18245.435, -10745.442, -4616.525]` and yaw `2.2832105`, rather than
the proof pose at `[18963.801, -11937.521, -4515.888]`, yaw `-1.5154`.
Correcting player position did not correct the submitted HMD yaw. The original
proof and its direction-balanced result remain real, but its view is not yet a
reproducible campaign input.

The subsequent separated-parameter factorial runs proved the `qualityParameters`
API, shader recompilation/readiness barrier, exact restoration, separate-eye
capture, and order reversal. Their ordinary pictures are rejected for a range
decision:

- the Whiterun pair used the repeatable near-wall entry rather than the historic
  multi-depth view and also contained HUD completion text;
- the Guardian Stones pair completed 30 frames in all five phases with zero
  drops and close left/right results, but foliage motion left only about
  `0.39`-`0.51` of pixels inside the stable mask and the signed range effects
  reversed with test order.

The shader source independently establishes the intended distance behavior. At
a `20,480`-unit cutoff, SSS fades to the unshadowed factor over
`19,280`-`20,480` units, then writes `1.0` beyond the cutoff. Whole-frame colour
differences cannot reliably isolate that band. The next build therefore exposes
the exact packed-stereo SSS factor buffer as the bounded, Info-level
`screen_space_shadows_factor` measurement. This named path retains the
Developer-Mode boundary for arbitrary texture readback and is the required
evidence source for the next range decision.

### Direct SSS factor result

The Info-level factor path was then run in both factor orders at the canonical
Guardian Stones clear-day anchor and at Dragonsreach as a static interior
negative control. Every state in all four accepted runs saved its requested
ordinary stereo frames, five factor frames, and factor statistics; all controls
returned to the exact 44-sample/unlimited baseline. The paired-order consensus
retains only pixels that changed by more than one `R8` code in both orders with
the same sign.

At 44 samples, candidate-minus-reference is unlimited minus 20,480-capped. A
negative factor means unlimited range added shadow:

| Scene / eye | Order 1 mean abs | Order 2 mean abs | Robust consensus mean abs | Robust signed mean | Same-sign share of changed pixels |
| --- | ---: | ---: | ---: | ---: | ---: |
| Guardian left | `0.02449` | `0.02137` | `0.01245` | `-0.001295` | `0.411` |
| Guardian right | `0.01089` | `0.01126` | `0.00628` | `-0.000867` | `0.410` |
| Dragonsreach left | `0.01102` | `0.00496` | `0.00202` | `+0.000057` | `0.207` |
| Dragonsreach right | `0.00353` | `0.00300` | `0.000915` | `-0.000035` | `0.206` |

Guardian's sign-agreeing response is therefore about 6.2× the interior left-eye
floor and 6.9× the interior right-eye floor. Its two order correlations on
changed pixels are positive (`0.292` left, `0.345` right); the interior controls
are slightly negative. The Guardian robust grid localizes mean-absolute response
to the upper third (`0.0279`-`0.0335` left and up to `0.0249` right), falling to
near zero across the bottom third. Raising 30 to 44 samples also has stable
direction and magnitude in both orders, independently of the range setting.

This is direct evidence that Quality's unlimited range changes the shader output
above the order-balanced static-interior noise floor while sample count is held
constant. It supports retaining unlimited range in the provisional Quality tier.
It is not yet release qualification: the Guardian response is foliage-heavy,
order residual remains larger than the consensus mean, the active foveated SSS
coverage leaves part of the right-eye factor target at the unshadowed clear
value, and no object in the response has a verified 19,280-20,480-unit depth.
A known-distance mostly-static target plus moving-view/disocclusion inspection
remains required before treating the cutoff as perceptually indispensable.

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
| `20260817-amd-null-whiterun-clear-sss-distance-bq-r01` | `B7C783434AC216B17980F82EC83E253E52CA1A291D0225DF2627DB92BB5182E0` |
| `20260817-amd-null-whiterun-clear-sss-distance-qb-r01` | `4D2B6D2743240F4953608045153EEBA4ED7F340D27F3D409BA1CB8CB1F7E1147` |
| `20260817-amd-null-guardian-clear-hudoff-sss-factor-mask-rangefirst-r01` | `99EF48621650472245DC0A399974A8B6430B3D83D1EEDA537C66FBAEF140008A` |
| `20260817-amd-null-guardian-clear-hudoff-sss-factor-mask-samplesfirst-r01` | `0A18B866467DA93ED65593B90269EC2961740282A3BD308365107CC0C3DCF4E1` |
| `20260817-amd-null-guardian-clear-hudoff-sss-factor-mask-consensus-r01` | `ABE38607A73DA5FB2DFA860D30B00A02048036855528EDEF89B31E4F93661C01` |
| `20260817-amd-null-dragonsreach-midday-hudoff-sss-factor-mask-rangefirst-r01` | `E2CBC664A136BBF10166CFC43F78AA5F6233AEAE7EA5D29C9BAD9BF8E4D8D6F5` |
| `20260817-amd-null-dragonsreach-midday-hudoff-sss-factor-mask-samplesfirst-r01` | `BCDBB9D1EE00F4F7C9C976D471E8DF9E33BF760E784462D5E5810DB52AAA3668` |
| `20260817-amd-null-dragonsreach-midday-hudoff-sss-factor-mask-consensus-r01` | `4E9E9F4EE2BA4D487D146FC4EBDCDEEE0BEA62F290C61A00C3E300DDA2BAC41F` |

Additional retained Windhelm evidence: rejected four-cycle BQ `D14ED72D82B62597FE93247B763F28FEC3E8CF870D1A54938DE89E254E7CD697`; accepted six-cycle BQ `EC228FA178A25AA5F8EDC89776F3CBAFB91771AC5EB1BA90425296459204D91D`; accepted six-cycle QB `6A8F4092E85499020B528F24D45EA0F4AC394072774BB255FCA9EAF8FC99BD30`.
