#include "Features/Upscaling/NeuralRendering/CharacterSettings.h"
#include "Features/Upscaling/NeuralRendering/ColorPolicy.h"
#include "Features/Upscaling/NeuralRendering/ConfigurationSerialization.h"

#include <stdexcept>

int main()
{
	using namespace NeuralRendering;
	const auto require = [](bool value) {
		if (!value)
			throw std::runtime_error("NR production diagnostic boundary failed");
	};
	CharacterSettings actors;
	actors.debugView = CharacterDebugView::CharacterMask;
	actors.maskTestMode = CharacterMaskTestMode::ForceZero;
	actors.visibilityDepthTest = false;
	actors.cropMode = static_cast<std::uint32_t>(CharacterCropMode::Automatic);
	SanitizeCharacterSettings(actors);
	if constexpr (!kDevelopmentDiagnostics) {
		require(actors.debugView == CharacterDebugView::Off);
		require(actors.maskTestMode == CharacterMaskTestMode::Authored);
		require(actors.visibilityDepthTest);
		require(actors.cropMode == CharacterPolicy::kDefaultCropMode);
		require(DiagnosticNow() == std::chrono::steady_clock::time_point{});
	} else {
		require(!actors.visibilityDepthTest);
		require(actors.cropMode == static_cast<std::uint32_t>(CharacterCropMode::Automatic));
	}
	Color::Settings settings;
	require(Color::Valid(settings));
	settings.mode = Color::Mode::PreserveSource;
	require(Color::Valid(settings));
	settings.mode = Color::Mode::Managed;
	require(Color::Valid(settings) == kDevelopmentDiagnostics);
	Color::Experiments experiments;
	require(Color::Valid(experiments));
	experiments.captureFrameEvidence = true;
	require(Color::Valid(experiments) == kDevelopmentDiagnostics);
	experiments = {};
	experiments.transportBypass = true;
	require(Color::Valid(experiments) == kDevelopmentDiagnostics);
	Color::Configuration configuration;
	configuration.experiments.captureEngineExposure = true;
	require(Color::NeedsExposureCapture(configuration) == kDevelopmentDiagnostics);
	const auto saved = PersistentRenderingSettings({ { "neuralRenderingEnabled", true },
		{ "neuralRenderingMode", 2 }, { "neuralCharacterFaceStrength", 0.55 },
		{ "neuralRenderingBatchedStereo", false }, { "neuralCharacterCropMode", 0 },
		{ "neuralCharacterDebugView", 1 } });
	require(saved.at("neuralRenderingEnabled") == true && saved.at("neuralRenderingMode") == 2);
	require(saved.at("neuralCharacterFaceStrength") == 0.55);
	require(!saved.contains("neuralCharacterDebugView"));
	require(saved.contains("neuralRenderingBatchedStereo") == kDevelopmentDiagnostics);
	require(saved.contains("neuralCharacterCropMode") == kDevelopmentDiagnostics);
}
