#pragma once

#include "../../package/Shaders/Common/SnowExclusions.hlsli"
#include "SeasonalWeatherPolicy.h"

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace SeasonalWeatherConfig
{
	inline constexpr float MaxAreaCoordinate = 10000000.0f;
	struct SnowExclusion
	{
		std::string Name = "New area";
		std::string Worldspace = "tamriel";
		bool Enabled = false;
		bool Snowfall = true;
		bool SnowCover = true;
		std::vector<std::array<float, 2>> Vertices;
	};
	struct SnowAssignment
	{
		std::string Region;
		std::string Weather;
		float Weight = 20.0f;
	};
	struct WeatherCategory
	{
		std::string Weather;
		SeasonalWeatherPolicy::Category Category = SeasonalWeatherPolicy::Category::Unchanged;
	};
	struct Settings
	{
		bool Enabled = false;
		bool SkipGlobalControlledRegions = true;
		SeasonalWeatherPolicy::MonthlyWeights MonthlyWeights = SeasonalWeatherPolicy::DefaultWeights;
		std::vector<std::string> ExcludedWorldspaces{ "blackreach", "alftandworld", "sovngarde", "dlc01soulcairn", "dlc01boneyard", "dlc2apocryphaworld", "dlc2solstheimworld" };
		std::vector<std::string> ExcludedRegions;
		std::vector<SnowAssignment> SnowAssignments;
		std::vector<WeatherCategory> WeatherCategories;
		std::vector<SnowExclusion> SnowExclusions;
	};
	/** Packs validated polygon coordinates for the shared CPU/shader mask. */
	SnowExclusions::Area PackArea(const SnowExclusion& area);
	/** Matches exterior world IDs without case-sensitive user configuration. */
	bool MatchesWorld(const SnowExclusion& area, std::string_view world);

	/** Validates a complete candidate before publishing any settings change. */
	Settings Parse(const nlohmann::json& settings);
	/** Returns the shared menu, disk and DevBench settings representation. */
	nlohmann::json Serialize(const Settings& settings);
	/** Canonicalizes a portable form identity, rejecting malformed or zero IDs. */
	std::string FormKey(std::string_view key);
	/** Describes the portable form identity accepted by settings and inspection. */
	const nlohmann::json& FormKeySchema();
	/** Describes the bounded settings accepted by the seasonal_weather action. */
	const nlohmann::json& Schema();
}
