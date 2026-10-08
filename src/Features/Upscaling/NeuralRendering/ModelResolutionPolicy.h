#pragma once

#include "../DLSSViewportCrop.h"
#include "ComputeSubrect.h"
#include "PipelinePolicy.h"

#include <array>
#include <cstdint>
#include <optional>

namespace NeuralRendering
{
	inline constexpr std::uint32_t kMinimumModelResolutionPercent = 33;
	inline constexpr std::uint32_t kMaximumModelResolutionPercent = 100;
	inline constexpr std::uint32_t kMaximumModelResolutionDimension = 16384;

	/** Independent inference scale is a whole percentage of the existing render grid. */
	[[nodiscard]] constexpr bool IsValidModelResolutionPercent(std::uint32_t percent) noexcept
	{
		return percent >= kMinimumModelResolutionPercent && percent <= kMaximumModelResolutionPercent;
	}

	/** Other routes retain their original evaluation domain and ignore the saved C scale. */
	[[nodiscard]] constexpr std::uint32_t EffectiveModelResolutionPercent(RenderingMode mode, std::uint32_t percent) noexcept
	{
		return mode == RenderingMode::ReducedResolution && IsValidModelResolutionPercent(percent) ? percent : kMaximumModelResolutionPercent;
	}

	/** Ceiling division retains nonempty odd dimensions without exceeding D3D11 limits. */
	[[nodiscard]] constexpr UpscalingDLSS::Extent BuildModelResolutionExtent(
		std::uint32_t width, std::uint32_t height, std::uint32_t percent) noexcept
	{
		if (!width || !height || width > kMaximumModelResolutionDimension || height > kMaximumModelResolutionDimension ||
			!IsValidModelResolutionPercent(percent))
			return {};
		return { (width * percent + 99u) / 100u, (height * percent + 99u) / 100u };
	}
	/** One physical eye crop, independent of the following DLSS reconstruction grid. */
	struct ModelResolutionGeometry
	{
		UpscalingDLSS::Extent sourceSize{}, modelSize{};
		ComputeSubrect sourceRegion{}, modelRegion{};
		UpscalingDLSS::ViewportCrop nativeCrop{};
		std::array<float, 2> motionNormalization{};
	};

	/** Map crop-local inference work while converting full-eye normalized motion exactly once. */
	[[nodiscard]] constexpr std::optional<ModelResolutionGeometry> BuildModelResolutionGeometry(
		const UpscalingDLSS::ViewportCrop& crop, UpscalingDLSS::Extent source,
		ComputeSubrect region, std::uint32_t percent) noexcept
	{
		const auto model = BuildModelResolutionExtent(source.width, source.height, percent);
		if (!model.IsValid() || !region.Fits(source.width, source.height) ||
			!crop.MatchesEvaluationExtents(source.width, source.height, source.width, source.height) ||
			crop.fullInput != crop.fullOutput || crop.input != crop.output ||
			crop.fullInput.width > kMaximumModelResolutionDimension || crop.fullInput.height > kMaximumModelResolutionDimension)
			return std::nullopt;
		return ModelResolutionGeometry{
			.sourceSize = source,
			.modelSize = model,
			.sourceRegion = region,
			.modelRegion = MapComputeSubrect(region, source.width, source.height, model.width, model.height),
			.nativeCrop = UpscalingDLSS::ViewportCrop::Identity(model.width, model.height, model.width, model.height),
			.motionNormalization = { static_cast<float>(crop.fullInput.width) / source.width,
				static_cast<float>(crop.fullInput.height) / source.height },
		};
	}
}
