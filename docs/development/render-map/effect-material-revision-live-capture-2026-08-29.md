# Effect material revision — Breezehome live capture — 2026-08-29

## Result

A bounded 120-frame Effect-only capture found no live material-state revision
in the static Breezehome fixture. It did establish a useful lifetime boundary:
68 `BSEffectShaderProperty` material identities remained stable and were each
used once per frame, while dynamic particle geometry produced 663 observation
identities across only 15 geometry labels.

The result does not prove Effect materials are immutable. It proves that the
currently fingerprinted material fields do not change in this scene while the
engine creates, replaces, or recatalogues particle geometry. A controlled
property mutation remains the correct positive revision test.

## Runtime provenance

| Item | Exact value |
|---|---|
| capture | `capture-live-0000f73bee14de68-2` |
| Skyrim runtime | `SkyrimVR.exe` 1.4.15 |
| CSX source commit | `b223489b6b2afb5441409b3e17e826ff964efb50` (dirty working tree) |
| deployed DLL SHA-256 | `1CF7F546C97FF0B0C2A7F25EE28831C74992878E8232C39FD07A2398C57C0D69` |
| DevBench build ID | `52deabbf1b36592cdfec5c1564bfc9650715ddda31ba19619c7a02440767e0b0` |
| fixture | verified Breezehome interior save, `Save2_2FAD4605_0_507269736F6E6572_WhiterunBreezehome_000005_20260822173026_1_1` |
| VR route | Valve null HMD plus qualified Codex head-pose driver |
| shader family selector | engine shader type `7` (`BSShader::Type::Effect`) |

The deployed artifact hash was verified by the DevBench controller before each
render-map operation. The loaded producer reported optimized VR compilation,
shader-cache ABI
`85f71f8f713542badba1027e6216e07f42a762782165d185f004e4c14af1d5c6`,
and compiler `d3dcompiler_47.dll:10.0.26100.9168`.

## Bounds and completion

The request retained object, geometry, material, and geometry-setup events
only, with `executionWithinSelectedGeometry: true`. It was bounded to 120
frames, five seconds, 65,536 events, and 32 MiB. Completion was `frame-limit`:

| Measure | Result |
|---|---:|
| accepted events | 17,084 |
| CPU frames | 120 (`15981` through `16100`) |
| dropped events | 0 |
| truncated | false |
| scope overflow / mismatch | 0 / 0 |
| scene objects | 33 |
| geometry observations | 663 |
| material-state observations | 68 |
| filtered events | 4,595,840 |

The retained stream consists of 33 object declarations, 663 geometry
declarations, 68 material declarations, 8,160 setup begins, and 8,160 balanced
setup ends. Filtering is selector work, not evidence loss.

## Material and geometry findings

All 68 materials are `BSEffectShaderProperty`, engine material type `1`,
material type `1`, and feature `0`. They expose four alpha values (`1.0`,
`0.8`, `0.5`, and `0.85`), nine flag combinations, and 67 hash-key values.
Every material observation has `stateRevision: 1`; no shader-property pointer
has multiple fingerprints or revisions.

Every declared material is referenced by exactly 120 setup scopes. The stable
material count therefore is not caused by unused catalogue entries. In
contrast, five particle labels (`pGreenMist`, `smoke07`, `CandleFlame01`,
`CandleGlow01`, and `pSmallSparks03`) each acquired 120 geometry declarations.
This is direct evidence that a changing geometry observation identity does not
imply a changing material identity.

The graph model now represents those concepts separately:

- `geometry` is the capture-local observed engine geometry identity;
- `geometry-setup` is the temporal invocation scope that binds an observed
  geometry and exact material-state revision into rendering.

This distinction preserves the engine's particle lifetime behaviour without
mislabeling 8,160 setup invocations as distinct geometry assets.

The derived graph contains 8,925 nodes and 16,501 evidence-bearing edges:

| Graph item | Count |
|---|---:|
| scene-object nodes | 33 |
| observed geometry nodes | 663 |
| temporal geometry-setup nodes | 8,160 |
| material nodes | 68 |
| shader-compilation-context nodes | 1 |

## Retained evidence

Authoritative directory:
`L:/Codex/evidence/render-map/20260829-effect-material-revision`

| Artifact | SHA-256 |
|---|---|
| two-frame baseline manifest | `EC9EEA84C2E99A802DAC5F0BEE8E06B7D37F865FADE6F7FDBABCA2C5B9B7C322` |
| two-frame baseline events | `0987AB9E7052E161D6067561284C133477BACE79CA904D43B05DB75A65506F03` |
| 120-frame manifest | `90F3BD356F7E84A7B6CB2E3CF3753D779E433F68903F01CC538C423F87A5AF57` |
| 120-frame events | `862B85C33B162AD5C6117759017418CB6CA4F2397A2485F4607821B6E1CB4198` |
| 120-frame derived graph | `9618D59D27577F4F8F11ADE8758EBA2FF5BE9806A3348609944FFB9DBE2F7E72` |

The generated graph and exact DevBench binding/invocation receipts are retained
beside the captures. Large capture artifacts remain evidence and are not
committed to Git.

## Next gate

The positive revision gate requires one deliberately bounded material-property
change with before/after captures. It must prove the same shader-property
identity receives a higher revision and a new fingerprint, then prove that the
following setup and draw project the new exact material-state observation. A
natural animation window is not a substitute when the fingerprinted fields do
not change.
