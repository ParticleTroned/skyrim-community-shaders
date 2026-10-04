# VR frame-generation settings normalization

This main-VR port of `5e85de548` prevents older and custom presets from
retaining unsupported frame-generation requests. The original NR branch
identified those saved requests as a blocker for neural-rendering admission.
This port carries the general VR settings invariant and preset correction.

Frame generation and force-enable both default to zero. VR now forces both
fields to zero at the shared settings-normalization boundary used by load,
save and reset. The two UI entry points already omit frame-generation
controls in VR, and DX12 proxy creation and execution already reject VR.
SE and AE retain their existing toggle clamping and restart behavior.
Normalization adds no render pass or per-frame GPU work.

Package version `d2026.10.04.1` sets `Upscaling.frameGenerationMode` to zero
in all three Unified VR tiers through one common policy override. Apart
from package identity and compatibility metadata, frame generation is the
only changed graphics preference.

The port retains main-VR's settings-contract revision 5 and its pinned base
template `1D3490B5C51C97E73A2ECC6CC8DE9B5413019F9DF050EC20F3780F99E2426CF0`.
Serialized fields and defaults remain unchanged. The reviewed fingerprint
moves from `54D9165BC2450408C22DCA37015E483BE844696A228EA7CA29F95ED40C6663B6`
to `27F545BF51FB1CD81F81F8C499DDE666517143780501C120C26146B7F1569480`.
The source branch's revision-8 fingerprint and NR-specific runtime changes
are outside this port.

Validation on main-VR parent `88f1b1a26` plus this port:

-   `pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target frame_generation_inputs_test preset_compatibility_test`
    builds both focused regression targets.
-   `pwsh ./tools/generate-unified-presets.ps1` and `-Check` pass for all
    three tiers, preserving their existing main-VR graphics settings.
-   `ctest --test-dir build/ALL -C Release -R '^(FrameGenerationInputs|PresetCompatibility|FeaturePresetCompatibilityContract)$' --output-on-failure`
    passes all three checks. The normalization regression covers disabled
    defaults, VR rejection, flat clamping, idempotence and execution safety.
    Extraction also verifies load, save and reset reach normalization.
-   Scoped whitespace, line-ending and Prettier hooks pass. The whole-file
    clang-format hook is skipped to preserve unrelated formatting; the
    cherry-picked C++ additions retain their source formatting.
-   No production DLL was built or installed. In-game SE, AE and physical-HMD
    validation was not run for this port.
