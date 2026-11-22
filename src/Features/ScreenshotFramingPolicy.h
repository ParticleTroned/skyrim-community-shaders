#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace CSX::ScreenshotPolicy
{
	inline constexpr uint32_t FramedWidth = 3840;
	inline constexpr uint32_t FramedHeight = 2160;
	inline constexpr uint32_t MaximumFramedDimension = 16384;

	struct FramedSize
	{
		uint32_t width;
		uint32_t height;
	};

	/** Resolve a bounded 16:9 eye crop, retaining at least 4K in native mode. */
	constexpr std::optional<FramedSize> ResolveFramedSize(std::size_t a_width, std::size_t a_height, bool a_native)
	{
		if (a_width < 16 || a_height < 9 ||
			a_width > MaximumFramedDimension || a_height > MaximumFramedDimension)
			return std::nullopt;
		const auto scale = a_native ? std::max<std::size_t>(FramedWidth / 16, std::min(a_width / 16, a_height / 9)) : FramedWidth / 16;
		return FramedSize{ static_cast<uint32_t>(scale * 16), static_cast<uint32_t>(scale * 9) };
	}

	/**
	 * Retain the dominant eye's central sampling density across a combined frame.
	 * Bound growth to the eye's pixel count, preserving the existing 4K minimum
	 * and native crop size. Invalid geometry or source dimensions fail closed.
	 */
	constexpr std::optional<FramedSize> ResolveFramedSourceDensitySize(
		double a_frameTangentWidth,
		double a_pixelsPerHeadTangent,
		std::size_t a_eyeWidth,
		std::size_t a_eyeHeight)
	{
		const auto isPositiveFinite = [](double a_value) {
			return a_value > 0.0 && a_value <= std::numeric_limits<double>::max();
		};
		const auto minimum = ResolveFramedSize(a_eyeWidth, a_eyeHeight, true);
		if (!minimum || !isPositiveFinite(a_frameTangentWidth) || !isPositiveFinite(a_pixelsPerHeadTangent))
			return std::nullopt;

		constexpr uint64_t aspectPixels = 16 * 9;
		const uint64_t pixelBudget = std::max(uint64_t{ a_eyeWidth } * a_eyeHeight, uint64_t{ FramedWidth } * FramedHeight);
		uint64_t maximumSteps = minimum->width / 16;
		uint64_t rejectedSteps = MaximumFramedDimension / 16 + 1;
		while (rejectedSteps - maximumSteps > 1) {
			const uint64_t candidateSteps = maximumSteps + (rejectedSteps - maximumSteps) / 2;
			if (candidateSteps * candidateSteps * aspectPixels <= pixelBudget)
				maximumSteps = candidateSteps;
			else
				rejectedSteps = candidateSteps;
		}

		const double requestedSteps = a_frameTangentWidth * a_pixelsPerHeadTangent / 16;
		if (!isPositiveFinite(requestedSteps))
			return std::nullopt;
		uint64_t steps = maximumSteps;
		if (requestedSteps < static_cast<double>(maximumSteps)) {
			steps = static_cast<uint64_t>(requestedSteps);
			// Float projection metadata can perturb an exact aspect step slightly.
			constexpr double roundingTolerance = 1.0e-4;
			steps += requestedSteps - static_cast<double>(steps) > roundingTolerance;
			steps = std::max<uint64_t>(steps, minimum->width / 16);
		}
		return FramedSize{ static_cast<uint32_t>(steps * 16), static_cast<uint32_t>(steps * 9) };
	}
}
