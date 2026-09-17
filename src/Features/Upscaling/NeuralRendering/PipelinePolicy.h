#pragma once

#include "CharacterMultiRoi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace NeuralRendering
{
	/** Independent routes exposed by the Neural Rendering feature. */
	enum class RenderingMode : std::uint32_t
	{
		FullResolution = 0,
		Foveated = 1,
		ReducedResolution = 2,
	};

	[[nodiscard]] constexpr RenderingMode ClampRenderingMode(std::uint32_t a_value) noexcept
	{
		return a_value <= static_cast<std::uint32_t>(RenderingMode::ReducedResolution) ?
		           static_cast<RenderingMode>(a_value) :
		           RenderingMode::Foveated;
	}

	[[nodiscard]] constexpr const char* GetRenderingModeName(RenderingMode a_mode) noexcept
	{
		switch (a_mode) {
		case RenderingMode::FullResolution:
			return "full_resolution";
		case RenderingMode::Foveated:
			return "foveated";
		case RenderingMode::ReducedResolution:
			return "reduced_resolution";
		default:
			return "unknown";
		}
	}

	[[nodiscard]] constexpr std::optional<RenderingMode> ParseRenderingModeName(std::string_view a_name) noexcept
	{
		for (auto mode : { RenderingMode::FullResolution, RenderingMode::Foveated, RenderingMode::ReducedResolution })
			if (a_name == GetRenderingModeName(mode))
				return mode;
		return std::nullopt;
	}

	/** The flat renderer keeps its native category format; character authoring is VR-only. */
	[[nodiscard]] constexpr bool IsRenderingConfigurationSupported(bool isVR, RenderingMode mode, bool character) noexcept
	{
		return isVR || (mode == RenderingMode::FullResolution && !character);
	}

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

	/** History keys separate insertion domains; healthy backends can retain resources. */
	[[nodiscard]] constexpr bool RequiresBackendRetirement(
		bool a_enableStateChanged, bool a_multiRoiChanged,
		bool a_insertionPointChanged, bool a_backendFailed) noexcept
	{
		return a_enableStateChanged || a_multiRoiChanged ||
		       (a_insertionPointChanged && a_backendFailed);
	}

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

	/** Public mode selection fixes image domains; only the foveated route selects an insertion. */
	[[nodiscard]] constexpr InsertionPoint ResolveInsertionPoint(RenderingMode mode, std::uint32_t selected) noexcept
	{
		return mode == RenderingMode::FullResolution    ? InsertionPoint::FinalLdrPreUi :
		       mode == RenderingMode::ReducedResolution ? InsertionPoint::UpscaledCenter :
		                                                  ClampInsertionPoint(selected);
	}

	/** Reduced resolution runs stateless Feature 18 before the temporal upscaler. */
	[[nodiscard]] constexpr PipelineArrangement ResolvePipelineArrangement(RenderingMode mode) noexcept
	{
		return mode == RenderingMode::ReducedResolution ? PipelineArrangement::NeuralThenDlss : PipelineArrangement::DlssThenNeural;
	}

	/** Mono transactions require one complete eye; VR requires the coherent pair. */
	[[nodiscard]] constexpr std::uint32_t RequiredEyeMask(bool stereo) noexcept
	{
		return stereo ? 0b11u : 0b01u;
	}

	[[nodiscard]] constexpr bool IsCompleteNeuralImageResult(std::uint32_t successful, std::uint32_t bypassed, bool stereo) noexcept
	{
		const auto required = RequiredEyeMask(stereo);
		return successful != 0 && (successful & bypassed) == 0 && (successful | bypassed) == required;
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

	/**
	 * Permits NR behind menus only when required UI is isolated for a later
	 * composite. The caller includes open menus and current presentation work
	 * in a_menuLayerRequired, but not the tracking tail after a menu has closed.
	 * A tail with no remaining UI must not suppress a fresh world frame.
	 */
	[[nodiscard]] constexpr bool ResolveMenuContinuityAllowed(
		bool a_hardMenuBlocked,
		bool a_menuLayerRequired,
		bool a_lateMenuCompositeReady) noexcept
	{
		return !a_hardMenuBlocked &&
		       (!a_menuLayerRequired || a_lateMenuCompositeReady);
	}

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
		std::uint32_t sourceWorldFrame =
			std::numeric_limits<std::uint32_t>::max();
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
			result.sourceWorldFrame = result.retainedWorldFrame ?
			                              result.lastCompletedWorldRenderFrame :
			                              result.currentFrame;
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

	/** Returns the correlated world-input frame consumed by an admitted route. */
	[[nodiscard]] constexpr std::uint32_t GetTemporalSourceFrame(
		const TemporalAdmissionResult& a_result) noexcept
	{
		return a_result.sourceWorldFrame;
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
		constexpr std::uint32_t mainMask = 0b00110011u;
		constexpr std::uint32_t submitMask = 0b11001100u;
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

	/** Physical region slots retain their logical route/eye in the lower two bits. */
	[[nodiscard]] constexpr std::uint32_t LogicalFeatureSlot(std::uint32_t a_slot) noexcept
	{
		return a_slot % 4u;
	}

	[[nodiscard]] constexpr std::uint32_t PhysicalRegionFeatureSlot(
		std::uint32_t a_logicalSlot, std::uint32_t a_region) noexcept
	{
		return a_logicalSlot < 4u && a_region < 2u ? a_logicalSlot + a_region * 4u : 8u;
	}

	/** Pure admission contract used before any resource allocation or GPU recording. */
	[[nodiscard]] constexpr std::string_view GetCharacterRegionSubmissionViolation(
		std::uint32_t a_logicalSlot, const CharacterComputeRegionPlan& a_plan,
		const ComputeSubrect& a_support, std::uint32_t a_width, std::uint32_t a_height,
		bool a_characterVisualIsolation) noexcept
	{
		if (a_logicalSlot >= 4u || a_plan.count > 2u)
			return "character regions require a logical feature slot and at most two regions";
		if (a_plan.count == 0u)
			return {};
		if (!a_characterVisualIsolation || !a_support.Fits(a_width, a_height))
			return "explicit character regions require exact outer CSX mask compositing and valid support bounds";
		for (std::uint32_t region = 0; region < a_plan.count; ++region) {
			if (!a_plan.regions[region].Fits(a_width, a_height) ||
				!ContainsComputeSubrect(a_support, a_plan.regions[region]) ||
				a_plan.historyKeys[region] == 0u || a_plan.clusterIdentities[region] == 0u)
				return "character region dimensions or persistent history identity are invalid";
		}
		if (a_plan.count == 2u &&
			(CharacterComputeRegionsOverlap(a_plan.regions[0], a_plan.regions[1]) ||
				a_plan.historyKeys[0] == a_plan.historyKeys[1] ||
				a_plan.clusterIdentities[0] == a_plan.clusterIdentities[1]))
			return "independent character regions must be disjoint with distinct histories";
		return {};
	}

	/** Region histories synchronize only for the same cluster in opposite eyes. */
	[[nodiscard]] constexpr bool IsMatchingRegionStereoPair(
		std::uint32_t a_leftSlot, std::uint64_t a_leftIdentity,
		std::uint32_t a_rightSlot, std::uint64_t a_rightIdentity) noexcept
	{
		return a_leftSlot < 8u && a_rightSlot < 8u &&
		       a_leftIdentity == a_rightIdentity &&
		       IsOrderedStereoFeatureSlotPair(
				   LogicalFeatureSlot(a_leftSlot), LogicalFeatureSlot(a_rightSlot));
	}

	/** Preserve logical-eye outcomes while exposing physical region counts in telemetry. */
	[[nodiscard]] constexpr std::uint32_t AggregateRegionEvaluationMask(
		std::uint32_t a_physicalMask,
		const std::array<std::uint32_t, 4>& a_requiredMasks,
		bool a_requireAll) noexcept
	{
		std::uint32_t result = 0;
		for (std::uint32_t logical = 0; logical < a_requiredMasks.size(); ++logical) {
			const auto required = a_requiredMasks[logical];
			const auto present = a_physicalMask & required;
			if (required != 0u && (a_requireAll ? present == required : present != 0u))
				result |= 1u << logical;
		}
		return result;
	}

	/**
	 * A stereo NR transaction is complete when every eye either presented a
	 * successful Feature 18 result or was independently proven empty. At least
	 * one eye must have presented NR, and no eye may be both states.
	 */
	[[nodiscard]] constexpr bool IsCompleteNeuralStereoResult(
		std::uint32_t a_successfulEyeMask,
		std::uint32_t a_bypassedEyeMask) noexcept
	{
		constexpr std::uint32_t stereoEyeMask = 0b11u;
		if ((a_successfulEyeMask | a_bypassedEyeMask) & ~stereoEyeMask)
			return false;
		return a_successfulEyeMask != 0u &&
		       (a_successfulEyeMask & a_bypassedEyeMask) == 0u &&
		       (a_successfulEyeMask | a_bypassedEyeMask) == stereoEyeMask;
	}

	static_assert(IsSequentialFrame(10u, 11u));
	static_assert(IsSequentialFrame(std::numeric_limits<std::uint32_t>::max(), 0u));
	static_assert(!IsSequentialFrame(10u, 10u));
	static_assert(!IsSequentialFrame(10u, 12u));

	// Legacy callers retain the foveated default; feature routes pass their
	// selected arrangement explicitly.
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

	/** Submit float resources belong to NR output; pre-DLSS NR publishes the later DLSS output. */
	[[nodiscard]] constexpr bool UsesSubmitNeuralFloatBridge(
		bool a_submitStage, bool a_neuralActive, PipelineArrangement a_arrangement) noexcept
	{
		return a_submitStage && a_neuralActive && !RunsBeforeDlss(a_arrangement);
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

	/**
	 * Resolves Feature 18's immutable upscaling mode from the resources that the
	 * provider will actually consume.  Native/DLAA geometry must not be
	 * advertised as upscaling merely because the selected pipeline can upscale;
	 * the private runtime treats this bit as part of feature creation state.
	 *
	 * A missing result denotes unsupported zero-sized or downscaling geometry.
	 */
	[[nodiscard]] constexpr std::optional<bool> ResolveFeatureUpscaling(
		std::uint32_t a_guideWidth,
		std::uint32_t a_guideHeight,
		std::uint32_t a_outputWidth,
		std::uint32_t a_outputHeight,
		PipelineArrangement a_arrangement = kPipelineArrangement) noexcept
	{
		if (!a_guideWidth || !a_guideHeight || !a_outputWidth ||
			!a_outputHeight || a_guideWidth > a_outputWidth ||
			a_guideHeight > a_outputHeight) {
			return std::nullopt;
		}

		const bool geometryUpscales =
			a_guideWidth != a_outputWidth ||
			a_guideHeight != a_outputHeight;
		if (geometryUpscales && !UsesFeatureUpscaling(a_arrangement))
			return std::nullopt;
		return geometryUpscales;
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
