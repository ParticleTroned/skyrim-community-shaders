#pragma once
#include "../DLSSViewportCrop.h"
#include "ComputeSubrect.h"
#include <cstdint>
namespace NeuralRendering
{
	inline constexpr std::uint32_t kMinimumModelResolutionPercent = 30;
	inline constexpr std::uint32_t kMaximumModelResolutionPercent = 100;
	[[nodiscard]] constexpr bool IsValidModelResolutionPercent(std::uint32_t percent) noexcept
	{
		return percent >= kMinimumModelResolutionPercent && percent <= kMaximumModelResolutionPercent;
	}
	/** Inference allocation is independent of the engine's scene render scale. */
	[[nodiscard]] constexpr UpscalingDLSS::Extent BuildModelResolutionExtent(std::uint32_t width,
		std::uint32_t height, std::uint32_t percent) noexcept
	{
		if (!width || !height || width > kMaximumNeuralImageDimension || height > kMaximumNeuralImageDimension || !IsValidModelResolutionPercent(percent))
			return {};
		return { (width * percent + 99u) / 100u, (height * percent + 99u) / 100u };
	}
}
