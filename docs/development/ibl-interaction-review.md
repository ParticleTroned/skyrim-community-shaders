# CSX Image Based Lighting interaction review

Status: focused static review

Snapshot: `debug/vr-followup-20260816` at `4f7f0db58eb37493d37e23602a4b4ab3c667d95b`

Review date: 2026-08-17

## Purpose

This document records the focused Image Based Lighting (IBL) review that
followed the first-pass shader and feature interaction survey. It distinguishes
IBL's local implementation contract from the wider ambient-light architecture
that remains unresolved.

This is a documentation record only. No shader or runtime behaviour was changed
while producing it.

## Intended contract

IBL is intended to replace Skyrim's ambient lighting with physically based
diffuse ambient radiance derived from cubemap spherical harmonics. It is not
described as an additional lighting effect layered indiscriminately over the
engine ambient term.

The implementation combines several sources and policies:

- environment radiance supplied by Dynamic Cubemaps;
- sky radiance derived from Skyrim's reflection cubemap;
- DALC matching to retain authored ambient brightness and tint;
- static cubemaps for inventory and other out-of-world objects;
- optional fog-colour integration and weather-specific overrides.

`features/IBL/Shaders/IBL/IBL.hlsli` describes `GetDiffuseIBL` as the full
diffuse ambient replacement. That replacement claim is the most important
semantic constraint for later composition work.

## Operational status

IBL is compiled and packaged as a core feature, but that does not currently
mean it is recommended for general activation:

- it defaults to disabled;
- it defaults off in interiors, the world map, and loading screens;
- the reviewed MGO AMD and NVIDIA presets explicitly disable `EnableIBL`;
- the installation guidance describes it as not fully ready for general use.

The useful distinction is therefore:

- **distribution status:** core, compiled, and shipped;
- **operational recommendation:** experimental and normally disabled.

## Producer and consumer reach

IBL is architectural rather than an isolated optional pass. Its data or
results reach ordinary Lighting, Effect, Water, Grass, DistantTree, the SSAO
composite, shared shadow/ambient sampling, and the deferred composite.

| Participant | Relationship to IBL | Unresolved contract |
| --- | --- | --- |
| Dynamic Cubemaps | Supplies environment radiance and reflection inputs. | Which texture owns diffuse radiance versus specular environment response, and when does each refresh? |
| Skylighting | Supplies ambient visibility and directional occlusion. | Is skylight a visibility multiplier for IBL, a separate radiance source, or both in different modes? |
| Screen Space GI | Supplies screen-space AO, diffuse GI, and optional specular GI. | Which terms add to IBL and which replace or attenuate it without double-counting? |
| Vanilla DALC | Provides authored ambient colour/brightness used by IBL matching modes. | Where does the vanilla ambient contribution end and the replacement begin? |
| Deferred composite | Converges IBL, skylight, SSGI, cubemap, and material data. | The intended composition order and energy policy are not declared as one contract. |
| Material shader families | Consume diffuse and specular environment terms through several paths. | Deferred and forward/material paths must agree on visibility, gamma, roughness, and context exclusions. |
| Adaptive Brightness / Linear Lighting | Adjust scene-lighting policy and colour handling around the same result. | Separate physical scene energy from artistic exposure or display adjustment. |

## Context and lifecycle exclusions

IBL has accumulated explicit restrictions because one per-frame ambient state
does not describe every Skyrim rendering context:

- sky IBL is disabled in interiors;
- runtime IBL is suppressed in world-map and loading contexts;
- static cubemap fallback state is validated and hardened;
- Dynamic Cubemaps being disabled must not leave IBL with invalid ownership or
  resources;
- main-menu and loading-model draws can use engine-authored per-draw ambient
  state while CSX's captured spherical-harmonic state is shared per frame.

A retained release-work commit, `15bbdf23c`, preserves Skyrim's authored
main/loading-menu lighting by preventing static IBL from being applied there.
That behavioural change is not part of this reviewed baseline and should be
assessed separately before integration.

## Historical evidence

Repository history shows a coherent feature still undergoing substantial
stabilisation:

- IBL was originally labelled unfinished, removed from core, then later
  returned and shipped as core;
- a crash when Dynamic Cubemaps was disabled was fixed;
- invalid settings and static fallback state were hardened;
- map and loading contexts acquired explicit suppression;
- environment specular calculations were unified across deferred and material
  paths;
- mode 3 specular was corrected after view-, normal-, and roughness-dependent
  visibility darkened a broad ambient cubemap contribution and could become
  eye-sensitive in VR;
- DALC/water paths were corrected where skylighting visibility was applied
  twice;
- diffuse gamma conversion has previously been lost and restored;
- menu/loading lighting needed special handling because engine per-draw state
  and CSX per-frame captured state have different lifetimes.

The performance audit also identifies DALC mode 3 plus Skylighting as a
credible conditional GPU cost: it can add another spherical-harmonic
visibility evaluation across high-coverage material and deferred paths.

## Diagnostic distinction

The recent "stale IBL" diagnostic was a package/version mismatch, not evidence
that enabled IBL caused the observed visual failure:

- the diagnostic DLL required feature version `1.1.3`;
- installed package/cache metadata still reported `1.1.2`;
- the reviewed HLSL content was byte-identical;
- CSX rejected the mandatory feature marker, retained the disk cache, and
  compiled memory-only.

That mismatch prolonged shader readiness and affected the first-load
investigation. It did not establish an IBL lighting defect.

## Current conclusion

IBL has a coherent local purpose: replace vanilla diffuse ambient radiance with
cubemap-derived spherical-harmonic lighting while preserving selected authored
colour and brightness cues.

The unresolved problem is the system-level hand-off. The codebase does not yet
state one complete composition contract for IBL, Skylighting, SSGI, Dynamic
Cubemaps, DALC, material environment response, and menu/out-of-world rendering.
Until that exists, local fixes can remain correct while combinations still
double-count radiance or visibility, apply the wrong lifetime, or diverge
between deferred, forward, and VR eye paths.

## Recommended next questions

1. Assign explicit ownership of diffuse ambient radiance, ambient visibility
   and AO, and specular environment response.
2. Record the composition operator for every pair: replace, add, multiply,
   blend, or conditional fallback.
3. Map the forward/material and deferred paths side by side and verify that
   gamma, visibility, roughness, and fallback conventions match.
4. Define context ownership for world, interior, inventory, main menu, loading
   screen, and world map rather than treating exclusions as isolated guards.
5. Define VR obligations for any view-dependent specular visibility and confirm
   that broad ambient terms cannot become eye-sensitive.
6. Profile DALC mode 3 plus Skylighting separately from visual correctness.

## Primary evidence locations

- Feature summary and settings: `src/Features/IBL.h` and `src/Features/IBL.cpp`
- Diffuse IBL implementation: `features/IBL/Shaders/IBL/IBL.hlsli`
- Shared consumers: `package/Shaders/` and `package/Shaders/Common/`
- Deferred convergence: `src/Deferred.cpp` and the deferred composite shader
- General interaction map: `docs/development/shader-feature-interaction-survey.md`
- Installation status: `Community-Shaders-Expanded-Installation.md`
- Preset defaults: `MGO-Presets/`
