#pragma once

#include "ComparisonLease.h"
#include "ModelResolutionPolicy.h"

#include <algorithm>
#include <span>

namespace NeuralRendering
{
	inline constexpr float kPeripheralProtectedFraction = 0.8f;
	inline constexpr std::uint32_t kPeripheralWorkingPercent = 90;
	inline constexpr std::uint32_t kPeripheralMinimumSavingPercent = 5;
	inline constexpr float kPeripheralMaximumFootprint = 16.0f;

	enum class PeripheralCompressionReason : std::uint8_t
	{
		Disabled,
		Identity,
		Experimental,
		ActorRoute,
		RestrictedDomain,
		CompactInputs,
		GuidedUnqualified,
		InvalidGeometry,
		InsufficientSavings,
		FootprintLimit,
		PairUnsupported,
		ShaderUnavailable,
		ForcedOrdinary
	};
	/** Eligibility is resolved after the existing uniform-scale and actor policies. */
	struct PeripheralCompressionSupport
	{
		bool requested = false, identity = false, actorRoute = false, restricted = false;
		bool compactInputs = false, guidedConflict = false;
	};

	/** Immutable projection shared by admission, native geometry, dispatch and retained evidence. */
	struct PeripheralCompressionPlan
	{
		PeripheralCompressionReason reason = PeripheralCompressionReason::Disabled;
		PeripheralMapping mapping{};
		UpscalingDLSS::Extent source{}, sourceGuides{}, ordinaryModel{}, ordinaryGuides{}, model{}, guides{};
		UpscalingDLSS::ViewportCrop sourceCrop{};
		ComputeSubrect ownedOutput{}, ordinaryContext{};
		std::array<float, 2> maximumFootprint{};
		std::uint32_t uniformPercent = 100;
		std::uint64_t baselinePixels = 0, candidatePixels = 0;
		bool requested = false;

		bool operator==(const PeripheralCompressionPlan&) const = default;
		[[nodiscard]] bool Active() const noexcept { return reason == PeripheralCompressionReason::Experimental; }
		/** A stale plan is a transaction error, never permission to guess a different geometry. */
		[[nodiscard]] bool Matches(const ModelResolutionGeometry& baseline, UpscalingDLSS::Extent sourceGuideSize,
			const UpscalingDLSS::ViewportCrop& crop, std::uint32_t percent) const noexcept
		{
			return source == baseline.sourceSize && sourceGuides == sourceGuideSize && sourceCrop == crop &&
			       ordinaryModel == baseline.modelSize && ordinaryGuides == baseline.modelGuideSize &&
			       ownedOutput == baseline.sourceRegion && ordinaryContext == baseline.modelRegion && uniformPercent == percent;
		}
	};

