#pragma once

#include "ShaderCompilationSchedulingPolicy.h"
#include <nlohmann/json.hpp>

namespace Util
{
	/** Both legacy checkbox values and malformed modes select conservative restart preparation. */
	inline ShaderCompilationSchedulingPolicy::PreparationMode ReadShaderPreparationMode(const nlohmann::json& general)
	{
		const auto mode = general.find("Optimised Shader Preparation");
		if (mode == general.end() || !mode->is_number_integer() || *mode < 0 || *mode > 2)
			return ShaderCompilationSchedulingPolicy::PreparationMode::StartupOnly;
		return static_cast<ShaderCompilationSchedulingPolicy::PreparationMode>(mode->get<int>());
	}
}
