#include "Features/Upscaling/NeuralRendering/CharacterSettings.h"
#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"

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

using json = nlohmann::json;

// Compile the production parser without a game, renderer, or live DevBench host.
#include "neural_rendering_request_under_test.h"

int main()
{
	const auto require = [](bool value) {
		if (!value)
			throw std::runtime_error("Neural Rendering request validation invariant");
	};
	for (std::uint32_t index = 0; index < 3; ++index) {
		const auto mode = static_cast<NeuralRendering::RenderingMode>(index);
		for (const auto& value : { json(index), json(NeuralRendering::GetRenderingModeName(mode)) }) {
			NeuralRenderingConfigurationRequest request;
			json error;
			require(TryParseNeuralRenderingConfiguration(
				{ { "action", "nr_configure" }, { "mode", value }, { "fovOnly", false },
					{ "characterEnabled", true }, { "characterVisualIsolationEnabled", true } },
				request, error));
			require(request.mode == mode && request.fovOnly == false && request.HasAnyControl());
			require(request.characterEnabled == true && request.characterVisualIsolationEnabled == true);
		}
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
}
