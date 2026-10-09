#pragma once

#include "../DLSSViewportCrop.h"
#include "ComputeSubrect.h"
#include "Features/FoveatedCommon.h"

#include <array>
#include <cstdint>

namespace NeuralRendering
{
	inline constexpr std::uint32_t kMinimumCentralAreaPercent = 25;
	inline constexpr std::uint32_t kMaximumCentralAreaPercent = 100;
	inline constexpr std::uint32_t kDefaultCentralFeatherPixels = 64;
	inline constexpr std::uint32_t kMaximumCentralFeatherPixels = 256;

	/** NR-only superellipse; size is independent of the headset/upscaler mask. */
	struct CentralArea
	{
		std::uint32_t percent = kMaximumCentralAreaPercent;
		std::uint32_t featherPixels = kDefaultCentralFeatherPixels;
		float horizontalScale = 1.0f;
		std::array<float, 2> offset{};
		UpscalingDLSS::Extent finalOutput{};

		[[nodiscard]] bool Active() const noexcept { return percent < kMaximumCentralAreaPercent; }
		[[nodiscard]] bool Valid() const noexcept
		{
			return percent >= kMinimumCentralAreaPercent && percent <= kMaximumCentralAreaPercent &&
			       featherPixels <= kMaximumCentralFeatherPixels && std::isfinite(horizontalScale) &&
			       horizontalScale >= FoveatedCommon::kCenterHorizontalScaleMin && horizontalScale <= FoveatedCommon::kCenterHorizontalScaleMax &&
			       std::isfinite(offset[0]) && std::isfinite(offset[1]) &&
			       (!Active() || (finalOutput.IsValid() && finalOutput.width <= kMaximumNeuralImageDimension && finalOutput.height <= kMaximumNeuralImageDimension));
		}
		[[nodiscard]] float Scale() const noexcept { return static_cast<float>(percent) / 100.0f; }
		/** Conservative radial support for the pixel-space feather on the shared FOV shape. */
		[[nodiscard]] float FeatherUV() const noexcept
		{
			const auto extent = std::min(static_cast<float>(finalOutput.width) * horizontalScale, static_cast<float>(finalOutput.height));
			return extent > 0.0f ? static_cast<float>(featherPixels) / extent : 0.0f;
		}
		bool operator==(const CentralArea&) const = default;
	};

	/** Map shared FOV-shape support into the NR crop, independently of model resolution. */
	[[nodiscard]] inline ComputeSubrect BuildCentralAreaSupport(const UpscalingDLSS::ViewportCrop& crop, const CentralArea& area) noexcept
	{
		if (!area.Valid() || !crop.IsValid() || crop.fullOutput.width > kMaximumNeuralImageDimension || crop.fullOutput.height > kMaximumNeuralImageDimension)
			return {};
		const ComputeSubrect output{ crop.output.left, crop.output.top, crop.output.Width(), crop.output.Height() };
		if (!area.Active())
			return { 0, 0, output.width, output.height };
		const auto bounds = FoveatedCommon::BuildCenteredDispatchBounds(0, crop.fullOutput.width, crop.fullOutput.height,
			area.Scale(), area.offset[0], area.offset[1], area.FeatherUV(), area.horizontalScale);
		const auto clipped = IntersectComputeSubrect(output, { static_cast<std::uint32_t>(bounds.minX), static_cast<std::uint32_t>(bounds.minY),
																 static_cast<std::uint32_t>(bounds.maxX - bounds.minX), static_cast<std::uint32_t>(bounds.maxY - bounds.minY) });
		return clipped.IsValid() ? ComputeSubrect{ clipped.baseX - output.baseX, clipped.baseY - output.baseY, clipped.width, clipped.height } : ComputeSubrect{};
	}
}
