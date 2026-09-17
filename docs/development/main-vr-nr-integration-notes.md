# Neural Rendering integration findings

This records the cross-cut integration of NR source `d69bdb7eb` with the
CPU-regression branch tip `60179f5b5`. The implementation is in the separate
`main-vr-nr` worktree; the original checkout is preserved.

## Integration decisions and issues fixed

-   Source character category bits overlapped the target's additive-lighting
    bit. Additive lighting stays at bit 8; category bits are 9–10 and exclusion
    is bit 11. C++ authoring and HLSL decoding use the same allocation, with a
    C++ assertion against accidental overlap. Target two-sided basic grass
    rendering and material guards remain in place.
-   The source native OpenVR hook had a four-argument declaration that did
    not match the target's validated native ABI. The port retains
    `void(RE::BSOpenVR*, ID3D11Texture2D*)`, the native stereo boundary,
    compositor serialization, and completed-pair relatch servicing. NR scopes
    nest within that boundary, use default native flags, suppress nested
    submissions, and restore their previous state with RAII. NR-disabled
    submissions do not acquire the NR boundary mutex.
-   Source screenshot target generations duplicated the target's stronger
    publication and retained-resource ownership. NR evidence attaches to the
    target's retained source; generation/device validation and staging remain
    within the target publication lease. Evidence is collected only for a
    pending capture, before accepted submission, and excludes keepalive work.
    The duplicate screenshot Present calls from the automatic merge were
    removed.
-   HDR exposure observation shares seven D3D draw hooks. Underwater fog keeps
    its existing pre-draw composition through a small `BeforeDraw` callback.
    Tonemap draw hooks retain target feature-data refresh and b5/b6 rebinding
    while adding the source exposure producer scope. Device creation retains
    the target validation contract.
-   The independent source save/load mutexes conflicted with target ordinary
    save recovery. One target mutex now serializes engine observations,
    recovery provenance, mutation/persistence deadlines, and world-transition
    windows. An ordinary save does not arm a world-replacement window; actual
    loads do. Wrap-safe deadline policy is retained.
-   Streamline retains the target coherent frame-token coordinator, immutable
    submit temporal snapshot, authoritative provider/profile checks, lifecycle
    mutation gates, and relatch drain invalidation. Exact source crop affines,
    per-viewport successful crop history, and NR pass evidence are integrated
    into those contracts. Diagnostics remain conditional; telemetry locking
    and copies are skipped unless NR or DevBench diagnostics are requested.
    Exact crop descriptors replace ambiguous floating pinhole parameters.
-   Category capture remains before deferred composite and preserves target
    scene-depth completion/copy policy. VR alone expands the category target;
    flat runtimes retain the original vertex-AO target format.

## Submit transaction adversarial assessment

The source pair proof was weaker than the CPU regression branch's native
producer identity. The port retains `SubmitBoundaryIdentity` as the public
submit ABI and requires the target `ProducerProof` before reading either
peer's color, depth, or motion data. Retained NR outputs also match that
proof, publication generation, settings, source descriptor, menu layer,
thread, and submit flags. Color, depth, and motion owners stay retained for
the pair. A revoked target presentation admission rejects cached NR output.
The source combined-texture proof cannot authorize a peer by itself.

Full-resolution NR now has a submit route independent of vendor foveation.
Its normal DLSS or FSR base prepares complete eye guides and then runs the
late NR pass before menu composition. The common FOV guide preparation is
reused when the full-resolution mode requests FOV-only NR. NR guide failure
keeps the completed normal vendor pair without evaluating its history again.
Vendor `Deferred` results retain the target cycle fallback and reset rules.

The merge split scene sharpening from presentation composition. This left
normal, replayed, and FSR replay paths without their UI/HMD finishing work;
all three now invoke both phases, while a completed NR pair invokes the
presentation phase once after NR. The ordinary cached-output state also
records NR temporal/settings metadata so repeated eye submits can reuse the
correct output without rerunning temporal evaluation.

Disabled NR does not hash its settings, publish its route snapshot under a
mutex, or update its temporal admission atomics on ordinary submit. Target
conditional vendor diagnostics, frame-token coordination, provider profile
admission, and native boundary proof remain authoritative.