	/** Fixed 80/90 candidate uses CSX's integer extents without adding the donor's even-size padding. */
	[[nodiscard]] inline PeripheralCompressionPlan BuildPeripheralCompressionPlan(const ModelResolutionGeometry& baseline,
		UpscalingDLSS::Extent sourceGuides, const UpscalingDLSS::ViewportCrop& crop, std::uint32_t percent,
		PeripheralCompressionSupport support) noexcept
	{
		PeripheralCompressionPlan plan;
		plan.requested = support.requested;
		plan.source = baseline.sourceSize;
		plan.sourceGuides = sourceGuides;
		plan.sourceCrop = crop;
		plan.ordinaryModel = plan.model = baseline.modelSize;
		plan.ordinaryGuides = plan.guides = baseline.modelGuideSize;
		plan.ownedOutput = baseline.sourceRegion;
		plan.ordinaryContext = baseline.modelRegion;
		plan.uniformPercent = percent;
		plan.baselinePixels = plan.candidatePixels = baseline.modelRegion.Area();
		using enum PeripheralCompressionReason;
		const auto reject = [&](PeripheralCompressionReason reason) { plan.reason = reason; return plan; };
		if (!support.requested)
			return plan;
		if (support.identity)
			return reject(Identity);
		if (!IsValidModelResolutionPercent(percent) || !baseline.modelSize.IsValid() || !baseline.modelGuideSize.IsValid() ||
			!baseline.sourceSize.IsValid() || !sourceGuides.IsValid() ||
			baseline.modelSize != BuildModelResolutionExtent(baseline.sourceSize.width, baseline.sourceSize.height, percent) ||
			baseline.modelGuideSize != BuildModelResolutionExtent(sourceGuides.width, sourceGuides.height, percent) ||
			!baseline.sourceRegion.Fits(baseline.sourceSize.width, baseline.sourceSize.height) ||
			!baseline.modelRegion.Fits(baseline.modelSize.width, baseline.modelSize.height) ||
			!crop.MatchesEvaluationExtents(sourceGuides.width, sourceGuides.height, baseline.sourceSize.width, baseline.sourceSize.height))
			return reject(InvalidGeometry);
		if (support.actorRoute || baseline.packedCrops.count)
			return reject(ActorRoute);
		if (support.compactInputs)
			return reject(CompactInputs);
		if (support.guidedConflict)
			return reject(GuidedUnqualified);
		if (support.restricted || baseline.sourceRegion != ComputeSubrect{ 0, 0, baseline.sourceSize.width, baseline.sourceSize.height } ||
			baseline.modelRegion != ComputeSubrect{ 0, 0, baseline.modelSize.width, baseline.modelSize.height } ||
			crop != UpscalingDLSS::ViewportCrop::Identity(sourceGuides.width, sourceGuides.height, baseline.sourceSize.width, baseline.sourceSize.height))
			return reject(RestrictedDomain);
		const std::array ordinary{ baseline.modelSize.width, baseline.modelSize.height };
		const std::array ordinaryGuides{ baseline.modelGuideSize.width, baseline.modelGuideSize.height };
		const std::array source{ baseline.sourceSize.width, baseline.sourceSize.height };
		const std::array sourceGuideAxes{ sourceGuides.width, sourceGuides.height };
		std::array<std::uint32_t, 2> model{}, guides{};
		for (std::size_t axis = 0; axis < 2; ++axis) {
			model[axis] = (ordinary[axis] * kPeripheralWorkingPercent + 99u) / 100u;
			guides[axis] = static_cast<std::uint32_t>((static_cast<std::uint64_t>(ordinaryGuides[axis]) * model[axis] + ordinary[axis] - 1u) / ordinary[axis]);
			const float work = static_cast<float>(model[axis]) / ordinary[axis];
			const float compression = (work - kPeripheralProtectedFraction) / (1.0f - kPeripheralProtectedFraction);
			plan.mapping.centre[axis] = kPeripheralProtectedFraction;
			plan.mapping.work[axis] = work;
			plan.mapping.compression[axis] = compression;
			plan.mapping.edgeSlope[axis] = compression * compression;
			plan.maximumFootprint[axis] = std::max(static_cast<float>(source[axis]) / model[axis],
											  static_cast<float>(sourceGuideAxes[axis]) / guides[axis]) *
			                              work / plan.mapping.edgeSlope[axis];
			if (!std::isfinite(plan.maximumFootprint[axis]) || plan.maximumFootprint[axis] > kPeripheralMaximumFootprint)
				return reject(FootprintLimit);
		}
		plan.model = { model[0], model[1] };
		plan.guides = { guides[0], guides[1] };
		if (!ResolveFeatureUpscaling(guides[0], guides[1], model[0], model[1]))
			return reject(InvalidGeometry);
		plan.candidatePixels = static_cast<std::uint64_t>(model[0]) * model[1];
		if (plan.candidatePixels * 100u > plan.baselinePixels * (100u - kPeripheralMinimumSavingPercent))
			return reject(InsufficientSavings);
		plan.reason = Experimental;
		return plan;
	}

	/** Resolve a whole transaction before native work; an unsupported eye preserves the ordinary pair. */
	inline void ReconcilePeripheralCompression(std::span<PeripheralCompressionPlan> plans) noexcept
	{
		if (plans.empty() || std::ranges::all_of(plans, [](const auto& plan) { return plan.Active(); }))
			return;
		for (auto& plan : plans)
			if (plan.Active())
				plan.reason = PeripheralCompressionReason::PairUnsupported;
	}

	[[nodiscard]] constexpr const char* GetPeripheralCompressionReasonName(PeripheralCompressionReason reason) noexcept
	{
		using enum PeripheralCompressionReason;
		switch (reason) {
		case Disabled:
			return "disabled";
		case Identity:
			return "identity";
		case Experimental:
			return "experimental_unqualified";
		case ActorRoute:
			return "actor_or_atlas_route";
		case RestrictedDomain:
			return "restricted_source_domain";
		case CompactInputs:
			return "compact_inputs";
		case GuidedUnqualified:
			return "guided_combination_unqualified";
		case InvalidGeometry:
			return "invalid_geometry";
		case InsufficientSavings:
			return "insufficient_pixel_saving";
		case FootprintLimit:
			return "filter_footprint_limit";
		case PairUnsupported:
			return "pair_unsupported";
		case ShaderUnavailable:
			return "shader_unavailable";
		case ForcedOrdinary:
			return "forced_ordinary";
		default:
			return "unknown";
		}
	}
#ifdef DEVBENCH_BRIDGE_ENABLED
	enum class PeripheralCompressionOverride
	{
		Auto,
		Ordinary,
		Identity,
		CompressedExperiment
	};
	using PeripheralCompressionOverrideStatus = ComparisonOverrideStatus<PeripheralCompressionOverride>;
	using PeripheralCompressionExperiment = ComparisonLease<PeripheralCompressionOverride, PeripheralCompressionOverride::CompressedExperiment>;
	[[nodiscard]] constexpr const char* GetPeripheralCompressionOverrideName(PeripheralCompressionOverride mode) noexcept
	{
		switch (mode) {
		case PeripheralCompressionOverride::Ordinary:
			return "ordinary";
		case PeripheralCompressionOverride::Identity:
			return "identity";
		case PeripheralCompressionOverride::CompressedExperiment:
			return "compressed_experiment";
		default:
			return "auto";
		}
	}
#endif

}
