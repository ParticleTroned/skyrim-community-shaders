#pragma once
#include <cstdint>
#include <optional>
namespace NeuralRendering
{
	enum class RenderingMode : std::uint32_t { FullResolution = 0, ReducedResolution = 2 };
	[[nodiscard]] constexpr RenderingMode ConfiguredRenderingMode(std::uint32_t value) noexcept
	{
		return value == 2 ? RenderingMode::ReducedResolution : RenderingMode::FullResolution;
	}
	/** Native feature upscaling follows actual guide/output geometry. */
	[[nodiscard]] constexpr std::optional<bool> ResolveFeatureUpscaling(std::uint32_t guideWidth,
		std::uint32_t guideHeight, std::uint32_t outputWidth, std::uint32_t outputHeight) noexcept
	{
		if (!guideWidth || !guideHeight || !outputWidth || !outputHeight || guideWidth > outputWidth || guideHeight > outputHeight)
			return std::nullopt;
		return guideWidth != outputWidth || guideHeight != outputHeight;
	}
}
