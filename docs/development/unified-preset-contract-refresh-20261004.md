# Unified preset source-contract review, 2026-10-04

The maintained preset checks rejected the source fingerprint after the NR
UI and batching diagnostics changed. The guard hashes complete settings
owner files, including their UI and tool descriptions. Reviewing those
changes established that the persisted settings contract did not change.

## Source and schema review

The 99 declared source hashes from the passing `7d9eee165575a7ef32c454a1a136047b5cce342e`
receipt reproduce the previous policy fingerprint exactly:

`CAE2498985888E6820CE22ACA5461FB5017A4FE0A65159BC2CCFB706E9A88AAB`.

Against `4c4f9501cf3121bffff4190c5bb0bc5086250555`, only three declared
owners differ:

| Owner                                     | Reviewed change                                                                                                  |
| ----------------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| `src/Features/NeuralRenderingFeature.cpp` | DevBench batching description and output schema; screenshot diagnostic-timing description.                       |
| `src/Features/Upscaling.cpp`              | Existing NR master and recovery controls moved into a helper so failure recovery remains visible when NR is off. |
| `src/Features/Upscaling.h`                | Private declaration for that UI helper.                                                                          |

The other 96 owner hashes are unchanged. Settings member definitions and
defaults, load/save serialization, normalization, owner inventory, pinned
base, tier overrides and migration rules are unchanged. The refreshed raw
source fingerprint is:

`F21C552FCC90456A46BAEF6C671C5B0538F11D5CC592E400C2E7F990286322CC`.

Settings-contract revision **8** remains the semantic compatibility
identity. The runtime requires that exact revision; incrementing it for
UI or description edits would reject otherwise compatible presets.
`sourceTreeSha256` records reviewed source provenance, while the runtime
validates its SHA-256 representation rather than requiring this specific
fingerprint.

## Generated outputs and preserved archives

The maintained generator refreshed the three tracked tier settings and
canonical report. A before/after JSON comparison found only
`Preset Compatibility/settingsContract/sourceTreeSha256` changed in each
tier. Every graphics value and all other metadata are equal. The report
changes only its fingerprint and the resulting three settings byte hashes.
The base, revision, package version and tier `meta.ini` bytes are unchanged.

The three existing user-owned `.7z` preset archives were preserved with
identical SHA-256 hashes. Their settings remain compatible; this review
does not require replacing them. No personal preset is bundled in the
production AIO.

## Validation

The initial regression failure remains recorded; its stale fingerprint was
not relabeled as a pass. After the source review and metadata refresh:

-   `pwsh -NoProfile -File ./tools/generate-unified-presets.ps1`: passed,
    2.2 seconds.
-   `pwsh -NoProfile -File ./tests/unified_preset_generator_test.ps1`:
    passed, 27.3 seconds.
-   `pwsh -NoProfile -File ./tools/generate-unified-presets.ps1 -Check`:
    passed, 2.1 seconds.
-   All 107 preset validation input hashes were captured after the successful
    checks; the checks left the maintained settings bytes and timestamps
    unchanged.

The historical receipt, failed first attempt, 99-source audit, metadata-only
comparison, user-archive hashes and genuine refreshed receipt are preserved
under `build/validation/nr-dynamic-kernel-production-aio-20261004/`. These
CPU checks qualify preset metadata consistency; they are not live NR image
quality or performance measurements.