## Focused validation

The temporary harness is preserved locally at
`build/nr-integration-tests/CMakeLists.txt` and builds existing production
policy tests and WARP shader tests. It is not a committed build system.

Commands run successfully:

```powershell
pwsh ./tools/cmake.ps1 -S build/nr-integration-tests -B build/nr-integration-tests/out -G "Visual Studio 18 2026" -A x64
pwsh ./tools/cmake.ps1 --build build/nr-integration-tests/out --config Release --parallel 3
ctest --test-dir build/nr-integration-tests/out -C Release --output-on-failure
pwsh ./tools/cmake.ps1 --build build/nr-integration-tests/out --config Release --target character_mask character_mask_bounds --parallel 2
ctest --test-dir build/nr-integration-tests/out -C Release --output-on-failure -R character_mask
python tests/neural_color/source_contract_test.py Contracts.test_vr_camera_observes_validated_engine_upload Contracts.test_exposure_observes_actual_draw_bindings
```

The first CTest run passed ordinary-save state/concurrency, independent
world-transition policy, and crop continuity (3/3). The second passed
production character mask and bounds HLSL synthetic WARP tests (2/2),
including category precision, isolation, dirty regions, depth rejection,
packed-eye offsets, and bounded readback. The two exposure/camera wiring
contracts passed. The first focused compile found a duplicated getter during
integration; it was removed before the passing build.

The native DLL build and actual SE/AE/VR runtime validation are separate
checks; these focused tests do not establish native hook ABI execution,
visual fidelity, vendor NR dispatch, runtime stability, or performance.

After the submit/state fixes, the focused harness was rebuilt and the same
CTest command passed all five tests together (5/5, 4.37 seconds). This run
also includes the mask-bounds test's newly added mono and stereo cases.
Compiler integration found an extra closing brace after the native-layout
submit guard and a duplicated menu-context declaration; both were removed.
The subsequent Release DLL build completed with zero warnings and zero
errors in `build/ALL/main-vr-nr-build-6.log` (elapsed 01:17:16.17).
This establishes native C++ compile/link integration, not runtime behavior.

## Additional target-preservation review

The forced SSS hook installation from the source NR startup path was
removed. Character category authoring is now part of the target's shared
lighting-geometry hook, so installing the SSS feature's own hook while it is
unloaded would add per-draw actor traversal with NR disabled. SSS's own
installation remains idempotent to prevent recursive thunk installation.

The source paused-menu continuation is admitted only with the target's
current completed two-eye producer proof as well as a sealed UI layer and
NR temporal admission. A retained older world frame cannot replace current
depth/motion producer evidence. Resource preparation can invalidate a menu
layer; that also revokes the preliminary NR admission.

The flat-runtime category-format and shader gates remain unchanged. The
new mono mask-reduction tests establish the shader's indexing behavior, not
SE/AE character NR support. Character NR requires VR's authored category
render target; the feature's capability/admission surface handles that
restriction explicitly.

The following focused production-extraction fixtures were updated and run:

```powershell
pwsh ./tools/cmake.ps1 --build build/ALL --config RelWithDebInfo --target scene_depth_plain_test scene_depth_devbench_test vr_relatch_native_boundary_test --parallel 3
ctest --test-dir build/ALL -C RelWithDebInfo --output-on-failure -R "SceneDepth_|VRRelatchNativeBoundary"
ctest --test-dir build/ALL -C RelWithDebInfo --output-on-failure -R neural_rendering_submit_pair_contract
```

The three compiled fixtures passed (3/3). They exercise authored-category
capture order before deferred composite, frame/category/extent/jitter
identity and all admission gates, plus native NR boundary cleanup for
nested, throwing, null-texture, and disabled submissions without changing
target relatch behavior. The submit source-contract check passed (1/1),
requiring the target two-argument raw-texture ABI, producer-proof peer
admission, retained guide owners, and overlay composition into a separate
submit texture after NR. The obsolete source ABI assertion was replaced;
its original four-argument declaration would read nonexistent arguments.
