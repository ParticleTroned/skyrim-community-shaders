# main-SE incremental synchronization — 2026-11-02

## Source and target

This synchronization covers every commit after the previous source pin through
`da73f53d8f6429d2d968cabde3c3eec9f3a04ab8`: **39 main-VR commits**. The SE
base is the published `72424bfbee2dc03d416c9606269461f02c2aee0f`.

| Boundary | Commit |
| --- | --- |
| Previous imported VR source | `f140c878138fe2783a6a4ad40420a3a2a996ccd0` |
| New VR source | `da73f53d8f6429d2d968cabde3c3eec9f3a04ab8` |
| Published SE base | `72424bfbee2dc03d416c9606269461f02c2aee0f` |

The published SE follow-ups `ebbd2ece`, `12bd83a4`, and `72424bfb` remain
ancestors. Their native build/FFX compatibility, settings/HDR interface,
presentation hooks and managed shader-cache corrections are preserved.
The project release version remains governed by `cmake/CSXVersion.cmake`.

## Result by subsystem

### Depth, culling and native presentation

Reverse Z uses SE/AE camera construction and publication relocations
`75711/77520` and `75694/77503`. Scene target conversion is transactional,
with rollback before activation and matching depth-stencil/raster state.
Camera/projection history, shader-cache generation and scene depth consumers
follow the native mono camera. No VR native offsets or headset facilities
were introduced.

SE consumers include grass/Hi-Z, terrain blending, screen-space shadows,
SSGI, clustered lights, dynamic cubemaps, water, SSR, volumetrics, TAA,
SSAO/fog and the upscalers. Depth format preparation and the current camera's
encoding are distinct: an initial menu camera can still use conventional
depth after D32 targets have been allocated. SDK conversions and shader
consumers must follow the camera that produced the sampled scene.

DLSS receives native depth with a matching inversion flag. FSR and SE frame
generation receive standard depth. D3D12 presentation matrices use that same
standard convention. SE frame generation, HDR, UI composition, shared-resource
ownership, synchronization and presented-frame acknowledgment stay in the
existing SE implementation. Upscaling/sharpening use one image and native
render/output dimensions, with no stereo crop, eye seam or HMD FOV assumptions.

### Neural rendering

The mono pipeline now uses the official NGX SDK entry points and parameter
contracts. Initialization, capabilities, scratch allocation, feature creation,
evaluation, diagnostics and teardown fail back through the existing SE path.
The build pins SDK 310.9.16 inputs; runtime admission still accepts compatible
user-supplied 310.8+ versions, subject to NGX capability checks.

The port adds budgeted startup, staged reservations, exact model/guide
prewarm, compressed-grid reconstruction safeguards, consistent tuning,
reuse of planned geometry and deferred success-detail formatting. Color inference
and engine-depth guide capacity remain independent. Guides are normalized
at the SDK boundary, while authored native scene depth follows the active
camera convention. Reduced-resolution inputs carry native jitter separately
from existing final-scene guide compensation.

The renderer stays mono, preserves SE HDR/capture ordering and blocks NR
when frame generation conflicts. Reference-color, native control-mask/jitter,
reconstruction-tuning and face-sampling experiments remain developer-only and
session-only; settings cannot enable them in a production build.

Retained-frame diagnostics report the actual native jitter, scaling, mask,
motion and feature-upscaling inputs, alongside the settings that requested
them. Each frame retains its own face-observation and admission evidence;
querying a captured frame does not consult later scene, fence or SDK state.
Prewarm records explicitly contain no face observation.

### Appearance and UI

Far grass billboards receive light on both faces. Aurora grading uses the
CPU aurora classification in pixel shaders. Skin saturation preserves linear
lighting energy, with the original SE scene-color input, alpha, and male/female
control buffers retained.

Adaptive Balance's export mode now has an in-menu folder chooser. The chosen
folder is session-only, must be an existing absolute path, and is validated
before replacement. Browsing/cancelling/selecting does not export anything.
Directory enumeration is bounded; UTF-8 paths and filenames are retained.
Atomic export failures keep the original preset and do not use destructive
write fallback. Import lookup continues to use the established preset folders.

