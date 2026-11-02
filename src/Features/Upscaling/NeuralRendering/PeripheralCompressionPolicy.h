#pragma once

#include "ModelResolutionPolicy.h"
#include "PeripheralMapping.h"
#include "RegionCapacity.h"

namespace NeuralRendering
{
	inline constexpr float kPeripheralMaximumFootprint = 32.0f;
	struct PeripheralCompressionPlan
	{
		PeripheralMapping mapping{};
		UpscalingDLSS::Extent model{};
		std::array<float, 2> maximumFootprint{};
		const char* reason = "disabled";
		bool active = false;
	};

	/** Compression owns a full rectangle; actor crops and restricted source domains keep uniform sampling. */
	[[nodiscard]] inline PeripheralCompressionPlan BuildMonoCompressionPlan(UpscalingDLSS::Extent source,
		UpscalingDLSS::Extent guides, std::uint32_t uniformPercent, std::uint32_t compressionPercent,
		bool actorRoute, bool restricted) noexcept
	{
		PeripheralCompressionPlan plan;
		plan.model = BuildModelResolutionExtent(source.width, source.height, uniformPercent);
		if (!compressionPercent)
			return plan;
		const auto reject = [&](const char* reason) { plan.reason = reason; return plan; };
		if (!plan.model.IsValid() || !guides.IsValid() || guides.width > kMaximumNeuralImageDimension || guides.height > kMaximumNeuralImageDimension || !IsValidModelResolutionPercent(uniformPercent) ||
			compressionPercent > kMaximumPeripheralCompressionPercent)
			return reject("invalid_geometry");
		if (actorRoute)
			return reject("actor_or_atlas_route");
		if (restricted)
			return reject("restricted_source_domain");
		auto mapping = PeripheralMapping{};
		mapping.protectedFraction = 1.0f - float(compressionPercent) * 0.01f;
		mapping.boundaryScale = (1.0f + mapping.protectedFraction) * 0.5f;
		mapping.edgeSlope = 0.25f;
		mapping.work = { mapping.boundaryScale, mapping.boundaryScale };
		mapping.origin = { (1.0f - mapping.boundaryScale) * 0.5f, (1.0f - mapping.boundaryScale) * 0.5f };
		UpscalingDLSS::Extent candidate{
			(plan.model.width * (200u - compressionPercent) + 199u) / 200u,
			(plan.model.height * (200u - compressionPercent) + 199u) / 200u
		};
		const std::array sourceAxes{ source.width, source.height };
		const std::array guideAxes{ guides.width, guides.height };
		const std::array guideReference{ std::min(source.width, guides.width), std::min(source.height, guides.height) };
		double minimumScale = 0.0;
		for (std::size_t axis = 0; axis < 2; ++axis) {
			const auto other = 1u - axis;
			const double colorFootprint = sourceAxes[axis] *
			                              (double(mapping.work[axis]) / sourceAxes[axis] + (1.0 - mapping.edgeSlope) * mapping.work[other] / sourceAxes[other]);
			const double guideFootprint = guideAxes[axis] *
			                              (double(mapping.work[axis]) / guideReference[axis] + (1.0 - mapping.edgeSlope) * mapping.work[other] / guideReference[other]);
			minimumScale = std::max(minimumScale, std::max(colorFootprint, guideFootprint) / (mapping.edgeSlope * kPeripheralMaximumFootprint));
		}
		// Approach the ordinary grid monotonically before bounded filtering loses its savings.
		candidate.width = std::min(plan.model.width, std::max({ candidate.width, kMinimumExperimentalContextExtent, static_cast<std::uint32_t>(std::ceil(source.width * minimumScale)) }));
		candidate.height = std::min(plan.model.height, std::max({ candidate.height, kMinimumExperimentalContextExtent, static_cast<std::uint32_t>(std::ceil(source.height * minimumScale)) }));
		if (candidate.width < kMinimumExperimentalContextExtent || candidate.height < kMinimumExperimentalContextExtent)
			return reject("minimum_context_extent");
		if (std::uint64_t(candidate.width) * candidate.height >= std::uint64_t(plan.model.width) * plan.model.height)
			return reject("insufficient_pixel_saving");
		const std::array axes{ candidate.width, candidate.height };
		const auto modelGuides = BuildMonoGuideExtent(source, guides, candidate);
		const std::array guideGrid{ modelGuides.width, modelGuides.height };
		for (std::size_t axis = 0; axis < 2; ++axis) {
			const float step = mapping.work[axis] / axes[axis] + (1.0f - mapping.edgeSlope) * mapping.work[1u - axis] / axes[1u - axis];
			const float guideStep = mapping.work[axis] / guideGrid[axis] + (1.0f - mapping.edgeSlope) * mapping.work[1u - axis] / guideGrid[1u - axis];
			plan.maximumFootprint[axis] = std::max(sourceAxes[axis] * step, guideAxes[axis] * guideStep) / mapping.edgeSlope;
			if (!std::isfinite(plan.maximumFootprint[axis]) || plan.maximumFootprint[axis] > kPeripheralMaximumFootprint)
				return reject("filter_footprint_limit");
		}
		plan.mapping = mapping;
		plan.model = candidate;
		plan.active = true;
		plan.reason = "rectangular_compression";
		return plan;
	}
}
