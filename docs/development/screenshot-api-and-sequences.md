# Screenshot API and sequence contract

Status: implementation candidate available in the current worktree; contract
major version 1 is not yet frozen pending null-HMD and physical-HMD
qualification. This document is normative where it uses **must**, **must not**,
**required**, or **terminal**.

Implemented in the candidate are the screenshot coordinator and the shared API
service foundation for envelopes, idempotency, session identity, and journaling;
the DevBench adapter, native service, obsolete-menu compatibility delegation,
in-game control delegation, still and bounded sequence capture,
multi-output/separate-eye processing, partial/final manifests, settings-schema
migration, idempotency, acknowledgement, cancellation, and retention. Offline
preview-video packaging remains capability-reported as unavailable. The test
and live-qualification matrix below remains the gate for freezing version 1.

## Decision summary

CSX will expose screenshot capture as a versioned asynchronous service rather
than a void `RequestCapture()` command. The service covers still images and
scheduled frame sequences. It communicates validation, acceptance, source
selection, fallback, staging, encoder progress, artifacts, partial results,
failures, cancellation, and client acknowledgement.

External adapters are the optional DevBench tool
`communityshaders.screenshot` and the native `csx.screenshot` service published
through the `CSXR` registry. The old `communityshaders.menu` screenshot action
is **obsolete**. It remains temporarily as a compatibility adapter so legacy
clients can migrate, and delegates through that same native service. New
clients must not use it. CSX's in-game capture controls also dispatch through
the native service. Screenshot methods are not appended to the legacy general
`CSAP` vtable.

A sequence is complete when its frame artifacts and final manifest are safely
written. Creating a playable video is an optional post-capture packaging step.
It must never make render-thread capture, frame-set completion, or evidence
preservation depend on real-time video encoding.

## Goals

1. Never silently accept a capture that cannot run.
2. Give every accepted operation a stable server-generated request ID.
3. Make retries idempotent across temporary DevBench reconnects.
4. Snapshot effective options at acceptance so later UI changes cannot alter an
   in-flight operation.
5. Distinguish requested source, resolved source, fallback, view composition,
   encoding, and destination.
6. Report every terminal outcome, including partial sequence completion.
7. Keep render-hook work bounded to source observation and GPU staging.
8. Keep image conversion, encoding, hashing, manifests, and optional video
   packaging off the render and game threads.
9. Bound memory, encoder backlog, event retention, and filesystem use.
10. Preserve still-capture behavior while allowing sequences to reuse the same
    acquisition and artifact pipeline.
11. Make the contract usable from flat Skyrim, Skyrim VR, null HMD, and physical
    HMD runs without implying that all sources exist in every runtime.
12. Make capability and version negotiation explicit enough for unattended
    automation.

## Non-goals for contract version 1

- Streaming pixels through MCP or the public SKSE messaging interface.
- Guaranteed capture at an exact wall-clock instant.
- Blocking a DevBench call until future render frames or disk writes complete.
- Real-time H.264/H.265/AV1 encoding on the render path.
- Deleting output files through the API.
- Exposing D3D resources or STL objects across a plugin ABI.
- Treating a null-HMD qualification as physical-headset driver timing proof.

## Layered architecture

The feature is split into six internal components with one source of truth for
operation state.

1. **ScreenshotCoordinator** validates commands, allocates IDs, snapshots
   options, serializes source acquisition, applies cancellation, and records
   terminal outcomes.
2. **CaptureSourceProvider** observes an eligible desktop Present or coherent
   accepted OpenVR submissions and stages immutable planes.
3. **ArtifactProcessor** performs readback, orientation, composition, crop,
   resize, colour conversion, encoding, atomic file commit, and hashing on a
   worker thread.
4. **SequenceScheduler** selects eligible game frames or wall-clock windows and
   submits child frame captures without reserving future encoder slots.
5. **RequestJournal** owns bounded request records and monotonically ordered
   events, including terminal receipts and client acknowledgements.
6. **Adapters** translate UI/hotkey, DevBench JSON, and the native CSXR service
   into the same coordinator commands.

No adapter may reach into `ActiveCapture`, the encoder queue, or source hooks
directly. UI and hotkey requests receive IDs and receipts too, even if they only
surface a HUD message to the player.

While a sequence is actively recording, CSX presents one red status dot at eye
height, one eighth of the submitted eye width left of centre. In VR it is
headset-locked (including the OpenComposite per-eye path), and it is composed
only after the lossless source for that frame has been staged. It therefore
remains visible without entering the saved frame set. Stop/finalize messages
may use the ordinary HUD; no second recording overlay is drawn.

## Contract identity and versioning

### Tool identity

