# SkyrimVR Full-Resolution Menu Presentation Strategy

Status: the ordinary-menu hybrid is implemented and under focused runtime
validation. RC88 proved reliable capture/submission but exposed three
presentation defects: Map's substituted target is cleared opaque, the desktop
path copied a masked HMD eye instead of preserving Skyrim's game-window view,
and the broad production monitor inspected unrelated draws at Info level. The
current working revision addresses those defects without toggling Render Scale
or recreating scene targets.

Branch: `cs-1.7-PL-VR`

Implementation base reviewed: `0c66c97b895c2ee2b939073ab6cf73c87eb6ea7f`

Current committed implementation: `87d6c7dc0` (`RC88`)

Current working implementation: uncommitted RC88 follow-up; no build or commit
has been made under the branch rules.

Implementation validation at that revision: `git diff --cached --check` and a
static ownership/resource-lifetime review. Build and runtime tests were not run
under the branch rule; the focused acceptance matrix below remains required.

Trace baseline: `87522b1f47ef5544b92962cdd11177db8e746ea8` (`RC83`)

Open Shaders reference reviewed: development commit
[`6070998cc5f290b929d072a76460d7965c053ea2`](https://github.com/alandtse/open-shaders/commit/6070998cc5f290b929d072a76460d7965c053ea2),
particularly `src/Features/Upscaling/PerfMode.h`. This is an architectural
comparison only; no Open Shaders source is copied by this document.

Evidence reviewed:

-   RC77 multi-user menu traces;
-   RC79 ordinary-menu and RaceSex traces;
-   RC81 semantic epoch, producer-generation, render-state, MainMenu, loading,
    and ordinary-menu trace.
-   RC81 Nvidia/OpenComposite trace containing ordinary menus and three Map
    sessions, including a 1,908-frame uninterrupted Map session.
-   RC81 RaceSex trace containing 2,013 in-menu frames through a race-change
    crash, together with the matching Crash Logger report.
-   RC82 self-calibrating generic draw accounting and multi-context hook
    coverage. Its runtime behavior is confirmed by the focused RC83 Map and
    RaceSex logs.
-   Focused RC83 Nvidia/OpenComposite Map trace containing eight substantive
    Map-containing sessions, 230 completed `DrawInterface` frames, detailed draw
    and resource-operation records, accumulator parity, and successful OpenVR
    submits. No trace fault or retained-record-limit truncation occurred.
-   Focused RC83 New Game RaceSex trace containing 309 completed
    `DrawInterface` frames, authoritative mode-24 parity, continuous producer
    generations, MessageBox overlap, clean close/tail/re-latch behavior, and 618
    successful original-path OpenVR submits.
-   Post-latch RaceSex trace opening at frame 62,793 after Render Scale had
    latched, spanning Console and MessageBox overlap, 666 completed RaceSex
    frames, and 1,332 successful but reduced-resolution original-path OpenVR
    submits.
-   Independent RC83 OpenComposite/AMD/FSR trace containing 28 completed trace
    sessions, 4,895 completed `DrawInterface` calls, 9,792 successful OpenVR
    submits, post-latch RaceSex, six substantive Map sessions, three post-latch
    Loading sessions, and ordinary MessageBox, Dialogue, Tween, and Journal
    coverage. It has no trace fault, record-cap saturation, dropped resource, or
    failed submit; its final Journal session is active at log end.
-   Paired RC83 SteamVR trace from the same machine and menu sequence, containing
    28 completed sessions, 4,833 matched `DrawInterface` calls, 4,518 successful
    traced submits after submit-hook installation, six substantive Map sessions,
    post-latch RaceSex and Loading, and the same ordinary operation contracts. It
    also has no trace fault, record-cap saturation, dropped resource, or failed
    submit; its final Journal session is active at log end.
-   RC88 follow-up trace ending 2026-07-21 09:16:33: Journal captured 59 exact
    bridge operations while the broad higher/direct hook observed 212,040 draws;
    Map completed 14 final eye composites and submits but cleared the substituted
    `4936x2740` target first to transparent and then to `[0,0,0,1]`; Console
    captured 68 exact operations and completed 46 final composites/submits. These
    are successful transport records with incorrect Map alpha, desktop base, and
    production-cost policies, not missing capture or OpenVR failures.

This document is a design and verification record. It is not a release claim.

## Decision

Keep CS's existing VR architecture:

-   reduced-resolution scene rendering;
-   submit-stage presentation upscaling;
-   existing per-eye output and OpenVR submission;
-   existing OCU, loading, save/load, device, and runtime-plan guards;
-   final full-resolution menu composition.

The earlier native-resolution relatch proposal is superseded. It was rejected
because transient menu entry would make target recreation and vendor-resource
teardown part of normal interaction. The implemented design leaves the reduced
scene contract latched and routes menu pixels around the reduced presentation
loss.

### Final routing

| Context                    | Presentation policy                                                                                                                                        | Why                                                                                                                                                                                                         |
| -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Ordinary menus and Console | Replay every exact mode-24 bridge operation into full-resolution staging and suppress that operation only after replay succeeds                            | The complete ordered bridge contract is proven and must not use selector, index-count, or deduplication heuristics                                                                                          |
| RaceSex                    | Same semantic mode-24 transport as ordinary menus                                                                                                          | RC83 proves the same three-operation projected/HUD contract, including persistent HUD reuse; no relatch is required                                                                                         |
| MainMenu and Loading       | Replay the exact direct projected-menu bridge outside an accumulator; stretch the reduced base at submit and composite the committed full-resolution layer | These menus have the exact higher/direct bridge but no ordinary mode-24 consumer                                                                                                                            |
| Map                        | Bypass hybrid capture/substitution and keep Skyrim's complete reduced-resolution mixed Map/UI path                                                         | RC88 proves Skyrim overwrites the substituted target's transparent alpha with opaque black, so compositing it hides terrain; visible/correct reduced Map is the safe policy until alpha ownership is solved |
| Desktop game window        | Preserve Skyrim's existing unmasked game-window backbuffer and alpha-composite the left half of the committed stereo menu layer at Present                 | Copying a completed HMD eye also copied the hidden-area mask and replaced the preferred desktop camera; only the suppressed menu pixels need restoration                                                    |

For ordinary menus and RaceSex, semantic ownership is:

1. A verified `BSShaderAccumulator::RenderBatches` consumer epoch authorizes
   capture.
2. The exact higher engine bridge call establishes a bridge operation.
3. The existing direct draw interception transports every operation, in order,
   into a full-resolution staging layer.
4. Epochs contribute to one frame transaction. The layer is committed only when
   that transaction is sealed before the first eye submit, then reused until
   superseded or invalidated.

Remove these from ordinary-menu correctness:

-   selector inspection, including the `subject + 0x190 == 0x80` test;
-   known or adaptively learned index counts;
-   PS resource scanning as ownership discovery;
-   resource-based operation deduplication;
-   menu-name whitelists;
-   frame-based clearing of the committed layer.

RC81 requires one correction to the earlier generation design: an observed,
complete producer generation must not authorize or gate capture. A persistent
HUD source can be valid even when its generation predates the currently armed
trace or menu session. The semantic consumer, exact bridge nesting, and bound
source/destination contract authorize capture. Producer generations remain
useful for diagnostics, reuse, and invalidation.

## Final Re-analysis And Hybrid Boundary

The hybrid remains the best fit for CS. The accumulated evidence fixes these
implementation rules:

1. **Frame ownership, not epoch publication.** The latest RC83 Console and
   Journal samples each contain exactly one mode-24 epoch per observed frame
   (212 and 105 frames respectively), but correctness must not depend on that
   count. All authorized epochs in one video frame append to one ordered staging
   transaction, which is sealed at presentation.
2. **Real atomic failure containment.** Resource preflight cannot guarantee that
   every later intercepted operation will succeed. Once an exact bridge
   operation is accepted into a required transaction, any capture failure
   poisons the frame; that frame cannot fall through to an original reduced
   OpenVR submit or a partial desktop mirror.
3. **Separate production ownership from tracing.** Semantic accumulator state now
   runs whenever the adapter is enabled. Developer tracing is only an observer,
   and even a trace overflow-allocation failure cannot skip the production end
   scope.
4. **Persistent Render Scale.** Menu context no longer blocks an already latched
   Render Scale contract. Startup may still defer the first latch while loading,
   and real save/load or vendor-reset transitions retain their existing safety
   guards, but menu presentation itself never requests a relatch.
5. **Map currently remains entirely engine-owned.** The mixed depth-writing
   epoch still disqualifies selective bridge replay, and RC88 now proves the
   attempted full-resolution substitute has no usable transparent-background
   contract: Skyrim clears it to alpha one. Map therefore bypasses the hybrid
   and remains visible through the original reduced `kMENUBG` path.
6. **Stretch is limited to transport.** Stretching the reduced scene/base cannot
   recover text detail. It is valid for MainMenu/Loading base presentation and
   only where projected/HUD UI is rendered or replayed at full resolution
   afterward. It is not used as a claim that Map is currently full-resolution.

Open Shaders obtains robust menu rendering by changing the broader rendering
contract: it enlarges selected menu/intermediate targets, supplies a display-
size depth target for the UI pass, adjusts viewport/copy behavior, and lets the
engine draw the UI at output resolution. That avoids selective draw discovery
and naturally includes unusual or modded UI, but it owns substantially more of
the render-target, depth, post-process, and memory lifecycle.

CS retains reduced scene rendering and submit-stage upscaling. The semantic
adapter avoids broad scene-target resizing and limits extra full-resolution
storage to staging and committed layers. The failed Map depth/target adapter and
desktop eye-pair banks are not part of the active presentation policy. No Open
Shaders source is copied.

## RC83 OpenComposite And SteamVR Cross-Validation

The paired runs use the same AMD machine, modlist, menu sequence, and CS FSR
Render Scale configuration:

-   OpenComposite: 1.92 GB, `3858x1929` stereo engine rendering to a `5016x2508`
    final stereo target (`2508x2508` per submitted eye);
-   SteamVR: 2.18 GB, `3852x1926` stereo engine rendering to a `5008x2504` final
    stereo target (`2504x2504` per submitted eye).

The small size difference follows each runtime's recommended eye target. CS
Render Scale remains active in the OpenComposite run, so it validates OCU when
OCU upscaling is not detected as active; it does not validate the separate
OCU-upscaling blocker.

OpenComposite log integrity is sufficient:

-   28 completed sessions covering 4,923 session frames;
-   4,896 `DrawInterface` begins and 4,895 ends, with the one-count difference at
    a session rearm boundary rather than an unmatched completed session;
-   9,792 successful submits and no failed submit in completed sessions;
-   no trace fault, accumulator mismatch, record-cap saturation, context overflow,
    hook coverage failure, or dropped tracked resource;
-   an additional active Journal session with 202 successful submits before the
    log ends.

SteamVR independently reports:

-   28 completed sessions covering 4,861 session frames;
-   4,833 matched `DrawInterface` begin/end calls;
-   4,518 successful traced submits and no failed submit after the compositor
    submit hook is installed, plus 140 submits in the active final Journal session;
-   no trace fault, accumulator mismatch, record-cap saturation, context overflow,
    hook coverage failure, or dropped tracked resource.

SteamVR startup MainMenu/loading sessions predate submit-hook installation, so
their OpenVR submit counts are absent. Their bridge destinations are nevertheless
the full native `5008x2504` `kVR_FRAMEBUFFER`, while every later post-latch
Loading bridge targets the reduced `3852x1926` framebuffer.

The ordinary contract is unchanged across this runtime and hardware:

-   MessageBox, Dialogue, and pre-Map Tween frames use one mode-24 epoch with
    projected `6x2`, HUD `504x2`, and HUD `252x2` operations after calibration;
-   `252x2` replaces the `144x2` HUD variant seen in another run, independently
    confirming that index count is payload rather than ownership;
-   the current target-class early termination again captures the projected and
    `504x2` operations, then keeps most `252x2` operations with
    `all-menu-targets-already-suppressed`;
-   Journal uses one projected operation per stable frame, confirming that the
    semantic transaction must accept dynamic operation counts;
-   all 3,365 OCU and all 2,823 SteamVR scaled bridge decisions use the previously observed blend,
    rasterizer, and depth state: color `SRC_ALPHA`/`INV_SRC_ALPHA`, solid back-face
    culling, depth `LESS_EQUAL`, depth writes off, and stencil off.

The exceptional decisions are also independently confirmed:

-   the OCU Map sessions contain 121 completed frames and the SteamVR sessions
    contain 133; both runtimes execute exactly two mode-24 epochs per Map frame;
-   in every one of the combined 254 Map frames, one epoch contains a menu bridge
    followed by two `kMENUBG` draws with depth writes enabled. Across the 508 Map
    epochs, 254 have this post-bridge dependency and none has the depth-writing
    work before the bridge;
-   MessageBox overlap can cause current capture behavior to change while Map is
    still active, proving that any active exceptional reason must block the whole
    ordinary adapter rather than only selected menu names or draws;
-   post-latch RaceSex spans 255 completed OCU and 181 completed SteamVR
    `DrawInterface` calls. Both use the same three-operation contract. The blocker
    reports protection active, but all 510 OCU and 362 SteamVR original submits
    remain at their respective reduced stereo dimensions;
-   the three post-latch Loading sessions contain 1,149 completed OCU and 1,121
    completed SteamVR frames. All 2,298 OCU and 2,242 SteamVR original submits use
    the corresponding reduced framebuffer. Presentation and Render Scale behavior
    are logically inactive, but no native resource transition occurs.

The last point proves that logically disabling presentation upscaling does not
restore native resources after the Render Scale latch. The implemented design
therefore keeps that latch and gives post-latch Loading and MainMenu an explicit
reduced-base stretch plus full-resolution direct menu-layer transport; it does
not infer post-latch safety from the startup case.

## RC81 Ordinary Log Integrity And Coverage

The supplied RC81 log is approximately 1.2 GB and reaches trace sequence
2,193,004 across 31 sessions.

-   The first MainMenu session reached the 300,000 record counter.
-   Required records continued after the counter saturated.
-   No trace fault was reported.
-   No accumulator stack overflow was reported.
-   No accumulator end-argument mismatch was reported.
-   No tracked resource was dropped.
-   The final Journal session was still active when logging ended, so it has no
    closing session summary; its retained records are present.

Covered contexts include:

-   Main Menu;
-   Loading Menu;
-   Tween;
-   Inventory;
-   Magic;
-   Stats/Skills;
-   Dialogue;
-   Container;
-   Crafting;
-   Journal;
-   Sleep/Wait;
-   Lockpicking.

This first RC81 log contains no Map session and no RaceSex event. It therefore
cannot resolve those exceptional paths by itself.

The sampled active runtime plan was:

-   reduced stereo render and `kMENUBG`: `4024x2125`;
-   final stereo output: `4736x2500`;
-   submitted eye: `2368x2500`;
-   DLSS quality mode: 1;
-   Render Scale and presentation upscaling active for ordinary menus.

## RC81 Nvidia/OCU Map Log Integrity And Coverage

The second RC81 log is approximately 1.07 GB and contains 52 trace sessions.
It was captured with the OpenComposite runtime and includes three Map-related
sessions: Map alone, MessageBox over Map, and a final uninterrupted Map session.
It contains no RaceSex menu-open event or RaceSex trace session; RaceSex will
be evaluated from a separate log.

The final Map session:

-   lasted 1,908 frames, with 1,907 matched `DrawInterface` begin/end pairs;
-   reached the 300,000 retained-record counter;
-   continued required records after saturation and flushed them per record;
-   reported no trace fault, accumulator overflow, or dropped resource;
-   closed normally and emitted a complete session summary.

The active Map presentation plan was:

-   reduced stereo render and `kMENUBG`: `2964x1425`;
-   final stereo layer: `5040x2424`;
-   submitted eye: `2520x2424`;
-   Render Scale and presentation upscaling active for every recorded Map bridge
    operation;
-   both OpenVR eye submissions succeeded throughout the final session.

This is an RC81 trace. Its headers do not contain RC83's Map per-draw stream,
resource-operation stream, deferred-context/command-list correlation,
persistent cross-session generation history, or menu-layer lifecycle records.
The required summary and bridge records are complete, but those later fields
cannot be reconstructed from this file.

## Ordinary Consumer Contract

RC81 recorded 22,214 active ordinary bridge operations:

-   8,455 `kPROJECTEDMENU` operations with draw arguments `6x2`;
-   6,721 `kHUDMENU` operations with draw arguments `168x2`;
-   6,721 `kHUDMENU` operations with draw arguments `144x2`;
-   317 `kHUDMENU` operations with draw arguments `78x2`.

Every observed active ordinary operation:

-   occurred inside `renderMode == 24`;
-   occurred inside accumulator group 16;
-   used `firstPass=1`, `lastPass=1543528565`, and `renderFlags=0`;
-   was nested under the exact higher call and direct draw callsite;
-   targeted `kMENUBG`;
-   had `operation(active=true)`;
-   used selector value zero;
-   used one RTV, no secondary RTV, and no UAV;
-   ran while Render Scale and presentation upscaling were active.

The complete argument tuple is strong diagnostic corroboration. Production
ownership should still come from the semantic render mode and exact engine
call nesting rather than hard-coding opaque pass values that may vary with an
engine build.

Operation count is not fixed. Depending on menu and transition state, an epoch
can contain projected content, HUD content, or both. Every nested direct bridge
operation must be transported in original order. The `168`, `144`, and `78`
HUD operations are distinct even when they use the same source and destination.

### Why discovery is conclusively invalid

All 22,214 valid ordinary operations used selector zero. A selector requirement
of `0x80` rejects the correct path.

The valid draw shapes include 6, 78, 144, and 168 indices per instance. Draw
shape is content, not ownership. A mod or menu transition may introduce more
valid shapes.

The two common HUD operations use the same source and destination but different
geometry. Resource deduplication can discard one and produce missing, stale, or
temporarily mixed text. This remains a credible explanation for the reported
`pltreeyer`-style presentation without requiring damage to the source texture.

## Console Readability Evidence

The user reports that the Console is visible in the HMD but absent from the
desktop mirror, and that its HMD text is poorly readable. The trace confirms
that the Console layer reaches the per-eye final-composite and OpenVR-submit
path, but it does not record the desktop mirror. Console validation must
therefore be performed in the HMD; the monitor image is not an output-equivalence
oracle for this menu.

The controlled post-latch RC83 log contains a 213-frame Console session while
Render Scale and presentation upscaling are active. The Console is an ordinary
mode-24 consumer, not an exceptional fallback:

-   after the first calibration epoch, 211 authoritative group-16 epochs each
    contain exactly three ordered bridge operations;
-   the order is projected `6x2`, HUD `504x2`, then HUD `144x2`;
-   all three sample complete `2048x2048` projected/HUD source generations and
    target the reduced `3290x1826` `kMENUBG`;
-   all 212 projected producer passes and all 212 HUD producer passes use
    color-clear-then-redraw; projected passes contain 7-10 draws and HUD passes
    contain 7-12 draws;
-   producer depth testing is disabled. HUD production uses stencil, while the
    projected producer does not.

The current capture path does not preserve that transaction. Across the
session it accepts and suppresses 424 operations, but rejects and keeps 211.
In every stable epoch it stages projected `6x2` and HUD `504x2`, then leaves HUD
`144x2` in reduced `kMENUBG` with reason
`all-menu-targets-already-suppressed`. The final layer therefore contains two
operations, even though the authoritative epoch contains three. On the final
Console frame the two staged operations replay into `4936x2740`, while the
third operation remains rasterized at `3290x1826`.

This proves a mixed-resolution and order-changing composition error. The
reduced HUD `144x2` result is upscaled with the scene, then the earlier
projected `6x2` and HUD `504x2` operations are composited over it at final
resolution. It is not equivalent to the engine's original three-operation
order and is a direct correctness defect consistent with the reported poor HMD
Console readability. The trace does not identify which Console glyphs or
panels belong to the `144x2` operation, so that narrower visual attribution
must not be asserted.

The fix is part of the generic ordinary-menu pipeline: capture all three
operations as opaque ordered payload, suppress each reduced original only after
its capture succeeds, fail closed if the later transaction becomes poisoned,
and publish only the complete layer. No Console-specific index-count rule is
appropriate. The
`2048x2048` source resolution remains an upper bound on recoverable detail, so
post-fix visual validation must distinguish any residual source limitation from
the now-proven bridge omission.

Suppressing the reduced originals also makes desktop-mirror presentation an
explicit part of correctness. The RC83 Console source is `3290x1826`, while the
two final eye outputs are each `2468x2740`. It cannot satisfy the current direct
writeback requirement of a `4936x2740` combined target. The alternate shader
blit runs only when `StabilizeRenderScaleDesktopMirror` is enabled, and that
setting defaults to false. The trace does not record its value or prove that a
writeback to the OpenVR source texture is consumed by the game window. The
reported HMD-only Console is therefore consistent with the current design, but
the available evidence cannot distinguish a disabled fallback from a
writeback-order or desktop-target mismatch.

## Bridge Render State

RC81 recorded one stable ordinary bridge state across all four operation
shapes:

-   source and destination format: DXGI format 28 (`R8G8B8A8_UNORM`);
-   sample count: 1;
-   topology: triangle list;
-   one full-destination viewport;
-   scissor disabled;
-   back-face culling with counter-clockwise front faces;
-   no UAV and no secondary RTV;
-   depth enabled with `LESS_EQUAL`;
-   depth writes disabled;
-   stencil disabled;
-   color blend: `SRC_ALPHA`, `INV_SRC_ALPHA`, `ADD`;
-   engine alpha blend: `ZERO`, `INV_SRC_ALPHA`, `ADD`;
-   full RGBA write mask.

The raw trace value for source alpha is 1, which is
`D3D11_BLEND_ZERO`; `D3D11_BLEND_ONE` is 2. The engine state is suitable when
blending into its existing opaque target, but would leave a transparent staging
texture with zero accumulated alpha. Full-resolution layer capture must keep
the observed color equation while deliberately replacing only the source-alpha
factor with `ONE`. The existing `vrMenuLayerCaptureBlendState` already makes
that staging-specific adaptation.

The source dimensions differed:

-   `kPROJECTEDMENU`: `1024x1024`;
-   `kHUDMENU`: `2048x2048`.

Earlier logs observed a `2048x2048` projected source. Resource dimensions must
therefore always be queried from the bound resource. Neither 1024 nor 2048 may
be a correctness constant.

The sources are not necessarily HMD-native resolution. They are nevertheless
consumed before rasterization into reduced `kMENUBG`. Replaying the bridge into
the final `4736x2500` layer avoids the additional reduced `4024x2125` menu
rasterization and scene-upscale loss; it cannot create detail absent from the
source.

## Producer And Consumer Generations

RC81 disproves the earlier assumption that all menu sources are cleared and
fully redrawn on every producer pass.

Observed `kPROJECTEDMENU` passes:

-   8,381 full transparent clears with no draw;
-   87 clear-then-redraw passes without stencil;
-   5 clear-then-redraw passes with stencil;
-   no copy, resolve, or resource-update path.

Observed `kHUDMENU` passes:

-   5 full-viewport redraws without an observed reset and without stencil;
-   2 full-viewport redraws without an observed reset and with stencil;
-   1 clear-only pass;
-   no copy, resolve, or resource-update path.

Producer depth testing was disabled for observed source draws. Projected and
HUD source generation may use stencil.

The ordinary ordering is commonly:

1. A mode-24 bridge epoch consumes the source generation completed previously.
2. CS performs its final per-eye composition and OpenVR submit.
3. A later `DrawInterface` pass clears or updates the source for the next
   consumer frame.

The trace demonstrated this directly for HUD: a full-viewport HUD draw at the
end of one frame became the complete generation consumed by both HUD bridge
operations in the following frame.

Many HUD consumers report `generation=unobserved`. This is expected when a
persistent HUD resource predates a newly armed trace/menu session. It does not
mean the resource is invalid.

Implementation consequences:

-   track producer resources continuously rather than resetting their history
    when a diagnostic/menu session rearms;
-   do not require a same-frame producer;
-   do not require an observed clear;
-   do not reject an otherwise valid consumer because the generation is
    unobserved;
-   clear staging lazily at the first authorized operation of a new frame
    transaction, then replay every consumer operation sampling the current
    persistent sources;
-   retain the last committed layer when no new eligible consumer transaction
    occurs.

## MainMenu And Loading

RC81 confirms that MainMenu and loading are not ordinary mode-24 consumers.

### MainMenu

The MainMenu session reports 4,315 bridge decisions. Its observed presentation
contract is:

-   source: `kPROJECTEDMENU`, `1024x1024`;
-   destination: full-resolution `kVR_FRAMEBUFFER`, `4736x2500`;
-   draw arguments: `6x2`;
-   no recognized accumulator epoch at the direct presentation call;
-   Render Scale inactive;
-   presentation upscaling inactive.

### Loading

The loading session reports 977 bridge decisions and the same presentation
shape:

-   source: `kPROJECTEDMENU`, `1024x1024`;
-   destination: full-resolution `kVR_FRAMEBUFFER`, `4736x2500`;
-   draw arguments: `6x2`;
-   no mode-24 consumer epoch;
-   Render Scale inactive;
-   presentation upscaling inactive.

Those RC81 sessions occur while the engine already owns native resources. The
paired OpenComposite and SteamVR runs separate startup from post-latch behavior:

-   startup MainMenu and Loading target each runtime's full native stereo
    framebuffer while the Render Scale latch is absent;
-   after the latch establishes a reduced engine plan, three later Loading
    sessions per runtime keep presentation upscaling logically inactive but submit
    that reduced framebuffer directly for all 2,298 OCU and 2,242 SteamVR eye
    submits;
-   no post-latch MainMenu session is present.

MainMenu and Loading cannot use the ordinary semantic adapter because they have
no mode-24 consumer. Their exact higher/direct bridge instead targets
`kVR_FRAMEBUFFER`: production replays that layer into full-resolution staging,
suppresses the reduced operation only after capture, stretches the reduced base
at submit, then composites the committed layer. Render Scale remains latched and
no engine target recreation is requested. Post-latch MainMenu still requires
focused visual acceptance because the traces establish its direct contract only
before the startup latch.

## RaceSex Evidence

The focused RC83 New Game log resolves the startup RaceSex presentation path
and the previously unobserved HUD producer. It does not exercise a RaceSex open
after Render Scale has already latched.

### RC83 session integrity and fallback

RaceSex spans sessions 11 through 13 because a `MessageBoxMenu` opens and
closes over it. Together these sessions contain:

-   309 matched `DrawInterface` begin/end passes;
-   924 bridge decisions and 308 complete mode-24 consumer epochs;
-   618 successful OpenVR submissions, one per eye per completed frame;
-   no record-cap hit, trace fault, accumulator overflow, or end-argument
    mismatch;
-   a clean RaceSex close at frame 3,480 followed by continued gameplay and
    later menu/load activity through frame 4,335.

The Render Scale startup protection is effective in this run:

-   Render Scale intent and performance mode remain requested;
-   RaceSex opens at frame 3,171 with the Render Scale latch off and protection
    active;
-   all 924 RaceSex composition draws report Render Scale and presentation
    upscaling inactive;
-   all originals are kept with `higher-filter-context-inactive` and
    `operation-inactive`; no capture, suppression, layer publish, layer lifecycle,
    or final composite occurs;
-   the engine targets full-resolution `6024x2996` `kMENUBG` and `kMAIN_COPY`;
-   OpenVR receives the original `6024x2996` stereo framebuffer for all 618 eye
    submissions.

On close, the protection tail extends from frame 3,480 through frame 3,540.
Protection then releases, the Render Scale latch becomes active at frame 3,576,
the relatch recreation runs from frames 3,577 through 3,589, and a later menu at
frame 3,753 confirms Render Scale and presentation upscaling active again. This
proves both fail-closed entry and delayed recovery for the startup path.

### Post-latch controlled run: fallback failure

The later controlled run exercises the missing case directly. Render Scale
relatches at frame 60,110 with a `4936x2740` display plan and a `3290x1826`
render plan. RaceSex then opens at frame 62,793 while the trace still reports
`latched=yes` and `protection=no`.

RaceSex spans sessions 8 through 11 because Console is initially open and a
`MessageBoxMenu` later opens and closes over RaceSex. Together the RaceSex
sessions contain:

-   666 matched `DrawInterface` begin/end passes;
-   2,129 dedicated bridge decisions and 666 group-16 mode-24 epochs;
-   1,332 successful OpenVR submissions, one per eye per completed frame;
-   no record-cap hit, trace fault, accumulator overflow, dropped resource, or
    failed OpenVR submission;
-   a name-change hook at frame 63,457, a clean RaceSex close at frame 63,459,
    and a 60-frame post-close tail through frame 63,519.

The trace-baseline implementation does not satisfy the post-latch fallback
contract:

-   RaceSex context records report Render Scale and presentation upscaling
    inactive, and all bridge originals are kept with no capture or final
    composite;
-   the bound `kMENUBG`, `kMAIN` depth, and `kVR_FRAMEBUFFER` resources remain at
    the reduced `3290x1826` render extent rather than the `4936x2740` display
    extent;
-   OpenVR receives the original reduced `3290x1826` stereo framebuffer on every
    recorded RaceSex submit;
-   `latched=yes`, `protection=no`, and `pendingRelatch=no` persist through open,
    close, and the protection tail, so no full-resolution recreation occurs and
    no recovery relatch is needed.

This is a clean trace-baseline implementation failure rather than a tracing or
submission failure. Its RaceSex gate disables presentation-upscaling behavior
logically while leaving reduced resources latched. The production fix removes
that gate and routes the proven full semantic consumer into the full-resolution
transaction instead of attempting a resource transition.

### Post-latch draw and producer details

The stable post-latch mode-24 contract contains one projected `6x2`, one HUD
`504x2`, and one HUD `144x2` operation. All 666 instances of each operation
target reduced `3290x1826` `kMENUBG`. Of the 666 epochs:

-   665 have authoritative three-draw generic/direct parity and `unrelated=0`;
-   frame 63,445 contains four generic composition draws: two projected `6x2`
    draws followed by HUD `504x2` and HUD `144x2`;
-   only three dedicated bridge decisions are emitted for that four-draw epoch,
    so calibration correctly marks it non-authoritative.

The trace also records 131 projected `6x2` operations outside any accumulator,
targeting an unregistered `1024x1024` resource. They are not final
`kPROJECTEDMENU`/`kHUDMENU -> kMENUBG` consumers and remain untouched. The
uncalibrated four-draw epoch still enters through the exact semantic/direct
contract, so production transports every registered operation without relying
on the generic diagnostic count.

Producer correlation remains complete for the stable mode-24 consumer:

-   all 666 RaceSex projected passes clear and redraw the source, totaling
    121,134 depth-disabled draws with per-pass counts from 36 to 192;
-   both HUD operations reuse complete generation 9,640, produced in the Console
    session at frame 62,792 by a clear and 11 draws;
-   all 1,332 HUD consumptions refer to that unchanged pre-RaceSex generation;
-   the 132 exceptional projected consumptions outside the stable mode-24 path
    are incomplete at their observation point and must not authorize capture.

### Post-latch ordinary-layer lifecycle

The Console's ordinary layer, generation 212 from frame 62,792, remains marked
published at RaceSex open, through all RaceSex sessions, and at RaceSex close.
It is not composed while RaceSex is active because final composition is
disabled. The next traced Journal frame stages and publishes generation 213
before either eye consumes a menu layer, so this run contains no stale-layer
consumption.

There is nevertheless no explicit generation-212 invalidation in the trace
baseline. Production invalidates the committed layer on menu open and does not
rely on a later eligible menu producing a replacement before old content can
satisfy final-composite preconditions.

### RC83 consumer contract

Every one of the 308 completed group-16, `renderMode == 24` epochs is
authoritative: dedicated and generic direct counts match, exactly three draws
are issued, and `unrelated=0`. Each epoch performs, in order:

1. `kPROJECTEDMENU`: `DrawIndexedInstanced(6, 2, 0, 0, 0)`;
2. `kHUDMENU`: `DrawIndexedInstanced(504, 2, 0, 0, 0)`;
3. `kHUDMENU`: `DrawIndexedInstanced(144, 2, 0, 0, 0)`.

All three operations use complete format-28 `2048x2048` sources, target the
same full-resolution `kMENUBG`, bind full-resolution `kMAIN_COPY`, enable
`LESS_EQUAL` depth testing with depth writes disabled, and use the exact
higher/direct bridge nesting. The selector is zero. RC81 observed HUD counts
168 and 144 with selector 128, so draw shape and selector vary across valid
RaceSex configurations and cannot authorize ownership.

### RC83 producer correlation

The projected source is rebuilt completely on every one of the 309
`DrawInterface` passes:

-   each pass begins with a full transparent clear and redraws the full viewport;
-   draw counts range from 37 to 180 and total 50,962;
-   producer depth is disabled and no producer draw writes depth;
-   all 308 consumed generations are complete and have exactly one frame of
    producer-to-consumer latency.

This differs materially from RC81's mostly clear-only projected producer and
demonstrates why producer draw counts cannot be part of the semantic contract.

The previously unobserved persistent HUD source is also resolved. Session 10,
frame 3,164 clears `kHUDMENU` and redraws it with 27 full-viewport,
depth-disabled draws, producing complete generation 3,742. RaceSex opens seven
frames later. Both HUD operations consume that exact generation on all 308
consumer frames, for 616 complete consumptions, with no HUD write during
RaceSex. Persistent cross-session reuse is therefore proven directly.

### RC83 pre-roll limitation

The explicit `RaceSexPreRoll` session did not arm. The runtime trace reports
`loadKind=unknown` throughout and contains neither `Handled kNewGame` nor the
`NotifyGameLoadStarted(true)` generation-reset record, so every summary reports
`raceSexPreRoll=false`. The log does not establish why the SKSE New Game signal
was absent.

This is a diagnostic trigger limitation, not missing producer evidence or a
Render Scale protection failure. The preceding MessageBox session retained the
required generation history and captured the exact HUD writer. If a RaceSex
adapter is ever reconsidered, the pre-roll trigger must be made independent of
that missing signal; it is not needed for the selected full-resolution
fallback.

### RC81 crash baseline

The crashing RC81 RaceSex log substantially extends RC79. It starts from New
Game loading, opens RaceSex at frame 7,736, and records through frame 9,748 at
the crash timestamp.

### Session integrity

The active RaceSex session contains:

-   2,013 matched `DrawInterface` begin/end pairs;
-   6,036 dedicated bridge decisions;
-   2,013 projected producer passes;
-   4,026 successful OpenVR submissions, one per eye per frame;
-   approximately 193,224 sequential trace records, below the 300,000 cap;
-   no trace-fault, accumulator-overflow, or end-argument-mismatch record.

The crash prevents a menu-close event and session summary. Required records
remain present through the last completed `DrawInterface` end at
14:25:07.096, matching the crash report's 14:25:07 timestamp.

### RaceSex consumer contract

From the first consumer frame through the last completed pre-crash frame,
RaceSex executes exactly one group-16, `renderMode == 24` consumer epoch per
frame. Every epoch contains these three direct operations in order:

1. `kPROJECTEDMENU`: `DrawIndexedInstanced(6, 2, 0, 0, 0)`;
2. `kHUDMENU`: `DrawIndexedInstanced(168, 2, 0, 0, 0)`;
3. `kHUDMENU`: `DrawIndexedInstanced(144, 2, 0, 0, 0)`.

All 6,036 operations:

-   use fixed `2048x2048`, format-28, single-sample sources for this session;
-   target the same reduced `2964x1425` `kMENUBG`;
-   bind the reduced `kMAIN` DSV;
-   use engine vertex/pixel shader IDs 66/66;
-   use triangle-list topology and one full-target viewport;
-   disable scissoring and stencil;
-   enable `LESS_EQUAL` depth testing with depth writes disabled;
-   use the same back-face/CCW rasterizer and alpha-blend state;
-   occur under the exact higher/direct bridge nesting;
-   report selector 128.

Selector 128 happens to be stable here, while valid ordinary consumers in the
other RC81 trace use selector zero. This reinforces that selector is not a
portable ownership rule. The three draw shapes are payload and must be replayed
without shape filtering or resource deduplication.

The consumer epoch redraws the complete RaceSex composition every frame from
the currently bound persistent sources. Producer updates are sparse, but the
consumer is not incremental. This establishes that the semantic adapter can
replay the complete ordered composition without producer-generation gating. No
path may clear a committed layer merely because neither source was updated in
that video frame.

### RaceSex producer behavior

The projected producer is fully characterized for the active session:

-   it is cleared to transparent in every one of the 2,013 `DrawInterface`
    passes;
-   1,959 passes are clear-only;
-   54 isolated passes clear and then perform one full-viewport draw;
-   producer depth testing is disabled for all 54 draws;
-   six of those draws use stencil and are preceded by a stencil clear;
-   no projected copy, resolve, or resource update is observed.

The projected generation completes at `DrawInterface` end and is consumed by
the next frame's `6x2` bridge operation. Its resource identity remains stable
through the crash.

The HUD source behaves differently:

-   both HUD operations sample the same stable `2048x2048` resource every frame;
-   all 4,024 HUD consumptions report `generation=unobserved`;
-   no write, clear, copy, resolve, update, producer pass, or global-output pass
    for that HUD resource appears during the RaceSex session;
-   the HUD resource observed during the preceding Loading session has a
    different identity and cannot be correlated to the RaceSex HUD source.

Loading closes at frame 7,690 and RaceSex opens at frame 7,736. RC81 has no
targeted session during that 46-frame interval and resets producer history when
RaceSex rearms. The RaceSex HUD may be created or written in that interval, in
an earlier uncorrelated path, or through a context RC81 does not cover. This log
does not distinguish those cases. It does prove that an unobserved generation
can remain a valid, repeatedly consumed source for more than 2,000 frames.

The implementation therefore treats the exact semantic consumer and bound
source identity as authority. Producer-generation visibility is diagnostic and
cannot gate capture.

### RC81 fallback behavior before the startup guard fix

RaceSex protection activates at menu open and remains active:

-   Render Scale intent remains enabled, but RaceSex eligibility is false;
-   every bridge decision is kept original with
    `higher-filter-context-inactive`/`operation-inactive`;
-   presentation upscaling is inactive for every RaceSex bridge operation;
-   no diagnostic full-resolution layer is captured or composed;
-   both eyes are submitted through the original path from the reduced
    `2964x1425` stereo framebuffer.

This proves the fallback avoids CS bridge suppression and final composition. It
does not prove the then-proposed full-resolution RaceSex adapter, because that
path never runs in this trace.

### Race-change crash correlation

The user-reported trigger is changing race from Nord. RC81 does not record
RaceMenu actions, so the exact input frame cannot be identified. The only
exceptional registered-resource operation near the reported change is one
opaque clear of reduced `kMENUBG` inside mode 24 at frame 9,552,
14:25:02.688. Seven later projected redraws occur before the crash. The same
three-operation bridge contract, resource identities, state, and successful
original OpenVR submissions continue through frame 9,748.

The crash report records:

-   `EXCEPTION_ACCESS_VIOLATION` in `SkyrimVR.exe+126C830` while reading through
    an invalid pointer;
-   a likely Papyrus VM/tasklet thread;
-   `RaceMenuPluginXPMSE.OnSliderRequest()` called from
    `racemenubase.OnMenuReinitialized()`;
-   `skeletonbeast.nif` as the first relevant object;
-   PapyrusTweaks in the probable call stack;
-   no Community Shaders frame in the probable or scanned call stack.

Community Shaders is loaded, but the renderer trace reports no fault and
finishes its final recorded frame normally. This does not prove the crash's
root cause or rule out prior memory corruption. It does show no direct evidence
of a CS menu-rendering failure at the crash site, while the immediate stack
context is RaceMenu/XPMSE/Papyrus reinitialization.

### Final RaceSex policy

Use the same semantic transaction as ordinary menus. RC83 proves a complete
ordered mode-24 consumer every RaceSex frame even when the HUD producer is
persistent and predates the session. Production therefore:

1. Invalidates stale committed content on menu open.
2. Accepts UI-query or registered-event RaceSex ownership.
3. Replays every exact registered projected/HUD consumer operation into staging.
4. Keeps Render Scale latched and composites the committed layer after scene
   upscaling through overlaps and the close tail.
5. Leaves unregistered non-final operations untouched.

The startup trace records a name-change hook at frame 3,478 and a clean close at
frame 3,480, while the post-latch trace records its name-change hook at frame
63,457 and clean close at frame 63,459. Neither log labels the exact race-choice
input, so the race-change acceptance case still requires user confirmation; it
does not require another discovery trace.

## Map Evidence

The focused Nvidia/OpenComposite RC83 log resolves the Map policy. It contains
eight substantive Map-containing sessions, including `MessageBoxMenu|MapMenu`,
and 230 completed `DrawInterface` frames. None of the sessions faulted, reached
the 300,000 retained-record limit, or reported accumulator overflow. All
generation consumptions reported by the completed session summaries were
complete; OpenVR submission succeeded throughout.

The active presentation plan rendered at `5120x2546`, upscaled to a
`6024x2996` stereo final target, and submitted `3012x2996` per-eye textures.
The existing diagnostic capture intentionally remained inactive for Map: all
486 bridge candidates kept their originals with
`higher-filter-context-inactive`, and all 460 final-composite attempts were
rejected by the base precondition. The trace therefore observes the unmodified
scaled Map path rather than a diagnostic replay.

### Map source production

In the stable final RC81 segment, each `DrawInterface` pass updates the
registered `2048x2048` menu sources before the next frame consumes them:

-   `kPROJECTEDMENU` receives a full transparent clear and no draw;
-   `kHUDMENU` receives a full transparent clear followed by 27 full-viewport
    draws;
-   the HUD producer draws have depth disabled;
-   the completed HUD generation is consumed by the next frame's bridge calls.

Across the final session, the tracer reported:

-   1,907 producer passes;
-   5,721 producer resource observations and 54,515 producer draws;
-   2,668 clear-then-draw resource updates and 3,053 operations-only updates;
-   1,907 global-output passes, 3,814 resources, and 51,489 draws, all inside
    `DrawInterface`;
-   no out-of-`DrawInterface` global-output writer; the RC81 aggregate summary
    does not classify every operation well enough to exclude copy, update,
    resolve, dispatch, or command-list paths for all Map states.

The exact per-resource totals cannot be reconstructed from the aggregate
summary alone. The final-frame records directly identify the projected
clear-only generation and the HUD clear-plus-27-draw generation.

### Map mode-24 ordering

Every stable Map frame contains two mode-24 epochs targeting reduced
`kMENUBG`. The semantic bridge operations remain recognizable:

-   230 HUD `24x2` draws with the reduced `kMAIN` DSV;
-   231 HUD `24x2` draws without a DSV;
-   nine projected `6x2` draws with `kMAIN` and nine paired projected draws
    without a DSV during transitions.

The second mode-24 epoch is not a pure HUD bridge. Its stable ordering is:

1. HUD `24x2` bridge draw without a DSV.
2. A non-composition `12x2` draw without a DSV.
3. One or more non-composition `3600x2` Map draws using `kMAIN`, depth testing,
   and depth writes.

Across the focused sessions, RC83 records 225 `12x2` draws and 462 depth-
writing `3600x2` draws in these mode-24 epochs. Another 50 `6x2` draws sample
`kWORLDUI0` with `kMAIN`. Transition frames can place projected bridge draws
before the HUD, but the defining property remains: Map content is written
after the HUD into the same destination.

Calibrated mode-24 epochs report complete generic/dedicated draw parity and no
`unrelated` draws. That does not make the epochs HUD-only. The additional
`12x2`, `3600x2`, and `kWORLDUI0` work is inside the exact higher/direct path,
so it is correctly classified as `directBridge`. A few epochs around hook-
coverage changes are explicitly non-authoritative and are not used for this
conclusion.

### Map resource and context behavior

The detailed stream records a `kMENUBG` render-target clear inside mode 24 on
all 230 completed Map frames and a `CopyResource` to `kMAIN.copy` outside the
accumulator on those frames. It also captures the surrounding dispatches,
updates, copies, clears, mip generation, and live map/world rendering.

RC83 installed the deferred-context and command-list hooks, but this run used
only the immediate context: it contains no deferred-context draw, no
`FinishCommandList`, and no `ExecuteCommandList`. Their absence is therefore a
runtime result, not a trace-coverage gap.

### Final Map policy

The narrow ordinary-menu adapter is invalid for Map:

-   replaying only the HUD bridge at submit moves it above Map content that the
    engine intentionally draws afterward;
-   replaying the whole mode-24 epoch moves depth-writing 3D Map work into the
    menu layer and requires full-resolution scene depth and related resources;
-   suppressing only selected original draws cannot preserve destination and
    depth dependencies atomically across the mixed epoch.

The desired eventual result is still a complete mixed epoch at full resolution
without changing the latched Render Scale plan. The attempted implementation
temporarily substituted full-resolution staging plus matching depth views for
`kMENUBG`, kept engine draw order, and restored the reduced target before later
copy work. RC88 proves that this is not yet a valid presentation contract:
after CS clears staging to `[0,0,0,0]`, Skyrim clears the same `4936x2740`
target to `[0,0,0,1]`. Final alpha composition therefore replaces the reduced
terrain with opaque black while only later widgets remain visible.

The active safe policy disables Map substitution, capture requirements, Map
depth allocation, and Map submit composition. Skyrim keeps its original mixed
epoch and reduced `kMENUBG -> kMAIN` transport, restoring a visible Map at the
cost of reduced-resolution Map UI. A future full-resolution Map adapter must
first establish alpha ownership or a complete replacement base; successful draw
capture and OpenVR submission alone are insufficient.

The log does not label individual pan, zoom, marker, or local/world actions, so
it cannot map each interaction name to a draw sequence. That mapping is not
needed for the policy decision: the repeatedly observed post-HUD depth-writing
dependency already disqualifies selective bridge replay. The RC88 opaque-clear
record is sufficient to disable the current substitution; any future design
needs focused visual validation of its new alpha/base contract.

## RC81 Trace Accounting Limitation And Fix

RC81's dedicated `bridge-decision` records are complete enough to establish
the ordinary operation contract. They record the direct call, semantic epoch,
source, destination, full state, and operation result before capture.

The generic D3D accumulator draw accounting is not equally reliable. Most
known bridge decisions occur in epochs whose generic `totalDraws` and
`directBridgeDraws` fields remain zero, although the dedicated direct hook
proves that the draw occurred. Consequently, a generic epoch report of zero
unrelated draws is not independent proof that every D3D draw was observed.

The post-RC81 tracer addresses this before another trace run:

-   the dedicated direct hook increments `directBridge` for every invocation,
    independent of source filtering, candidate acceptance, or suppression;
-   `directIssued` counts original bridge draws sent to D3D and
    `directSuppressed` counts operations replaced by the diagnostic replay;
    `genericDirect` and `internalReplay` independently count those two paths in
    the generic hook;
-   the D3D hook installer now registers up to eight distinct concrete context
    implementations instead of assuming the first context/vtable covers all
    bridge calls;
-   the first epoch in which a new context implementation is registered is
    deliberately uncalibrated because earlier draws in that epoch may have been
    missed;
-   each epoch records every observed context/vtable pair, context overflow,
    installation failure, coverage changes, and direct-draw calibration;
-   each bridge decision records its context pointer, vtable, concrete
    `DrawIndexedInstanced` target, context type, and current coverage state;
-   any exception in generic draw tracing invalidates the current epoch's draw
    accounting.

`menu-accumulator-end` now reports `drawAccounting(authoritative=...)`.
Unrelated counts, including zero, may be used as evidence only when this is
`true`. That requires hook coverage before the epoch, no registration change or
failure during it, no context-capacity overflow, at least one dedicated bridge
decision, `directIssued == genericDirect`, and
`directSuppressed == internalReplay`. Dedicated `bridge-decision` records
remain the authoritative operation records. The accumulator end arguments and
trace session must also still match their begin values.

RC82/RC83 provide that runtime verification: the first newly discovered context
epoch is uncalibrated, later matching semantic epochs become authoritative, and
their direct-issued/generic counts agree. This repairs the diagnostic counter;
it does not justify whole-epoch suppression or reduced-`kMENUBG` rollback. The
dedicated bridge records remain the ownership evidence.

## Implemented Menu Pipeline

### 1. Eligibility and preflight

The adapter runs only when all of these are true:

-   SkyrimVR;
-   a stable, latched Render Scale contract owns reduced engine targets and a
    larger final output, including Loading frames where logical presentation
    upscaling is temporarily inactive;
-   the runtime plan is stable and final size exceeds render size;
-   OCU is not providing its own upscaling;
-   the context selects its proven ordinary semantic or Main/Loading direct
    adapter; Map explicitly bypasses hybrid ownership;
-   CS menu, OCU upscaling, RenderDoc, device-loss, pending real transitions, and
    other existing safety blockers permit it;
-   separate staging and committed resources can be preflighted before ownership;
-   shaders and immutable states can be prewarmed before ownership;
-   expected formats, sample counts, and stereo dimensions are valid;
-   the committed stereo layer can be sampled directly by the Present-time
    desktop overlay without allocating or copying HMD eye-pair banks.

No allocation, shader compilation, or resource recreation may occur after the
transaction captures its first operation. The adapter supports only the
immediate D3D11 context used by the
verified bridge path. A different or deferred context is a contract failure,
not a candidate for opportunistic interception.

### 2. Authorize semantic epochs independently of tracing

The existing `BSShaderAccumulator::RenderBatches` hook now invokes production
begin/end ownership independently of the optional trace observer. Production
state therefore runs when developer logging is disabled. Every successful
production begin has a matching normal-path end.

An ordinary operation requires:

-   `renderMode == 24`;
-   the exact higher bridge call nested in that epoch;
-   the exact direct draw nested in the higher call;
-   a bound `kPROJECTEDMENU` or `kHUDMENU` source;
-   `kMENUBG` as the destination;
-   an active, stable Render Scale presentation plan.

The observed group-16/pass tuple remains diagnostic data; it does not authorize
or reject production ownership.

The semantic state is render-thread-owned and stack-bounded. It records epoch
ID, full `RenderBatches` arguments, operation count, display-substitution state,
and validity. The frame transaction retains a bounded sample of epoch IDs while
counting additional sequential epochs. Menu names select exceptional adapters
or block incompatible overlap; they do not authorize ordinary capture.

At Info level, production higher/direct ownership is entered only inside an
already-authorized mode-24 semantic epoch or the explicit MainMenu/Loading
direct adapter. The previous broad monitor performed source/RTV and runtime-plan
queries for hundreds of thousands of unrelated calls; RC88 measured 212,040
direct observations for 59 Journal bridges. Broad out-of-epoch monitoring now
exists only while developer tracing is active. A MainMenu/Loading direct bridge
must still target `kVR_FRAMEBUFFER`; no selector/index exception or menu capture
whitelist is learned at runtime.

### 3. Build one ordered frame transaction

Start staging lazily on the first verified operation in a video frame. Clear
only the staging texture at that point. Never clear or mutate the committed
layer because the frame advanced.

Every eligible semantic epoch in the frame appends to the same transaction in
D3D call order. This removes an unnecessary one-epoch-per-frame assumption and
preserves the engine result if modded UI introduces another authorized epoch.
Each epoch end validates its own stack and operation accounting; publication is
deferred until the frame transaction is sealed for presentation.

For every verified direct operation:

-   query source/destination descriptors dynamically;
-   preserve IA bindings, shaders, constants, samplers, topology, the observed
    color blend equation, and rasterizer behavior;
-   retain the observed RGB equation `SRC_ALPHA`, `INV_SRC_ALPHA`, `ADD`, while
    using `ONE`, `INV_SRC_ALPHA`, `ADD` for staging alpha instead of the engine
    target's `ZERO`, `INV_SRC_ALPHA`, `ADD` alpha equation;
-   transform viewport/scissor to the final stereo extent;
-   apply the verified depth policy;
-   draw into full-resolution staging;
-   suppress only the matching reduced `kMENUBG` operation.

Capture into staging first. Suppress the original only when that individual
capture completed successfully and all D3D state was restored. Use scope guards
for the replay-in-progress flag and every acquired COM interface. A failed
capture leaves that original engine draw intact in the reduced engine target,
but the recognized required transaction is poisoned so that target cannot be
submitted as a complete HMD frame.

Do not inspect selector or index count. Do not use PS resource scanning to
discover ownership. After semantic ownership is established—or when an
out-of-epoch monitor candidate must prove it is the exact bridge—inspect only
the bound input and destination to validate the registered
`kPROJECTEDMENU`/`kHUDMENU` to `kMENUBG` contract and obtain the SRV/RTV. Do not
deduplicate by resource or destination. Repeated operations using the same
source and destination remain distinct ordered payload. The Console trace
specifically requires projected `6x2`, HUD `504x2`, and HUD `144x2` to survive
as one transaction.

### 4. Seal, publish, and fail closed

At the first OpenVR eye submit for the frame, require:

-   every source-capture scope that contributed to the current consumer frame has
    ended; an earlier `DrawInterface` must be closed, while the traced later
    source-producing `DrawInterface` belongs to the next consumer generation;
-   no semantic, higher-call, direct-call, or internal-replay scope remains open;
-   every contributing epoch ended with matching begin/end arguments;
-   the runtime-plan and resource generation still match preflight;
-   at least one operation was captured, every recognized operation is represented
    in staging, and every suppressed operation has exact capture parity.

If valid, seal the transaction, swap staging and committed textures, and retain
the frame, epoch IDs, runtime-plan generation, operation count, and layer
generation. Both eyes must consume that same committed generation. A semantic,
direct, or Map operation that still belongs to the sealed consumer is a
transaction fault. The traced `DrawInterface` producer that begins after submit
is detached from the sealed transaction, keeps its original draw, and cannot
mutate staging or poison the second eye.

Preflight alone cannot make an opaque sequence of later draw interceptions
non-fallible. If any accepted operation fails, including the first operation:

1. mark the frame transaction poisoned;
2. keep staging uncommitted so the previous committed generation is not
   replaced;
3. let `SubmitVRUpscaledFrame` reject the replacement, then let the existing
   reduced-fallback guard consume the original submit without forwarding it so
   OpenVR reprojects the previous complete frame;
4. keep the previous complete desktop eye pair rather than presenting a partial
   or mixed pair;
5. retry preflight on a later frame after normal resource or plan recovery; menu
   failure never requests a Render Scale relatch.

Context changes before rendering ownership update the transaction requirements
without poisoning it. After any recognized operation, capture, suppression, Map
substitution, or replay, a context change remains fail-closed for the rest of
that transaction.

Compact diagnostics record the first poison reason and remain around semantic
completion, sealing, final composition, desktop publication, and OpenVR submit.
Another broad menu-discovery trace is unnecessary.

Do not add a reduced-`kMENUBG` backup/restore path based on RC81's generic
unrelated-draw counter. RC82 repaired the counter, but the evidence still does
not prove that rolling back the complete destination is safe across arbitrary
ordinary menu and mod combinations.

### 5. Reuse and invalidation

Do not clear or invalidate solely because a video frame advanced or a trace
session rearmed.

The committed layer is reused while its presentation contract remains current.
Menu open/close events invalidate the previous context before a replacement is
built. Runtime-plan generation changes, final context end, Community Shaders
overlay ownership, device loss, resource rebuild, or an incompatible layer
descriptor also invalidate it.

A poisoned frame does not overwrite the committed layer. It rejects current
presentation and retains the previous committed generation for a later valid
frame in the same context; context or plan invalidation still discards that
generation and both desktop-pair states.

An ordinary menu-name change is not ownership by itself. Menu masks may inform
transition safety but must not become a whitelist.

### 6. Submit-stage composition

Keep the current order:

1. Upscale the reduced scene into the existing per-eye output.
2. Select the matching half of the committed stereo menu layer.
3. Composite it over the upscaled eye.
4. Submit through the existing OpenVR path.

The bridge's staging blend creates premultiplied accumulated color. Final
composition must use:

-   source: `ONE`;
-   destination: `INV_SRC_ALPHA`;
-   operation: `ADD`.

Using `SRC_ALPHA` again at final composition would multiply edge alpha twice
and blur or darken glyph edges.

### 7. Desktop mirror composition

The desktop mirror must receive the same completed ordinary-menu transaction
when CS suppresses any reduced engine bridge operation. This is a correctness
requirement, not merely the optional Render Scale mirror-quality enhancement.
Leaving the desktop path unchanged after suppression produces an HMD-only menu
by construction.

Do not copy a final per-eye HMD output to the monitor. RC88 showed why: that
texture contains the VR hidden-area mask and uses the headset eye camera, so it
replaces Skyrim's preferred unmasked desktop view even when only one eye is
shown. The monitor needs only the bridge pixels that were suppressed from the
engine path.

Immediately before `IDXGISwapChain::Present`:

1. Require a sealed compatible committed menu generation; a non-poisoned
   current transaction with captured work must have committed in this frame.
2. Acquire and validate the current swap-chain backbuffer and descriptor,
   leaving its existing Skyrim desktop image intact.
3. Sample the left half of the committed stereo menu layer and composite it
   over the full backbuffer with the same premultiplied-alpha equation used for
   HMD eyes: `ONE`, `INV_SRC_ALPHA`, `ADD`.
4. Draw the CS desktop overlay afterward so its existing presentation order is
   preserved.

This removes two pending and two retained full-eye textures plus two per-frame
eye copies. A poisoned transaction may reuse only the last sealed committed
menu layer; staging is never exposed. Atomic HMD eye submission remains owned
by the submit transaction rather than a duplicate desktop eye-pair cache.

The existing post-submit writeback to the OpenVR source texture remains a
quality optimization, but it is not the ownership boundary: the game
may already have copied that texture to its desktop target, and the current
trace does not establish otherwise. Present-time back-buffer composition makes
the destination explicit and avoids depending on undocumented engine copy
timing. The Present hook runs only while a compatible committed menu layer is
valid. The existing user setting continues to control enhanced mirror blits on
other Render Scale frames. The CS desktop overlay is drawn after this menu
composition.

## Persistent Context Adapters

MainMenu, Loading, Map, and RaceSex do not enter a native-resolution state
machine. They preserve the user's requested method, quality, Render Scale
intent, boot snapshot, vendor resources, and reduced scene targets.

-   RaceSex uses the complete ordinary semantic transaction.
-   MainMenu and Loading stretch only the reduced base, then add the exact direct
    projected/HUD layer at full resolution.
-   Map currently bypasses the hybrid and uses Skyrim's complete reduced mixed
    epoch. The dormant substitution code must not be enabled until opaque-clear
    ownership is solved.
-   Menu open and close events invalidate stale layers and arm overlap tails. Any
    real close-transition draw can publish a fresh layer; a tail alone cannot keep
    a closed Map overlay alive. Events never queue a target recreation.
-   A required adapter that cannot preflight, complete, or composite holds the
    reduced submit. It does not silently fall back to a partial frame or toggle
    Render Scale.

## Resource And Performance Budget

The hybrid avoids resizing the broad Skyrim pipeline, but atomic publication is
not free. At the observed `4936x2740` stereo output, one full stereo texture is
about 51.6 MiB in 32-bit RGBA or 103.2 MiB in 64-bit RGBA. Staging plus committed
layers cost about 103.2 MiB or 206.4 MiB. Present now samples the committed
layer directly, so the former four full-eye desktop-bank textures are neither
preflighted nor allocated. With Map substitution disabled, the full-size Map
depth resource is also outside the active policy.

Production derives formats and dimensions from the runtime layer destination
and final stereo plan rather than a hard-coded format. Allocation failure leaves
unsuppressed operations intact when possible and holds any required or
already-suppressed transaction; it cannot degrade into partial capture.

## Depth Policy

RC81 proves that every observed ordinary bridge operation binds a reduced-size
`kMAIN` DSV, enables `LESS_EQUAL`, disables depth writes, and disables stencil.
That reduced DSV cannot be bound with a larger final-resolution staging RTV.

The implementation disables depth and stencil for ordinary, RaceSex,
MainMenu, and Loading bridge replay. Map keeps the engine's original reduced
color/depth path because the attempted full-resolution mixed target has no valid
alpha contract. Focused visual validation must still confirm ordinary projected
HUD and controller/hand overlap; a depth-preserving ordinary path is not
implemented.

## Remaining Verification

Do not repeat broad discovery runs. The post-latch runs expose reduced-resource
fallback failures even though their logical presentation blockers are active.
The ordinary hybrid is committed through RC88 and the Map/desktop/production-
cost corrections are currently uncommitted; remaining work is focused visual
validation. A post-latch MainMenu run remains useful as a narrow validation
case because MainMenu shares Loading's non-semantic engine path.

### Final targeted instrumentation

The RC83 diagnostic build closes the known logging gaps:

-   Map records every hooked D3D11 draw, including draws unrelated to registered
    menu resources, both inside and outside accumulator epochs.
-   Each Map draw records a session ordinal, epoch-local ordinal, draw arguments,
    relationship to the bridge, all bound PS resources, all graphics/compute
    shader identities, engine shader IDs, IA layout/index/vertex buffers, stream
    output, render targets, DSV, viewports, scissors, topology, blend, rasterizer,
    depth/stencil, OM UAVs, CS UAVs, context identity, vtable, and context type.
-   Map also records dispatches, copies, updates, resolves, render-target/depth/UAV
    clears, structure-count copies, mip generation, and command-list finish and
    execute events in the same global sequence.
-   `CreateDeferredContext` is intercepted while developer tracing is enabled so
    newly created deferred contexts receive the same draw and operation hooks.
    Command-list identity correlates deferred recording with immediate execution.
-   Session summaries report Map draw totals inside/outside accumulators,
    unrelated draw totals, resource-operation totals, hook-bank coverage, and
    deferred-context hook coverage.
-   Producer-generation history is no longer reset when a trace session rearms.
    When the game-load notification is received, it resets at that boundary so
    Loading, pre-open, and RaceSex records share one generation ledger. The
    focused RC83 run retained continuous cross-session history even though its
    game-load notification was absent.
-   A new-game-only `RaceSexPreRoll` session is designed to start when Loading
    closes and last for at most 180 frames or until RaceSex opens. It did not arm
    in the focused RC83 run because `loadKind` remained unknown; the preceding
    traced session captured the required HUD producer instead.
-   Every captured full-resolution layer now has a trace generation, source
    generation, source/destination identity, accumulator epoch, frame, operation
    count, runtime-plan/contract generation, publish point, per-eye consumption,
    and invalidation reason. Existing bridge decisions retain all preflight and
    rejection reasons.
-   Required records continue after the 300,000 retained-record counter saturates,
    and every required record is flushed immediately.

These records are sufficient to decide semantic ownership, exceptional-menu
fallbacks, RaceSex producer/reuse behavior, transaction boundaries, and stale
layer invalidation. Logs cannot establish pixel equivalence, glyph-edge alpha
quality, or the visually correct depth policy; those remain explicit
diagnostic-parallel visual acceptance checks.

### MainMenu and Loading

Startup native behavior is proven. Post-latch Loading failure is independently
proven across 1,149 completed OCU and 1,121 completed SteamVR frames, with 4,540
reduced original submits in total. No additional discovery trace is required.
Validate one startup Loading, one post-latch Loading, and one post-latch
MainMenu lifecycle, including save/load tails and the stretched-base/direct-layer
ordering.

### Map

RC83 proves the mixed post-HUD depth-writing dependency. RC88 then proves the
full-resolution substitute is cleared to opaque black despite successful final
composites and submits. Together they select the current reduced engine bypass,
not the target-substitution adapter. No more broad Map logging is needed; the
next run should visually confirm a complete visible Map and absence of the
text-only/black hybrid result.

### RaceSex

Satisfied for semantic transport by the focused RC83 log. It provides the
pre-open persistent HUD writer, continuous source generations, authoritative
three-draw epoch parity, full-resolution targets, original submits, clean menu
close, and protection tail needed to validate the staged layer lifecycle.

The controlled post-latch run is complete and fails: all 1,332 successful
RaceSex eye submits use the reduced `3290x1826` framebuffer even though RaceSex
context flags report Render Scale and presentation upscaling inactive. Repeat
this narrow case and confirm that the semantic layer is sharp, no stale ordinary
layer survives, overlapping menus remain correct, and Render Scale remains
latched through open/close. Add an explicit test marker or user confirmation if
race-change completion itself must be distinguished from other RaceSex UI
changes.

## Current Implementation Reference

This section describes the uncommitted RC88 follow-up working tree. The trace
logs are not required to understand the code path; the measured contracts are
preserved above. Reconcile newer changes against these invariants before
editing.

### Code ownership

| File                                 | Current responsibility                                                                                                                                                             |
| ------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/Features/Upscaling.cpp`         | Semantic/direct adapters, frame transaction, staging and committed layers, submit composition, production fast-path gating, Map bypass, and Present-time desktop layer composition |
| `src/Features/Upscaling.h`           | Transaction, layer, compositor, and hook state                                                                                                                                     |
| `src/FrameAnnotations.cpp`           | Calls production accumulator begin/end around `BSShaderAccumulator::RenderBatches`; developer tracing is optional                                                                  |
| `src/Hooks.cpp`                      | Runs `PresentVRMenuDesktopMirror` before state reset and before the CS desktop overlay                                                                                             |
| `src/Features/VR/InSceneOverlay.cpp` | Suppresses unsafe reduced OpenVR fallback submits when a transition or owned menu transaction requires final-sized output                                                          |

The implementation adds no settings or UI. Existing Render Scale intent,
quality, upscaler selection, OCU blockers, and mirror-quality setting remain the
public control surface.

### Implemented production state

| State                | Ownership and invariant                                                                                                                                                            |
| -------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Semantic epoch stack | Render-thread state created for every hooked `RenderBatches` call, independent of developer tracing                                                                                |
| Frame transaction    | Frame and plan generation, contributing epochs, captured/suppressed counts, Map display-epoch count, presentation-started/render-complete/sealed/poisoned state and failure reason |
| Staging layer        | Mutable only while building the current ordinary/direct frame transaction                                                                                                          |
| Committed layer      | Immutable after a staging/committed swap; sampled by both eye composites and retained across persistent-source frames                                                              |
| Desktop compositor   | Samples the left half of the sealed committed layer over Skyrim's existing backbuffer; never copies a masked HMD eye                                                               |
| Map policy           | No transaction, target substitution, full-resolution depth allocation, or final hybrid composite; Skyrim retains the complete reduced mixed pass                                   |

No native-menu transition state exists. Menu events arm context/tails and
invalidate stale committed content; they do not modify the public Render Scale
request or recreate engine scene targets.

The transport contract exists only for a latched reduced VR Render Scale plan.
It does not create requirements, suppress submits, or alter behavior for the
existing VR dynamic-resolution path or other upscaling ownership modes.

### Production routing

1. The first eligible semantic or direct bridge operation begins or joins the
   frame transaction. An earlier `DrawInterface` may prewarm required resources;
   the traced source-producing `DrawInterface` that follows OpenVR submit is
   detached from the transaction already being presented. RaceSex ownership
   accepts both the UI/state query and the registered menu event so a delayed or
   unreliable UI lookup cannot miss or misclassify the first frame. Preflight
   must confirm the menu compositor shader, immutable D3D states,
   sampler/constant-buffer state, and both layer buffers before bridge
   suppression. MainMenu and Loading defer only the layer-format
   choice until their exact `kVR_FRAMEBUFFER` destination is known; they do not
   depend on an unrelated `kMENUBG` descriptor.
2. Every accumulator pushes production semantic state. Developer trace state is
   an observer only. Mode-24 passes are adapted only after the complete menu and
   stable reduced-plan eligibility check succeeds; unrelated mode-24 work cannot
   poison a transaction.
3. The exact higher call requires flag zero and mode zero, but does not inspect
   the disproven selector byte.
4. The exact direct draw requires bound slot-zero `kPROJECTEDMENU` or `kHUDMENU`.
   Index count and draw shape are recorded but do not authorize ownership.
5. In an eligible non-Map mode-24 epoch, the destination must be `kMENUBG`.
   Replay writes the operation into full-resolution staging, restores D3D state,
   then suppresses that one original draw.
6. In MainMenu or Loading outside a semantic epoch, the same exact bridge must
   target `kVR_FRAMEBUFFER`. It is replayed and suppressed under the same
   transaction rules. At Info level, other contexts never enter the broad
   production higher/direct monitor; developer tracing may still observe them
   without granting ownership. Bound slot-zero source and destination identity
   remain mandatory before a transaction is armed.
7. Map mode-24 always remains on Skyrim's reduced engine targets. The dormant
   substitution/ISCopy/depth code is disabled and does not mark Map required.
8. Before the first eye output, the transaction must be complete and sealable.
   Staging and committed layers swap once. The committed generation is then
   composited into both vendor-upscaled or stretched eye outputs.
   The transaction counts every contributing epoch while retaining only a
   bounded diagnostic sample of epoch IDs; additional sequential epochs do not
   invalidate otherwise correct modded menus. Layer draw count must match the
   captured-operation count at seal.
   Staging accumulates straight-alpha bridge inputs into premultiplied color;
   final composition therefore uses `ONE`/`INV_SRC_ALPHA` and does not multiply
   glyph-edge alpha a second time.
9. MainMenu/Loading may use the existing presentation stretch for the reduced
   base. Their projected/HUD layer is composited afterward at full resolution,
   so stretch is not treated as a text-quality solution.
10. The game-window Present hook preserves Skyrim's unmasked desktop base and
    alpha-composites only the committed layer's left half. It performs no HMD
    eye publication, full-eye copies, or pending/retained pair swaps.

### Failure invariants

-   A failed capture always keeps that original engine draw in the reduced target,
    but every accepted bridge operation is transaction-required before validation;
    failure therefore poisons the frame and suppresses the reduced OpenVR fallback
    even when it is the first operation.
-   Once bridge work is suppressed, the original submit is unsafe regardless of
    its nominal dimensions and is always held on failure. Map redirects no work.
-   Work arriving after the transaction is sealed is rejected and poisoned; it
    cannot change staging or produce different committed generations for the two
    eyes.
-   Menu work or a context change arriving after eye presentation starts poisons
    the transaction, including frames that reuse a retained committed layer.
-   Any submit-stage failure after menu eye presentation starts poisons the frame,
    so the other eye cannot publish a different or fallback presentation.
-   MainMenu/Loading are explicitly required transactions. Missing direct bridge
    capture, hook unavailability, resource allocation failure, scope imbalance or
    plan mismatch holds the reduced submit. Map is not a hybrid transaction.
-   A staging layer is never sampled. Only a sealed committed generation is used.
-   Eye-output cache reuse includes the committed menu generation.
-   Plan-generation changes and menu open, close, or final context end invalidate
    committed content.
-   A menu open/close event updates an already-initialized same-frame transaction;
    mixed-context work is poisoned instead of being published. Loading resets its
    tracking state before the new presentation tail is armed.
-   Opening the Community Shaders overlay invalidates a retained game-menu layer.
    An untouched same-frame requirement is released, while captured or active
    game-menu work remains fail-closed.
-   Resource reset clears the active transaction, layer-frame identity, dormant
    Map replacement resources, and prewarm cache so same-frame device/resource
    recovery cannot retain an unusable allocation decision.
-   OCU-provided upscaling and RenderDoc continue to block CS Render Scale and this
    transport.
-   Device-loss and real save/load/vendor transitions retain their existing submit
    protection. Menu presentation itself never initiates a relatch.

### Removed correctness heuristics

The production path no longer contains:

-   selector-based authorization;
-   fixed or adaptively learned index-count authorization;
-   draw-shape authorization;
-   target/resource deduplication;
-   `all-menu-targets-already-suppressed` early termination;
-   producer-generation gating;
-   broad menu blockers that disable an already latched submit-stage path.

Producer generations, shader state, selector and draw arguments remain useful
diagnostic fields only. Registered source and destination identity remain part
of the exact operation contract.

## Acceptance Gates

The implementation is structurally complete but is not a release claim. Do not
run another broad discovery trace. Validate these focused cases:

-   Inventory, Magic, Journal, Crafting, Dialogue, Tween and Console render sharp,
    update immediately, and preserve every bridge operation in order.
-   Console is readable and stable during HMD motion at Info logging level, and
    appears in the desktop game window without requiring the optional
    mirror-stabilization setting.
-   RaceSex is sharp after Render Scale has latched, including MessageBox/Console
    overlap and race changes; no Render Scale off/on transition occurs.
-   MainMenu and startup/post-latch Loading show a correctly stretched base plus
    full-resolution projected UI, with no top-left stamp or reduced original
    fallback.
-   Map remains visible through open/close, zoom/pan, markers and MessageBox
    overlap on the original reduced `kMENUBG -> kMAIN` path. No opaque black
    hybrid layer or text-only Map may be composited.
-   The desktop remains Skyrim's single unmasked game-window camera before,
    during, and after menus; it must never become an HMD eye, hidden-area mask, or
    side-by-side stereo view.
-   Glyph-edge alpha and ordinary depth behavior match the expected engine result.
-   Allocation, hook-signature, plan-change and post-suppression failure injection
    never submits or mirrors a partial/reduced transaction.
-   Repeated menu switching does not expose stale content, mixed eye generations,
    one-frame-late desktop output or unbounded resource growth. Info-level menu
    handling must not inspect broad unrelated higher/direct draws.
-   Validate materially different modlists under SteamVR and OpenComposite with
    OCU upscaling disabled. Separately confirm the existing OCU-upscaling blocker.

## Non-Goals

Do not port Open Shaders' complete Performance Mode, broad render-target
resizing, `kTOTAL` replacement, tonemap interception or earlier upscaler
placement. Do not toggle Render Scale for menu entry. The dormant Map
target/depth substitution and ISCopy correction are explicitly not part of the
active policy after RC88's opaque-clear failure.
