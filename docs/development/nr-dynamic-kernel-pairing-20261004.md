# Native-layout kernel pairing qualification

The installed adapter rejected all three live A/B/C routes because its
complete launch hash included the earlier test's fixed X/Y dimensions.
It also assumed adjacent regions were compatible. The captured scene
instead contained two different ROI shapes per eye; corresponding shapes
could be paired across eyes without changing either model context.

## Implementation

Graph-family admission retains the exact provider, modules, entries,
158-stage order, Z dimensions, blocks, shared memory, packet sizes and
command schedule. Only X/Y launch dimensions are excluded from the
family identity. Complete launch signatures remain available and must
match within every pair. The adapter tries all three complete pairings
of four regions and retains unchanged region identities, parameter
packets, input/output buffers, dimensions, origins and history.

Tensor and completion-buffer span checks use bounded actual dimensions
instead of fixed 160 x 160 tensors. Private scratch and dependency checks
remain mandatory. An unmatched family or incomplete compatible pairing
uses the original path with a specific visible reason. A matching graph
is admission evidence; attributed private-launch counter deltas are
required to prove actual batching.

The older replay selected-stage batch path now uses compatible pairings,
checks both launch geometries before submission, and records actual grids
and descriptor IDs. It cannot silently launch a second unequal region
with the first region's grid.

The explicit `--qualify-native-layout` replay preserves captured native
formats, unequal colour/guide grids, tuning and runtime provenance. It
evaluates immutable frame zero with independent fresh features and
`static_reset_each_evaluation`. This does not reproduce captured history
or change default replay admission.

## Preserved live results

The measured DLL producer was Build ID
`e96a2f7f7be84686bb5e51ffd6aec8a9a3a71d02b57892c5656570aa92d9aae0`,
based on source `998af941b89a58f323006f4a88cdee44c3c37f07` with its
recorded dirty source digest. The physical enabled AIO DLL, manifest,
provider and kernel payload were verified. Camera position was retained;
AI was off for stationary measurements and restored before moving checks
and clean game shutdown. Settings were not saved.

Each performance capture resolved 180 frames and passed its guards.
Numbers are stereo NR GPU mean / CPU self mean in milliseconds:

| Route | Automatic single | Independent multi-ROI | Selected batched, original fallback |
| ----- | ---------------: | --------------------: | ----------------------------------: |
| A     | 20.5450 / 1.5143 |      19.2872 / 2.5623 |                    19.1409 / 2.8323 |
| B     | 19.8011 / 1.9107 |      19.0238 / 2.8566 |                    19.8216 / 2.7347 |
| C     | 10.7562 / 1.7279 |      14.2701 / 2.5447 |                    13.4298 / 2.3877 |

Private launches and batched frames were zero in those live captures.
Different contexts, intermittent bounds and history resets prevent exact
quality or production-cost qualification. C evaluated approximately 23%
fewer pixels in independent mode but cost approximately 33% more GPU time
than its enclosure. No production cost profile was adopted.

The session ended with 270,066 successful evaluations/output commits,
79,183 stereo successes and 83 successful reset attempts. NR, stereo and
reset failures, device removals and quarantines were all zero. Eight
bounded mask/reset/mode/master transitions and short moving-actor checks
passed. Skyrim exited normally and RootBuilder released its game hooks.

All six native HMD image sequences completed: 72 stereo pairs, 144 images.
A and C comparisons were within their repeat variation after excluding
pending-bounds context changes. B retained larger chronological brightness
variation despite unchanged context rectangles; history/bank assignments
also differed. These images show original fallback, not private-kernel
quality, and the B observation remains unresolved.

## Independent native-output audit

Nine captured-layout lanes compare original, shared-body N1 and shared N2
for A, B and C. All 252 saved outputs and 72 captured input payloads were
rehash/length checked. All 168 original-to-replacement comparisons were
byte exact. Actual private coverage is 84 N1 outputs and 48 N2 outputs;
36 N2 warmup outputs used original kernels and are recorded separately.

