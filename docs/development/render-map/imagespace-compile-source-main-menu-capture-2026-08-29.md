# ImageSpace compile-source main-menu capture — 2026-08-29

## Purpose

This capture validates the distinction between an engine shader's runtime
loader alias and the HLSL compile source used by CSX. That distinction matters
for ImageSpace shaders because several named engine classes are variants of one
shared source file. Joining static and runtime evidence by the loader name would
either fail or create an invented one-file-per-class model.

The tested implementation records `compileSourceName` on both the typed engine
shader declaration (`shader-observation-v2`) and every selected stage object
alias (`stage-shader-observation-v3`). The graph builder prefers that exact
identity when joining runtime stage objects to the deterministic shader
manifest; the older loader-family join remains only as a compatibility fallback
for captures that predate the field.

## Runtime identity

| Field | Value |
|---|---|
| Skyrim | `SkyrimVR.exe` 1.4.15 |
| Runtime route | Valve null HMD, independently head-pose qualified |
| Scene | Rendered `Main Menu` |
| CSX source commit | `b223489b6b2afb5441409b3e17e826ff964efb50` |
| CSX source describe | `v3.19.0-pr5-6-gb223489b6-dirty` |
| CSX build ID | `c4deefae349940299901a11cd27692961f1a2789a51411835671f3137e6526e9` |
| Deployed DLL SHA-256 | `C454BB8AB1292FEB31E16B3984F185D3736D611549F7AF8E523AC2A8E3506CCA` |
| Shader-cache ABI | `85f71f8f713542badba1027e6216e07f42a762782165d185f004e4c14af1d5c6` |
| Compiler identity | `d3dcompiler_47.dll:10.0.26100.9168` |
| Cache startup result | Reused managed pack; `contracts-match`; no blanket rebuild |

The binary was intentionally built from a dirty evidence-development tree. The
exact build ID, source state, DLL hash, cache ABI and compiler identity are
therefore retained rather than representing the binary as the clean source
commit alone.

The before/after cache inventory confirms that reuse was real. Both optimized
pack files and both empty developer-pack headers remained byte-identical. Only
`Info.ini` changed, replacing the prior build ID with this run's build ID while
preserving its size. A DLL build-identity change therefore did not, by itself,
invalidate or rewrite compiled shader payloads when the cache contracts still
matched.

## Capture bounds and result

Capture `capture-live-0000f169bae512b0-2` requested only
`shader-observed` and `stage-shader-observed` events. It accepted 43 unique
typed declarations:

- 12 engine shader observations;
- 31 vertex or pixel stage-shader observations;
- zero dropped events or typed observations;
- zero boundary, scope, stop-race or overflow rejections;
- no truncation.

The selector filtered 3,990,112 unrelated calls. This is expected filtering,
not loss. The capture was explicitly stopped after its useful declarations had
stabilized.

Capture-start provenance also froze the optimized/VR compile mode, empty global
define text, compiler flags, cache ABI, compiler identity, global compile-state
digest and the complete empty external-compatibility registration set. Static
shader-manifest and engine-map paths were not supplied to the in-process
controller, so the capture manifest correctly marks those inputs unavailable.
The offline graph derivation records and hashes the exact static inputs it used.

## Observed shared-source joins

| Runtime loader alias(es) | Effective compile source | Static compile unit |
|---|---|---|
| `ISHDRDownSample4`, `ISHDRDownSample4RGB2Lum`, `ISHDRDownSample16Lum`, `ISHDRTonemapBlendCinematic` | `ISHDR` | `compile-0088` — `package/Shaders/ISHDR.hlsl` |
| `ISBrightPassBlur15`, `ISBlur15` | `ISBlur` | `compile-0079` — `package/Shaders/ISBlur.hlsl` |
| `ISTemporalAA_Water` | `ISTemporalAA` | `compile-0104` — `package/Shaders/ISTemporalAA.hlsl` |
| `ISCopy` | `ISCopy` | `compile-0081` — `package/Shaders/ISCopy.hlsl` |
| `ISSimpleColor` | `ISSimpleColor` | `compile-0102` — `package/Shaders/ISSimpleColor.hlsl` |

The same mechanism also joined ordinary `Utility`, `Effect`, and `Lighting`
stage objects to `compile-0120`, `compile-0074`, and `compile-0115`
respectively. Across all 31 stage objects the graph produced 31 confirmed
`implemented-by` edges, all with `joinBasis: compile-source`, and no ambiguity.

This result proves the identity route for the observed main-menu shader set. It
does not assert that unobserved ImageSpace classes use a particular source; each
additional alias still requires static or runtime evidence.

## Derived graph

The graph built from the retained capture contains:

- 52 nodes: 12 engine shaders, 31 pipeline states, eight shader compile units,
  and one shader-compilation context;
- 62 edges, including the 31 confirmed compile-source joins;
- no ambiguity groups;
- two explicit gaps.

The gaps are intentional consequences of the narrow shader-only selector: no
typed resource declarations were requested, and exact view-subresource overlap
and D3D11's final hazard-resolution result remain outside this slice. Neither
gap weakens the shader-to-compile-unit proof.

## Retained evidence

Authoritative evidence is retained under:

`L:/Codex/evidence/render-map/20260829-compile-source-live`

The capture-specific directory is:

`L:/Codex/evidence/render-map/20260829-compile-source-live/capture-live-0000f169bae512b0-2`

| Artifact | SHA-256 |
|---|---|
| `capture-manifest.json` | `6D499D484FCD96DF3029CAFDF0F658EB50F1201D432AE45D22E7BDB2D08E1541` |
| `events.jsonl` | `EDC2100E33A1B0571CA1FFB4C30032BAACBCA9547B80CAFFCA2337F9ECE91C1A` |
| `render-graph.static-joined.json` | `8BAD8A6FC40DC0214FB46AD915C235805D229DFCBA361B54CD86860A16D10A5A` |
| Static shader manifest used by the graph | `E6F6F0BDBF38916F43FE1975363E29EA2EEA234F101C1F3EB1146C9F89C68522` |
| Skyrim VR engine map used by the graph | `7A574BE254AF43DD390D31BBE54CBA4137208DE07713E614EF8FAB757132C553` |

The capture artifacts were copied from CSX's durable finalization directory and
their hashes were verified against the stop response before analysis.
