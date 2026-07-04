#pragma once

#include "Features/LightLimitFix.h"
#include "Features/Upscaling.h"
#include "Features/ScreenSpaceGI.h"
#include "Features/ScreenSpaceShadows.h"
#include "Features/VolumetricLighting.h"
#include "Globals.h"
#include "VRAPI/CSinterface001.h"

#include <algorithm>
#include <atomic>
#include <cstdint>

inline constexpr unsigned int CSBuildNumber = 8;

namespace CSPluginAPI
{
	void* GetApi(unsigned int revisionNumber);

	void ModMessageHandler(SKSE::MessagingInterface::Message* message);

	// This object provides access to Community Shaders' mod support API.
	struct CSInterface001 : ICSInterface001
	{
		// Must mirror ICSInterface001 virtual order exactly.
		virtual unsigned int getBuildNumber() override;

		virtual bool GetSSSEnabled() override;
		virtual void SetSSSEnabled(bool enabled) override;

		virtual bool GetSSGIEnabled() override;
		virtual void SetSSGIEnabled(bool enabled) override;

		virtual bool GetVolumetricLightingExteriorEnabled() override;
		virtual void SetVolumetricLightingExteriorEnabled(bool enabled) override;

		virtual UpscalePreset GetUpscalePreset() override;
		virtual void SetUpscalePreset(UpscalePreset preset) override;

		virtual bool GetLightLimitFixContactShadowsEnabled() override;
		virtual void SetLightLimitFixContactShadowsEnabled(bool enabled) override;

		virtual DLSSProfile GetDLSSProfile() override;
		virtual void SetDLSSProfile(DLSSProfile profile) override;

		virtual bool GetRenderAtUpscaleResEnabled() override;
		virtual void SetRenderAtUpscaleResEnabled(bool enabled) override;
		virtual bool GetRenderAtUpscaleResActive() override;
		virtual void SetVRUpscalingTransitionProfile(bool renderScaleModeEnabled, UpscalePreset preset, DLSSProfile profile) override;

		virtual UpscaleMethod GetUpscaleMethod() override;
		virtual void SetUpscaleMethod(UpscaleMethod method) override;
		virtual void SetVRUpscalingTransitionProfileForMethod(UpscaleMethod method, bool renderScaleModeEnabled, UpscalePreset preset, DLSSProfile profile) override;

		virtual uint32_t GetVRUpscalingApplyBlockReasons() override;
		virtual bool IsVRUpscalingProfileApplyAllowed() override;
	};

	namespace detail
	{
		static_assert(static_cast<uint32_t>(VRUpscalingApplyBlockReason::kRaceSexMenu) == Upscaling::kVRUpscalingApplyBlockRaceSexMenu);
		static_assert(static_cast<uint32_t>(VRUpscalingApplyBlockReason::kRaceSexStartupTail) == Upscaling::kVRUpscalingApplyBlockRaceSexStartupTail);
		static_assert(static_cast<uint32_t>(VRUpscalingApplyBlockReason::kLoadingMenu) == Upscaling::kVRUpscalingApplyBlockLoadingMenu);
		static_assert(static_cast<uint32_t>(VRUpscalingApplyBlockReason::kRelatchPending) == Upscaling::kVRUpscalingApplyBlockRelatchPending);
		static_assert(static_cast<uint32_t>(VRUpscalingApplyBlockReason::kTransitionPending) == Upscaling::kVRUpscalingApplyBlockTransitionPending);

		inline bool IsValidInterfaceRequest(const SKSE::MessagingInterface::Message* message)
		{
			return message &&
			       message->type == CSMessage::kMessage_GetInterface &&
			       message->data &&
			       message->dataLen >= sizeof(CSMessage);
		}

		template <class TFlag>
		constexpr TFlag BoolToFlag(bool enabled)
		{
			return enabled ? static_cast<TFlag>(1) : static_cast<TFlag>(0);
		}

		inline bool IsValidUpscalePreset(UpscalePreset preset)
		{
			switch (preset) {
			case UpscalePreset::kNativeAA:
			case UpscalePreset::kQuality:
			case UpscalePreset::kBalanced:
			case UpscalePreset::kPerformance:
			case UpscalePreset::kUltraPerformance:
			case UpscalePreset::kHoshipa:
			case UpscalePreset::kUltraQuality:
				return true;
			default:
				return false;
			}
		}

