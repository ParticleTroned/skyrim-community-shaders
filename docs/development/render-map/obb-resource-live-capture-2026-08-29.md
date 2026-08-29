# OBB resource and visibility live capture — 2026-08-29

## Purpose

This run joins the statically mapped Skyrim VR
`BSOBBOcclusionTestingShader` lifecycle to concrete D3D11 resources, views,
bindings, draw submission, result publication, GPU-to-staging copies, reset
ordering, visibility consumers, and nonblocking CPU readback decisions in a
loaded Breezehome scene.

The capture proves runtime identities only for this process and capture
generation. Raw pointers are retained as evidence but are not stable IDs.

## Exact runtime identity

- Skyrim VR: `1.4.15.0`, process `SkyrimVR.exe`.
- CSX source commit: `b223489b6b2afb5441409b3e17e826ff964efb50`
  (`v3.19.0-pr5-6-gb223489b6-dirty`).
- CSX build ID:
  `aa2f9b2040fb72d9e1404fd07f7913d6cb7b6fd69e65e0c6e4ec1e9ef49db305`.
- Built DLL SHA-256:
  `58eca2412d5f18398dec265390eeab2eba6bac0cb43e25262f2fedbe10b255d6`.
- Shader-cache ABI:
  `85f71f8f713542badba1027e6216e07f42a762782165d185f004e4c14af1d5c6`.
- Compiler identity: `d3dcompiler_47.dll:10.0.26100.9168`.
- MO2 profile:
  `Codex Task - 20260829t025250z-obb-resource-join-be0ece42`.
- Scene: the verified Breezehome interior fixture copied from the stable base
  profile.
- Runtime route: verified Valve null HMD with the Virtual Desktop display
  redirector isolated for the session.
- Managed shader cache: five packed files, `543226223` bytes, tree SHA-256
  `D1E8E9AA7ED685BE5C2B258BCE3F1CBC68F93797920776877AE6DE99EFACD934`.
  Runtime logging reported `startup-validation result=reuse
  reason=contracts-match`; no shader compilation occurred.

The canonical `CSmain` build receipts are:

- configure:
  `L:/Codex/logs/managed-process/20260829-034945.668-csx-render-system-map-configure-prebuilt/receipt.json`;
- build:
  `L:/Codex/logs/managed-process/20260829-035007.358-csx-render-system-map-build-CSmain-prebuilt/receipt.json`.

## Capture progression

The Breezehome frame contains more than 20,000 observed calls. A small capture
therefore saturates within one CPU frame. The run deliberately increased the
event budget while reducing identity-catalogue budgets, retaining every
intermediate result instead of treating the final trace as the only evidence.

| Capture | Accepted events | CPU frames represented | Important result |
|---|---:|---|---|
| `capture-live-0000e2df588df124-2` | 8,192 | one partial frame | First exact OBB shader/resource/draw/result chain; only two event drops and no catalogue drops. |
| `capture-live-0000e33717368f30-3` | 20,000 | `46621` | Larger one-frame fragment with no catalogue drops. |
| `capture-live-0000e348d0eb0288-4` | 60,000 | `49015`–`49017` | First frame-spanning command stream; two result publications and two result-reset copies. |
| `capture-live-0000e3919eb04500-5` | 60,000 | `58827`–`58829` | Live depth diagnostics enabled; 721 cull decisions join producer frame `58827` to CPU readback in frame `58828`. |

All captures stopped at their event limit and contain an explicit terminal gap.
The 60,000-event captures also exhausted the deliberately small resource/view
catalogues. They establish presence and ordering, but absence from them is not
evidence of absence.

## Exact OBB command chain

The correlated capture's first OBB chain is sequences `904` through `924`:

1. Sequence `904` observes `fxpFilename=OBBOcclusionTesting`.
2. Sequence `905` enters descriptor pair `0/0` from caller RVA `0x1355A88`,
   within the statically mapped `SetupTechnique` body at `0x1355A70`.
3. Sequences `906`–`908` resolve both stage objects through the `engine` route,
   not the CSX cache route.
4. Sequence `910` binds a depth-only output-merger state. Its capture-local
   depth view is `obs-depth-target-264-g4`; no colour targets are present.
5. Sequences `911`–`913` observe and bind the result resource:
   a 16,384-byte default structured buffer, 4-byte stride, SRV+UAV bind flags,
   with a 4,096-element UAV at output-merger slot 0.
6. Sequences `914`–`917` observe and bind the transformed-OBB input:
   a 262,144-byte dynamic CPU-write structured buffer, 64-byte stride, with a
   4,096-element SRV at pixel and vertex slot 0.
