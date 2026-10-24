#pragma once

#include "ComparisonLease.h"
#include "ModelResolutionPolicy.h"

#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace NeuralRendering
{
	inline constexpr std::uint32_t kProvisionalGuidedCutoffPercent = 50;

	enum class GuidedReconstructionMode
	{
		Basic,
		GuidedLowResolution
	};
	enum class GuidedReconstructionReason
	{
		NotRequired,
		Disabled,
		NoReduction,
		AboveCutoff,
		Unqualified,
		UnsupportedRoute,
		UnsupportedDomain,
		InvalidGeometry,
		ShaderUnavailable,
		ForcedBasic,
		Eligible,
		Experiment
	};
	struct GuidedReconstructionDecision
	{
		GuidedReconstructionMode mode = GuidedReconstructionMode::Basic;
		GuidedReconstructionReason reason = GuidedReconstructionReason::NotRequired;
		bool operator==(const GuidedReconstructionDecision&) const = default;
	};
	/** Qualification is independent for pre-DLSS and final-image reconstruction. */
	struct GuidedReconstructionQualification
	{
		std::uint32_t maximumPercent = kProvisionalGuidedCutoffPercent;
		bool beforeUpscaling = false, afterUpscaling = false;
	};
	inline constexpr GuidedReconstructionQualification kGuidedReconstructionQualification{};

	/** Sampling uses the unpacked grid, never atlas occupancy or allocation capacity. */
	struct GuidedReconstructionInput
	{
		bool enabled = false;
		std::uint32_t effectivePercent = 100;
		UpscalingDLSS::Extent target{}, unpackedModel{};
		InsertionPoint insertion = kDefaultInsertionPoint;
		bool matchedReferenceDomain = false;
	};

	/** Decide once for the complete transaction from already validated CPU metadata. */
	[[nodiscard]] constexpr GuidedReconstructionDecision SelectGuidedReconstruction(
		std::span<const GuidedReconstructionInput> inputs,
		GuidedReconstructionQualification qualification = kGuidedReconstructionQualification) noexcept
	{
		using enum GuidedReconstructionReason;
		const auto basic = [](GuidedReconstructionReason reason) { return GuidedReconstructionDecision{ GuidedReconstructionMode::Basic, reason }; };
		if (inputs.empty() || inputs.size() > kMaximumRegionEvaluations || !IsValidModelResolutionPercent(qualification.maximumPercent))
			return basic(InvalidGeometry);
		for (const auto& input : inputs) {
			if (!input.enabled)
				return basic(Disabled);
			if (!IsValidModelResolutionPercent(input.effectivePercent) || !input.target.IsValid() || !input.unpackedModel.IsValid() ||
				input.unpackedModel != BuildModelResolutionExtent(input.target.width, input.target.height, input.effectivePercent))
				return basic(InvalidGeometry);
			if (input.unpackedModel.width >= input.target.width || input.unpackedModel.height >= input.target.height)
				return basic(NoReduction);
			if (input.insertion != InsertionPoint::UpscaledCenter && input.insertion != InsertionPoint::FinalLdrPreUi)
				return basic(UnsupportedRoute);
			if (!input.matchedReferenceDomain)
				return basic(UnsupportedDomain);
			if (input.effectivePercent > qualification.maximumPercent)
				return basic(AboveCutoff);
			if (!(input.insertion == InsertionPoint::UpscaledCenter ? qualification.beforeUpscaling : qualification.afterUpscaling))
				return basic(Unqualified);
		}
		return { GuidedReconstructionMode::GuidedLowResolution, Eligible };
	}

	/** Stable names shared by live status and retained execution evidence. */
	[[nodiscard]] constexpr const char* GetGuidedReconstructionModeName(GuidedReconstructionMode mode) noexcept
	{
		switch (mode) {
		case GuidedReconstructionMode::Basic:
			return "basic";
		case GuidedReconstructionMode::GuidedLowResolution:
			return "guided_low_resolution";
		default:
			return "unknown";
		}
	}

	[[nodiscard]] constexpr const char* GetGuidedReconstructionReasonName(GuidedReconstructionReason reason) noexcept
	{
		switch (reason) {
		case GuidedReconstructionReason::NotRequired:
			return "not_required";
		case GuidedReconstructionReason::Disabled:
			return "disabled";
		case GuidedReconstructionReason::NoReduction:
			return "no_reduction";
		case GuidedReconstructionReason::AboveCutoff:
			return "above_cutoff";
		case GuidedReconstructionReason::Unqualified:
			return "unqualified";
		case GuidedReconstructionReason::UnsupportedRoute:
			return "unsupported_route";
		case GuidedReconstructionReason::UnsupportedDomain:
			return "unsupported_reference_domain";
		case GuidedReconstructionReason::InvalidGeometry:
			return "invalid_geometry";
		case GuidedReconstructionReason::ShaderUnavailable:
			return "optional_shader_unavailable";
		case GuidedReconstructionReason::ForcedBasic:
			return "forced_basic";
		case GuidedReconstructionReason::Eligible:
			return "eligible";
		case GuidedReconstructionReason::Experiment:
			return "guided_experiment";
		default:
			return "unknown";
		}
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	enum class GuidedReconstructionOverride
	{
		Auto,
		Basic,
		GuidedExperiment
	};
	[[nodiscard]] constexpr const char* GetGuidedReconstructionOverrideName(GuidedReconstructionOverride mode) noexcept
	{
		return mode == GuidedReconstructionOverride::Basic ? "basic" : mode == GuidedReconstructionOverride::GuidedExperiment ? "guided_experiment" :
		                                                                                                                        "auto";
	}
	using GuidedReconstructionOverrideStatus = ComparisonOverrideStatus<GuidedReconstructionOverride>;
	using GuidedReconstructionExperiment = ComparisonLease<GuidedReconstructionOverride, GuidedReconstructionOverride::GuidedExperiment>;
#endif
}
