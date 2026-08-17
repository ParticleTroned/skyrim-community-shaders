# First pairwise interaction measurements — 2026-08-17

Status: accepted source evidence; composition constraints promoted to the interaction ledger

## Scope and method

These tests use the same AMD native OpenVR → SteamVR null-HMD lane (`SVR-OVR-NULL`), Info logging, fixed Guardian Stones pose, and Release+DevBench bridge as the first profile curves:

- source commit: `dbad76d12`;
- DLL SHA-256: `35D76E9B86EA8928E56CA52C83727B102AF7A10597394A4967E7B3728315453D`;
- archived timing root: `L:\CSX Preset Automation\Sessions\2026-08-17\MO2-overwrite\CSX Baselines\preset-automation-interactions`;
- archived visual root: `L:\CSX Preset Automation\Sessions\2026-08-17\MO2-overwrite\CSX Baselines\preset-interaction-visual`.

Each pair used a full `2 × 2` factorial:

| State | Factor A | Factor B |
| --- | --- | --- |
| `11` | enabled | enabled |
| `10` | enabled | disabled |
| `01` | disabled | enabled |
| `00` | disabled | disabled |

Both timing and visual tests begin and end in `11`, and each pair was run in both A-first and B-first mutation order. Timing phases contain 120 unique resolved profiler samples. Visual phases contain 30 combined-stereo BMP pairs at a four-compositor-cycle interval with no failed, incomplete, or backpressured pairs. Time and weather are reapplied before every phase. Controls are session-only, effective-state checked, settled according to their API contract, and returned to their enabled baseline.

The factorial interaction is:

```text
I = state11 - state10 - state01 + state00
```

`I = 0` is consistent with additivity. A non-zero value means one factor changes the contribution of the other. The visual analyzer forms temporal medians and linearly interpolates the two `11` baselines at each state's compositor-token position. Stable-region results retain only pixels with at most 2 luma units of baseline-return drift and temporal frame delta. Forward/reverse agreement remains necessary because interpolation cannot remove non-linear foliage or weather motion.

## Skylighting × Screen Space Shadows

Factor A is Skylighting `performanceActive`; factor B is Screen Space Shadows `performanceActive`. The clear-day anchor exercises broad ambient redistribution and local contact/relief shadowing over the same stone, foliage, and terrain.

### Timing

Aggregate GPU interaction did not reproduce: A-first measured `+0.0414 ms`, while B-first measured `-0.2445 ms`. The latter includes an anomalously slow `10` phase and must not be interpreted as a negative feature cost.

Named timers separate the useful signal:

| Direct observation | A-first | B-first | Interpretation |
| --- | ---: | ---: | --- |
| Skylighting timer interaction | `-0.0235 ms` | `+0.0009 ms` | Not repeatable; probe cadence sampling dominates |
| SSS timer with Skylighting on | `0.0166 ms` | `0.0144 ms` | Repeatable small scheduled cost |
| SSS timer with Skylighting off | `0.0085 ms` | `0.0035 ms` | Lower in both orders |
| SSS timer interaction | `+0.0082 ms` | `+0.0108 ms` | Direction repeats |

SSS active-sample fraction was approximately `29–30%` with Skylighting enabled and `13–17%` without it. This is direct evidence that the observed SSS pass cadence/eligibility changes with the ambient state at this anchor. It does not yet prove the source-code cause or portability to other scenes.

### Visual composition

Mean absolute stable-region values, across both eyes, were:

| Effect | A-first | B-first |
| --- | ---: | ---: |
| Skylighting contribution with SSS on | `1.84–1.87` | `1.57–1.58` |
| Skylighting contribution with SSS off | `0.81` | `1.09` |
| SSS contribution with Skylighting on | `1.93` | `2.37–2.39` |
| SSS contribution with Skylighting off | `1.59–1.60` | `1.41–1.43` |
| Interaction | `1.81–1.84` | `1.97–1.98` |

The absolute interaction is repeatable and confirms spatial overlap. Its signed mean changes from about `-0.37` luma A-first to `+0.62` B-first, so the current anchor does not establish a stable masking or amplification direction. Preset decisions may treat the pair as coupled for visual review, but may not claim a deterministic whole-scene brightness relationship.

Left/right results agree closely. The stable region covered about `56%` of sampled pixels A-first and `47%` B-first.

## Wetness × Image-Based Lighting

Factor A is Wetterness `performanceActive`; factor B is IBL's live `EnableIBL` shader control. The IBL package itself remained loaded: its package switch is correctly reported as restart-only and was not used. The storm anchor exercises wet material response, ambient replacement, precipitation, and fogged exterior surfaces.

### Timing

Aggregate GPU interaction was positive in both orders: `+0.1109 ms` A-first and `+0.0524 ms` B-first. Wetness appeared near zero with IBL enabled (`+0.0334` and `+0.0190 ms`) and implausibly negative with IBL disabled (`-0.0776` and `-0.0334 ms`). This supports a possible shared/background interaction but is not an isolated Wetness cost measurement.

