#pragma once

#include "ModelResolutionPolicy.h"
#include "RegionCapacity.h"
#include "PeripheralMapping.h"

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
		if (!compressionPercent) return plan;
		const auto reject = [&](const char* reason) { plan.reason = reason; return plan; };
		if (!source.IsValid() || !guides.IsValid() || !IsValidModelResolutionPercent(uniformPercent) ||
			compressionPercent > kMaximumPeripheralCompressionPercent)
			return reject("invalid_geometry");
		if (actorRoute) return reject("actor_or_atlas_route");
		if (restricted) return reject("restricted_source_domain");
		auto mapping = PeripheralMapping{};
		mapping.protectedFraction = 1.0f - float(compressionPercent) * 0.01f;
		mapping.boundaryScale = (1.0f + mapping.protectedFraction) * 0.5f;
		mapping.edgeSlope = 0.25f;
		mapping.work = { mapping.boundaryScale, mapping.boundaryScale };
		mapping.origin = { (1.0f - mapping.boundaryScale) * 0.5f, (1.0f - mapping.boundaryScale) * 0.5f };
		const UpscalingDLSS::Extent candidate{
			(plan.model.width * (200u - compressionPercent) + 199u) / 200u,
			(plan.model.height * (200u - compressionPercent) + 199u) / 200u
		};
		if (candidate.width < kMinimumExperimentalContextExtent || candidate.height < kMinimumExperimentalContextExtent)
			return reject("minimum_context_extent");
		if (std::uint64_t(candidate.width) * candidate.height >= std::uint64_t(plan.model.width) * plan.model.height)
			return reject("insufficient_pixel_saving");
		const std::array axes{ candidate.width, candidate.height };
		const std::array input{ std::max(source.width, guides.width), std::max(source.height, guides.height) };
		for (std::size_t axis = 0; axis < 2; ++axis) {
			const float step = mapping.work[axis] / axes[axis] + (1.0f - mapping.edgeSlope) * mapping.work[1u - axis] / axes[1u - axis];
			plan.maximumFootprint[axis] = input[axis] * step / mapping.edgeSlope;
			if (!std::isfinite(plan.maximumFootprint[axis]) || plan.maximumFootprint[axis] > kPeripheralMaximumFootprint)
				return reject("filter_footprint_limit");
		}
		plan.mapping = mapping; plan.model = candidate; plan.active = true; plan.reason = "rectangular_compression";
		return plan;
	}
}
