#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <cstdint>

#include "VRAPI/CScaptureapi.h"

namespace CSPluginAPI
{
	// Returns an ICSInterface001 object compatible with the API shown below.
	// This should only be called after SKSE sends kMessage_PostLoad to your plugin.
	constexpr const auto CSPluginName = "CommunityShaders";
	inline constexpr uint32_t CSInterfaceMessageType = 0x43534150;  // "CSAP"
	inline constexpr unsigned int CSInterfaceRevision001 = 1;
	inline constexpr unsigned int CSInterfaceRevision002 = 2;
	inline constexpr unsigned int CSInterfaceRevision003 = 3;
	inline constexpr unsigned int CSInterfaceRevision004 = 4;
	inline constexpr unsigned int CSInterfaceRevision005 = 5;
	inline constexpr unsigned int CSInterfaceRevision = CSInterfaceRevision005;
	// Deprecated source-compatibility aliases. CSX build 11 and newer own
	// render-transition coverage and external controllers must not add a
	// fixed-duration fade. Older binaries retain their previously inlined values.
	[[deprecated("CSX owns VR render-transition fade coverage")]]
	inline constexpr float CSVRRenderScaleTransitionFadeOutSeconds = 0.0f;
	[[deprecated("CSX owns VR render-transition fade coverage")]]
	inline constexpr float CSVRRenderScaleTransitionBlackHoldAfterProfileSeconds = 0.0f;
	[[deprecated("CSX owns VR render-transition fade coverage")]]
	inline constexpr float CSVRRenderScaleTransitionFadeInSeconds = 0.0f;
	// A message used to fetch CSX' interface.
	struct CSMessage
	{
		enum : uint32_t
		{
			kMessage_GetInterface = CSInterfaceMessageType
		};
		void* (*GetApiFunction)(unsigned int revisionNumber) = nullptr;
	};

	struct ICSInterface001;
	ICSInterface001* GetCSInterface001();

	// Shared upscaler render-scale presets for DLSS, FSR 3.1.5, and runtime FSR4.
	enum class UpscalePreset : uint32_t
	{
		// Values 0-4 are kept stable for existing compiled API users.
		kNativeAA = 0,       // Native render scale: DLAA for DLSS, Native AA for FSR/FSR4.
		kDLAA = kNativeAA,  // Legacy DLSS-specific name for the native-scale preset.
		kQuality = 1,
		kBalanced = 2,
		kPerformance = 3,
		kUltraPerformance = 4,
		kHoshipa = 5,
		kUltraQuality = 6
	};

	// Legacy type name kept for source compatibility. It is the same enum type.
	using DLSSMode = UpscalePreset;

	enum class DLSSProfile : uint32_t
	{
		kJ = 0,
		kK = 1,
		kL = 2,
		kM = 3,
		kF = 4
	};

	enum class UpscaleMethod : uint32_t
	{
		kNone = 0,
		kTAA = 1,
		kFSR = 2,
		kDLSS = 3
	};

	enum class VRUpscalingApplyBlockReason : uint32_t
	{
		kNone = 0,
		kRaceSexMenu = 1u << 0,
		kRaceSexStartupTail = 1u << 1,
		kLoadingMenu = 1u << 2,
		kRelatchPending = 1u << 3,
		kTransitionPending = 1u << 4,
		kOpenCompositeUpscaling = 1u << 5
	};

	enum class VRUpscalingTransitionProfileDecision : uint32_t
	{
		kBlocked = 0,
		kNoChange = 1,
		kApply = 2
	};

	// This object provides access to CSX' mod support API.
	struct ICSInterface001
	{
		// ABI note: keep virtual methods append-only. Inserting new virtuals before
		// existing entries changes vtable slots for already-compiled consumers.
		virtual unsigned int getBuildNumber() = 0;

		// SSS here means Screen Space Shadows.
		virtual bool GetSSSEnabled() = 0;
		virtual void SetSSSEnabled(bool enabled) = 0;