- DevBench tool: `communityshaders.screenshot`
- Contract name: `csx.screenshot`
- Initial contract: major `1`, minor `0`
- Schema URN: `urn:csx:devbench:screenshot:1`

### Native discovery

Native SKSE consumers request the `CSXR` registry described by
`include/VRAPI/CSserviceapi.h`, then query `csx.screenshot` major 1 with the
inspection, runtime-mutation, asynchronous-operation, and event-stream
capabilities. `include/VRAPI/CSscreenshotapi.h` exposes one ABI-stable
`Dispatch` function over this document's UTF-8 JSON contract.

`Dispatch` validates or admits a request synchronously; calls from Papyrus or
other native threads are marshalled to Skyrim's runtime main thread with a
bounded wait. Capture, encoding, hashing, and sequence finalization remain
asynchronous. The returned JSON bytes are owned by CSX and remain valid until
the caller's next `Dispatch` on the same thread, so clients must copy them.
Transport failures use `CSX::ScreenshotAPI::Status`; command rejection and
operation state remain in the normal JSON response envelope.

Every request must include `contractMajor`.
Clients may include `contractMinor`; absence means zero. A major mismatch is a
structured `unsupported_contract_version` error and must not perform work.

Minor revisions are additive. A server may add fields, events, capabilities,
error details, and enum values advertised by capabilities. It must not change
the meaning or type of an existing version-1 field. A breaking change uses a
new major version and, if necessary, a new tool alias during migration.

`schemaRevision` identifies corrections or additive schema detail within the
same major/minor semantics. It is not a substitute for a breaking major.

### Response envelope

Every response, including errors, uses one envelope:

```json
{
  "ok": true,
  "contract": {
    "name": "csx.screenshot",
    "major": 1,
    "minor": 0,
    "schemaRevision": 1
  },
  "server": {
    "sessionId": "55be1d4e-7d90-4eb9-9199-cbb50fd31388",
    "csxBuild": 11,
    "csxVersion": "3.19-VR",
    "featureVersion": "1.5.1",
    "binary": {
      "sha256": null,
      "sourceRevision": null,
      "identityState": "not_recorded"
    },
    "runtime": {
      "game": "SkyrimVR",
      "presentation": "openvr",
      "hmd": "unknown"
    },
    "devBenchBuilt": true
  },
  "command": {
    "action": "capture",
    "clientId": "automation-host-a",
    "commandId": "0de51a0e-8ed6-49ee-a1ca-94b5aa54a919"
  },
  "timestampUtc": "2026-08-20T04:15:30.1234567Z",
  "result": {}
}
```

`sessionId` changes at each Skyrim/CSX process start. `commandId` is generated
by the client and supplies idempotency. Within one server session, repeating
the same `clientId + commandId` returns the original command result or current
operation receipt and must not create a second capture. Reusing the pair with
different arguments is `idempotency_conflict`.

The server-generated `requestId` identifies accepted screenshot or sequence
work. It is distinct from the client command ID because status, stop, cancel,
poll, and acknowledgement commands each have their own command IDs.

`binary.identityState` is `complete`, `partial`, or `not_recorded`. Null identity
fields are explicit unknowns, never permission to infer a commit from a filename
or DLL timestamp. Production and evidence builds should cache the DLL hash and
source revision at startup so receipts can identify exactly what produced an
artifact.

### Error envelope

```json
{
  "ok": false,
  "contract": {
    "name": "csx.screenshot",
    "major": 1,
    "minor": 0,
    "schemaRevision": 1
  },
  "server": {
    "sessionId": "55be1d4e-7d90-4eb9-9199-cbb50fd31388",
    "csxBuild": 11,
    "csxVersion": "3.19-VR",
    "featureVersion": "1.5.1",
    "binary": {
      "sha256": null,
      "sourceRevision": null,
      "identityState": "not_recorded"
    },
    "runtime": {
      "game": "SkyrimVR",
      "presentation": "openvr",
      "hmd": "unknown"
    }
  },
  "command": {
    "action": "capture",
    "clientId": "automation-host-a",
    "commandId": "0de51a0e-8ed6-49ee-a1ca-94b5aa54a919"
  },
  "timestampUtc": "2026-08-20T04:15:30.1234567Z",
  "error": {
    "code": "feature_disabled",
    "message": "CSX screenshot capture is disabled",
    "phase": "validation",
    "retryable": true,
    "field": null,
    "requestId": null,
    "details": {}
  }
}
```

Human-readable text is diagnostic. Automation must branch on `error.code`, not
message text.

## Actions

Version 1 defines these actions:

