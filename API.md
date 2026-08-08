# Community Shaders Expanded (CSX) SKSE API

This document explains how another SKSE plugin can talk to Community Shaders Expanded (CSX) at runtime.

## Audience

This is for **consumer plugins** (mods that want to call into CSX).

## API Version

-   Interface revision: `4` (`1`, `2`, and `3` remain accepted for existing compiled consumers)
-   Build number: returned by `getBuildNumber()`

## What This API Exposes

-   Screen Space Shadows toggle (`SSS` in method names)
-   Screen Space GI toggle
-   Volumetric Lighting Exterior toggle
-   Shared upscaler preset control for DLSS, FSR 3.1.5, and runtime FSR4 (`DLAA`/`Native AA`, `Hoshipa`, `Ultra Quality`, `Quality`, `Balanced`, `Performance`, `Ultra Performance`)
-   Explicit upscaler method control (`None`, `TAA`, `FSR`, `DLSS`)
-   DLSS profile control (`J`, `K`, `L`, `M`, `F`)
-   VR Render Scale Mode control with transition-time render-target relatching
-   VR upscaling apply-safety query for external transition controllers
-   Target-aware VR transition-profile decisions that distinguish blocked, already-matched, and required applies
-   Deprecated, zero-valued VR transition fade aliases retained for source compatibility; CSX owns transition coverage

The first three are direct runtime toggles. Upscaler preset control changes the internal render scale used by DLSS, FSR 3.1.5, and runtime FSR4. `DLSSMode` remains a type alias for `UpscalePreset` so old enum values keep their numeric layout. DLSS profile control is DLSS-only. In VR, presets below native enable Render Scale Mode and Native AA/DLAA disables it. Render Scale Mode requests a render-target relatch; call it during loading/interior-exterior transitions for the cleanest switch. Legacy revision-1 upscaling calls keep DLSS-first behavior on DLSS-capable systems. Revision 2 adds method-explicit calls for controllers that must select FSR/FSR4 or otherwise distinguish DLSS from FSR/FSR4 instead of relying on legacy DLSS behavior. Revision 3 adds the strict global apply-safety query. Revision 4 adds the target-aware atomic-profile decision used to stage a door relatch early without releasing unrelated CSX settings.

## Files You Need In The Consumer Mod

Use the interface contract only:

-   `include/VRAPI/CSinterface001.h`
-   Optionally `src/VRAPI/CSinterface001.cpp` (convenience helper that fetches and caches the interface)

You do **not** need provider internals like `CSpluginapi.*`.

## Handshake Details

-   Target plugin name: `CommunityShaders`
-   Message type: `0x43534150` (`CSMessage::kMessage_GetInterface`)
-   Requested revision: `4` for target-aware VR upscaling transition decisions. Revisions `1`, `2`, and `3` remain accepted for existing binaries, and `0` requests "latest". The bundled `GetCSInterface001()` helper requests the header's current revision and returns `nullptr` on older providers so callers do not accidentally use methods beyond an older vtable.

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
                logger::warn("CSX API unavailable");
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

-   `unsigned int getBuildNumber()`
-   `bool GetSSSEnabled()`
-   `void SetSSSEnabled(bool enabled)`
-   `bool GetSSGIEnabled()`
-   `void SetSSGIEnabled(bool enabled)`
-   `bool GetVolumetricLightingExteriorEnabled()`
-   `void SetVolumetricLightingExteriorEnabled(bool enabled)`
-   `UpscalePreset GetUpscalePreset()` / `void SetUpscalePreset(UpscalePreset preset)`
-   `bool GetLightLimitFixContactShadowsEnabled()`
-   `void SetLightLimitFixContactShadowsEnabled(bool enabled)`
-   `DLSSProfile GetDLSSProfile()`
-   `void SetDLSSProfile(DLSSProfile profile)`
-   `bool GetRenderAtUpscaleResEnabled()`
-   `void SetRenderAtUpscaleResEnabled(bool enabled)`
-   `bool GetRenderAtUpscaleResActive()`
-   `void SetVRUpscalingTransitionProfile(bool renderScaleModeEnabled, UpscalePreset preset, DLSSProfile profile)`
-   `UpscaleMethod GetUpscaleMethod()`
-   `void SetUpscaleMethod(UpscaleMethod method)`
-   `void SetVRUpscalingTransitionProfileForMethod(UpscaleMethod method, bool renderScaleModeEnabled, UpscalePreset preset, DLSSProfile profile)`
-   `uint32_t GetVRUpscalingApplyBlockReasons()`
-   `bool IsVRUpscalingProfileApplyAllowed()`
-   `VRUpscalingTransitionProfileDecision GetVRUpscalingTransitionProfileDecision(UpscaleMethod method, bool renderScaleModeEnabled, UpscalePreset preset, DLSSProfile profile)`