The four-region model chain uses 632 logical launches. Measured N2 samples
execute 316 physical batched launches with pairs `[0,2]` and `[1,3]`.
All 63 samples, native evaluations, descriptor integrity, configuration
matches, terminal GPU-idle/restoration checks and six replacement
retirements passed. Original and replacement contexts match exactly
within each route, and all reset repeats have identical slot output.

A/B retain 1512 x 1680 R11G11B10 colour/output and 1008 x 1120 guides,
with rectangles `(256,768,192,192)` and `(576,704,936,960)` per eye.
C retains 1008 x 1120 RGBA8 with rectangles `(384,512,320,448)` and
`(768,640,240,480)` per eye. Guide mapping follows captured native layout.

The measured replay executable SHA-256 is
`0f8a4da73abf0324f156fe3a7162e15822e233b4edbe82279ba9c9e5ee90d378`;
its original executable is preserved separately from subsequent rebuilds.
The provider SHA-256 is
`8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206`.
Source and generated-code hashes are retained in every result. The
independent audit is under
`build/validation/nr-kernel-live-20261004T173942Z/independent-offline-output-audit.json`.

This proves native output only for fixed, reset inputs. Final colour
reconstruction, composition, DLSS and HMD publication, temporal-history
equivalence and new-DLL live performance remain unqualified. Instrumented
replay timings showed substantial variation and establish no additional
production performance claim.

## Additional replay shutdown investigation

An additional C layout, `(192,320,816,800)` and `(448,64,192,256)`,
completed original and N1 evaluations but both processes exceeded their
180-second guards at NGX SDK bootstrap shutdown. Their running receipts,
outputs, logs and measured executable remain preserved; they are failed
qualification lanes. The second run began while the first process was
still alive after its logged GPU retirement. Neither timing window is
an isolated performance measurement.

The replay retained successfully restored probe COM owners through SDK
shutdown, unlike the production adapter's ownership order. Cleanup must
release safely restored owners before backend shutdown and close the
initialized device instance. Those ownership corrections and idempotent
failure reporting are implemented and covered by shutdown-order tests.
A fresh original-kernel repeat still exceeded its shutdown guard, so the
correction did not resolve the stall.

The same layout also exceeded its guard with both kernel-chain hooks and
module capture disabled. A separate live thread-stack capture places the
main thread's wait in `NvTelemetryAPI64.dll`, called through the driver
`_nvngx.dll` during SDK bootstrap cleanup. This excludes a requirement
for private batching in that reproduction. It does not establish that
telemetry is the underlying cause or qualify the incomplete receipts.

Repeating with the same executable and unchanged inputs outside the
restricted sandbox completed normally in approximately six seconds for
native, original, N1 and shared N2. The executable SHA-256 is
`f2dde3946fef6d898860b93311c479b68fb4f2c3a081e256015e784d06b75fce`.
The execution-environment receipt and both live stack captures remain
with the local evidence. Native GPU qualification requires normal driver
IPC and telemetry access; the restricted failures remain preserved and
excluded. Real SDK shutdown is retained without a bypass or forced exit.

## Validation boundary

Release `NeuralRenderingUI` and `NeuralRenderingControls` passed. The
replay's nine C++ tests and 34 input-admission tests passed after the
dynamic-grid and shutdown-ownership corrections. The VR DevBench DLL and
controller/shader test targets compiled; all 231 main CTest checks passed,
including shader validation. Scoped hooks passed for the UI and kernel
changes. Final artifact validation is recorded with its exact producer
rather than inferred from those earlier passes.
The broader user-reported whole-panel UI reversion remains unconfirmed;
see [the recovery UI record](nr-ui-runtime-recovery-20261004.md).

To repeat the local build and CTest checks:

```powershell
pwsh ./tools/cmake.ps1 --build build/nr-ui-compatible-kernels-20261004 --config Release --target CommunityShaders controller_tests shader_tests
ctest --test-dir build/nr-ui-compatible-kernels-20261004 -C Release --output-on-failure --no-tests=error
ctest --test-dir build/nr-compatible-pairs-20261004 -C Release --output-on-failure --no-tests=error
```

Full logs, CTest XML, exact replay command journals, physical producer
copies and independent image-byte audits remain under `build/validation/`.
No replacement DLL was installed and no new game was launched.
