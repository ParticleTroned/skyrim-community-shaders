#include "Features/Upscaling/VRRenderScaleModePolicy.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>

struct Upscaling
{
	enum class UpscaleMethod
	{
		kNONE,
		kTAA,
		kFSR,
		kDLSS
	};
	enum class VRUpscalingTransitionOrigin
	{
		VRAPI
	};
	enum class VRRenderScaleTransitionState
	{
		Idle,
		Active,
		Pending
	};
	static constexpr uint32_t kQualityModeMaxIndex = 6;
	struct VRFpsStabilizerProfile
	{
		bool hasUpscaleMethod = true;
		UpscaleMethod upscaleMethod = UpscaleMethod::kDLSS;
		bool hasLegacyMethodSelection = false;
		bool hasQualityMode = true;
		uint32_t qualityMode = 0;
		bool hasDLSSPreset = true;
		uint32_t dlssPreset = 1;
		bool hasRenderScaleMode = true;
		bool renderScaleMode = false;
		bool HasAnyUpscalingSetting() const
		{
			return hasUpscaleMethod || hasLegacyMethodSelection || hasQualityMode || hasDLSSPreset || hasRenderScaleMode;
		}
	};
	struct Config
	{
		VRFpsStabilizerProfile interior, exterior;
	} config;
	VRFpsStabilizerProfile current;
	bool syncActive = true;
	uint32_t blockReasons = 0;
	uint64_t availableSerial = 0, clearedSerial = 0;
	unsigned int applications = 0;
	bool latched = false, converged = true;
	bool pendingVendorReset = false, pendingTransition = false;
	VRRenderScaleTransitionState controllerState = VRRenderScaleTransitionState::Active;
	std::atomic_bool pendingPerfModeRenderTargetRecreate{ false };
	std::atomic_bool perfModeRenderTargetRecreateInProgress{ false };
	std::atomic_bool postLoadRuntimeResetPending{ false };
	struct PerfMode
	{
		bool restartRequired = false;
		bool HasRestartRequiredChange() const { return restartRequired; }
	} perfMode;
	bool IsVRFpsStabilizerSyncActive() const { return syncActive; }
	const Config& GetVRFpsStabilizerSessionConfig() const { return config; }
	UpscaleMethod GetConfiguredUpscaleMethodForTransition() const { return current.upscaleMethod; }
	UpscaleMethod GetLegacyDLSSPreferredUpscaleMethodForAPI() const { return UpscaleMethod::kDLSS; }
	uint32_t GetEffectiveUpscalingQualityMode() const { return current.qualityMode; }
	uint32_t GetEffectiveDLSSPreset() const { return current.dlssPreset; }
	bool GetVRRenderScaleModePreference() const { return current.renderScaleMode; }
	bool GetVRRenderScalePreferenceForSelection(UpscaleMethod) const { return current.renderScaleMode; }
	bool IsRenderScaleModeRequested() const;
	bool GetPerfModeRequested() const { return IsRenderScaleModeRequested(); }
	bool IsVRRenderScaleModeLatched() const { return latched; }
	bool IsVRRenderScalePhysicalContractConverged(UpscaleMethod, uint32_t) const { return converged; }
	struct Controller
	{
		VRRenderScaleTransitionState state;
	};
	Controller GetVRRenderScaleTransitionSnapshot() const { return { controllerState }; }
	bool HasPendingVRUpscalingTransition() const { return pendingTransition; }
	uint32_t GetVRUpscalingApplyBlockReasonsForAPI() const { return blockReasons; }
	uint64_t CanBufferVRFpsStabilizerAPITransitionProfile(uint32_t) { return availableSerial; }
	void ClearVRFpsStabilizerAPITransitionProfileAdmission(uint64_t serial) { clearedSerial = serial; }
	bool IsVRFpsStabilizerAPITransitionProfileAllowed(UpscaleMethod, bool, uint32_t, uint32_t, uint64_t) const;
	bool IsVRUpscalingTransitionProfileNoOp(UpscaleMethod, bool, uint32_t, uint32_t) const;
	void ApplyCSMenuUpscalingTransition(UpscaleMethod method, bool scale, uint32_t quality, uint32_t dlss, const char*, VRUpscalingTransitionOrigin, uint64_t)
	{
		++applications;
		current.upscaleMethod = method;
		current.renderScaleMode = VRRenderScaleModePolicy::Resolve(method == UpscaleMethod::kFSR || method == UpscaleMethod::kDLSS, quality != 0, scale).preference;
		current.qualityMode = quality;
		current.dlssPreset = dlss;
		latched = IsRenderScaleModeRequested();
	}
};
using VRFpsStabilizerProfile = Upscaling::VRFpsStabilizerProfile;
struct VRFpsStabilizerTransitionTarget
{
	Upscaling::UpscaleMethod method;
	uint32_t qualityMode, dlssPreset;
	bool renderScaleModePreference, renderScaleMode;
};
constexpr uint32_t kDefaultRenderScaleQualityMode = 3;
bool IsRenderScaleMethodEligible(Upscaling::UpscaleMethod method)
{
	return method == Upscaling::UpscaleMethod::kFSR || method == Upscaling::UpscaleMethod::kDLSS;
}
bool IsRenderScaleQualityMode(uint32_t quality) { return quality != 0; }
uint32_t ClampDLSSPresetUInt(uint32_t preset) { return std::min(preset, 4u); }
bool HasPendingVRVendorRuntimeReset(const Upscaling& upscaling, Upscaling::UpscaleMethod) { return upscaling.pendingVendorReset; }
bool Upscaling::IsRenderScaleModeRequested() const
{
	return VRRenderScaleModePolicy::Resolve(IsRenderScaleMethodEligible(current.upscaleMethod), IsRenderScaleQualityMode(current.qualityMode), current.renderScaleMode).enabled;
}
namespace globals::features
{
	Upscaling upscaling;
}
namespace globals::game
{
	bool isVR = true;
}
namespace Util
{
	bool interior = false;
	bool IsInterior() { return interior; }
}
namespace logger
{
	template <class... Args>
	void warn(const char*, Args&&...)
	{}
	template <class... Args>
	void debug(const char*, Args&&...)
	{}
}
namespace CSPluginAPI
{
	enum class UpscaleMethod : uint32_t
	{
		kNone,
		kTAA,
		kFSR,
		kDLSS
	};
	enum class UpscalePreset : uint32_t
	{
		kNativeAA,
		kQuality,
		kBalanced,
		kPerformance,
		kUltraPerformance,
		kHoshipa,
		kUltraQuality
	};
	enum class DLSSProfile : uint32_t
	{
		kJ,
		kK,
		kL,
		kM,
		kF
	};
	enum class VRUpscalingTransitionProfileDecision : uint32_t
	{
		kBlocked,
		kNoChange,
		kApply
	};
	struct CSInterface001
	{
		VRUpscalingTransitionProfileDecision GetVRUpscalingTransitionProfileDecision(UpscaleMethod, bool, UpscalePreset, DLSSProfile);
		void SetVRUpscalingTransitionProfileForMethod(UpscaleMethod, bool, UpscalePreset, DLSSProfile);
	};
}
#include "stabilizer_api_under_test.h"

