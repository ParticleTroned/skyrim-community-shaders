# Material-input execution live capture — 2026-08-29

## Purpose

Qualify the exact join from a loaded-scene Lighting material texture identity,
through ordered immediate-context SRV state, to the draw that consumes it.

## Retained evidence

The complete zero-loss capture is retained outside the repository at:

```text
L:\Codex\evidence\render-map\20260829-material-srv-live\lighting-execution-complete\capture-live-0000fd1c2dfcdd10-6
```

- capture ID: `capture-live-0000fd1c2dfcdd10-6`
- `events.jsonl` SHA-256:
  `8AAA6862135727AAC78FEBD9695B91DDCCD147EE079F132DF7A0A2809B2CF011`
- `capture-manifest.json` SHA-256:
  `5BF22F241F3A1A54636755ADD050B91EDC6C26063200E748FA3F9886CA4B4BC4`
- graph 1.7 SHA-256:
  `BDCA935A429A90E6B862ADC8344E9D09FB1062A38141195E432E529C1C2E1863`
- stop reason: `frame-limit`
- truncation: false
- stop-race events: zero
- every dropped event and catalogue counter: zero

The capture retained 8,539 events from 10,642 attempts, with 3,090 intentional
selector filters. Its relevant inventories were:

| Evidence | Count |
|---|---:|
| resource-view bindings | 3,855 |
| geometry observations | 719 |
| material observations | 719 |
| geometry setup begin/end pairs | 719 |
| resource observations | 680 |
| draw events | 0 |

## Finding

The empty draw inventory is a runtime-contract result, not capture loss.
`BSLightingShader::SetupGeometry` is wrapped by the geometry RAII scope, but the
engine issues its D3D11 draw only after that function returns. With
`executionWithinSelectedGeometry=true`, every later draw was therefore outside
the active call-stack scope and was intentionally filtered. The material and
effective SRV declarations were complete, but no honest execution join existed.

## Corrective contract

Render-event 1.11 / `draw-call-v3` adds a one-shot
`preparedGeometrySetupObservationId` handoff:

1. a selected setup publishes its exact geometry-setup observation;
2. any later geometry setup, selected or unselected, invalidates the prior
   candidate;
3. the next same-thread draw must match the capture generation and immediate
   context;
4. the draw consumes the candidate exactly once; and
5. the serialized draw keeps `scopes.geometry=null`, making the post-return
   relationship explicit rather than manufacturing scope nesting.

Derived graph 1.8 / `static-semantic-resource-graph-5` emits a `prepares` edge
and projects scene, geometry and material identities through the explicit
prepared setup. Runtime, serialization, graph-builder and schema regression
tests pass.

## Corrective live qualification

The corrective contract was then qualified in a loaded Breezehome scene. The
complete, zero-loss capture is retained at:

```text
L:\Codex\evidence\render-map\20260829-material-srv-live-v3\lighting-prepared-draw-complete\capture-live-0000ffab2b04eaa8-2
```

- capture ID: `capture-live-0000ffab2b04eaa8-2`
- `events.jsonl` SHA-256:
  `ABD6F5AC3D35BC4995D6CC2501901BD649EE13C2D8DD54BD234F38564D9A763E`
- `capture-manifest.json` SHA-256:
  `057BE76C3A2CB5097AFA7B12B84BB82F832B40A9362DFEE85B9DEF8DCAC860CA`
- graph 1.8 SHA-256:
  `EE5B864B585B6D12F3B4457EB1431BD35DE1D3EA276FEF9EF5377E46501BCB38`
- stop reason: `frame-limit`
- retained events: 4,724 from 6,035 attempts
- intentional selector filters: 1,580
- truncation and every dropped event/catalogue counter: zero

The first corrective attempt used a 12,000-event cap and correctly reported
truncation: restoring draw capture increased the admitted event volume. The
authoritative repeat raised the cap to 32,768 and completed without loss. This
was a capture-budget correction, not a runtime or graph defect.

| Correlation evidence | Count |
|---|---:|
| geometry setup begin/end pairs | 321 |
| material observations | 321 |
| geometry observations | 321 |
| draw events | 321 |
| draws with explicit prepared setup ID | 321 |
| unique prepared setup IDs | 321 |
| missing referenced setup declarations | 0 |
| resource-view bindings | 2,025 |
| resource observations | 381 |

All 321 draws correctly retained `scopes.geometry=null`; all 321 instead named
their declared prepared setup. The graph contains 4,304 nodes and 10,719 edges,
is acyclic, and includes 321 `prepares` edges and 1,305 non-zero
`materialInputMatches`. It also contains 2,103 `binds-texture`, 2,838 `reads`,
80 `writes`, and 321 `uses-material-state` edges.