| Action | Mutation | Purpose |
| --- | --- | --- |
| `capabilities` | No | Contract, runtime, sources, views, encoders, limits, and optional components |
| `status` | No | Feature, worker, queue, active-operation, and journal summary |
| `settings_get` | No | Current persisted and effective screenshot settings |
| `settings_validate` | No | Validate a proposed settings patch without applying it |
| `settings_apply` | Yes | Apply an explicit runtime or persisted settings patch |
| `capture` | Yes | Submit one still operation |
| `sequence_start` | Yes | Submit one scheduled frame sequence |
| `sequence_stop` | Yes | Gracefully stop scheduling and finalize frames already accepted |
| `request_get` | No | Return one current or terminal receipt |
| `request_list` | No | Return bounded summaries, with filters and pagination |
| `request_cancel` | Yes | Cancel work that has not crossed an irreversible commit boundary |
| `events_poll` | No | Read journal events after a cursor |
| `acknowledge` | Yes | Confirm receipt of events or a terminal operation receipt |

`status` must never trigger capture. This differs deliberately from the legacy
profiler status behavior.

### Capabilities

Capabilities are authoritative. UI and clients must not infer support from the
game edition or a previously observed build number.

The response includes at least:

```json
{
  "sources": ["desktop_mirror", "hmd_submission"],
  "views": [
    "source_native",
    "left_eye",
    "right_eye",
    "side_by_side",
    "framed_left",
    "framed_right",
    "framed_combined"
  ],
  "formats": ["png", "bmp"],
  "colourContracts": ["sdr_srgb"],
  "scheduleBases": ["game_frames", "wall_clock"],
  "pathPolicies": ["settings_default", "game_relative", "absolute"],
  "optional": {
    "separateEyeArtifacts": true,
    "clipboardFileReference": true,
    "previewVideo": {
      "available": false,
      "encoders": [],
      "runsAfterFrameFinalization": true
    }
  },
  "limits": {
    "activeSourceCaptures": 1,
    "outstandingArtifacts": 2,
    "pendingOperations": 64,
    "maximumOutputsPerFrame": 4,
    "maximumSequenceFrames": 10000,
    "maximumSequenceDurationMs": 3600000,
    "maximumRetainedTerminalRequests": 256,
    "maximumRetainedEvents": 4096,
    "retentionSeconds": 3600
  }
}
```

These numbers are examples, not frozen limits. The implementation reports its
actual values. The present worker's two-outstanding-artifact limit may remain
initially; a sequence scheduler must adapt rather than enlarge it blindly.

### Operational status

`status` is a read-only health snapshot. It reports enough information to
distinguish feature configuration, source readiness, active acquisition,
worker backlog, and journal retention without initiating work:

```json
{
  "feature": {
    "loaded": true,
    "enabled": true,
    "settingsSchemaVersion": 2
  },
  "sourceReadiness": {
    "desktopPresentObserved": true,
    "openVrSubmitHookInstalled": true,
    "lastAcceptedEyeFrame": 123456,
    "loadingMenuOpen": false
  },
  "dispatcher": {
    "activeAcquisitionRequestId": null,
    "pendingOperations": 0,
    "activeSequences": 0
  },
  "worker": {
    "running": true,
    "outstandingArtifacts": 0,
    "capacity": 2,
    "completedArtifacts": 18,
    "failedArtifacts": 0
  },
  "journal": {
    "oldestRetainedEventId": 310,
    "latestEventId": 481,
    "retainedTerminalRequests": 7
  },
  "lastTerminalRequest": {
    "requestId": "a43cdf9a-22be-4899-bad4-7fb0966e084d",
    "state": "completed",
    "terminalUtc": "2026-08-20T04:15:30.402Z"
  }
}
```

Counters are monotonic for the server session unless the capability response
documents a reset action. Readiness timestamps/frame IDs are accompanied by
validity flags when zero is otherwise ambiguous.

## Capture descriptor

Still and sequence frames use one normalized capture descriptor. Saved UI
settings are translated into this descriptor and then frozen.

```json
{
  "source": {
    "kind": "hmd_submission",
    "fallback": "reject"
  },
  "outputs": [
    {
      "view": "framed_combined",
      "dominantEye": "left",
      "width": 2560,
      "height": 1440,
      "crop": null,
      "encoding": {
        "format": "png",
        "colourContract": "sdr_srgb"
      },
      "nameSuffix": "combined"
    },
    {
      "view": "left_eye",
      "encoding": {
        "format": "png",
        "colourContract": "sdr_srgb"
      },
      "nameSuffix": "left"
    },
    {
      "view": "right_eye",
      "encoding": {
        "format": "png",
        "colourContract": "sdr_srgb"
      },
      "nameSuffix": "right"
    }
  ],
  "destination": {
    "policy": "settings_default",
    "directory": null,
    "baseName": null,
    "overwrite": "never"
  },
  "clipboard": "none",
  "tags": {
    "scene": "Riften exterior",
    "state": "enabled"
  }
}
```

