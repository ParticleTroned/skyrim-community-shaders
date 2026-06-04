# Community Shaders SKSE API

This document explains how another SKSE plugin can talk to Community Shaders at runtime.

## Audience

This is for **consumer plugins** (mods that want to call into Community Shaders).

## API Version

- Interface revision: `2` (`1` remains accepted for existing compiled consumers)
- Build number: returned by `getBuildNumber()`

## What This API Exposes

- Screen Space Shadows toggle (`SSS` in method names)
- Screen Space GI toggle
- Volumetric Lighting Exterior toggle
- Shared upscaler preset control for DLSS, FSR 3.1.5, and runtime FSR4 (`DLAA`/`Native AA`, `Hoshipa`, `Ultra Quality`, `Quality`, `Balanced`, `Performance`, `Ultra Performance`)
- Explicit upscaler method control (`None`, `TAA`, `FSR`, `DLSS`)
- DLSS profile control (`J`, `K`, `L`, `M`, `F`)
- VR Render Scale Mode control with transition-time render-target relatching
- Advisory VR transition fade timing constants for controllers that want to hide render-scale relatches behind a fade-to-black

The first three are direct runtime toggles. Upscaler preset control changes the internal render scale used by DLSS, FSR 3.1.5, and runtime FSR4. `DLSSMode` remains a type alias for `UpscalePreset` so old enum values keep their numeric layout. DLSS profile control is DLSS-only. In VR, presets below native enable Render Scale Mode and Native AA/DLAA disables it. Render Scale Mode requests a render-target relatch; call it during loading/interior-exterior transitions for the cleanest switch. Legacy revision-1 upscaling calls keep DLSS-first behavior on DLSS-capable systems. Revision 2 adds method-explicit calls for controllers that must select FSR/FSR4 or otherwise distinguish DLSS from FSR/FSR4 instead of relying on legacy DLSS behavior.

## Files You Need In The Consumer Mod

Use the interface contract only:

- `include/VRAPI/CSinterface001.h`
- Optionally `src/VRAPI/CSinterface001.cpp` (convenience helper that fetches and caches the interface)

You do **not** need provider internals like `CSpluginapi.*`.

## Handshake Details

- Target plugin name: `CommunityShaders`
- Message type: `0x43534150` (`CSMessage::kMessage_GetInterface`)
- Requested revision: `2` for method-explicit upscaling calls. Revision `1` remains accepted for existing binaries, and `0` requests "latest". The bundled `GetCSInterface001()` helper requests revision `2` and returns `nullptr` on older providers so new callers do not accidentally use revision-2 methods on a revision-1 vtable.

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
- `UpscaleMethod GetUpscaleMethod()`
- `void SetUpscaleMethod(UpscaleMethod method)`
- `void SetVRUpscalingTransitionProfileForMethod(UpscaleMethod method, bool renderScaleModeEnabled, UpscalePreset preset, DLSSProfile profile)`

`UpscalePreset` values:

- `UpscalePreset::kNativeAA` / `UpscalePreset::kDLAA` (1.00x; shown as `DLAA` for DLSS and `Native AA` for FSR/FSR4)
- `UpscalePreset::kHoshipa` (0.85x)
- `UpscalePreset::kUltraQuality` (0.77x)
- `UpscalePreset::kQuality` (0.67x)
- `UpscalePreset::kBalanced` (0.59x)
- `UpscalePreset::kPerformance` (0.50x)
- `UpscalePreset::kUltraPerformance` (0.33x)

Numeric enum values keep backwards compatibility for the original five modes; they are not the same as the in-menu order for the two newer modes. `UpscalePreset::kDLAA` is an alias for `UpscalePreset::kNativeAA`. `DLSSMode` is a legacy alias for `UpscalePreset`, so `DLSSMode::kQuality` and `UpscalePreset::kQuality` name the same enum value.

`UpscaleMethod` values:

- `UpscaleMethod::kNone`
- `UpscaleMethod::kTAA`
- `UpscaleMethod::kFSR` (covers FSR 3.1.5 and runtime FSR4; runtime FSR4 remains a CS-side runtime path choice)
- `UpscaleMethod::kDLSS`

