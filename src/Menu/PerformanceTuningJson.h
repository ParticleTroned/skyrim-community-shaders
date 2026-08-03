#pragma once

#include <nlohmann/json_fwd.hpp>

namespace PerformanceTuning
{
	bool AreJsonValuesEquivalent(
		const nlohmann::json& lhs,
		const nlohmann::json& rhs);
}