The graph reports 302 remaining gaps. Of these, 301 are `uncorrelated` draws
whose output-merger target state was already active before capture began, and
one is an `unsupported-route`. None is a missing prepared-setup declaration or
failed material-input join. The dominant residual gap therefore identifies the
next mapping slice: seed the capture with effective immediate-context state,
especially output-merger target bindings.

## Output-merger capture-start qualification

Render-event 1.12 closes that residual boundary without retaining render-target
COM objects outside an active capture. Before the first draw on an immediate
context, the collector claims the active capture generation and queries the
effective output-merger state only when no real OM bind has yet been observed.
The queried state is emitted as `render-target-binding-v2` with source
`capture-state-snapshot`; ordinary hooked binds remain `observed-call`.

The complete, zero-loss loaded-Breezehome qualification is retained at:

```text
L:\Codex\evidence\render-map\20260829-om-state-seed-live\lighting-om-seed-complete\capture-live-000101c0601b5224-3
```

- capture ID: `capture-live-000101c0601b5224-3`
- `events.jsonl` SHA-256:
  `65B7CD6EFB1844E1A52F12D5618685E8B0B504847FDA3E69EEA89B81D00AE080`
- `capture-manifest.json` SHA-256:
  `B9FC42CC057EF13E1E4A0BC963470FF2D4D42B4CC089FCBD7C79166DBB515BEB`
- graph 1.8 SHA-256:
  `180A6E05670DE3C046ECEA7ED1FC2815E58086F50D30A061839B2027B24618BC`
- graph 1.9 exact-subresource re-derivation SHA-256:
  `0F69F2FE2F3DAF492C5860E3CBF2E2C052F8F2E773ED0F6A14548E0F5F16F0FB`
- stop reason: `frame-limit`
- retained events: 9,762
- intentional selector filters: 3,168
- truncation, stop-race rejections, dropped events, and every catalogue drop
  counter: zero

The capture contains 724 geometry setups, 724 material observations, and 724
draws. It emitted one `capture-state-snapshot` at sequence 4 for
`obs-device-context-1-g3`; the first draw on that context occurred at sequence
571. Eighty-seven later `render-target-bind` events are `observed-call` events.
The snapshot therefore precedes the first selected draw and is suppressed once
normal OM observations take over, exactly as designed.

The derived graph contains 16,558 nodes and 62,402 edges. The former 301
pre-capture output-merger gaps are absent. Its only remaining gap is the
deliberate, non-blocking `unsupported-route`: resource versions and hazard
edges are allocation-wide because exact D3D11 view-subresource overlap and the
state returned by automatic hazard resolution are not yet observed. That
limitation is now the next resource-flow slice rather than an output-state
correlation failure.

Graph 1.9 / `static-semantic-resource-graph-6` then replaced the conservative
same-allocation conflict test with exact D3D11 mip/array subresource overlap.
It recognizes buffers as one subresource and represents texture-array SRVs as
rectangular mip-by-array regions, consistent with the D3D11 view contract. The
same capture still produced all 719 effective-state adjustments and required
zero unknown-descriptor fallbacks. In this scene, none of the previous hazard
adjustments was a false conflict between disjoint mips or array slices. The
single remaining gap is therefore narrower: query and retain the actual
post-setter state returned by D3D11 rather than deriving its automatic NULLs
from the documented conflict rule.

## Post-call effective-state implementation

The next implementation slice closes that unsupported route in the capture
contract. Render-event 1.13 and service contract 1.15 / schema revision 16 now
emit setter intent as `resource-view-binding-v2` source `requested-call`, then
query and emit the state D3D11 actually retained as source `post-call-query`.
`resource-view-state-observed-v1` records the queried kind, stage, slot range,
source, and delta count, so an unchanged query remains affirmative evidence.
Output-merger bindings likewise distinguish `observed-call` from
`post-call-query`.

Graph 1.10 / `static-semantic-resource-graph-7` uses the queried state for
execution edges and keeps exact subresource overlap as a prediction. It reports
the number of predicted adjustments, descriptor fallbacks, verified queried
slots, and mismatches. The retained capture above necessarily remains a graph
1.9 qualification because its event stream predates the new getters; a new live
capture is required to qualify graph 1.10 and measure its bounded event overhead.