		inline uint32_t UpscalePresetToQualityMode(UpscalePreset preset)
		{
			switch (preset) {
			case UpscalePreset::kNativeAA:
				return 0u;
			case UpscalePreset::kHoshipa:
				return 1u;
			case UpscalePreset::kUltraQuality:
				return 2u;
			case UpscalePreset::kQuality:
				return 3u;
			case UpscalePreset::kBalanced:
				return 4u;
			case UpscalePreset::kPerformance:
				return 5u;
			case UpscalePreset::kUltraPerformance:
				return 6u;
			default:
				return 0u;
			}
		}

		inline UpscalePreset QualityModeToUpscalePreset(uint32_t mode)
		{
			switch (mode) {
			case 1:
				return UpscalePreset::kHoshipa;
			case 2:
				return UpscalePreset::kUltraQuality;
			case 3:
				return UpscalePreset::kQuality;
			case 4:
				return UpscalePreset::kBalanced;
			case 5:
				return UpscalePreset::kPerformance;
			case 6:
				return UpscalePreset::kUltraPerformance;
			default:
				return UpscalePreset::kNativeAA;
			}
		}

		inline bool IsValidDLSSProfile(DLSSProfile profile)
		{
			switch (profile) {
			case DLSSProfile::kJ:
			case DLSSProfile::kK:
			case DLSSProfile::kL:
			case DLSSProfile::kM:
			case DLSSProfile::kF:
				return true;
			default:
				return false;
			}
		}

		inline bool IsValidUpscaleMethod(UpscaleMethod method)
		{
			switch (method) {
			case UpscaleMethod::kNone:
			case UpscaleMethod::kTAA:
			case UpscaleMethod::kFSR:
			case UpscaleMethod::kDLSS:
				return true;
			default:
				return false;
			}
		}

		inline Upscaling::UpscaleMethod ToInternalUpscaleMethod(UpscaleMethod method)
		{
			switch (method) {
			case UpscaleMethod::kTAA:
				return Upscaling::UpscaleMethod::kTAA;
			case UpscaleMethod::kFSR:
				return Upscaling::UpscaleMethod::kFSR;
			case UpscaleMethod::kDLSS:
				return Upscaling::UpscaleMethod::kDLSS;
			case UpscaleMethod::kNone:
			default:
				return Upscaling::UpscaleMethod::kNONE;
			}
		}

		inline UpscaleMethod FromInternalUpscaleMethod(Upscaling::UpscaleMethod method)
		{
			switch (method) {
			case Upscaling::UpscaleMethod::kTAA:
				return UpscaleMethod::kTAA;
			case Upscaling::UpscaleMethod::kFSR:
				return UpscaleMethod::kFSR;
			case Upscaling::UpscaleMethod::kDLSS:
				return UpscaleMethod::kDLSS;
			case Upscaling::UpscaleMethod::kNONE:
			default:
				return UpscaleMethod::kNone;
			}
		}

		inline Upscaling::UpscaleMethod GetLegacyDLSSPreferredUpscaleMethod(const Upscaling& upscaling)
		{
			return upscaling.GetLegacyDLSSPreferredUpscaleMethodForAPI();
		}

		inline bool ShouldBlockVRUpscalingApply(const char* action)
		{
			const uint32_t reasons = globals::features::upscaling.GetVRUpscalingApplyBlockReasonsForAPI();
			if (reasons == 0)
				return false;

			logger::debug(
				"[CS API] Ignoring {} while VR upscaling profile apply is blocked (reasons=0x{:X}).",
				action ? action : "VR upscaling API request",
				reasons);
			return true;
		}
	}

	inline CSInterface001 g_interface001;

	// Constructs and returns an API of the revision number requested.
	inline void* GetApi(unsigned int revisionNumber)
	{
		// Accept revision 0 as "latest" and keep older revisions available for
		// already-compiled consumers. New revisions only append vtable entries.
		if (revisionNumber != 0 &&
		    revisionNumber != CSInterfaceRevision001 &&
		    revisionNumber != CSInterfaceRevision002 &&
		    revisionNumber != CSInterfaceRevision) {
			return nullptr;
		}

		return &g_interface001;
	}

	// Handles SKSE mod messages requesting to fetch API functions from Community Shaders.
	inline void ModMessageHandler(SKSE::MessagingInterface::Message* message)
	{
		if (!detail::IsValidInterfaceRequest(message)) {
			return;
		}

		auto* csMessage = static_cast<CSMessage*>(message->data);
		csMessage->GetApiFunction = GetApi;
		logger::info("Provided Community Shaders plugin interface to {}", message->sender ? message->sender : "<unknown>");
	}

	// Fetches the version number.
	inline unsigned int CSInterface001::getBuildNumber()
	{
		return CSBuildNumber;
	}

	inline bool CSInterface001::GetSSSEnabled()
	{
		return globals::features::screenSpaceShadows.bendSettings.Enable != 0;
	}

