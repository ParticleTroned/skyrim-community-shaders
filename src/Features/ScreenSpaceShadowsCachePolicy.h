#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>

namespace ScreenSpaceShadowsCachePolicy
{
	inline constexpr std::size_t kFlatVariantCount = 2;
	inline constexpr std::size_t kVRVariantCount = 8;
	inline constexpr std::size_t kNoVariant =
		std::numeric_limits<std::size_t>::max();

	enum class ShaderLookupAction
	{
		ReturnCached,
		Compile,
		ReturnFailure
	};

	[[nodiscard]] constexpr ShaderLookupAction SelectShaderLookupAction(
		bool a_cached,
		bool a_compileFailed) noexcept
	{
		if (a_cached)
			return ShaderLookupAction::ReturnCached;
		return a_compileFailed ? ShaderLookupAction::ReturnFailure :
		                         ShaderLookupAction::Compile;
	}

	[[nodiscard]] constexpr std::size_t ActiveVariantCount(
		bool a_vrRuntime,
		std::size_t a_capacity) noexcept
	{
		return std::min(
			a_vrRuntime ? kVRVariantCount : kFlatVariantCount,
			a_capacity);
	}

	/** @brief Selects an exact hit, then an empty slot, then the LRU slot. */
	template <class Container>
	[[nodiscard]] constexpr std::size_t SelectVariantIndex(
		const Container& a_variants,
		std::size_t a_activeCount,
		unsigned int a_sampleCount,
		bool a_usesTerrainBlendingDepth) noexcept
	{
		const std::size_t activeCount =
			std::min(a_activeCount, a_variants.size());
		if (activeCount == 0)
			return kNoVariant;

		for (std::size_t index = 0; index < activeCount; ++index) {
			const auto& variant = a_variants[index];
			if (variant.valid && variant.sampleCount == a_sampleCount &&
				variant.usesTerrainBlendingDepth ==
					a_usesTerrainBlendingDepth) {
				return index;
			}
		}
		for (std::size_t index = 0; index < activeCount; ++index) {
			if (!a_variants[index].valid)
				return index;
		}

		std::size_t leastRecentlyUsed = 0;
		for (std::size_t index = 1; index < activeCount; ++index) {
			if (a_variants[index].lastUse <
				a_variants[leastRecentlyUsed].lastUse) {
				leastRecentlyUsed = index;
			}
		}
		return leastRecentlyUsed;
	}
}
