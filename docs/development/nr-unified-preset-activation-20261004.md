# NR activation after loading Unified presets

The Unified presets retain `Upscaling.frameGenerationMode: 1`. Frame
generation cannot run in VR, but NR previously rejected that saved request
as if it were active. The NR master could remain enabled while all three
pipelines and character capture silently skipped their work.

The affected physical-rig log identifies production Build ID
`ff1f4fb2c064`, source `7434dcbed`, SteamVR and an RTX 4090. Its 929 lines
include 328 Debug messages and active DLSS Render Scale presentation, but
no `[DLSSNR]` probe, initialization or evaluation record. The earlier
successful null-driver test used defaults with frame generation set to
zero; it did not qualify the affected preset on the physical rig.

All nine NR frame-generation gates now use one runtime-aware predicate.
VR ignores the saved frame-generation request. SE and AE still reject
nonzero requests, and DX12 ownership retains its existing exclusion.
This changes admission only: no shader, model, resource allocation,
GPU dispatch or synchronization operation was added. Enabled NR now
performs its expected work instead of silently bypassing it.

The settings schema, defaults, serialization and migrations are unchanged.
Only the two inventoried Upscaling source owners changed. The reviewed
preset fingerprint moves from `763159E0BA5639A2C9F735433D3756EC97E0B962C1EF2EB305ABD53034BC4D7E`
to `C3027F4ACFC197198E20CF1C006E68AE20771756F810246E77AC97ACD8CB5357`.
Preset regeneration retains every graphics setting, including the saved
frame-generation preference for flat runtimes.

Validation on parent `8a5600421` plus this change:

-   The extracted production preparation and predicate test passes 12
    flat/VR, saved-mode and DX12-ownership combinations. It checks actual
    guide preparation and frame-generation fallback evidence.
-   Seven focused CTest checks pass: UI, controls, full-resolution
    preparation, full-resolution FOV, prepared selection, its bridge variant
    and its source contract.
-   Preset generation/check, scoped hooks and `git diff --check` pass.
    C++ formatting was limited to changed lines; the full-file formatter hook
    was skipped to preserve unrelated code.
-   Physical-HMD validation of the corrected DLL remains pending.

For the existing DLL, setting `Upscaling.frameGenerationMode` to zero in
the saved preset and reloading it removes this specific blocker. NR still
needs its normal DLSS, FOV and Render Scale prerequisites for the selected
pipeline. The runtime correction allows the original presets to retain
their flat-runtime frame-generation preference.

## VR preset correction for the existing DLL

The Unified packages explicitly target VR. Package version
`d2026.10.04.1` therefore sets `Upscaling.frameGenerationMode` to zero in
all three tiers through one common policy override. This removes the
blocker for production Build ID `ff1f4fb2c064` without installing a new
DLL. Apart from package identity, frame generation is the only changed
graphics preference. The runtime correction in `8801e4904` remains needed
for older and user-authored presets with a nonzero saved preference.

## General VR settings invariant

Frame generation and force-enable both default to zero. VR now forces
both fields to zero at the shared settings-normalization boundary used
by load, save, reset and NR configuration updates. Older and user-authored
presets therefore cannot retain an enabled request in VR. The two UI
entry points already omit frame-generation controls in VR, and DX12
proxy creation and frame-generation execution already reject VR.
SE and AE retain their existing toggle clamping and restart behavior.
Normalization runs only at settings boundaries; it adds no render pass
or per-frame GPU work.

Only runtime-specific normalization changes; serialized fields and their
defaults are unchanged. The reviewed source-contract fingerprint moves
from `C3027F4ACFC197198E20CF1C006E68AE20771756F810246E77AC97ACD8CB5357`
to `5A3D98482F901402FD32FE3B5CF42F0EF3B4AF8B6688163489DACF37CCE5A32F`.
The historical base template remains pinned, and the common VR override
corrects all generated tiers.

Validation on parent `8801e4904` plus this change:

-   `frame_generation_inputs_test` builds and passes with extracted
    production defaults, normalization and execution. It checks off-by-default
    settings, VR rejection of saved enable/force-enable values, flat clamping,
    idempotence, and existing menu, missing-input and presentation safety.
    Extraction verifies that load, save and reset reach normalization.
-   Ten focused CTest checks pass: FrameGenerationInputs, the seven NR
    checks listed above, PresetCompatibility and
    FeaturePresetCompatibilityContract. Logs remain under
    `build/validation/nr-unified-vr-preset-fix-20261004/`.
-   `pwsh ./tools/generate-unified-presets.ps1` and `-Check` pass for all
    three tiers. Only the saved frame-generation preference, package version
    and reviewed source fingerprint change in generated settings.
-   The three `dist/CSX_Unified-*_NR-VR-Fix_20261004.7z` archives pass
    `7z t`. Extracted settings and metadata match their generated sources
    byte-for-byte and contain no additional files. Archive hashes and sizes
    are recorded in the local evidence directory.
-   Scoped whitespace, line-ending and Prettier hooks pass. Changed C++
    lines use clang-format 22.1.4; the whole-file formatter hook was skipped
    to preserve unrelated formatting.
-   No new DLL was built or installed for this change. The general VR
    normalization requires a rebuilt DLL; the corrected presets remove the
    identified blocker with the existing DLL. Physical-HMD retest remains
    pending.