	inline void CSInterface001::SetSSSEnabled(bool enabled)
	{
		using EnableFlag = decltype(globals::features::screenSpaceShadows.bendSettings.Enable);
		globals::features::screenSpaceShadows.bendSettings.Enable = detail::BoolToFlag<EnableFlag>(enabled);
	}

	inline bool CSInterface001::GetSSGIEnabled()
	{
		return globals::features::screenSpaceGI.settings.Enabled;
	}

	inline void CSInterface001::SetSSGIEnabled(bool enabled)
	{
		globals::features::screenSpaceGI.settings.Enabled = enabled;
	}

	inline bool CSInterface001::GetVolumetricLightingExteriorEnabled()
	{
		return globals::features::volumetricLighting.IsExteriorEnabled();
	}

	inline void CSInterface001::SetVolumetricLightingExteriorEnabled(bool enabled)
	{
		globals::features::volumetricLighting.SetExteriorEnabled(enabled);
	}

	inline UpscalePreset CSInterface001::GetUpscalePreset()
	{
		const uint32_t clampedMode = std::min(globals::features::upscaling.GetEffectiveDLSSQualityMode(), Upscaling::kQualityModeMaxIndex);
		return detail::QualityModeToUpscalePreset(clampedMode);
	}

	inline void CSInterface001::SetUpscalePreset(UpscalePreset preset)
	{
		if (!detail::IsValidUpscalePreset(preset)) {
			logger::warn("[CS API] Ignoring invalid upscaler preset value {}", static_cast<uint32_t>(preset));
			return;
		}
		if (detail::ShouldBlockVRUpscalingApply("upscaler preset change"))
			return;

		auto& upscaling = globals::features::upscaling;
		const uint32_t qualityMode = detail::UpscalePresetToQualityMode(preset);
		const auto upscaleMethod = detail::GetLegacyDLSSPreferredUpscaleMethod(upscaling);
		const bool renderScaleModeEnabled =
			upscaling.IsRenderScaleModeRequested() &&
			Upscaling::GetQualityModeResolutionScale(qualityMode) < 0.99f;
		upscaling.ApplyCSMenuUpscalingTransition(
			upscaleMethod,
			renderScaleModeEnabled,
			qualityMode,
			upscaling.GetEffectiveDLSSPreset(),
			"CS API upscaler preset change",
			Upscaling::VRUpscalingTransitionOrigin::VRAPI);
	}

	inline bool CSInterface001::GetLightLimitFixContactShadowsEnabled()
	{
		return globals::features::lightLimitFix.settings.EnableContactShadows;
	}

	inline void CSInterface001::SetLightLimitFixContactShadowsEnabled(bool enabled)
	{
		globals::features::lightLimitFix.settings.EnableContactShadows = enabled;
	}

	inline DLSSProfile CSInterface001::GetDLSSProfile()
	{
		const uint32_t clampedProfile = std::min(globals::features::upscaling.GetEffectiveDLSSPreset(), Upscaling::kDLSSPresetMaxIndex);
		return static_cast<DLSSProfile>(clampedProfile);
	}

	inline void CSInterface001::SetDLSSProfile(DLSSProfile profile)
	{
		if (!detail::IsValidDLSSProfile(profile)) {
			logger::warn("[CS API] Ignoring invalid DLSS profile value {}", static_cast<uint32_t>(profile));
			return;
		}
		if (detail::ShouldBlockVRUpscalingApply("DLSS profile change"))
			return;

		auto& upscaling = globals::features::upscaling;
		const uint32_t dlssPreset = static_cast<uint32_t>(profile);
		const auto upscaleMethod = detail::GetLegacyDLSSPreferredUpscaleMethod(upscaling);
		const bool stageVRDLSSProfileChange = globals::game::isVR && upscaleMethod == Upscaling::UpscaleMethod::kDLSS;

		if (stageVRDLSSProfileChange) {
			upscaling.ApplyCSMenuUpscalingTransition(
				upscaleMethod,
				upscaling.GetPerfModeRequested(),
				upscaling.GetEffectiveDLSSQualityMode(),
				dlssPreset,
				"CS API legacy DLSS profile change",
				Upscaling::VRUpscalingTransitionOrigin::VRAPI);
			return;
		}

		if (upscaling.settings.dlssPreset == dlssPreset)
			return;

		upscaling.settings.dlssPreset = dlssPreset;
		upscaling.RequestHistoryReset();
	}

	inline bool CSInterface001::GetRenderAtUpscaleResEnabled()
	{
		return globals::features::upscaling.GetPerfModeRequested();
	}

