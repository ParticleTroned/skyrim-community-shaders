# Attributed HMD colour campaign

`hmd_capture.py` runs a frozen plan through the existing capture and DevBench
controllers. `hmd_assess.py` imports the resulting committed native PNG
sequences, measures the original pixels and prepares the existing automated
visual-review provider. The reviewer receives anonymous images. No user
judgment, automatic colour correction or production-default selection is part
of this workflow.

Read [the assessment protocol](../../docs/development/nr-colour-hmd-assessment.md)
and the installed DevBench, MO2 and capture skills before runtime work. Use an
owned, already prepared scene. Keep screenshots and recording outside the
performance campaign. A broad FOV such as 0.95 is still the FOV route; record
it accurately and qualify another available route in a separate campaign.

## Install and validate the offline tools

Use a task-local Python environment; do not replace system packages:

```powershell
python -m venv build/hmd-python
build/hmd-python/Scripts/python.exe -m pip install -r tools/nr-color/hmd_requirements.txt
build/hmd-python/Scripts/python.exe tests/neural_color/test_hmd_assess.py
```

The requirements pin NumPy, Pillow and JSON Schema validation. Output records
their actual versions and the analyser source SHA-256. These tests use small
synthetic fixtures to exercise provenance rejection, measurement formulas,
blinding and finalization. They are not HMD image-quality evidence. Running
the fixture file without image dependencies explicitly skips the image
tests; that is not an image-tooling pass.

## Freeze the scene, candidate settings and regions

The scene must already be stabilized and have a recorded save/cell, weather,
time, camera and graphics configuration. Select native-pixel semantic regions
from the source scene before examining candidates. A machine image-analysis
context can select them; the user need not judge a colour result. Do not
white-balance the source or choose regions to favour a later candidate.

The region-policy JSON has:

| Field                         | Required value                                                                            |
| ----------------------------- | ----------------------------------------------------------------------------------------- |
| `schemaVersion`               | `1`                                                                                       |
| `sceneFingerprint`            | The exact prepared scene identifier shared with the specification                         |
| `motionGradientMismatchLimit` | A fixed finite fraction between 0 and 1                                                   |
| `eyes.left`, `eyes.right`     | Separate per-eye objects                                                                  |
| `nativeSize`                  | `[width, height]` from the native staged eye, not the packed stereo texture               |
| `regions`                     | Objects with safe `id`, `class`, `description`, and native `[x,y,width,height]` rectangle |
| `unavailable`                 | A mapping from each absent region class to its explicit reason                            |

The classes are `skin`, `material`, `shadow`, `highlight` and `background`.
Each must have a region or an absence reason in each eye. Match region IDs
across eyes, with separate rectangles for the corresponding scene surface.
Do not assume that a material or the scene lighting should be neutral.
Rectangles must be at least three pixels in each dimension and stay within
the native extent. No per-candidate crop adjustment is accepted.

The specification JSON contains `sceneFingerprint`, `fixedScene`,
`fixedSettings`, `candidates` and optional `intervalMs` and `untestedConditions`:

-   `fixedSettings` is the full `requestedConfiguration.upscaling` object with
    only `neuralRenderingEnabled` removed. Keep ROI/FOV/mask policy, category
    strengths, insertion, upscaler and all unrelated settings fixed.
-   `fixedScene.cameraEvidence` pins the actual engine cached unjittered
    `view` and `projection` matrices, each as two arrays of 16 numbers, and
    `positionAdjust` as two arrays of four numbers. The corresponding
    `viewTolerance`, `projectionTolerance` and `positionAdjustTolerance` are
    explicitly chosen finite, nonnegative absolute-element tolerances. They
    must be fixed before the candidate run. Do not invent identity matrices
    or substitute a later tracking sample.
-   `fixedScene.recordingScene` pins the recorded cell, `cellFormID`,
    `interior`, `weatherFormID` and any available worldspace fields.
    `fixedScene.gameHour` and `fixedScene.gameHourTolerance` pin the time
    window. The runtime runner checks the preserved recorder evidence.
-   `candidates` contains one each of `nr_off`, `raw`, `managed_identity` and
    `preserve_source`, plus optional `neural_lighting` and any justified
    `conversion` entries. Each candidate
    has `condition` and a nonempty `settings` patch for the existing
    `nr_color.configure` settings/experiments structure. Names are optional
    private labels and never enter the visual-review prompt.
