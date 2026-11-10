#pragma once

#include "EngineFix.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <nlohmann/json_fwd.hpp>
#endif

/** @brief Accelerates duplicate-light checks after validating the native function and AVX2 support. */
struct LightListSearchFix : EngineFix
{
	std::string GetName() override { return "Light List Search Fix"; }
	bool TryInstall() override;
};

#ifdef DEVBENCH_BRIDGE_ENABLED
namespace LightListSearch
{
	/** @brief Inspect admission or toggle the installed search for session-only comparisons. */
	nlohmann::json Control(const nlohmann::json& a_args);
}
#endif