	inline void CSInterface001::SetRenderAtUpscaleResEnabled(bool enabled)
	{
		if (detail::ShouldBlockVRUpscalingApply("render-scale mode change"))
			return;

		globals::features::upscaling.SetPerfModeRequested(
			enabled,
			"CS API render-scale mode change",
			true,
			Upscaling::VRUpscalingTransitionOrigin::VRAPI);
	}

	inline bool CSInterface001::GetRenderAtUpscaleResActive()
	{
		return globals::features::upscaling.IsPerfModeActive();
	}

	inline void CSInterface001::SetVRUpscalingTransitionProfile(bool renderScaleModeEnabled, UpscalePreset preset, DLSSProfile profile)
	{
		if (!detail::IsValidUpscalePreset(preset)) {
			logger::warn("[CS API] Ignoring invalid transition upscaler preset value {}", static_cast<uint32_t>(preset));
			return;
		}
		if (!detail::IsValidDLSSProfile(profile)) {
			logger::warn("[CS API] Ignoring invalid transition DLSS profile value {}", static_cast<uint32_t>(profile));
			return;
		}
		if (detail::ShouldBlockVRUpscalingApply("legacy DLSS-preferred VR upscaling transition profile"))
			return;

		auto& upscaling = globals::features::upscaling;
		const uint32_t qualityMode = detail::UpscalePresetToQualityMode(preset);
		const uint32_t dlssPreset = static_cast<uint32_t>(profile);
		upscaling.ApplyCSMenuUpscalingTransition(
			detail::GetLegacyDLSSPreferredUpscaleMethod(upscaling),
			renderScaleModeEnabled,
			qualityMode,
			dlssPreset,
			"CS API legacy DLSS-preferred VR upscaling transition profile",
			Upscaling::VRUpscalingTransitionOrigin::VRAPI);
	}

	inline UpscaleMethod CSInterface001::GetUpscaleMethod()
	{
		return detail::FromInternalUpscaleMethod(globals::features::upscaling.GetConfiguredUpscaleMethodForTransition());
	}

	inline void CSInterface001::SetUpscaleMethod(UpscaleMethod method)
	{
		if (!detail::IsValidUpscaleMethod(method)) {
			logger::warn("[CS API] Ignoring invalid upscaler method value {}", static_cast<uint32_t>(method));
			return;
		}
		if (detail::ShouldBlockVRUpscalingApply("upscaler method change"))
			return;

		auto& upscaling = globals::features::upscaling;
		upscaling.ApplyCSMenuUpscalingTransition(
			detail::ToInternalUpscaleMethod(method),
			upscaling.IsRenderScaleModeRequested(),
			upscaling.GetEffectiveDLSSQualityMode(),
			upscaling.GetEffectiveDLSSPreset(),
			"CS API upscaler method change",
			Upscaling::VRUpscalingTransitionOrigin::VRAPI);
	}

	inline void CSInterface001::SetVRUpscalingTransitionProfileForMethod(UpscaleMethod method, bool renderScaleModeEnabled, UpscalePreset preset, DLSSProfile profile)
	{
		if (!detail::IsValidUpscaleMethod(method)) {
			logger::warn("[CS API] Ignoring invalid transition upscaler method value {}", static_cast<uint32_t>(method));
			return;
		}
		if (!detail::IsValidUpscalePreset(preset)) {
			logger::warn("[CS API] Ignoring invalid transition upscaler preset value {}", static_cast<uint32_t>(preset));
			return;
		}
		if (!detail::IsValidDLSSProfile(profile)) {
			logger::warn("[CS API] Ignoring invalid transition DLSS profile value {}", static_cast<uint32_t>(profile));
			return;
		}
		if (detail::ShouldBlockVRUpscalingApply("VR method-specific upscaling transition profile"))
			return;

		auto& upscaling = globals::features::upscaling;
		const uint32_t qualityMode = detail::UpscalePresetToQualityMode(preset);
		const uint32_t dlssPreset = static_cast<uint32_t>(profile);
		upscaling.ApplyCSMenuUpscalingTransition(
			detail::ToInternalUpscaleMethod(method),
			renderScaleModeEnabled,
			qualityMode,
			dlssPreset,
			"CS API VR method-specific upscaling transition profile",
			Upscaling::VRUpscalingTransitionOrigin::VRAPI);
	}

	inline uint32_t CSInterface001::GetVRUpscalingApplyBlockReasons()
	{
		return globals::features::upscaling.GetVRUpscalingApplyBlockReasonsForAPI();
	}

	inline bool CSInterface001::IsVRUpscalingProfileApplyAllowed()
	{
		return GetVRUpscalingApplyBlockReasons() == 0;
	}
}  // namespace CSPluginAPI
