# Packed-region native NR feasibility

## Question and implementation

This experiment tests whether amortizing native inference over multiple
character patches can reduce the per-call cost already measured in Tasks
2, 9 and 10. It does not change the game renderer or qualify a production
cost profile. The production AIO remains unchanged.

The current adapter accepts one colour/depth/motion/output set and one
rectangle per feature evaluation. Pinned NGX headers expose no verified
NR region-array or sparse-batch contract. CSX already batches the separate
evaluations into one GPU submission and retains feature handles; combining
their CPU wrappers would not combine the neural inference work.

The standalone replay now supports explicit bounded C rectangles and an
offline atlas campaign. Colour, depth and motion retain their original
bytes and guide alignment. Each eye is packed independently. Context may
overlap in the original image, while output ownership stays disjoint.
Motion bytes and their explicit captured conversion scale are preserved.
History resets on every evaluation. This is not a temporal A/B atlas design.

The campaign compares separate calls, one enclosing rectangle, and packed
atlases with 0, 64, 128 and 256 pixels of context, in forward and reversed
tile order. It verifies complete native receipts, immutable input hashes,
warm steady samples and every owned output crop. Exact RGB equality is the
initial feasibility gate; alpha differences are recorded separately. The
experiment does not substitute that gate for perceptual or HMD review.

Native GPU intervals exclude CPU atlas preparation, upload, readback and
scatter. Their savings are an upper bound on a possible benefit before
adding GPU preparation/composition. Padding counts as evaluated work.
Instrumented RenderDoc runs are kept separate from these timings.

## Reproduction and source identity

The source is frozen C frame 41177 from
`build/validation/nr-task9-11-stationary-20261003/native-input/manifest.json`,
SHA-256 `3c02792f5d203d2afca501583327f54b4f663293730972c4e85836ceabca12d6`.
Each eye uses 1008 × 1120 RGBA8 colour/output, R32 depth and RG16 motion.
The two manually selected face patches are `(192,512,128,128)` and
`(672,512,128,128)` in both eyes. They are fixed sample ownership, not a
claim of complete character coverage or live mask attribution.

Provider: NR 310.8.0, SHA-256
`8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206`.
GPU: NVIDIA GeForce RTX 5070 Ti Laptop GPU. Replay verifies the captured
adapter, LUID and driver version before executing.

The executable was built from `main-vr-nr` base
`1e07a237fa3e58804bed5a3a1203af600c191305` plus the standalone changes in
this commit. Runtime.cpp and D3D12Interop.cpp are compiled from the unchanged
production sources. Exact executable and compiled-source hashes are in each
native receipt; campaign.json pins the executable, input and Python tools.
No main DLL, shader or AIO rebuild is part of this experiment.

Measured executable SHA-256:
`9fbc0505d7841dc88c569e376ba699fb2221345da9f30c817ab306d37621a536`.
Runtime source SHA-256:
`9a2fe412cb880d942bf3bef180b843e6e666ce9b9429356527271001d95d7bd3`.
Interop source SHA-256:
`d63abf08e3ea7709202cb99ddffbaeaf0bd784937f85734cc229113af5ff23ee`.

Local evidence is preserved under
`build/validation/nr-packed-feasibility-20261004-v2`. Earlier preparation-only
directories remain local. They contain no native performance qualification.
The finalized summary SHA-256 is
`9aeffd6bd52b3d14e999a5143be5f618b8e1d6340a7a98b3feb83783ca5c7ce4`.
The campaign runs three repetitions, with three warmups and eight retained
samples per case; case order reverses on alternating repeats.

```powershell
pwsh ./tools/cmake.ps1 -S tools/nr-replay -B build/nr-packed-replay-20261004 `
  -G 'Visual Studio 18 2026' -A x64 `
  -D 'CMAKE_PREFIX_PATH=C:/src/skyrim-community-shaders/build/pnr1004b/vcpkg_installed/x64-windows-static-md'
pwsh ./tools/cmake.ps1 --build build/nr-packed-replay-20261004 --config Release
python tools/nr-replay/packed_replay.py prepare `
  --manifest build/validation/nr-task9-11-stationary-20261003/native-input/manifest.json `
  --replay build/nr-packed-replay-20261004/Release/csx_nr_replay.exe `
  --output build/validation/NEW-CAMPAIGN `
  --roi 192,512,128,128 --roi 672,512,128,128 --halos 0,64,128,256 --repeats 3
python tools/nr-replay/packed_replay.py run `
  --campaign build/validation/NEW-CAMPAIGN/campaign.json `
  --runtime build/pnr1004b/aio/Shaders/Upscaling/Streamline/nvngx_dlssnr.dll `
  --warmup 3 --samples 8
