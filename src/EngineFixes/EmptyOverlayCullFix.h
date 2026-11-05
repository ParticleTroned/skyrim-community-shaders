#pragma once

#include "EngineFix.h"

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <nlohmann/json_fwd.hpp>
#endif

/** @brief Omits inert skinned overlays after verifying their default texture's alpha. */
struct EmptyOverlayCullFix : EngineFix
{
	std::string GetName() override { return "Empty Overlay Cull Fix"; }
	bool TryInstall() override;
};

#ifdef DEVBENCH_BRIDGE_ENABLED
namespace EmptyOverlayCull
{
	/** @brief Inspects verification and toggles the installed cull for session-only comparisons. */
	nlohmann::json Control(const nlohmann::json& a_args);
}
#endif
