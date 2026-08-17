# SSGI and Volumetric Lighting calibration

Status: first AMD null-HMD qualification pass, 2026-08-17

## Scope and provenance

These runs use the native OpenVR → SteamVR null-HMD lane (`SVR-OVR-NULL`), an AMD GPU, Info logging, the fixed Guardian Stones and Dragonsreach anchors, and a VR Release+DevBench DLL built from commit `16a8ec96b` with SHA-256 `D1A3F0BAA32ED16140A04DA90706E2A0D0D18CCEC0A46D41DB08D5FD9D821605`.

Timing and lossless stereo capture were separate workloads. Every accepted timing phase retained 120 unique resolved frames. Every visual phase saved six exact left/right BMP pairs with zero failed, incomplete, or backpressured pairs. The stopped-runtime finaliser moved the completed D: staging tree to `L:\CSX Preset Automation\Sessions\2026-08-17\CSX Baselines`; neither the game directory nor MO2 overwrite retains evidence.

This is AMD fixed-pose evidence. It does not qualify a physical or moving HMD, NVIDIA, OpenXR, VDXR, or Open Composite Unleashed.

## Volumetric Lighting essence and control boundary

Volumetric Lighting represents atmospheric light scattering in a 3D grid, then generates, ray-marches, and blurs that representation before composition. It supplies fog depth and visible shafts rather than the shared directional-shadow map provided by the separate Volumetric Shadows feature.

The engine's Low, Medium, and High grids are already allocated and can be selected live. The official control API now exposes bounded `ExteriorQuality` and `InteriorQuality` values `0..2`, exact readback, an eight-frame settle, snapshot restoration, and `live-resource-rebind-settle` metadata. Selecting a tier clears/rebinds the active location's volumetric targets without compiling shaders. Custom grid dimensions are deliberately read-only in this API and reported as restart-bound because they change VR target allocation.

### Exterior timing

At Guardian Stones under `SkyrimClear`, 14:00, the sum of the four named Volumetric Lighting passes followed a clean monotonic curve:

| Quality | Named-pass median | Representative whole-frame medians | Main response |
| --- | ---: | ---: | --- |
| Low (`0`) | ~0.12 ms | 4.92, 4.96 ms | Generate ~0.03 ms |
| Medium (`1`) | ~0.14 ms | 4.96, 4.98 ms | Generate ~0.05 ms |
| High (`2`) | ~0.20 ms | 4.99, 5.00 ms | Generate ~0.10 ms |

Blur H/V remained near 0.02/0.04 ms; most of the tier response is grid generation, with a smaller ray-march change. Whole-frame ordering agrees with the named timers but includes more scene drift, so the named total is the isolated estimate.

### Exterior visual result

At normal scale, Low and High were not confidently distinguishable in either tested static composition. Guardian clear day was foliage- and shadow-motion limited. The two Low-minus-bracketing-High residuals correlated about `0.62`, but returning-High drift was larger than the inferred signal. Under Guardian fog/dawn, the residual correlation fell to about `0.16`, while Low/High magnitude remained comparable to returning-High drift. The evidence therefore supports “no confidently visible loss at these two fixed poses,” not a claim that all godray/fog scenes are equivalent.

The provisional tier candidate is Low / Medium / High. It is a measured performance gradient and a semantically conservative Quality ceiling, but its perceptual ordering remains low-confidence until a reproducible scene with strong shafts or dense volumetric structure is available.

## SSGI essence and control boundary

Screen Space GI can produce screen-space ambient occlusion and indirect lighting. The tested resource profile is AO-only: `ResourceProfile=1`, `EnableGI=false`, three angular slices, six steps, adaptive sampling, no temporal denoiser, and no blur. It retains AO buffers but omits the full-GI/IL/specular allocation.

The API exposes the already-loaded package's `Enabled`, `AOInteriorsOnly`, `ResolutionMode`, `NumSlices`, `NumSteps`, and `VRCullDistance` controls with exact effective readback. Slices, steps, cull distance, location gating, and enablement are live. Full/Half/Quarter resolution changes select runtime shader variants, reset history, and require readiness after an Info-lane compile. `ResourceProfile` and `activeResourceProfile` are read-only through this surface: changing the allocation profile is a genuine process-restart boundary.

The restore path now detects shader-permutation changes, requests compilation, and queues a temporal reset. Inactive SSGI is considered ready even when a different permutation will be compiled on its next activation; inactive work must not block session restoration.

### Resolution timing

