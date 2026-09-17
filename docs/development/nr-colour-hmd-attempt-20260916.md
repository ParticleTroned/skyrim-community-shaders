# NR HMD colour capture attempt, 2026-09-16

The automatic campaign stopped at its first planned NR-off baseline because
the running DLL stopped publishing fresh camera evidence after the first
screenshot acquisition. No Raw, Managed identity, Preserve Source or
conversion comparison ran. Colour fidelity, useful neural detail, temporal
stability and stereo consistency remain **indeterminate**.

## Preserved evidence

-   Evidence directory: `D:\Coding\GitHub\skyrim-community-shaders\.tmp\worktrees\face-of-gogh-colour-managed-20260914/build/validation/nr-hmd-readiness-20260916T090800Z`
-   Complete run index: `campaign-index.json`
-   Capture integrity and every camera/acquisition frame: `capture-summary.json`
-   Preserved analyser originals and exclusions: `analysis/private/`
-   Frozen candidate schedule, settings and regions: `private-plan.json`
-   Local automation feedback: `AUTO-20260916-092605003-8E6F939A`

The scout and first planned baseline each wrote twelve native stereo pairs:
48 PNGs total, each 2468 x 2740 per eye, from `hmd_submission`, fallback
`reject`, encoding `sdr_srgb`. All artifacts matched committed byte lengths
and SHA-256 hashes, with no capture drops or failures. One of 24 pairs had
valid camera provenance. The remaining 23 are excluded; all 68 subsequent
schedule entries explicitly record why they were not run.

Scout acquisition frame 103983 matched camera source frame 103983. Scout
frames 104033 through 104401 and planned baseline frames 142828 through
143355 retained camera frame 103983 and returned
`source_world_matrix_unmatched`. The unchanged matrices therefore do not
establish zero physical camera drift. Actual intervals are retained per pair;
scout spacing was 516.299–597.106 ms. Sparse PNG capture cannot qualify HMD
frame-rate flicker.

Exact delayed engine-exposure companions were retrieved for all twelve
planned-baseline pairs. Their complete values are preserved separately from
inference bindings. The first companion reports a valid unit ratio and
frame-gamma exponent 1; this is not evidence of NVIDIA's colour contract.
Scout companions expired before retrieval and remain explicitly unavailable.

The original images show the selected Dragonsreach scene with a standing
character, seated character, armour, stone, deep shadows and firelit detail.
A fixed per-eye region policy was saved before candidate review. Full-size
image viewing encountered host base64 transport errors; original PNGs were
retained, with selection-only overviews and native-pixel tiles kept as
derived files. No blinded candidate reviewer was invoked.

## Runtime identity and admission

The complete direct-MCP catalogue exposes `communityshaders.nr_color`,
`nr_status`, `nr_configure`, `nr_readiness`, exposure diagnostics,
recording and screenshot actions. Live read-only NR and screenshot calls
succeeded. Only direct MCP was used for live DevBench operations.

The scene readiness gate passed; SkyrimVR PID 31920, start time
2026-09-16T08:43:06.6040464Z, answered on port 8921. Only HUD Menu was open.
The route was Upscaled Centre with character rendering and visual isolation
enabled; the retained foveated centre area was approximately 0.98.
General NR and the FOV-only route were not tested.

The loaded CommunityShaders module's actual mapped backing file was:
`D:\Skyrim Mods\mods\CSX_AIO-CommunityShaders-2eef86720-20260916T082850Z-faceofGogh-color\SKSE\Plugins\CommunityShaders.dll`

-   Build ID: `80fb139bfbeec23b32ebea94b98cbc5053f6c7f914e31aecdf1fd22cc6fe5003`
-   Compiled source: `2eef86720e5c6a48e6fa9ad93506a1f2093d0f2f`, clean
-   Size: 24,922,112 bytes
-   SHA-256: `c0436272458a865fa43e501db5d231ef2e457417356c9c8a4c7594fa2976ad18`

These match the adjacent build manifest and preserved AIO receipt. The loaded
DevBench mapped backing file was
`D:\Skyrim Mods\mods\Devbench\SKSE\Plugins\devbench.dll`, 2,417,152 bytes,
SHA-256 `9e4455b501f0a5c6286df36fb7e8c3f1067b8542ce024e49822a1d7cbb7154f8`.
Its version identifies compiled PR9 merge checkout `7f60c2b`; the existing
handoff proves source-tree equivalence to merged main `5734b26`.
GetMappedFileName was used to resolve actual backing files instead of
accepting the VFS virtual module paths. Exact enabled loose providers were
enumerated; neither DLL existed in Overwrite or unmanaged Data.

MO2's selected profile and legacy lease did not constitute a prepared campaign
session. After that finding was reported, the user explicitly assigned this
manually launched session to the task and authorized continuation. That
exception is retained in `user-session-authorization.json`; no prepared
session ID was invented and the existing MO2 lease/profile was not changed.

Automation remained on authorized `dev` commit
`bd52844884c9026a254639d91b847cb60248d300`. The installed plugin remained
`0.9.0+codex.20260916085755`. Neither checkout was switched.

## Importer correction

Actual screenshot sequence children contain an abbreviated requested object:
`clientId=sequence:<parent-id>`, `commandId=frame:<ordinal>`, action
`capture`, contract major 1. The analyser previously required a full
requested capture descriptor in every child.

The analyser now accepts this form only with that exact preserved parent
identity and ordinal. Parent requested/effective and child effective
descriptors must all satisfy strict HMD source, rejected fallback and native
PNG encoding checks. Raw manifests remain unchanged. Both offline import and
the canonical capture validator use this same check.

Validation performed in the task checkout with the retained
`build/validation/hmd-python-deps` on PYTHONPATH:

-   `python tests/neural_color/test_hmd_assess.py`: 31 passed.
-   `python tests/neural_color/test_hmd_capture.py`: 22 passed.
-   Repository Git wrapper `diff --check`: passed.
-   Real campaign import: correctly rejected with
    `no accepted sequences; exclusions and originals retained`.

No runtime C++/HLSL code, production colour default, binary or package was
changed during this capture attempt. The importer correction is retained
separately from the subsequent runtime repair.

## Camera fault investigation and next boundary

Camera attribution currently depends on `globals::CacheFramebuffer`,
called through startup-detoured D3D11 Map/Unmap methods. First screenshot
staging calls `Util::ProtectImmediateContext`, which can enable D3D11
multithread protection. Prior exposure investigation already established that
this runtime can replace per-context D3D11 method targets while the original
detours remain on old functions.

The exact first-capture boundary and source path make changed Map/Unmap
dispatch a concrete hypothesis. This attempt has not measured the active
Map/Unmap targets, so the dispatch explanation is not yet a proven root cause.
Simply relabeling cached matrices with newer frame numbers would manufacture
provenance and is not an acceptable correction. A corrected instrumentation
build needs to demonstrate continuously fresh source-frame matrices across
repeated native sequences before the colour comparison can continue.

The subsequent [camera upload repair](nr-camera-upload-fix-20260916.md)
records direct process inspection, the confirmed dispatch failure, its
regression test and the independently portable runtime correction.

## Restoration

Original complete Upscaling and NR Colour configuration matched the
pre-mutation snapshot after guarded restoration. TimeScale was read as 20,
temporarily set to 0 to fix the lighting clock, then verified restored to 20.
Both recordings stopped with their exact correlation guards, with no limits
or unrecorded tails. Final screenshot status reported zero active sequences,
pending operations or outstanding capture jobs; recording status was idle.
Skyrim and MO2 remained running. No save, profile, shader cache or mod package
was replaced, and no performance measurement was claimed.
