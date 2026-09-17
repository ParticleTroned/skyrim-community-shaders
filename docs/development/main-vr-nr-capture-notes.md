# Neural Rendering capture and DevBench integration

The port retains the CPU regression branch's screenshot renderer-context
ownership, publication identity, accepted-eye pairing, service dispatch,
and capture indicator behavior. Neural Rendering adds frozen per-eye
evidence to the existing accepted-Submit observer through an optional
final JSON argument. Captures continue to acquire their source through
the existing pending-capture gate.

Exposure and private measurement companions use bounded CPU retention
leases. Acquisition pins exact identities; terminal transitions finalize
their snapshots once and release the leases. Encoding delays cannot
replace evidence with a newer rolling-history entry. Missing, pending,
invalid, and capacity-exhausted companions retain explicit reasons.

Adversarial review found that joining malformed eye JSON could throw
before producing an unavailable-evidence result. The join now rejects
non-object evidence, non-boolean availability, and invalid eye identity
while preserving the received payloads. Tests cover these cases alongside
configuration, transaction, camera, and compositor-cycle mismatches.

The render-scale DevBench bridge keeps the target branch's qualification,
ownership, diagnostic, and main-thread dispatch paths. The source branch's
NR and foveation actions are added to the shared handler and descriptor.
The separate feature registers `communityshaders.neural_rendering`; the
legacy `communityshaders.renderscale` NR actions remain available to the
preserved capture tools. The historical `communityshaders.nr_color` tool
remains owned by the Neural Rendering feature.

`nr_configure` accepts `mode` as `full_resolution`/`0`, `foveated`/`1`, or
`reduced_resolution`/`2`, plus boolean `fovOnly`. Mode changes use the same
history-transition path as the feature menu. Status reports the effective
insertion point and arrangement, and frozen requested configurations keep
their legacy `upscaling.neural*` layout for capture-tool compatibility.
The shared `expectedBuildId` guard runs before strict action-field
validation. NR readiness observes the target branch's completed resource
publication generation instead of the source branch's removed hook API.
Status and the registered schema publish runtime support explicitly:
full-resolution rendering supports SE/AE/VR, while foveated,
reduced-resolution, character ROI, and stereo implementation controls
require VR. Flat unsupported enabled configurations are rejected before
mutation. The target flat G-buffer format and material authoring remain
unchanged. Readiness reports unsupported configurations instead of
silently accepting a character route with no authored category data.

Feature persistence review found that session-only multi-ROI experiments
could enter the feature settings file. Save and load now strip those
controls, along with character mask/debug experiments. Legacy Upscaling
NR settings migrate even when the old color feature section is absent.
An unsuccessful backend retirement preserves the prior configuration and
does not attempt another reset or overwrite the color settings. The
configuration parser also preserves live mask/debug experiment values
when applying unrelated persistent controls.
Frozen capture configurations and fingerprints explicitly include those
transient mask/debug controls, so distinct diagnostic output cannot share
a configuration fingerprint just because those values are not persisted.
DevBench configuration and implementation cycling likewise roll back
settings when backend retirement fails, and an unsuccessful explicit
renderer reset leaves character resources intact.

The source character capture assumed two packed eyes. Capture allocation,
projection, mask validation, early bounds dispatch, and bounds readback
now retain an explicit active eye count: one for SE/AE, two for VR. Mono
capture does not read the nonexistent second cached camera. The existing
mask shader supports both dispatch depths without a second implementation.
Readback reuse requires matching eye count as well as extent and source
identity. The target GPU pass macros replace the source-only profiler
macro at screenshot and character pass entry points.
The mono-safe helpers do not advertise character rendering on SE/AE;
that route still requires the VR category-authoring path.

Static contract reconciliation retained checks that exposed lost
integration invariants: hook installation idempotence, matching retained
vendor output to the NR configuration/source frame, and sealed-menu
continuity. The target's current producer proof remains mandatory for
menu NR dispatch; resource preparation that retires its UI layer revokes
NR admission. The target's shared main-thread claim policy, desired
render-scale profile, and persistence-only save notification replace
equivalent older source forms in the checks.

Frame evidence remains behind its existing default-off atomic opt-in.
Explicitly enabled evidence also records NR-disabled baseline frames;
restricting it to NR-enabled frames would invalidate comparisons.

## Validation

-   Built the six screenshot test targets with MSVC 19.51 through
    `tools/cmake.ps1`, using an isolated `build/nr-capture-tests` directory and
    existing dependency packages. No game or shader caches were changed.
-   `ctest --test-dir build/nr-capture-tests -C Release -R '^Screenshot' --output-on-failure`:
    six passed: API policy, manifest snapshots, NR evidence, NR diagnostic
    retention, dispatch, and capture Present handling.
-   Parsed the screenshot DevBench descriptor as JSON and checked its
    diagnostic output schema.
-   The production request-parser extraction test covers all three mode
    names and numeric values, optional FOV controls, combined character
    selection, unknown fields, wrong types, bounds, and nonfinite values.
-   The production feature Load/Save/Restore extraction test verifies
    transient persistence filtering and backend/color failure behavior.
    Together with the six screenshot tests, all eight focused CTests pass:
    `ctest --test-dir build/nr-capture-tests -C Release -R '^(Screenshot|NeuralRenderingRequest|NeuralFeatureSettings)' --output-on-failure`
    (0.17 seconds).
-   `python tests/neural_color/feature_registration_test.py cmake`:
    all six registration fixtures pass.
-   `pwsh ./tools/cmake.ps1 -P tests/neural_rendering_devbench_contract_test.cmake`:
    passed after semantic reconciliation with the target ownership,
    source-proof, main-thread, and resource-retirement contracts.
-   Built `character_mask_bounds_gpu_test` with the repository CMake wrapper;
    `ctest --test-dir build/ALL -C Release -R '^character_mask_bounds_shaders$' --output-on-failure`
    passes (3.48 seconds). The actual WARP shader now runs every category
    selection against both mono and packed stereo input, in addition to the
    existing bounds and connected capture/mask/depth cases.
-   Full renderer compilation and runtime capture verification are recorded
    in the accompanying implementation report; the focused tests do not
    establish live renderer behavior.
