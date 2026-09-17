# NR colour HMD instrumentation and build handoff

The source audit began at `dd47c7c61b80dbbc5fd9cc7a709dbb9f4d200860`
on `work/face-of-gogh-colour-managed-20260914`. The resulting implementation
adds capture attribution and an automated image workflow. It does not change
colour shader math or choose a production default. Compilation and synthetic
tests do not establish a live HMD colour verdict.

Read the [assessment protocol](nr-colour-hmd-assessment.md) and
[executable workflow](../../tools/nr-color/hmd_workflow.md) for preparation,
capture, regional measurements and blinded review.

## CSX implementation

-   `nr_color.configure` accepts `experiments.captureFrameEvidence`.
    It defaults to false and enables CPU evidence retention without activating
    colour passes or changing inference input epochs. The separate
    `captureEngineExposure` flag requests existing HDR observations; the
    campaign enables it consistently, including NR-off.
-   The renderer freezes its actual configuration, input epochs and physical
    regions by route, render/source frame, generation and insertion identity.
    The outer route records attempts, successful processing, final commitment
    and fallback. Display visibility is separate from inference success.
-   `nr_status` exposes `requestedConfiguration`, its
    `requestedConfigurationFingerprint`, and `captureEvidence`.
    The configuration contains complete Upscaling and colour settings.
    The fingerprint is labeled `xxh3-128-json`; retain its full input object.
    It is not a cryptographic proof or universal mod-configuration snapshot.
    Artifact integrity continues to use SHA-256.
-   `nr_configure.expectedConfigurationFingerprint` rejects changed settings
    before mutation. Colour changes retain their existing revision guard.
    These complement selected-transport and MO2 ownership checks.
-   Engine framebuffer publication freezes both eyes' view, unjittered
    projection and position adjustment. Source-frame provenance and fixed
    matrix tolerances must pass; later tracking samples cannot substitute.
-   Successful presentation paths pin the producer record to the returned
    texture, eye and compositor cycle. Main-owned NR passed through submit
    upscaling keeps its main producer. Retained outputs and source-texture
    copyback keep their original producer.
-   The accepted OpenVR callback carries frozen evidence through staging and
    encoding without querying later configuration. Matching eyes are joined
    under `actual.acquisition.nrEvidence`; camera evidence also appears at
    `actual.acquisition.cameraEvidence`. Rejections retain both submissions.
    General screenshots remain available; insufficient attribution excludes
    an image comparison without discarding its originals.

The existing screenshot service remains authoritative for actual source,
native dimensions, colour encoding, publication/device identity, hashes and
committed manifests. Screenshot and NR generation counters are distinct.

## Delayed exposure

The CPU history retains 4,096 exact `{frame, epoch, sequence}` identities,
independently of the existing eight-sample status view. Completion must
retain resource, view, shader, sampler and layout identities. Ambiguity is
sticky. Missing, pending, evicted, retired and failed results remain explicit;
an unavailable scalar never means unit exposure.

`nr_color.capture_diagnostics` accepts one to 64 exact `stamps` and returns
their key, availability, reason and evidence. It neither polls nor waits
for the GPU. Source-frame engine exposure supports drift analysis for Raw
and NR-off; it does not claim that inference consumed that scalar. The
runner preserves companion receipts, and the analyser joins only exact
identities without rewriting capture manifests.

## DevBench and automation

The audited DevBench main update was
`ad84fd077154284099fb7e9fdf29680327afecff`. The remaining host fixes are in
[DevBench PR #9](https://github.com/ParticleTroned/devbench/pull/9):
advertise atomic observation in both schemas and guard `record.stop` with
`expectedCorrelationId` under the recording mutex before mutation.

The capture controller now uses the host keyboard-duration contract,
persists accepted VR tokens before polling, verifies exact-generation
restoration, checks recording limits, guards stop, and reads real sequence
children and nested metadata from owned manifests. Source and packaged
copies are maintained together.

The executable campaign selects the established controller lane only when
the complete callable catalog lacks a direct DevBench lane. It does not
invent an endpoint, take over another owner or mix transports. Plugin-cache
installation and runtime launch are separate from archive creation.

## Assessment behavior

Repeated unchanged baselines precede randomized, counterbalanced candidates.
NR-off, Raw, Managed identity, justified conversion, Preserve Source and
display-only source conditions remain distinct. Scene, camera and regions
are frozen before candidate review.

The analyser uses original native PNGs for signed colour/luminance shifts,
shadow/highlight changes, contrast, per-eye reference differences, stereo
differences and temporal repeatability. Confounded cases retain their
evidence and exclusion reasons. No normalization or registration is applied.
Anonymous originals, identical crops and fixed annotations feed fresh
automated review contexts. Responses are sealed before the private settings
map is read.

Colour fidelity, useful neural detail, temporal stability and stereo
consistency remain separate. Zero source-reference RGB error does not make
that reference the best neural result. Appearance cannot establish
NVIDIA's colour contract.

## Build and remaining live validation

Use `Internal-DLSSNR-AIO`, `DEVBENCH_BRIDGE=ON`,
`AUTO_PLUGIN_DEPLOYMENT=OFF`, Release and `Package-AIO-Manual`.
Initialize pinned submodules in this worktree and use a short isolated
build directory through the repository CMake wrapper. Preserve other
worktrees and their caches.

The all-runtime build stages six pinned normal DLSS modules and the
explicitly selected, fingerprinted local 310.8 NR runtime. Preserve its
internal packaging designation, committed source identity, producer Build
ID, adjacent `CSX.BuildManifest.json`, AIO receipt, and DLL/archive hashes.

DevBench remains a separate MO2 mod. Building the CSX bridge does not update
`devbench.dll`. Preserve the host build identity/hash separately. Host
inspection exposes version/process identity, not the physical loaded module
hash. Deployment qualification requires the exact enabled-provider MO2
receipt; the analyser retains this limitation explicitly.

Focused tests cover colour policy/WARP shaders, frozen stereo evidence,
exposure retention, screenshot/DevBench contracts, capture orchestration,
regional metrics and sealed review. Exact results accompany build evidence.
Synthetic fixtures validate tooling rather than image quality.

No game is launched or mod deployed by this implementation. Fixed-scene HMD
captures, useful-detail review, flicker and exposure recovery remain
untested until the prepared runtime campaign. Unsupported or stale source
associations are excluded.

General NR still uses the broad-FOV approximation, such as 0.95. FOV and
character-mask routes require separate fixed-region comparison blocks.
This work neither implements a dedicated general-NR route nor ports into
`main-VR`. Screenshots and recording stay outside performance campaigns;
verify owned captures inactive before timing. No performance claim is made.