`UpscalePreset` values:

-   `UpscalePreset::kNativeAA` / `UpscalePreset::kDLAA` (1.00x; shown as `DLAA` for DLSS and `Native AA` for FSR/FSR4)
-   `UpscalePreset::kHoshipa` (0.85x)
-   `UpscalePreset::kUltraQuality` (0.77x)
-   `UpscalePreset::kQuality` (0.67x)
-   `UpscalePreset::kBalanced` (0.59x)
-   `UpscalePreset::kPerformance` (0.50x)
-   `UpscalePreset::kUltraPerformance` (0.33x)

Numeric enum values keep backwards compatibility for the original five modes; they are not the same as the in-menu order for the two newer modes. `UpscalePreset::kDLAA` is an alias for `UpscalePreset::kNativeAA`. `DLSSMode` is a legacy alias for `UpscalePreset`, so `DLSSMode::kQuality` and `UpscalePreset::kQuality` name the same enum value.

`UpscaleMethod` values:

-   `UpscaleMethod::kNone`
-   `UpscaleMethod::kTAA`
-   `UpscaleMethod::kFSR` (covers FSR 3.1.5 and runtime FSR4; runtime FSR4 remains a CSX-side runtime path choice)
-   `UpscaleMethod::kDLSS`

`VRUpscalingApplyBlockReason` bit values returned by `GetVRUpscalingApplyBlockReasons()`:

-   `VRUpscalingApplyBlockReason::kNone`
-   `VRUpscalingApplyBlockReason::kRaceSexMenu`
-   `VRUpscalingApplyBlockReason::kRaceSexStartupTail`
-   `VRUpscalingApplyBlockReason::kLoadingMenu`
-   `VRUpscalingApplyBlockReason::kRelatchPending`
-   `VRUpscalingApplyBlockReason::kTransitionPending`
-   `VRUpscalingApplyBlockReason::kOpenCompositeUpscaling`

`VRUpscalingTransitionProfileDecision` values returned by the revision-4 target-aware preflight:

-   `VRUpscalingTransitionProfileDecision::kBlocked` — retain the latest valid target and retry; invalid enum/configuration input must instead be treated as a terminal caller error
-   `VRUpscalingTransitionProfileDecision::kNoChange` — the requested settings and physical contract already match; do not call the setter
-   `VRUpscalingTransitionProfileDecision::kApply` — immediately call the method-specific atomic setter synchronously

The former advisory timed-fade constants remain as deprecated, zero-valued source-compatibility aliases. They must not be used to schedule a separate transition fade; CSX now owns render-change coverage. Build 11 identifies this behavior at runtime because already-compiled consumers retain the former inline values until rebuilt.

## Behavior Notes

