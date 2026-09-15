# NR character transition reproduction, 15 September 2026

## Live result

The running `c45eb42e16d5f6b8467184bbf91943d94e6796a7` DLL reproduced
character preparation failures in all three early/late insertion cycles.
Producer Build ID:
`89d7029adfdd8677df543e1fcae7c1cced3cd7d49eb2b49417b134cfb6ae21af`.
The existing process was SkyrimVR.exe PID 8296, DevBench port 8921,
in Dragonsreach with the CSXTest01 scene, FOV enabled, character faces,
skin and hair selected, and experimental multi-ROI off. Direct DevBench
MCP was the sole transport. No AIO validation or deployment ran.

The character off/on control produced 54 NR receipts with no increase
from the existing one preparation failure. Three insertion cycles
produced 162 NR receipts and increased that counter from 1 to 10.
Each cycle selected Upscaled Center, waited 1000 ms, then selected
Final LDR and sampled status 48 times with 10 ms pacing.

| Cycle | Late configure frame | Failure frame | Cumulative failures | Recovery                                      |
| ----- | -------------------: | ------------: | ------------------: | --------------------------------------------- |
| 1     |               356425 |        356426 |                   4 | Subsequent neural pair                        |
| 2     |               356483 |        356484 |                   7 | Subsequent neural pair                        |
| 3     |               356541 |        356542 |                  10 | Masks ready at 356543; subsequent neural pair |

All three preserved the exact error:
`character category selection expanded beyond the captured source policy`.
The outer route reported `normal_dlss_pair` / `stereo_preflight_failed`.
The preceding configure receipts exposed earlier cumulative increases
and `neural_evaluation_failed`; those failures are retained too.
Final observation at frame 368920 showed a recovered `neural_pair`,
10 character preparation failures and zero renderer evaluation failures.
Successful tool execution and eventual recovery do not make the
transitions pass.

Local evidence:
`build/validation/nr-live-20260915/transition-repro-20260915T062834`.
Numbered requests/responses contain the complete two scenario transcripts.
`activity.json` preserves 1553 tracking samples and 1482 pose samples
over 209570 ms. Capture stopped normally, below the running DLL's limit.
No physical-headset or performance qualification was attempted.

## Source correction

`RequestHistoryReset` invalidated both character history and the immutable
category/depth source captured earlier in the same frame. That erased its
enabled-category policy, causing the later preparation to reject a
selection which had not expanded. History reset now retains only a source
capture whose frame exactly matches the current frame. Derived masks and
region history still invalidate. Explicit resource invalidation retains
its full invalidation behavior; frame, device, dimensions and category
subset checks remain mandatory during preparation.

Character status now retains `lastPreparationFailure` after recovery,
including source/capture frames, generation, slot, category masks and
reason. This distinguishes a missing source from a real policy expansion.

The separate duplicate-constants correction permits main VR foveated
calls to reuse only successfully submitted constants with identical
frame/token/viewport/crop/tuning identity and every exact version-two
constant value. It does not reinterpret a vendor duplicate error as
success or reuse changed constants. Exposure observation moves to the
actual D3D draw boundary with explicit pixel-shader identity checking.

These source changes require a rebuilt DLL and repetition of the saved
live reproducer. The reproduction above establishes the original defect;
it does not validate the changed DLL. The earlier colour-treatment,
multi-ROI completeness, exposure and headset limitations remain explicit.

## Offline validation before plugin build

-   `python tools/nr-color/verify_assets.py`: passed through the standalone
    CTest asset-inventory check.
-   `cmake -S tests/neural_color -B build/nr-color-tests`, Release build,
    and `ctest -C Release --output-on-failure`: 16/16 passed, 19.84 seconds.
    This includes exact Streamline constant comparisons, exposure binding
    policy, source wiring, colour/measurement policies and WARP shader tests.
-   Companion DevBench: `xmake build devbench-tests`, then
    `build/windows/x64/releasedbg/devbench-tests.exe`: 79 cases, zero failed.
    Fake-clock regressions cover crossing 30 minutes, delayed finalization,
    manual/capacity stop, restart and unchanged replay limits.

The companion recorder now defaults to a bounded four-hour observation
window, exposes remaining time/capacity, and freezes captured duration when
stopped. Its 60,000-sample retention and 30-minute replay limits remain.
This is not unlimited recording or automatic segmentation. Toolkit feedback
is recorded as `AUTO-20260915-064247421-2BF9B024`.
