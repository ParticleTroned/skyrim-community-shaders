# SkyrimVR Full-Resolution Menu Presentation Strategy

Status: evidence collection is complete and the implementation handover is
ready. Ordinary menus use semantic bridge transport; Map and RaceSex use an
engine-native full-resolution transition. MainMenu and loading retain their
existing native paths. Remaining work is implementation plus focused visual and
failure-path validation, not another broad discovery trace.

Branch: `cs-1.7-PL-VR`

Implementation head reviewed: `0c66c97b895c2ee2b939073ab6cf73c87eb6ea7f`

Trace baseline: `87522b1f47ef5544b92962cdd11177db8e746ea8` (`RC83`)

Open Shaders reference reviewed: development commit
[`6070998cc5f290b929d072a76460d7965c053ea2`](https://github.com/alandtse/open-shaders/commit/6070998cc5f290b929d072a76460d7965c053ea2),
particularly `src/Features/Upscaling/PerfMode.h`. This is an architectural
comparison only; no Open Shaders source is copied by this document.

Evidence reviewed:

- RC77 multi-user menu traces;
- RC79 ordinary-menu and RaceSex traces;
- RC81 semantic epoch, producer-generation, render-state, MainMenu, loading,
  and ordinary-menu trace.
- RC81 Nvidia/OpenComposite trace containing ordinary menus and three Map
  sessions, including a 1,908-frame uninterrupted Map session.
- RC81 RaceSex trace containing 2,013 in-menu frames through a race-change
  crash, together with the matching Crash Logger report.
- RC82 self-calibrating generic draw accounting and multi-context hook
  coverage. Its runtime behavior is confirmed by the focused RC83 Map and
  RaceSex logs.
- Focused RC83 Nvidia/OpenComposite Map trace containing eight substantive
  Map-containing sessions, 230 completed `DrawInterface` frames, detailed draw
  and resource-operation records, accumulator parity, and successful OpenVR
  submits. No trace fault or retained-record-limit truncation occurred.
- Focused RC83 New Game RaceSex trace containing 309 completed
  `DrawInterface` frames, authoritative mode-24 parity, continuous producer
  generations, MessageBox overlap, clean close/tail/re-latch behavior, and 618
  successful original-path OpenVR submits.
- Post-latch RaceSex trace opening at frame 62,793 after Render Scale had
  latched, spanning Console and MessageBox overlap, 666 completed RaceSex
  frames, and 1,332 successful but reduced-resolution original-path OpenVR
  submits.

This document is a design and verification record. It is not a release claim.

## Decision

Keep CS's existing VR architecture:

- reduced-resolution scene rendering;
- submit-stage presentation upscaling;
- existing per-eye output and OpenVR submission;
- existing OCU, loading, save/load, device, and runtime-plan guards;
- final full-resolution menu composition.

For ordinary menus, replace discovery with semantic ownership:

1. A verified `BSShaderAccumulator::RenderBatches` consumer epoch authorizes
   capture.
2. The exact higher engine bridge call establishes a bridge operation.
3. The existing direct draw interception transports every operation, in order,
   into a full-resolution staging layer.
4. Epochs contribute to one frame transaction. The layer is committed only when
   that transaction is sealed before the first eye submit, then reused until
   superseded or invalidated.

Remove these from ordinary-menu correctness:

- selector inspection, including the `subject + 0x190 == 0x80` test;
- known or adaptively learned index counts;
- PS resource scanning as ownership discovery;
- resource-based operation deduplication;
- menu-name whitelists;
- frame-based clearing of the committed layer.

RC81 requires one correction to the earlier generation design: an observed,
complete producer generation must not authorize or gate capture. A persistent
HUD source can be valid even when its generation predates the currently armed
trace or menu session. The semantic consumer, exact bridge nesting, and bound
source/destination contract authorize capture. Producer generations remain
useful for diagnostics, reuse, and invalidation.

MainMenu and loading are proven fallback paths with Render Scale inactive.
Map must use the same full-resolution policy: RC83 proves that its mode-24
epochs interleave the HUD bridge with later depth-writing Map content in the
same `kMENUBG` destination. A HUD-only replay changes ordering, while a whole-
epoch replay would absorb 3D Map work and require a full-resolution depth path.
RaceSex must also use a full-resolution fallback. RC83 proves that the historical
startup guard kept Render Scale unlatched from menu open through its post-close
tail, while the engine performed the complete RaceSex composition into full-
resolution targets. No RaceSex bridge replay is needed.

The reviewed implementation head is materially different from the RC83 trace
baseline: `0c66c97b8` removed RaceSex startup and post-load Render Scale
blocking. The current code therefore does not provide either the historical
startup fallback or a post-latch fallback. Do not restore the old logical
"inactive" flags as the fix. Implement the native-resolution transition state
machine defined below so engine resources actually become full resolution
before a RaceSex frame can be submitted.

## Final Re-analysis And Hybrid Boundary

The hybrid remains the best fit for CS, but the accumulated evidence changes
four implementation details from the earlier outline:

1. **Frame ownership, not epoch publication.** The latest RC83 Console and
   Journal samples each contain exactly one mode-24 epoch per observed frame
   (212 and 105 frames respectively), but correctness must not depend on that
   count. All authorized epochs in one video frame append to one ordered staging
   transaction, which is sealed at presentation.
2. **Real atomic failure containment.** Resource preflight cannot guarantee that
   every later intercepted operation will succeed. Once any reduced engine draw
   is suppressed, a later failure poisons the frame; that frame must not fall
   through to an original reduced OpenVR submit or a partial desktop mirror.
3. **Separate production ownership from tracing.** The current accumulator
   begin/end API is trace-gated. Semantic transaction state must run whenever the
   adapter is enabled, with tracing attached only as an observer.
4. **Exceptional menus require resource transitions.** Map and RaceSex cannot
   be made correct by toggling presentation booleans while reduced resources
   remain latched. They need an overlap-aware native-resolution state machine
   that preserves user intent and controls both entry and relatch.

Final routing is:

| Context | Presentation policy | Reason |
| --- | --- | --- |
| Ordinary menus, including Console | Semantic mode-24 bridge transport to a full-resolution layer | Complete ordered bridge contract is known |
| Map | Temporary engine-native full-resolution resources | HUD bridge and later depth-writing 3D Map draws share `kMENUBG` |
| RaceSex | Temporary engine-native full-resolution resources | Complete composition is known; post-latch logical bypass failed |
| MainMenu and loading | Existing engine-native fallback | Trace proves Render Scale is inactive and no ordinary consumer exists |
| Desktop game window | Present-time blit of a complete matching eye pair | Suppression otherwise removes UI from the engine mirror path |

Open Shaders obtains robust menu rendering by changing the broader rendering
contract: it enlarges selected menu/intermediate targets, supplies a display-
size depth target for the UI pass, adjusts viewport/copy behavior, and lets the
engine draw the UI at output resolution. That avoids selective draw discovery
and naturally includes unusual or modded UI, but it owns substantially more of
the render-target, depth, post-process, and memory lifecycle.

CS should retain reduced scene rendering and submit-stage upscaling. The narrow
semantic adapter preserves that architecture, avoids broad scene-target resizing,
and limits extra full-resolution storage to menu staging, committed layers, and
the final eye pair. For Map and RaceSex, however, the hybrid deliberately adopts
the Open Shaders principle rather than its implementation: let the engine run the
complete exceptional pipeline against native resources instead of trying to
classify or replay selected draws.

## RC81 Ordinary Log Integrity And Coverage

The supplied RC81 log is approximately 1.2 GB and reaches trace sequence
2,193,004 across 31 sessions.

- The first MainMenu session reached the 300,000 record counter.
- Required records continued after the counter saturated.
- No trace fault was reported.
- No accumulator stack overflow was reported.
- No accumulator end-argument mismatch was reported.
- No tracked resource was dropped.
- The final Journal session was still active when logging ended, so it has no
  closing session summary; its retained records are present.

Covered contexts include:

- Main Menu;
- Loading Menu;
- Tween;
- Inventory;
- Magic;
- Stats/Skills;
- Dialogue;
- Container;
- Crafting;
- Journal;
- Sleep/Wait;
- Lockpicking.

This first RC81 log contains no Map session and no RaceSex event. It therefore
cannot resolve those exceptional paths by itself.

The sampled active runtime plan was:

- reduced stereo render and `kMENUBG`: `4024x2125`;
- final stereo output: `4736x2500`;
- submitted eye: `2368x2500`;
- DLSS quality mode: 1;
- Render Scale and presentation upscaling active for ordinary menus.

## RC81 Nvidia/OCU Map Log Integrity And Coverage

The second RC81 log is approximately 1.07 GB and contains 52 trace sessions.
It was captured with the OpenComposite runtime and includes three Map-related
sessions: Map alone, MessageBox over Map, and a final uninterrupted Map session.
It contains no RaceSex menu-open event or RaceSex trace session; RaceSex will
be evaluated from a separate log.

The final Map session:

- lasted 1,908 frames, with 1,907 matched `DrawInterface` begin/end pairs;
- reached the 300,000 retained-record counter;
- continued required records after saturation and flushed them per record;
- reported no trace fault, accumulator overflow, or dropped resource;
- closed normally and emitted a complete session summary.

The active Map presentation plan was:

- reduced stereo render and `kMENUBG`: `2964x1425`;
- final stereo layer: `5040x2424`;
- submitted eye: `2520x2424`;
- Render Scale and presentation upscaling active for every recorded Map bridge
  operation;
- both OpenVR eye submissions succeeded throughout the final session.

This is an RC81 trace. Its headers do not contain RC83's Map per-draw stream,
resource-operation stream, deferred-context/command-list correlation,
persistent cross-session generation history, or menu-layer lifecycle records.
The required summary and bridge records are complete, but those later fields
cannot be reconstructed from this file.

## Ordinary Consumer Contract

RC81 recorded 22,214 active ordinary bridge operations:

- 8,455 `kPROJECTEDMENU` operations with draw arguments `6x2`;
- 6,721 `kHUDMENU` operations with draw arguments `168x2`;
- 6,721 `kHUDMENU` operations with draw arguments `144x2`;
- 317 `kHUDMENU` operations with draw arguments `78x2`.

Every observed active ordinary operation:

- occurred inside `renderMode == 24`;
- occurred inside accumulator group 16;
- used `firstPass=1`, `lastPass=1543528565`, and `renderFlags=0`;
- was nested under the exact higher call and direct draw callsite;
- targeted `kMENUBG`;
- had `operation(active=true)`;
- used selector value zero;
- used one RTV, no secondary RTV, and no UAV;
- ran while Render Scale and presentation upscaling were active.

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

- after the first calibration epoch, 211 authoritative group-16 epochs each
  contain exactly three ordered bridge operations;
- the order is projected `6x2`, HUD `504x2`, then HUD `144x2`;
- all three sample complete `2048x2048` projected/HUD source generations and
  target the reduced `3290x1826` `kMENUBG`;
- all 212 projected producer passes and all 212 HUD producer passes use
  color-clear-then-redraw; projected passes contain 7-10 draws and HUD passes
  contain 7-12 draws;
- producer depth testing is disabled. HUD production uses stencil, while the
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

- source and destination format: DXGI format 28 (`R8G8B8A8_UNORM`);
- sample count: 1;
- topology: triangle list;
- one full-destination viewport;
- scissor disabled;
- back-face culling with counter-clockwise front faces;
- no UAV and no secondary RTV;
- depth enabled with `LESS_EQUAL`;
- depth writes disabled;
- stencil disabled;
- color blend: `SRC_ALPHA`, `INV_SRC_ALPHA`, `ADD`;
- engine alpha blend: `ZERO`, `INV_SRC_ALPHA`, `ADD`;
- full RGBA write mask.

The raw trace value for source alpha is 1, which is
`D3D11_BLEND_ZERO`; `D3D11_BLEND_ONE` is 2. The engine state is suitable when
blending into its existing opaque target, but would leave a transparent staging
texture with zero accumulated alpha. Full-resolution layer capture must keep
the observed color equation while deliberately replacing only the source-alpha
factor with `ONE`. The existing `vrMenuLayerCaptureBlendState` already makes
that staging-specific adaptation.

The source dimensions differed:

- `kPROJECTEDMENU`: `1024x1024`;
- `kHUDMENU`: `2048x2048`.

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

- 8,381 full transparent clears with no draw;
- 87 clear-then-redraw passes without stencil;
- 5 clear-then-redraw passes with stencil;
- no copy, resolve, or resource-update path.

Observed `kHUDMENU` passes:

- 5 full-viewport redraws without an observed reset and without stencil;
- 2 full-viewport redraws without an observed reset and with stencil;
- 1 clear-only pass;
- no copy, resolve, or resource-update path.

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

- track producer resources continuously rather than resetting their history
  when a diagnostic/menu session rearms;
- do not require a same-frame producer;
- do not require an observed clear;
- do not reject an otherwise valid consumer because the generation is
  unobserved;
- clear staging lazily at the first authorized operation of a new frame
  transaction, then replay every consumer operation sampling the current
  persistent sources;
- retain the last committed layer when no new eligible consumer transaction
  occurs.

## MainMenu And Loading

RC81 confirms that MainMenu and loading are not ordinary mode-24 consumers.

### MainMenu

The MainMenu session reports 4,315 bridge decisions. Its observed presentation
contract is:

- source: `kPROJECTEDMENU`, `1024x1024`;
- destination: full-resolution `kVR_FRAMEBUFFER`, `4736x2500`;
- draw arguments: `6x2`;
- no recognized accumulator epoch at the direct presentation call;
- Render Scale inactive;
- presentation upscaling inactive.

### Loading

The loading session reports 977 bridge decisions and the same presentation
shape:

- source: `kPROJECTEDMENU`, `1024x1024`;
- destination: full-resolution `kVR_FRAMEBUFFER`, `4736x2500`;
- draw arguments: `6x2`;
- no mode-24 consumer epoch;
- Render Scale inactive;
- presentation upscaling inactive.

After loading, the Render Scale resource plan relatches to the reduced and
display resolutions, and ordinary menus resume with presentation upscaling
active.

MainMenu and loading therefore need no full-resolution bridge adapter unless
keeping Render Scale active in those states becomes a requirement. Keep their
normal engine path, prevent ordinary-layer composition while they are active,
invalidate incompatible committed state on transition, and resume ordinary
eligibility only after the gameplay runtime plan is stable.

## RaceSex Evidence

The focused RC83 New Game log resolves the startup RaceSex presentation path
and the previously unobserved HUD producer. It does not exercise a RaceSex open
after Render Scale has already latched.

### RC83 session integrity and fallback

RaceSex spans sessions 11 through 13 because a `MessageBoxMenu` opens and
closes over it. Together these sessions contain:

- 309 matched `DrawInterface` begin/end passes;
- 924 bridge decisions and 308 complete mode-24 consumer epochs;
- 618 successful OpenVR submissions, one per eye per completed frame;
- no record-cap hit, trace fault, accumulator overflow, or end-argument
  mismatch;
- a clean RaceSex close at frame 3,480 followed by continued gameplay and
  later menu/load activity through frame 4,335.

The Render Scale startup protection is effective in this run:

- Render Scale intent and performance mode remain requested;
- RaceSex opens at frame 3,171 with the Render Scale latch off and protection
  active;
- all 924 RaceSex composition draws report Render Scale and presentation
  upscaling inactive;
- all originals are kept with `higher-filter-context-inactive` and
  `operation-inactive`; no capture, suppression, layer publish, layer lifecycle,
  or final composite occurs;
- the engine targets full-resolution `6024x2996` `kMENUBG` and `kMAIN_COPY`;
- OpenVR receives the original `6024x2996` stereo framebuffer for all 618 eye
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

- 666 matched `DrawInterface` begin/end passes;
- 2,129 dedicated bridge decisions and 666 group-16 mode-24 epochs;
- 1,332 successful OpenVR submissions, one per eye per completed frame;
- no record-cap hit, trace fault, accumulator overflow, dropped resource, or
  failed OpenVR submission;
- a name-change hook at frame 63,457, a clean RaceSex close at frame 63,459,
  and a 60-frame post-close tail through frame 63,519.

The trace-baseline implementation does not satisfy the post-latch fallback
contract:

- RaceSex context records report Render Scale and presentation upscaling
  inactive, and all bridge originals are kept with no capture or final
  composite;
- the bound `kMENUBG`, `kMAIN` depth, and `kVR_FRAMEBUFFER` resources remain at
  the reduced `3290x1826` render extent rather than the `4936x2740` display
  extent;
- OpenVR receives the original reduced `3290x1826` stereo framebuffer on every
  recorded RaceSex submit;
- `latched=yes`, `protection=no`, and `pendingRelatch=no` persist through open,
  close, and the protection tail, so no full-resolution recreation occurs and
  no recovery relatch is needed.

This is a clean trace-baseline implementation failure rather than a tracing or
submission failure. Its RaceSex gate disables presentation-upscaling behavior
logically, but does not atomically replace the already-latched reduced resource
plan with the required full-resolution plan before the first RaceSex frame. The
reviewed HEAD subsequently removes that gate entirely; it still lacks the
required native transition.

### Post-latch draw and producer details

The stable post-latch mode-24 contract contains one projected `6x2`, one HUD
`504x2`, and one HUD `144x2` operation. All 666 instances of each operation
target reduced `3290x1826` `kMENUBG`. Of the 666 epochs:

- 665 have authoritative three-draw generic/direct parity and `unrelated=0`;
- frame 63,445 contains four generic composition draws: two projected `6x2`
  draws followed by HUD `504x2` and HUD `144x2`;
- only three dedicated bridge decisions are emitted for that four-draw epoch,
  so calibration correctly marks it non-authoritative.

The trace also records 131 projected `6x2` bridge operations outside any
accumulator, targeting an unregistered `1024x1024` resource. Together with the
uncalibrated four-draw epoch, these operations show that a selective RaceSex
bridge adapter would require additional exceptional paths. The full-resolution
fallback preserves them without classifying or moving individual draws.

Producer correlation remains complete for the stable mode-24 consumer:

- all 666 RaceSex projected passes clear and redraw the source, totaling
  121,134 depth-disabled draws with per-pass counts from 36 to 192;
- both HUD operations reuse complete generation 9,640, produced in the Console
  session at frame 62,792 by a clear and 11 draws;
- all 1,332 HUD consumptions refer to that unchanged pre-RaceSex generation;
- the 132 exceptional projected consumptions outside the stable mode-24 path
  are incomplete at their observation point and must not authorize capture.

### Post-latch ordinary-layer lifecycle

The Console's ordinary layer, generation 212 from frame 62,792, remains marked
published at RaceSex open, through all RaceSex sessions, and at RaceSex close.
It is not composed while RaceSex is active because final composition is
disabled. The next traced Journal frame stages and publishes generation 213
before either eye consumes a menu layer, so this run contains no stale-layer
consumption.

There is nevertheless no explicit generation-212 invalidation at RaceSex open
or close. Correct fallback entry must invalidate any published ordinary layer;
it must not rely on a later eligible menu producing a replacement before the
old layer can satisfy final-composite preconditions.

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

- each pass begins with a full transparent clear and redraws the full viewport;
- draw counts range from 37 to 180 and total 50,962;
- producer depth is disabled and no producer draw writes depth;
- all 308 consumed generations are complete and have exactly one frame of
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

- 2,013 matched `DrawInterface` begin/end pairs;
- 6,036 dedicated bridge decisions;
- 2,013 projected producer passes;
- 4,026 successful OpenVR submissions, one per eye per frame;
- approximately 193,224 sequential trace records, below the 300,000 cap;
- no trace-fault, accumulator-overflow, or end-argument-mismatch record.

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

- use fixed `2048x2048`, format-28, single-sample sources for this session;
- target the same reduced `2964x1425` `kMENUBG`;
- bind the reduced `kMAIN` DSV;
- use engine vertex/pixel shader IDs 66/66;
- use triangle-list topology and one full-target viewport;
- disable scissoring and stencil;
- enable `LESS_EQUAL` depth testing with depth writes disabled;
- use the same back-face/CCW rasterizer and alpha-blend state;
- occur under the exact higher/direct bridge nesting;
- report selector 128.

Selector 128 happens to be stable here, while valid ordinary consumers in the
other RC81 trace use selector zero. This reinforces that selector is not a
portable ownership rule. The three draw shapes are payload and must be replayed
without shape filtering or resource deduplication.

The consumer epoch redraws the complete RaceSex composition every frame from
the currently bound persistent sources. Producer updates are sparse, but the
consumer is not incremental. This explains why a selective ordinary adapter
could technically replay the three operations, but the selected native fallback
is safer because RaceSex also owns the surrounding mixed scene/UI lifecycle. No
path may clear a committed ordinary layer merely because neither source was
updated in that video frame.

### RaceSex producer behavior

The projected producer is fully characterized for the active session:

- it is cleared to transparent in every one of the 2,013 `DrawInterface`
  passes;
- 1,959 passes are clear-only;
- 54 isolated passes clear and then perform one full-viewport draw;
- producer depth testing is disabled for all 54 draws;
- six of those draws use stencil and are preceded by a stencil clear;
- no projected copy, resolve, or resource update is observed.

The projected generation completes at `DrawInterface` end and is consumed by
the next frame's `6x2` bridge operation. Its resource identity remains stable
through the crash.

The HUD source behaves differently:

- both HUD operations sample the same stable `2048x2048` resource every frame;
- all 4,024 HUD consumptions report `generation=unobserved`;
- no write, clear, copy, resolve, update, producer pass, or global-output pass
  for that HUD resource appears during the RaceSex session;
- the HUD resource observed during the preceding Loading session has a
  different identity and cannot be correlated to the RaceSex HUD source.

Loading closes at frame 7,690 and RaceSex opens at frame 7,736. RC81 has no
targeted session during that 46-frame interval and resets producer history when
RaceSex rearms. The RaceSex HUD may be created or written in that interval, in
an earlier uncorrelated path, or through a context RC81 does not cover. This log
does not distinguish those cases. It does prove that an unobserved generation
can remain a valid, repeatedly consumed source for more than 2,000 frames.

Implementation must therefore treat the exact semantic consumer and bound
source identity as authority. Producer-generation visibility is diagnostic and
useful for invalidation, but cannot gate capture.

### RC81 fallback behavior before the startup guard fix

RaceSex protection activates at menu open and remains active:

- Render Scale intent remains enabled, but RaceSex eligibility is false;
- every bridge decision is kept original with
  `higher-filter-context-inactive`/`operation-inactive`;
- presentation upscaling is inactive for every RaceSex bridge operation;
- no diagnostic full-resolution layer is captured or composed;
- both eyes are submitted through the original path from the reduced
  `2964x1425` stereo framebuffer.

This proves the fallback avoids CS bridge suppression and final composition. It
does not prove the proposed full-resolution RaceSex adapter, because that path
never runs in this trace.

### Race-change crash correlation

The user-reported trigger is changing race from Nord. RC81 does not record
RaceMenu actions, so the exact input frame cannot be identified. The only
exceptional registered-resource operation near the reported change is one
opaque clear of reduced `kMENUBG` inside mode 24 at frame 9,552,
14:25:02.688. Seven later projected redraws occur before the crash. The same
three-operation bridge contract, resource identities, state, and successful
original OpenVR submissions continue through frame 9,748.

The crash report records:

- `EXCEPTION_ACCESS_VIOLATION` in `SkyrimVR.exe+126C830` while reading through
  an invalid pointer;
- a likely Papyrus VM/tasklet thread;
- `RaceMenuPluginXPMSE.OnSliderRequest()` called from
  `racemenubase.OnMenuReinitialized()`;
- `skeletonbeast.nif` as the first relevant object;
- PapyrusTweaks in the probable call stack;
- no Community Shaders frame in the probable or scanned call stack.

Community Shaders is loaded, but the renderer trace reports no fault and
finishes its final recorded frame normally. This does not prove the crash's
root cause or rule out prior memory corruption. It does show no direct evidence
of a CS menu-rendering failure at the crash site, while the immediate stack
context is RaceMenu/XPMSE/Papyrus reinitialization.

### Final RaceSex policy

Do not replace the fallback with a RaceSex bridge adapter. The RC83 startup run
proves that keeping Render Scale unlatched lets Skyrim render the entire mixed
RaceSex scene and UI at full resolution with no CS capture or composition.

The fallback contract is:

1. Block Render Scale activation before the first RaceSex frame.
2. Invalidate any committed ordinary-menu layer and keep every engine draw.
3. Keep presentation upscaling and final menu composition inactive.
4. Preserve the full-resolution plan through RaceSex, overlapping menus, and
   the 60-frame post-close protection tail.
5. Restore Render Scale only after the tail expires and runtime-plan recreation
   completes.

The historical RC83 startup path satisfies this contract. The controlled
post-latch run proves that the trace-baseline implementation does not: it keeps
the latch and reduced resources while merely reporting presentation upscaling
inactive. The reviewed HEAD removes the historical startup blocker as well, so
neither entry case is currently implemented. Fallback entry must force an atomic
full-resolution resource-plan transition before the first RaceSex submit,
preserve that plan across overlap and the close tail, and restore the prior
latched plan afterward. The startup trace records a
name-change hook at frame 3,478 and a clean close at frame 3,480, while the
post-latch trace records its name-change hook at frame 63,457 and clean close at
frame 63,459. Neither log contains an explicit race-selection event; user
confirmation remains necessary if completing a race change must be distinguished
from other RaceSex UI changes. This does not change the fallback decision.

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

- `kPROJECTEDMENU` receives a full transparent clear and no draw;
- `kHUDMENU` receives a full transparent clear followed by 27 full-viewport
  draws;
- the HUD producer draws have depth disabled;
- the completed HUD generation is consumed by the next frame's bridge calls.

Across the final session, the tracer reported:

- 1,907 producer passes;
- 5,721 producer resource observations and 54,515 producer draws;
- 2,668 clear-then-draw resource updates and 3,053 operations-only updates;
- 1,907 global-output passes, 3,814 resources, and 51,489 draws, all inside
  `DrawInterface`;
- no out-of-`DrawInterface` global-output writer; the RC81 aggregate summary
  does not classify every operation well enough to exclude copy, update,
  resolve, dispatch, or command-list paths for all Map states.

The exact per-resource totals cannot be reconstructed from the aggregate
summary alone. The final-frame records directly identify the projected
clear-only generation and the HUD clear-plus-27-draw generation.

### Map mode-24 ordering

Every stable Map frame contains two mode-24 epochs targeting reduced
`kMENUBG`. The semantic bridge operations remain recognizable:

- 230 HUD `24x2` draws with the reduced `kMAIN` DSV;
- 231 HUD `24x2` draws without a DSV;
- nine projected `6x2` draws with `kMAIN` and nine paired projected draws
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

- replaying only the HUD bridge at submit moves it above Map content that the
  engine intentionally draws afterward;
- replaying the whole mode-24 epoch moves depth-writing 3D Map work into the
  menu layer and requires full-resolution scene depth and related resources;
- suppressing only selected original draws cannot preserve destination and
  depth dependencies atomically across the mixed epoch.

Map must fail closed to a full-resolution path: disable Render Scale before the
first Map frame, invalidate any committed ordinary-menu layer, keep all engine
Map draws, and restore the prior Render Scale state only after Map and any
overlapping dependent menu have fully closed. Open, close, MessageBox overlap,
load transition, and runtime-plan changes must share the same lifecycle guard.

The log does not label individual pan, zoom, marker, or local/world actions, so
it cannot map each interaction name to a draw sequence. That mapping is not
needed for the policy decision: the repeatedly observed post-HUD depth-writing
dependency already disqualifies both the narrow adapter and whole-epoch replay.
No additional Map trace is required before implementing the fallback.

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

- the dedicated direct hook increments `directBridge` for every invocation,
  independent of source filtering, candidate acceptance, or suppression;
- `directIssued` counts original bridge draws sent to D3D and
  `directSuppressed` counts operations replaced by the diagnostic replay;
  `genericDirect` and `internalReplay` independently count those two paths in
  the generic hook;
- the D3D hook installer now registers up to eight distinct concrete context
  implementations instead of assuming the first context/vtable covers all
  bridge calls;
- the first epoch in which a new context implementation is registered is
  deliberately uncalibrated because earlier draws in that epoch may have been
  missed;
- each epoch records every observed context/vtable pair, context overflow,
  installation failure, coverage changes, and direct-draw calibration;
- each bridge decision records its context pointer, vtable, concrete
  `DrawIndexedInstanced` target, context type, and current coverage state;
- any exception in generic draw tracing invalidates the current epoch's draw
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

## Updated Ordinary-Menu Pipeline

### 1. Eligibility and preflight

Run only when all of these are true:

- SkyrimVR;
- Render Scale presentation upscaling is active;
- the runtime plan is stable and final size exceeds render size;
- OCU is not providing its own upscaling;
- MainMenu, loading, save/load, Map, RaceSex, CS menu, RenderDoc, device-loss,
  and other existing blockers permit it;
- separate staging and committed resources already exist;
- shaders and immutable states are prewarmed;
- expected formats, sample counts, and stereo dimensions are valid;
- once suppression is enabled, a stable prior complete eye pair is retained for
  desktop failure containment.

No allocation, shader compilation, or resource recreation may occur after a
frame transaction begins. The adapter supports only the immediate D3D11 context
used by the verified bridge path. A different or deferred context is a contract
failure, not a candidate for opportunistic interception.

### 2. Authorize semantic epochs independently of tracing

Use the existing `BSShaderAccumulator::RenderBatches` hook, but split the
current trace-gated API into production begin/end ownership plus an optional
trace observer. Production begin/end must execute whenever the ordinary adapter
is eligible, even when developer logging is disabled. Use a scope guard so an
exception or early return cannot leave the semantic stack active.

An ordinary operation requires:

- `renderMode == 24`;
- the exact higher bridge call nested in that epoch;
- the exact direct draw nested in the higher call;
- a bound `kPROJECTEDMENU` or `kHUDMENU` source;
- `kMENUBG` as the destination;
- an active, stable Render Scale presentation plan.

Use the observed group-16/pass tuple as a diagnostic assertion and fail-closed
validator where appropriate, not as the sole semantic owner.

The semantic state is render-thread-owned and stack-safe. Record epoch ID,
frame, full `RenderBatches` arguments, higher-call depth, direct-call depth,
operation count, and validity. Menu names may block exceptional contexts but
must not authorize ordinary capture.

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

- query source/destination descriptors dynamically;
- preserve IA bindings, shaders, constants, samplers, topology, the observed
  color blend equation, and rasterizer behavior;
- use the staging capture alpha equation `ONE`, `INV_SRC_ALPHA`, `ADD` instead
  of the engine target's `ZERO`, `INV_SRC_ALPHA`, `ADD` equation;
- transform viewport/scissor to the final stereo extent;
- apply the verified depth policy;
- draw into full-resolution staging;
- suppress only the matching reduced `kMENUBG` operation.

Capture into staging first. Suppress the original only when that individual
capture completed successfully and all D3D state was restored. Use scope guards
for the replay-in-progress flag and every acquired COM interface. A failed
capture leaves that original engine draw intact if no earlier operation in the
frame was suppressed.

Do not inspect selector or index count. Do not use PS resource scanning to
discover ownership. After semantic ownership is established, inspect the exact
bound input and destination only to validate the registered
`kPROJECTEDMENU`/`kHUDMENU` to `kMENUBG` contract and obtain the SRV/RTV. Do not
deduplicate by resource or destination. Repeated operations using the same
source and destination remain distinct ordered payload. The Console trace
specifically requires projected `6x2`, HUD `504x2`, and HUD `144x2` to survive
as one transaction.

### 4. Seal, publish, and fail closed

At the first OpenVR eye submit for the frame, require:

- the outermost `DrawInterface` call has ended;
- no semantic, higher-call, direct-call, or internal-replay scope remains open;
- every contributing epoch ended with matching begin/end arguments;
- the runtime-plan and resource generation still match preflight;
- at least one operation was captured, and every suppressed operation is
  represented in staging.

If valid, seal the transaction, swap staging and committed textures, and retain
frame, epoch IDs, runtime-plan generation, source identities, observed producer
generations, operation count, and layer generation. Both eyes must consume that
same committed generation. A second semantic operation after sealing is a
contract fault and must disable the adapter; it must never mutate the published
layer between eye submits.

Preflight alone cannot make an opaque sequence of later draw interceptions
non-fallible. If any failure occurs after the first original draw was suppressed:

1. mark the frame transaction poisoned;
2. discard staging without replacing the committed layer;
3. return success from the compositor hook without forwarding either reduced
   original submit, matching the existing device-loss/reduced-fallback hold
   behavior so OpenVR reprojects the previous complete frame;
4. keep the previous complete desktop eye pair rather than presenting a partial
   or mixed pair;
5. invalidate the adapter contract and request the native-resolution recovery
   path if the failure is structural or repeats.

The current `SubmitVRUpscaledFrame` boolean is not expressive enough for all
three outcomes. Refactor the submit decision to distinguish pass-through,
submit replacement, and hold/reproject. A poisoned menu frame must not return a
generic failure that falls through to `submit("original", ...)`.

The first implementation phase must capture in parallel without suppression.
Enable suppression only after operation parity, scope ordering, and output are
validated. Keep compact assertions around `DrawInterface` end and first submit;
another broad menu-discovery trace is unnecessary.

Do not add a reduced-`kMENUBG` backup/restore path based on RC81's generic
unrelated-draw counter. RC82 repaired the counter, but the evidence still does
not prove that rolling back the complete destination is safe across arbitrary
ordinary menu and mod combinations.

### 5. Reuse and invalidation

Do not clear or invalidate solely because a video frame advanced or a trace
session rearmed.

Reuse the committed layer while its presentation contract remains current.
Stop composing or invalidate on:

- transition into MainMenu, loading, Map, RaceSex, save/load, or another
  exceptional policy;
- leaving eligible ordinary presentation without a compatible successor;
- source resource replacement or incompatible descriptor change;
- Render Scale/runtime-plan generation change;
- device loss or resource rebuild;
- a poisoned frame or semantic/hook contract failure.

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

- source: `ONE`;
- destination: `INV_SRC_ALPHA`;
- operation: `ADD`.

Using `SRC_ALPHA` again at final composition would multiply edge alpha twice
and blur or darken glyph edges.

### 7. Desktop mirror composition

The desktop mirror must receive the same completed ordinary-menu transaction
when CS suppresses any reduced engine bridge operation. This is a correctness
requirement, not merely the optional Render Scale mirror-quality enhancement.
Leaving the desktop path unchanged after suppression produces an HMD-only menu
by construction.

Reuse the final per-eye outputs after `ApplyKnownGameMenuFinalComposite`; do not
replay menu bridge operations a second time for the monitor. Track the frame,
runtime-plan generation, committed menu-layer generation, and readiness of both
eye outputs. Immediately before `IDXGISwapChain::Present`:

1. Require both eyes from the same completed frame and contract generation.
2. Acquire and validate the current swap-chain back buffer and descriptor.
3. Blit the completed eye outputs using the established desktop layout and
   format-compatible render-target view.
4. Draw the CS desktop overlay afterward so its existing presentation order is
   preserved.
5. If the current frame is complete, advance the retained pair only after both
   eyes match. If a suppressed frame is poisoned or held, actively blit the
   retained complete pair so the engine's partial back buffer cannot leak to the
   monitor. Never present one new eye with one stale eye.

The retained pair must reference storage that cannot be overwritten by the next
submit-stage dispatch before Present consumes it. Validate the current hook
ordering in the focused diagnostic. If existing output texture lifetime is not
stable across the hold case, rotate or copy into dedicated pair storage. Do not
store generation metadata beside mutable textures and assume that makes their
pixels persistent.

The existing post-submit writeback to the OpenVR source texture may remain as a
quality optimization, but it is not a sufficient ownership boundary: the game
may already have copied that texture to its desktop target, and the current
trace does not establish otherwise. Present-time back-buffer composition makes
the destination explicit and avoids depending on undocumented engine copy
timing. It should run automatically only on frames where ordinary bridge draws
were suppressed; the existing user setting can continue to control enhanced
mirror blits on other Render Scale frames.

Add compact developer diagnostics for the final eye-ready markers, source and
back-buffer identities and dimensions, layer/contract generations, selected
mirror path, blit result, and sequence relative to both OpenVR submits and
`IDXGISwapChain::Present`. This requires targeted implementation validation,
not another broad menu-discovery trace.

## Exceptional Native-Resolution State Machine

Map and RaceSex require one shared transition controller. Do not change the
user's Render Scale setting to enter it. Preserve the requested method, quality,
Render Scale intent, and active boot snapshot, then apply a transient
forced-native policy to resource planning.

Use overlap-aware reason bits and explicit states:

| State | Required behavior |
| --- | --- |
| `ScaledGameplay` | Normal reduced scene and ordinary semantic adapter |
| `EnteringNativeMenu` | Invalidate ordinary layer, keep engine menu draws, request native recreation, hold reduced submits and the last complete mirror pair |
| `NativeMenu` | Require engine menu/depth/framebuffer descriptors to match the final display plan and submit the original full-size path |
| `NativeTail` | Stay native until all exceptional reasons and dependent overlaps close; RaceSex retains its verified 60-frame tail |
| `RelatchingScaled` | Restore the captured Render Scale intent through the existing recreation scheduler and hold mismatched submits until the reduced plan and submit-stage outputs are stable |

At minimum the reason mask contains Map and RaceSex. MainMenu, loading,
save/load, MessageBox overlap, runtime-plan changes, and device loss must
participate in transition safety even where their existing fallback remains the
owner. Closing one menu cannot relatch while another native reason is active.

On startup RaceSex, defer the initial Render Scale latch and enter native mode
directly. On post-latch Map or RaceSex, queue a genuine render-target recreation
before accepting another presented frame. Add a dedicated internal transition
origin such as `ExceptionalMenu`; do not route this through public setting
changes. Entry is complete only after the current engine render targets, depth
target, VR framebuffer, resolution plan, and protected-submit classification all
agree on native dimensions. Logical `presentationUpscalingActive=false` is not
an entry condition.

If menu-open notification arrives too late to recreate before that frame's
rendering, hold its OpenVR submits and desktop update until native descriptors
are observed. Do not present one reduced transitional frame. On close, restore
the exact captured plan generation only after the reason mask and tail expire.
If user settings change while native mode is active, merge the latest requested
intent into the relatch target rather than restoring a stale snapshot.

The transition controller must be idempotent under duplicate menu events and
must survive event/UI-state disagreement. Poll the authoritative UI state as a
reconciliation path, but log disagreement and state changes once per transition
instead of every frame.

## Resource And Performance Budget

The hybrid avoids resizing the broad Skyrim pipeline, but atomic publication is
not free. At the observed `4936x2740` stereo output, one full stereo texture is
about 51.6 MiB in 32-bit RGBA or 103.2 MiB in 64-bit RGBA. Staging plus committed
layers therefore cost about 103.2 MiB or 206.4 MiB. A dedicated retained eye
pair adds another stereo texture of the same total pixel count. A full-size
depth target, if selected, adds its own allocation.

Use the actual traced menu/output formats, not a hard-coded assumption, and log
the calculated budget once when the plan changes. Reuse existing stable eye
outputs for Present only if lifetime validation proves they are not overwritten.
Otherwise allocate a bounded double-buffer/ring and include it in Render Scale
eligibility. Allocation failure must select native recovery before suppression;
it must not degrade into partial capture.

## Depth Policy

RC81 proves that every observed ordinary bridge operation binds a reduced-size
`kMAIN` DSV, enables `LESS_EQUAL`, disables depth writes, and disables stencil.

It does not prove whether the scene-depth comparison materially clips ordinary
menu pixels. The reduced DSV cannot be bound with a larger final-resolution
staging RTV.

Before release, compare these policies visually in diagnostic parallel mode:

1. a final-resolution DSV cleared to far depth;
2. depth disabled for staging replay;
3. a depth-preserving design only if ordinary projected UI demonstrably needs
   scene occlusion.

The comparison must include projected HUD, hands/controllers crossing menu
geometry, and transitions. Map and other world-dependent UI must not inherit an
ordinary depth policy without their own evidence.

## Remaining Verification

Do not repeat the broad ordinary-menu, focused Map, or startup RaceSex runs.
Their architectural decisions are complete. The controlled RaceSex open after
Render Scale was already active is also complete and exposes a reduced-resource
fallback failure. Remaining work is implementation and post-fix validation.

### Final targeted instrumentation

The RC83 diagnostic build closes the known logging gaps:

- Map records every hooked D3D11 draw, including draws unrelated to registered
  menu resources, both inside and outside accumulator epochs.
- Each Map draw records a session ordinal, epoch-local ordinal, draw arguments,
  relationship to the bridge, all bound PS resources, all graphics/compute
  shader identities, engine shader IDs, IA layout/index/vertex buffers, stream
  output, render targets, DSV, viewports, scissors, topology, blend, rasterizer,
  depth/stencil, OM UAVs, CS UAVs, context identity, vtable, and context type.
- Map also records dispatches, copies, updates, resolves, render-target/depth/UAV
  clears, structure-count copies, mip generation, and command-list finish and
  execute events in the same global sequence.
- `CreateDeferredContext` is intercepted while developer tracing is enabled so
  newly created deferred contexts receive the same draw and operation hooks.
  Command-list identity correlates deferred recording with immediate execution.
- Session summaries report Map draw totals inside/outside accumulators,
  unrelated draw totals, resource-operation totals, hook-bank coverage, and
  deferred-context hook coverage.
- Producer-generation history is no longer reset when a trace session rearms.
  When the game-load notification is received, it resets at that boundary so
  Loading, pre-open, and RaceSex records share one generation ledger. The
  focused RC83 run retained continuous cross-session history even though its
  game-load notification was absent.
- A new-game-only `RaceSexPreRoll` session is designed to start when Loading
  closes and last for at most 180 frames or until RaceSex opens. It did not arm
  in the focused RC83 run because `loadKind` remained unknown; the preceding
  traced session captured the required HUD producer instead.
- Every captured full-resolution layer now has a trace generation, source
  generation, source/destination identity, accumulator epoch, frame, operation
  count, runtime-plan/contract generation, publish point, per-eye consumption,
  and invalidation reason. Existing bridge decisions retain all preflight and
  rejection reasons.
- Required records continue after the 300,000 retained-record counter saturates,
  and every required record is flushed immediately.

These records are sufficient to decide semantic ownership, exceptional-menu
fallbacks, RaceSex producer/reuse behavior, transaction boundaries, and stale
layer invalidation. Logs cannot establish pixel equivalence, glyph-edge alpha
quality, or the visually correct depth policy; those remain explicit
diagnostic-parallel visual acceptance checks.

### Map

Satisfied by the focused RC83 Nvidia/OpenComposite log. It contains complete
`MapMenu` sessions, `map-draw` and `map-resource-operation` records,
authoritative mode-24 accumulator parity, bridge and final-composite decisions,
and successful OpenVR submits. It proves the mixed post-HUD depth-writing
dependency and selects the full-resolution fallback. No further Map logging is
required for that implementation decision.

### RaceSex

Satisfied for the startup fallback by the focused RC83 log. It provides the
pre-open persistent HUD writer, continuous source generations, authoritative
three-draw epoch parity, full-resolution targets, original submits, clean menu
close, protection tail, and successful Render Scale relatch. No layer lifecycle
is expected because the selected fallback intentionally never stages a layer.

The controlled post-latch run is complete and fails: all 1,332 successful
RaceSex eye submits use the reduced `3290x1826` framebuffer even though RaceSex
context flags report Render Scale and presentation upscaling inactive. After
fixing fallback entry, repeat this narrow case and confirm that no reduced
RaceSex frame is presented, no stale ordinary layer survives, overlapping
menus remain full resolution, and relatch occurs only after close and tail
completion. Add an explicit test marker or user confirmation if race-change
completion itself must be distinguished from other RaceSex UI changes.

## SOL Implementation Handover

This section is the implementation contract for an agent working on another
machine. The trace logs are not required to begin; the relevant conclusions and
measured contracts are preserved above. Work from branch `cs-1.7-PL-VR` at or
after reviewed head `0c66c97b8`, and reconcile any newer changes before editing.

### Current implementation gaps

- `BeginVRMenuFinalCompositeFrame` invalidates the layer and resets operation
  state every frame.
- There is one live `vrMenuFinalCompositeLayer`; staging and committed content
  are not independent.
- The direct path still uses draw-shape filtering, explicit menu contexts,
  resource scanning as discovery, target deduplication, and adaptive index
  counts. This is the path that rejects Console's third valid operation.
- `TryMakeVRMenuBridgeHigherFilterContext` still requires selector `0x80`, even
  though RC81 proves selector zero is valid.
- `BeginVRMenuAccumulatorTrace`/`EndVRMenuAccumulatorTrace` own the available
  semantic stack only while developer tracing is active.
- `SubmitVRUpscaledFrame` returns a boolean, so an unclassified failure can fall
  through to the original reduced OpenVR submit.
- Reviewed HEAD `0c66c97b8` removed RaceSex render-scale blocking without adding
  native resource transition ownership.

Treat the existing capture as diagnostic scaffolding, not as a base whose
candidate rules should be extended with more menu-specific cases.

### Required production state

Keep production state separate from the large developer tracer:

| State object | Minimum contents and ownership |
| --- | --- |
| `VRMenuSemanticEpoch` | Render-thread stack entry: epoch ID, frame, full `RenderBatches` arguments, eligible flag, validity, higher/direct depths, operation count |
| `VRMenuFrameTransaction` | Render-thread state: frame, plan generation, staging generation, contributing epoch IDs, captured/suppressed counts, building/sealed/poisoned state, failure reason |
| `VRMenuCommittedLayer` | Staging-independent texture/SRV, dimensions/format, layer generation, source frame, plan generation, operation count, valid bit |
| `VRPresentedEyePair` | Stable per-eye final storage or proven-lifetime references, frame, plan/layer generation and ready bits; retain only a complete matching pair for desktop fallback |
| `VRNativeMenuTransition` | Exceptional state, reason mask, entry generation, captured user intent/boot snapshot, latest requested intent, tail deadline, resource-readiness state |

The render thread owns draw-path structures without a mutex. Menu events may
set atomic reason inputs, but the render thread reconciles them into one ordered
transition state. Never hold a mutex or allocate memory in the intercepted draw
hot path.

### Code touchpoints

- `src/FrameAnnotations.cpp`, `BSShaderAccumulator_RenderBatches::thunk`:
  introduce unconditional production semantic begin/end around the original
  call. Preserve the existing trace calls as optional observers and use a scope
  guard for balanced end handling.
- `src/Features/Upscaling.h`: replace the single live-layer fields with the
  state objects above. Add explicit adapter and native-transition methods. Add a
  submit disposition that can represent pass-through, replacement, and hold.
- `src/Features/Upscaling.cpp`, `BeginVRMenuFinalCompositeFrame`,
  `EnsureVRMenuFinalCompositeLayer`, `TryCaptureAndSuppressVRMenuBridgeDraw`,
  `ShouldTraceVRMenuBridgeDirectDrawCandidate`, and the higher/direct hooks:
  convert the current discovery/dedup implementation into semantic frame
  transport and double buffering. Keep trace formatting outside production
  decisions.
- `src/Features/Upscaling.cpp`, `SubmitVRUpscaledFrame` and
  `ApplyKnownGameMenuFinalComposite`: seal/publish before first eye use, require
  both eyes to consume one layer generation, and expose the completed eye pair
  to the desktop path.
- `src/Features/Upscaling.cpp`, existing boot-latch/recreation code around
  `PerfModeState::BootSnapshot`, `RequestPerfModeRenderTargetRecreate`, and
  `ApplyPendingPerfModeRenderTargetRecreate`: add the transient exceptional-menu
  native policy without changing the public Render Scale request.
- `src/Features/VR/InSceneOverlay.cpp`, `IVRCompositor_Submit::thunk`: consume
  the explicit submit disposition. A poisoned or transitioning reduced frame
  returns `VRCompositorError_None` without calling the original compositor
  submit, as the existing device-loss and reduced-fallback guards already do.
- `src/Hooks.cpp`, `IDXGISwapChain_Present::thunk`: blit a complete matching eye
  pair to the acquired back buffer before `menu->DrawOverlay()`. Preserve the CS
  overlay's current final ordering.
- `features/Upscaling/Shaders/Upscaling/VRMenuLayerCompositePS.hlsl` and
  `VRDesktopMirrorBlitPS.hlsl`: reuse the existing shaders unless diagnostic
  alpha/depth comparison proves a shader change is necessary.

### Ordinary-frame control flow

1. Reconcile exceptional state and preflight resources before rendering.
2. On each accumulator begin, push production semantic state; attach trace state
   only when developer logging is enabled.
3. On the exact higher call, mark only the current eligible epoch as nested.
   Do not require selector `0x80`.
4. On the exact direct call, require semantic/higher nesting, validate the bound
   registered source and `kMENUBG` destination, capture into staging, restore all
   D3D state, then suppress that original operation.
5. At accumulator end, validate stack parity and append the epoch result to the
   frame transaction. Do not publish or clear the committed layer there.
6. At outer `DrawInterface` end, mark the transaction render-complete. At first
   eye submit, require no active scope, seal and swap the valid staging layer,
   then composite one immutable committed generation into both eye outputs.
7. After both eyes are ready, publish the pair for the next matching Present.
   Never expose a one-eye or cross-generation pair.
8. On ordinary context exit with no compatible successor, runtime-plan change,
   exceptional entry, resource replacement, or failure, invalidate explicitly
   with a recorded reason.

### Failure and transition invariants

- Before any suppression, an operation failure keeps the engine draw and aborts
  the adapter attempt without changing the committed layer.
- After any suppression, every later frame failure poisons the transaction and
  holds both OpenVR submits. It cannot fall back to the original reduced texture.
- The staging texture is never sampled by submit or Present. Only a sealed
  committed texture may be sampled.
- The committed texture is never cleared or mutated in place. Publication is a
  staging/committed swap.
- A plan/resource generation mismatch prevents publication and eye reuse.
- OCU-provided upscaling continues to block CS Render Scale and this adapter.
- Missing hook signature, unsupported context, device loss, or repeated
  semantic contract failure makes the adapter unavailable and enters native
  recovery; it must not silently restore reduced blurry menus.
- Exceptional entry preserves user intent. `SetVRRenderScaleModeRequested(false)`
  is not an acceptable implementation because it changes public configuration.
- A Map/RaceSex frame is presentable only after native target descriptors are
  observed. A logical inactive flag with reduced descriptors is a failure.

### Remove after diagnostic parity

Delete these from production correctness once parallel validation passes:

- `kVRMenuBridgeHigherExpectedSelector` and selector-based rejection;
- fixed and adaptive index-count candidate tables;
- menu-name authorization and target-class early termination;
- runtime resource/destination deduplication;
- frame-advance invalidation of the committed layer;
- `all-menu-targets-already-suppressed` behavior;
- producer-generation gating.

Keep source/destination identity, producer generation, index count, selector,
and menu mask only as diagnostic fields. Keep exceptional menu names only in the
native-transition reason policy.

### Focused implementation diagnostics

Log one compact record per state transition or fault, plus sampled developer
records for:

- semantic/frame/layer/plan generation IDs and operation parity;
- `DrawInterface` end to first-submit ordering;
- staging publish, reuse, invalidation, and poison reason;
- per-eye generation and complete-pair publication;
- Present back-buffer identity, dimensions, format, pair generation, and result;
- exceptional reason mask, transition state, requested/latched plan, actual
  resource dimensions, submit disposition, and relatch result.

Do not restore the broad per-draw tracer as a release-time dependency. It may
remain behind developer logging for regression investigation.

### Implementation completion criteria

The implementation is complete only when ordinary transport, desktop mirroring,
and exceptional transitions are one coherent lifecycle. A partial delivery that
improves the HMD while leaving Console absent on the monitor, or that marks
RaceSex inactive while retaining reduced targets, does not satisfy this design.

## Acceptance Gates

### Satisfied for diagnostic ordinary implementation

- Observed ordinary consumers share one semantic mode-24 contract.
- Exact higher/direct bridge nesting is stable.
- Selector zero is valid and selector filtering is disproven.
- Multiple distinct index shapes are valid and index filtering is disproven.
- Every operation can be transported in original order without deduplication.
- Console proves that destination/resource deduplication currently omits one
  valid HUD operation and produces a mixed-resolution, reordered result.
- Complete blend, rasterizer, depth/stencil, viewport, target, format, and UAV
  state is known.
- Source dimensions are known to vary and can be queried dynamically.
- Producer/consumer ordering and persistent HUD reuse are understood.
- MainMenu and loading have a proven full-resolution fallback with Render
  Scale inactive.
- Map has a documented full-resolution fallback contract based on complete
  RC83 mixed-epoch evidence.
- Startup RaceSex has a proven full-resolution fallback, persistent HUD source,
  one-frame projected producer chain, clean close tail, and successful relatch.

### Still required before release

- Diagnostic parallel output matches the engine bridge for ordinary menus.
- A safe ordinary depth policy is visually verified.
- A failure after suppression holds the frame and cannot submit or mirror a
  partial result; a failure before suppression preserves the original draw.
- Final premultiplied-alpha composition is verified on glyph edges.
- Console presents all three observed operations at final resolution in engine
  order, with no `all-menu-targets-already-suppressed` rejection, and its HMD
  text is visually compared before and after the fix.
- When an ordinary menu transaction suppresses reduced bridge draws, the same
  completed per-eye result is visible in the desktop game window without
  enabling the optional mirror-quality setting. Validate Console specifically
  and confirm that no stale, mixed-eye, duplicated, or one-frame-late menu is
  presented.
- The Map fallback disables Render Scale across its complete lifecycle and is
  visually verified through open/close, MessageBox overlap, and load/runtime-
  plan transitions.
- Fix the demonstrated post-latch RaceSex failure so opening RaceSex transitions
  atomically from the reduced `3290x1826` plan to the `4936x2740`
  full-resolution fallback before its first submit, then verify close and
  relatch.
- Menu and runtime-plan transitions cannot publish a stale committed layer.
- The result is validated on materially different modlists and SteamVR/OCU
  configurations where OCU upscaling is disabled.

## Implementation Phases

1. Separate production semantic epochs from tracing and add frame transaction,
   staging/committed, eye-pair, and exceptional-transition state without
   changing rendering.
2. Add ordered full-resolution transport in diagnostic parallel mode; retain
   original reduced bridge draws and compare operation parity/output.
3. Implement submit disposition, poison handling, double-buffer publication,
   premultiplied final composition, and Present-time complete-pair mirroring.
4. Validate alpha, glyph edges, scope ordering, Console's three operations, and
   depth-policy variants before enabling suppression.
5. Enable fail-closed suppression for verified ordinary frame transactions and
   remove discovery, deduplication, and target-class early termination.
6. Implement the shared Map/RaceSex native state machine, including startup,
   post-latch entry, overlaps, tails, user changes during native mode, and stable
   relatch.
7. Run focused fault injection for allocation/device/hook/plan failures and
   prove no reduced or partial frame escapes after suppression or transition.
8. Run the menu/modlist/runtime validation matrix on SteamVR and OCU with OCU
   upscaling disabled; separately confirm the existing OCU-upscaling blocker.

## Non-Goals

Do not port Open Shaders' complete Performance Mode, broad render-target
resizing, `kTOTAL` replacement, tonemap interception, or earlier upscaler
placement. Do not resize the broad Skyrim scene pipeline. The intended change
is limited to semantic menu ownership, transaction-safe bridge transport, and
submit-stage full-resolution composition.
