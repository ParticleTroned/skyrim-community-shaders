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
