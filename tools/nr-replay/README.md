# Native NR replay

This optional developer executable replays captured native NR inputs through
the production `Runtime.cpp` and `D3D12Interop.cpp`. It does not install a
game DLL or change a production default. Its GPU timings cover the native
provider and its submission, not complete A/B/C rendering or CSX colour
reconstruction. Raw native output differences are not Preserve Source
quality assessments.

The standalone target substitutes dependency includes and extracts the exact
existing path, version and resource naming helpers. Runtime admission,
caller-path proxy, parameter-core selection and teardown are unchanged.
Source and SDK header hashes accompany every result. The executable uses
its own `Data/Shaders/Upscaling/Streamline` directory; `--runtime` may copy
the exact captured provider there, but never replaces a different DLL.

Configure and build from the repository root. `CSX_NR_DEPENDENCY_ROOT` may
point to another local checkout with the same populated external SDKs:

```powershell
pwsh ./tools/cmake.ps1 -S tools/nr-replay -B build/nr-replay `
  -G 'Visual Studio 18 2026' -A x64 `
  -D 'CMAKE_PREFIX_PATH=<existing-vcpkg-installed>/x64-windows-static-md'
pwsh ./tools/cmake.ps1 --build build/nr-replay --config Release
```

Inspect the actual runtime admission without evaluating a model:

```powershell
./build/nr-replay/Release/csx_nr_replay.exe --inspect `
  --runtime '<physical-provider>/nvngx_dlssnr.dll' `
  --output build/nr-replay-inspection-unique
```

The input is a completed `csx-nr-replay-input-v1` native capture. Screenshot
PNGs alone are insufficient: the bundle needs matching full initialized
colour, depth, motion and native output resources, source frames, source
and guide origins/jitter evidence, runtime identity and driver identity.
Every binary resource is hashed and tightly row-packed in its original
DXGI format. The capture must show a nonzero native edit. No synthetic
input is accepted as runtime performance evidence by the supplied workflow.

Install the matching DevBench-enabled AIO, enable developer mode and the
existing NR `captureFrameEvidence` experiment, then use the dynamically
registered `communityshaders.nr_replay` tool:

```json
{ "action": "capture", "frames": 1, "timeoutMs": 30000 }
```

Poll with `{"action":"status"}`. Cancellation requires the returned
`requestId`. Completed bundles are under `NRReplay` beside the SKSE log;
they are outside MO2's virtual Data tree. The tool does not change NR
settings. Capture full initialized auto-mask rectangles with real model
edits; partial character rectangles are rejected. Capture each A/B/C route
separately with matched settings. The replay then selects one/two native
regions from that complete source, retaining each route's provenance.

Capture reserves at most 512 MiB of logical staging plus CPU payload
(twice the tightly packed bytes); driver allocation padding is excluded.
This limits high-resolution stereo bundles to very few frames. Request
one frame for static throughput first. A short consecutive bundle can
exercise history handling with explicit small sample counts, but cannot
establish moving-scene temporal quality. Capture overhead is excluded from
the standalone provider measurements.

```powershell
./build/nr-replay/Release/csx_nr_replay.exe `
  --manifest '<capture>/manifest.json' --output build/nr-replay-input-check-unique `
  --validate-input
./build/nr-replay/Release/csx_nr_replay.exe `
  --manifest '<capture>/manifest.json' --output build/nr-replay-results-unique `
  --runtime '<physical-provider>/nvngx_dlssnr.dll' `
  --warmup 3 --samples 8 --seconds 180
python tools/nr-color/replay_report.py --help
```

Output directories must be new. `--case ID` selects a bounded case, such as
`calls-two`, `capacity-compact` or `temporal-continuous`. Maximums are 64
measured samples, 32 warmup samples, 600 seconds, a 512 MiB input bundle
and 2 GiB of logical replay textures. Native calls cannot be preempted;
the deadline is checked between submissions, and readback/teardown use
bounded waits. Private provider allocations are observed through DXGI
process memory accounting, not included in the logical texture bound.

Static throughput repeats one frozen frame and resets every evaluation.
Cold creation releases features after a GPU-idle proof each iteration.
Temporal pairs consume the same consecutive captured sequence with fresh
equally initialized contexts, once continuously and once resetting every
frame. If there are fewer frames than warmup plus samples, the lane is
unavailable. Reduce the explicit sample/warmup counts for a short bundle;
the tool never loops a short temporal sequence or fabricates frames.
Temporal outputs are retained for a separate quality assessment.

Width, height, offset, aspect ratio and one/two-region call count vary at
fixed creation capacity. Full/compact creation uses integer crops of the
same source density and content, with identical source/guide phase; an
unaligned compact crop is unavailable. Alignment cases include 31, 32,
63, 64 and 65 pixels. These are experiments, not provider requirements.
The current two-region-per-eye limit remains; four/eight-region experiments
await the capacity task.

All outputs are private. A raw NaN canary (or deterministic UNORM pattern)
is uploaded before each evaluation, outside the GPU timing scope. Readback
records unwritten/nonfinite pixels inside the requested rectangle and
writes outside it. A zero-edit or unwritten result cannot qualify as fast.
The batch timestamp includes barriers and cold creation; per-evaluation
timestamps exclude creation. Each iteration waits for completion and
reads back output, so these are isolated submission costs, not pipelined
steady-state game throughput. CPU elapsed time includes diagnostics.

Native auto-mask remains enabled. Occupancy cases use deterministic binary
1%, 25% and 100% masks only in a CPU composite after native inference, with
the same native rectangle, inputs and capacity. They choose either the
source pixel or private native output exactly once. This synthetic proxy
does not measure the CSX GPU compositor or its colour reconstruction; no
unverified provider `ControlMask` is supplied. Selected pixel counts and
composite hashes are retained separately from the unmasked native edit.
No NR-specific Streamline feature query is made without its plugin loaded.
The generic viewport limit does not establish native feature capacity.
Short GPU captures can supplement these results externally; absence of a
capture is recorded explicitly.

Run input-contract checks without loading the provider or claiming timings:

```powershell
python tools/nr-replay/test_input.py build/nr-replay/Release/csx_nr_replay.exe
```
