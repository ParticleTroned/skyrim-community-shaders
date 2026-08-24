#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <string>
#include <string_view>

namespace WaterAppearanceFallbackPolicy
{
	enum class EffectiveSource
	{
		UnifiedWater,
		AdaptiveBalance
	};

	constexpr EffectiveSource SelectEffectiveSource(bool a_adaptiveBalanceRuntimeEnabled)
	{
		return a_adaptiveBalanceRuntimeEnabled ? EffectiveSource::AdaptiveBalance :
		                                         EffectiveSource::UnifiedWater;
	}

	template <std::size_t N>
	bool MirrorAppearanceValues(
		const nlohmann::json& a_unifiedWater,
		nlohmann::json& a_adaptiveBalanceDestination,
		const std::array<std::string_view, N>& a_keys)
	{
		if (!a_unifiedWater.is_object() || !a_adaptiveBalanceDestination.is_object())
			return false;

		bool changed = false;
		for (const auto key : a_keys) {
			const auto sourceIt = a_unifiedWater.find(key.data());
			if (sourceIt == a_unifiedWater.end() || !sourceIt->is_number())
				continue;

			const auto destinationIt = a_adaptiveBalanceDestination.find(key.data());
			if (destinationIt != a_adaptiveBalanceDestination.end() && destinationIt->is_number())
				continue;

			a_adaptiveBalanceDestination[std::string(key)] = *sourceIt;
			changed = true;
		}
		return changed;
	}
}
