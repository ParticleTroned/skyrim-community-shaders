# SE/AE default settings alignment

Source review date: 2026-09-30.

-   Reference: `cs-1.7-PL-SE` at
    `734435e158c91438812ff96314645f2c69a8b8db`.
-   Main-VR baseline before this change:
    `8c06b897ba529e483a0bc5f129ccf65fd9af4226`.
-   Scope: built-in SE/AE defaults for equivalent settings and techniques.
    Preserve current VR defaults, saved choices, and later correctness fixes.
    Skylighting shares the VR Balanced defaults on every runtime, as requested.
    This is a source comparison, not a visual or performance equivalence claim.

## Defaults changed

| Setting                                         | SE/AE default                | Preserved VR default         |
| ----------------------------------------------- | ---------------------------- | ---------------------------- |
| SSGI enabled                                    | On                           | Off                          |
| SSGI resolution                                 | Half resolution              | Full resolution              |
| SSGI AO power                                   | 1.0                          | 1.8                          |
| SSGI distance culling                           | 0, disabled                  | 1500 units                   |
| Dynamic cubemap resolution                      | 256                          | 128                          |
| Complex grass settings override                 | Off                          | On                           |
| Basic grass brightness                          | 1.0                          | 0.75                         |
| LOD water reflection strength override          | On                           | Off                          |
| Alternate sun path                              | Off                          | On                           |
| Minimum shadow elevation                        | 10 degrees                   | 0.25 degrees                 |
| Skylighting probe grid                          | 192 x 192 x 96               | 192 x 192 x 96               |
| Skylighting field width                         | 4096 x 3.1666667 world units | 4096 x 3.1666667 world units |
| Skylighting incremental updates / stable slices | On / 11                      | On / 11                      |
| Skylighting reduced update frequency            | On                           | On                           |
| Skylighting occlusion / probe intervals         | 6 / 13 frames                | 6 / 13 frames                |
| Skylighting probe weighting                     | Normal-based                 | Normal-based                 |
| Base and human skin blur radius                 | 1.0                          | 0.5                          |
| Terrain blend strength                          | 1.0                          | 0.5                          |
| FSR sharpening                                  | 0.0                          | 0.9                          |
| DLSS sharpening                                 | 0.5                          | 0.9                          |
| Heat refraction scale                           | 0.5                          | 0.25                         |

SE/AE SSGI retains GI enabled, four slices, eight steps, temporal
denoising and blur enabled, and GI strength 1.0. Adaptive sampling remains
enabled as requested. VR retains its existing AO-only resource profile,
GI disabled, three slices, six steps, and denoising and blur disabled.
Distance culling is exposed only in VR; SE/AE already uploads zero to the
shader, and its stored default now also explicitly resolves to zero.

Skylighting is an explicit exception to the SE reference alignment. All
runtimes use the existing Balanced defaults: a 192 x 192 x 96 grid,
3.1666667-world-cell field width, 11-slice incremental updates, reduced update
frequency and normal-based probe weighting. The configured occlusion/probe
intervals are 6/13 frames; incremental updates follow fresh occlusion
quadrants.
The shared distance clamp and UI range remain 2.5 to 8 cells. Named
presets and their independent enable switch retain their existing behavior.

## Loading, resetting and VR isolation

Runtime-specific defaults use `REL::Module::IsVR()` at the existing
initialization sites. Both SE and AE take the flat branch, while
Skylighting uses one shared settings initializer. Feature JSON
deserialization and restore-default operations consume these same defaults;
explicit saved values still take precedence. No user settings files are
rewritten or migrated to force the new values.
Advanced heat refraction retains its existing partial-load behavior: an
omitted key preserves the current value, while fresh initialization uses
the runtime default.

LOD Blending's separately stored water-reflection toggle uses one private
runtime-default helper during construction, missing-key loading and reset.
Dynamic Cubemaps initializes its active resolution and mip count from
the settings and uses the runtime default for invalid resolutions, retaining
its existing allocation lock and restart behavior. Skylighting keeps its
existing normalization and resource rebuild paths.

The diff adds no new settings keys, resources, shader permutations or
DevBench actions. All modified runtime branches retain the pre-change VR
values. Skylighting's Balanced preset is the default on every runtime.
Skin scattering defaults to Burley with 16 samples on both runtimes. The
aligned blur radius belongs to Separable SSS and does not affect Burley.

