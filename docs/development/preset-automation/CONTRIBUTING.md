# Contributing preset-automation evidence

This corpus accepts results from AMD, NVIDIA, Intel, physical HMDs, synthetic HMDs, OpenVR, OpenXR compatibility layers, and distinct runtime implementations. A contribution qualifies only the hardware and runtime lane it actually records.

The immediate NVIDIA goal is to test the same vendor-unified settings policy, not to create a parallel NVIDIA preset tree. Provider-specific behavior belongs in the recorded capability boundary and becomes a policy override only after evidence demonstrates a real divergence.

## Before running

1. Use a clean, published Community Shaders Expanded commit. Record the full commit, branch, DLL version and DLL SHA-256.
2. Record the exact complete `SettingsUser.json` artifact and SHA-256 before the first mutation. If a control changes settings in memory, retain the starting snapshot plus every requested and effective value.
3. Record dependency revisions and dirty states, especially CommonLibSSE-NG.
4. Record exact GPU adapter, device ID, driver, CPU and operating system.
5. Define the runtime lane field by field. Native OpenVR, OCU to SteamVR OpenXR, and OCU to VDXR are different lanes.
6. Record HMD mode, driver, render target, refresh, projection source, pose source and presence state.
7. Select a validated anchor or define a reproducible scene, time, weather and pose recipe before changing the candidate.

SteamVR's null HMD is not part of the CSX DLL or presets. It is an external SteamVR `driver_null` configuration. A contributor must configure it separately and record it as a synthetic-HMD lane. CSX supplies only the optional DevBench control/capture surface used by the harness.

## Create a record

For NVIDIA, copy [`measurements/nvidia-run.template.json`](./measurements/nvidia-run.template.json). For other hardware, copy [`measurements/measurement-run.template.json`](./measurements/measurement-run.template.json).

Rename the copy to exactly `<runId>.json`. Recommended NVIDIA identity:

`YYYYMMDD-nvidia-<lane>-<hmd>-<scene>-<candidate>-rNN`

For example:

`20260818-nvidia-svr-ovr-physical-dragonsreach-balanced-r01`

Use lowercase in `runId`. Keep paired IDs explicit in `comparability.baselineRunId`; filenames and alphabetical adjacency do not establish a pair.

Schema version 2 adds `provenanceGaps`. A complete new run should use an empty array. If a required identity is genuinely unavailable, state the gap and mark the record `incomplete`; do not use zero hashes or guessed values in a completed record.

## Run the comparison

- Keep timing and screenshot/video capture as separate passes.
- Require an explicit settle barrier and effective readback after every mutation.
- Retain unique resolved profiler frames newer than the arming boundary.
- Preserve raw left and right eyes for VR visual evidence.
- Include a return-to-baseline phase to expose drift.
- Record failed and rejected runs as `invalid`; they are useful harness evidence.
- Do not compare unlike runtime lanes or HMD modes as though a scalar correction made them equivalent.

For an AMD/NVIDIA comparison, use the exact same generated settings file and hash wherever capability permits. Record the resolved provider separately—for example DLSS on NVIDIA and FSR on AMD—because provider selection is the intended narrow capability boundary.

## Artifacts

Small settings snapshots, run records, manifests, profiler summaries and textual analyses may enter source control after review. Keep lossless sequences, videos, RenderDoc captures and dumps outside ordinary Git.

Every external artifact needs:

- a stable non-machine-local locator;
- media type and byte size;
- SHA-256; and
- retention state and access notes where needed.

Put a local absolute convenience path only in `localPath`. Never use `C:`, `D:`, `L:` or a UNC path as the sole `locator`.

## Validate and update the index

From the repository root:

```powershell
./tools/validate-preset-measurements.ps1 -WriteIndex
./tools/validate-preset-measurements.ps1
```

Commit the new measurement record and the regenerated [`corpus-index.json`](./measurements/corpus-index.json). Pull requests run the same schema, identity, artifact-reference, status-consistency, portable-locator and index-freshness validation automatically.

The validator requires PowerShell 7's built-in `Test-Json`; it downloads no package and does not need access to the external evidence archive.

## Review boundary

A valid record does not automatically change a preset. Review retains the full objective vector: cost and tails, perceptual contribution, temporal and stereo behavior, resource cost, interaction risk, scene coverage and confidence. Cross-vendor agreement can raise portability confidence; disagreement should first be localized to a provider, driver, capability, or runtime-lane boundary.

Do not edit an existing immutable run to make a new conclusion fit. Add a new run, analysis or decision entry and link the evidence explicitly.
