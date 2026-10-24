#pragma once

#include "ComparisonLease.h"
#include "ModelResolutionPolicy.h"

#include <algorithm>
#include <span>

namespace NeuralRendering
{
	inline constexpr float kPeripheralMaximumFootprint = 32.0f;
	// The quartic mask has max |x|*|y|^3 below 0.57 when x^4+y^4=1.
	inline constexpr float kPeripheralFovCrossDerivativeBound = 0.57f;

	enum class PeripheralCompressionReason : std::uint8_t
	{
		Disabled,
		Identity,
		Experimental,
		ActorRoute,
		RestrictedDomain,
		CompactInputs,
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
		bool compactInputs = false;
		std::uint32_t compressionPercent = kLegacyPeripheralCompressionPercent;
		PeripheralMask mask{};
	};

	/** Immutable projection shared by admission, native geometry, dispatch and retained evidence. */
	struct PeripheralCompressionPlan
	{
		PeripheralCompressionReason reason = PeripheralCompressionReason::Disabled;
		PeripheralMapping mapping{};
		PeripheralMask mask{};
		UpscalingDLSS::Extent source{}, sourceGuides{}, ordinaryModel{}, ordinaryGuides{}, model{}, guides{};
		UpscalingDLSS::ViewportCrop sourceCrop{};
		ComputeSubrect ownedOutput{}, ordinaryContext{};
		std::array<float, 2> maximumFootprint{};
		std::uint32_t uniformPercent = 100, compressionPercent = 0;
		std::uint64_t baselinePixels = 0, candidatePixels = 0;
		bool requested = false;

		bool operator==(const PeripheralCompressionPlan&) const = default;
		[[nodiscard]] bool Active() const noexcept { return reason == PeripheralCompressionReason::Experimental; }
		/** A stale plan is a transaction error, never permission to guess a different geometry. */
		[[nodiscard]] bool Matches(const ModelResolutionGeometry& baseline, UpscalingDLSS::Extent sourceGuideSize,
			const UpscalingDLSS::ViewportCrop& crop, std::uint32_t percent, std::uint32_t requestedPercent, const PeripheralMask& currentMask) const noexcept
		{
			return source == baseline.sourceSize && sourceGuides == sourceGuideSize && sourceCrop == crop &&
			       ordinaryModel == baseline.modelSize && ordinaryGuides == baseline.modelGuideSize &&
			       ownedOutput == baseline.sourceRegion && ordinaryContext == baseline.modelRegion && uniformPercent == percent &&
			       compressionPercent == requestedPercent && mask.active == currentMask.active && (!mask.active || mask == currentMask);
		}
	};

