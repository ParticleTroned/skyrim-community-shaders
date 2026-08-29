# Capture provenance request contract

## Objective

Allow a bounded render-map capture to name the exact static shader manifest,
Skyrim engine map, CSX build, runtime route, MO2 profile, shader-cache
inventory, and scenario that produced it without converting caller assertions
into runtime-observed facts.

The hot collector does not own this metadata. The DevBench bridge validates and
freezes one immutable artifact context before starting the collector, then the
artifact writer copies that context into the completed capture manifest.

## Start request

`communityshaders.render_map` retains the existing `start` request and accepts
one optional `provenance` object:

```json
{
  "schema": {
    "name": "csx.render-capture-provenance-request",
    "major": 1,
    "minor": 0
  },
  "expectedProducer": {
    "sourceCommit": "40 hexadecimal characters or null",
    "buildId": "exact build identifier or null",
    "dllSha256": "64 hexadecimal characters or null"
  },
  "inputs": {
    "shaderManifest": {
      "availability": "verified",
      "path": "absolute local path",
      "sha256": "64 hexadecimal characters",
      "schemaMajor": 1
    },
    "engineMap": {
      "availability": "verified",
      "path": "absolute local path",
      "sha256": "64 hexadecimal characters",
      "schemaMajor": 1
    },
    "csxBuildManifest": {
      "availability": "verified",
      "path": "absolute local path",
      "sha256": "64 hexadecimal characters",
      "schemaMajor": 1
    }
  },
  "environment": {},
  "scenario": {}
}
```

The complete `environment` and `scenario` shapes remain those defined by the
capture-manifest schema. The bridge normalizes missing optional provenance to
the current `unknown`/`unavailable` defaults; it never guesses a profile,
runtime route, save, cell, weather, preset, or cache identity.

## Evidence classes

The normalized manifest continues to expose the convenient `inputs`,
`environment`, and `scenario` fields. Its `extensions.csx.provenance` object
records how every supplied value was established:

| Class | Meaning |
|---|---|
| `server-verified` | The bridge independently checked the exact value before capture start. |
| `runtime-observed` | CSX obtained the value from the running process/runtime. |
| `caller-asserted` | The orchestration client supplied the value; CSX did not independently prove it. |
| `unavailable` | No value was supplied or observable. |

Environment and scenario fields are caller assertions unless a named runtime
probe verifies them. A caller-supplied hash does not become `server-verified`
merely because it is syntactically valid.

## Verification rules

For an input with `availability: verified`, the bridge must, before starting
the collector:

1. require an absolute, existing regular-file path;
2. stream and compare SHA-256 without returning file contents;
3. parse the JSON document and compare `schema.major`;
4. reject a mismatch atomically; and
5. record the normalized absolute path, uppercase digest, schema major, and
   `server-verified` evidence class.

`unverified` may preserve a caller-supplied path or identity, but the derived
graph must not use it for a hard static join. `unavailable` requires null path,
digest, and schema major.

`expectedProducer` is a guard, not metadata decoration. Any supplied source
commit or build ID must match CSX's own build provenance. A supplied DLL hash
must match the loaded module file when that file can be read. A mismatch fails
before capture allocation so the command cannot accidentally gather evidence
from the wrong build.

## Safety and compatibility

- `provenance` is optional. An old client receives the existing honest
  unavailable/unknown manifest values.
- Unknown provenance major versions are rejected. New minor fields are ignored
  only when their semantics are optional and preserved in the normalized
  receipt.
- Strings and notes have explicit byte bounds; paths are normalized but never
  searched for heuristically.
- Validation is all-or-nothing. The bridge does not start a capture with a
  partly accepted `verified` set.
- The request and normalized verification receipt are immutable once the
  collector starts.
- The start response returns the normalized provenance receipt so orchestration
  can reject a downgraded or unavailable field immediately.
- Top-level capture-manifest compatibility is preserved by recording detailed
  evidence-class metadata under `extensions.csx.provenance`; no new required
  top-level member is added in schema major 1.

## Static-join rule

The offline graph builder may create `shader-manifest` and `engine-map`
`sourceRefs` only when:

- the corresponding capture input is `verified`;
- its verification class is `server-verified`;
- the exact retained file hash matches the capture manifest; and
- the runtime event explicitly names the stable ID being joined, or an
  independently documented deterministic mapping produces it.

Merely supplying the manifest and engine-map files as graph inputs does not
authorize the join. This preserves the boundary exposed by the retained
Breezehome semantic projection.

