# VR Depth-Culling Temporal Evidence

This record preserves the source-history, live reverse-engineering, and local
validation evidence used to design the bounded temporal recovery policy.

## Regression history

-   [PR 17](https://github.com/ParticleTroned/skyrim-community-shaders/pull/17)
    introduced the temporal readback hook in
    [`42e94d004`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/42e94d004c4a420b2350c597bae162f28c88b61e).
    On a producer/consumer pose mismatch it promoted every zero result to
    visible, with a maximum native object count of 4096.
-   [`348e37b7e`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/348e37b7e5a71420fbcc7dbc71ece406ea9b8189)
    made the original coherence thresholds explicit: 0.05 degrees and 0.1
    Skyrim world unit.
-   [PR 27](https://github.com/ParticleTroned/skyrim-community-shaders/pull/27)
    removed that blanket workaround in
    [`72f6e84ab`](https://github.com/ParticleTroned/skyrim-community-shaders/commit/72f6e84ab1e40a0185fabbd92929627e55cd753c)
    and changed fresh defaults to exterior-only depth culling.

The new policy restores temporal validation without restoring PR 17's
all-visible fallback. It also restores interior culling in fresh defaults while
retaining an explicit interior switch.

## Live Skyrim VR inspection

The analysis session used the user's compiled
[PR 43](https://github.com/ParticleTroned/skyrim-community-shaders/pull/43)
AIO. DevBench health identified `SkyrimVR.exe`, Skyrim VR 1.4.15, and a live VR
process. Ghidra inspected bounded memory dumps from that process; it did not use
an SE or AE executable as a layout proxy.

Preserved local capture names:

-   `20260826-165935-obb-readback-pr43`
-   `20260826-170004-obb-readback-target-pr43`
-   `20260826-170040-obb-culler-methods-pr43`
-   `20260826-170133-obb-culler-neighborhood-pr43`

The captures established these Skyrim VR 1.4.15 contracts:

| Contract           | Evidence                                                     |
| ------------------ | ------------------------------------------------------------ |
| Readback callsite  | RVA `0x132208B`, calling RVA `0x1356270`                     |
| Object count       | Culler offset `+0xB0`, maximum allocation count 4096         |
| CPU OBB transforms | Culler offset `+0xB8`, 64-byte affine transforms             |
| Result selector    | Culler offset `+0xC0`, valid values 0 or 1                   |
| CPU result arrays  | Culler offsets `+0xD0` and `+0xD8`                           |
| GPU OBB buffer     | Culler offset `+0xF8`                                        |
| Readback resources | Culler offsets `+0x100` and `+0x108`                         |
| Allocation sizes   | `0x40000` bytes for OBBs and `0x4000` bytes per result array |

The native OBB upload reads `count * 0x40` bytes from `+0xB8`. A sampled
record had three scaled oriented axes, a world-space translation in the fourth
column, and an affine final row. This is why Balanced recovery can derive a
conservative bound from already CPU-resident native data instead of adding a
synchronous GPU readback.

## Local implementation validation

The feature diff was built and tested on base `d52ef9a750`; the branch was then
updated to current `main-VR` head `9daec10c7`. At the user's request, the build
was not repeated after that source-only base update.

Passed checks:

```powershell
pwsh ./tools/cmake.ps1 --preset ALL -DBUILD_CONTROLLER_TESTS=ON -DBUILD_SHADER_TESTS=OFF
pwsh ./tools/cmake.ps1 --build --preset CSmain -- /m:1
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target controller_tests -- /m:1
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target generation_claim_test -- /m:1
ctest --test-dir build/ALL -C Release --output-on-failure -L ControllerTests --timeout 300
```

Results:

-   Release `CommunityShaders.dll` built successfully.
-   All 27 controller tests passed.
-   `VRDepthCullingTemporalPolicy` passed independently after final C++
    formatting.
-   `clang-format` and `prettier` passed for the staged files.

Not run:

-   The candidate DLL was not deployed to the user's AIO.
-   No post-change headset visual or frame-time A/B was run.
-   Shader tests were disabled because this change has no HLSL surface. An
    earlier configure attempt with shader tests enabled also encountered local
    ShaderTestFramework/NuGet SSL setup errors before generation completed.