	/** Shrink the existing evaluation crop while retaining its calibrated mask and source ownership. */
	[[nodiscard]] inline PeripheralCompressionPlan BuildPeripheralCompressionPlan(const ModelResolutionGeometry& baseline,
		UpscalingDLSS::Extent sourceGuides, const UpscalingDLSS::ViewportCrop& crop, std::uint32_t percent,
		PeripheralCompressionSupport support) noexcept
	{
		PeripheralCompressionPlan plan;
		plan.requested = support.requested && support.compressionPercent != 0;
		plan.compressionPercent = support.compressionPercent;
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
		if (!plan.requested)
			return plan;
		if (support.identity)
			return reject(Identity);
		if (support.compressionPercent > kMaximumPeripheralCompressionPercent || !IsValidModelResolutionPercent(percent) || !baseline.modelSize.IsValid() || !baseline.modelGuideSize.IsValid() ||
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
		if (support.restricted || baseline.sourceRegion != ComputeSubrect{ 0, 0, baseline.sourceSize.width, baseline.sourceSize.height } ||
			baseline.modelRegion != ComputeSubrect{ 0, 0, baseline.modelSize.width, baseline.modelSize.height } ||
			(!support.mask.active && crop != UpscalingDLSS::ViewportCrop::Identity(sourceGuides.width, sourceGuides.height, baseline.sourceSize.width, baseline.sourceSize.height)))
			return reject(RestrictedDomain);
		auto& mapping = plan.mapping;
		mapping.protectedFraction = 1.0f - static_cast<float>(support.compressionPercent) * 0.01f;
		mapping.boundaryScale = (1.0f + mapping.protectedFraction) * 0.5f;
		mapping.edgeSlope = 0.25f;
		mapping.shaped = support.mask.active ? 1u : 0u;
		const std::array ordinary{ baseline.modelSize.width, baseline.modelSize.height };
		const std::array ordinaryGuides{ baseline.modelGuideSize.width, baseline.modelGuideSize.height };
		const std::array source{ baseline.sourceSize.width, baseline.sourceSize.height };
		const std::array sourceGuideAxes{ sourceGuides.width, sourceGuides.height };
		const std::array full{ crop.fullOutput.width, crop.fullOutput.height };
		const std::array offset{ crop.output.left, crop.output.top };
		for (std::size_t axis = 0; axis < 2; ++axis) {
			if (support.mask.active) {
				if (!std::isfinite(support.mask.center[axis]) || !std::isfinite(support.mask.radii[axis]) ||
					support.mask.center[axis] < 0.0f || support.mask.center[axis] > 1.0f ||
					support.mask.radii[axis] < 1e-4f || support.mask.radii[axis] > 1.0f)
					return reject(InvalidGeometry);
				mapping.center[axis] = (support.mask.center[axis] * full[axis] - offset[axis]) / source[axis];
				mapping.radii[axis] = support.mask.radii[axis] * full[axis] / source[axis];
				if (mapping.center[axis] < 0.0f || mapping.center[axis] > 1.0f)
					return reject(InvalidGeometry);
			}
		}
		// Invalid or inactive mask payloads must not poison ordinary transaction comparisons.
		if (support.mask.active)
			plan.mask = support.mask;
		std::array<std::uint32_t, 2> model{}, guides{};
		PeripheralMapping::Point origin{}, work{};
		for (std::size_t axis = 0; axis < 2; ++axis) {
			PeripheralMapping::Point lower{ std::clamp(mapping.center[0], 0.0f, 1.0f), std::clamp(mapping.center[1], 0.0f, 1.0f) };
			auto upper = lower;
			lower[axis] = 0.0f;
			upper[axis] = 1.0f;
			origin[axis] = mapping.Forward(lower)[axis];
			work[axis] = mapping.Forward(upper)[axis] - origin[axis];
			if (!std::isfinite(work[axis]) || work[axis] <= 0.0f || work[axis] > 1.0f)
				return reject(InvalidGeometry);
			model[axis] = mapping.shaped ? std::clamp(static_cast<std::uint32_t>(std::ceil(ordinary[axis] * work[axis])), 1u, ordinary[axis]) :
			                               (ordinary[axis] * (200u - support.compressionPercent) + 199u) / 200u;
			guides[axis] = static_cast<std::uint32_t>((static_cast<std::uint64_t>(ordinaryGuides[axis]) * model[axis] + ordinary[axis] - 1u) / ordinary[axis]);
		}
		mapping.origin = origin;
		mapping.work = work;

		for (std::size_t axis = 0; axis < 2; ++axis) {
			const auto other = 1u - axis;
			const float cross = mapping.shaped ? kPeripheralFovCrossDerivativeBound : 1.0f;
			const float aspect = mapping.radii[axis] / mapping.radii[other];
			const float colorStep = mapping.work[axis] / model[axis] + cross * aspect * (1.0f - mapping.edgeSlope) * mapping.work[other] / model[other];
			const float guideStep = mapping.work[axis] / guides[axis] + cross * aspect * (1.0f - mapping.edgeSlope) * mapping.work[other] / guides[other];
			plan.maximumFootprint[axis] = std::max(source[axis] * colorStep, sourceGuideAxes[axis] * guideStep) / mapping.edgeSlope;
			if (!std::isfinite(plan.maximumFootprint[axis]) || plan.maximumFootprint[axis] > kPeripheralMaximumFootprint)
				return reject(FootprintLimit);
		}

		plan.model = { model[0], model[1] };
		plan.guides = { guides[0], guides[1] };
		if (!ResolveFeatureUpscaling(guides[0], guides[1], model[0], model[1]))
			return reject(InvalidGeometry);
		plan.candidatePixels = static_cast<std::uint64_t>(model[0]) * model[1];
		if (plan.candidatePixels >= plan.baselinePixels)
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
