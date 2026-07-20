# SkyrimVR Full-Resolution Menu Presentation Strategy

Status: RC81 ordinary-menu and crashing RaceSex evidence is incorporated;
RC82 draw accounting is corrected; and the focused RC83 Map and RaceSex
evidence is complete. Ordinary-menu implementation may proceed in diagnostic
parallel mode. The RC83 streams resolve Map and startup RaceSex to full-
resolution Render Scale fallbacks rather than submit-stage adapters. Opening
RaceSex after Render Scale is already latched remains an implementation and
validation requirement.

Branch: `cs-1.7-PL-VR`

Baseline: `87522b1f47ef5544b92962cdd11177db8e746ea8` (`RC83`)

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

This document is an untracked design and verification record. It is not a
release claim.

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
4. The layer is committed only at the consumer epoch boundary and reused until
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
RaceSex must also use a full-resolution fallback. RC83 proves the startup guard
keeps Render Scale unlatched from menu open through its post-close tail, while
the engine performs the complete RaceSex composition into full-resolution
targets. No RaceSex bridge replay is needed. A later RaceSex invocation must
receive the same contract even when Render Scale was already active before the
menu opened.

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
- clear staging at the beginning of a new semantic consumer transaction, then
  replay every consumer operation sampling the current persistent sources;
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
consumer is not incremental. A staging transaction may therefore clear once at
consumer start and replay all three operations atomically every epoch; it must
not clear a committed layer merely because neither source was updated in that
video frame.

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

The startup path satisfies this contract. The current startup bypass returns
false once Render Scale is already latched, so a later RaceSex open is not
proven to transition atomically to the same full-resolution fallback. That case
must be implemented or explicitly guarded before claiming general RaceSex
coverage. The trace records a name-change hook at frame 3,478 and a clean close
at frame 3,480, but it has no explicit race-selection event; it therefore does
not prove that a race change occurred. Neither limitation changes the fallback
decision.

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

This implementation still requires runtime verification. A successful trace
must show that the first newly discovered context epoch is uncalibrated, later
matching semantic epochs become authoritative, and their direct issued/generic
counts agree. Until that evidence exists, do not use RC81 or post-RC81 zero
unrelated counts to justify whole-epoch suppression, texture rollback, or
restoration.

## Updated Ordinary-Menu Pipeline

### 1. Eligibility and preflight

Run only when all of these are true:

- SkyrimVR;
- Render Scale presentation upscaling is active;
- the runtime plan is stable and final size exceeds render size;
- OCU is not providing its own upscaling;
- MainMenu, loading, save/load, Map, RaceSex, CS menu, RenderDoc, device-loss,
  and other existing blockers permit it;
- staging and committed resources already exist;
- shaders and immutable states are prewarmed;
- expected formats, sample counts, and stereo dimensions are valid.

No allocation, shader compilation, or resource recreation may occur after a
consumer transaction begins.

### 2. Authorize a semantic transaction

Use the existing `BSShaderAccumulator::RenderBatches` hook.

An ordinary transaction requires:

- `renderMode == 24`;
- the exact higher bridge call nested in that epoch;
- the exact direct draw nested in the higher call;
- a bound `kPROJECTEDMENU` or `kHUDMENU` source;
- `kMENUBG` as the destination;
- an active, stable Render Scale presentation plan.

Use the observed group-16/pass tuple as a diagnostic assertion and fail-closed
validator where appropriate, not as the sole semantic owner.

Start staging lazily on the first verified operation. Do not clear merely
because an arbitrary mode-24 epoch began.

### 3. Transport every bridge operation

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

Do not inspect selector or index count. Do not scan for ownership. Do not
deduplicate by resource. Operation count and geometry remain opaque payload.

### 4. Commit atomically

At consumer epoch end:

- commit only when at least one verified operation was transported and the
  transaction remained valid;
- swap staging and committed textures rather than copying them;
- retain epoch ID, frame, runtime-plan generation, source identities, and any
  observed producer generations for diagnostics;
- discard invalid staging without publishing a partial layer.

Suppression is safe only after preflight makes operation transport non-fallible
for the duration of the epoch. The first implementation phase must capture in
parallel without suppression. Enable suppression only after operation parity
and output are validated.

Do not add a reduced-`kMENUBG` backup/restore path based on RC81's generic
unrelated-draw counter. That counter is currently incomplete.

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
- aborted semantic transaction after capture began.

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
Their architectural decisions are complete. Remaining work is implementation
and post-change validation, including a controlled RaceSex open after Render
Scale is already active.

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

After implementing general RaceSex fallback entry, validate a RaceSex open
while Render Scale is already latched. Confirm that no reduced RaceSex frame is
presented, no stale ordinary layer survives, overlapping menus remain full
resolution, and relatch occurs only after close and tail completion. Add an
explicit test marker or user confirmation if race-change completion itself must
be distinguished from other RaceSex UI changes.

## Acceptance Gates

### Satisfied for diagnostic ordinary implementation

- Observed ordinary consumers share one semantic mode-24 contract.
- Exact higher/direct bridge nesting is stable.
- Selector zero is valid and selector filtering is disproven.
- Multiple distinct index shapes are valid and index filtering is disproven.
- Every operation can be transported in original order without deduplication.
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
- Preflight guarantees no partial suppression.
- Final premultiplied-alpha composition is verified on glyph edges.
- The Map fallback disables Render Scale across its complete lifecycle and is
  visually verified through open/close, MessageBox overlap, and load/runtime-
  plan transitions.
- RaceSex opened after Render Scale is already latched transitions atomically to
  the same full-resolution fallback and is visually verified through close and
  relatch.
- Menu and runtime-plan transitions cannot publish a stale committed layer.
- The result is validated on materially different modlists and SteamVR/OCU
  configurations where OCU upscaling is disabled.

## Implementation Phases

1. Refactor semantic epoch and persistent source-generation state without
   changing rendering.
2. Add generic full-resolution staging transport in diagnostic parallel mode;
   keep original reduced bridge draws.
3. Validate operation count/order, output identity, alpha, transitions, and
   depth-policy variants.
4. Enable fail-closed all-or-nothing suppression for verified ordinary epochs.
5. Apply premultiplied final composition and validate glyph/text edges.
6. Implement the Map full-resolution fallback and extend the proven startup
   RaceSex fallback to RaceSex opens after Render Scale is already latched.
7. Remove selector scanning, adaptive index tables, menu whitelists, target
   early termination, and resource deduplication after semantic parity is
   proven.
8. Run the menu/modlist/runtime validation matrix.

## Non-Goals

Do not port Open Shaders' complete Performance Mode, broad render-target
resizing, `kTOTAL` replacement, tonemap interception, or earlier upscaler
placement. Do not resize the broad Skyrim scene pipeline. The intended change
is limited to semantic menu ownership, transaction-safe bridge transport, and
submit-stage full-resolution composition.