## Runtime-observed shader compilation identity

Capture-manifest 1.6 adds the optional
`extensions["csx.shaderCompilation"]` snapshot. The bridge freezes it before
the collector starts; no draw or dispatch hook reads configuration state.
When CSX runtime state is available, the snapshot records:

- the embedded shader-cache ABI and loaded D3D compiler identity;
- optimized/debug mode, VR mode, partial-precision and avoid-flow-control
  compiler flags;
- the exact accepted global shader-define text and its cache-path suffix;
- the exact XXH3-128 global compile-state digest used by managed-pack lookup;
  and
- the compatibility registry phase, revision, set digest, and a complete
  structured copy of each registration, its canonical identity, and scopes.

The global digest deliberately excludes the shader-specific compatibility
requirement set. Offline analysis derives that set by applying the retained
registration scopes to an observed shader family/source, then combines its
canonical digest exactly as the runtime cache does. A capture must not call the
global digest a complete bytecode recipe without that shader-specific step and
the source-closure digest from the shader manifest.

If shader state is not initialized, the extension remains present with
`availability: unavailable`; the bridge does not substitute build defaults.
The compatibility registration list is marked `complete` only when every
entry named by the frozen snapshot was copied successfully.

Capture-manifest 1.7 also records independent catalogue bounds and completion
counters for scene objects, geometries, and material-state revisions. These
are structural identity limits: exceeding one makes the capture incomplete,
because a later setup or draw must never be silently joined to an older
observation. Render-event 1.9 declares those identities before their first
`geometry-boundary-v2` consumer.

Render-event 1.10 adds bounded runtime texture bindings to each material-state
revision. Each non-null resource reference is an exact capture-local
observation declared before the material event. The recorded binding index is
only a runtime material-list position; shader-register semantics remain
unproven until a later pipeline-binding correlation slice.

Render-event 1.14 adds `resource-cpu-access-v1` for immediate-context Map/Unmap
boundaries. It records resource and subresource identity, map type and flags,
HRESULT, pitches, measured QPC duration, and capture-local pairing. It never
records the mapped address or mapped bytes. A successful readable Map is valid
evidence that the allocation was CPU-readable after return; it does not prove
which bytes the application consumed. A matched writable Unmap proves D3D11
publication after return; it does not prove a later shader read. Failed Maps
and unmatched Unmaps must remain negative/incomplete evidence and must not be
promoted into resource-version edges.

## Performance-context extension

Identity and ordering captures remain useful when frame pacing is abnormal,
but a shader or feature cost comparison does not. Timing-sensitive capture
reports therefore use the optional
`capture-manifest.json` extension `extensions.csx.performanceContext`:

```json
{
  "observedFps": null,
  "frameTimeDistributionMs": null,
  "configuredCapFps": null,
  "presentInterval": null,
  "compositorRefreshHz": null,
  "framePacingMode": "unknown",
  "reprojectionMode": "unknown",
  "externalLimiter": "unknown",
  "gpuUtilizationPercent": null,
  "bottleneck": "unknown",
  "captureLoad": [],
  "shaderCompilationActive": null,
  "loadingOrMenuState": "unknown",
  "timingEvidenceUsable": false,
  "invalidationReasons": ["unexplained-fps-throttle"]
}
```

The namespace is optional in schema major 1 and preserves old producers. A
timing report must set `timingEvidenceUsable: false` when observed FPS is
materially below both the configured cap and demonstrated GPU capability and
no compositor, reprojection, driver, external limiter, capture overhead, CPU
stall, loading, or shader-compilation cause has been established. Structural
identity, resource, and command-order facts from such a capture remain
eligible; absolute and comparative performance claims do not.

A screenshot overlay may corroborate observed FPS, headset route, or runtime
mode, but it does not independently prove limiter configuration, GPU
saturation, or the absence of reprojection.

## Implementation order

1. Add bounded parsing and verification helpers with isolated unit tests.
2. Add the optional start-request object and normalized receipt; bump the
   render-map service minor and schema revision.
3. Freeze the verified artifact context before collector start and discard it
   if start fails.
4. Emit `extensions.csx.provenance` in the capture manifest and return the same
   receipt from `start`.
5. Add a guard test for producer/hash/schema mismatch and a legacy-start test.
6. Build and run the existing controller/artifact tests.
7. Perform one bounded live capture with all three static inputs verified.
