#include "Features/Upscaling/FSRTemporalTuningSerialization.h"
#include "Features/Upscaling/FoveatedMaskCalibrationJson.h"
#include "Features/Upscaling/NeuralRendering/CharacterSettingsJson.h"
#include "Features/Upscaling/NeuralRendering/ConfigurationSerialization.h"
#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"

#include <cstdlib>
#include <format>
#include <iostream>
#include <tuple>

using uint = unsigned;
using json = nlohmann::json;
namespace REL
{
	struct Module
	{
		static inline bool vr = true;
		static bool IsVR() { return vr; }
	};
}
namespace logger
{
	template <class... Args>
	void warn(const char*, Args&&...)
	{}
}
struct Upscaling
{
#include "upscaling_settings_enums.h"
#include "upscaling_settings_members.h"
#include "upscaling_settings_sharpener.h"
};
#include "upscaling_settings_serialization_under_test.h"

void Check(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << '\n';
		std::exit(1);
	}
}

int main()
{
	for (unsigned filter : { 0u, 1u }) {
		Upscaling::Settings settings;
		from_json({ { "neuralRenderingReconstructionFilter", filter } }, settings);
		Check(settings.neuralRenderingReconstructionFilter == filter && json(settings).at("neuralRenderingReconstructionFilter") == filter,
			"Reconstruction filter did not round-trip");
		for (const auto& invalid : { json(-1), json(2), json(1.0), json(true), json("lanczos3"), json(nullptr), json(0x100000001ULL) }) {
			const auto previous = settings;
			bool rejected = false;
			try {
				from_json({ { "neuralRenderingReconstructionFilter", invalid } }, settings);
			} catch (const std::invalid_argument&) {
				rejected = true;
			}
			Check(rejected && settings == previous, "Invalid filter partially changed the saved configuration");
		}
		from_json(json::object(), settings);
		Check(settings.neuralRenderingReconstructionFilter == 0, "Legacy settings must default to bilinear");
	}
	for (bool vr : { false, true }) {
		REL::Module::vr = vr;
		Upscaling::Settings settings;
		settings.foveatedPeripheryMaskVisualization = true;
		settings.foveatedCenterArea = .65f;
		settings.foveatedLeftEyeMaskOffsetX = -.08f;
		settings.foveatedRightEyeMaskOffsetY = .06f;
		settings.foveatedDisableOuterFeather = true;
		settings.foveatedAutomaticMaskScaling = 85;
		settings.foveatedMaskSetup = FoveatedMaskCalibration::CaptureSetupConfirmation(settings);
		settings.foveatedCalibrationReference.version = FoveatedMaskCalibration::kReferenceVersion;
		settings.foveatedCalibrationReference.leftToRight = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
		settings.foveatedCalibrationReference.coveragePercent = 85;
		settings.periphery_taa_enable = true;
		json saved = settings;
		Check(!saved.contains("foveatedPeripheryMaskVisualization"), "Saving must omit the session-only preview");
		Check(settings.foveatedPeripheryMaskVisualization, "Saving must not end the live setup preview");
		auto expected = settings;
		expected.foveatedPeripheryMaskVisualization = false;
		for (const json legacy : { json(true), json(false), json(nullptr), json("true") }) {
			auto input = saved;
			input["foveatedPeripheryMaskVisualization"] = legacy;
			from_json(input, settings);
			Check(settings == expected, "Legacy preview values must be ignored without altering saved mask controls");
			Check(FoveatedMaskCalibration::HasFovOnlySetup(settings), "Loading must retain the user's mask setup confirmation");
			Check(input["foveatedPeripheryMaskVisualization"] == legacy, "Loading must not modify the source profile");
		}
		settings.foveatedPeripheryMaskVisualization = true;
		from_json(saved, settings);
		Check(settings == expected, "Loading settings must disable preview and retain the fitted mask");
		auto extended = saved;
		extended["foveatedAutomaticMaskScaling"] = 130;
		from_json(extended, settings);
		Check(settings.foveatedAutomaticMaskScaling == 100 && settings.foveatedCalibrationReference.version == 0,
			"Legacy coverage above the new maximum must expire its reference without rejecting the profile");
		Check(settings.foveatedCenterArea == expected.foveatedCenterArea && settings.foveatedMaskSetup == expected.foveatedMaskSetup,
			"Migrating the old coverage range must preserve the fitted geometry and user confirmation");
		from_json(json::object(), settings);
		Check(!settings.foveatedPeripheryMaskVisualization, "Defaults must not enable visualization");
	}
	std::cout << "Upscaling settings serialization passed\n";
}