Tags are optional evidence labels only. Keys and string values are length-
limited, the total serialized tag payload is bounded, and tags cannot change
paths, scheduling, or feature behavior.

### Source and view are orthogonal

The existing `VRCaptureSource` enum conflates acquisition and composition.
Version 1 separates them:

- `source.kind = desktop_mirror` observes the desktop backbuffer.
- `source.kind = hmd_submission` observes coherent accepted OpenVR eye
  submissions before compositor distortion.
- `source.kind = settings_default` resolves either source from the immutable
  settings snapshot.
- `outputs[].view` selects native, individual-eye, side-by-side, or framed
  composition from the acquired planes.

`source_native` means desktop dimensions for desktop capture and the current
side-by-side accepted-eye representation for HMD capture. An unsupported
source/view combination is rejected during validation.

`source.fallback` is `reject` or `desktop_mirror`. The actual source and any
fallback reason must appear in the receipt. Framed views default to `reject`;
silently converting a requested framed HMD view into a desktop image would be
misleading evidence.

### Outputs

One source acquisition may produce several outputs without restaging the GPU
texture. This is how sequences save separate left/right eyes plus a composed
view. Each artifact has a unique suffix and its own terminal result.

Version 1 supports current lossless PNG and BMP encoders. Other formats must be
capability-advertised before acceptance. `sdr_srgb` includes the current SDR
conversion and sRGB metadata rules. Future HDR/EXR contracts use new explicit
enum values; they must not silently change `sdr_srgb`.

Crop coordinates are normalized and validated within `[0,1]`. Framed views use
their defined framing transform and reject an additional crop unless a future
capability explicitly permits both.

### Destination safety

- `settings_default` uses the frozen screenshot or frame-sequence folder
  setting. Relative still paths resolve below
  `Pictures\Community Shaders`; relative sequence paths resolve below
  `Videos\Community Shaders`.
- `game_relative` resolves under the canonical game directory.
- `absolute` is accepted only when advertised and must be an absolute canonical
  path.
- Relative traversal outside the selected root is rejected as `unsafe_path`.
- Existing files are never overwritten in version 1. `overwrite` must be
  `never`; name collisions receive a deterministic numeric suffix.
- The worker writes a sibling temporary file, flushes and closes it, then
  atomically renames it to the final name where the filesystem permits.
- The receipt records both the requested destination policy and resolved path.
- The API never deletes artifacts.

An empty `baseName` uses the CSX timestamp plus a short request ID. Explicit
names are sanitized, length-limited, and cannot contain path separators.

### Immutable effective descriptor

`capture` may provide a complete descriptor or `useSettings: true` with a
request-scoped patch. The acceptance receipt always contains the fully expanded
effective descriptor. UI settings changed afterward affect only later requests.

## Still-capture lifecycle

Still states are:

```text
submitted -> accepted -> waiting_source -> staged -> queued -> encoding
          -> completed
          -> completed_with_warnings

submitted -> rejected
accepted/waiting_source -> cancelled
waiting_source/staged/queued/encoding -> failed
```

`submitted` exists only during command validation. A rejected command has no
server request ID unless an operation record is required for an idempotent
retry response.

Acceptance means options are valid, the feature is enabled, a pending-operation
record was reserved, and the server intends to attempt the capture. It does not
mean a source frame or encoder slot has already been acquired.

Once staging has committed GPU resources to the artifact worker, cancellation
is best-effort. Already committed artifacts may finish and are reported. A
cancelled operation with written artifacts is `cancelled_partial`, not
`cancelled` and not `completed`.

## Sequence contract

A sequence is a parent operation whose child frames use the same capture
descriptor.

Parent states are:

```text
submitted -> accepted -> running -> finalizing -> completed
                              |                 -> completed_with_warnings
                              |                 -> failed_partial
                              +-> stop_requested -> stopped
                              +-> cancel_requested -> cancelled
                                                   -> cancelled_partial
```

Every parent terminal state includes counts and a manifest outcome. A sequence
cannot be terminal-success while any child frame remains in a mutable state.

```json
{
  "contractMajor": 1,
  "clientId": "automation-host-a",
  "commandId": "39421691-94cd-405f-b441-81bcdb8e891f",
  "action": "sequence_start",
  "sequence": {
    "frameCount": 30,
    "schedule": {
      "basis": "game_frames",
      "intervalFrames": 6,
      "startDelayFrames": 0,
      "pausePolicy": "hold"
    },
    "backpressure": {
      "policy": "skip",
      "maximumConsecutiveSkips": 10
    },
    "failurePolicy": "continue",
    "capture": {},
    "packaging": {
      "frameManifest": true,
      "previewVideo": {
        "requested": true,
        "framesPerSecond": 15,
        "required": false
      }
    }
  }
}
```

