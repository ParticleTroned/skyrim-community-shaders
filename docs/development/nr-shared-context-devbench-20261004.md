# Shared original-coordinate NR context experiment

## Purpose and qualification boundary

The [output-equivalence investigation](nr-output-equivalence-investigation-20261004.md)
identified one enlarged, original-coordinate context per eye as the most
promising live qualification candidate. In that frozen-input fixture, a
256-pixel halo measured 6.2335 ms against 8.6245 ms for two independent
regions per eye, a 27.72% native-time reduction. It was closer to full-eye
NR but still differed by mean 0.9479/255 and maximum 11/255.

This implementation makes that candidate testable in game. It does not
establish exact output equivalence, unchanged perceived quality or a live
performance gain. The normal production path remains unchanged. All new
renderer policy, state and branches compile only with DevBench enabled;
the session-only experiment defaults to off and adds no saved UI setting.

## Input context and output ownership

For eligible reduced-resolution C character NR, the renderer replaces the
independent native requests with one request per eye. `enclosing` unions
the existing inference rectangles, expands by the selected halo, aligns
outward to the existing provider grid and clips to the available input.
`full_eye` evaluates the complete available per-eye processing grid. With
FOV cropping active, that means the active crop, not uncropped HMD input.
Neither mode packs, translates or rearranges the source image.

The [live follow-up](nr-shared-context-live-20261004.md) found no useful
coalescing when the production planner already selected one call per eye.
`enclosing` now retains the original execution path in that case, reporting
`no_coalescing_opportunity`. In mixed batches, single-call eyes retain their
original inference context. Only multi-call eyes receive enlargement.
`full_eye` remains an explicit reference regardless of invocation count.

The original disjoint output rectangles and their tracking metadata remain
intact. Both private eye outputs are evaluated and reconstructed before
external writes. Every copy domain is checked before either eye is copied.
Only the original rectangles are written, followed by the existing exact
character-mask composition. Raw output uses original source coordinates;
colour reconstruction uses the corresponding context-local coordinates.
Halo and gap pixels are input context, never additional output ownership.

Admission requires stateless, equal-grid C requests with character
isolation, automatic masking, proven sampling support, no native upscaling
and no manual control mask or UI correction. Contexts must meet the
existing minimum experimental geometry. Ineligible requests retain the
original execution route and publish a reason. Active measured-plan
experiments and transport bypass prevent application. Configuration
rejects combination with compact inputs or shared source transport.
Native execution failures retain the existing failure/quarantine handling;
the experiment does not retry a failed native evaluation.

## DevBench controls and evidence

Use `communityshaders.nr_color` with the existing revision guard when
changing an inspected configuration:

```json
{
    "action": "configure",
    "experiments": {
        "sharedContext": { "mode": "enclosing", "halo": 256 }
    }
}
```

Modes are `off`, `enclosing` and `full_eye`. Halos are 0, 64, 128 and 256;
`full_eye` ignores the halo. `reset_experiments` restores off/256 along
with the other existing experiment defaults. Configuration and reset use
the existing completed-frame command queue; revision validation occurs
inside the admitted command. Rejected or expired unclaimed requests cannot
mutate the registry later. Read-only actions remain outside that queue.
Changing effective context policy invalidates only the C input epoch and
does not enable colour processing.

Inspect `nr_status` at `neuralRendering.sourceTransport.sharedContext`.
The main and submit observations identify the source frame, requested and
planned invocation counts, actual application, completion, reason,
inference context and original ownership rectangles. A successful settings
reply alone is not evidence that the experiment was applied.

Frozen execution records use the context policy
`experimental_shared_original_coordinates`. They contain `ownedOutputs`
and its exact summed `ownedOutputPixels`; scalar `ownedOutput` is null so
an old reader cannot silently interpret the entire context as ownership.
`inferenceContext` and `inferencePixels` describe actual native work.
The frozen selection has phase `selection_before_native_execution` and
omits a terminal success claim. Final execution fields report the outcome.
The maintained transaction reader validates the disjoint array and delayed
join, and cost keys include its exact geometry. Failed-before-evaluation
records retain their real reset/attempt state.

## Validation and next live comparison

The original producer and validation below are historical. The
[live follow-up](nr-shared-context-live-20261004.md) records subsequent
measurements, output-equivalence rejection and the replacement no-cache AIO.

