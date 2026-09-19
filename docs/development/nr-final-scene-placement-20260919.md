# Final-scene Foveated NR and character defaults

## Reason and behavior

The tester reported overbright fire/lights with Foveated NR and Render
Scale off, then confirmed that selecting Final LDR (Pre-UI) corrected that
scene. This is user-reported visual evidence, not a new automated capture
or a general colour/performance qualification.

Foveated NR now always uses the existing final-scene path, after scene
post-processing and before UI, with Render Scale on or off. Full resolution
keeps its existing final-scene placement. Renderscale NR stays before DLSS,
including its independent optional FOV restriction. No shader, brightness
clamp, colour slider, source-density or provider-format change is added.

The shared mode policy resolves legacy saved insertion values, so an old
Upscaled Centre selection cannot keep Foveated on the early path. Settings
normalization writes the resolved placement; history keys, blend support
and status also consume that policy. The menu shows read-only placement
with a tooltip instead of an independent insertion selector. The DevBench
`insertionPoint` field remains an optional assertion: inconsistent requests
return `nr_insertion_point_conflict` before changing live settings.

Existing final-LDR baseline preservation, finite-value/UNORM handling,
alpha, stereo rollback, source/guide alignment and character mask contracts
remain in their shared implementations. This change adds no render pass,
copy or allocation implementation; choosing the late route uses its
existing staging resources. No performance equivalence is claimed.

Hair selection now defaults on alongside face and skin across SE/AE/VR.
Hair strength remains 0.65; NR and Characters only retain their existing
master defaults. Missing hair keys use the new default, and an explicit
saved hair-off value still round-trips unchanged. The three generated
unified presets inherit the same selection and final-scene placement.
Preset compatibility revision 8 is synchronized between generator and
loader; earlier marked packages require regeneration under the existing
compatibility policy. Unmarked user settings retain normal migration.

## Renderscale NR FOV default follow-up

Loading NR with no saved `neuralRenderingRenderscaleFov` value and restoring
NR defaults select the crop when FOV is enabled in VR. SE/AE and VR with
FOV off default to the full image. Explicit saved on/off choices still
win, and DevBench patches that omit `renderscaleFov` retain the live value.
Changing global FOV later does not overwrite the user's NR selection.

The load path also accepts normalization of valid legacy insertion values
0/1 to the mode's required placement. Other invalid values still reject
the profile before live mutation. Previously that generic range check
could reject a profile solely because its valid old placement migrated.
Regression coverage includes all runtime/FOV default combinations and
valid/invalid placement normalization; C++ execution remains pending.
Preset schema revision 8 remains compatible: explicit booleans are not
reinterpreted, and the shipped presets retain false because FOV is off.

## Validation

Passed without compiling:

-   `cmake -P tests/neural_rendering_devbench_contract_test.cmake`
-   `cmake -P tests/neural_rendering_submit_pair_contract_test.cmake`
-   `cmake -P tests/neural_multi_roi_contract_test.cmake`
-   `cmake -D CSX_NEURAL_COLOR_ROOT=<worktree> -P cmake/ValidateNeuralColor.cmake`
-   `python tests/neural_color/source_contract_test.py`: 8 tests.
-   `python tests/neural_color/transaction_evidence_test.py`: 27 tests.
-   Production extraction scripts for request validation, settings keys and
    full-resolution/Foveated crop planning, with `PROJECT_ROOT` and
    `OUTPUT_DIRECTORY` set explicitly.

Preset verification with
`pwsh -File tools/generate-unified-presets.ps1 -Check` passed for all three
tiers. A JSON comparison against HEAD confirmed only placement, hair and
the matching compatibility revision/hash changed in each. The loader
revision matches the generated metadata.

`git diff --check` passed through the repository wrapper. clang-format
22.1.4 was applied to edited C++ line ranges. Scoped pre-commit whitespace,
line-ending and Prettier checks passed; whole-file clang-format was skipped
to preserve unrelated legacy formatting.

CMake commands used the repository `tools/cmake.ps1` wrapper. Updated C++
coverage checks legacy placement values across A/B/C, matching/conflicting
DevBench assertions, stable history keys, late feather support, shared
colour latches and hair defaults/persistence. At implementation time these
C++ tests and DLL/shader builds were deferred under the user's no-build
instruction. The subsequently requested production build supersedes the
C++/DLL limitation as recorded below; no live game test was performed.

## Requested production build validation

The universal Release DLL compiled with DevBench and Tracy disabled.
The focused C++ host compiled and passed all 10 targets: NR UI, controls,
pipeline policy, FOV geometry, settings keys, colour route latching,
request parsing, feature settings, reduced selection and character settings.
The first run exposed one stale UI assertion expecting Hair to start off;
correcting the post-click expectation produced 10/10 passes. The initial
failure and successful rerun are both retained with the local AIO receipt.

Commands used the repository CMake wrapper to configure/build
`build/validation/nr-renderscale-fov/host`, then
`ctest --test-dir build/validation/nr-renderscale-fov/host/build -C Release --output-on-failure`.
Production uses `cmake --build build/ALL --config Release --target CommunityShaders --parallel 8`.
The AIO receipt records the final DLL source/Build ID, archive integrity,
extracted payload hashes and absence of FOMOD and compiled shader caches.
Shader execution and live game validation were not run.

Next runtime check: repeat the same fire/light scene in Foveated with
Render Scale off/on and confirm final-scene placement, paired eye output
and character hair boundaries. Keep Full resolution and Renderscale NR as
separate controls; do not infer their quality or timing from that scene.