```

## Results

All 30 uninstrumented processes completed and shut down cleanly. The
report admits all 240 steady samples and verifies their retained output
hashes. Every row owns the same 65,536 stereo pixels. Times are the sum
of the native per-evaluation GPU intervals, in milliseconds, pooled across
24 steady samples per row.

| Configuration             | Calls, stereo | Evaluated pixels, stereo | Native median ms |    Change | Exact owned RGB |
| ------------------------- | ------------: | -----------------------: | ---------------: | --------: | --------------- |
| Separate                  |             4 |                   65,536 |           8.6305 | reference | repeatable      |
| Enclosing rectangle       |             2 |                  155,648 |           4.5940 |   −46.77% | differs         |
| Atlas, halo 0             |             2 |                   65,536 |           4.3170 |   −49.98% | differs         |
| Atlas, halo 0, reversed   |             2 |                   65,536 |           4.3120 |   −50.04% | differs         |
| Atlas, halo 64            |             2 |                  262,144 |           4.4605 |   −48.32% | differs         |
| Atlas, halo 64, reversed  |             2 |                  262,144 |           4.4620 |   −48.30% | differs         |
| Atlas, halo 128           |             2 |                  589,824 |           4.8610 |   −43.68% | differs         |
| Atlas, halo 128, reversed |             2 |                  589,824 |           4.8635 |   −43.65% | differs         |
| Atlas, halo 256           |             2 |                1,495,040 |           6.7675 |   −21.59% | differs         |
| Atlas, halo 256, reversed |             2 |                1,495,040 |           6.7675 |   −21.59% | differs         |

The separate reference is byte-repeatable across all runs. Its three
repeat medians are 8.6405, 8.6265 and 8.6295 ms. The reversed halo-128 case
contains a 20.713 ms native sample; it is retained, not discarded. These
medians do not establish a tail-latency guarantee. At a 90 Hz frame budget,
the 4.3135 ms difference between separate and halo-zero native medians is
about 38.8% of a frame, before accounting for GPU packing and composition.

All fused layouts differ from the separate reference. The enclosing
rectangle changes 99.87% of owned RGB pixels, maximum 22/255 and mean
absolute channel error about 3.38/255, despite retaining original full
source resources. Therefore the difference cannot be attributed solely
to atlas packing. Requested evaluation geometry itself changes output in
this fixture.

Reversing tile order changes output at every tested halo. With halo 128,
84.11% of owned RGB pixels differ between the two orders, maximum 11/255.
Halo 256 still changes about 90.6%, maximum 11/255. No compared alpha
values change. These are reproducible layout sensitivities, not a proven
internal mechanism or a perceptual quality verdict. Increasing context
alone did not establish equivalence. Halo 128 evaluates nine times the
owned pixels; halo 256 evaluates 22.81 times as many after source clipping.

## Native workload audit limit

The optional explicit capture path loads RenderDoc before device creation,
captures one warmed sample, records capture-library identity, and keeps
instrumented timings outside the cost campaign. The analyzer records
dispatch dimensions, shader identity, resources, API events and available
GPU durations; it requires an explicit completion receipt.

The real capture attempt used signed portable RenderDoc 1.46, library
SHA-256 `809da38e3867d9fd09cc5c30dd5310500dee75e999166d7a14ad1ef6e9eca65d`.
NGX D3D11 initialization returned `0xbad00002` (`FAIL_PlatformError`) before
any NR case or capture began. This generic result does not identify the
failing platform call. The process exited with the failure preserved at
`traces/atlas-h0/results.json`; no further hooked runs were attempted.
The analyzer's actual embedded-Python invalid-capture rejection test passes.
Valid native dispatch analysis remains unavailable on this capture route.
No private kernel classification or queue-gap attribution is claimed.

The supported next candidate is NVIDIA Nsight Graphics 2026.2 GPU Trace:
its [documented Windows minimum](https://archive.docs.nvidia.com/nsight-graphics/2026.2/ReleaseNotes/index.html)
is driver 591.86, below this machine's 610.88. Version 2026.3 requires
615 or newer. No installed `ngfx` or supported portable Windows package
was found; no NVIDIA tool installation or driver change was performed.
NVIDIA documents [D3D12 NGX workload visibility](https://developer.nvidia.com/nsight-graphics-2024_2),
but this private NR feature still needs a successful trace. Use verified
submission-count triggers and `--set-gpu-clocks=unaltered` under the
[GPU Trace CLI contract](https://archive.docs.nvidia.com/nsight-graphics/2026.2/UserGuide/gpu-trace-overview.html).

## Validation and decision

The standalone Release build passes. The compiler warnings are from
existing CommonLib headers. Native input admission has 13 passing tests.
The packed input/controller/report suites have 35 tests: 34 pass and one
Windows symlink-privilege check skips; absolute and traversal rejection
checks still run. Python compilation, scoped repository hooks and Git
whitespace checks pass. No SE/AE/VR game renderer validation is claimed:
no renderer source or shader changed.

Adversarial review addressed immutable provenance, finite padding,
ownership overlap, guide phase, unsupported format admission, existing
evidence preservation, bounded edited campaigns, exclusive GPU campaign
ownership, process exclusion, case-specific capture journals, case-sensitive
hash handling, and malformed or partial result rejection. The same native
geometry floor and existing replay sample-admission helpers are reused.

The experiment establishes that combining work into one inference per eye
can amortize a substantial native cost. It does not establish cost directly
proportional to owned pixels, equivalent output, or an in-game improvement.
The evaluated atlas adapter fails the initial strict output gate and stays
offline. No production profile, automatic planner adoption or new AIO is
made from these results.

Next, a provider-compatible workload trace must distinguish fixed native
passes from area-scaled work. Output qualification must first explain or
accept the measured rectangle/layout sensitivity under a defined quality
contract. Independent per-region context/history within one inference would
need a verified native batch/sparse contract or backend changes; packing
unrelated patches into an ordinary image does not supply that contract.
GPU packing/scattering, temporal histories and live stereo testing follow
only after a viable output contract is established.