### Scheduling

- `game_frames` uses CSX's monotonic engine render-frame counter. It is the
  deterministic default and maps the existing `SequenceFrameInterval` setting.
- `wall_clock` uses monotonic elapsed time, not local calendar time. Capture
  occurs at the next eligible render opportunity; no exact-time guarantee is
  made.
- A finite `frameCount` is required in version 1 unless capabilities advertise
  `untilStopped`. Even then a maximum duration is mandatory.
- `pausePolicy = hold` does not advance the schedule while no eligible rendered
  frame is observed. A future `skip` policy may count missed wall-clock slots as
  dropped frames.
- The scheduler records requested slot, actual engine frame, compositor cycle,
  monotonic timestamp, UTC timestamp, and lateness.

The scheduler never reserves all future encoder slots. At each eligible slot it
attempts one child capture. This preserves bounded memory with the current
worker limit and permits manual still captures between sequence frames.

### Backpressure

Version 1 policies are:

- `skip`: record a dropped child frame with `encoder_backpressure` or
  `source_busy`, then continue.
- `abort`: stop scheduling on the first capacity miss and finalize partial
  output as failed.

There is intentionally no unbounded `queue` policy. A later adaptive policy may
delay wall-clock scheduling but must be explicitly named because it changes
timing semantics.

Manual/UI still requests and sequences share a fair dispatcher. A sequence may
have only one child in source acquisition at a time. The coordinator must
prevent a high-frequency sequence from starving manual requests.

### Failure and stop behavior

`failurePolicy` is `continue` or `abort`.

- `sequence_stop` is graceful: stop creating new child frames, finish committed
  artifacts, write the final manifest, and report `stopped` or
  `completed_with_warnings`.
- `request_cancel` is immediate best-effort: cancel unscheduled and waiting
  children, allow irreversible writes to finish, and finalize
  `cancelled`/`cancelled_partial`.
- Disabling the screenshot feature behaves as immediate cancellation for
  source acquisition. It does not abandon committed worker writes.
- Device loss, runtime source loss, or worker shutdown must finalize a partial
  manifest rather than leave the sequence indistinguishable from success.

### Frame-set output

Each sequence owns a unique directory:

```text
CS_sequence_2026-08-20_041530_2f8c91a0/
  sequence.json.partial
  frame_000001_combined.png
  frame_000001_left.png
  frame_000001_right.png
  ...
  sequence.json
  preview.mp4                 # optional post-capture artifact
```

`sequence.json.partial` is updated atomically at bounded checkpoints, not after
every pixel write. On normal finalization it becomes `sequence.json`. Recovery
can identify an interrupted sequence from the partial manifest and preserved
frames.

The final manifest includes:

- contract and CSX identities;
- request/client IDs and the accepted command identity;
- requested and effective sequence/capture descriptors;
- start/end times and frame counters;
- scheduled, acquired, written, dropped, failed, and cancelled counts;
- one child record per scheduled ordinal;
- source/fallback, dimensions, format, colour contract, path, byte size, and
  optional SHA-256 for every artifact;
- backpressure, failure, cancellation, and warning details;
- preview packaging request and outcome.

Large manifests remain on disk. DevBench receipts return counts, recent
failures, and the manifest path; `request_get` supports paginated child summaries
rather than returning thousands of frames in one MCP response.

`requested`, `effective`, and `actual` are separate normative objects. A source
or view fallback is therefore durable sequence evidence rather than an
in-memory warning only. The schema requires child warning/error arrays and the
actual view, dimensions, format, and colour contract for every committed frame
artifact. A missing required final manifest is `failed` or `failed_partial`; it
is never reported as completion with a warning.

### Optional video packaging

Frame-set completion is independent of video packaging. If preview video is
requested:

1. Frame capture and the primary manifest finalize first.
2. An advertised post-capture packager reads committed frame artifacts.
3. Packaging state is `not_requested`, `unsupported`, `queued`, `running`,
   `completed`, or `failed`.
4. `required = false` allows the sequence to complete with a packaging warning.
5. `required = true` makes packaging failure a terminal sequence failure while
   preserving all frames and the manifest.
6. Packager command/version, codec, container, FPS, exit status, output size,
   and hash are recorded.

No encoder may consume D3D resources directly or hold up the sequence scheduler.
An unavailable video encoder is reported during validation when `required` is
true and as an explicit unsupported optional component when false.

## Events and acknowledgements

Every state transition writes a journal event with a process-wide monotonic
`eventId`, request-local `eventIndex`, request ID, type, timestamp, and payload.

Core event types are:

