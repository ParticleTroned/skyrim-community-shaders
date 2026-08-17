# Benchmark and capture protocol

Status: initial protocol; synthetic-HMD execution path demonstrated, with the first run retained as invalid benchmark evidence

## Objective

Produce repeatable timing and visual evidence without confusing a controlled automation environment with the physical headset and runtime paths used by players.

Every comparison is scoped to an exact runtime lane. Cross-lane testing initially measures agreement and bias; it must not be used to merge unlike results until repeatability and correlation have been demonstrated.

## Runtime identity

A measurement must record these layers independently:

1. application VR API, such as OpenVR;
2. compatibility layer, such as none or Open Composite Unleashed;
3. target runtime API, such as OpenVR or OpenXR;
4. runtime implementation, such as SteamVR or VDXR;
5. compositor or transport path;
6. HMD mode: occupied physical, unoccupied physical, or synthetic;
7. HMD driver and profile, including render target, refresh, projection, and eye transforms.

For example, native Skyrim VR through SteamVR and Skyrim VR through OCU into SteamVR's OpenXR runtime are different lanes even though both contain “SteamVR”.

## Initial comparison matrix

| Lane ID | HMD mode | Application path | Runtime implementation | OCU evidence? | Initial use |
| --- | --- | --- | --- | --- | --- |
| `SVR-OVR-NULL` | Synthetic SteamVR null HMD | OpenVR -> SteamVR OpenVR | SteamVR | No | First unmanned automation proof of concept |
| `SVR-OVR-PHYS-O` | Occupied physical HMD | OpenVR -> SteamVR OpenVR | SteamVR | No | Native SteamVR reference |
| `SVR-OVR-PHYS-U` | Unoccupied physical HMD | OpenVR -> SteamVR OpenVR | SteamVR | No | Occupancy-effect comparison |
| `OCU-SVRXR-PHYS-O` | Occupied physical HMD | OpenVR -> OCU -> OpenXR | SteamVR OpenXR | Yes | SteamVR OpenXR implementation reference |
| `OCU-SVRXR-PHYS-U` | Unoccupied physical HMD | OpenVR -> OCU -> OpenXR | SteamVR OpenXR | Yes | Occupancy-effect comparison if the session remains valid |
| `OCU-VDXR-PHYS-O` | Occupied physical HMD | OpenVR -> OCU -> OpenXR | VDXR | Yes | VDXR reference |
| `OCU-VDXR-PHYS-U` | Unoccupied physical HMD | OpenVR -> OCU -> OpenXR | VDXR | Yes | Occupancy-effect comparison if the transport remains active |

The matrix describes planned comparisons, not presumed equivalence. A lane that cannot remain active without occupancy is recorded as unavailable rather than approximated. The built-in SteamVR null lane does not include OCU and cannot validate either OCU/OpenXR lane.

## First synthetic-HMD result

Run `20260817-amd-svr-ovr-null-honeyside-poc-r01` demonstrated the complete unattended native-OpenVR path: SteamVR selected its null HMD, Skyrim VR reached DataLoaded, DevBench loaded a settled Honeyside save, CSX returned resolved profiler data, and three visually coherent left/right submission pairs were saved. The physical Virtual Desktop headset remained live, and its SteamVR driver loaded, but the null driver retained HMD ownership.

The run is not accepted as benchmark evidence. All three requested pairs completed, but the six-cycle capture cadence produced eight queue-backpressure rejections. Repeat the visual pass at a slower cadence and quantify fixed-pose repeatability before promoting the lane. This result establishes viability of the harness, not equivalence to a physical HMD or any OCU/OpenXR path.

Run `20260817-amd-svr-ovr-null-riften-info-ibl-vs-r01` establishes the first valid timing-only Info baseline. It uses the AMD Quality stack in exterior Riften with IBL and Volumetric Shadows enabled. Thirty-eight unique resolved frames completed with no profiler slot refusals. Automated stereo capture was intentionally not performed: CSX restricts that control surface to Developer Mode, and changing from Info would change the performance lane. A separately paired visual run is still required before this candidate supports visual conclusions.

## First proof-of-concept acceptance

The `SVR-OVR-NULL` proof of concept succeeds only when all of the following are demonstrated and recorded:

- SteamVR starts with the intended synthetic HMD profile;
- Skyrim VR obtains a usable VR system and, if required for unattended progression, compatible controller identities;
- CSX installs its OpenVR submit observation path;
- both submitted eyes are accepted under the same compositor-cycle token;
- a short lossless `hmd_stereo` capture completes;
- the run reports no incomplete stereo-pair or queue-backpressure drops;
- the CSX profiler can produce resolved GPU measurements without capture running;
- a fixed synthetic pose can be repeated closely enough for paired visual comparison; and
- the measurement record contains runtime, HMD geometry, build, settings, hardware, manifest, and profiler provenance.

