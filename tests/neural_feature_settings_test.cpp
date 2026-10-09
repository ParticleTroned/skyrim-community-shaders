#include "Features/Upscaling/NeuralRendering/ColorPolicy.h"
#include "Features/Upscaling/NeuralRendering/ConfigurationSerialization.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
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

struct IndependentSettings
{
	unsigned qualityMode = 0;
	bool neuralRenderingEnabled = false;
	float neuralRenderingIntensity = 0.8f;
	unsigned neuralCharacterDebugView = 0;
	unsigned neuralCharacterMaskTestMode = 0;
	bool neuralCharacterCurrentContextEnabled = false;
	bool neuralCharacterGpuMaskSupportEnabled = false;
	unsigned neuralRenderingModelResolutionPercent = 100;
	unsigned neuralRenderingCentralAreaPercent = 100, neuralRenderingCentralFeatherPixels = 64;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(IndependentSettings, qualityMode, neuralRenderingEnabled, neuralRenderingIntensity, neuralRenderingModelResolutionPercent, neuralRenderingCentralAreaPercent, neuralRenderingCentralFeatherPixels)

int main()
{
	using Json = nlohmann::json;
	const auto require = [](bool value) {
		if (!value)
			throw std::runtime_error("Neural Rendering feature settings invariant");
	};
	const auto defaults = NeuralRendering::RenderingSettings(Json(IndependentSettings{}));
	require(defaults.at("neuralRenderingCentralAreaPercent") == 100 && defaults.at("neuralRenderingCentralFeatherPixels") == 64);
	const auto savedArea = NeuralRendering::PersistentRenderingSettings({ { "neuralRenderingCentralAreaPercent", 50 }, { "neuralRenderingCentralFeatherPixels", 256 } });
	require(savedArea.at("neuralRenderingCentralAreaPercent") == 50 && savedArea.at("neuralRenderingCentralFeatherPixels") == 256);
	for (std::uint32_t percent : { 30u, 33u, 50u, 75u, 100u }) {
		for (const auto& value : { Json(percent), Json(std::int64_t(percent)) })
			require(NeuralRendering::ParseModelResolutionPercent(value) == percent);
	}
	for (const auto& value : { Json(0), Json(29), Json(101), Json(-1), Json(75.0), Json(75.5), Json(true),
			 Json("75"), Json(nullptr), Json::array(), Json::object(), Json(std::numeric_limits<std::uint64_t>::max()),
			 Json(std::numeric_limits<std::int64_t>::min()), Json(std::numeric_limits<std::int64_t>::max()),
			 Json(std::numeric_limits<double>::infinity()), Json(std::numeric_limits<double>::quiet_NaN()) })
		require(!NeuralRendering::ParseModelResolutionPercent(value).has_value());
	for (const auto legacy : { Json(0u), Json(1u), Json(2u), Json(-1), Json(0.5), Json("invalid"), Json(false) }) {
		for (const unsigned resolved : { 0u, 1u }) {
			bool accepted = false;
			try {
				NeuralRendering::ValidateRenderingSettingsNormalization(
					{ { "neuralRenderingInsertionPoint", legacy } },
					{ { "neuralRenderingInsertionPoint", resolved } });
				accepted = true;
			} catch (const std::invalid_argument&) {
			}
			require(accepted == (legacy == 0u || legacy == 1u));
		}
	}
	bool tuningRejected = false;
	try {
		NeuralRendering::ValidateRenderingSettingsNormalization(
			{ { "neuralRenderingIntensity", -1.0 } }, { { "neuralRenderingIntensity", 0.0 } });
	} catch (const std::invalid_argument&) {
		tuningRejected = true;
	}
	require(tuningRejected);
	NeuralRenderingFeature feature;
	auto& backend = globals::features::upscaling;
	auto& colour = NeuralRendering::Color::Registry::Instance();
	backend.rendering["neuralCharacterDebugView"] = 2;
	backend.rendering["neuralCharacterMaskTestMode"] = 1;
	colour.state.experiments.captureFrameEvidence = true;
	Json saved;
	feature.SaveSettings(saved);
	require(saved.at("rendering").size() == 1 && saved.at("rendering").at("neuralRenderingMode") == 1);
	require(!saved.contains("experiments") && !saved.at("colour").contains("experiments"));
	backend.rendering = Json::object();
	feature.LoadSettings(saved);
	require(backend.applies == 1 && backend.rendering.at("neuralRenderingMode") == 1);
	require(!colour.state.experiments.captureFrameEvidence);
	for (const bool masked : { true, false }) {
		backend.rendering["neuralRenderingFovOnly"] = !masked;
		backend.rendering["neuralRenderingRenderscaleFov"] = masked;
		Json roundTrip;
		feature.SaveSettings(roundTrip);
		backend.rendering = Json::object();
		feature.LoadSettings(roundTrip);
		require(backend.rendering.at("neuralRenderingRenderscaleFov") == masked &&
				backend.rendering.at("neuralRenderingFovOnly") == !masked);
	}
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

	for (const bool optimized : { false, true }) {
		backend.accept = true;
		Json legacy = { { "schemaVersion", 1 }, { "colour", Json::object() },
			{ "rendering", { { "neuralRenderingOptimizedStereoPath", optimized },
							   { "neuralRenderingDirectCommit", !optimized }, { "neuralRenderingIntensity", 1.25 },
							   { "neuralCharacterDebugView", 2 } } } };
		feature.LoadSettings(legacy);
		if constexpr (NeuralRendering::kDevelopmentDiagnostics) {
			require(backend.rendering.at("neuralRenderingBatchedStereo") == optimized);
			require(backend.rendering.at("neuralRenderingDirectCommit") == !optimized);
		} else {
			require(!backend.rendering.contains("neuralRenderingBatchedStereo") && !backend.rendering.contains("neuralRenderingDirectCommit"));
		}
		require(backend.rendering.at("neuralRenderingIntensity") == 1.25);
		require(!backend.rendering.contains("neuralRenderingOptimizedStereoPath"));
		require(!backend.rendering.contains("neuralCharacterDebugView"));
		const auto applies = backend.applies;
		legacy["rendering"]["neuralRenderingOptimizedStereoPath"] = "invalid";
		feature.LoadSettings(legacy);
		require(backend.applies == applies);
		legacy["rendering"]["neuralRenderingBatchedStereo"] = optimized;
		feature.LoadSettings(legacy);
		require(backend.applies == applies + 1);
		if constexpr (NeuralRendering::kDevelopmentDiagnostics)
			require(backend.rendering.at("neuralRenderingBatchedStereo") == optimized);
	}

	IndependentSettings live{ 2, true, 1.75f, 2, 1 };
	live.neuralRenderingModelResolutionPercent = 67;
	IndependentSettings upscalerEdit{ 4, false, 0.0f, 0, 0 };
	NeuralRendering::CopyRenderingSettings(upscalerEdit, live);
	require(upscalerEdit.qualityMode == 4 && upscalerEdit.neuralRenderingEnabled);
	require(upscalerEdit.neuralRenderingIntensity == live.neuralRenderingIntensity);
	require(upscalerEdit.neuralRenderingModelResolutionPercent == 67);
	require(upscalerEdit.neuralCharacterDebugView == 2 && upscalerEdit.neuralCharacterMaskTestMode == 1);
	require(NeuralRendering::RenderingSettings(Json{ { "qualityMode", 4 } }).empty());
	require(NeuralRendering::RenderingSettings(Json{ { "qualityMode", 4 }, { "neuralRenderingEnabled", false } }).size() == 1);

	backend.accept = true;
	for (const Json disabled : { Json(true), Json(false), Json("invalid"), Json(nullptr) }) {
		Json legacy = { { "Upscaling", { { "neuralRenderingEnabled", true } } },
			{ "Neural Rendering Colour", { { "enabled", true }, { "mode", "managed" }, { "detailStrength", 1.25 },
											 { "lightingPreservation", 0.37f } } },
			{ "Disable at Boot", { { "NeuralColor", disabled } } } };
		const auto migrated = NeuralRendering::LegacyColourSettings(legacy);
		require(migrated.at("enabled") == (disabled != Json(true)));
		require(migrated.at("mode") == "managed" && migrated.at("detailStrength") == 1.25);
		require(migrated.at("lightingPreservation").get<float>() == 0.37f);
		Json envelope = { { "schemaVersion", 1 }, { "rendering", NeuralRendering::RenderingSettings(legacy.at("Upscaling")) },
			{ "colour", migrated } };
		feature.LoadSettings(envelope);
		require(backend.rendering.at("neuralRenderingEnabled") == true);
		require(colour.Snapshot().settings.enabled == (disabled != Json(true)));
		require(colour.Snapshot().settings.mode == (NeuralRendering::kDevelopmentDiagnostics ? NeuralRendering::Color::Mode::Managed : NeuralRendering::Color::Mode::LegacyRaw));
		require(colour.Snapshot().settings.lightingPreservation == 0.37f);
		Json roundTrip;
		feature.SaveSettings(roundTrip);
		require(roundTrip.at("colour").at("lightingPreservation").get<float>() == 0.37f);
		feature.RestoreDefaultSettings();
		feature.LoadSettings(roundTrip);
		require(colour.Snapshot().settings.enabled == (disabled != Json(true)) &&
				colour.Snapshot().settings.lightingPreservation == 0.37f);
	}
}