-   `SSS` means **Screen Space Shadows**, not Subsurface Scattering.
-   Setters change runtime state in CSX.
-   `GetUpscalePreset`/`SetUpscalePreset` control the shared upscaler preset for DLSS, FSR 3.1.5, and runtime FSR4. These are renamed versions of the old `GetDLSSMode`/`SetDLSSMode` vtable slots, with the same return type size and parameter layout.
-   These presets are CSX render-scale presets, not AMD FSR quality enum values. `Hoshipa` and `Ultra Quality` are valid for both DLSS and FSR/FSR4 because the backend receives explicit render and display sizes.
-   DLSS profile control is DLSS-only and does **not** affect FSR 3.1.5 or FSR4.
-   `SetRenderAtUpscaleResEnabled` is the legacy API name for changing the requested VR Render Scale Mode state. Enabling it from Native AA/DLAA promotes the shared preset to `Quality` so the render-scale state stays valid. `GetRenderAtUpscaleResActive` reports whether the Render Scale Mode render-target relatch is actually active.
-   Render Scale Mode is only eligible in VR with DLSS/FSR upscaling presets below native scale. Selecting Native AA/DLAA disables Render Scale Mode and clears the relatch request.
-   `SetVRUpscalingTransitionProfile` is the legacy transition call. On DLSS-capable systems, it stages DLSS, Render Scale Mode, the shared render-scale preset, and the DLSS profile together so CSX can apply one relatch. If DLSS is known unavailable, it falls back to the configured non-DLSS method. This preserves old `DLSSMode`/`DLSSProfile` caller expectations; FSR-specific callers should use the revision-2 method-specific call. During VR save/load safe mode, RaceSex startup, loading presentation windows, or pending relatches, external upscaling setters are blocked by the revision-3 safety mask and should be buffered by the caller.
-   `SetUpscaleMethod` selects the CSX upscaler method explicitly while preserving the current preset, DLSS profile, and Render Scale Mode request where valid.
-   `SetVRUpscalingTransitionProfileForMethod` is the preferred revision-2 call for interior/exterior controllers that need deterministic DLSS/FSR behavior. It stages method, Render Scale Mode, shared preset, and DLSS profile together, so `DLSS + NativeAA + K` is unambiguously DLAA/K and `FSR + Hoshipa` is unambiguously FSR render scale. When active unconditional VR FPS Stabilizer Interior/Exterior profiles are available, the two atomic transition-profile calls accept the configured destination profile at either supported timing: before the cell type changes or during the destination-cell `LoadingMenu` handoff. Current-cell profile reassertions during ordinary gameplay remain ignored. Individual setters and consumers without active stabilizer profiles retain their existing behavior.
-   External VR transition controllers should call `GetVRUpscalingApplyBlockReasons()` or `IsVRUpscalingProfileApplyAllowed()` before applying ordinary CSX changes; both revision-3 queries remain strictly blocked for every non-zero reason mask. Revision 4 / build 10 adds `GetVRUpscalingTransitionProfileDecision(...)` for the method-specific atomic upscaling profile only. It returns `kBlocked` when the caller must buffer and retry, `kNoChange` when settings and the physical render-scale contract already match (do not call the setter), and `kApply` when the caller should immediately call `SetVRUpscalingTransitionProfileForMethod`. During a real in-game Stabilizer `LoadingMenu` handoff, this intent-specific preflight may stage the immutable destination profile while only soft loading/transition blockers remain; renderer mutation still waits until Skyrim releases loading-target ownership.
-   `kOpenCompositeUpscaling` means Open Composite owns the active upscaling path. CSX remains locked to `None`, and callers must not retry an upscaling profile until the game has restarted without Open Composite upscaling.
-   The individual legacy `SetUpscalePreset`, `SetDLSSProfile`, and `SetRenderAtUpscaleResEnabled` setters use the same VR transition staging when called separately. `SetUpscalePreset` and `SetDLSSProfile` prefer DLSS on DLSS-capable systems for backwards compatibility with consumers built around the old DLSS naming.
-   External transition controllers must not add a timed fade for a CSX profile change. For `kNoChange`, call no setter. For `kApply`, synchronously call `SetVRUpscalingTransitionProfileForMethod` on the same game thread; do not wait for the screen to become black or defer the setter to another frame. CSX stages the immutable request, performs physical renderer mutation as soon as Skyrim releases loading-target ownership, and holds Skyrim at the beginning of its existing black fade-in only while concrete engine save/load activity, CSX recovery/relatch work, or coherent destination rendering remains incomplete. The broader 120-frame save/load mutation and disk-persistence grace does not extend this presentation hold. The normal game fade resumes from its start as soon as the compositor has accepted the completed stereo state. Legacy callers without revision 4 should apply only when the strict revision-3 gate allows it.
-   New virtual methods must only be appended to the interface to preserve binary compatibility.
-   VR DLSS keeps two viewport/resource slots for recent quality/profile combinations. Alternating between an exterior profile and an interior profile can reuse those slots instead of rebuilding DLSS every time.
-   Reflex settings are not exposed by this API.
-   If the API pointer is null, CSX is missing, too old, or not ready yet.
-   Call from the main/game thread, or queue via SKSE task interface.

## Compatibility Guidance

-   Always null-check the API pointer.
-   Prefer checking `getBuildNumber()` before relying on behavior.
-   `GetUpscalePreset`/`SetUpscalePreset` require `getBuildNumber() >= 2`.
-   `GetLightLimitFixContactShadowsEnabled`/`SetLightLimitFixContactShadowsEnabled` and `GetDLSSProfile`/`SetDLSSProfile` require `getBuildNumber() >= 3`.
-   `UpscalePreset::kHoshipa` and `UpscalePreset::kUltraQuality` require `getBuildNumber() >= 4`.
-   `GetRenderAtUpscaleResEnabled`/`SetRenderAtUpscaleResEnabled`/`GetRenderAtUpscaleResActive` require `getBuildNumber() >= 5`.
-   `SetVRUpscalingTransitionProfile` and the current VR render-scale transition staging behavior require `getBuildNumber() >= 6`.
-   `GetUpscaleMethod`/`SetUpscaleMethod`/`SetVRUpscalingTransitionProfileForMethod` require interface revision `2` and `getBuildNumber() >= 7`.
-   `GetVRUpscalingApplyBlockReasons`/`IsVRUpscalingProfileApplyAllowed` require interface revision `3` and `getBuildNumber() >= 8`.
-   `VRUpscalingApplyBlockReason::kOpenCompositeUpscaling` requires `getBuildNumber() >= 9`.
-   `GetVRUpscalingTransitionProfileDecision` and atomic Stabilizer door-profile staging while only soft LoadingMenu blockers remain require interface revision `4` and `getBuildNumber() >= 10`. Revision-3 queries and individual setters retain their fail-closed behavior.
-   Treat missing API as optional integration and continue without hard failure.
