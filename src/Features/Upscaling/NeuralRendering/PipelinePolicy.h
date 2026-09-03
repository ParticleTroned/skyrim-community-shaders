#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace NeuralRendering
{
	struct PipelineImplementation
	{
		bool batchedStereo = false;
		bool directCommit = false;
	};

	inline constexpr std::array<PipelineImplementation, 4>
		kPipelineImplementations{
			PipelineImplementation{ false, false },
			PipelineImplementation{ true, false },
			PipelineImplementation{ false, true },
			PipelineImplementation{ true, true },
		};

	/** Experimental placement of NGX Feature 18 relative to normal DLSS. */
	enum class PipelineArrangement : std::uint32_t
	{
		DlssThenNeural = 0,
		NeuralThenDlss = 1,
		NeuralReplacesDlss = 2,
	};

	/** Image-space boundary at which Feature 18 consumes the DLSS result. */
	enum class InsertionPoint : std::uint32_t
	{
		UpscaledCenter = 0,
		FinalLdrPreUi = 1,
		Count,
	};

	inline constexpr InsertionPoint kDefaultInsertionPoint =
		InsertionPoint::UpscaledCenter;
	inline constexpr std::size_t kInsertionPointCount =
		static_cast<std::size_t>(InsertionPoint::Count);

	[[nodiscard]] constexpr bool IsValidInsertionPoint(
		InsertionPoint a_insertionPoint) noexcept
	{
		return a_insertionPoint >= InsertionPoint::UpscaledCenter &&
		       a_insertionPoint < InsertionPoint::Count;
	}

	/** Maps persisted numeric settings to a supported insertion point. */
	[[nodiscard]] constexpr InsertionPoint ClampInsertionPoint(
		std::uint32_t a_value) noexcept
	{
		const auto insertionPoint = static_cast<InsertionPoint>(a_value);
		return IsValidInsertionPoint(insertionPoint) ?
		           insertionPoint :
		           kDefaultInsertionPoint;
	}

	/** Stable settings and diagnostics identifier for an insertion point. */
	[[nodiscard]] constexpr const char* GetInsertionPointName(
		InsertionPoint a_insertionPoint = kDefaultInsertionPoint) noexcept
	{
		switch (a_insertionPoint) {
		case InsertionPoint::UpscaledCenter:
			return "upscaled_center";
		case InsertionPoint::FinalLdrPreUi:
			return "final_ldr_pre_ui";
		default:
			return "unknown";
		}
	}

	/** Concise user-facing label for an insertion point. */
	[[nodiscard]] constexpr const char* GetInsertionPointDisplayName(
		InsertionPoint a_insertionPoint = kDefaultInsertionPoint) noexcept
	{
		switch (a_insertionPoint) {
		case InsertionPoint::UpscaledCenter:
			return "Upscaled Centre";
		case InsertionPoint::FinalLdrPreUi:
			return "Final LDR (Pre-UI)";
		default:
			return "Unknown";
		}
	}

	/** Resolves a stable settings identifier without accepting aliases. */
	[[nodiscard]] constexpr std::optional<InsertionPoint>
	ParseInsertionPointName(std::string_view a_name) noexcept
	{
		if (a_name == GetInsertionPointName(InsertionPoint::UpscaledCenter))
			return InsertionPoint::UpscaledCenter;
		if (a_name == GetInsertionPointName(InsertionPoint::FinalLdrPreUi))
			return InsertionPoint::FinalLdrPreUi;
		return std::nullopt;
	}

	enum class FeatureSlotRoute : std::uint8_t
	{
		Unexpected,
		Main,
		Submit,
	};

	enum class CachedStereoPairReuse : std::uint8_t
	{
		Reject,
		Reuse,
		CompleteLatchedPair,
		BypassPresentedEye,
	};

	enum class SubmitSourceIdentityMatch : std::uint8_t
	{
		None,
		OpenVRTexture,
		DirectXHandle,
	};

	enum class SubmitStereoSourceProofKind : std::uint8_t
	{
		None,
		OuterBoundary,
		CombinedTextureCycle,
	};

	struct SubmitStereoSourceProof
	{
		SubmitStereoSourceProofKind kind = SubmitStereoSourceProofKind::None;
		std::uint64_t value = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return kind != SubmitStereoSourceProofKind::None && value != 0;
		}
	};

	/**
	 * Selects an exact stereo-source correlation proof for one submit cycle.
	 * A canonical side-by-side resource contains both eye inputs and therefore
	 * does not depend on the engine wrapper reaching its outer virtual hook.
	 */
	[[nodiscard]] constexpr SubmitStereoSourceProof ResolveSubmitStereoSourceProof(
		std::uint64_t a_compositorCycle,
		std::uint64_t a_expectedOuterBoundary,
		std::uint64_t a_matchedOuterBoundary,
		bool a_usesCombinedStereoLayout,
		std::uint32_t a_arraySize,
		bool a_sourceSignatureProven) noexcept
	{
		if (a_compositorCycle == 0 || !a_sourceSignatureProven)
			return {};

		if (a_usesCombinedStereoLayout && a_arraySize == 1u) {
			return {
				.kind = SubmitStereoSourceProofKind::CombinedTextureCycle,
				.value = a_compositorCycle,
			};
		}

		const bool sourceContainsBothEyes =
			a_usesCombinedStereoLayout || a_arraySize > 1u;
		if (sourceContainsBothEyes &&
			a_expectedOuterBoundary != 0 &&
			a_expectedOuterBoundary == a_matchedOuterBoundary) {
			return {
				.kind = SubmitStereoSourceProofKind::OuterBoundary,
				.value = a_matchedOuterBoundary,
			};
		}

		return {};
	}

	/** Prevents equal numeric values from matching across proof domains. */
	[[nodiscard]] constexpr bool MatchesSubmitStereoSourceProof(
		const SubmitStereoSourceProof& a_latched,
		const SubmitStereoSourceProof& a_current) noexcept
	{
		return a_latched.IsValid() &&
		       a_latched.kind == a_current.kind &&
		       a_latched.value == a_current.value;
	}

	[[nodiscard]] constexpr const char* GetSubmitStereoSourceProofName(
		SubmitStereoSourceProofKind a_kind) noexcept
	{
		switch (a_kind) {
		case SubmitStereoSourceProofKind::OuterBoundary:
			return "outer_boundary";
		case SubmitStereoSourceProofKind::CombinedTextureCycle:
			return "combined_texture_cycle";
		default:
			return "none";
		}
	}

	/** Matches an opaque outer submit source to validated nested representations. */
	[[nodiscard]] constexpr SubmitSourceIdentityMatch ResolveSubmitSourceIdentityMatch(
		std::uintptr_t a_outerIdentity,
		std::uintptr_t a_openVRTextureIdentity,
		std::uintptr_t a_directXHandleIdentity) noexcept
	{
		if (a_outerIdentity == 0)
			return SubmitSourceIdentityMatch::None;
		if (a_outerIdentity == a_openVRTextureIdentity && a_openVRTextureIdentity != 0)
			return SubmitSourceIdentityMatch::OpenVRTexture;
		if (a_outerIdentity == a_directXHandleIdentity && a_directXHandleIdentity != 0)
			return SubmitSourceIdentityMatch::DirectXHandle;
		return SubmitSourceIdentityMatch::None;
	}

	enum class TemporalRoute : std::uint8_t
	{
		Main,
		Submit,
	};

	enum class TemporalAdmissionBlockReason : std::uint8_t
	{
		None,
		MenuContext,
		GamePaused,
		TemporalSourceStale,
	};

	struct TemporalAdmissionInputs
	{
		bool menuContextActive = false;
		bool gamePaused = false;
		bool pausedContinuityAllowed = false;
		bool worldFrameStateAvailable = false;
		std::uint32_t currentFrame = 0;
		std::uint32_t lastWorldRenderFrame = 0;
		std::uint32_t lastCompletedWorldRenderFrame = 0;
	};

	struct TemporalAdmissionResult
	{
		TemporalRoute route = TemporalRoute::Main;
		TemporalAdmissionBlockReason blockReason =
			TemporalAdmissionBlockReason::TemporalSourceStale;
		bool admitted = false;
		bool menuContextActive = false;
		bool gamePaused = false;
		bool pausedContinuityAllowed = false;
		bool worldFrameStateAvailable = false;
		bool worldFrameStarted = false;
		bool worldFrameCompleted = false;
		bool retainedWorldFrame = false;
		bool temporalSourceFresh = false;
		std::uint32_t currentFrame = 0;
		std::uint32_t lastWorldRenderFrame = 0;
		std::uint32_t lastCompletedWorldRenderFrame = 0;
	};

	/** Resolves one immutable temporal admission decision for a stereo route. */
	[[nodiscard]] constexpr TemporalAdmissionResult EvaluateTemporalAdmission(
		TemporalRoute a_route,
		const TemporalAdmissionInputs& a_inputs) noexcept
	{
		TemporalAdmissionResult result{
			.route = a_route,
			.menuContextActive = a_inputs.menuContextActive,
			.gamePaused = a_inputs.gamePaused,
			.pausedContinuityAllowed =
				a_inputs.pausedContinuityAllowed,
			.worldFrameStateAvailable = a_inputs.worldFrameStateAvailable,
			.worldFrameStarted =
				a_inputs.worldFrameStateAvailable &&
				a_inputs.lastWorldRenderFrame == a_inputs.currentFrame,
			.worldFrameCompleted =
				a_inputs.worldFrameStateAvailable &&
				a_inputs.lastCompletedWorldRenderFrame == a_inputs.currentFrame,
			.currentFrame = a_inputs.currentFrame,
			.lastWorldRenderFrame = a_inputs.lastWorldRenderFrame,
			.lastCompletedWorldRenderFrame =
				a_inputs.lastCompletedWorldRenderFrame,
		};
		const bool currentRouteSourceFresh = result.worldFrameStarted &&
		                                     (a_route == TemporalRoute::Main || result.worldFrameCompleted);
		const bool completedWorldFrameAvailable =
			result.worldFrameStateAvailable &&
			result.lastCompletedWorldRenderFrame != 0 &&
			result.lastCompletedWorldRenderFrame !=
				std::numeric_limits<std::uint32_t>::max() &&
			result.lastWorldRenderFrame == result.lastCompletedWorldRenderFrame &&
			result.lastCompletedWorldRenderFrame <= result.currentFrame;
		result.retainedWorldFrame =
			result.gamePaused && result.pausedContinuityAllowed &&
			!currentRouteSourceFresh && completedWorldFrameAvailable;
		result.temporalSourceFresh =
			currentRouteSourceFresh || result.retainedWorldFrame;

		if (result.menuContextActive) {
			result.blockReason = TemporalAdmissionBlockReason::MenuContext;
		} else if (result.gamePaused && !result.pausedContinuityAllowed) {
			result.blockReason = TemporalAdmissionBlockReason::GamePaused;
		} else if (!result.temporalSourceFresh) {
			result.blockReason =
				TemporalAdmissionBlockReason::TemporalSourceStale;
		} else {
			result.blockReason = TemporalAdmissionBlockReason::None;
			result.admitted = true;
		}
		return result;
	}

	[[nodiscard]] constexpr const char* GetTemporalAdmissionBlockReasonName(
		TemporalAdmissionBlockReason a_reason) noexcept
	{
		switch (a_reason) {
		case TemporalAdmissionBlockReason::None:
			return "none";
		case TemporalAdmissionBlockReason::MenuContext:
			return "menu_context";
		case TemporalAdmissionBlockReason::GamePaused:
			return "game_paused";
		case TemporalAdmissionBlockReason::TemporalSourceStale:
			return "temporal_source_stale";
		default:
			return "unknown";
		}
	}

	/** Temporal histories are reusable only across adjacent rendered frames. */
	[[nodiscard]] constexpr bool IsSequentialFrame(
		std::uint32_t a_previousFrame,
		std::uint32_t a_currentFrame) noexcept
	{
		return a_currentFrame == a_previousFrame + 1u;
	}

	/**
	 * Resolves whether a retained stereo pair may be presented for one eye.
	 * A changed context may finish the unpresented peer of an already-started
	 * same-frame pair. A repeated accepted eye is bypassed without poisoning the
	 * missing peer. A retained pair cannot start a mixed-context or later pair.
	 */
	[[nodiscard]] constexpr CachedStereoPairReuse ResolveCachedStereoPairReuse(
		bool a_contextMatches,
		bool a_sameFrame,
		std::uint32_t a_presentedEyeMask,
		std::uint32_t a_eyeIndex) noexcept
	{
		if (a_eyeIndex > 1u || !a_sameFrame)
			return CachedStereoPairReuse::Reject;

		const std::uint32_t currentEyeBit = 1u << a_eyeIndex;
		if (a_contextMatches)
			return CachedStereoPairReuse::Reuse;
		if ((a_presentedEyeMask & currentEyeBit) != 0)
			return CachedStereoPairReuse::BypassPresentedEye;

		const std::uint32_t peerEyeBit = 1u << (a_eyeIndex ^ 1u);
		if (a_presentedEyeMask == peerEyeBit)
			return CachedStereoPairReuse::CompleteLatchedPair;
		return CachedStereoPairReuse::Reject;
	}

	/** Attributes a valid per-eye or stereo Feature 18 slot mask to its route. */
	[[nodiscard]] constexpr FeatureSlotRoute ClassifyFeatureSlotMask(
		std::uint32_t a_slotMask) noexcept
	{
		constexpr std::uint32_t mainMask = 0b0011u;
		constexpr std::uint32_t submitMask = 0b1100u;
		if (a_slotMask != 0 && (a_slotMask & ~mainMask) == 0)
			return FeatureSlotRoute::Main;
		if (a_slotMask != 0 && (a_slotMask & ~submitMask) == 0)
			return FeatureSlotRoute::Submit;
		return FeatureSlotRoute::Unexpected;
	}

	/** Validates the ordered left/right slot pair for one stereo route. */
	[[nodiscard]] constexpr bool IsOrderedStereoFeatureSlotPair(
		std::uint32_t a_leftSlot,
		std::uint32_t a_rightSlot) noexcept
	{
		return (a_leftSlot == 0u && a_rightSlot == 1u) ||
		       (a_leftSlot == 2u && a_rightSlot == 3u);
	}

	static_assert(IsSequentialFrame(10u, 11u));
	static_assert(IsSequentialFrame(std::numeric_limits<std::uint32_t>::max(), 0u));
	static_assert(!IsSequentialFrame(10u, 10u));
	static_assert(!IsSequentialFrame(10u, 12u));

	// Each experiment keeps its ordering fixed so saved settings cannot silently
	// turn one validation branch into another.
	inline constexpr PipelineArrangement kPipelineArrangement =
		PipelineArrangement::DlssThenNeural;

	[[nodiscard]] constexpr const char* GetPipelineArrangementName(
		PipelineArrangement a_arrangement = kPipelineArrangement) noexcept
	{
		switch (a_arrangement) {
		case PipelineArrangement::DlssThenNeural:
			return "dlss_then_neural";
		case PipelineArrangement::NeuralThenDlss:
			return "neural_then_dlss";
		case PipelineArrangement::NeuralReplacesDlss:
			return "neural_replaces_dlss";
		default:
			return "unknown";
		}
	}

	[[nodiscard]] constexpr bool RunsBeforeDlss(
		PipelineArrangement a_arrangement = kPipelineArrangement) noexcept
	{
		return a_arrangement == PipelineArrangement::NeuralThenDlss;
	}

	[[nodiscard]] constexpr bool RunsAfterDlss(
		PipelineArrangement a_arrangement = kPipelineArrangement) noexcept
	{
		return a_arrangement == PipelineArrangement::DlssThenNeural;
	}

	[[nodiscard]] constexpr bool ReplacesDlss(
		PipelineArrangement a_arrangement = kPipelineArrangement) noexcept
	{
		return a_arrangement == PipelineArrangement::NeuralReplacesDlss;
	}

	/** Feature 18 advertises upscaling for the reference and replacement paths. */
	[[nodiscard]] constexpr bool UsesFeatureUpscaling(
		PipelineArrangement a_arrangement = kPipelineArrangement) noexcept
	{
		return a_arrangement != PipelineArrangement::NeuralThenDlss;
	}

	/** A replacement failure must execute normal DLSS; other failures bypass NR. */
	[[nodiscard]] constexpr bool RunsDlssAfterNeuralFailure(
		PipelineArrangement a_arrangement = kPipelineArrangement) noexcept
	{
		return ReplacesDlss(a_arrangement);
	}

	/** Stable DevBench identifier for the selected stereo pipeline. */
	[[nodiscard]] constexpr const char* GetImplementationName(
		bool a_batchedStereo,
		bool a_directCommit) noexcept
	{
		if (a_batchedStereo)
			return a_directCommit ?
			           "stereo_batched_direct_commit" :
			           "stereo_batched_staged_commit";
		return a_directCommit ? "per_eye_direct_commit" : "per_eye_staged_commit";
	}

	/** Concise user-facing label for the selected stereo pipeline. */
	[[nodiscard]] constexpr const char* GetImplementationDisplayName(
		bool a_batchedStereo,
		bool a_directCommit) noexcept
	{
		if (a_batchedStereo)
			return a_directCommit ? "Batched + direct" : "Batched + staged";
		return a_directCommit ? "Per-eye + direct" : "Per-eye + staged";
	}

	/** Experimental purpose of one lane in the two-axis comparison matrix. */
	[[nodiscard]] constexpr const char* GetImplementationPurpose(
		bool a_batchedStereo,
		bool a_directCommit) noexcept
	{
		if (a_batchedStereo)
			return a_directCommit ?
			           "Fully optimized path" :
			           "Isolates stereo batching benefit";
		return a_directCommit ?
		           "Isolates direct-commit benefit" :
		           "Original baseline";
	}

	/** Stable DevBench identifier for the comparison purpose of a lane. */
	[[nodiscard]] constexpr const char* GetImplementationPurposeName(
		bool a_batchedStereo,
		bool a_directCommit) noexcept
	{
		if (a_batchedStereo)
			return a_directCommit ?
			           "fully_optimized_path" :
			           "isolates_batching_benefit";
		return a_directCommit ?
		           "isolates_direct_commit_benefit" :
		           "original_baseline";
	}

	[[nodiscard]] constexpr const char* GetStereoSubmissionName(
		bool a_batchedStereo) noexcept
	{
		return a_batchedStereo ? "batched" : "per_eye";
	}

	[[nodiscard]] constexpr const char* GetOutputCommitName(
		bool a_directCommit) noexcept
	{
		return a_directCommit ? "direct" : "staged";
	}

	/** Resolves a stable DevBench lane name into its two independent axes. */
	[[nodiscard]] constexpr std::optional<PipelineImplementation>
	ParseImplementationName(std::string_view a_name) noexcept
	{
		for (const auto implementation : kPipelineImplementations) {
			if (a_name == GetImplementationName(
							  implementation.batchedStereo, implementation.directCommit)) {
				return implementation;
			}
		}
		return std::nullopt;
	}
}
