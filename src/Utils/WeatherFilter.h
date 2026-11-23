#pragma once

#include <RE/T/TESWeather.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

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

	/** Reuses matching records while observing in-place category and catalog changes. */
	class CachedList
	{
	public:
		/** Returns whether the matching sequence changed; allocation failure preserves the prior result. */
		bool Update(std::span<RE::TESWeather* const> catalog, uint32_t filter)
		{
			auto filtered = catalog | std::views::filter([filter](auto* weather) {
				return weather && Matches(weather->data.flags.underlying(), filter);
			});
			if (std::ranges::equal(filtered, matches))
				return false;
			std::vector<RE::TESWeather*> updated;
			updated.reserve(catalog.size());
			for (auto* weather : filtered)
				updated.push_back(weather);
			matches.swap(updated);
			return true;
		}

		/** Returns catalog-ordered matches; the view is invalidated only when the sequence changes. */
		std::span<RE::TESWeather* const> Get() const noexcept { return matches; }

	private:
		std::vector<RE::TESWeather*> matches;
	};
}