- `request.accepted`
- `request.cancel_requested`
- `source.waiting`
- `source.acquired`
- `source.fallback`
- `source.timeout`
- `artifact.queued`
- `artifact.encoding`
- `artifact.written`
- `artifact.failed`
- `sequence.frame_scheduled`
- `sequence.frame_dropped`
- `sequence.stop_requested`
- `sequence.finalizing`
- `packaging.queued`
- `packaging.completed`
- `packaging.failed`
- `request.terminal`

`events_poll` takes `afterEventId`, optional `requestId`, and a bounded `limit`.
It is non-blocking in version 1. The result returns events plus `nextEventId`,
`oldestRetainedEventId`, and `moreAvailable`. If a client cursor predates
retention, the response includes `cursor_expired` and directs the client to
`request_get` for current receipts.

There are two distinct acknowledgements:

1. **Server acceptance acknowledgement** is the successful response to
   `capture` or `sequence_start`. It contains the request ID, state, effective
   descriptor, and first event ID.
2. **Client receipt acknowledgement** is the `acknowledge` action. It records
   that a client has received events through an event ID or has received the
   terminal receipt for a request.

Acknowledgement never cancels work, deletes artifacts, or changes success. It
allows early journal compaction. Terminal receipts remain until acknowledged or
the advertised retention limit expires. Terminal manifests on disk are not
removed by journal expiry.

## Receipts

`request_get` returns a stable receipt shape for stills and sequences:

```json
{
  "requestId": "a43cdf9a-22be-4899-bad4-7fb0966e084d",
  "kind": "still",
  "origin": "devbench",
  "state": "completed",
  "terminal": true,
  "acceptedUtc": "2026-08-20T04:15:30.124Z",
  "terminalUtc": "2026-08-20T04:15:30.402Z",
  "effectiveCapture": {},
  "source": {
    "requested": "hmd_submission",
    "resolved": "hmd_submission",
    "fallbackUsed": false,
    "engineFrame": 123456,
    "compositorCycle": 9192,
    "planes": [
      {
        "eye": "left",
        "width": 2496,
        "height": 2592,
        "dxgiFormat": "R16G16B16A16_FLOAT",
        "colourSpace": "linear",
        "boundsApplied": true
      },
      {
        "eye": "right",
        "width": 2496,
        "height": 2592,
        "dxgiFormat": "R16G16B16A16_FLOAT",
        "colourSpace": "linear",
        "boundsApplied": true
      }
    ]
  },
  "progress": {
    "artifactsRequested": 1,
    "artifactsWritten": 1,
    "artifactsFailed": 0
  },
  "artifacts": [
    {
      "artifactId": "a43cdf9a:combined",
      "kind": "image",
      "view": "framed_combined",
      "path": "D:/Games/Skyrim/Screenshots/CS_..._combined.png",
      "format": "png",
      "colourContract": "sdr_srgb",
      "width": 2560,
      "height": 1440,
      "bytes": 4829911,
      "sha256": "...",
      "state": "written"
    }
  ],
  "warnings": [],
  "error": null,
  "timingsMs": {
    "sourceWait": 14.2,
    "staging": 0.4,
    "queueWait": 0.1,
    "readbackAndComposition": 122.8,
    "encoding": 86.3,
    "commit": 3.4,
    "total": 227.2
  },
  "lastEventId": 481
}
```

Input-plane format, dimensions, colour space, submitted bounds, orientation,
tonemap decision, and device generation are recorded when known. Timings are
diagnostic monotonic durations with a documented measurement boundary; they are
not substituted for GPU profiler measurements.

Hashes may be capability-controlled because large frame sequences can make
hashing material. If omitted, `hashState` explains `disabled`, `pending`, or
`failed`; absence must not ambiguously mean success.

## Settings contract and migration

Request-scoped capture descriptors never mutate settings. Settings actions use
an explicit scope:

- `runtime_session`: applies until restart or a later settings load.
- `persistent_user`: updates the feature settings and invokes the normal CSX
  persistence path. The receipt reports the file/save acknowledgement.

`settings_apply` defaults to no scope and is rejected until the caller states
one. This prevents an automation typo from silently becoming a permanent user
preference.

The canonical settings add `ScreenshotSettingsSchemaVersion` and a nested
sequence object:

```json
{
  "ScreenshotSettingsSchemaVersion": 2,
  "Enabled": true,
  "ScreenshotPath": "Screenshots",
  "SdrUsePng": true,
  "CopyToClipboard": false,
  "VRCaptureSource": "HMDSubmission",
  "VRFramedView": "Left",
  "VRFramedDominantEye": "Left",
  "Sequence": {
    "FrameCount": 30,
    "Schedule": {
      "Basis": "GameFrames",
      "IntervalFrames": 6,
      "PausePolicy": "Hold"
    },
    "BackpressurePolicy": "Skip",
    "FailurePolicy": "Continue",
    "SaveSeparateEyes": true,
    "PreviewVideo": {
      "Enabled": true,
      "FramesPerSecond": 15,
      "Required": false
    }
  }
}
```

