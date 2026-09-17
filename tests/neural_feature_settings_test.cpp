#include "Features/Upscaling/NeuralRendering/ColorPolicy.h"

#include <algorithm>
#include <array>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace logger
{
	inline unsigned warnings = 0;
	template <class... Args>
	void warn(std::string_view, Args&&...)
	{
		++warnings;
	}
}
namespace NeuralRendering::Color
{
	struct Registry
	{
		Configuration state{};
		unsigned changes = 0;
		static Registry& Instance()
		{
			static Registry value;
			return value;
		}
		Configuration Snapshot() const { return state; }
		bool Configure(const Settings& settings, const Experiments& experiments)
		{
			state.settings = settings;
			state.experiments = experiments;
			++changes;
			return true;
		}
	};
}
namespace globals::features
{
	struct Upscaling
	{
		nlohmann::json rendering = { { "neuralRenderingMode", 1 } };
		unsigned resets = 0, applies = 0;
		bool accept = true;
		nlohmann::json GetNeuralRenderingConfiguration() const { return rendering; }
		bool ApplyNeuralRenderingConfiguration(const nlohmann::json& value, std::string& error)
		{
			++applies;
			if (!accept) {
				error = "retirement failed";
				return false;
			}
			rendering.update(value);
			return true;
		}
		bool ResetNeuralRenderingConfiguration()
		{
			++resets;
			return accept;
		}
	} upscaling;
}
struct NeuralRenderingFeature
{
	void LoadSettings(nlohmann::json&);
	void SaveSettings(nlohmann::json&);
	void RestoreDefaultSettings();
};

#include "neural_feature_settings_under_test.h"

int main()
{
	using Json = nlohmann::json;
	const auto require = [](bool value) {
		if (!value)
			throw std::runtime_error("Neural Rendering feature settings invariant");
	};
	NeuralRenderingFeature feature;
	auto& backend = globals::features::upscaling;
	auto& colour = NeuralRendering::Color::Registry::Instance();
	backend.rendering["neuralCharacterMultiRoiEnabled"] = true;
	backend.rendering["neuralCharacterMultiRoiSavingsGateEnabled"] = false;
	backend.rendering["neuralCharacterDebugView"] = 2;
	backend.rendering["neuralCharacterMaskTestMode"] = 1;
	colour.state.experiments.captureFrameEvidence = true;
	Json saved;
	feature.SaveSettings(saved);
	require(saved.at("rendering").size() == 1 && saved.at("rendering").at("neuralRenderingMode") == 1);
	require(!saved.contains("experiments") && !saved.at("colour").contains("experiments"));
	backend.rendering = Json::object();
	saved["rendering"]["neuralCharacterMultiRoiEnabled"] = true;
	feature.LoadSettings(saved);
	require(backend.applies == 1 && !backend.rendering.contains("neuralCharacterMultiRoiEnabled"));
	require(!colour.state.experiments.captureFrameEvidence);
	const auto priorColour = colour.Snapshot();
	const auto priorChanges = colour.changes;
	backend.accept = false;
	saved["colour"]["detailStrength"] = 1.5;
	feature.LoadSettings(saved);
	require(backend.resets == 0 && colour.changes == priorChanges && colour.Snapshot().settings == priorColour.settings);
	feature.RestoreDefaultSettings();
	require(backend.resets == 1 && colour.changes == priorChanges);
	const auto priorApplies = backend.applies;
	saved["colour"]["detailStrength"] = 999;
	feature.LoadSettings(saved);
	require(backend.applies == priorApplies && backend.resets == 1 && colour.changes == priorChanges);
	require(logger::warnings == 3);
}
