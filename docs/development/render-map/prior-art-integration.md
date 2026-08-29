# Prior-art integration and spot-check plan

The prior-art catalogue is a candidate-evidence layer between public rendering
work and the version-specific Skyrim VR engine map. It exists to reuse good
reverse engineering without allowing a familiar name from another executable
or shader fork to become an accidental fact about this target.

The current target identity is recorded in
[`prior-art-catalog.json`](./prior-art-catalog.json): Skyrim VR 1.4.15.0 with
the pinned executable hash, CSX 3.19-VR at the recorded source revision, and
the exact generated shader-manifest hash. Each source independently records:

- repository, branch, and full commit SHA;
- declared or inferred Skyrim product and runtime;
- shader-system name, version, and revision where applicable;
- source lineage without treating a fork as behavioural equivalence;
- candidate claims with commit-pinned file and symbol locators;
- target verification state, confidence, spot checks, and reuse decision.

`unknown` and `null` are intentional provenance values. A missing runtime is
not filled from memory or inferred from a current mod release.

## Promotion boundary

A public claim does not enter an `engine-map` merely because it is detailed or
agrees with another public source. Promotion requires a target spot check
against the catalogue's exact Skyrim VR executable and CSX shader identity.
The spot check must produce or cite target evidence, update the claim's target
assessment, and add a separately reviewed engine-map entity or relation.

The usual progression is:

```text
unverified/candidate prior claim
  -> targeted Skyrim VR static or runtime check
  -> matched, partial, or mismatched spot-check record
  -> corroborated claim when warranted
  -> separately reviewed engine-map fact
```

Partial matches remain partial. A contradiction is retained alongside the
source claim; it is not erased by changing the claim text after the fact.

## Efficient spot-check order

The catalogue saves work by giving each slice a concrete hypothesis and source
location. The recommended order is based on expected mapping value per unit of
verification work:

| Priority | Candidate | Target check | Useful result |
|---|---|---|---|
| 1 | `aers/skyrim-rendering` scene accumulation | Identify the Skyrim VR 1.4.15 scene-list builder and compare the shadow-scene-node inputs, worker arrays, and portal/cell distribution | Establishes the upstream half of the scene-to-draw graph and gives depth culling stable producer boundaries |
| 2 | `aers/skyrim-rendering` culling flow | Match `BSCullingProcess`/`NiCullingProcess` types, vtables, flags, and call boundaries in the VR PDB/disassembly, then capture a bounded traversal | Reuses a detailed culling hypothesis while separating AE-only details |
| 3 | `Nukem9/skyrimse-test` batch renderer | Compare Begin/End/RenderBatches/pass-list control flow against Skyrim VR symbols, disassembly, and existing render-pass events | Names the central accumulation-to-technique bridge without rebuilding its shape blindly |
| 4 | Nukem shader families and constants | Select one already observed family and descriptor pair; compare its tables, constant slots, setup calls, and render-target roles | Measures exactly which SE tables transfer to VR before expanding family by family |
| 5 | Upstream Community Shaders and Shader Tools lineage | Diff only the target files owning a current CSX route before using upstream behaviour | Avoids rediscovering retained architecture while catching VR/fork divergence |
| 6 | Extension-owned render graphs | Reuse graph API/lifetime ideas only after native Skyrim evidence is represented | Improves our model without importing extension passes as engine facts |

The first spot checks should therefore focus on scene accumulation and culling,
then join those observations to the render-pass/technique/resource graph already
captured. This closes a larger missing section than enumerating another shader
family in isolation.

## Source exclusions

Generated summaries such as DeepWiki may be useful search indexes, but they are
not catalogue evidence unless every imported claim is traced back to a pinned
primary source. RenderDoc documentation explains capture semantics and the
tool's API; it is not a map of Skyrim's renderer. Per-frame RenderDoc captures
remain target runtime evidence under the capture contract, not public prior
art.

## Updating the catalogue

1. Pin a source to a full commit SHA before extracting claims.
2. Record engine and shader applicability from the source itself. Use
   `basis: unknown` where it does not say.
3. State one testable architectural claim per catalogue item and use
   commit-pinned locators.
4. Set target status from target evidence only. Similar names are not a match.
5. Add target references only when the referenced entity or manifest record is
   genuinely relevant; an empty reference list is preferable to a speculative
   join.
6. Run `tools/test-render-map-contracts.ps1` to validate source lineage, claim
   references, target joins, and spot-check evidence.
7. Promote a corroborated claim in a separate engine-map change so provenance
   review remains visible.

