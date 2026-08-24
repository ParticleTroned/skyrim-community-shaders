#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <string>
#include <string_view>

namespace WaterAppearanceFallbackPolicy
{
	inline constexpr std::string_view kFallbackKey = "WaterAppearanceFallback";
	inline constexpr std::array<std::string_view, 8> kAppearanceKeys{
		"WaterBrightness",
		"GlobalReflectionAmount",
		"RefractionAmount",
		"SunSpecularMultiplier",
		"WaveAmplitude",
		"FresnelMin",
		"FresnelMax",
		"Muddiness"
	};

	enum class EffectiveSource
	{
		UnifiedWaterFallback,
		AdaptiveBalance
	};

	constexpr EffectiveSource SelectEffectiveSource(bool a_adaptiveBalanceRuntimeEnabled)
	{
		return a_adaptiveBalanceRuntimeEnabled ? EffectiveSource::AdaptiveBalance :
		                                         EffectiveSource::UnifiedWaterFallback;
	}

	template <std::size_t N>
	bool HasAppearanceValues(
		const nlohmann::json& a_source,
		const std::array<std::string_view, N>& a_keys)
	{
		if (!a_source.is_object())
			return false;

		for (const auto key : a_keys) {
			const auto valueIt = a_source.find(key.data());
			if (valueIt != a_source.end() && valueIt->is_number())
				return true;
		}
		return false;
	}

	template <std::size_t N>
	bool CanonicalizeAppearanceFallback(
		nlohmann::json& a_unifiedWater,
		std::string_view a_fallbackKey,
		const std::array<std::string_view, N>& a_keys)
	{
		if (!a_unifiedWater.is_object())
			return false;

		auto fallbackIt = a_unifiedWater.find(a_fallbackKey.data());
		bool changed = false;
		if (fallbackIt != a_unifiedWater.end() && !fallbackIt->is_object()) {
			a_unifiedWater.erase(fallbackIt);
			fallbackIt = a_unifiedWater.end();
			changed = true;
		}

		const bool hasLegacyValues = HasAppearanceValues(a_unifiedWater, a_keys);
		if (fallbackIt == a_unifiedWater.end() && hasLegacyValues) {
			a_unifiedWater[std::string(a_fallbackKey)] = nlohmann::json::object();
			fallbackIt = a_unifiedWater.find(a_fallbackKey.data());
			changed = true;
		}
		if (fallbackIt != a_unifiedWater.end()) {
			for (const auto key : a_keys) {
				const auto valueIt = fallbackIt->find(key.data());
				if (valueIt != fallbackIt->end() && !valueIt->is_number()) {
					fallbackIt->erase(valueIt);
					changed = true;
				}
			}
		}

		// Canonicalize old top-level fields into a named compatibility snapshot.
		// A legacy value wins because it can come from a higher-priority user layer
		// merged over a lower-priority canonical fallback.
		for (const auto key : a_keys) {
			auto legacyIt = a_unifiedWater.find(key.data());
			if (legacyIt == a_unifiedWater.end())
				continue;

			if (legacyIt->is_number() && fallbackIt != a_unifiedWater.end())
				(*fallbackIt)[std::string(key)] = *legacyIt;
			a_unifiedWater.erase(legacyIt);
			changed = true;
		}
		return changed;
	}

	template <std::size_t N>
	bool MigrateAppearanceValues(
		nlohmann::json& a_unifiedWater,
		nlohmann::json& a_adaptiveBalanceDestination,
		std::string_view a_fallbackKey,
		std::string_view a_forceGlobalKey,
		bool a_forceGlobal,
		const std::array<std::string_view, N>& a_keys)
	{
		if (!a_unifiedWater.is_object() || !a_adaptiveBalanceDestination.is_object())
			return false;

		const auto fallbackItBefore = a_unifiedWater.find(a_fallbackKey.data());
		const bool hasFallbackValues = fallbackItBefore != a_unifiedWater.end() &&
		                               HasAppearanceValues(*fallbackItBefore, a_keys);
		if (!hasFallbackValues && !HasAppearanceValues(a_unifiedWater, a_keys))
			return false;

		bool changed = CanonicalizeAppearanceFallback(a_unifiedWater, a_fallbackKey, a_keys);
		const auto fallbackIt = a_unifiedWater.find(a_fallbackKey.data());

		const auto forceIt = a_adaptiveBalanceDestination.find(a_forceGlobalKey.data());
		if (forceIt == a_adaptiveBalanceDestination.end() ||
			!forceIt->is_boolean() ||
			forceIt->get<bool>() != a_forceGlobal) {
			a_adaptiveBalanceDestination[std::string(a_forceGlobalKey)] = a_forceGlobal;
			changed = true;
		}

		// Preserve valid Adaptive Balance values. Missing or malformed fields inherit
		// the canonical fallback snapshot.
		for (const auto key : a_keys) {
			const auto sourceIt = fallbackIt->find(key.data());
			if (sourceIt == fallbackIt->end() || !sourceIt->is_number())
				continue;

			const auto destinationIt = a_adaptiveBalanceDestination.find(key.data());
			if (destinationIt == a_adaptiveBalanceDestination.end() || !destinationIt->is_number()) {
				a_adaptiveBalanceDestination[std::string(key)] = *sourceIt;
				changed = true;
			}
		}
		return changed;
	}
}
