#include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"

#include <limits>
#include <string_view>

int main()
{
	using NeuralRendering::FeatureSlotRoute;
	using NeuralRendering::InsertionPoint;
	using NeuralRendering::PipelineArrangement;
	using NeuralRendering::SubmitSourceIdentityMatch;
	using NeuralRendering::SubmitStereoSourceProof;
	using NeuralRendering::SubmitStereoSourceProofKind;
	using NeuralRendering::TemporalAdmissionBlockReason;
	using NeuralRendering::TemporalAdmissionInputs;
	using NeuralRendering::TemporalRoute;
	static_assert(
		NeuralRendering::kPipelineArrangement == PipelineArrangement::DlssThenNeural,
		"paintball must remain the DLSS-then-Neural experiment");

	static_assert(NeuralRendering::RunsAfterDlss(PipelineArrangement::DlssThenNeural));
	static_assert(!NeuralRendering::RunsBeforeDlss(PipelineArrangement::DlssThenNeural));
	static_assert(!NeuralRendering::ReplacesDlss(PipelineArrangement::DlssThenNeural));
	static_assert(NeuralRendering::UsesFeatureUpscaling(PipelineArrangement::DlssThenNeural));
	static_assert(!NeuralRendering::RunsDlssAfterNeuralFailure(PipelineArrangement::DlssThenNeural));

	static_assert(NeuralRendering::RunsBeforeDlss(PipelineArrangement::NeuralThenDlss));
	static_assert(!NeuralRendering::RunsAfterDlss(PipelineArrangement::NeuralThenDlss));
	static_assert(!NeuralRendering::ReplacesDlss(PipelineArrangement::NeuralThenDlss));
	static_assert(!NeuralRendering::UsesFeatureUpscaling(PipelineArrangement::NeuralThenDlss));
	static_assert(!NeuralRendering::RunsDlssAfterNeuralFailure(PipelineArrangement::NeuralThenDlss));

	static_assert(!NeuralRendering::RunsBeforeDlss(PipelineArrangement::NeuralReplacesDlss));
	static_assert(!NeuralRendering::RunsAfterDlss(PipelineArrangement::NeuralReplacesDlss));
	static_assert(NeuralRendering::ReplacesDlss(PipelineArrangement::NeuralReplacesDlss));
	static_assert(NeuralRendering::UsesFeatureUpscaling(PipelineArrangement::NeuralReplacesDlss));
	static_assert(NeuralRendering::RunsDlssAfterNeuralFailure(PipelineArrangement::NeuralReplacesDlss));

	constexpr auto scaledFeature =
		NeuralRendering::ResolveFeatureUpscaling(756u, 840u, 1512u, 1680u);
	static_assert(scaledFeature && *scaledFeature);
	constexpr auto nativeFeature =
		NeuralRendering::ResolveFeatureUpscaling(1512u, 1680u, 1512u, 1680u);
	static_assert(nativeFeature && !*nativeFeature);
	constexpr auto preDlssFeature = NeuralRendering::ResolveFeatureUpscaling(
		756u, 840u, 1512u, 1680u, PipelineArrangement::NeuralThenDlss);
	static_assert(!preDlssFeature);
	constexpr auto nativePreDlssFeature = NeuralRendering::ResolveFeatureUpscaling(
		1512u, 1680u, 1512u, 1680u, PipelineArrangement::NeuralThenDlss);
	static_assert(nativePreDlssFeature && !*nativePreDlssFeature);
	constexpr auto horizontalScaleFeature =
		NeuralRendering::ResolveFeatureUpscaling(756u, 1680u, 1512u, 1680u);
	static_assert(horizontalScaleFeature && *horizontalScaleFeature);
	static_assert(!NeuralRendering::ResolveFeatureUpscaling(
		1512u, 1680u, 756u, 840u));
	static_assert(!NeuralRendering::ResolveFeatureUpscaling(
		1512u, 840u, 756u, 1680u));
	static_assert(!NeuralRendering::ResolveFeatureUpscaling(
		0u, 840u, 1512u, 1680u));

	static_assert(std::string_view(NeuralRendering::GetPipelineArrangementName(
					  PipelineArrangement::DlssThenNeural)) == "dlss_then_neural");
	static_assert(std::string_view(NeuralRendering::GetPipelineArrangementName(
					  PipelineArrangement::NeuralThenDlss)) == "neural_then_dlss");
	static_assert(std::string_view(NeuralRendering::GetPipelineArrangementName(
					  PipelineArrangement::NeuralReplacesDlss)) == "neural_replaces_dlss");

	static_assert(
		NeuralRendering::kDefaultInsertionPoint == InsertionPoint::UpscaledCenter);
	static_assert(NeuralRendering::kInsertionPointCount == 2u);
	static_assert(static_cast<std::uint32_t>(InsertionPoint::UpscaledCenter) == 0u);
	static_assert(static_cast<std::uint32_t>(InsertionPoint::FinalLdrPreUi) == 1u);
	static_assert(NeuralRendering::IsValidInsertionPoint(InsertionPoint::UpscaledCenter));
	static_assert(NeuralRendering::IsValidInsertionPoint(InsertionPoint::FinalLdrPreUi));
	static_assert(!NeuralRendering::IsValidInsertionPoint(InsertionPoint::Count));
	static_assert(NeuralRendering::ClampInsertionPoint(0u) == InsertionPoint::UpscaledCenter);
	static_assert(NeuralRendering::ClampInsertionPoint(1u) == InsertionPoint::FinalLdrPreUi);
	static_assert(NeuralRendering::ClampInsertionPoint(2u) == InsertionPoint::UpscaledCenter);
	static_assert(
		NeuralRendering::ClampInsertionPoint(std::numeric_limits<std::uint32_t>::max()) ==
		InsertionPoint::UpscaledCenter);
	static_assert(std::string_view(NeuralRendering::GetInsertionPointName(
					  InsertionPoint::UpscaledCenter)) == "upscaled_center");
	static_assert(std::string_view(NeuralRendering::GetInsertionPointName(
					  InsertionPoint::FinalLdrPreUi)) == "final_ldr_pre_ui");
	static_assert(std::string_view(NeuralRendering::GetInsertionPointName(
					  InsertionPoint::Count)) == "unknown");
	static_assert(std::string_view(NeuralRendering::GetInsertionPointDisplayName(
					  InsertionPoint::UpscaledCenter)) == "Upscaled Centre");
	static_assert(std::string_view(NeuralRendering::GetInsertionPointDisplayName(
					  InsertionPoint::FinalLdrPreUi)) == "Final LDR (Pre-UI)");
	static_assert(
		NeuralRendering::ParseInsertionPointName("upscaled_center") ==
		InsertionPoint::UpscaledCenter);
	static_assert(
		NeuralRendering::ParseInsertionPointName("final_ldr_pre_ui") ==
		InsertionPoint::FinalLdrPreUi);
	static_assert(!NeuralRendering::ParseInsertionPointName("unknown"));

	static_assert(std::string_view(NeuralRendering::GetImplementationName(
					  false, false)) == "per_eye_staged_commit");
	static_assert(std::string_view(NeuralRendering::GetImplementationName(
					  true, false)) == "stereo_batched_staged_commit");
	static_assert(std::string_view(NeuralRendering::GetImplementationName(
					  false, true)) == "per_eye_direct_commit");
	static_assert(std::string_view(NeuralRendering::GetImplementationName(
					  true, true)) == "stereo_batched_direct_commit");
	static_assert(std::string_view(NeuralRendering::GetStereoSubmissionName(false)) == "per_eye");
	static_assert(std::string_view(NeuralRendering::GetStereoSubmissionName(true)) == "batched");
	static_assert(std::string_view(NeuralRendering::GetOutputCommitName(false)) == "staged");
	static_assert(std::string_view(NeuralRendering::GetOutputCommitName(true)) == "direct");

	static_assert(NeuralRendering::ClassifyFeatureSlotMask(0b0001u) == FeatureSlotRoute::Main);
	static_assert(NeuralRendering::ClassifyFeatureSlotMask(0b0010u) == FeatureSlotRoute::Main);
	static_assert(NeuralRendering::ClassifyFeatureSlotMask(0b0011u) == FeatureSlotRoute::Main);
	static_assert(NeuralRendering::ClassifyFeatureSlotMask(0b0100u) == FeatureSlotRoute::Submit);
	static_assert(NeuralRendering::ClassifyFeatureSlotMask(0b1000u) == FeatureSlotRoute::Submit);
	static_assert(NeuralRendering::ClassifyFeatureSlotMask(0b1100u) == FeatureSlotRoute::Submit);
	static_assert(NeuralRendering::ClassifyFeatureSlotMask(0u) == FeatureSlotRoute::Unexpected);
	static_assert(NeuralRendering::ClassifyFeatureSlotMask(0b0101u) == FeatureSlotRoute::Unexpected);
	static_assert(NeuralRendering::ClassifyFeatureSlotMask(0b00110011u) == FeatureSlotRoute::Main);
	static_assert(NeuralRendering::ClassifyFeatureSlotMask(0b00010001u) == FeatureSlotRoute::Main);
	static_assert(NeuralRendering::ClassifyFeatureSlotMask(0b11001100u) == FeatureSlotRoute::Submit);
	static_assert(NeuralRendering::ClassifyFeatureSlotMask(0b01010001u) == FeatureSlotRoute::Unexpected);
	static_assert(NeuralRendering::ClassifyFeatureSlotMask(0x100u) == FeatureSlotRoute::Unexpected);
	static_assert(NeuralRendering::LogicalFeatureSlot(4u) == 0u);
	static_assert(NeuralRendering::LogicalFeatureSlot(7u) == 3u);
	static_assert(NeuralRendering::IsMatchingRegionStereoPair(0u, 42u, 1u, 42u));
	static_assert(NeuralRendering::IsMatchingRegionStereoPair(4u, 42u, 5u, 42u));
	static_assert(NeuralRendering::IsMatchingRegionStereoPair(0u, 42u, 5u, 42u));
	static_assert(!NeuralRendering::IsMatchingRegionStereoPair(0u, 42u, 1u, 43u));
	static_assert(!NeuralRendering::IsMatchingRegionStereoPair(0u, 42u, 4u, 42u));
	static_assert(!NeuralRendering::IsMatchingRegionStereoPair(0u, 42u, 3u, 42u));
	static_assert(!NeuralRendering::IsMatchingRegionStereoPair(8u, 42u, 1u, 42u));
	constexpr std::array<std::uint32_t, 4> splitRequirements{ 0x11u, 0x22u, 0u, 0u };
	static_assert(NeuralRendering::AggregateRegionEvaluationMask(0x1u, splitRequirements, false) == 0x1u);
	static_assert(NeuralRendering::AggregateRegionEvaluationMask(0x1u, splitRequirements, true) == 0u);
	static_assert(NeuralRendering::AggregateRegionEvaluationMask(0x13u, splitRequirements, false) == 0x3u);
	static_assert(NeuralRendering::AggregateRegionEvaluationMask(0x13u, splitRequirements, true) == 0x1u);
	static_assert(NeuralRendering::AggregateRegionEvaluationMask(0x33u, splitRequirements, true) == 0x3u);
	static_assert([] {
		std::uint32_t seen = 0;
		for (std::uint32_t logical = 0; logical < 4u; ++logical) {
			for (std::uint32_t region = 0; region < 2u; ++region) {
				const auto physical = NeuralRendering::PhysicalRegionFeatureSlot(logical, region);
				if (physical >= 8u || (seen & (1u << physical)) != 0u ||
					NeuralRendering::LogicalFeatureSlot(physical) != logical)
					return false;
				seen |= 1u << physical;
			}
		}
		return seen == 0xFFu &&
		       NeuralRendering::PhysicalRegionFeatureSlot(4u, 0u) == 8u &&
		       NeuralRendering::PhysicalRegionFeatureSlot(0u, 2u) == 8u;
	}());
	static_assert([] {
		using NeuralRendering::CharacterComputeRegionPlan;
		using NeuralRendering::ComputeSubrect;
		using NeuralRendering::GetCharacterRegionSubmissionViolation;
		constexpr ComputeSubrect support{ 16u, 16u, 128u, 64u };
		CharacterComputeRegionPlan plan;
		plan.regions = { ComputeSubrect{ 16u, 16u, 32u, 64u }, ComputeSubrect{ 112u, 16u, 32u, 64u } };
		plan.historyKeys = { 101u, 202u };
		plan.clusterIdentities = { 11u, 22u };
		plan.count = 2u;
		const auto valid = [&](const auto& candidate) {
			return GetCharacterRegionSubmissionViolation(0u, candidate, support, 256u, 128u, true).empty();
		};
		if (!valid(plan) || GetCharacterRegionSubmissionViolation(0u, plan, support, 256u, 128u, false).empty() ||
			GetCharacterRegionSubmissionViolation(4u, plan, support, 256u, 128u, true).empty() ||
			GetCharacterRegionSubmissionViolation(0u, plan, {}, 256u, 128u, true).empty())
			return false;
		auto invalid = plan;
		invalid.count = 3u;
		if (valid(invalid)) return false;
		invalid = plan;
		invalid.regions[1].baseX = 47u;  // One-pixel overlap.
		if (valid(invalid)) return false;
		invalid.regions[1].baseX = 48u;  // Touching exclusive bounds is valid.
		if (!valid(invalid)) return false;
		invalid = plan;
		invalid.regions[0].baseX = 15u;  // In texture, outside declared composite support.
		if (valid(invalid)) return false;
		invalid = plan;
		invalid.regions[1].width = 33u;
		if (valid(invalid)) return false;
		invalid = plan;
		invalid.regions[1].baseX = std::numeric_limits<std::uint32_t>::max();
		if (valid(invalid)) return false;
		invalid = plan;
		invalid.regions[1].width = 0u;
		if (valid(invalid)) return false;
		invalid = plan;
		invalid.historyKeys[1] = invalid.historyKeys[0];
		if (valid(invalid)) return false;
		invalid.historyKeys[1] = 0u;
		if (valid(invalid)) return false;
		invalid = plan;
		invalid.clusterIdentities[1] = 0u;
		if (valid(invalid)) return false;
		invalid.clusterIdentities[1] = invalid.clusterIdentities[0];
		if (valid(invalid)) return false;
		plan.count = 1u;
		if (!valid(plan)) return false;
		// An absent plan keeps the legacy validator/path in charge, without new restrictions.
		return GetCharacterRegionSubmissionViolation(0u, {}, {}, 0u, 0u, false).empty();
	}());
	static_assert(NeuralRendering::IsOrderedStereoFeatureSlotPair(0u, 1u));
	static_assert(NeuralRendering::IsOrderedStereoFeatureSlotPair(2u, 3u));
	static_assert(!NeuralRendering::IsOrderedStereoFeatureSlotPair(1u, 0u));
	static_assert(!NeuralRendering::IsOrderedStereoFeatureSlotPair(0u, 2u));
	static_assert(NeuralRendering::IsCompleteNeuralStereoResult(0b11u, 0u));
	static_assert(NeuralRendering::IsCompleteNeuralStereoResult(0b01u, 0b10u));
	static_assert(NeuralRendering::IsCompleteNeuralStereoResult(0b10u, 0b01u));
	static_assert(!NeuralRendering::IsCompleteNeuralStereoResult(0u, 0b11u));
	static_assert(!NeuralRendering::IsCompleteNeuralStereoResult(0b01u, 0u));
	static_assert(!NeuralRendering::IsCompleteNeuralStereoResult(0b01u, 0b01u));
	static_assert(!NeuralRendering::IsCompleteNeuralStereoResult(0b100u, 0b11u));
	static_assert(NeuralRendering::IsSequentialFrame(42u, 43u));
	static_assert(!NeuralRendering::IsSequentialFrame(42u, 42u));
	static_assert(!NeuralRendering::IsSequentialFrame(42u, 44u));

	static_assert(
		NeuralRendering::ResolveSubmitSourceIdentityMatch(0x1000u, 0x1000u, 0x2000u) ==
		SubmitSourceIdentityMatch::OpenVRTexture);
	static_assert(
		NeuralRendering::ResolveSubmitSourceIdentityMatch(0x2000u, 0x1000u, 0x2000u) ==
		SubmitSourceIdentityMatch::DirectXHandle);
	static_assert(
		NeuralRendering::ResolveSubmitSourceIdentityMatch(0u, 0x1000u, 0x2000u) ==
		SubmitSourceIdentityMatch::None);
	static_assert(
		NeuralRendering::ResolveSubmitSourceIdentityMatch(0x3000u, 0x1000u, 0x2000u) ==
		SubmitSourceIdentityMatch::None);

	constexpr auto loggedCombinedSourceProof =
		NeuralRendering::ResolveSubmitStereoSourceProof(
			1923u, 0u, 0u, true, 1u, true);
	static_assert(loggedCombinedSourceProof.IsValid());
	static_assert(
		loggedCombinedSourceProof.kind ==
		SubmitStereoSourceProofKind::CombinedTextureCycle);
	static_assert(loggedCombinedSourceProof.value == 1923u);
	constexpr auto combinedSourceWithOuterProof =
		NeuralRendering::ResolveSubmitStereoSourceProof(
			1923u, 7u, 7u, true, 1u, true);
	static_assert(
		combinedSourceWithOuterProof.kind ==
		SubmitStereoSourceProofKind::CombinedTextureCycle);
	static_assert(combinedSourceWithOuterProof.value == 1923u);
	constexpr auto combinedSourceWithRejectedOuterProof =
		NeuralRendering::ResolveSubmitStereoSourceProof(
			1923u, 7u, 0u, true, 1u, true);
	static_assert(
		combinedSourceWithRejectedOuterProof.kind ==
		SubmitStereoSourceProofKind::CombinedTextureCycle);
	constexpr auto arraySourceOuterProof =
		NeuralRendering::ResolveSubmitStereoSourceProof(
			1923u, 7u, 7u, false, 2u, true);
	static_assert(
		arraySourceOuterProof.kind ==
		SubmitStereoSourceProofKind::OuterBoundary);
	static_assert(arraySourceOuterProof.value == 7u);
	static_assert(!NeuralRendering::ResolveSubmitStereoSourceProof(
		0u, 0u, 0u, true, 1u, true)
			.IsValid());
	static_assert(!NeuralRendering::ResolveSubmitStereoSourceProof(
		1923u, 0u, 0u, true, 1u, false)
			.IsValid());
	static_assert(!NeuralRendering::ResolveSubmitStereoSourceProof(
		1923u, 0u, 0u, false, 1u, true)
			.IsValid());
	static_assert(!NeuralRendering::ResolveSubmitStereoSourceProof(
		1923u, 7u, 7u, false, 1u, true)
			.IsValid());
	static_assert(!NeuralRendering::ResolveSubmitStereoSourceProof(
		1923u, 0u, 0u, false, 2u, true)
			.IsValid());
	static_assert(!NeuralRendering::ResolveSubmitStereoSourceProof(
		1923u, 7u, 0u, false, 2u, true)
			.IsValid());
	static_assert(!NeuralRendering::ResolveSubmitStereoSourceProof(
		1923u, 7u, 8u, false, 2u, true)
			.IsValid());
	constexpr SubmitStereoSourceProof outerDomain{
		.kind = SubmitStereoSourceProofKind::OuterBoundary,
		.value = 41u,
	};
	constexpr SubmitStereoSourceProof combinedDomain{
		.kind = SubmitStereoSourceProofKind::CombinedTextureCycle,
		.value = 41u,
	};
	static_assert(!NeuralRendering::MatchesSubmitStereoSourceProof(
		outerDomain, combinedDomain));
	static_assert(NeuralRendering::MatchesSubmitStereoSourceProof(
		combinedDomain, combinedDomain));
	static_assert(!NeuralRendering::MatchesSubmitStereoSourceProof(
		SubmitStereoSourceProof{}, SubmitStereoSourceProof{}));
	static_assert(!NeuralRendering::MatchesSubmitStereoSourceProof(
		combinedDomain,
		NeuralRendering::ResolveSubmitStereoSourceProof(
			42u, 0u, 0u, true, 1u, true)));
	static_assert(std::string_view(
					  NeuralRendering::GetSubmitStereoSourceProofName(
						  SubmitStereoSourceProofKind::CombinedTextureCycle)) ==
				  "combined_texture_cycle");

	constexpr TemporalAdmissionInputs currentWorldFrame{
		.worldFrameStateAvailable = true,
		.currentFrame = 42u,
		.lastWorldRenderFrame = 42u,
		.lastCompletedWorldRenderFrame = 41u,
	};
	constexpr auto mainAdmission = NeuralRendering::EvaluateTemporalAdmission(
		TemporalRoute::Main, currentWorldFrame);
	static_assert(mainAdmission.admitted);
	static_assert(mainAdmission.worldFrameStarted);
	static_assert(!mainAdmission.worldFrameCompleted);
	static_assert(mainAdmission.temporalSourceFresh);

	constexpr auto incompleteSubmitAdmission =
		NeuralRendering::EvaluateTemporalAdmission(
			TemporalRoute::Submit, currentWorldFrame);
	static_assert(!incompleteSubmitAdmission.admitted);
	static_assert(
		incompleteSubmitAdmission.blockReason ==
		TemporalAdmissionBlockReason::TemporalSourceStale);
	constexpr auto completeSubmitAdmission =
		NeuralRendering::EvaluateTemporalAdmission(
			TemporalRoute::Submit,
			TemporalAdmissionInputs{
				.worldFrameStateAvailable = true,
				.currentFrame = 42u,
				.lastWorldRenderFrame = 42u,
				.lastCompletedWorldRenderFrame = 42u,
			});
	static_assert(completeSubmitAdmission.admitted);
	static_assert(completeSubmitAdmission.worldFrameStarted);
	static_assert(completeSubmitAdmission.worldFrameCompleted);
	static_assert(completeSubmitAdmission.temporalSourceFresh);
	static_assert(
		NeuralRendering::GetTemporalSourceFrame(completeSubmitAdmission) == 42u);
	constexpr auto unavailableFrameState =
		NeuralRendering::EvaluateTemporalAdmission(
			TemporalRoute::Main,
			TemporalAdmissionInputs{
				.currentFrame = std::numeric_limits<std::uint32_t>::max(),
				.lastWorldRenderFrame = std::numeric_limits<std::uint32_t>::max(),
				.lastCompletedWorldRenderFrame = std::numeric_limits<std::uint32_t>::max(),
			});
	static_assert(!unavailableFrameState.admitted);
	static_assert(!unavailableFrameState.worldFrameStarted);
	static_assert(
		NeuralRendering::GetTemporalSourceFrame(unavailableFrameState) ==
		std::numeric_limits<std::uint32_t>::max());

	constexpr auto freshPausedSubmitAdmission =
		NeuralRendering::EvaluateTemporalAdmission(
			TemporalRoute::Submit,
			TemporalAdmissionInputs{
				.gamePaused = true,
				.pausedContinuityAllowed = true,
				.worldFrameStateAvailable = true,
				.currentFrame = 42u,
				.lastWorldRenderFrame = 42u,
				.lastCompletedWorldRenderFrame = 42u,
			});
	static_assert(freshPausedSubmitAdmission.admitted);
	static_assert(freshPausedSubmitAdmission.temporalSourceFresh);
	static_assert(
		freshPausedSubmitAdmission.blockReason ==
		TemporalAdmissionBlockReason::None);

	constexpr auto pausedSubmitWithoutContinuity =
		NeuralRendering::EvaluateTemporalAdmission(
			TemporalRoute::Submit,
			TemporalAdmissionInputs{
				.gamePaused = true,
				.worldFrameStateAvailable = true,
				.currentFrame = 42u,
				.lastWorldRenderFrame = 42u,
				.lastCompletedWorldRenderFrame = 42u,
			});
	static_assert(!pausedSubmitWithoutContinuity.admitted);
	static_assert(
		pausedSubmitWithoutContinuity.blockReason ==
		TemporalAdmissionBlockReason::GamePaused);

	constexpr auto stalePausedSubmitAdmission =
		NeuralRendering::EvaluateTemporalAdmission(
			TemporalRoute::Submit,
			TemporalAdmissionInputs{
				.gamePaused = true,
				.pausedContinuityAllowed = true,
				.worldFrameStateAvailable = true,
				.currentFrame = 42u,
				.lastWorldRenderFrame = 42u,
				.lastCompletedWorldRenderFrame = 41u,
			});
	static_assert(!stalePausedSubmitAdmission.admitted);
	static_assert(!stalePausedSubmitAdmission.temporalSourceFresh);
	static_assert(
		stalePausedSubmitAdmission.blockReason ==
		TemporalAdmissionBlockReason::TemporalSourceStale);
	static_assert(
		NeuralRendering::GetTemporalSourceFrame(stalePausedSubmitAdmission) ==
		std::numeric_limits<std::uint32_t>::max());

	constexpr auto staleUnpausedSubmitAdmission =
		NeuralRendering::EvaluateTemporalAdmission(
			TemporalRoute::Submit,
			TemporalAdmissionInputs{
				.worldFrameStateAvailable = true,
				.currentFrame = 42u,
				.lastWorldRenderFrame = 41u,
				.lastCompletedWorldRenderFrame = 41u,
			});
	static_assert(!staleUnpausedSubmitAdmission.admitted);
	static_assert(!staleUnpausedSubmitAdmission.retainedWorldFrame);

	constexpr auto retainedPausedSubmitAdmission =
		NeuralRendering::EvaluateTemporalAdmission(
			TemporalRoute::Submit,
			TemporalAdmissionInputs{
				.gamePaused = true,
				.pausedContinuityAllowed = true,
				.worldFrameStateAvailable = true,
				.currentFrame = 42u,
				.lastWorldRenderFrame = 41u,
				.lastCompletedWorldRenderFrame = 41u,
			});
	static_assert(retainedPausedSubmitAdmission.admitted);
	static_assert(retainedPausedSubmitAdmission.retainedWorldFrame);
	static_assert(retainedPausedSubmitAdmission.temporalSourceFresh);
	static_assert(
		NeuralRendering::GetTemporalSourceFrame(
			retainedPausedSubmitAdmission) == 41u);

	constexpr auto pausedMainAdmission = NeuralRendering::EvaluateTemporalAdmission(
		TemporalRoute::Main,
		TemporalAdmissionInputs{
			.gamePaused = true,
			.pausedContinuityAllowed = true,
			.worldFrameStateAvailable = true,
			.currentFrame = 42u,
			.lastWorldRenderFrame = 42u,
			.lastCompletedWorldRenderFrame = 42u,
		});
	static_assert(pausedMainAdmission.admitted);
	static_assert(
		NeuralRendering::GetTemporalSourceFrame(pausedMainAdmission) == 42u);
	static_assert(
		pausedMainAdmission.blockReason ==
		TemporalAdmissionBlockReason::None);

	constexpr auto retainedPausedMainAdmission =
		NeuralRendering::EvaluateTemporalAdmission(
			TemporalRoute::Main,
			TemporalAdmissionInputs{
				.gamePaused = true,
				.pausedContinuityAllowed = true,
				.worldFrameStateAvailable = true,
				.currentFrame = 42u,
				.lastWorldRenderFrame = 41u,
				.lastCompletedWorldRenderFrame = 41u,
			});
	static_assert(retainedPausedMainAdmission.admitted);
	static_assert(retainedPausedMainAdmission.retainedWorldFrame);
	static_assert(
		NeuralRendering::GetTemporalSourceFrame(retainedPausedMainAdmission) == 41u);

	constexpr auto hardMenuAdmission = NeuralRendering::EvaluateTemporalAdmission(
		TemporalRoute::Submit,
		TemporalAdmissionInputs{
			.menuContextActive = true,
			.gamePaused = true,
			.pausedContinuityAllowed = true,
			.worldFrameStateAvailable = true,
			.currentFrame = 42u,
			.lastWorldRenderFrame = 42u,
			.lastCompletedWorldRenderFrame = 42u,
		});
	static_assert(!hardMenuAdmission.admitted);
	static_assert(
		hardMenuAdmission.blockReason ==
		TemporalAdmissionBlockReason::MenuContext);
	static_assert(
		NeuralRendering::GetTemporalSourceFrame(hardMenuAdmission) ==
		std::numeric_limits<std::uint32_t>::max());
	static_assert(std::string_view(
					  NeuralRendering::GetTemporalAdmissionBlockReasonName(
						  TemporalAdmissionBlockReason::TemporalSourceStale)) ==
				  "temporal_source_stale");

	// A tracking tail is deliberately not a UI requirement. Exercise the whole
	// admission sequence, including nested menus and retained paused-world input,
	// so closing the final menu admits the very next completed world frame.
	constexpr auto gameplayMenuLifecycle = []() {
		struct MenuFrame
		{
			bool menuLayerRequired;
			bool isolatedLayerReady;
			bool paused;
			std::uint32_t frame;
			std::uint32_t worldFrame;
		};
		constexpr MenuFrame frames[] = {
			{ false, false, false, 40u, 40u },  // Gameplay.
			{ true, true, false, 41u, 41u },    // Dialogue over a live world.
			{ true, true, true, 42u, 41u },     // Console nested over dialogue.
			{ true, true, false, 43u, 43u },    // Console closes; dialogue remains.
			{ false, false, false, 44u, 44u },  // Final menu closes; tail only.
			{ true, true, false, 45u, 45u },    // A captured closing UI animation.
			{ false, false, false, 46u, 46u },  // Tail remains without UI work.
			{ true, true, true, 47u, 46u },     // Wait/save/activation modal.
			{ false, false, true, 48u, 46u },   // Menu closes before pause clears.
			{ false, false, false, 49u, 49u },  // World rendering resumes.
		};
		for (const auto& frame : frames) {
			const bool continuityAllowed =
				NeuralRendering::ResolveMenuContinuityAllowed(
					false, frame.menuLayerRequired, frame.isolatedLayerReady);
			const auto admission = NeuralRendering::EvaluateTemporalAdmission(
				TemporalRoute::Submit,
				TemporalAdmissionInputs{
					.gamePaused = frame.paused,
					.pausedContinuityAllowed = continuityAllowed,
					.worldFrameStateAvailable = true,
					.currentFrame = frame.frame,
					.lastWorldRenderFrame = frame.worldFrame,
					.lastCompletedWorldRenderFrame = frame.worldFrame,
				});
			if (!continuityAllowed || !admission.admitted ||
				admission.sourceWorldFrame != frame.worldFrame ||
				admission.retainedWorldFrame != (frame.frame != frame.worldFrame)) {
				return false;
			}
		}
		return true;
	};
	static_assert(gameplayMenuLifecycle());

	// Open or nested menus and observed closing UI work still require a complete
	// isolated layer, even if the world itself is fresh and the game is unpaused.
	static_assert(!NeuralRendering::ResolveMenuContinuityAllowed(false, true, false));
	static_assert(!NeuralRendering::ResolveMenuContinuityAllowed(true, true, true));
	static_assert(!NeuralRendering::ResolveMenuContinuityAllowed(true, false, false));
	constexpr bool closedMenuContinuity =
		NeuralRendering::ResolveMenuContinuityAllowed(false, false, false);
	constexpr auto closedMenuIncompleteWorld =
		NeuralRendering::EvaluateTemporalAdmission(
			TemporalRoute::Submit,
			TemporalAdmissionInputs{
				.gamePaused = true,
				.pausedContinuityAllowed = closedMenuContinuity,
				.worldFrameStateAvailable = true,
				.currentFrame = 50u,
				.lastWorldRenderFrame = 50u,
				.lastCompletedWorldRenderFrame = 49u,
			});
	static_assert(!closedMenuIncompleteWorld.admitted);
	static_assert(!closedMenuIncompleteWorld.retainedWorldFrame);
	constexpr auto closedMenuStaleWorld =
		NeuralRendering::EvaluateTemporalAdmission(
			TemporalRoute::Submit,
			TemporalAdmissionInputs{
				.pausedContinuityAllowed = closedMenuContinuity,
				.worldFrameStateAvailable = true,
				.currentFrame = 50u,
				.lastWorldRenderFrame = 49u,
				.lastCompletedWorldRenderFrame = 49u,
			});
	static_assert(!closedMenuStaleWorld.admitted);
	static_assert(!closedMenuStaleWorld.retainedWorldFrame);

	using NeuralRendering::CachedStereoPairReuse;
	static_assert(
		NeuralRendering::ResolveCachedStereoPairReuse(true, true, 0u, 0u) ==
		CachedStereoPairReuse::Reuse);
	static_assert(
		NeuralRendering::ResolveCachedStereoPairReuse(true, true, 0b01u, 0u) ==
		CachedStereoPairReuse::Reuse);
	static_assert(
		NeuralRendering::ResolveCachedStereoPairReuse(false, true, 0b01u, 1u) ==
		CachedStereoPairReuse::CompleteLatchedPair);
	static_assert(
		NeuralRendering::ResolveCachedStereoPairReuse(false, true, 0b10u, 0u) ==
		CachedStereoPairReuse::CompleteLatchedPair);
	static_assert(
		NeuralRendering::ResolveCachedStereoPairReuse(false, false, 0b01u, 1u) ==
		CachedStereoPairReuse::Reject);
	static_assert(
		NeuralRendering::ResolveCachedStereoPairReuse(true, false, 0u, 0u) ==
		CachedStereoPairReuse::Reject);
	static_assert(
		NeuralRendering::ResolveCachedStereoPairReuse(false, true, 0u, 0u) ==
		CachedStereoPairReuse::Reject);
	static_assert(
		NeuralRendering::ResolveCachedStereoPairReuse(false, true, 0b01u, 0u) ==
		CachedStereoPairReuse::BypassPresentedEye);
	static_assert(
		NeuralRendering::ResolveCachedStereoPairReuse(false, true, 0b10u, 1u) ==
		CachedStereoPairReuse::BypassPresentedEye);
	static_assert(
		NeuralRendering::ResolveCachedStereoPairReuse(false, true, 0b11u, 0u) ==
		CachedStereoPairReuse::BypassPresentedEye);

	constexpr auto perEyeStaged =
		NeuralRendering::ParseImplementationName("per_eye_staged_commit");
	constexpr auto batchedStaged =
		NeuralRendering::ParseImplementationName("stereo_batched_staged_commit");
	constexpr auto perEyeDirect =
		NeuralRendering::ParseImplementationName("per_eye_direct_commit");
	constexpr auto batchedDirect =
		NeuralRendering::ParseImplementationName("stereo_batched_direct_commit");
	static_assert(perEyeStaged && !perEyeStaged->batchedStereo && !perEyeStaged->directCommit);
	static_assert(batchedStaged && batchedStaged->batchedStereo && !batchedStaged->directCommit);
	static_assert(perEyeDirect && !perEyeDirect->batchedStereo && perEyeDirect->directCommit);
	static_assert(batchedDirect && batchedDirect->batchedStereo && batchedDirect->directCommit);
	static_assert(!NeuralRendering::ParseImplementationName("unknown"));
	return 0;
}
