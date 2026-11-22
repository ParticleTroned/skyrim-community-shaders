#pragma once

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace MenuUI
{
	/** Browse weather categories and edit or copy the selected Visuals adjustment block. */
	void DrawVisualsWeather();
	/** Apply and save only Visuals, to one selected weather or all matching loaded records. */
	nlohmann::json ApplyVisualsWeather(const std::string& sourceKey, uint32_t filter, bool allMatches, const nlohmann::json* settings = nullptr);
}
