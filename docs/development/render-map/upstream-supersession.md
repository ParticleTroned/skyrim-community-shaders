# Upstream runtime and adapter coverage

PR [103](https://github.com/ParticleTroned/skyrim-community-shaders/pull/103)
combines the intended runtime from PR
[55](https://github.com/ParticleTroned/skyrim-community-shaders/pull/55)
at `3967cdc6bdc7bd692a7ff39caccf5c41309f9ccd` and callable adapter from PR
[56](https://github.com/ParticleTroned/skyrim-community-shaders/pull/56)
at `dcdcf61faf6d8cf6ab3736dc70d0533bcd2d8197`. These historical branches and
their attribution remain intact. PR103 does not depend on either branch.

The comparison covers public contracts, observation paths and their tests;
matching filenames alone does not establish coverage. Current qualification
receipts remain separate from the old PRs' build and review results.

| Intended capability        | Integrated implementation and coverage                                                                                                                                                                                                                                                                                                                                                        |
| -------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Bounded opt-in runtime     | `Collector` and `Runtime` retain every earlier event kind and runtime declaration. Capture starts inactive and accounts for event, byte, frame, time, catalogue, string and scope limits, filtering and explicit gaps. Collector/runtime tests cover admission, limits, ownership and incomplete evidence.                                                                                    |
| Engine observations        | `Hooks`, `Globals` and `InSceneOverlay` feed render passes, techniques, geometry, materials, culling observations and accepted eye submissions into the same runtime. They observe existing decisions; depth-culling policy is unchanged.                                                                                                                                                     |
| D3D observations           | Device/context hooks retain shader, resource/view/binding, CPU-access, version, draw and dispatch observations. Deferred-context and command-list identities are additional evidence, with fail-closed gaps and capability qualification kept explicit.                                                                                                                                       |
| Session controller         | `CaptureController` retains single-session start/status/stop, completed-capture ownership and lookup. Controller tests exercise repeated stop, missing captures, draining and publication faults.                                                                                                                                                                                             |
| Deterministic artifacts    | `Artifacts` and `Serialization` retain JSONL events, manifest, summaries and completed-event paging. Existing evidence is not overwritten. Prepared-cache ownership now precedes writing; verified output survives response faults within the live process. Real artifact tests check immutable-file recovery and foreign-file rejection.                                                     |
| Event and manifest schemas | Both runtime schemas remain present. Earlier event kinds and required envelope fields are retained. Revision 1.17 adds explicit deferred identity and nullable unavailable evidence; summary and manifest truncation meanings are documented independently. Fixtures and contract tests cover valid, missing and contradictory records.                                                       |
| Callable control adapter   | `communityshaders.render_map` is registered at the existing three plugin lifecycle sites. Actions remain `registry`, `status`, `start`, `stop` and `capture_events`; contract 1.21/schema23 replaces 1.17/schema18. Action names, required inputs, selectors, bounds and paging validation match the earlier input schema; descriptions now explain stricter provenance/publication behavior. |
| Start and stop safety      | Start prepares its provenance and response before activating capture. Stop requires that retained provenance and reserves publication ownership before writing. Failures remain explicit; response failure does not invalidate a verified bundle. Controller and shared production-helper tests exercise these boundaries.                                                                    |
| Shader compile provenance  | `ShaderCache::GetCompileContextSnapshot()` retains all nine PR56 fields using the existing `CaptureGlobalCompileState()` snapshot and BuildProvenance identities. It does not compile, schedule, invalidate or change cache policy. The contract runner verifies the adapter call, declaration and definition together.                                                                       |
| Shader diagnostic lifetime | Native shader results are preserved through a catch-all observation boundary. Weak D3D private-data retirement, identity/alias/dump limits and allocation-free failure counters strengthen the earlier observations; native tests cover catalogue exhaustion, conflicts, teardown and exceptions.                                                                                             |
| Bridge OFF                 | Render Map translation units are excluded and adapter entry points are inline inert stubs. Diagnostic hooks are guarded. Ordinary renderer behavior does not require DevBench or an active capture.                                                                                                                                                                                           |
| Bridge ON                  | The integrated adapter and runtime compile together. Registration requires an available DevBench interface, remains idempotent and does not start capture. Full ON DLL qualification verifies the production dependency closure separately from native policy hosts.                                                                                                                          |

The supersession audit found that the adapter in the earlier integrated head
called a missing `GetCompileContextSnapshot()` dependency. The read-only
declaration and implementation from PR56 are restored here, with concise API
documentation. The source contract now detects loss of that dependency.
Earlier policy-host passes did not compile the production adapter and cannot
establish that the missing dependency was valid.

## Limits and provenance

The adapter retains the earlier public maxima: 600 frames, 10 seconds,
65,536 events, 64 MiB capture storage, scope depth 32, and pages of at most
500 completed events. Fixed catalogue storage is included before event
storage is admitted. Independent persistent shader metadata limits are
65,536 bytecode identities, 65,536 stage identities, eight aliases per
stage identity and 64 MiB optional dump bytes. These are not process-memory
measurements and do not belong to the live capture's `maxBytes` budget.

Compile state is runtime-observed global provenance. The shader-specific
compatibility registry remains explicitly incomplete. Missing native creation
metadata stays unavailable. Deferred-context and command-list capability flags
remain false until live coverage is qualified. Source tests, native hosts,
production compilation, loaded runtime, pixels and performance are separate
claims. No historical build or review is relabeled as current-head evidence.

## Preserved exclusions

This composition includes its existing deterministic offline graph builder
and fixtures. It does not add unfinished shader dependency analysis,
generated shader manifests, engine maps, Ghidra helpers, prior-art catalogues
or captured-analysis reports. Local depth-culling development is not imported;
depth-culling events describe existing upstream behavior.

PR55 and PR56 may be retired as redundant only after this intended coverage
and the current candidate's applicable checks are established. Their original
branches, commits, authorship and validation records remain historical evidence.
