#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NeuralRendering
{
	struct ComputeSubrect
	{
		std::uint32_t baseX = 0;
		std::uint32_t baseY = 0;
		std::uint32_t width = 0;
		std::uint32_t height = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return width != 0 && height != 0;
		}

		[[nodiscard]] constexpr bool Fits(
			std::uint32_t a_width,
			std::uint32_t a_height) const noexcept
		{
			return IsValid() && baseX <= a_width && baseY <= a_height &&
			       width <= a_width - baseX && height <= a_height - baseY;
		}

		[[nodiscard]] constexpr std::uint64_t Area() const noexcept
		{
			return IsValid() ?
			           static_cast<std::uint64_t>(width) * height :
			           0;
		}

		bool operator==(const ComputeSubrect&) const = default;
	};

	[[nodiscard]] inline ComputeSubrect BuildCenteredComputeSubrect(
		std::uint32_t a_width,
		std::uint32_t a_height,
		float a_scale) noexcept
	{
		if (!a_width || !a_height)
			return {};
		const float scale = std::clamp(
			std::isfinite(a_scale) ? a_scale : 1.0f, 0.25f, 1.0f);
		const auto scaled = [scale](std::uint32_t a_extent) {
			const auto scaledExtent = static_cast<std::uint32_t>(std::floor(
				static_cast<double>(a_extent) * static_cast<double>(scale)));
			return std::max(
				1u,
				std::min(a_extent, scaledExtent));
		};
		const std::uint32_t width = scaled(a_width);
		const std::uint32_t height = scaled(a_height);
		return {
			.baseX = (a_width - width) / 2u,
			.baseY = (a_height - height) / 2u,
			.width = width,
			.height = height,
		};
	}

	/** Maps one reference-space rectangle outwards into another resource extent. */
	[[nodiscard]] inline constexpr ComputeSubrect MapComputeSubrect(
		const ComputeSubrect& a_referenceSubrect,
		std::uint32_t a_referenceWidth,
		std::uint32_t a_referenceHeight,
		std::uint32_t a_targetWidth,
		std::uint32_t a_targetHeight) noexcept
	{
		if (!a_referenceSubrect.Fits(a_referenceWidth, a_referenceHeight) ||
			!a_targetWidth || !a_targetHeight) {
			return {};
		}
		const auto mapFloor = [](std::uint32_t a_value,
								  std::uint32_t a_sourceExtent,
								  std::uint32_t a_targetExtent) {
			return static_cast<std::uint32_t>(
				static_cast<std::uint64_t>(a_value) * a_targetExtent /
				a_sourceExtent);
		};
		const auto mapCeil = [](std::uint32_t a_value,
								 std::uint32_t a_sourceExtent,
								 std::uint32_t a_targetExtent) {
			const auto numerator =
				static_cast<std::uint64_t>(a_value) * a_targetExtent;
			return static_cast<std::uint32_t>(
				(numerator + a_sourceExtent - 1u) / a_sourceExtent);
		};
		const auto referenceRight =
			a_referenceSubrect.baseX + a_referenceSubrect.width;
		const auto referenceBottom =
			a_referenceSubrect.baseY + a_referenceSubrect.height;
		const auto left = mapFloor(
			a_referenceSubrect.baseX, a_referenceWidth, a_targetWidth);
		const auto top = mapFloor(
			a_referenceSubrect.baseY, a_referenceHeight, a_targetHeight);
		const auto right = std::min(
			a_targetWidth,
			mapCeil(referenceRight, a_referenceWidth, a_targetWidth));
		const auto bottom = std::min(
			a_targetHeight,
			mapCeil(referenceBottom, a_referenceHeight, a_targetHeight));
		return {
			.baseX = left,
			.baseY = top,
			.width = right > left ? right - left : 0u,
			.height = bottom > top ? bottom - top : 0u,
		};
	}
}