`framed_stereo` may be tested after raw submitted-eye capture works. Because it uses runtime projection, eye transforms, and hidden-area geometry, its result is specific to the synthetic HMD profile.

## Controlled run

Before each candidate:

1. Record the repository commit, built DLL digest/version, dependency snapshot when relevant, driver, runtime lane, HMD geometry, game configuration, mod list/profile, and complete CSX settings digest.
2. Load the same save and establish scene ID, location, weather, time, pose, visible targets, and motion script.
3. Reach a defined warm state. Do not mix shader compilation, cache creation, loading transition, or history initialization with steady-state samples unless one is the phenomenon being studied.
4. Run the timing pass with screenshot capture disabled.
5. Run the visual pass separately using the same candidate and pose/motion script.
6. Preserve raw left and right submissions for stereo analysis. Treat combined or preview output as derivative evidence.
7. Repeat candidates in a balanced order and include a return-to-baseline check to expose drift.

Change one declared factor per paired comparison. If an interaction is the subject, declare the complete combination as that factor.

## Shader-cache lanes

Shader compilation is a controlled preparation phase and must not be silently folded into a timing or visual pass. Preserve a completed cache as an atomic set containing the entire `ShaderCache` tree and its matching `Info.ini`. Keep at least separate `Info` and `Debug` sets whenever both have completed successfully.

A cache identity record must include:

- repository commit and built DLL SHA-256;
- build configuration, compiler-affecting defines, and dependency revisions;
- complete feature-package identity and feature versions;
- settings digest, log level, and developer-mode state;
- GPU vendor and runtime lane when either can affect generated content; and
- cache schema/plugin version from `Info.ini`.

Switch the cache set and the matching settings/preset together. In MO2, enable exactly one named cache mod for the lane and leave the other cache mods disabled. Confirm the selected log level and the `Using disk cache` startup message before accepting a run.

Do not promote a cache when CSX reports it as held, missing, disabled, mismatched, or memory-only. Source or dependency changes may legitimately invalidate part or all of a preserved set; allow that rebuild to finish outside a measured pass, then snapshot the new complete state under a new identity. Never merge arbitrary files from unlike identities. A small overwrite overlay may be merged only into the exact active cache identity after Skyrim has exited cleanly. Remove or archive the verified overlay after merging it so it cannot override a different named cache mod later.

## Timing evidence

- Prefer resolved CSX GPU timing over a current unresolved sample.
- Retain sample count, warmup count, average, median when available, `p95`, `p99`, and observed variance.
- Record frame budget and refresh rate; milliseconds remain the comparison unit.
- Measure settle or history-reset cost separately from steady-state cost.
- Treat CPU, GPU, memory, and resource-lifecycle observations as separate objectives.
- Do not time screenshot encoding and then attribute that cost to the feature candidate.

## Visual and correctness evidence

At minimum inspect:

- raw left and right eyes for stereo divergence, double vision, eye-specific omissions, and projection errors;
- lossless stills for spatial changes and feature intent;
- a bounded sequence for flicker, ghosting, disocclusion, instability, and history settling;
- hard failure signatures declared in the dossier; and
- scene coverage declared by the policy.

Preview video is for navigation and temporal review only. Lossless frames and their manifest remain authoritative.

## Validity and comparability gates

Reject or mark incomplete a run when:

- source snapshot, settings, runtime lane, GPU/driver, HMD geometry, or scene identity is missing;
- capture reports an incomplete eye pair, backpressure drop, partial session, or unexpected source;
- the profiler lacks sufficient resolved samples;
- the candidate violates a hard correctness or stability constraint;
- the runtime or HMD changes within a paired comparison;
- an undeclared dependency, shader cache, preset, or mod-list change is detected; or
- pose, weather, time, scene state, or motion differs beyond the comparison's tolerance.

Comparisons across occupied/unoccupied, native OpenVR/OCU, SteamVR OpenXR/VDXR, physical/synthetic, or different HMD profiles are exploratory until their within-lane variance and cross-lane bias are known. Retain all raw objectives; do not “correct” one lane into another with an assumed scalar multiplier.

## Minimal calibration sequence

After the null-HMD proof of concept:

1. repeat an unchanged baseline several times in `SVR-OVR-NULL`;
2. repeat the same scene and settings with occupied and unoccupied physical SteamVR/OpenVR;
3. repeat through OCU with SteamVR's OpenXR runtime;
4. repeat through OCU with VDXR;
5. compare success rate, image geometry, correctness findings, mean/tail timing, and run-to-run variance;
6. define which conclusions are stable within a lane, correlated across lanes, or not comparable.

This sequence evaluates the harness before it is trusted to select presets.
