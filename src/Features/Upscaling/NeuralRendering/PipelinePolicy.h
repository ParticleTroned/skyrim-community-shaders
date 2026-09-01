#pragma once

#include <array>
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
		if ((a_presentedEyeMask & currentEyeBit) != 0)
			return CachedStereoPairReuse::BypassPresentedEye;
		if (a_contextMatches)
			return CachedStereoPairReuse::Reuse;

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