That qualification exposed a second-order controller limit. With the default
scene, geometry, material, shader, target-view, and resource catalogues admitted,
the former 32 MiB maximum left only a few MiB for the substantially richer
effective-state event stream. A direct 12 ms loaded-scene capture could therefore
reach `byte-limit` before an explicit stop despite zero catalogue drops. Reducing
catalogues made completeness depend on which render phase happened to be active.
Service contract 1.16 / schema revision 17 consequently raises both the admitted
default and maximum to 64 MiB. Capture remains off by default and explicitly
bounded; the change restores a phase-independent useful default rather than
weakening any stop or ownership rule.

## Post-call effective-state live qualification

The rebuilt service was qualified against the verified Breezehome save under a
Valve null-HMD runtime. DevBench reported `playerLoaded=true`, the
`WhiterunBreezehome` cell, no message box, and only `HUD Menu` open. The runtime
registry reported service 1.16, schema revision 17, and both the default and
maximum byte budget as 67,108,864 bytes. Its admitted fixed catalogues occupied
29,821,088 bytes.

The authoritative capture is retained at:

```text
L:\Codex\evidence\render-map\20260829-effective-state-live\capture-live-000106698b975364-1
```

- capture ID: `capture-live-000106698b975364-1`
- capture duration: 27.5056 ms
- stop reason: `requested`
- retained events: 27,877 from 34,737 attempts
- intentional selector filters: 6,860
- truncation, stop-race rejections, boundary rejections, dropped events, and
  every catalogue-drop counter: zero
- `events.jsonl` SHA-256:
  `F3939D413FE57807E0D2B4F80144F31110A60333AB36D2AD92A7BD8A6C9A55C7`
- `capture-manifest.json` SHA-256:
  `DF4CF200A8C930F67E4952B05F9610461F95BD6827B409B4B82ABF4EFC9634FD`
- graph 1.10 SHA-256:
  `184CA252B10C75C5FD1C29D21D16E19791F525CE0BFB616BE578B6E155979712`

The capture retained 7,028 requested SRV/UAV bindings, 7,364 post-call query
events, nine capture-start state snapshots, and four observed output-merger
calls. More specifically, it contains 7,068 explicit effective-state query
events: 7,060 post-call queries and eight capture-start snapshots. The graph
verified 7,042 predicted slots against the state actually returned by D3D11 and
found zero mismatches.

The derived graph contains 26,812 nodes and 117,904 edges, is acyclic, and has
zero gaps or ambiguities. This short render window did not contain a conflicting
input/output view setter, so both the observed effective-state adjustment count
and the exact-overlap fallback count are zero. That is a qualified negative
result: the queried state agrees with every prediction exercised by this
capture, while the machinery remains able to report a later automatic NULL as
an observed mismatch rather than inferring it as fact.

## Recovery evidence

The live run promoted the unchanged known-working shader cache snapshot and
restored the pre-task cache tree. The exact task profile bytes, MO2 session and
lease were restored/released. SteamVR initially exposed a partial restoration
race: another actor had already restored the exact pre-test OpenVR registration,
while SteamVR had changed only its `GpuSpeed` telemetry. After reconstructing
the receipt's isolated state, the transactional controller restored and verified
the exact pre-test hashes:

- `steamvr.vrsettings`:
  `EE31AC1004DF9AA152264A600BDD43C87D31D8628E9CCA5B6075393B3E4D0A38`
- `openvrpaths.vrpath`:
  `7255D1865FA042B6046016160BCCFD360BAA467D630FA62335030C49D8CAA51A`

No Skyrim, MO2 or SteamVR processes remained after teardown.

The corrective run was also restored exactly. Its task-local working cache was
preserved, then the pre-task managed cache tree
`7A8A9AA134AAF3F6A56F5E71E3A713DCAF372EE16E891F29957E523FFF4126F7`
was restored. OCU was re-enabled, the exact MO2 task session and lease were
released, and SteamVR was stopped gracefully. SteamVR had rewritten the
semantically identical isolated OpenVR state with different formatting; after
reconstructing the receipt's exact isolated serialization, transactional
restore verified the same pre-test hashes shown above. No Skyrim, MO2, or
SteamVR process remained.

The post-call effective-state qualification used the same exact restoration
rules. OCU and the complete task-profile bytes were restored to
`D02159E891CD4D9B63DF1C69361A2A28E7F98E9345A04B1130A75D18EE37A622`,
and the retained MO2 session and access lease were released. SteamVR again
rewrote only the semantically identical isolated OpenVR JSON formatting. A
recovery receipt admitted that independently verified rewrite hash, after which
the transactional controller restored the exact pre-test SteamVR and OpenVR
hashes listed above. Final inspection found no Skyrim, MO2, or SteamVR process
and no active MO2 session lock.
