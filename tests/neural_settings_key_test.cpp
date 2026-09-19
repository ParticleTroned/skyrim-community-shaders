#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace globals::game
{
	bool isVR = true;
}

using uint = unsigned int;
using std::int64_t;
using std::uint64_t;

// Cases use already-valid values; range validation is covered separately.
float ClampFoveatedCenterScale(float value) { return value; }
float ClampFoveatedCenterHorizontalScale(float value) { return value; }
float ClampFoveatedMaskOffsetAdjustment(float value) { return value; }
float ClampPeripheryTAACenterBlendFeather(float value) { return value; }
float ClampFoveatedBlendFeather(float value) { return value; }
float ClampPeripheryTAAOuterScaleForCenter(float value, float) { return value; }

#include "neural_settings_key_under_test.h"

int main()
{
	const auto require = [](bool value) {
		if (!value)
			throw std::runtime_error("NR effective settings-key invariant");
	};
	Upscaling::Settings foveated{};
	foveated.neuralRenderingEnabled = true;
	foveated.neuralRenderingMode = static_cast<uint>(NeuralRendering::RenderingMode::Foveated);
	const auto foveatedKey = BuildNeuralRenderingSettingsKey(foveated);
	for (const uint legacy : { 0u, 1u, 99u }) {
		foveated.neuralRenderingInsertionPoint = legacy;
		require(BuildNeuralRenderingSettingsKey(foveated) == foveatedKey);
	}
	foveated.neuralRenderingBlendFeather = 0.1f;
	require(BuildNeuralRenderingSettingsKey(foveated) != foveatedKey);
	for (const auto mode : { NeuralRendering::RenderingMode::FullResolution, NeuralRendering::RenderingMode::ReducedResolution }) {
		Upscaling::Settings baseline{};
		baseline.neuralRenderingEnabled = true;
		baseline.neuralRenderingMode = static_cast<uint>(mode);
		baseline.neuralRenderingFovOnly = true;
		baseline.neuralRenderingRenderscaleFov = true;
		baseline.foveatedCenterArea = 0.3f;
		baseline.foveatedCenterHorizontalScale = 1.0f;
		const auto key = BuildNeuralRenderingSettingsKey(baseline);
		for (auto field : { &Upscaling::Settings::foveatedCenterArea, &Upscaling::Settings::foveatedLeftEyeMaskOffsetX,
				 &Upscaling::Settings::foveatedRightEyeMaskOffsetY, &Upscaling::Settings::foveatedCenterHorizontalScale }) {
			auto changed = baseline;
			changed.*field += 0.1f;
			require(BuildNeuralRenderingSettingsKey(changed) != key);
		}
		auto changed = baseline;
		changed.neuralRenderingInsertionPoint = 1;
		require(BuildNeuralRenderingSettingsKey(changed) == key);
		baseline.foveatedPeripheryMaskVisualization = true;
		changed = baseline;
		changed.foveatedCenterArea = 0.5f;
		require(BuildNeuralRenderingSettingsKey(changed) != BuildNeuralRenderingSettingsKey(baseline));
		baseline.neuralRenderingFovOnly = false;
		baseline.neuralRenderingRenderscaleFov = false;
		changed = baseline;
		changed.foveatedCenterArea = 0.5f;
		require(BuildNeuralRenderingSettingsKey(changed) == BuildNeuralRenderingSettingsKey(baseline));
		changed = baseline;
		changed.neuralRenderingRenderscaleFov = true;
		require(BuildNeuralRenderingSettingsKey(changed) != BuildNeuralRenderingSettingsKey(baseline));
		changed = baseline;
		changed.foveatedCenterArea = 0.5f;
		globals::game::isVR = false;
		require(BuildNeuralRenderingSettingsKey(changed) == BuildNeuralRenderingSettingsKey(baseline));
		globals::game::isVR = true;
	}
}
