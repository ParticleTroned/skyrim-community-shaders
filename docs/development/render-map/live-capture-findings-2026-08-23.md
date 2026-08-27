# Live render-map capture findings — 2026-08-23

This report records the first bounded live validation of the render-map
controller and DevBench service. It is a compact derived report; raw runtime
events remain in retained external evidence storage.

## Provenance

- CSX build ID:
  `5cfc377538ad45d3f7af9b5c0db160e4956fcc1c5f4de868617bf3b407160a33`
- diagnostic `CommunityShaders.dll` SHA-256:
  `a2400fa73c271e1d1039868896affe2c9d63035da91635e43a0090c1803496d4`
- build-manifest SHA-256:
  `64b4a3eb96452345e4958a9a1a8ed3163be642f58c37173a90a2f2ad1c2ace76`
- raw event-page SHA-256:
  `7ec34f84ee034fd18e81463068ab9514f6b60990025cf721666ff886e933939d`
- derived summary SHA-256:
  `0eba1577d55deeb5a55137b41008984d6f79fb33f3961b62fae7ea7125033135`

The run used Skyrim VR at the main menu through the Valve null-HMD driver while
shader initialization was active. It is evidence for the controller and hook
plumbing, not a normative workload profile or a complete rendering map.

## Capture bounds and completion

The controller requested four frames, a two-second duration, at most 16,384
events, at most 4 MiB, and a maximum scope depth of 16. Capture
`capture-live-000080cb406bce48-1` completed on the frame bound.

The retained page contains 192 events with contiguous sequence numbers 0–191,
distributed as exactly 48 events over each CPU frame 5264–5267. All events
were emitted by OS thread 26684. The collector reported no scope overflow, no
scope mismatch, and no event or byte truncation.

## Observed event coverage

| Event pair | Begin/enter | End/exit | Total |
|---|---:|---:|---:|
| render pass | 28 | 28 | 56 |
| technique | 52 | 52 | 104 |
| geometry setup | 16 | 16 | 32 |

All observed scope pairs were balanced. Scope attachment was present on 144
render-pass events, 104 technique events, and 32 geometry events. No
command-list scope was observed because that hook layer is not yet implemented.

Seven distinct numeric pass/technique values appeared in this sample:

`8261`, `81989`, `536879147`, `1073741934`, `1074270451`, `1140850733`, and
`1224753773`.

These values are retained as observations only. This sample does not establish
stable semantic names for them.

Pointer evidence was present on 88 render-pass payloads, 88 geometry payloads,
and 136 shader payloads. These process-local values help correlate events
inside the capture, but are not stable identifiers and must not enter the
static engine or shader manifests.

## Confirmed boundaries

The run confirms that:

- the DevBench service can discover, start, stop, and page a capture from the
  loaded build with exact build identity validation;
- the existing engine hook owners produce balanced render-pass, technique, and
  geometry observations without adding new detours;
- capture remains bounded and inactive until explicitly started;
- capture-local scope identifiers correlate nested events across the serialized
  envelope; and
- the resulting data is sufficient to identify the next missing layers without
  pretending that source, engine, and runtime identities are already joined.

## Gaps exposed by the run

Every event had an `unknown` eye. No event carried command-list scope, device
context identity, engine references, shader-manifest references, observation
references, or causal edges. The next slices should therefore prioritize:

1. frame/scene/eye attribution at a reliable engine boundary;
2. device-context and command-list identity;
3. bounded D3D draw, dispatch, target, and depth-source observation;
4. deterministic joins from observed shader/pass descriptors to the static
   shader manifest and versioned engine map;
5. semantic decoding of numeric technique/pass values; and
6. validation across gameplay states and multiple runs before promoting a
   correlation to an engine fact.

## Limit-accounting observation

After the four-frame bound was reached, the controller remained logically
active until the explicit `stop` request. Hook calls during that interval were
rejected and counted, producing 31,656 post-bound drops from 31,848 total
attempts. The retained 192-event capture itself is complete and balanced; this
large drop count describes controller finalization latency, not missing events
inside the accepted four-frame sample.

A later controller slice should make a latched bound immediately disable the
inactive fast path while preserving explicit, idempotent finalization and a
truthful completion summary. That change needs separate testing around bounds
reached inside nested scopes and is not inferred from this single run.