Migration rules:

1. Continue reading `VRFramedEye` as the existing compatibility alias.
2. Read the current flat `SequenceFrameCount`, `SequenceFrameInterval`,
   `SequencePreviewFramesPerSecond`, `SequenceSaveSeparateEyes`, and
   `SequenceWritePreviewVideo` keys when the nested object is absent.
3. Nested version-2 values win when both forms exist.
4. Save the canonical nested object. Unified preset generation is updated in
   the same change so dead flat keys are not reintroduced.
5. Unknown future settings fields are preserved where the existing settings
   framework permits, but ignored by an older runtime.
6. Invalid persisted values fall back to documented safe defaults and produce
   one structured warning; they must not create an unbounded sequence.

The menu gains still and sequence controls using the same validator as the API.
It shows active request ID, counts, stop/cancel, most recent terminal outcome,
manifest path, and optional packaging outcome. Manual controls must never claim
success merely because a request was accepted.

## Error codes

Version 1 reserves these stable codes:

| Code | Typical phase | Retryable |
| --- | --- | --- |
| `invalid_request` | validation | No |
| `unsupported_contract_version` | validation | No |
| `idempotency_conflict` | validation | No |
| `feature_unavailable` | validation | Maybe after restart/build change |
| `feature_disabled` | validation | Yes after enable |
| `unsupported_capability` | validation | No for current process |
| `invalid_option` | validation | No until corrected |
| `unsafe_path` | validation | No until corrected |
| `capacity_exhausted` | acceptance | Yes |
| `source_busy` | acquisition | Yes |
| `source_unavailable` | acquisition | Maybe |
| `source_timeout` | acquisition | Yes |
| `device_changed` | staging | Yes on a new request |
| `gpu_stage_failed` | staging | Maybe |
| `encoder_backpressure` | queue | Yes |
| `readback_timeout` | encoding | Yes on a new request |
| `composition_failed` | encoding | Depends on requested fallback |
| `encode_failed` | encoding | Maybe |
| `unsafe_output_collision` | commit | No until destination changes |
| `write_failed` | commit | Maybe |
| `manifest_failed` | finalization | Maybe |
| `packager_unavailable` | packaging | No for current process |
| `packaging_failed` | packaging | Maybe |
| `cancelled` | any mutable phase | Caller-controlled |
| `internal_error` | any | Maybe |
| `cursor_expired` | event read | Yes via `request_get` |
| `request_not_found` | lookup | No or expired |

An error records its phase, whether retry is safe, field when applicable,
request/artifact/frame IDs, and structured details. Sequence frame errors do
not become parent errors unless the selected failure policy says so.

## Threading and lifecycle rules

1. DevBench parses JSON and performs non-game validation off the game thread.
2. Main-thread work is limited to safe feature state transitions and snapshots.
3. Render hooks acquire and stage only the current eligible source.
4. The artifact worker owns mapping, CPU image work, encoding, hashing, disk
   commit, and worker outcome publication.
5. The RequestJournal is thread-safe and never calls game or D3D APIs.
6. No MCP call blocks the game thread waiting for a future frame.
7. Worker outcome callbacks update the journal even when no client is connected.
8. On shutdown, stop accepting work, cancel source-waiting operations, drain
   committed writes for a bounded interval, and write partial sequence
   manifests. Never detach a worker that still owns feature memory.
9. On device replacement, invalidate unstaged source work. Do not reuse textures
   from a previous device generation.
10. Disabling capture cancels acquisition and sequence scheduling; committed
    encoder work remains owned until terminal.

## Obsolete compatibility behavior

The old `communityshaders.menu` action `{"action":"screenshot"}` is obsolete
and scheduled for removal after a migration window. While retained, it
delegates to `capture` with the current settings. Its response preserves the
existing `action`, `path`, and menu `status` fields, adds `delegatedRequest`
containing the screenshot request ID and acceptance receipt, and adds explicit
`deprecation` metadata naming `communityshaders.screenshot`, contract major 1,
action `capture` as the replacement. New clients must use
`communityshaders.screenshot` directly.

An obsolete-action response includes migration metadata alongside its normal
compatibility response:

```json
{
  "action": "screenshot",
  "delegatedRequest": { "ok": true, "request": { "requestId": "..." } },
  "deprecation": {
    "obsolete": true,
    "message": "communityshaders.menu screenshot is obsolete; migrate to communityshaders.screenshot contractMajor 1",
    "replacement": {
      "tool": "communityshaders.screenshot",
      "contractMajor": 1,
      "action": "capture"
    }
  }
}
```

