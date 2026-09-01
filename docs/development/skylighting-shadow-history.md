# Skylighting shadow history

Each probe stores its last 32 accepted directional-shadow samples as a
bitmask. An accepted sample shifts out the oldest bit and appends the new
visibility. After 32 accepted samples, no earlier history remains,
regardless of the preset's frame interval or slice schedule.

Jitter has an independent per-probe cursor through the 32 noise offsets.
The cursor advances when an on-screen probe attempts a directional-shadow
sample, including rejected jitter positions. This lets a stationary probe
near a projection or cascade boundary try the next offset. Rejected
samples preserve visibility history.

| Probe condition                           | Visibility history                      | Jitter cursor                       |
| ----------------------------------------- | --------------------------------------- | ----------------------------------- |
| Valid cascade sample                      | Append sampled visibility               | Advance                             |
| Valid position beyond shadow coverage     | Append fully lit                        | Advance                             |
| On-screen probe, rejected jitter position | Preserve                                | Advance                             |
| Offscreen probe                           | Preserve                                | Preserve                            |
| Shadow data unavailable                   | Append fully lit                        | Preserve                            |
| Newly exposed probe                       | Initialize fully lit before this update | Initialize zero before this attempt |
| Feature disabled                          | Preserve                                | Preserve                            |

## Resource contract

`Skylighting::AccumFramesArray` uses `R16_UINT`. Bits 0–7 store the SH
accumulation count; bits 8–12 store the shadow jitter cursor. The two
fields progress independently, including when a probe is outside the
current occlusion-map quadrant. The SH blend still uses a calculation
count of 256 after a stored count of 255; only the stored count saturates.

The existing allocation and reset paths initialize both fields to zero.
Resources are transient, so saves and settings need no migration. The
shader and DLL must be updated together because the shader requires the
wider texture allocated by the DLL.

The wider state costs one additional byte per probe: 1 MiB for the
Performance grid, 3.375 MiB for Balanced, and 8 MiB for Quality/Hoshipa.

## Regression

`SkylightingProbeSlice` executes the production compute shader on D3D11
WARP with both SE/AE and VR layouts. It checks slice isolation, temporal
history replacement, independent cursor progression, rejected-sample
recovery, and packed-state interaction with SH accumulation.

The test belongs to the `controller_tests` build target and the
`ControllerTests` CTest label. Run it from a configured controller-test
build with `ctest -C Release -R "^SkylightingProbeSlice$" --output-on-failure`.
