#pragma once

#include "../DLSSViewportCrop.h"
#include "CentralAreaPolicy.h"
#include "ComputeSubrect.h"
#include "PipelinePolicy.h"

#include <array>
#include <cstdint>
#include <optional>

namespace NeuralRendering
{
	inline constexpr std::uint32_t kMinimumModelResolutionPercent = 30;
	inline constexpr std::uint32_t kMaximumModelResolutionPercent = 100;
	inline constexpr std::uint32_t kMaximumModelResolutionDimension = kMaximumNeuralImageDimension;

	/** Independent inference scale is a whole percentage of the selected route's colour grid. */
	[[nodiscard]] constexpr bool IsValidModelResolutionPercent(std::uint32_t percent) noexcept
	{
		return percent >= kMinimumModelResolutionPercent && percent <= kMaximumModelResolutionPercent;
	}

	/** All supported routes share one independent inference scale. */
	[[nodiscard]] constexpr std::uint32_t EffectiveModelResolutionPercent(RenderingMode mode, std::uint32_t percent) noexcept
	{
		const bool supported = mode == RenderingMode::FullResolution || mode == RenderingMode::Foveated || mode == RenderingMode::ReducedResolution;
		return supported && IsValidModelResolutionPercent(percent) ? percent : kMaximumModelResolutionPercent;
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
	/** Source crop and scale remain part of temporal identity after model-grid projection. */
	struct ModelResolutionHistory
	{
		UpscalingDLSS::ViewportCrop viewportCrop{};
		std::uint32_t percent = kMaximumModelResolutionPercent;
		ComputeSubrect sourceRegion{};
		std::uintptr_t controlMaskIdentity = 0;
		CentralArea centralArea{};

		bool operator==(const ModelResolutionHistory&) const = default;
	};

	/** One physical eye crop, independent of the surrounding upscaler. */
	struct ModelResolutionGeometry
	{
		UpscalingDLSS::Extent sourceSize{}, modelSize{}, modelGuideSize{};
		ComputeSubrect sourceRegion{}, modelRegion{};
		UpscalingDLSS::ViewportCrop nativeCrop{};
		std::array<float, 2> motionNormalization{};
	};

	/** Map crop-local inference work while converting full-eye normalized motion exactly once. */
	[[nodiscard]] constexpr std::optional<ModelResolutionGeometry> BuildModelResolutionGeometry(
		const UpscalingDLSS::ViewportCrop& crop, UpscalingDLSS::Extent source,
		ComputeSubrect region, std::uint32_t percent, UpscalingDLSS::Extent guides = {}) noexcept
	{
		if (guides == UpscalingDLSS::Extent{})
			guides = source;
		const auto model = BuildModelResolutionExtent(source.width, source.height, percent);
		const auto modelGuides = BuildModelResolutionExtent(guides.width, guides.height, percent);
		if (!model.IsValid() || !modelGuides.IsValid() || !region.Fits(source.width, source.height) ||
			!ResolveFeatureUpscaling(guides.width, guides.height, source.width, source.height) ||
			!crop.MatchesEvaluationExtents(guides.width, guides.height, source.width, source.height) ||
			(guides == source && (crop.fullInput != crop.fullOutput || crop.input != crop.output)) ||
			crop.fullInput.width > kMaximumModelResolutionDimension || crop.fullInput.height > kMaximumModelResolutionDimension ||
			crop.fullOutput.width > kMaximumModelResolutionDimension || crop.fullOutput.height > kMaximumModelResolutionDimension)
			return std::nullopt;
		return ModelResolutionGeometry{
			.sourceSize = source,
			.modelSize = model,
			.modelGuideSize = modelGuides,
			.sourceRegion = region,
			.modelRegion = MapComputeSubrect(region, source.width, source.height, model.width, model.height),
			.nativeCrop = UpscalingDLSS::ViewportCrop::Identity(modelGuides.width, modelGuides.height, model.width, model.height),
			.motionNormalization = { static_cast<float>(crop.fullInput.width) / guides.width,
				static_cast<float>(crop.fullInput.height) / guides.height },
		};
	}
}
