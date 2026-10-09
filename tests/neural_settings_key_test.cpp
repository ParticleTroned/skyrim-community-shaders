#include "Features/Upscaling/FoveatedBlendPolicy.h"
#include "Features/Upscaling/NeuralRendering/ModelResolutionPolicy.h"

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
	for (bool enabled : { false, true }) {
		for (auto mode : { NeuralRendering::RenderingMode::FullResolution, NeuralRendering::RenderingMode::Foveated,
				 NeuralRendering::RenderingMode::ReducedResolution }) {
			Upscaling::Settings baseline{};
			baseline.neuralRenderingEnabled = enabled;
			baseline.neuralRenderingMode = static_cast<uint>(mode);
			baseline.neuralRenderingModelResolutionPercent = 100;
			baseline.neuralRenderingCentralAreaPercent = 100;
			baseline.neuralRenderingCentralFeatherPixels = 64;
			const auto key = BuildNeuralRenderingSettingsKey(baseline);
			auto central = baseline;
			central.neuralRenderingCentralAreaPercent = 50;
			require((BuildNeuralRenderingSettingsKey(central) != key) == enabled);
			auto feather = baseline;
			feather.neuralRenderingCentralFeatherPixels = 256;
			require(BuildNeuralRenderingSettingsKey(feather) == key);
			feather.neuralRenderingCentralAreaPercent = 50;
			require((BuildNeuralRenderingSettingsKey(feather) != BuildNeuralRenderingSettingsKey(central)) == enabled);
			for (uint percent : { 30u, 33u, 67u, 99u }) {
				auto changed = baseline;
				changed.neuralRenderingModelResolutionPercent = percent;
				require((BuildNeuralRenderingSettingsKey(changed) != key) ==
						enabled);
			}
		}
	}
	{
		Upscaling::Settings central{};
		central.neuralRenderingEnabled = true;
		central.neuralRenderingCentralAreaPercent = 50;
		central.neuralRenderingCentralFeatherPixels = 64;
		central.neuralRenderingModelResolutionPercent = 100;
		central.foveatedVendorDispatch = false;
		central.neuralRenderingFovOnly = false;
		central.neuralRenderingRenderscaleFov = false;
		globals::game::isVR = true;
		const auto key = BuildNeuralRenderingSettingsKey(central);
		for (auto member : { &Upscaling::Settings::foveatedCenterArea, &Upscaling::Settings::foveatedCenterHorizontalScale,
				 &Upscaling::Settings::foveatedLeftEyeMaskOffsetX, &Upscaling::Settings::foveatedRightEyeMaskOffsetY }) {
			auto moved = central;
			moved.*member += 0.05f;
			require(BuildNeuralRenderingSettingsKey(moved) != key);
		}
	}
	Upscaling::Settings foveated{};
	foveated.neuralRenderingCentralAreaPercent = 100;
	foveated.neuralRenderingEnabled = true;
	foveated.neuralRenderingMode = static_cast<uint>(NeuralRendering::RenderingMode::Foveated);
	const auto foveatedKey = BuildNeuralRenderingSettingsKey(foveated);
	for (bool vr : { false, true }) {
		globals::game::isVR = vr;
		for (uint mode : { 0u, 1u, 2u }) {
			auto actor = foveated;
			actor.neuralRenderingMode = mode;
			actor.neuralCharacterRenderingEnabled = true;
			const auto compositorKey = BuildNeuralRenderingSettingsKey(actor);
			auto savedScenePreference = actor;
			savedScenePreference.neuralCharacterSceneStrengthsEnabled = true;
			require(BuildNeuralRenderingSettingsKey(savedScenePreference) == compositorKey);
			for (const auto field : { &Upscaling::Settings::neuralCharacterArmorStrength, &Upscaling::Settings::neuralCharacterWeaponsStrength }) {
				auto changed = actor;
				changed.*field = 0.375f;
				require(BuildNeuralRenderingSettingsKey(changed) != compositorKey);
				changed.neuralCharacterRenderingEnabled = false;
				auto inactive = changed;
				inactive.*field = 1.0f;
				require(BuildNeuralRenderingSettingsKey(changed) == BuildNeuralRenderingSettingsKey(inactive));
			}
			actor.neuralCharacterProviderBlending = true;
			require(BuildNeuralRenderingSettingsKey(actor) != compositorKey);
			actor.neuralCharacterRenderingEnabled = false;
			const auto ordinaryKey = BuildNeuralRenderingSettingsKey(actor);
			actor.neuralCharacterSceneStrengthsEnabled = true;
			const auto sceneKey = BuildNeuralRenderingSettingsKey(actor);
			require(sceneKey != ordinaryKey);
			for (const auto field : { &Upscaling::Settings::neuralCharacterMaximumDistanceMeters,
					 &Upscaling::Settings::neuralCharacterFocusScale, &Upscaling::Settings::neuralCharacterRoiMargin,
					 &Upscaling::Settings::neuralCharacterDepthThreshold }) {
				auto changed = actor;
				changed.*field += 0.25f;
				require(BuildNeuralRenderingSettingsKey(changed) == sceneKey);
			}
			for (const auto field : { &Upscaling::Settings::neuralCharacterMinimumFacePixelSize,
					 &Upscaling::Settings::neuralCharacterCropMode, &Upscaling::Settings::neuralCharacterRoiHoldFrames,
					 &Upscaling::Settings::neuralCharacterFeatherRadius }) {
				auto changed = actor;
				++(changed.*field);
				require(BuildNeuralRenderingSettingsKey(changed) == sceneKey);
			}
			for (const auto field : { &Upscaling::Settings::neuralCharacterAdaptiveRoiSelectionEnabled,
					 &Upscaling::Settings::neuralCharacterDepthAwareFeatherEnabled,
					 &Upscaling::Settings::neuralCharacterVisibilityDepthTestEnabled
#ifdef DEVBENCH_BRIDGE_ENABLED
					 ,
					 &Upscaling::Settings::neuralCharacterCurrentContextEnabled,
					 &Upscaling::Settings::neuralCharacterGpuMaskSupportEnabled
#endif
				 }) {
				auto changed = actor;
				changed.*field = !(changed.*field);
				require(BuildNeuralRenderingSettingsKey(changed) == sceneKey);
				changed.neuralCharacterRenderingEnabled = true;
				auto restricted = actor;
				restricted.neuralCharacterRenderingEnabled = true;
				require(BuildNeuralRenderingSettingsKey(changed) != BuildNeuralRenderingSettingsKey(restricted));
			}
			for (const auto field : { &Upscaling::Settings::neuralCharacterFaceStrength, &Upscaling::Settings::neuralCharacterSkinStrength,
					 &Upscaling::Settings::neuralCharacterHairStrength, &Upscaling::Settings::neuralCharacterArmorStrength,
					 &Upscaling::Settings::neuralCharacterWeaponsStrength }) {
				auto changed = actor;
				changed.*field = 0.375f;
				require(BuildNeuralRenderingSettingsKey(changed) != sceneKey);
			}
		}
	}
	globals::game::isVR = true;
	for (const uint legacy : { 0u, 1u, 99u }) {
		foveated.neuralRenderingInsertionPoint = legacy;
		require(BuildNeuralRenderingSettingsKey(foveated) == foveatedKey);
	}
	foveated.neuralRenderingBlendFeather = 0.1f;
	require(BuildNeuralRenderingSettingsKey(foveated) != foveatedKey);
	auto curved = foveated;
	curved.foveatedBlendFalloff = 1.5f;
	require(BuildNeuralRenderingSettingsKey(curved) == BuildNeuralRenderingSettingsKey(foveated));
	curved.foveatedBlendCurveEnabled = true;
	require(BuildNeuralRenderingSettingsKey(curved) != BuildNeuralRenderingSettingsKey(foveated));
	curved.foveatedBlendFalloff = 1.0f;
	require(BuildNeuralRenderingSettingsKey(curved) == BuildNeuralRenderingSettingsKey(foveated));
	curved.foveatedBlendFalloff = 1.5f;
	globals::game::isVR = false;
	require(BuildNeuralRenderingSettingsKey(curved) == BuildNeuralRenderingSettingsKey(foveated));
	globals::game::isVR = true;
	for (const auto mode : { NeuralRendering::RenderingMode::FullResolution, NeuralRendering::RenderingMode::ReducedResolution }) {
		Upscaling::Settings baseline{};
		baseline.neuralRenderingCentralAreaPercent = 100;
		baseline.neuralRenderingCentralFeatherPixels = 64;
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
