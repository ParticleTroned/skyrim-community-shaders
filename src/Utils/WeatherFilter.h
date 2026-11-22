#pragma once

#include <RE/T/TESWeather.h>
#include <array>
#include <cstdint>
#include <string_view>

namespace Util::WeatherFilter
{
	struct Category
	{
		RE::TESWeather::WeatherDataFlag flag;
		std::string_view canonicalName;
	};

	inline constexpr std::array<Category, 6> Classified = { {
		{ RE::TESWeather::WeatherDataFlag::kPleasant, "Pleasant" },
		{ RE::TESWeather::WeatherDataFlag::kCloudy, "Cloudy" },
		{ RE::TESWeather::WeatherDataFlag::kRainy, "Rainy" },
		{ RE::TESWeather::WeatherDataFlag::kSnow, "Snow" },
		{ RE::TESWeather::WeatherDataFlag::kPermAurora, "Aurora" },
		{ RE::TESWeather::WeatherDataFlag::kAuroraFollowsSun, "Aurora Sun" },
	} };
	inline constexpr uint32_t Tracked = [] {
		uint32_t mask = 0;
		for (const auto& category : Classified)
			mask |= static_cast<uint32_t>(category.flag);
		return mask;
	}();
	inline constexpr uint32_t Unclassified = 1u << 6;
	inline constexpr uint32_t All = Tracked | Unclassified;

	/** Match any selected category; unclassified includes unknown-only flags. */
	constexpr bool Matches(uint32_t flags, uint32_t filter)
	{
		return filter == All || (flags & filter & Tracked) != 0 ||
		       ((filter & Unclassified) != 0 && (flags & Tracked) == 0);
	}
}
