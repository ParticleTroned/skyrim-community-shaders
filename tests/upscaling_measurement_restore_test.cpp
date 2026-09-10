#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include <magic_enum/magic_enum.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace logger
{
	int warnings = 0;
	std::string rejection;
	void warn(std::string_view, std::string_view reason)
	{
		++warnings;
		rejection = reason;
	}
}

// Compile the production restore body against observable transition effects.
// Rejected physical changes must still restore independent user preferences.
struct Upscaling
{
#include "upscaling_measurement_restore_types.h"

	enum class UpscaleMethod
	{
		kNONE,
		kTAA,
		kFSR,
		kDLSS
	};
	enum class VRUpscalingTransitionOrigin
	{
		CSMenu
	};
	struct Settings
	{
		uint32_t upscaleMethod = 0;
		uint32_t upscaleMethodNoDLSS = 0;
		uint32_t qualityMode = 0;
		uint32_t dlssPreset = 0;
		uint32_t renderScaleMode = 0;
		bool fsr4RuntimeEnable = false;
		bool renderScaleLinkedToUpscaling = false;
		bool foveatedVendorDispatch = false;
		bool periphery_taa_enable = false;
		bool sanitized = false;
	} settings;

	UpscalingTransitionApplyResult result{ .disposition = UpscalingTransitionApplyDisposition::AppliedSynchronously };
	int transitions = 0;
	int invalidations = 0;
	json requested;

	bool ApplyOpenCompositeUpscalingBlocker(bool)
	{
		return result.rejection == UpscalingTransitionApplyRejection::OpenComposite;
	}

	UpscaleMethod ResolvePerformanceCostMeasurementMethod(uint32_t primary, uint32_t)
	{
		return static_cast<UpscaleMethod>(primary);
	}

	UpscalingTransitionApplyResult ApplyCSMenuUpscalingTransition(
		UpscaleMethod method, bool renderScale, uint32_t quality, uint32_t preset,
		const char*, VRUpscalingTransitionOrigin, uint64_t, bool fsr4)
	{
		++transitions;
		requested = {
			{ "upscaleMethod", static_cast<uint32_t>(method) },
			{ "renderScaleMode", renderScale ? 1u : 0u },
			{ "qualityMode", quality }, { "dlssPreset", preset },
			{ "fsr4RuntimeEnable", fsr4 }
		};
		if (ApplyOpenCompositeUpscalingBlocker(true))
			return result;
		if (result.disposition == UpscalingTransitionApplyDisposition::AppliedSynchronously) {
			settings.upscaleMethod = static_cast<uint32_t>(method);
			settings.renderScaleMode = renderScale ? 1u : 0u;
		}
		return result;
	}

	void InvalidateFrameScopedUpscalingState() { ++invalidations; }
	void RestorePerformanceCostMeasurementState(const json& state);
};

uint32_t ClampQualityModeUInt(uint32_t value) { return std::min(value, 5u); }
uint32_t ClampDLSSPresetUInt(uint32_t value) { return std::min(value, 4u); }
uint32_t ClampToggleUInt(uint32_t value) { return std::min(value, 1u); }
void SanitizeFoveatedSettings(Upscaling::Settings& settings) { settings.sanitized = true; }

#include "upscaling_measurement_restore_under_test.h"

int main()
{
	using Disposition = Upscaling::UpscalingTransitionApplyDisposition;
	using Rejection = Upscaling::UpscalingTransitionApplyRejection;
	int failures = 0;
	int cases = 0;
	auto check = [&](bool passed, const std::string& name) {
		++cases;
		if (!passed) {
			++failures;
			std::cerr << "Failed: " << name << '\n';
		}
	};

	for (bool saved : { false, true }) {
		const json state{
			{ "upscaleMethod", 3u }, { "upscaleMethodNoDLSS", 2u },
			{ "qualityMode", 3u }, { "dlssPreset", 2u },
			{ "renderScaleMode", 1u }, { "fsr4RuntimeEnable", true },
			{ "renderScaleLinkedToUpscaling", saved },
			{ "foveatedVendorDispatch", saved },
			{ "periphery_taa_enable", !saved }
		};
		for (auto disposition : magic_enum::enum_values<Disposition>()) {
			for (auto rejection : magic_enum::enum_values<Rejection>()) {
				const bool rejected = disposition == Disposition::Rejected;
				if (rejected == (rejection == Rejection::None))
					continue;
				Upscaling upscaling;
				upscaling.result = { .disposition = disposition, .rejection = rejection };
				upscaling.settings.renderScaleLinkedToUpscaling = !saved;
				upscaling.settings.foveatedVendorDispatch = !saved;
				upscaling.settings.periphery_taa_enable = saved;
				logger::warnings = 0;
				logger::rejection.clear();
				upscaling.RestorePerformanceCostMeasurementState(state);
				const auto& restored = upscaling.settings;
				const bool physicallyApplied = disposition == Disposition::AppliedSynchronously;
				check(
					restored.renderScaleLinkedToUpscaling == saved &&
						restored.foveatedVendorDispatch == saved &&
						restored.periphery_taa_enable == !saved &&
						restored.sanitized && upscaling.invalidations == 1 &&
						upscaling.transitions == 1 &&
						upscaling.requested == json({ { "upscaleMethod", 3u },
												   { "renderScaleMode", 1u }, { "qualityMode", 3u },
												   { "dlssPreset", 2u }, { "fsr4RuntimeEnable", true } }) &&
						restored.upscaleMethod == (physicallyApplied ? 3u : 0u) &&
						restored.renderScaleMode == (physicallyApplied ? 1u : 0u) &&
						logger::warnings == (rejected ? 1 : 0) &&
						(!rejected || logger::rejection == magic_enum::enum_name(rejection)),
					std::string(magic_enum::enum_name(disposition)) + "/" +
						std::string(magic_enum::enum_name(rejection)) + (saved ? "/on" : "/off"));
			}
		}
		Upscaling upscaling;
		upscaling.settings.renderScaleLinkedToUpscaling = saved;
		upscaling.settings.foveatedVendorDispatch = !saved;
		upscaling.settings.periphery_taa_enable = saved;
		upscaling.RestorePerformanceCostMeasurementState(json::object());
		check(upscaling.settings.renderScaleLinkedToUpscaling == saved &&
				  upscaling.settings.foveatedVendorDispatch == !saved &&
				  upscaling.settings.periphery_taa_enable == saved,
			"missing preferences preserve current values");
	}
	for (const json& invalid : { json(nullptr), json::array(), json(true), json(7) }) {
		Upscaling upscaling;
		logger::warnings = 0;
		upscaling.RestorePerformanceCostMeasurementState(invalid);
		check(upscaling.transitions == 0 && upscaling.invalidations == 0 &&
				  !upscaling.settings.sanitized && logger::warnings == 0,
			"non-object snapshot has no effects");
	}
	std::cout << (cases - failures) << '/' << cases << " restore scenarios passed\n";
	return failures ? 1 : 0;
}
