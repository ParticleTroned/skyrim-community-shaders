# NR colour null-driver retest, 15 September 2026

## Identity and scope

Tested source: `655fa51fe607deb1017bbb4aa1b0c467504d3692`.
Producer Build ID:
`ce88898ce9dc5c42526e7c64646f557218156a9174a6c4917fe33917c9c137ad`.
DLL SHA-256:
`dec5947ce7504f389cffbd0df5efb07089722b25e7127670c6ddd0124ee34f77`.
DLL size: 24,620,032 bytes. The enabled AIO's physical DLL, sidecar and six
colour assets matched the build receipt, with no competing loose providers
in enabled mods, Overwrite or unmanaged Data.

Skyrim VR PID 21184 ran WhiterunDragonsreach with Valve's null HMD. OpenVR
reported a valid standing pose at 1.73 m, 63 mm eye separation and 1512x1680
per eye. The user explicitly authorized feasible null-driver tests despite
the absent managed pose provider. Controls used the discovered direct
DevBench MCP transport. Native camera movement was used; no managed head or
controller replay was claimed. These results do not establish headset
presentation, colour correctness or representative headset performance.

Raw evidence remains local at:
`C:/src/skyrim-community-shaders/build/validation/nr-live-20260915/retest-655fa51-20260915T003834Z`.
Numbered request/response receipts precede and follow every live call.

## Executed checks

| Area               | Observed result                                                                                                                                                                                                                                                                                                   |
| ------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Main NR controls   | Exercised enable/disable, presets, styles, intensity endpoints, tone/structure sliders, all four implementations, individual batch/direct/optimized controls and subrect scales. Raw inference recovered after both shader failures.                                                                              |
| Character controls | Exercised selection policies, strength, distance, pixel size, margin, hold, feather, depth threshold, adaptive/depth/visibility flags, mask modes, four debug views and experimental multi-ROI. Real character inference and a secondary slot were observed. Empty selection bypassed without claiming inference. |
| Control assertions | 100 recorded batch checks: 96 initially matched, three combined preset/style expectations were wrong because custom preset zero is intended, and final-LDR readiness failed. Separate preset-only checks passed. Two initial intensity checks are outside that count.                                             |
| Colour registry    | 35 valid configuration/readback cases passed with NR disabled. Stale revision 39 was rejected at current revision 40 without changing configuration. Display-only A/B preserved input epochs. These are registry checks, not processed-colour results.                                                            |
| Captures           | Four completed stereo requests produced twelve PNGs: NR on, NR off, final-LDR fallback and character multi-ROI. Each request has same-cycle left/right images and a 3024x1680 side-by-side image, generation 3, DXGI format 28. This does not prove managed-colour output or physical display.                    |
| Menus and movement | Inventory opened/closed. Owned native free-camera steps completed and ownership was released. Full-frame motion advanced commits from 76,294 to 76,378 without a new failure; an earlier character view with an empty mask was not counted as inference.                                                          |
| Recording          | Cold status threw before start. Start, running status, stop and subsequent idle status worked. Preserved 1,175,852 ms, 9,612 poses, 10,100 tracking samples and 117 activity events; no recording limit reached.                                                                                                  |
| Final health       | 81,388 feature evaluations and 81,388 output commits; two resource-creation failures, zero device removals, zero quarantines, zero failed resets, 28 successful resets. Whole-history stereo failures (8,661) and latched bypasses (17,318) remain failures; final recovery does not erase them.                  |
| Shutdown           | Owned recording and screenshot work were inactive, camera ownership released, Skyrim closed normally, MO2 closed cooperatively through its controller, and the shutdown session/access lease were released. No force termination or settings restoration.                                                         |

## Defects and corrections prepared after shutdown

1. Three colour compute shaders included `ColorCommon.hlsli` relative to
   themselves. The runtime include handler resolves paths from `Data/Shaders`.
   Use the package-root path, make asset validation follow the same rule,
   and share that include handler contract with colour and character WARP
   tests. The earlier sibling-search compiler test missed this defect.
2. Exposure capture rejected every observed AvgTex binding at
   RestoreTechnique. Move observation to flushed graphics bindings before
   the identified HDR draw and expose rejected resource descriptors. Keep
   the scalar 1x1 and frame/epoch admission rules. Actual exposure capture
   and timing remain unverified until the next run.
3. Final-LDR main routing required a framebuffer UAV that the tested game
   target did not expose. Stage the exact framebuffer in a named private
   UAV texture, commit only a complete pair, and refresh that copy after UI
   before HMD-mask repair. Share reset/abandon ownership with existing
   foveated resources. A WARP regression executes this copy/commit path
   against a non-UAV render target and checks failed-eye preservation,
   fresh post-UI contents and render-target restoration.
4. DevBench cold status called JSON `value()` on a null manifest. The
   companion DevBench change handles the pre-start absence while retaining
   correlation identity and rejecting malformed non-string identities.

Colour/exposure resources also receive the standard D3D resource names.
Capture-only requests no longer enable raw-path colour reconstruction.

## Remaining qualification

The revised DLL has not run in game. Transport identity, managed and
preserve-source inference, manual and captured exposure candidates, complete
multi-ROI stereo evidence, final-LDR presentation and flat-rendering paths
must be repeated with its new Build ID. No treatment was selected as the
correct NVIDIA NR input domain. No production colour defaults changed.

Two NR-disable transitions produced `eErrorDuplicatedConstants` for
FoveatedCenter viewport 4352 at frames 41,489 and 99,014, followed by normal
full-frame DLSS fallback. They are retained as unresolved transition
diagnostics. The existing duplicate-constant reuse policy is restricted to
submit routes; broadening it without a controlled constants comparison
would change normal DLSS behavior. Recheck these transitions next run.

The runtime exposed profiler sampling but no standalone performance-neutral
probe. No controlled profiler campaign or performance conclusion is
reported. Full headset motion, optics and presentation remain outside this
null-driver run. The new framebuffer copies add work; their cost needs a
matched in-game measurement.

## Offline validation

Before the plugin build, all 14 NR tests passed, including
`NRFramebufferWARP`, `NRColorShadersWARP` and `NRColorExposureWARP`.
The DevBench host suite passed 76 cases, including the cold-manifest test.
Build/archive hashes and the final validation logs belong to the next AIO's
local receipt, rather than being inferred from this older live session.
