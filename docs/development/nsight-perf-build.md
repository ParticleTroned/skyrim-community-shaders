# Optional NVIDIA hardware-counter builds

CSX can include the Nsight Perf SDK D3D11 range profiler in developer builds.
Both `DEVBENCH_BRIDGE=ON` and `NVPERF_SUPPORT=ON` are required. The SDK option
defaults off; enabling it without the developer bridge is a configuration
error. Production builds contain no counter collector, controls or tool.

Supply an extracted SDK explicitly using `NVPERF_SDK_ROOT`. This directory
must contain `include/nvperf_d3d11_target.h`, the x64 import library and
`bin/x64/nvperf_grfx_host.dll`. The build does not download the SDK.

```powershell
pwsh ./tools/cmake.ps1 --preset ALL -DDEVBENCH_BRIDGE=ON -DNVPERF_SUPPORT=ON -DNVPERF_SDK_ROOT=<sdk-directory>
```

Use a separate build directory for this configuration. CMake records the
option in build provenance and stages the SDK DLL in the developer AIO at
`SKSE/Plugins/CommunityShaders/NvPerf`. The runtime delay-loads that library;
missing or unsupported SDK/GPU combinations report unavailable rather than
preventing CSX from loading. Existing public release presets keep the
developer bridge off.

Developer controls appear on the Profiling page. The matching versioned
DevBench tool is `communityshaders.nvperf`; its registered schema describes
capture limits, polling, cancellation, results, and terminal events.
Each exported CSV has an adjacent JSON record with build identity and
the frame IDs used by its counter passes. Export errors remain distinct
from counter-collection failures.

This integration measures D3D11 immediate-context ranges across live frames.
It does not instrument the D3D12 queues used for parallel-eye NR and never
changes their scheduling. Samples requiring several counter passes assume
comparable frame workloads; the tool does not establish deterministic replay
or whole-frame/per-eye timing coverage. Counter collection perturbs timing.
CSX pass ranges do not require enabling the ordinary timing profiler or
changing global frame annotations. Engine-specific ranges still require
their existing annotation hooks. RenderDoc must be detached before capture.

Build changes should be qualified with SDK support both on and off, the
developer bridge off, and each affected runtime. Capture validation should
include missing SDK, unsupported GPU, cancellation, device loss, incomplete
metrics and parallel-eye NR. Source review alone does not establish runtime
compatibility or measurement accuracy.