Developer mode exposes Reverse-Z controls in shader settings with restart guidance;
shader replacements required by active reverse depth cannot be disabled.
New user-facing strings use the existing translation system.

### Build, cache and distribution

The NR SDK archive/header/license are pinned and checked. Build extraction
selects headers and the original license only. All packages reject NR runtime
DLLs; users install their own compatible runtime. SE's separate DX11/DX12
Streamline payloads and frame-generation DLLs remain intact, and additional
original SDK notices accompany the existing payloads.

Prebuilt SE cache compiler defines derive from the selected ReverseZ feature
state, including Utility's distinct macro value. Existing SE HDR/Fresnel,
character-category companions, native ImageSpace indices, managed pack
identity and AIO installer behavior are preserved. The NR settings contract
is revision 29. Feature versions are monotonic for SE; the SE SSS version
advances from 3.2.1 to 3.2.2 rather than importing the older VR number.

## Commit-by-commit disposition

The machine-readable inventory is
[main-se-sync-2026-11-02-commits.json](main-se-sync-2026-11-02-commits.json).
“SE preserved” identifies an existing native owner that already supplies the
relevant guarantee. VR-only rows do not introduce a substitute feature.

| Source commit | Disposition | SE result |
| --- | --- | --- |
| [`ee369af0a`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/ee369af0a8e6421d10447b9dd8c25ae3c72154e4) — fix(grass): light both far billboard faces | direct | Two-sided far grass billboard lighting; retain SE billboard geometry and culling ownership. |
| [`84dd0f515`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/84dd0f5154ff1de39129fbaa459d1e93ad388436) — fix(vr): avoid grass visibility lock recursion | VR-only | VR parent-node visibility hook and its lock recursion have no SE hook; preserve existing SE grass group locks. |
| [`0a162b833`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/0a162b833407703c4a063e05d0fff4fba6d985a5) — fix(nr): guide compressed model grids | adapted | Mono compressed-grid guide reconstruction, footprint confidence and minimum-grid validation. |
| [`2764dd530`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/2764dd5308d07f8b8c624af9171a795252bc2c1e) — feat(rendering): enable reverse Z depth precision | adapted | Reverse-Z on verified SE/AE camera hooks, transactional D32 scene targets, depth consumers, native upscaling and preserved FG boundaries. |
| [`4ae44e500`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/4ae44e500fdfc1dee11388f60571468035ca7b44) — fix(adaptive-balance): restore aurora sliders | direct | Aurora pixel processing follows the CPU aurora classification without requiring a vertex-only SKY_OBJECT define. |
| [`420684826`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/420684826463e2f594b0ee0068577d7bb03cef1c) — fix(nr): protect compressed reconstruction | adapted | Mono reconstruction admission/coverage safeguards and standard guide versus native scene-depth handling. |
| [`aabf7d0b0`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/aabf7d0b0fe10c1e55472229660897ea763517c3) — feat(adaptive-balance): choose preset export folders | adapted | Desktop preset folder browser, session-only export destination, bounded enumeration and atomic UTF-8 exports; keep default import lookup. |
| [`0f29416dc`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/0f29416dc6055ac2909ced289c91f94c6bbd75cd) — fix(nr): make reconstruction tuning consistent | adapted | Consistent reconstruction modes/tuning in mono settings, serialization, prewarm and shader configuration. |
| [`c8958790f`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/c8958790fc7895356e0d9f46769e759565c98eeb) — fix(sss): preserve brightness during skin tuning | adapted | Shared luminance-preserving skin colour helper while retaining SE original-scene colour, alpha and per-frame human controls. |
| [`a1807f351`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/a1807f3511b50b46925d0ba68e7f9fc741c9ae7b) — build(nr): avoid shadowing in DevBench builds | adapted | DevBench shadowing corrections adapted to mono; portable production-method fixtures pass. The full native bridge translation unit remains uncompiled here. |
| [`78bff3266`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/78bff32666c59edbad6608ee63f35ebaea6abf71) — fix(nr): budget and stage native startup | adapted | Budgeted/staged native startup, residency reservations, retry timing and mono fallback. |
| [`78ea03c92`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/78ea03c92783853f48f702156a346d42a4a056cb) — fix(rendering): harden reverse Z shader paths | adapted | Reverse-Z hardened through SE depth consumers, including TAA, SSAO fog, terrain, shadows, SSR and water. |
| [`0f12c3ae0`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/0f12c3ae0b4e1b33efc5a7834f21da7b5fd0fd12) — fix(nr): accept rounded stereo compression grids | adapted | Mono rounded-grid admission and minimum dimensions; stereo eye-layout tolerance is omitted. |
| [`7e20bdcd9`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/7e20bdcd9f2d7bd350c098ab6527f22483d925e4) — test(rendering): align fixtures with runtime contracts | adapted | Production-contract fixtures adapted to native camera, scene depth, single-image ownership and SDK boundary behavior. |
| [`8687ea1f2`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/8687ea1f2d3afbf936b2a2971fea952f580b511d) — build(nr): allow the shipped scene-selection shader | adapted | Explicit mono shader payload allowlist, retaining the shipped MonoSelection helper; no VR scene-selection shader is introduced. |
| [`a10f091c2`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/a10f091c206e8dabf65f0f67ac27d55ba29e97f4) — build(presets): refresh the NR settings contract | adapted | NR settings contract revision 29 and matching mono defaults/serialization; no VR MGO preset bundles. |
| [`7b365f1fb`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/7b365f1fb0ac81bc4f22f570b5ead24fcde03be6) — build(upscaling): isolate DLSS diagnostics | SE preserved | VR crop/per-eye DLSS diagnostic storage and hot-path mutexes are absent; preserve required SE FG/token synchronization. |
| [`a454736cd`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/a454736cd069246664cf801b3e042151cc8d0846) — fix(upscaling): unify evaluation generations | adapted | Preserve SE resource/FG generations and add depth-convention invalidation at the native SDK evaluation boundary. |
| [`c35b96f87`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/c35b96f87749ab51d4f2ede4859f497bbd20e268) — refactor(nr): defer success detail formatting | direct | NR success details are stored as structured evidence and formatted only when queried. |
| [`2f26948da`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/2f26948dafc433a7ab36a528a990370a47b10fda) — refactor(upscaling): reuse fixed shader defines | direct | Fixed shader-definition lists avoid per-request temporary allocations. |
| [`6bc310862`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/6bc310862858264c3efa11a8ea33603140b75815) — refactor(upscaling): reuse owned texture descriptions | adapted | Operation-local owned color/motion texture descriptions reused through the SE resource transaction. |
| [`433d8e0be`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/433d8e0bede2493468c163a1c8b64e61890f0147) — refactor(nr): reuse finalised late input plans | adapted | Mono finalized geometry/guide plans reused in allocation, startup and evaluation instead of adding VR late-eye plans. |
| [`f99a60cc7`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/f99a60cc7d94f3e6c7321204a1ef34cac47ae80a) — fix(nr): prewarm projected model geometry | adapted | Prewarm exact projected model/color/guide dimensions and policy identity for mono evaluation. |
| [`c8cf385a0`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/c8cf385a0ca836fb5d4afedd19c43dc3520a675e) — refactor(nr): fuse eligible prepared centres | adapted | Preserve SE’s single fused preparation pass; dispatch native color/guide rectangles once, without stereo center copies. |
| [`d6dbff195`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/d6dbff19594a6a4b0e805620151c2608960e6020) — refactor(upscaling): tile DLSS encoder depth | adapted | Tiled mono DLSS depth encoder preserves donor order, ties, border behavior, barriers and FSR depth semantics. |
| [`f9ec85faa`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/f9ec85faa62ccc74669dce196ee6463d77f209ff) — refactor(upscaling): skip unused TAA cache setup | adapted | Avoid vendor resource/descriptor setup for native NONE/TAA; VR periphery-TAA caching has no SE route. |
| [`5f17c0f7a`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/5f17c0f7a8816fbe28795d705d4c63e1081274af) — refactor(nr): skip unused colour filter | adapted | Skip the 3×3 luminance residual estimator when lighting preservation is zero. |
| [`7745861e2`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/7745861e251284d56599d41ce9648bfa381b5b7e) — fix(upscaling): isolate fixed sharpening eyes | adapted | Fixed/adaptive sharpening validates complete mono output ownership; no packed-eye boundaries. |
| [`67ea23692`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/67ea23692b9e7fdf6a7bdb6a8240b5e0c3ebbb64) — refactor(shaders): use signed sharpening seam bounds | VR-only | Signed eye-seam sampling bounds have no SE seam; native image-edge handling is preserved. |
| [`774712c9b`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/774712c9b95d5696dd7cac6abbb96f7942607054) — docs(tooling): document Skyrim VR control routing | VR-only | SkyrimVR controller/overlay tool routing documentation is not applicable to the desktop SE/AE UI. |
| [`bc796ed8c`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/bc796ed8c58d7ad90312a1e0b02515ddcab219a4) — fix(nr): route neural rendering through NGX | adapted | Official NGX D3D12 initialization/capability/scratch/feature/evaluation/teardown with shared project identity and mono interop. |
| [`6fcc7d033`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/6fcc7d033a7745265d2eece6c5d3c59c5a3bc00f) — build(nr): keep NR runtime user-supplied | direct | NR runtime stays user-supplied; pinned build-time SDK cache, unchanged notices and package rejection of NR binaries. |
| [`f9204e922`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/f9204e922abfcd6a73a93f8d8bce21025b7d36a6) — fix(nr): honor official SDK input contracts | adapted | Official SDK parameter names, independent color and guide capacity, native jitter and depth conversion contracts. |
| [`76c7c60e9`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/76c7c60e98d2cd9994e6127d29a06cff4ef50a01) — fix(nr): harden SDK inputs and failure handling | adapted | SDK input validation, failure recovery, guarded experiments and unsupported-path fallback retain SE presentation. |
| [`6f4e103b9`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/6f4e103b9bdf35b68efa6d472aedc55f96e48e3e) — build(nr): add face sampling experiment | adapted | DevBench-only mono face sampling experiment; production rendering cannot activate experimental controls. |
| [`c0a2cafdd`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/c0a2cafdd1ad2d3f3e88b26f1dd6e2950c1fe858) — build(nr): fix SDK integration validation | adapted | SDK integration validation and mono SDK/policy/extraction fixture coverage. |
| [`b91d8a699`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/b91d8a69995e60a76929b27b8dd23f4587790b4c) — build(nr): resolve bridge and SDK test warnings | adapted | SDK and portable bridge-interface fixtures compile without warnings; full native DevBench bridge compilation requires Windows. |
| [`85917186c`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/85917186c7df11b95b09d93c8a704c95168c1116) — test(nr): align tuning descriptions with SDK presets | adapted | Tuning descriptions and reference-color experiments aligned with SDK presets in mono settings/diagnostics. |
| [`da73f53d8`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/da73f53d8f6429d2d968cabde3c3eec9f3a04ab8) — build(shaders): align versions and Reverse Z cache | adapted | Monotonic SE feature versions, generated version registry and Reverse-Z-aware prebuilt SE shader cache; preserve SE release identity. |

## Deliberate scope boundaries

VR headset submission, hidden-area masks, controller/overlay routing,
per-eye FOV cropping, stereo presentation, eye seams and VR render-scale
ownership are absent. The VR parent visibility lock hook and developer
controller-routing documentation therefore have no SE counterpart.

VR MGO preset bundles and their generator are not part of this SE branch.
Relevant NR defaults and settings contracts are represented by the mono
serializer/UI and compatibility checks. The VR color-capture asset verifier
is also absent; SE uses its mono shader allowlist, dependency compilation and
actual packaging tests. Source tests were adapted to native SE owners instead
of copying VR test harnesses into the branch.

## Validation and remaining environment limits

The initial test results are recorded in
[main-se-sync-2026-11-02-validation.md](main-se-sync-2026-11-02-validation.md).
The [second adversarial review](main-se-sync-2026-11-02-adversarial-review.md)
records the subsequent six corrections and final 121-test Debug/Release
results against the same source scope.
Portable and supplemental shader diagnostics do not establish a Windows DLL
build, native FXC bytecode, WARP execution, runtime GPU performance or actual
Skyrim SE/AE image correctness. Those require the Windows/game environment.