-   A `conversion` also requires `rationale`, `domainHypothesis` and
    `producerEvidence`, and an explicit active-insertion profile in
    `settings.experiments`. Supply `domain`, `transform`, `exposureSource` and
    `exposureMultiplier`. Floating-point storage or appearance alone does not
    establish a linear input domain. Captured current/previous exposure are
    distinct candidates.

The automatic configuration fingerprint covers Upscaling and NR Colour.
It is not a universal lock or snapshot of every mod's graphics settings.
Preserve the prepared scene, build and MO2 environment receipts separately;
unverified changes outside these APIs remain an environment limitation.

The runner derives NR master/mode from the condition. Raw and NR-off use
`legacy_raw`; Managed identity uses identity profiles with manual multiplier
one. Preserve Source retains its explicitly chosen detail/appearance
parameters. The runner forces `captureFrameEvidence: true`, keeps expensive
colour `diagnostics` and `transportBypass` disabled, and changes the
display-only switch without changing the effective input profile or epoch.
The runner enables `captureEngineExposure` consistently for all conditions,
including NR-off and Raw, so exposure drift has the same observation path.

Create an immutable private plan before dispatch:

```powershell
build/hmd-python/Scripts/python.exe tools/nr-color/hmd_assess.py plan --spec build/hmd-input/spec.json --regions build/hmd-input/regions.json --seed 19417 --output build/hmd-input/private-plan.json
build/hmd-python/Scripts/python.exe tools/nr-color/hmd_capture.py --plan build/hmd-input/private-plan.json
```

The second command is plan-only. The plan retains explicit `candidateOrder`
so JSON object key ordering cannot change the frozen randomization.
The plan assigns random opaque candidate
IDs, retains the seed/settings mapping privately and starts with three
unchanged NR-off baseline sequences. Each sequence requests at least twelve
stereo pairs at 500 ms intervals. Three independent repetitions randomize
candidate order and repeat its reverse, with flanking baseline returns.
Each inference condition also has shown/hidden/shown and hidden/shown/hidden
blocks. Display-only sources remain associated with their own inference
condition and never replace NR-off or Raw.

An explicit `intervalMs` overrides the default before the plan is frozen;
the controller accepts integers from 50 through 60000 ms. Native PNG
backpressure and recorder budgets still apply. The default is two samples
per second, and even 50 ms samples cannot qualify HMD frame-rate flicker.
Reports retain actual spacing and always include the aliasing/missed-flicker
uncertainty. Temporal verdicts describe only the sampled acquisition cadence.

## Capture through the selected live transport

The current executable runner implements the established controller lane.
Before selecting it, search the complete callable tool catalog. If direct
`mcp__devbench_vr__` tools are callable, use that lane and produce the same
campaign-index contract; do not run the controller alongside it. The runner
rejects a transport-selection receipt that reports direct tools available.

The explicit transport-selection JSON records `selected: "controller"`,
`directDevBenchTools: []` and the actual `catalogCheckedUtc`. It is a receipt
of discovery, not permission to manufacture a different live endpoint.

The prepared-session JSON retains `sceneFingerprint`, `ownershipReceipt`,
the flat pinned `runtimeIdentity`, `insertion` (`upscaled_center` or
`final_ldr_pre_ui`), the exact CSX `producer`, separate `devbench` physical
identity evidence, `screenshotSessionId`, full `configuration`, and an
absolute physical `recordingDirectory` from the owned MO2 workspace. An
optional `maximumRenderFrameAge` defaults to zero; a retained-source test
must explicitly declare a larger bound. Never infer host binary identity
from the MO2 mod name alone.

`ownershipReceipt` contains the absolute active MO2 `configPath`, public
`leaseId` and owned MO2 `sessionId`; it does not contain an access credential.
The runner rechecks the exact public ownership state before mutation/capture.
`devbench` pins physical `path`, `bytes`, `sha256` and
`buildIdentity: {version, sourceCommit}`. The runtime runner preserves the
verified DLL snapshot and an `identityReceipt` with observed version/process,
then records `runtimeMatched`, `observedVersion`, `observedPid` and
`physicalModulePathVerified`. Version/process matching alone is explicitly
insufficient to claim that the loaded module has the same physical hash.
Keep the separate exact enabled-provider MO2 receipt and AIO/host build
receipts for deployment qualification.

With those actual paths and producer values available, the live command is:

```powershell
build/hmd-python/Scripts/python.exe tools/nr-color/hmd_capture.py --live --plan $plan --prepared-session $prepared --transport-selection $transport --automation-root $automationRoot --runtime $runtime --evidence-dir $newEvidenceDirectory --workspace-manifest $workspaceManifest --artifact-path $deployedCsxDll --expected-build-id $buildId --expected-artifact-sha256 $dllSha256 --expected-cell $cell
```

The variables come from the retained workspace/build/runtime receipts. No
default machine path, game launch, transport switch, ROI/camera mutation or
ownership takeover is implied by this command. It preflights advertised
contracts, uses revision/fingerprint ownership guards, waits for fresh applied
stereo evidence, awaits terminal captures and preserves recorder coverage.
It stops dispatching if ownership or cleanup becomes uncertain. Its index
retains every failed and unrun schedule entry.

## Import committed evidence and prepare blinded review

```powershell
build/hmd-python/Scripts/python.exe tools/nr-color/hmd_assess.py analyse --campaign $campaignIndex --output $newAnalysisDirectory
```

The campaign index pins the plan path/SHA-256, scene, fixed settings, CSX
producer, screenshot session and DevBench identity. Its sequence entries
contain `scheduleOrdinal`, a committed final `manifest` descriptor, the
exact `expected` applied configuration, recorder coverage and motion evidence.
Unrun entries instead contain `notRunReason`. The runner writes this contract.

`expected` contains full `configuration`, the opaque
`configurationFingerprint`, `colorRevision`, active-insertion scalar
`inputEpoch`, `insertion`, native source `planes.left/right`, and any declared
`maximumRenderFrameAge`, `requiresCapturedExposure` and `exposureAge`.
The configuration fingerprint is the producer's `xxh3-128-json` identity;
it is not a file SHA-256. The analyser compares the complete configuration as
well as the opaque fingerprint. File integrity always uses SHA-256.

The importer verifies actual `hmd_submission` with rejected fallback,
including abbreviated sequence-child request metadata through the exact
preserved parent request ID, child ordinal and contract version. Both parent
requested/effective capture descriptors and the child's effective descriptor
must retain the strict source and encoding policy; no manifest is rewritten.
It verifies
committed PNG size/hash/encoding/native extents, same acquisition and eye-pair
provenance, exact colour configuration/revision/epoch, real inference versus
disabled/source display, outer visible output outcome, and captured-exposure
age where applicable. Camera matrices and world-position adjustments are
checked against the fixed source frame. Requested cadence never substitutes
for actual acquisition timestamps.

With screenshot capability `nrCaptureDiagnostics.schemaVersion=1`,
`captureFrameEvidence` pins CPU-only companions at the accepted stereo
acquisition. Enable colour `diagnostics` for private measurements and
`captureEngineExposure` for engine observations as before. Terminal
receipts and sequence children include immutable
`actual.captureDiagnostics`: exact `transactionId`, `exposures`,
`measurementBatches` and per-key `measurementRequests`. Frozen
`actual.acquisition.nrEvidence` is never rewritten. Private measurements
still precede final masking and do not prove presentation.

Capture ownership survives rolling-history eviction and source-configuration
changes. Storage is bounded to 32 distinct keys per publisher; outstanding
owners are never evicted. Completion uses existing render-thread readbacks.
Finalization adds no GPU poll or wait and records unavailable/pending,
invalid, retired or exhausted evidence explicitly. Success, failure,
cancellation and shutdown terminal paths release the CPU ownership. Final
JSON follows normal receipt and manifest retention.

The importer prefers each child's transaction-matched companions. The
controller runner avoids live exposure polling for those children, including
explicit unavailable results; older captures retain exact-stamp lookup.
Finalized matching companions override earlier valid stamps, preserving
later ambiguity. Legacy lookup results stay separate from owned companions
so repeated stamps cannot create false duplicate matches across captures.
An acquisition can finish before a readback drains, so a committed PNG alone
does not guarantee complete diagnostics.

Pending exposure, unknown camera provenance, fallback or mismatched evidence
is retained as an exclusion. Original artifacts remain unchanged. The
analyser refuses to overwrite any existing analysis directory.
Preserved `recording.artifacts` are hash-verified and their scene/activity
metadata is rechecked; a summarized success flag cannot replace the raw
recording. The retained host snapshot and identity receipt are also verified.

Outputs include:

