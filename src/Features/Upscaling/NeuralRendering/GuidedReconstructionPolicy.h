#pragma once

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
	struct GuidedReconstructionOverrideStatus
	{
		GuidedReconstructionOverride mode = GuidedReconstructionOverride::Auto;
		std::uint64_t revision = 1;
		std::uint32_t remainingFrames = 0;
	};
	/** A bounded comparison lease cannot restore over a newer request or user generation. */
	class GuidedReconstructionExperiment
	{
	public:
		static constexpr std::uint32_t kMaximumFrames = 600;
		[[nodiscard]] GuidedReconstructionOverrideStatus Status() const noexcept { return status_; }
		bool Configure(GuidedReconstructionOverride mode, std::uint32_t frames, std::uint64_t expectedRevision) noexcept
		{
			if (expectedRevision != status_.revision ||
				(mode != GuidedReconstructionOverride::Auto && mode != GuidedReconstructionOverride::Basic && mode != GuidedReconstructionOverride::GuidedExperiment) ||
				(mode == GuidedReconstructionOverride::Auto ? frames != 0 : frames == 0 || frames > kMaximumFrames))
				return false;
			changed_ |= mode != status_.mode;
			status_ = { mode, status_.revision + 1, frames };
			frame_.reset();
			generation_.reset();
			return true;
		}
		void Cancel() noexcept
		{
			if (status_.mode != GuidedReconstructionOverride::Auto)
				(void)Configure(GuidedReconstructionOverride::Auto, 0, status_.revision);
		}
		[[nodiscard]] bool ConsumeChange() noexcept { return std::exchange(changed_, false); }
		/** Resolve before source admission; the caller invalidates downstream history once on change. */
		[[nodiscard]] std::pair<GuidedReconstructionOverride, bool> Resolve(std::uint32_t frame, std::uint64_t generation) noexcept
		{
			if (status_.mode != GuidedReconstructionOverride::Auto) {
				if (frame == std::numeric_limits<std::uint32_t>::max() || (generation_ && *generation_ != generation))
					Cancel();
				else if (frame_ && frame != *frame_) {
					const auto elapsed = frame - *frame_;
					if (elapsed >= status_.remainingFrames)
						Cancel();
					else
						status_.remainingFrames -= elapsed;
				}
				frame_ = frame;
				generation_ = generation;
			}
			return { status_.mode, ConsumeChange() };
		}

	private:
		GuidedReconstructionOverrideStatus status_{};
		std::optional<std::uint32_t> frame_;
		std::optional<std::uint64_t> generation_;
		bool changed_ = false;
	};
#endif
}