Advisory VR render-scale transition fade constants:

- `CSVRRenderScaleTransitionFadeOutSeconds = 1.0f`
- `CSVRRenderScaleTransitionBlackHoldAfterProfileSeconds = 2.0f`
- `CSVRRenderScaleTransitionFadeInSeconds = 1.0f`

These constants do not control Community Shaders directly and do not change the ABI. They are timing guidance for transition controllers that call `Game.FadeOutGame` or an equivalent fade system.

## Behavior Notes

- `SSS` means **Screen Space Shadows**, not Subsurface Scattering.
- Setters change runtime state in Community Shaders.
- `GetUpscalePreset`/`SetUpscalePreset` control the shared upscaler preset for DLSS, FSR 3.1.5, and runtime FSR4. These are renamed versions of the old `GetDLSSMode`/`SetDLSSMode` vtable slots, with the same return type size and parameter layout.
- These presets are Community Shaders render-scale presets, not AMD FSR quality enum values. `Hoshipa` and `Ultra Quality` are valid for both DLSS and FSR/FSR4 because the backend receives explicit render and display sizes.
- DLSS profile control is DLSS-only and does **not** affect FSR 3.1.5 or FSR4.
- `SetRenderAtUpscaleResEnabled` is the legacy API name for changing the requested VR Render Scale Mode state. Enabling it from Native AA/DLAA promotes the shared preset to `Quality` so the render-scale state stays valid. `GetRenderAtUpscaleResActive` reports whether the Render Scale Mode render-target relatch is actually active.
- Render Scale Mode is only eligible in VR with DLSS/FSR upscaling presets below native scale. Selecting Native AA/DLAA disables Render Scale Mode and clears the relatch request.
- `SetVRUpscalingTransitionProfile` is the legacy transition call. On DLSS-capable systems, it stages DLSS, Render Scale Mode, the shared render-scale preset, and the DLSS profile together so Community Shaders can apply one relatch. If DLSS is known unavailable, it falls back to the configured non-DLSS method. This preserves old `DLSSMode`/`DLSSProfile` caller expectations; FSR-specific callers should use the revision-2 method-specific call. During VR save/load safe mode or while game/CS menus are open, render-scale transitions stay queued until the post-load runtime reset has completed, then apply after the normal short transition delay. Native/no-render-scale preset changes and DLSS profile-only changes apply normally.
- `SetUpscaleMethod` selects the CS upscaler method explicitly while preserving the current preset, DLSS profile, and Render Scale Mode request where valid.
- `SetVRUpscalingTransitionProfileForMethod` is the preferred revision-2 call for interior/exterior controllers that need deterministic DLSS/FSR behavior. It stages method, Render Scale Mode, shared preset, and DLSS profile together, so `DLSS + NativeAA + K` is unambiguously DLAA/K and `FSR + Hoshipa` is unambiguously FSR render scale.
- The individual legacy `SetUpscalePreset`, `SetDLSSProfile`, and `SetRenderAtUpscaleResEnabled` setters use the same VR transition staging when called separately. `SetUpscalePreset` and `SetDLSSProfile` prefer DLSS on DLSS-capable systems for backwards compatibility with consumers built around the old DLSS naming.
- `Game.FadeOutGame` does not pause Community Shaders or serialize D3D/vendor resource rebuilds. It only hides the transition visually. For render-scale transitions, fade to black first, call `SetVRUpscalingTransitionProfileForMethod` as soon as the screen is black and the destination profile is known, perform the move/cell transition, then keep the screen black for at least `CSVRRenderScaleTransitionBlackHoldAfterProfileSeconds` after the profile call or move, whichever is later, before fading back in. The clean baseline measured render-target relatches around `1.3` to `1.7` seconds, so the `2.0` second black hold gives margin without reintroducing Community Shaders-side safety latches.
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
- `GetUpscaleMethod`/`SetUpscaleMethod`/`SetVRUpscalingTransitionProfileForMethod` require interface revision `2` and `getBuildNumber() >= 7`.
- Treat missing API as optional integration and continue without hard failure.