Policy tests exercise alignment, clipping, geometry and sampling-support
admission, one through eight ownership rectangles, and coordinate-coded
scatter copies with untouched gaps. Registry and JSON tests cover guarded
configuration, immutable ownership, separate inference/output areas and
legacy evidence. Python regressions cover admission and comparison keys.
Build, CTest, preset, producer identity and archive receipts are preserved
under `build/validation/nr-shared-context-20261004/`.

The test AIO is VR-only with DevBench on and Tracy off. Packaging verifies
the DLL against its producer manifest, preserves the compatible optimized
VR shader cache and omits the Horizon Fix installer choice. It does not
install the package or launch Skyrim. No shader source changes are needed.

After installation, retain the same scene, camera, character selection and
colour settings. Bracket `enclosing` halo 0/64/128/256 and `full_eye` with
`off`, confirming applied decisions and unchanged owned rectangles in
frame-attributed evidence. First compare captured inputs offline where
possible, then qualify native-HMD appearance, stereo and motion with the
same original output ownership. Measure native time and the complete
preparation/copy/composition path separately. Restore off afterward. Exact
output and live cost/quality qualification remain outstanding until those
measurements are performed on this producer.

For each cost bracket, explicitly opt into the shared-context comparison
axis with the three completed screenshot-sequence manifests:

```text
python tools/nr-color/cost_report.py before.json candidate.json after.json --comparison-axis shared_context --output build/validation/shared-context-cost
```

This permits differences only at `color.experiments.sharedContext`; all
other configuration remains fixed, including reconstruction, exposure,
preservation and region limit. The report retains raw configurations,
requires baseline restoration, verifies the applied frozen selection and
requires unchanged original output ownership per eye throughout the
bracket. Without this option, different shared-context settings retain the
default configuration-mismatch rejection. Comparable timings never adopt
a production profile or establish output equivalence.

Native replay capture still requires a fully initialized input grid. Use
`full_eye` to capture the immutable offline source, then derive enclosing
contexts in replay. Capturing a partial enclosing context is deliberately
rejected unless it covers that full grid; the initialization proof is not
relaxed for this experiment.

## Final build and archive evidence

The VR Release DLL was built with DevBench on, Tracy off and automatic
deployment off. The preserved producer identity is:

-   Build ID: `8cb747096fd6a8f5c2c0d8b81fb53e5e761d8a0f39fe3ffd2a2b695c790896cc`.
-   Compile source: `b5524f22289e3c527a18f348a695a69061836ee7` plus dirty digest
    `f68dcc39f74ed898d1be5735f434305aba31b12a34ec3de1ed7bdd8187866c1e`.
-   DLL: 31,659,520 bytes, SHA-256
    `78c7eb9387298a4f3d05b0bb20eb2e53a9acd671fa07b99c304923855b932cef`.

`tools/cmake.ps1 --build build/sctx1004b --config Release` built the DLL,
`controller_tests`, `shader_tests` and the isolated `AIO` stage. The final
complete Release CTest run passed **230/230**, with zero skips or failures
in 124.60 seconds. Preset regression/check, scoped hooks, diff checks,
producer manifest, compiler identity and unchanged source/dependency
checks passed. The focused Python suites include 43 transaction-evidence
tests and 22 cost-report tests. Production-off and DevBench-on execution
JSON test variants both pass.

Earlier evidence is retained: a local validation helper first selected
multiple installed Python paths and was corrected before running tests.
The first complete CTest run passed 229/230; three geometry-test widths
omitted the intended outward 64-pixel alignment. The expectations were
corrected, diagnostic output improved, and the focused test and complete
final suite passed. Runtime geometry was unchanged by that correction.

Final receipts are `validation-summary-03.json`, `validation-run-03/`,
`preset-refresh-validation.json`, `cache-reuse-provenance.json` and
`archive-verification.json` under the evidence directory above. The full
source snapshot stayed unchanged through packaging. This final validation
record was appended afterward; the compile identity above remains the
authoritative identity of the DLL.

The archive is
`dist/CSX_AIO-main-vr-nr-SharedContext-DevBench-VR-ShaderCache-20261004-8cb747096fd6.7z`:
201,736,094 bytes, SHA-256
`76c6f1d2e37ba20d9c378194f4341f9129f2536d7198446eab4bde5b4751a867`.
Its adjacent receipt and checksum identify the complete package. All 394
extracted files match the staged inventory. The six cache files are byte
identical to the preserved production cache, with 4,301 optimized VR
records and zero developer records; shader sources, feature metadata,
version and cache ABI match. No installation, game launch, live image
comparison or new performance measurement was performed.