-   `private/captures/`: copied manifests, import records and original artifact
    bytes, including accessible failed or excluded captures.
-   `private/regions.csv` and JSON: per-frame/per-eye signed RGB shifts,
    encoded luma and fixed-transfer linear luminance, shadow/highlight
    percentiles, clipping/saturation, local contrast and edge energy.
-   `private/temporal.*`, `stereo.*`, `sequence-repeatability.*` and
    `baseline-repeatability.json`: actual spacing, successive changes,
    per-eye-reference stereo deltas and unchanged-baseline variation.
-   `private/exposure.*`: exact producer-frame/epoch/sequence measurements,
    captured-ratio changes and gamma evidence, retaining every unavailable
    sample and reason. Engine observations remain distinct from each eye's
    inference exposure binding.
-   `review/originals/` and `review/crops/`: anonymous byte-identical originals
    and identical native-pixel crops, with no colour correction or resizing.
-   `private/annotations/`: explicitly derived source/sample/signed-difference
    panels. The difference-map gain is fixed at two; 128 means zero difference.
-   `review/requests/`: compatible review-provider request objects and the
    task-specific structured schema, containing no mode mapping or metric rank.
-   `private/`: separate settings/ID, acquisition-order and presentation maps,
    exclusions, technical diagnostics and analysis/version provenance.

Local contrast is the mean 3x3 encoded-luma RMS contrast. Regional standard
deviation and adjacent-pixel edge RMS are retained separately. The motion
screen compares fixed-location gradient presence and sign above two code
values; it is conservative and does not register or warp images. Confounded
rows remain in the measurements and are excluded from repeatability claims.
The blinded reviewer must also inspect animation/occlusion: passing a camera
or edge check alone does not prove that the scene was unchanged.

## Run the existing fresh-context provider and seal its assessments

For each ready request, use the automation repository's generic provider:

```powershell
Import-Module (Join-Path $automationRoot 'tools/render-scale-qualification/AutomatedVisualReviewProvider.psm1')
$request = Get-Content -LiteralPath $requestPath -Raw | ConvertFrom-Json
$preflight = Get-CSXCodexVisualReviewProviderPreflight -CodexExecutable $codexExecutable
$receipt = Invoke-CSXCodexVisualReviewProvider -WorkingDirectory $request.workingDirectory -Passes $request.passes -CodexExecutable $codexExecutable -Preflight $preflight -DeadlineSeconds 90
$receipt | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $newProviderReceipt -Encoding utf8
```

Retain the complete receipt. Confirm the installed provider's current
signature before invocation; run no render-scale qualification scenario.
The generic provider supplies two swapped presentation passes and three
fresh replicate contexts, binding prompts, original images and responses by
hash. Each request includes complete short sequences, with both eyes and
fixed crops. Forward/reverse acquisition directions have separate requests.
The first complete phase sequence is preselected for visual presentation;
all repeated phases remain in the numerical analysis and retained originals.

Native imagery can be large. Missing images, a provider capacity/deadline
failure, insufficient replicates or absent image-capable contexts must stay
explicitly unqualified. Do not silently replace originals with thumbnails,
skip a presentation pass or ask the user to choose a winner.

Supply one retained provider receipt per ready request:

```powershell
build/hmd-python/Scripts/python.exe tools/nr-color/hmd_assess.py finalise --analysis $analysisDirectory --provider-receipt $receiptOne --provider-receipt $receiptTwo
```

Repeat `--provider-receipt` for all ready requests. The finalizer verifies
provider/input/response hashes and structured image coverage. It writes an
immutable `sealed-reviews.json` before reading the private candidate mapping.
Only then does it emit `final-assessment.json` and the written
`final-assessment.md` with separate colour fidelity, useful detail, temporal
stability and stereo results.
`imageReviewComplete` and `runtimeQualificationComplete` are separate. This
offline workflow always leaves runtime qualification false; it does not
silently promote a missing loaded-provider/deployment check into a pass.

Directional conclusions require agreement across all swapped replicates and
repeated signed effects beyond the unchanged-baseline envelope in both
capture orders. Disagreement or insufficient corroboration remains
indeterminate, with the original image judgments retained. Higher edge
energy alone is not useful neural detail. Neither zero RGB difference nor
fallback can make a source reference the best neural result. The physical
panel/lenses, untested scenes/routes, deliberate exposure challenges not run,
and NVIDIA's colour contract remain outside the demonstrated evidence.