Removal must not occur before at least one published migration release has
carried this notice. Before removal, repeat the repository and automation-tool
call-site audit for `communityshaders.menu` plus action `screenshot`, and record
the removal in the release notes. Absence of known clients is not, by itself, a
reason to skip the migration release.

The native screenshot settings button and screenshot hotkey use
`ScreenshotFeature::RequestUiCapture()`. That adapter dispatches through the
same public `CSX::ScreenshotAPI::Interface001` entry point as native mods. It is
not coupled to the obsolete menu action. Code requiring a receipt uses
`RequestApiCapture()` and its returned JSON or calls the versioned service
directly.

The legacy general CSAP vtable remains unchanged. Screenshot consumers query
the separately negotiated `csx.screenshot` service and use fixed-width POD
transport structures plus UTF-8 JSON. Do not append a broad
screenshot surface to the upscaling-oriented interface; screenshot evolves
independently behind its own service major.

## Testing requirements

### Pure/unit tests

- Contract version acceptance and rejection.
- Envelope and stable error-code serialization.
- Idempotent retry and conflicting-command detection.
- Capture descriptor normalization and capability validation.
- Source/view compatibility and explicit fallback.
- Path canonicalization, traversal rejection, sanitization, collision suffixes,
  and never-overwrite behavior.
- Still and sequence state-machine transitions.
- Stop, cancellation, partial completion, and irreversible artifact boundaries.
- Game-frame and wall-clock scheduling, pause, late slots, and frame counts.
- Backpressure skip/abort policies and fair interleaving with manual requests.
- Event ordering, cursor expiry, acknowledgement, and retention compaction.
- Legacy settings migration and nested-settings precedence.
- Sequence manifest recovery after interruption.

### Component tests

- Fake source provider with left/right plane arrival order, missing eyes,
  compositor-cycle changes, and timeout.
- Worker success and injected map, composition, encode, write, hash, and rename
  failures.
- Multi-output acquisition writes separate eyes and composition from one stage.
- Device-generation invalidation.
- Shutdown with waiting, queued, encoding, and finalizing operations.
- JSON golden files validated against the frozen request/response/manifest
  schemas.

### Live qualification

- Flat desktop mirror.
- VR desktop mirror, accepted-eye side-by-side, framed left, framed right,
  framed combined, and separate-eye outputs.
- Main menu, gameplay, loading-screen rejection/fallback, paused game, and menu
  open/closed.
- Null HMD for repeatable automation and physical HMD for actual compositor,
  pose, projection, and driver behavior.
- Hotkey, menu, DevBench direct tool, DevBench scenario dispatch, reconnect and
  idempotent retry.
- Burst stills and sequences at/over worker capacity with stable frame pacing.
- Feature disable, game exit, device reset, full disk, unwritable directory,
  and unavailable optional packager.
- Verify every claimed artifact by existence, size, decodability, dimensions,
  and receipt/manifest identity.

## Implementation order

1. Freeze JSON request, response, event, receipt, and sequence-manifest schemas;
   add golden contract tests.
2. Introduce IDs, normalized descriptors, RequestJournal, and pure state-machine
   helpers without changing capture output.
3. Wrap the current still pipeline in ScreenshotCoordinator; make worker success
   and every failure path publish structured outcomes.
4. Add `communityshaders.screenshot` capabilities, status, capture,
   request/event, cancellation, and acknowledgement actions.
5. Delegate the menu action and UI/hotkey to the coordinator.
6. Add SequenceScheduler, child frame receipts, backpressure/failure policies,
   partial and final manifests, and graceful stop.
7. Add multi-output/separate-eye artifact processing from one staged source.
8. Implement version-2 settings migration and full menu sequence controls;
   update unified presets in the same commit.
9. Add optional offline preview-video packaging behind capability discovery.
10. Complete null-HMD and physical-HMD qualification before freezing contract
    major 1 for external consumers.

## Definition of done

The screenshot API and sequence feature are complete when:

- no accepted operation can disappear without a terminal receipt;
- command retries cannot create duplicate captures;
- all current worker failures map to structured terminal outcomes;
- still capture retains current flat/VR output correctness;
- a sequence produces a recoverable frame set and final manifest without
  unbounded queues or render-thread encoding;
- separate-eye and composed outputs accurately report what was written;
- stop, cancel, disable, device loss, and shutdown have tested partial-result
  semantics;
- capabilities and version negotiation are authoritative;
- UI, hotkey, menu DevBench alias, and direct DevBench tool share one service;
- legacy sequence settings migrate and are no longer inert;
- contract schemas, tests, documentation, null-HMD qualification, and physical-
  HMD qualification agree with the shipped implementation.