		virtual bool GetSSGIEnabled() = 0;
		virtual void SetSSGIEnabled(bool enabled) = 0;

		virtual bool GetVolumetricLightingExteriorEnabled() = 0;
		virtual void SetVolumetricLightingExteriorEnabled(bool enabled) = 0;

		// Controls the shared DLSS/FSR/FSR4 upscaler preset.
		virtual UpscalePreset GetUpscalePreset() = 0;
		virtual void SetUpscalePreset(UpscalePreset preset) = 0;

		virtual bool GetLightLimitFixContactShadowsEnabled() = 0;
		virtual void SetLightLimitFixContactShadowsEnabled(bool enabled) = 0;

		// DLSS profile selection only. This does not affect FSR 3.1.5 or FSR4.
		virtual DLSSProfile GetDLSSProfile() = 0;
		virtual void SetDLSSProfile(DLSSProfile profile) = 0;

		// VR only. Legacy compatibility names for CSX' Render Scale Mode request.
		// Runtime activation can require a render-target relatch during a loading transition.
		virtual bool GetRenderAtUpscaleResEnabled() = 0;
		virtual void SetRenderAtUpscaleResEnabled(bool enabled) = 0;
		virtual bool GetRenderAtUpscaleResActive() = 0;

		// Legacy convenience for interior/exterior transition controllers. On
		// DLSS-capable systems this stages DLSS, render-scale path, shared
		// preset, and DLSS profile together. FSR-specific callers should use
		// SetVRUpscalingTransitionProfileForMethod in revision 2. When active VR
		// FPS Stabilizer Interior/Exterior profiles are available, the provider
		// accepts the configured destination profile at either supported door
		// timing: before the cell-type flip, or during the destination LoadingMenu.
		// Ordinary current-cell reconciliation outside that handoff is ignored.
		virtual void SetVRUpscalingTransitionProfile(bool renderScaleModeEnabled, UpscalePreset preset, DLSSProfile profile) = 0;

		// Revision 2. Explicit upscaler method control for callers that must
		// distinguish DLSS from FSR/FSR4 instead of relying on current CSX settings.
		virtual UpscaleMethod GetUpscaleMethod() = 0;
		virtual void SetUpscaleMethod(UpscaleMethod method) = 0;
		virtual void SetVRUpscalingTransitionProfileForMethod(UpscaleMethod method, bool renderScaleModeEnabled, UpscalePreset preset, DLSSProfile profile) = 0;

		// Revision 3. External transition controllers should query this before
		// applying VR upscaling profiles. Non-zero block reasons mean the caller
		// should buffer its latest desired profile and try again later. A zero result
		// is a runtime-safety check, not permission to synthesize a door transition
		// while the player remains in the same cell type.
		virtual uint32_t GetVRUpscalingApplyBlockReasons() = 0;
		virtual bool IsVRUpscalingProfileApplyAllowed() = 0;

		// Revision 4 / build 10. Intent-specific preflight for the method-specific
		// atomic profile call. Unlike the revision-3 global gate, this may admit a
		// destination profile during a real Stabilizer LoadingMenu handoff without
		// authorizing unrelated CSX setters. kNoChange means both settings and the
		// physical render-scale contract already match, so the caller must not
		// invoke the setter. kApply means immediately call
		// SetVRUpscalingTransitionProfileForMethod. CSX covers actual renderer work
		// with Skyrim's existing loading fade; callers must not add a timed fade.
		virtual VRUpscalingTransitionProfileDecision GetVRUpscalingTransitionProfileDecision(
			UpscaleMethod method,
			bool renderScaleModeEnabled,
			UpscalePreset preset,
			DLSSProfile profile) = 0;

		// Revision 5. Capture evolves independently behind its own versioned
		// interface. The returned pointer remains owned by CSX.
		virtual ICSCaptureInterface001* GetCaptureInterface001() = 0;
	};
}  // namespace CSPluginAPI

extern CSPluginAPI::ICSInterface001* g_CSInterface;
