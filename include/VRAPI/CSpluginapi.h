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

inline constexpr unsigned int CSBuildNumber = 7;

namespace CSPluginAPI
{
	void* GetApi(unsigned int revisionNumber);

	void ModMessageHandler(SKSE::MessagingInterface::Message* message);

	// This object provides access to Community Shaders' mod support API version 1.
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

		virtual DLSSMode GetDLSSMode() override;
		virtual void SetDLSSMode(DLSSMode mode) override;

		virtual bool GetLightLimitFixContactShadowsEnabled() override;
		virtual void SetLightLimitFixContactShadowsEnabled(bool enabled) override;

		virtual DLSSProfile GetDLSSProfile() override;
		virtual void SetDLSSProfile(DLSSProfile profile) override;

		virtual bool GetRenderAtUpscaleResEnabled() override;
		virtual void SetRenderAtUpscaleResEnabled(bool enabled) override;
		virtual bool GetRenderAtUpscaleResActive() override;
		virtual void SetVRUpscalingTransitionProfile(bool renderAtUpscaleResEnabled, DLSSMode mode, DLSSProfile profile) override;
	};

	namespace detail
	{
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

		inline bool IsValidDLSSMode(DLSSMode mode)
		{
			switch (mode) {
			case DLSSMode::kDLAA:
			case DLSSMode::kQuality:
			case DLSSMode::kBalanced:
			case DLSSMode::kPerformance:
			case DLSSMode::kUltraPerformance:
			case DLSSMode::kHoshipa:
			case DLSSMode::kUltraQuality:
				return true;
			default:
				return false;
			}
		}

		inline uint32_t DLSSModeToQualityMode(DLSSMode mode)
		{
			switch (mode) {
			case DLSSMode::kDLAA:
				return 0u;
			case DLSSMode::kHoshipa:
				return 1u;
			case DLSSMode::kUltraQuality:
				return 2u;
			case DLSSMode::kQuality:
				return 3u;
			case DLSSMode::kBalanced:
				return 4u;
			case DLSSMode::kPerformance:
				return 5u;
			case DLSSMode::kUltraPerformance:
				return 6u;
			default:
				return 0u;
			}
		}

		inline DLSSMode QualityModeToDLSSMode(uint32_t mode)
		{
			switch (mode) {
			case 1:
				return DLSSMode::kHoshipa;
			case 2:
				return DLSSMode::kUltraQuality;
			case 3:
				return DLSSMode::kQuality;
			case 4:
				return DLSSMode::kBalanced;
			case 5:
				return DLSSMode::kPerformance;
			case 6:
				return DLSSMode::kUltraPerformance;
			default:
				return DLSSMode::kDLAA;
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
	}

	inline CSInterface001 g_interface001;

	// Constructs and returns an API of the revision number requested.
	inline void* GetApi(unsigned int revisionNumber)
	{
		// Accept revision 0 as "latest" in addition to explicit revision 1.
		if (revisionNumber != 0 && revisionNumber != CSInterfaceRevision) {
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

	inline DLSSMode CSInterface001::GetDLSSMode()
	{
		const uint32_t clampedMode = std::min(globals::features::upscaling.GetEffectiveDLSSQualityMode(), Upscaling::kQualityModeMaxIndex);
		return detail::QualityModeToDLSSMode(clampedMode);
	}

	inline void CSInterface001::SetDLSSMode(DLSSMode mode)
	{
		if (!detail::IsValidDLSSMode(mode)) {
			logger::warn("[CS API] Ignoring invalid upscaler preset value {}", static_cast<uint32_t>(mode));
			return;
		}

		auto& upscaling = globals::features::upscaling;
		const uint32_t qualityMode = detail::DLSSModeToQualityMode(mode);
		const auto upscaleMethod = upscaling.GetUpscaleMethod();
		const bool stageVRUpscalingChange =
			globals::game::isVR &&
			(upscaleMethod == Upscaling::UpscaleMethod::kDLSS || upscaleMethod == Upscaling::UpscaleMethod::kFSR);

		if (stageVRUpscalingChange) {
			upscaling.SetVRUpscalingTransitionProfile(upscaling.GetPerfModeRequested(), qualityMode, upscaling.GetEffectiveDLSSPreset(), "CS API upscaler preset change");
			return;
		}

		const bool renderScaleModeChanged = upscaling.SyncRenderScaleModeForQuality(qualityMode, "CS API native upscaler preset change");
		if (upscaling.settings.qualityMode == qualityMode) {
			if (renderScaleModeChanged) {
				upscaling.RequestHistoryReset();
				upscaling.RequestPerfModeRenderTargetRecreate("CS API upscaler preset change");
			}
			return;
		}

		upscaling.settings.qualityMode = qualityMode;
		upscaling.RequestHistoryReset();
		upscaling.RequestPerfModeRenderTargetRecreate("CS API upscaler preset change");
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

		auto& upscaling = globals::features::upscaling;
		const uint32_t dlssPreset = static_cast<uint32_t>(profile);
		const bool stageVRDLSSProfileChange = globals::game::isVR && upscaling.GetUpscaleMethod() == Upscaling::UpscaleMethod::kDLSS;

		if (stageVRDLSSProfileChange) {
			upscaling.SetVRUpscalingTransitionProfile(upscaling.GetPerfModeRequested(), upscaling.GetEffectiveDLSSQualityMode(), dlssPreset, "CS API DLSS profile change");
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
		globals::features::upscaling.SetPerfModeRequested(enabled, "CS API render-at-upscale-res change", true);
	}

	inline bool CSInterface001::GetRenderAtUpscaleResActive()
	{
		return globals::features::upscaling.IsPerfModeActive();
	}

	inline void CSInterface001::SetVRUpscalingTransitionProfile(bool renderAtUpscaleResEnabled, DLSSMode mode, DLSSProfile profile)
	{
		if (!detail::IsValidDLSSMode(mode)) {
			logger::warn("[CS API] Ignoring invalid transition upscaler preset value {}", static_cast<uint32_t>(mode));
			return;
		}
		if (!detail::IsValidDLSSProfile(profile)) {
			logger::warn("[CS API] Ignoring invalid transition DLSS profile value {}", static_cast<uint32_t>(profile));
			return;
		}

		auto& upscaling = globals::features::upscaling;
		const uint32_t qualityMode = detail::DLSSModeToQualityMode(mode);
		const uint32_t dlssPreset = static_cast<uint32_t>(profile);
		upscaling.SetVRUpscalingTransitionProfile(renderAtUpscaleResEnabled, qualityMode, dlssPreset, "CS API VR upscaling transition profile");
	}
}  // namespace CSPluginAPI
