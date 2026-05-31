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
- VR Render Scale Mode control with transition-time render-target relatching

The first three are direct runtime toggles. Upscaler preset control changes the internal render scale used by DLSS, FSR 3.1.5, and runtime FSR4. `DLSSMode` remains a type alias for `UpscalePreset` so old enum values keep their numeric layout. DLSS profile control is DLSS-only. In VR, presets below native enable Render Scale Mode and Native AA/DLAA disables it. Render Scale Mode requests a render-target relatch; call it during loading/interior-exterior transitions for the cleanest switch.

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
- `UpscalePreset GetUpscalePreset()` / `void SetUpscalePreset(UpscalePreset preset)`
- `bool GetLightLimitFixContactShadowsEnabled()`
- `void SetLightLimitFixContactShadowsEnabled(bool enabled)`
- `DLSSProfile GetDLSSProfile()`
- `void SetDLSSProfile(DLSSProfile profile)`
- `bool GetRenderAtUpscaleResEnabled()`
- `void SetRenderAtUpscaleResEnabled(bool enabled)`
- `bool GetRenderAtUpscaleResActive()`
- `void SetVRUpscalingTransitionProfile(bool renderScaleModeEnabled, UpscalePreset preset, DLSSProfile profile)`

`UpscalePreset` values:

- `UpscalePreset::kNativeAA` / `UpscalePreset::kDLAA` (1.00x; shown as `DLAA` for DLSS and `Native AA` for FSR/FSR4)
- `UpscalePreset::kHoshipa` (0.85x)
- `UpscalePreset::kUltraQuality` (0.77x)
- `UpscalePreset::kQuality` (0.67x)
- `UpscalePreset::kBalanced` (0.59x)
- `UpscalePreset::kPerformance` (0.50x)
- `UpscalePreset::kUltraPerformance` (0.33x)

Numeric enum values keep backwards compatibility for the original five modes; they are not the same as the in-menu order for the two newer modes. `UpscalePreset::kDLAA` is an alias for `UpscalePreset::kNativeAA`. `DLSSMode` is a legacy alias for `UpscalePreset`, so `DLSSMode::kQuality` and `UpscalePreset::kQuality` name the same enum value.

## Behavior Notes

- `SSS` means **Screen Space Shadows**, not Subsurface Scattering.
- Setters change runtime state in Community Shaders.
- `GetUpscalePreset`/`SetUpscalePreset` control the shared upscaler preset for DLSS, FSR 3.1.5, and runtime FSR4. These are renamed versions of the old `GetDLSSMode`/`SetDLSSMode` vtable slots, with the same return type size and parameter layout.
- These presets are Community Shaders render-scale presets, not AMD FSR quality enum values. `Hoshipa` and `Ultra Quality` are valid for both DLSS and FSR/FSR4 because the backend receives explicit render and display sizes.
- DLSS profile control is DLSS-only and does **not** affect FSR 3.1.5 or FSR4.
- `SetRenderAtUpscaleResEnabled` is the legacy API name for changing the requested VR Render Scale Mode state. Enabling it from Native AA/DLAA promotes the shared preset to `Quality` so the render-scale state stays valid. `GetRenderAtUpscaleResActive` reports whether the Render Scale Mode render-target relatch is actually active.
- Render Scale Mode is only eligible in VR with DLSS/FSR upscaling presets below native scale. Selecting Native AA/DLAA disables Render Scale Mode and clears the relatch request.
- `SetVRUpscalingTransitionProfile` is intended for interior/exterior transition controllers. It stages Render Scale Mode and shared DLSS/FSR render-scale preset transitions so Community Shaders can apply one relatch. During the VR save/load grace window or while game/CS menus are open, render-scale transitions stay queued until the post-load runtime reset has completed and the transition has settled for a few frames. Native/no-render-scale preset changes and DLSS profile-only changes apply normally.
- The individual `SetUpscalePreset`, `SetDLSSProfile`, and `SetRenderAtUpscaleResEnabled` setters use the same VR transition staging when called separately.
- New virtual methods must only be appended to the interface to preserve binary compatibility.
- VR DLSS keeps two viewport/resource slots for recent quality/profile combinations. Alternating between an exterior profile and an interior profile can reuse those slots instead of rebuilding DLSS every time.
- Reflex settings are not exposed by this API.
- If the API pointer is null, Community Shaders is missing, too old, or not ready yet.
- Call from the main/game thread, or queue via SKSE task interface.

## Compatibility Guidance

- Always null-check the API pointer.
- Prefer checking `getBuildNumber()` before relying on behavior.
- `GetUpscalePreset`/`SetUpscalePreset` require `getBuildNumber() >= 2`.
- `GetLightLimitFixContactShadowsEnabled`/`SetLightLimitFixContactShadowsEnabled` and `GetDLSSProfile`/`SetDLSSProfile` require `getBuildNumber() >= 3`.
- `UpscalePreset::kHoshipa` and `UpscalePreset::kUltraQuality` require `getBuildNumber() >= 4`.
- `GetRenderAtUpscaleResEnabled`/`SetRenderAtUpscaleResEnabled`/`GetRenderAtUpscaleResActive` require `getBuildNumber() >= 5`.
- `SetVRUpscalingTransitionProfile` and the current VR render-scale transition staging behavior require `getBuildNumber() >= 6`.
- Treat missing API as optional integration and continue without hard failure.