namespace
{
	unsigned int checks = 0;
	void Require(bool value, const std::source_location where = std::source_location::current())
	{
		++checks;
		if (!value)
			throw std::runtime_error("Failed at line " + std::to_string(where.line()));
	}
}

int main()
{
	using namespace CSPluginAPI;
	using Decision = VRUpscalingTransitionProfileDecision;
	CSInterface001 api;
	auto& upscaling = globals::features::upscaling;
	try {
		// The reported exterior request must apply while standing outdoors.
		upscaling.config.exterior.qualityMode = 1;
		upscaling.config.exterior.renderScaleMode = true;
		Require(api.GetVRUpscalingTransitionProfileDecision(UpscaleMethod::kDLSS, true, UpscalePreset::kHoshipa, DLSSProfile::kK) == Decision::kApply);
		api.SetVRUpscalingTransitionProfileForMethod(UpscaleMethod::kDLSS, true, UpscalePreset::kHoshipa, DLSSProfile::kK);
		Require(upscaling.applications == 1);
		Require(api.GetVRUpscalingTransitionProfileDecision(UpscaleMethod::kDLSS, true, UpscalePreset::kHoshipa, DLSSProfile::kK) == Decision::kNoChange);

		for (uint32_t method = 0; method < 4; ++method) {
			for (uint32_t preset = 0; preset < 7; ++preset) {
				for (uint32_t dlss = 0; dlss < 5; ++dlss) {
					for (uint32_t scale = 0; scale < 2; ++scale) {
						for (uint32_t timing = 0; timing < 4; ++timing) {
							const auto apiMethod = static_cast<UpscaleMethod>(method);
							const auto apiPreset = static_cast<UpscalePreset>(preset);
							const auto apiDLSS = static_cast<DLSSProfile>(dlss);
							const auto quality = detail::UpscalePresetToQualityMode(apiPreset);
							const bool eligible = method >= 2;
							Util::interior = (timing & 1) != 0;
							upscaling.availableSerial = (timing & 2) != 0 ? 42 : 0;
							upscaling.blockReasons = upscaling.availableSerial != 0 ? 4 : 0;
							upscaling.config.interior = {};
							upscaling.config.exterior = {};
							upscaling.current = {};
							upscaling.current.upscaleMethod = method == 0 ? Upscaling::UpscaleMethod::kTAA : Upscaling::UpscaleMethod::kNONE;
							upscaling.latched = false;
							upscaling.applications = 0;
							auto& target = Util::interior ? upscaling.config.interior : upscaling.config.exterior;
							target.upscaleMethod = static_cast<Upscaling::UpscaleMethod>(method);
							target.qualityMode = quality;
							target.dlssPreset = dlss;
							target.renderScaleMode = scale != 0;
							Require(api.GetVRUpscalingTransitionProfileDecision(apiMethod, scale != 0, apiPreset, apiDLSS) == Decision::kApply);
							api.SetVRUpscalingTransitionProfileForMethod(apiMethod, scale != 0, apiPreset, apiDLSS);
							Require(upscaling.applications == 1 && upscaling.current.qualityMode == quality);
							Require(upscaling.latched == (eligible && quality != 0 && scale != 0));
							Require(api.GetVRUpscalingTransitionProfileDecision(apiMethod, scale != 0, apiPreset, apiDLSS) == Decision::kNoChange);
							Require(upscaling.clearedSerial == upscaling.availableSerial);
							if (method != 3)
								Require(api.GetVRUpscalingTransitionProfileDecision(apiMethod, scale != 0, apiPreset, static_cast<DLSSProfile>((dlss + 1) % 5)) == Decision::kNoChange);
							// A pre-move call uses the same configured target on the other side.
							Util::interior = !Util::interior;
							upscaling.current.upscaleMethod = method == 0 ? Upscaling::UpscaleMethod::kTAA : Upscaling::UpscaleMethod::kNONE;
							upscaling.latched = false;
							Require(api.GetVRUpscalingTransitionProfileDecision(apiMethod, scale != 0, apiPreset, apiDLSS) == Decision::kApply);
						}
					}
				}
			}
		}

		upscaling.availableSerial = 0;
		upscaling.blockReasons = 0;
		upscaling.config.interior = {};
		upscaling.config.interior.qualityMode = 4;
		upscaling.config.exterior = {};
		upscaling.config.exterior.qualityMode = 1;
		upscaling.config.exterior.renderScaleMode = true;
		upscaling.current = upscaling.config.exterior;
		upscaling.current.renderScaleMode = false;
		upscaling.current.upscaleMethod = Upscaling::UpscaleMethod::kDLSS;
		upscaling.current.qualityMode = 0;
		upscaling.current.dlssPreset = 1;
		upscaling.latched = false;
		// Completed work needs no mutation authority for another configured profile.
		Require(api.GetVRUpscalingTransitionProfileDecision(UpscaleMethod::kDLSS, false, UpscalePreset::kNativeAA, DLSSProfile::kK) == Decision::kNoChange);
		for (uint32_t mask = 1; mask < 128; ++mask) {
			upscaling.blockReasons = mask;
			Require(api.GetVRUpscalingTransitionProfileDecision(UpscaleMethod::kDLSS, false, UpscalePreset::kNativeAA, DLSSProfile::kK) == Decision::kBlocked);
			const auto before = upscaling.applications;
			api.SetVRUpscalingTransitionProfileForMethod(UpscaleMethod::kDLSS, false, UpscalePreset::kNativeAA, DLSSProfile::kK);
			Require(upscaling.applications == before);
		}
		upscaling.blockReasons = 0;
		for (uint32_t invalid : { 7u, 99u, UINT32_MAX }) {
			Require(api.GetVRUpscalingTransitionProfileDecision(static_cast<UpscaleMethod>(invalid), false, UpscalePreset::kNativeAA, DLSSProfile::kK) == Decision::kBlocked);
			Require(api.GetVRUpscalingTransitionProfileDecision(UpscaleMethod::kDLSS, false, static_cast<UpscalePreset>(invalid), DLSSProfile::kK) == Decision::kBlocked);
			Require(api.GetVRUpscalingTransitionProfileDecision(UpscaleMethod::kDLSS, false, UpscalePreset::kNativeAA, static_cast<DLSSProfile>(invalid)) == Decision::kBlocked);
		}
		upscaling.config.exterior = {};
		upscaling.config.exterior.qualityMode = 1;
		upscaling.config.exterior.renderScaleMode = true;
		upscaling.current = upscaling.config.exterior;
		upscaling.latched = true;
		for (uint32_t unsettled = 0; unsettled < 8; ++unsettled) {
			upscaling.controllerState = unsettled == 0 ? Upscaling::VRRenderScaleTransitionState::Pending : Upscaling::VRRenderScaleTransitionState::Active;
			upscaling.pendingTransition = unsettled == 1;
			upscaling.pendingPerfModeRenderTargetRecreate = unsettled == 2;
			upscaling.perfModeRenderTargetRecreateInProgress = unsettled == 3;
			upscaling.postLoadRuntimeResetPending = unsettled == 4;
			upscaling.pendingVendorReset = unsettled == 5;
			upscaling.perfMode.restartRequired = unsettled == 6;
			upscaling.converged = unsettled != 7;
			Require(!upscaling.IsVRUpscalingTransitionProfileNoOp(Upscaling::UpscaleMethod::kDLSS, true, 1, 1));
		}
		upscaling.converged = true;
		// Targets outside both configured profiles remain rejected.
		upscaling.current.upscaleMethod = Upscaling::UpscaleMethod::kNONE;
		Require(api.GetVRUpscalingTransitionProfileDecision(UpscaleMethod::kDLSS, false, UpscalePreset::kPerformance, DLSSProfile::kK) == Decision::kBlocked);
		upscaling.syncActive = false;
		Require(api.GetVRUpscalingTransitionProfileDecision(UpscaleMethod::kDLSS, false, UpscalePreset::kBalanced, DLSSProfile::kK) == Decision::kApply);
		globals::game::isVR = false;
		Require(api.GetVRUpscalingTransitionProfileDecision(UpscaleMethod::kDLSS, false, UpscalePreset::kBalanced, DLSSProfile::kK) == Decision::kApply);
		std::cout << checks << " Stabilizer API assertions passed.\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
