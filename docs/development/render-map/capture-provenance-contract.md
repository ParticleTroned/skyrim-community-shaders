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