With AO enabled in Guardian exteriors, otherwise holding the inherited AO-only settings constant:

| Resolution | SSGI named-pass median | Representative whole-frame medians | Reading |
| --- | ---: | ---: | --- |
| Full (`0`) | 0.35–0.37 ms | 5.48, 5.57 ms | GI/AO compute ~0.33–0.34 ms |
| Half (`1`) | ~0.14 ms | 5.34–5.40 ms | GI/AO compute ~0.10 ms plus ~0.02 ms upsample |
| Quarter (`2`) | ~0.06 ms | 5.25 ms | GI/AO compute ~0.03 ms plus ~0.02 ms upsample |

The Full → Half saving is about 0.21–0.23 ms in named SSGI work; Half → Quarter saves about 0.08 ms. Whole-frame medians preserve the same ordering in both Full repeats and all Half repeats.

### AO contribution and visual resolution

At Dragonsreach, Half-resolution AO on/off produced a repeatable scene contribution and a measurable cost:

- AO-on whole-frame medians: `4.57–4.59 ms`;
- AO-off whole-frame medians: `4.40–4.44 ms`;
- off-minus-on mean luma: `+1.203` left, `+1.162` right;
- two-cycle mean absolute luma: `1.312` left, `1.271` right;
- p95 absolute luma: `4.455` left, `4.479` right;
- residual-map correlation: `0.792` left, `0.793` right; and
- same-state AO-on drift MAE: only `0.284` left, `0.277` right.

AO visibly deepens broad contact and recess shading rather than merely changing exposure. About 41% of left-eye and 39% of right-eye pixels changed by more than one luma unit in the two-cycle mean.

Resolution differences are much subtler. Full-minus-bracketing-Half averaged `0.760/0.744` luma MAE with only `0.248/0.255` cycle correlation; Quarter-minus-Half was about `-0.167/-0.165` signed luma. NPC movement contaminated parts of the frame, but same-Half drift was only `0.314/0.311` MAE. This supports the performance ordering and a small Full-resolution quality response, not a strong perceptual ranking from this one interior.

## Preset implication

All inherited AMD and NVIDIA presets currently set SSGI `Enabled=false`; their `AOInteriorsOnly` and `ResolutionMode` differences are therefore dormant. The measured AO contribution is strong enough, and Half/Quarter cost low enough, to promote an **evaluation candidate** rather than silently preserving inactive tier values:

| Tier | Candidate |
| --- | --- |
| Performance | AO-only, interiors only, Quarter resolution |
| Balanced | AO-only, interiors only, Half resolution |
| Quality | AO-only, interiors only, Full resolution |

This candidate is not yet cleared for generated release presets. SSGI belongs to the ambient cluster with IBL, Skylighting, engine ambient/DALC, and deferred composition. A paired interaction screen must show that enabling AO does not over-darken the intended IBL/Skylighting baseline, and moving/physical-HMD capture must qualify stereo and disocclusion stability. Until those gates pass, the release-safe fallback remains disabled in all tiers.

## Accepted artifact identities

| Run | Record SHA-256 |
| --- | --- |
| `vl-guardian-clear-quality-curve-b-20260817` | `20C092BC5154E13FDCF060BB8EE83AE4DF8F01CBD41D0028E7A5CD90085B9C99` |
| `vl-guardian-clear-quality-visual-a-20260817` | `91753E2DB09F943ABA674F8EFDE4BCBE26EF511C360EADEFA86B72D256656C44` |
| `vl-guardian-fog-quality-visual-a-20260817` | `F90F22277805C9F8E22C14EEB61CA1B18DC81BF30F251E31439396EEAA6D5190` |
| `ssgi-guardian-clear-resolution-curve-b-20260817` | `92AE5E01C4F69331B224DD9D1C3509592848BA627C112938EA721E60EA68AC48` |
| `ssgi-dragonsreach-enabled-curve-a-20260817` | `2D359EDB9879446F4885FF112270C5CE13DDFA11A72B7CF89C06AE52F0F78B66` |
| `ssgi-dragonsreach-resolution-visual-a-20260817` | `A994BB0FED9C65E65A2AB3F118A78D97623BEBD7E8B8EDC02D9D1CABADABAA3A` |
| `ssgi-dragonsreach-enabled-visual-a-20260817` | `1AF62E28C8A2F9164B4F621B57A236E4DEFD6D2388C078D86B5018D8D98FE6E1` |

Failed or conservatively rejected setup runs remain in the same archive for audit but are not used as accepted evidence.
