#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string_view>

namespace CSX::ScreenshotPolicy
{
	// Size of a framed output that does not request source-native sizing.
	inline constexpr std::uint32_t FramedFixedWidth = 2560;
	inline constexpr std::uint32_t FramedFixedHeight = 1440;
	inline constexpr std::uint32_t FramedAspectWidth = FramedFixedWidth / std::gcd(FramedFixedWidth, FramedFixedHeight);
	inline constexpr std::uint32_t FramedAspectHeight = FramedFixedHeight / std::gcd(FramedFixedWidth, FramedFixedHeight);
	// Bounds a composed framed view independently of the submitted eye size.
	inline constexpr std::uint32_t MaximumFramedWidth = 16384;

	inline constexpr std::string_view FramedSizingFixed = "fixed";
	inline constexpr std::string_view FramedSizingSourceNative = "source_native";

	/** Pixel region of a source image; empty when the source cannot hold one aspect step. */
	struct FramedCrop
	{
		std::size_t x = 0;
		std::size_t y = 0;
		std::size_t width = 0;
		std::size_t height = 0;

		constexpr explicit operator bool() const noexcept { return width != 0 && height != 0; }
	};

	/** Largest centred region with the target's aspect ratio, in whole aspect steps. */
	constexpr FramedCrop ResolveFramedCrop(
		std::size_t a_sourceWidth,
		std::size_t a_sourceHeight,
		std::size_t a_targetWidth,
		std::size_t a_targetHeight)
	{
		if (a_sourceWidth == 0 || a_sourceHeight == 0 || a_targetWidth == 0 || a_targetHeight == 0)
			return {};

		const std::size_t aspectDivisor = std::gcd(a_targetWidth, a_targetHeight);
		const std::size_t aspectWidth = a_targetWidth / aspectDivisor;
		const std::size_t aspectHeight = a_targetHeight / aspectDivisor;
		const std::size_t aspectScale = std::min(a_sourceWidth / aspectWidth, a_sourceHeight / aspectHeight);
		if (aspectScale == 0)
			return {};

		const std::size_t cropWidth = aspectScale * aspectWidth;
		const std::size_t cropHeight = aspectScale * aspectHeight;
		return { (a_sourceWidth - cropWidth) / 2, (a_sourceHeight - cropHeight) / 2, cropWidth, cropHeight };
	}

	/** Dimensions of a framed output; empty when no valid size exists. */
	struct FramedSize
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;

		constexpr explicit operator bool() const noexcept { return width != 0 && height != 0; }
	};

	/**
	 * Size that keeps an eye's pixel density across a frame of the given tangent
	 * width, in whole aspect steps. Never exceeds the eye's pixel count or
	 * MaximumFramedWidth; empty when an extent is not positive and finite or the
	 * eye image cannot hold one aspect step.
	 */
	constexpr FramedSize ResolveFramedSourceDensitySize(
		double a_frameTangentWidth,
		double a_eyeTangentWidth,
		double a_eyeTangentHeight,
		std::size_t a_eyeWidth,
		std::size_t a_eyeHeight)
	{
		// Ordered comparisons reject NaN and infinite extents without <cmath>.
		const auto isPositiveFinite = [](double a_value) {
			return a_value > 0.0 && a_value <= std::numeric_limits<double>::max();
		};
		if (!isPositiveFinite(a_frameTangentWidth) || !isPositiveFinite(a_eyeTangentWidth) ||
			!isPositiveFinite(a_eyeTangentHeight))
			return {};

		// Saturate each side so an implausible eye size cannot overflow the budget.
		const std::uint64_t eyePixels = std::min<std::uint64_t>(a_eyeWidth, MaximumFramedWidth) *
		                                std::min<std::uint64_t>(a_eyeHeight, MaximumFramedWidth);
		const std::uint64_t stepPixels = std::uint64_t{ FramedAspectWidth } * FramedAspectHeight;
		// Bisection keeps constant evaluation well inside compiler step limits.
		std::uint64_t maximumSteps = 0;
		std::uint64_t rejectedSteps = MaximumFramedWidth / FramedAspectWidth + 1;
		while (rejectedSteps - maximumSteps > 1) {
			const std::uint64_t candidateSteps = maximumSteps + (rejectedSteps - maximumSteps) / 2;
			if (candidateSteps * candidateSteps * stepPixels <= eyePixels)
				maximumSteps = candidateSteps;
			else
				rejectedSteps = candidateSteps;
		}
		if (maximumSteps == 0)
			return {};

		const double horizontalDensity = static_cast<double>(a_eyeWidth) / a_eyeTangentWidth;
		const double verticalDensity = static_cast<double>(a_eyeHeight) / a_eyeTangentHeight;
		const double nearestSteps = a_frameTangentWidth * std::max(horizontalDensity, verticalDensity) / FramedAspectWidth + 0.5;
		// Comparing before the cast keeps an overflowed density inside the budget.
		const std::uint64_t steps = nearestSteps < static_cast<double>(maximumSteps) ?
		                                std::max<std::uint64_t>(static_cast<std::uint64_t>(nearestSteps), 1) :
		                                maximumSteps;
		return { static_cast<std::uint32_t>(steps * FramedAspectWidth), static_cast<std::uint32_t>(steps * FramedAspectHeight) };
	}
}
