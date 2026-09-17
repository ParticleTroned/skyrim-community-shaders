# NR exposure observation repair, 2026-09-15

## Reproduction and confirmed cause

The live retest of `78571eb853568df74e91e5967e0608abaadb0e36` used
Build ID `332b3f84e7bef9872e063296d14380a5dfe9ce5d4a330f37cfa330488c1c7729`.
The installed AIO and physical DLL matched that producer. Two characters in
Dragonsreach yielded 63 valid four-region identity-transport batches and
60 valid four-region manual-exposure NR batches. Both eyes reported 38,176
successful ROI readbacks and zero readback fallbacks. Successful submission
captures do not establish physical HMD presentation or correct model domain.

The 2x2 unit-ratio proof passed, but automatic exposure correction failed:
capture count remained 183 with source frame 77713 while producer callbacks
advanced through frame 78609. Strict current-frame and explicit previous-frame
correction both rejected stale data. Reloading the save did not fix cadence.
Immediate insertion/category mutation receipts also retained fallbacks;
500ms probes recovered, without establishing their exact display duration.

Read-only process inspection with the matching PDB confirmed a dispatch
coverage defect. The same immediate context's method table contained
different D3D11 functions from those detoured at startup. For example:

| Method                       | Patched startup target, d3d11.dll RVA | Observed active target RVA            |
| ---------------------------- | ------------------------------------- | ------------------------------------- |
| DrawIndexed                  | `0x1545a0`                            | `0x155d30` (also observed `0x154260`) |
| DrawIndexedInstanced         | `0x1541b0`                            | `0x156640`                            |
| DrawIndexedInstancedIndirect | `0x1902b0`                            | `0x1423c0`                            |

The original entries still contained Detours jumps. The active entries
contained unpatched D3D11 prologues. Their context and method-table addresses
were unchanged. Consequently, observing every draw signature at startup did
not provide continuous coverage of the runtime's replaceable draw methods.
Inspection did not pause, inject into, or modify the running process.

Complete retest evidence is retained at
`build/validation/nr-live-20260915/retest-78571eb85-20260915T174257Z`.
Root-cause evidence is in
`build/validation/nr-live-20260915/draw-hook-fix/live-draw-hook-inspection.json`.

## Correction and invariants

The existing `BSGraphics_SetDirtyStates` engine hook now observes exposure
after the engine and CS finish applying graphics bindings, before the draw.
Both normal and diagnostic hook paths use this boundary. Compute flushes
are excluded, and the exact thread-local HDR effect scope remains mandatory.
This shared SE/AE/VR hook does not depend on the D3D11 draw method address.
Existing D3D11 observations remain supplementary evidence; no driver methods
are patched dynamically and no rendering defaults change.

Both boundaries use the same live shader/AvgTex read and existing validation:
immediate context/device identity, exact producer shader selection, supported
view/format/mip/sampler, and bounded GPU scalar proof. One frame keeps its
first immutable snapshot. Conflicting sources remain ambiguous. Current-frame
and explicit previous-frame matching, epoch rejection and stereo/ROI latching
are unchanged. The compute-state guard restores bindings after extraction.

DevBench reports `graphicsStateFlushes` and `lastGraphicsStateFlushFrame`
alongside producer and draw counters. Each snapshot identifies the boundary
that actually produced it. These fields distinguish a missing engine callback
from an input rejection or delayed GPU readback. Configuration acceptance
alone remains insufficient evidence of correction.

## Validation

The source contract covers both engine hook branches, graphics-only
admission and shared binding extraction. The exposure WARP regression adds
64 consecutive frames with alternating bound AvgTex resources and changing
values. It exercises the production binding reader and compute-state guard,
checks every frozen snapshot texel after later source writes, and verifies
that pixel and compute bindings are preserved. Existing shader, policy,
epoch, previous-frame and stereo-latch regressions remain required.

Build/test output and the AIO receipt belong under the local `draw-hook-fix`
evidence directory. The new DLL must still demonstrate continuous capture
and valid correction batches in game. The currently loaded older DLL cannot
validate this implementation. No 30-minute recording or AIO validation
campaign is part of this correction.
