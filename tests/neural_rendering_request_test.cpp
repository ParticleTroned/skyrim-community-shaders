#include "Features/Upscaling/NeuralRendering/CharacterSettings.h"
#include "Features/Upscaling/NeuralRendering/ConfigurationSerialization.h"
#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"
#include "Features/Upscaling/NeuralRendering/Runtime.h"
#include <tuple>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace NeuralRendering
{
	Runtime& Runtime::Instance()
	{
		static Runtime instance;
		return instance;
	}
	Runtime::~Runtime() = default;
	TuningLimits Runtime::GetTuningLimits() const { return {}; }
}

using json = nlohmann::json;

// Compile the production parser without a game, renderer, or live DevBench host.
#include "neural_rendering_request_under_test.h"

int main()
{
	const auto require = [](bool value) {
		if (!value)
			throw std::runtime_error("Neural Rendering request validation invariant");
	};
	static_assert(NeuralRendering::GetTuningLimitsForVersion(310, 8).intensity == 1.0f);
	static_assert(NeuralRendering::GetTuningLimitsForVersion(310, 8).localStructureStrength == 2.0f);
	static_assert(NeuralRendering::GetTuningLimitsForVersion(310, 9).localStructureStrength == 1.0f);
	static_assert(NeuralRendering::GetTuningLimitsForVersion(310, 9).skinStructureStrength == 1.0f);
	static_assert(NeuralRendering::GetTuningLimitsForVersion(310, 9).localToneStrength == 2.0f);
	static_assert(NeuralRendering::GetTuningLimitsForVersion(310, 90).localStructureStrength == 2.0f);
	static_assert(NeuralRendering::GetTuningLimitsForVersion(311, 9).localStructureStrength == 2.0f);
	static_assert(NeuralRendering::GetTuningLimitsForVersion(0, 0).intensity == 2.0f);
	for (unsigned minor : { 8u, 9u, 10u }) {
		const auto limits = NeuralRendering::GetTuningLimitsForVersion(310, minor);
		for (const auto& [field, maximum] : {
				 std::pair{ "intensity", limits.intensity }, std::pair{ "localToneStrength", limits.localToneStrength },
				 std::pair{ "localStructureStrength", limits.localStructureStrength }, std::pair{ "skinStructureStrength", limits.skinStructureStrength } }) {
			for (double value : { -0.01, 0.0, 0.5, 1.0, 1.01, 1.5, 2.0, 2.01 }) {
				NeuralRenderingConfigurationRequest request;
				json error;
				const bool accepted = TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" }, { field, value } }, request, error, limits);
				require(accepted == (value >= 0 && value <= maximum));
				if (!accepted)
					require(error.at("errorCode") == "nr_tuning_out_of_range" && error.at("maximum") == maximum && error.at("field") == field);
			}
		}
	}
	const auto descriptor = json::parse(kNeuralRenderingDescriptor);
	const auto& limitsSchema = descriptor.at("outputSchema").at("properties").at("neuralRendering").at("properties").at("tuningLimits").at("properties");
	require(limitsSchema.at("localToneStrength").at("const") == 2);
	for (const auto* field : { "intensity", "localStructureStrength", "skinStructureStrength" }) {
		require(limitsSchema.at(field).at("enum") == json::array({ 1, 2 }));
		require(descriptor.at("inputSchema").at("properties").at(field).at("description").get<std::string>().find("310.9") != std::string::npos);
	}
	const auto& performanceSchema = descriptor.at("outputSchema").at("properties").at("neuralRendering").at("properties").at("performance").at("properties");
	require(performanceSchema.contains("lastFeatureParallel") && performanceSchema.contains("lastEvaluationGpuMicroseconds") &&
			performanceSchema.contains("parallelCommandSubmissions"));
	require(descriptor.at("description").get<std::string>().find("parallelEyes") != std::string::npos);
	const auto& packedProperty = descriptor.at("inputSchema").at("properties").at("characterPackedCrops");
	require(packedProperty.at("type") == "boolean" && packedProperty.at("default") == NeuralRendering::CharacterSettings{}.packedCrops);
	require(descriptor.at("description").get<std::string>().find("characterPackedCrops (Multi-Crop) defaults false") != std::string::npos);
	for (const auto& value : { json(true), json(false), json(1), json("true"), json(nullptr) }) {
		NeuralRenderingConfigurationRequest request;
		json error;
		const bool accepted = TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" }, { "characterPackedCrops", value } }, request, error);
		require(accepted == value.is_boolean());
		if (accepted)
			require(request.HasAnyControl() && request.HasCharacterControls() && request.characterPackedCrops == value.get<bool>());
	}
	const auto& parallelProperty = descriptor.at("inputSchema").at("properties").at("parallelEyes");
	require(parallelProperty.at("type") == "boolean" && parallelProperty.at("default") == true);
	for (const auto& value : { json(true), json(false), json(1), json("true"), json(nullptr) }) {
		NeuralRenderingConfigurationRequest request;
		json error;
		const bool accepted = TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" }, { "parallelEyes", value } }, request, error);
		require(accepted == value.is_boolean());
		if (accepted)
			require(request.HasAnyControl() && request.parallelEyes == value.get<bool>());
	}
	for (const auto& [field, minimum, maximum, member] : {
			 std::tuple{ "centralAreaPercent", 25u, 100u, &NeuralRenderingConfigurationRequest::centralAreaPercent },
			 std::tuple{ "centralFeatherPixels", 0u, 256u, &NeuralRenderingConfigurationRequest::centralFeatherPixels } }) {
		for (const auto value : { minimum, (minimum + maximum) / 2, maximum }) {
			NeuralRenderingConfigurationRequest request;
			json error;
			require(TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" }, { field, value } }, request, error));
			require(request.HasAnyControl() && request.*member == value);
		}
		for (const auto& value : { json(-1), json(maximum + 1), json(50.0), json(true), json("50"), json(nullptr),
				 json::array(), json::object(), json(std::numeric_limits<std::uint64_t>::max()) }) {
			NeuralRenderingConfigurationRequest request;
			json error;
			require(!TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" }, { field, value } }, request, error));
			require(!(request.*member).has_value() && error.at("field") == field);
		}
	}
	for (const auto& value : { json(30), json(33), json(75u), json(100) }) {
		NeuralRenderingConfigurationRequest request;
		json error;
		require(TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" }, { "modelResolutionPercent", value } }, request, error));
		require(request.HasAnyControl() && request.modelResolutionPercent == value.get<std::uint32_t>());
	}
	for (const auto& value : { json(29), json(101), json(-1), json(75.0), json(75.5), json(true), json("75"),
			 json(nullptr), json::array(), json::object(), json(std::numeric_limits<std::uint64_t>::max()),
			 json(std::numeric_limits<std::int64_t>::min()), json(std::numeric_limits<std::int64_t>::max()) }) {
		NeuralRenderingConfigurationRequest request;
		json error;
		require(!TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" }, { "modelResolutionPercent", value } }, request, error));
		require(!request.modelResolutionPercent.has_value() && !error.empty());
	}
	for (std::uint32_t index = 0; index < 3; ++index) {
		const auto mode = static_cast<NeuralRendering::RenderingMode>(index);
		for (const auto& value : { json(index), json(NeuralRendering::GetRenderingModeName(mode)) }) {
			NeuralRenderingConfigurationRequest request;
			json error;
			require(TryParseNeuralRenderingConfiguration(
				{ { "action", "nr_configure" }, { "mode", value }, { "fovOnly", false },
					{ "characterEnabled", true }, { "characterVisualIsolationEnabled", true } },
				request, error));
			require(request.mode == NeuralRendering::ConfiguredRenderingMode(index) && request.fovOnly == false && request.HasAnyControl());
			require(request.characterEnabled == true && request.characterVisualIsolationEnabled == true);
		}
	}
	for (bool enabled : { false, true }) {
		NeuralRenderingConfigurationRequest request;
		json error;
		require(TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" }, { "prewarmEnabled", enabled } }, request, error));
		require(request.HasAnyControl() && request.prewarmEnabled == enabled && !request.HasCharacterControls());
	}
	for (const auto& value : { json(1), json("true"), json(nullptr) }) {
		NeuralRenderingConfigurationRequest request;
		json error;
		require(!TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" }, { "prewarmEnabled", value } }, request, error));
	}
	for (bool enabled : { false, true }) {
		NeuralRenderingConfigurationRequest request;
		json error;
		require(TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" }, { "characterSceneStrengthsEnabled", enabled } }, request, error));
		require(request.HasAnyControl() && request.HasCharacterControls() && request.characterSceneStrengthsEnabled == enabled);
		require(TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" }, { "characterProviderBlending", enabled } }, request, error));
		require(request.HasAnyControl() && request.characterProviderBlending == enabled);
	}
	for (const auto& value : { json(1), json("true"), json(nullptr) }) {
		NeuralRenderingConfigurationRequest request;
		json error;
		require(!TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" }, { "characterSceneStrengthsEnabled", value } }, request, error));
		require(!TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" }, { "characterProviderBlending", value } }, request, error));
	}
	for (const auto& [field, member] : {
			 std::pair{ "characterArmorStrength", &NeuralRenderingConfigurationRequest::characterArmorStrength },
			 std::pair{ "characterWeaponsStrength", &NeuralRenderingConfigurationRequest::characterWeaponsStrength } }) {
		for (const float value : { 0.0f, 0.375f, 1.0f }) {
			NeuralRenderingConfigurationRequest request;
			json error;
			require(TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" }, { field, value } }, request, error));
			require(request.HasAnyControl() && request.HasCharacterControls() && request.*member == value);
		}
		for (const auto& value : { json(-0.01), json(1.01), json(true), json("0.5"), json(nullptr),
				 json(std::numeric_limits<double>::infinity()), json(std::numeric_limits<double>::quiet_NaN()) }) {
			NeuralRenderingConfigurationRequest request;
			json error;
			require(!TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" }, { field, value } }, request, error));
			require(!error.empty() && !(request.*member).has_value());
		}
	}
	// Explicit placement is an assertion, resolved against the final requested mode.
	for (const auto mode : { NeuralRendering::RenderingMode::FullResolution, NeuralRendering::RenderingMode::Foveated,
			 NeuralRendering::RenderingMode::ReducedResolution }) {
		for (const auto insertion : { NeuralRendering::InsertionPoint::UpscaledCenter, NeuralRendering::InsertionPoint::FinalLdrPreUi }) {
			NeuralRenderingConfigurationRequest request;
			json error;
			require(TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" },
															 { "insertionPoint", NeuralRendering::GetInsertionPointName(insertion) } },
				request, error));
			const auto required = NeuralRendering::ResolveInsertionPoint(mode);
			require(TryValidateNeuralRenderingPlacement(request, mode, error) == (insertion == required));
			if (insertion != required) {
				require(error.at("errorCode") == "nr_insertion_point_conflict" && error.at("settingsChanged") == false);
				require(error.at("requiredValue") == NeuralRendering::GetInsertionPointName(required));
			}
		}
		NeuralRenderingConfigurationRequest request;
		json error;
		require(TryValidateNeuralRenderingPlacement(request, mode, error) && error.is_null());
	}
	for (const auto& invalid : { json(-1), json(3), json(std::numeric_limits<std::uint64_t>::max()),
			 json(1.5), json(true), json(nullptr), json::array(), json::object(), json("unknown") }) {
		NeuralRenderingConfigurationRequest request;
		json error;
		require(!TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" }, { "mode", invalid } }, request, error));
		require(error.at("errorCode") == "nr_mode_invalid" && !request.mode);
	}
	for (const auto& invalid : { json(0), json("true"), json(nullptr), json::array() }) {
		NeuralRenderingConfigurationRequest request;
		json error;
		require(!TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" }, { "fovOnly", invalid } }, request, error));
		require(error.at("errorCode") == "nr_fov_only_invalid");
	}
	for (const auto& invalid : { json{ { "action", "nr_configure" } },
			 json{ { "action", "nr_configure" }, { "unexpected", true } },
			 json{ { "action", "nr_configure" }, { "singleSubrectScale", 0.0 } },
			 json{ { "action", "nr_configure" }, { "singleSubrectScale", 1.01 } },
			 json{ { "action", "nr_configure" }, { "intensity", std::numeric_limits<double>::infinity() } } }) {
		NeuralRenderingConfigurationRequest request;
		json error;
		require(!TryParseNeuralRenderingConfiguration(invalid, request, error) && !error.empty());
	}
	NeuralRenderingConfigurationRequest request;
	json error;
	for (const auto field : { "unknownControl" }) {
		for (const auto& value : { json(false), json(true), json(1), json(8), json(nullptr), json("automatic_single"), json("independent"), json("batched") }) {
			request = {};
			require(!TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" },
															  { "enabled", true }, { field, value } },
				request, error));
			require(error.at("errorCode") == "nr_request_field_unknown" && error.at("field") == field && !request.HasAnyControl());
		}
	}
	request = {};
	require(TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" }, { "fovOnly", false } }, request, error));
	require(request.HasAnyControl() && request.fovOnly.has_value());
	for (const bool enabled : { false, true }) {
		request = {};
		require(TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" }, { "renderscaleFov", enabled } }, request, error));
		require(request.HasAnyControl() && request.renderscaleFov == enabled && !request.fovOnly.has_value());
	}
	for (const auto& invalid : { json(0), json("true"), json(nullptr), json::array() }) {
		request = {};
		require(!TryParseNeuralRenderingConfiguration({ { "action", "nr_configure" }, { "renderscaleFov", invalid } }, request, error));
		require(error.at("errorCode") == "nr_renderscale_fov_invalid");
	}
	for (bool sharedMask : { false, true }) {
		for (const auto full : { std::optional<bool>{}, std::optional(false), std::optional(true) }) {
			for (const auto scaled : { std::optional<bool>{}, std::optional(false), std::optional(true) }) {
				request = {};
				request.fovOnly = full;
				request.renderscaleFov = scaled;
				error = nullptr;
				const bool allowed = (!full || *full == sharedMask) && (!scaled || *scaled == sharedMask);
				require(TryValidateNeuralRenderingCoverage(request, sharedMask, error) == allowed);
				if (!allowed)
					require(error.at("errorCode") == "nr_coverage_inherited" && error.at("mutationApplied") == false && error.at("settingsChanged") == false);
			}
		}
	}
}