IBL's two named diffuse timers were extremely small in this capture. Their interaction was only `0.00017 ms` A-first and `0.00009 ms` B-first. The aggregate interaction therefore does not come from a measurable increase in those named timers alone; Wetness lacks a dedicated timer and broader lighting/material work remains unattributed.

### Visual composition

The visual result is strong and directionally repeatable:

| Effect | A-first | B-first | Signed interpretation |
| --- | ---: | ---: | --- |
| Wetness contribution with IBL on | `5.98–6.04` | `4.42–4.52` | Wetness darkens the scene by about `5.3–5.9` mean luma |
| Wetness contribution with IBL off | `2.58–2.60` | `2.31–2.34` | Wetness darkens by only `0.6–1.6` mean luma |
| IBL contribution with Wetness on | `4.10–4.13` | `3.94` | IBL brightens by about `1.8–3.2` mean luma |
| IBL contribution with Wetness off | `7.18–7.26` | `5.77–5.87` | IBL brightens by about `6.9–7.1` mean luma |
| Interaction | `5.72–5.78` | `3.96–3.97` | Negative signed interaction of about `-5.3` and `-3.7` luma |

Thus the combined result is emphatically non-additive. IBL makes the visible Wetness response much stronger, while Wetness reduces the apparent contribution of IBL. The full frames show this over exposed stone, earth, bark, foliage, and the foreground weapon, not only one small object.

This is a preset constraint rather than a reason to remove either feature. The inherited Wetness tiers must be evaluated with the intended ambient/IBL policy active. An “IBL off” Wetness test materially understates the production look. Conversely, IBL review scenes need both dry and wet material states.

Left/right results again agree closely. Stable regions covered about `75–76%` of sampled pixels A-first and `65%` B-first.

## Preset implications and next routing

1. Keep Wetness and IBL enabled as a coupled visual cluster for provisional Balanced and Quality policies; Performance may reduce Wetness density/range but should not be selected from IBL-off evidence.
2. A Wetness tier curve should be repeated with IBL active at a purpose-built near/far material anchor. The current three-tier storm view did not isolate distance or precipitation density.
3. Treat Skylighting and SSS as visually overlapping but independently valuable. The present evidence supports cheaper individual tiers, not removal of one as “duplicated” by the other.
4. Investigate why the SSS named pass active fraction falls when Skylighting is disabled before turning the measured cadence interaction into a code-level claim.
5. Repeat the most important coupled checks under a moving/separate-eye sequence and later under the physical-HMD/runtime matrix. Null-HMD fixed-pose symmetry is not full stereo qualification.
6. These interactions do not introduce an AMD/NVIDIA policy split. They define vendor-neutral composition requirements to validate on NVIDIA later.

## Artifact hashes

Timing hashes refer to `factorial-profiler-raw.json`; visual hashes refer to `visual-factorial-run.json`.

| Run id | SHA-256 |
| --- | --- |
| `20260817-amd-svr-ovr-null-guardian-clear-info-skylighting-sss-factorial-afirst-r01` | `AA8723AB141434EFEA9BEE7AB18E383CE480E68AB71F638F5AAE6AA1F7479F82` |
| `20260817-amd-svr-ovr-null-guardian-clear-info-skylighting-sss-factorial-bfirst-r01` | `3D06A2145EF91F3BD52DCDC5A050A9214EFBC31E0255FB97927DDA44C2F212B4` |
| `20260817-amd-null-guardian-clear-sky-sss-a-r01` | `57459808CF90A67D50C5FFA90921EBED1F72A2CD5051C3CA2BCDE8830AA5715A` |
| `20260817-amd-null-guardian-clear-sky-sss-b-r01` | `A70E44426AF387CB06A657A04F1F8AAC87CC53295A859E9912E67F3B634EAF65` |
| `20260817-amd-svr-ovr-null-guardian-storm-info-wetterness-ibl-factorial-afirst-r01` | `EF4A7247F312FDEDEECCC9DF364018A32EA4F6EA3E70EA65F0B416AB6F9BFE6F` |
| `20260817-amd-svr-ovr-null-guardian-storm-info-wetterness-ibl-factorial-bfirst-r01` | `23349D32DC88BFDC3692C44B9AFE0D3A9AAA89231C3AA5D0AD8B6E345DF90009` |
| `20260817-amd-null-guardian-storm-wet-ibl-a-r01` | `09CC71680125B005559493FBFD900BD096787FAB370D82EA4DEE04C19A1A1133` |
| `20260817-amd-null-guardian-storm-wet-ibl-b-r01` | `9FF99F76B324D0FA551F600CA18D3115B89A9E932700E00CA9AB2F843BAD99E5` |