7. Sequence `919` submits one `DrawIndexedInstanced` with 36 indices and 1,588
   instances on the typed immediate context. The subsequent result-ready event
   reports 794 objects, so the live submission is exactly `2 * objectCount`,
   confirming the statically mapped draw-stereo path.
8. Sequences `920`–`921` observe the 16,384-byte staging buffer and copy the
   result primary into it. This is the live `RestoreTechnique` staging copy.
9. Sequences `922`–`924` version the primary result and publish
   `obs-resource-version-276-g4` with readiness domain
   `same-immediate-context-order`.

A second diagnostic staging copy follows at sequences `925`–`926`; it belongs
to the explicitly enabled nonblocking diagnostic readback ring rather than the
engine's `+0x100` staging companion.

The same capture records copies into the primary result resource at sequences
`10633` and `57633`, after publication in frames `58827` and `58828`
respectively. Their sources could not receive capture-local observation IDs
after the reduced resource catalogue filled. Static disassembly independently
fixes this copy as `+0x108` zero-initialized reset source to `+0x100` primary;
the live evidence confirms the destination and order, while the source identity
remains a static/runtime correlation rather than a fully observed live join.

## Visibility consumption and readback

With `communityshaders.depth_culling_diagnostics` collecting in `live` mode:

- producer frame `58827` published one result for 794 OBB objects;
- 721 lighting objects consumed that resource version;
- frame `58828` emitted exactly 721 `cull-decision` events for the same version;
- 489 covered objects were visible and 232 were occluded;
- every decision carried `readinessDomain=cpu-readback-complete`;
- distant-tree and grass draws were not represented in this interior slice.

The bounded diagnostic interval covered 132 engine frames. It queued 132 GPU
copies, completed 131 readbacks, dropped none, and recorded no map-not-ready or
readback errors. Across those samples, 104,014 objects were read: 30,574
occluded and 73,440 visible. This is structural evidence for the lifecycle, not
a performance verdict: capture load, the null-HMD route, compositor pacing, and
the wider test configuration are not a controlled performance baseline.

## Derived graph

The graph derived from the correlated capture has:

- 47,690 nodes;
- 172,119 edges;
- 1,588 decision windows;
- zero ambiguities;
- 14,254 gaps, of which one is blocking because the source capture is
  truncated.

`render-graph.json` SHA-256 is
`7D5E407E6B290FC4F5102BB4C14A8CC6D6BB1D158D2B031D07442FEF066D2854`.
The capture event SHA-256 is
`6AAFB2D98BB3B2331CFB10BBDDFBC2AE81537EF1F37BEB6C6AAEEC84BE6EA5E1`;
its manifest SHA-256 is
`F340746EA1BAA9EDD27FCD54B82AE98A7B85EDAE65F61064F94C5AAC4A14C78D`.

The retained evidence root is:

`L:/Codex/evidence/render-map/20260829-obb-resource-live-join/`

## Remaining honest gaps

1. No `eye-submitted` event appeared in this loaded-scene slice. A subsequent
   selector-isolated main-menu capture validates the accepted-submit hook under
   the null route and records the shared texture plus exact per-eye bounds; see
   [`eye-submission-live-capture-2026-08-29.md`](./eye-submission-live-capture-2026-08-29.md).
   The remaining gap is the resource-flow join from loaded-scene writes to that
   submitted allocation, plus cross-route validation on physical SteamVR.
2. The retained capture predates direct `Map`/`Unmap` events. Subsequent exact
   disassembly proves the engine uses
   `Map(staging, 0, D3D11_MAP_READ, 0, ...)`; paired with the D3D11 contract,
   successful Map return is therefore the CPU-visibility/completion boundary
   for that staging allocation. Render-event 1.14 now implements bounded CPU
   access observation and measured Map-call duration; a new loaded-scene live
   qualification is still required to close this historical capture's gap.
3. The reset source lacked a live observation ID after catalogue exhaustion;
   its identity is currently an exact static claim correlated with a live copy
   into the exact primary resource.
4. This capture could not fit a complete high-density world frame within its
   then-current aggregate event and identity budgets. Service 1.16 raises the
   bounded envelope to 64 MiB, while selector-isolated captures remain the
   preferred dense-scene method; catalogue persistence/segmentation is still a
   possible later refinement.
5. This capture predates exact view-subresource overlap and post-call D3D11
   effective-state observation. Graph 1.10 and the subsequent zero-loss live
   qualification close that general hazard-state gap; this retained graph must
   not be retroactively described as having those observations.
