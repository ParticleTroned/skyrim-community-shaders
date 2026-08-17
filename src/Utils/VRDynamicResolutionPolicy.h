#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace VRDynamicResolutionPolicy
{
	struct PixelContract
	{
		std::uint32_t displayExtent = 0;
		std::uint32_t renderExtent = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return displayExtent != 0 &&
			       renderExtent != 0 &&
			       renderExtent <= displayExtent;
		}
	};

	struct StereoContract
	{
		PixelContract horizontal{};
		PixelContract vertical{};

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return horizontal.IsValid() &&
			       vertical.IsValid() &&
			       horizontal.displayExtent >= 4u &&
			       horizontal.renderExtent >= 4u &&
			       (horizontal.displayExtent & 1u) == 0u &&
			       (horizontal.renderExtent & 1u) == 0u;
		}
	};

	[[nodiscard]] inline float SnapNearIntegerPixelExtent(float a_extent) noexcept
	{
		if (!std::isfinite(a_extent) || a_extent <= 0.0f)
			return a_extent;

		const float nearestPixel = std::round(a_extent);
		if (a_extent == nearestPixel ||
			std::nextafter(a_extent, nearestPixel) == nearestPixel) {
			return nearestPixel;
		}
		return a_extent;
	}

	[[nodiscard]] inline bool TryResolvePixelExtent(
		float a_extent,
		std::uint32_t& a_result) noexcept
	{
		if (!std::isfinite(a_extent) ||
			a_extent < 1.0f ||
			static_cast<double>(a_extent) >
				static_cast<double>(std::numeric_limits<std::uint32_t>::max()) ||
			std::floor(a_extent) != a_extent) {
			return false;
		}

		a_result = static_cast<std::uint32_t>(a_extent);
		return true;
	}

	[[nodiscard]] inline bool MatchesPublishedRatio(
		float a_ratio,
		const PixelContract& a_contract) noexcept
	{
		return a_contract.IsValid() &&
		       std::isfinite(a_ratio) &&
		       a_ratio == static_cast<float>(a_contract.renderExtent) /
		                      static_cast<float>(a_contract.displayExtent);
	}

	[[nodiscard]] constexpr std::int32_t MapBoundaryTruncated(
		std::int32_t a_boundary,
		const PixelContract& a_contract) noexcept
	{
		if (!a_contract.IsValid())
			return a_boundary;

		const auto scaled =
			static_cast<std::int64_t>(a_boundary) *
			static_cast<std::int64_t>(a_contract.renderExtent);
		return static_cast<std::int32_t>(
			scaled / static_cast<std::int64_t>(a_contract.displayExtent));
	}
}