## Adversarial review and corrections

Reviewed the combined changes from `7d7614c0359ed67819f9985a0af81e4de83d82f1`
and `24620878798b7d212a64cfff30142da2456b051a` for scope, correctness,
robustness and duplicated policy. Source inspection found and corrected:

-   Partial skin profiles did not inherit their enclosing defaults. The
    profile deserializer created an uninitialized fallback object, so
    omitting fields such as `BlurRadius` could read indeterminate values.
    Loading now recursively overlays saved settings onto the serialized
    runtime defaults before strict profile deserialization. Base and human
    falloff defaults remain distinct, explicit values survive, and the same
    helper covers configuration loading and measurement-state restoration.
-   Replacing skin settings could retain kernels from an earlier profile.
    Loading, restoring defaults and measurement-state replacement now mark
    the existing kernel cache dirty. The normal reset path rebuilds it;
    shader dispatch and resource ownership are unchanged.
-   The LOD water-reflection default was repeated in construction, loading
    and reset. One feature-local helper now owns that runtime decision.

The skin loading and cache issues predate the alignment but prevent its
defaults from being applied consistently. Their corrections cover SE, AE
and VR without changing valid saved values or VR default values. No other
rendering implementation was imported from the SE reference. Skylighting
source and its isolated harnesses are unchanged against the baseline.

Manual source review covered empty and partial feature settings, explicit
false/zero values, the two skin profile identities, legacy human controls,
invalid cubemap resolutions, allocation locking, and SE/AE versus VR
initialization. Malformed skin profile types retain the existing feature
loader's exception-to-default handling. No test harness was executed.

## Reviewed defaults retained

The audit compared shared feature settings, renderer and cache settings,
menu settings, JSON fallback values and restore-default paths. Matching
settings were left alone. In particular:

-   The six bundled theme `Theme` payloads match the SE reference. The prior
    flat menu fix already supplies automatic 21/28/42 px sizing at
    1080p/1440p/2160p. Hardcoded fallback palettes differ, but copying them
    would replace the shipped theme policy rather than correct the effective
    default theme.
-   Shared Screen Space Shadows defaults already resolve to the SE values.
    Adaptive Balance's neutral profile and Volumetric Lighting's default
    exterior/interior profiles retain the equivalent shared values.
-   Other shared feature settings, upscaler selection and quality, and shader
    cache preferences do not require changes. Runtime state, shader register
    assignments and schema-version numbers are not quality defaults.
-   TruePBR's omitted material-object `specularLevel` stays at `0.04`.
    `fcf2ee60d4806c99034dadc7cfe9be7b95170962` corrected the reference's
    `1.0` fallback to match projected-material initialization and reset.
    Restoring that older value would undo a documented correctness fix.
-   Features and controls without an equivalent in the other branch retain
    their current implementations and defaults. This includes separate
    Wetterness behavior and its exclusion of legacy Wetness Effects,
    VR-specific controls and unsupported SE-only features. This change does
    not import features or older setting schemas.
-   The prior celestial-light handoff and horizon-dimming fixes remain
    intact; this change only aligns the two differing Sky Sync defaults.

## Preset compatibility

The unified packages target VR. Their source fingerprint covers complete
files, including the SE/AE defaults changed here and the preceding menu
and Sky Sync fixes. Review of serialized keys, loading, saving and
migrations found no incompatible VR settings change. The preceding Sky
Sync additions have defaults for omitted keys.

Contract revision 5, the base template, and all tier settings are unchanged.
`pwsh ./tools/generate-unified-presets.ps1` completed successfully after
the reviewed fingerprint refresh. Review of the generated diffs confirms
that all three packages differ only in their source fingerprint; the base
template is unchanged. The generated report records the resulting hashes.

## Validation limits

No build, shader compilation, automated tests, deployment or in-game
validation was run, as requested. Source and artifact comparisons,
scoped formatting and Git whitespace checks are the available evidence.
Skylighting's source and existing isolated harnesses match the pre-alignment
baseline above; no runtime-detection stubs are needed. The harnesses were
not executed.
Scoped formatting hooks and `pwsh ./tools/git.ps1 diff --check` passed.
All six bundled theme payloads compared equal to the SE reference.
These checks do not establish image equivalence or GPU cost. The default
alignment does not diagnose or promise a reduction in the reported GPU
power draw.
