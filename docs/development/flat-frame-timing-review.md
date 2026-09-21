# Flat frame-timing adversarial review

Reviewed against main-VR base `8d5b006cf`. Only the flat CPU/GPU source
and its necessary asynchronous sample association are ported. Unrelated
NR profiler changes and the dirty NR worktree are not included.

## Findings addressed

-   Frame-generation samples were hidden by the UI but retained by the
    source. Reject unsupported presentation paths during acquisition and
    invalidate their history, preventing replay after returning to D3D11.
-   The draft added flat resources and bookkeeping to every shared frame
    slot. Move these into flat-only allocated state; preserve the existing
    frame-slot layout, resource allocation count, and capture lifecycle on VR.
-   A nested Present could consume the outer timing completion. Require
    ownership from BeginFlatPresent before completing that boundary.
-   Accept only S_OK presentations. Test probes, occlusion, failed Presents,
    and other non-presenting statuses cannot publish fresh timings.
-   Avoid QPC reads when the profiler is disabled.
-   Reject a pending Present if history was reset after its acquisition,
    preventing an old sample from being published into the new source epoch.
-   Update the existing extracted Present-hook test harness, including
    exact VR call order and zero flat capability queries on VR.

## VR isolation and shared behavior

A source comparison against the base confirmed identical definitions of
the legacy profiler BeginFrame, EndFrame, BeginPass, EndPass,
ResetFrameState, PublishImmediateCpuResults, and LatchCaptureRequest,
plus the FrameQueries structure. CaptureOpenVRGameTiming,
ApplyOpenVRTimingCache, GetAverageGameFrameMs, and NormalizeGameFrameTiming
also remain identical.

Runtime guards prevent flat initialization, clock reads, GPU timestamps,
history copies, and delayed-result accumulation on VR. No shaders, VR
presentation ordering, render-scale policy, or settings defaults change.
Shared code gains guards and optional timing fields; this is not a claim
of measured zero instruction overhead.

Both runtimes retain the ten-second initial settle, five one-second
sample blocks, ten-second comparison settle, five comparison blocks,
one-second restoration, ten-second cooldown, existing statistics, and
missing-metric tolerance. Delayed flat results use their original block
weights and cannot extend those phases or reuse a reset source epoch.

## Validation

Commands run from the main-VR worktree:

```powershell
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target CommunityShaders --parallel 4
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target controller_tests --parallel 4
ctest --test-dir build/ALL -C Release -L ControllerTests --output-on-failure --parallel 4
```

-   Universal SE/AE/VR Release DLL build passed.
-   All 138 controller tests passed after rebuilding the controller group.
-   FlatFrameTiming covers active VR no-op behavior, query allocation,
    whole-frame/pass-ring coexistence, delayed results, CPU independence,
    missing/disjoint/failed queries, failed/test/occluded Presents, ownership,
    unsupported-path recovery, resets, bounded history, and block weights.
-   ProfilerTiming resolves the flat timestamps through D3D11 WARP as well
    as exercising the existing profiler integration.
-   Scoped pre-commit checks and git diff --check passed.

The first full controller build exposed missing Present-hook mocks;
those were fixed before the successful rebuild and test run.
No in-game SE/AE or headset VR A/B performance assay was run, and no DLL
was deployed. Empirical FPS/frametime neutrality remains unmeasured.
D3D12 frame-generation CPU/GPU timing intentionally remains unavailable.
