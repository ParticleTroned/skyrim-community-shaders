#pragma once
#include "../DLSSViewportCrop.h"
#include "ComputeSubrect.h"
#include <algorithm>
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
	/** Preserve engine-guide density independently of the selected colour inference grid. */
	[[nodiscard]] constexpr UpscalingDLSS::Extent BuildMonoGuideExtent(UpscalingDLSS::Extent source,
		UpscalingDLSS::Extent guides, UpscalingDLSS::Extent model) noexcept
	{
		for (const auto extent : { source, guides, model })
			if (!extent.IsValid() || extent.width > kMaximumNeuralImageDimension || extent.height > kMaximumNeuralImageDimension)
				return {};
		const auto axis = [](std::uint32_t input, std::uint32_t color, std::uint32_t output) {
			return std::min(output, static_cast<std::uint32_t>((std::uint64_t(input) * output + color - 1u) / color));
		};
		return { axis(guides.width, source.width, model.width), axis(guides.height, source.height, model.height) };
	}
}
