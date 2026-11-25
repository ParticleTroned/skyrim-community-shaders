#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace PerformanceResults::Charts
{
	/** Render measured CPU/GPU values with source-specific median definitions and run links. */
	std::string Overview(const nlohmann::json& records, const std::vector<nlohmann::json>& summaries);
	/** Offline presentation assets contain no measurement data or external dependencies. */
	std::string_view Style();
	std::string_view Script();
}
