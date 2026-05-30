# Community Shaders SKSE API

This document explains how another SKSE plugin can talk to Community Shaders at runtime.

## Audience

This is for **consumer plugins** (mods that want to call into Community Shaders).

## API Version

- Interface revision: `1`
- Build number: returned by `getBuildNumber()`

## What This API Exposes

- Screen Space Shadows toggle (`SSS` in method names)
- Screen Space GI toggle
- Volumetric Lighting Exterior toggle
- Shared upscaler preset control for DLSS, FSR 3.1.5, and runtime FSR4 (`DLAA`/`Native AA`, `Hoshipa`, `Ultra Quality`, `Quality`, `Balanced`, `Performance`, `Ultra Performance`)
- DLSS profile control (`J`, `K`, `L`, `M`, `F`)
- VR Render at Upscale Res control with transition-time render-target relatching

The first three are direct runtime toggles. Upscaler preset control changes the internal render scale used by DLSS, FSR 3.1.5, and runtime FSR4. The legacy API names still say `DLSSMode` for compatibility. DLSS profile control is DLSS-only. In VR, Render at Upscale Res requests a render-target relatch; call it during loading/interior-exterior transitions for the cleanest switch.

## Files You Need In The Consumer Mod

Use the interface contract only:

- `include/VRAPI/CSinterface001.h`
- Optionally `src/VRAPI/CSinterface001.cpp` (convenience helper that fetches and caches the interface)

You do **not** need provider internals like `CSpluginapi.*`.

## Handshake Details

- Target plugin name: `CommunityShaders`
- Message type: `0x43534150` (`CSMessage::kMessage_GetInterface`)
- Requested revision: `1` (provider also accepts `0` as "latest")

## Integration Steps

1. Register your plugin's SKSE messaging listener.
2. On `SKSE::MessagingInterface::kPostLoad`, fetch the interface.
3. Store the pointer and check for `nullptr`.
4. Call methods when needed.

## Minimal Consumer Example

```cpp
#include "VRAPI/CSinterface001.h"

namespace
{
    CSPluginAPI::ICSInterface001* g_csApi = nullptr;

    void OnMessage(SKSE::MessagingInterface::Message* msg)
    {
        if (!msg) {
            return;
        }

        if (msg->type == SKSE::MessagingInterface::kPostLoad) {
            g_csApi = CSPluginAPI::GetCSInterface001();
            if (!g_csApi) {
                logger::warn("Community Shaders API unavailable");
            }
        }
    }
}

bool RegisterMessages()
{
    auto* messaging = SKSE::GetMessagingInterface();
    return messaging && messaging->RegisterListener("SKSE", OnMessage);
}

void SetShadowsEnabled(bool enabled)
{
    if (g_csApi) {
        g_csApi->SetSSSEnabled(enabled);  // SSS == Screen Space Shadows
    }
}
```

## Method Contract

`ICSInterface001` exposes:

- `unsigned int getBuildNumber()`
- `bool GetSSSEnabled()`
- `void SetSSSEnabled(bool enabled)`
- `bool GetSSGIEnabled()`
- `void SetSSGIEnabled(bool enabled)`
- `bool GetVolumetricLightingExteriorEnabled()`
- `void SetVolumetricLightingExteriorEnabled(bool enabled)`
- `DLSSMode GetDLSSMode()` / `void SetDLSSMode(DLSSMode mode)` (legacy names for the shared upscaler preset)
- `UpscalePreset GetUpscalePreset()` / `void SetUpscalePreset(UpscalePreset preset)` (header convenience wrappers)
- `DLSSProfile GetDLSSProfile()`
- `void SetDLSSProfile(DLSSProfile profile)`
- `bool GetRenderAtUpscaleResEnabled()`
- `void SetRenderAtUpscaleResEnabled(bool enabled)`
- `bool GetRenderAtUpscaleResActive()`
- `void SetVRUpscalingTransitionProfile(bool renderAtUpscaleResEnabled, DLSSMode mode, DLSSProfile profile)`

`DLSSMode` / `UpscalePreset` values:

- `DLSSMode::kDLAA` / `UpscalePreset::kNativeAA` (1.00x; shown as `DLAA` for DLSS and `Native AA` for FSR/FSR4)
- `DLSSMode::kHoshipa` / `UpscalePreset::kHoshipa` (0.85x)
- `DLSSMode::kUltraQuality` / `UpscalePreset::kUltraQuality` (0.77x)
- `DLSSMode::kQuality` / `UpscalePreset::kQuality` (0.67x)
- `DLSSMode::kBalanced` / `UpscalePreset::kBalanced` (0.59x)
- `DLSSMode::kPerformance` / `UpscalePreset::kPerformance` (0.50x)
- `DLSSMode::kUltraPerformance` / `UpscalePreset::kUltraPerformance` (0.33x)

Numeric enum values keep backwards compatibility for the original five modes; they are not the same as the in-menu order for the two newer modes. `UpscalePreset::kNativeAA` is an alias for `DLSSMode::kDLAA`.

## Behavior Notes

- `SSS` means **Screen Space Shadows**, not Subsurface Scattering.
- Setters change runtime state in Community Shaders.
- `DLSSMode` and `GetDLSSMode`/`SetDLSSMode` are legacy API names. They control the shared upscaler preset for DLSS, FSR 3.1.5, and runtime FSR4.
- `UpscalePreset` is an alias for `DLSSMode`; new integrations should prefer the `UpscalePreset` wording.
- These presets are Community Shaders render-scale presets, not AMD FSR quality enum values. `Hoshipa` and `Ultra Quality` are valid for both DLSS and FSR/FSR4 because the backend receives explicit render and display sizes.
- DLSS profile control is DLSS-only and does **not** affect FSR 3.1.5 or FSR4.
- `SetRenderAtUpscaleResEnabled` changes the requested VR Render at Upscale Res state. `GetRenderAtUpscaleResActive` reports whether the relatched render targets are actually active.
- Render at Upscale Res is only eligible in VR with DLSS/FSR upscaling presets below native scale. Unsupported combinations remain requested but inactive.
- `SetVRUpscalingTransitionProfile` is intended for interior/exterior transition controllers. It stages Render at Upscale Res, the shared upscaler preset, and the DLSS profile together so Community Shaders can apply one relatch.
- VR DLSS keeps two viewport/resource slots for recent quality/profile combinations. Alternating between an exterior profile and an interior profile can reuse those slots instead of rebuilding DLSS every time.
- Reflex settings are not exposed by this API.
- If the API pointer is null, Community Shaders is missing, too old, or not ready yet.
- Call from the main/game thread, or queue via SKSE task interface.

## Compatibility Guidance

- Always null-check the API pointer.
- Prefer checking `getBuildNumber()` before relying on behavior.
- `GetDLSSMode`/`SetDLSSMode` require `getBuildNumber() >= 2`; `GetUpscalePreset`/`SetUpscalePreset` are header wrappers over those methods.
- `GetDLSSProfile`/`SetDLSSProfile` require `getBuildNumber() >= 3`.
- `DLSSMode::kHoshipa` / `UpscalePreset::kHoshipa` and `DLSSMode::kUltraQuality` / `UpscalePreset::kUltraQuality` require `getBuildNumber() >= 4`.
- `GetRenderAtUpscaleResEnabled`/`SetRenderAtUpscaleResEnabled`/`GetRenderAtUpscaleResActive` require `getBuildNumber() >= 5`.
- `SetVRUpscalingTransitionProfile` requires `getBuildNumber() >= 6`.
- Treat missing API as optional integration and continue without hard failure.
