#pragma once

#include "CharacterMultiRoi.h"

#include <cstdint>
#include <limits>
#include <string_view>

namespace NeuralRendering
{
	/** Fixed Feature 18 placement for this experimental branch. */
	enum class PipelineArrangement : std::uint32_t
	{
		DlssThenNeural = 0,
		NeuralThenDlss = 1,
	};

	struct HardMenuContextInputs
	{
		bool mainMenu = false;
		bool loading = false;
		bool saveLoadTransition = false;
		bool loadingSubmitProtection = false;
		bool communityShadersMenu = false;
	};

	[[nodiscard]] constexpr bool IsHardMenuContext(
		const HardMenuContextInputs& a_inputs) noexcept
	{
		return a_inputs.mainMenu ||
		       a_inputs.loading ||
		       a_inputs.saveLoadTransition ||
		       a_inputs.loadingSubmitProtection ||
		       a_inputs.communityShadersMenu;
	}

	struct MenuContinuityInputs
	{
		bool hardMenuContext = false;
		bool gamePaused = false;
		bool worldFrameStateAvailable = false;
		std::uint32_t currentFrame = 0;
		std::uint32_t lastWorldRenderFrame =
			std::numeric_limits<std::uint32_t>::max();
		std::uint32_t lastCompletedWorldRenderFrame =
			std::numeric_limits<std::uint32_t>::max();
	};

	struct MenuContinuityResult
	{
		bool admitted = false;
		bool currentWorldFrame = false;
		bool retainedWorldFrame = false;
	};

	/** Admits ordinary paused menus only while their world image is correlated. */
	[[nodiscard]] constexpr MenuContinuityResult EvaluateMenuContinuity(
		const MenuContinuityInputs& a_inputs) noexcept
	{
		MenuContinuityResult result{};
		if (a_inputs.hardMenuContext)
			return result;

		if (!a_inputs.gamePaused) {
			result.admitted = true;
			return result;
		}

		if (!a_inputs.worldFrameStateAvailable)
			return result;

		result.currentWorldFrame =
			a_inputs.lastWorldRenderFrame == a_inputs.currentFrame;
		const bool completedWorldFrameAvailable =
			a_inputs.lastCompletedWorldRenderFrame != 0 &&
			a_inputs.lastCompletedWorldRenderFrame !=
				std::numeric_limits<std::uint32_t>::max() &&
			a_inputs.lastWorldRenderFrame ==
				a_inputs.lastCompletedWorldRenderFrame &&
			a_inputs.lastCompletedWorldRenderFrame <= a_inputs.currentFrame;
		result.retainedWorldFrame =
			!result.currentWorldFrame && completedWorldFrameAvailable;
		result.admitted =
			result.currentWorldFrame || result.retainedWorldFrame;
		return result;
	}

	inline constexpr PipelineArrangement kPipelineArrangement =
		PipelineArrangement::DlssThenNeural;

	[[nodiscard]] constexpr const char* GetPipelineArrangementName() noexcept
	{
		return kPipelineArrangement == PipelineArrangement::NeuralThenDlss ?
		           "neural_then_dlss" :
		           "dlss_then_neural";
	}

	[[nodiscard]] constexpr const char* GetPipelineArrangementDisplayName() noexcept
	{
		return kPipelineArrangement == PipelineArrangement::NeuralThenDlss ?
		           "NR -> DLSS" :
		           "DLSS -> NR";
	}

	[[nodiscard]] constexpr bool RunsBeforeDlss() noexcept
	{
		return kPipelineArrangement == PipelineArrangement::NeuralThenDlss;
	}

	[[nodiscard]] constexpr bool RunsAfterDlss() noexcept
	{
		return kPipelineArrangement == PipelineArrangement::DlssThenNeural;
	}

	/** The post-DLSS reference path uses Feature 18's mixed-resolution contract. */
	[[nodiscard]] constexpr bool UsesFeatureUpscaling() noexcept
	{
		return RunsAfterDlss();
	}
	/** Pure admission contract used before any resource allocation or GPU recording. */
	[[nodiscard]] constexpr std::string_view GetCharacterRegionSubmissionViolation(
		std::uint32_t a_logicalSlot, const CharacterComputeRegionPlan& a_plan,
		const ComputeSubrect& a_support, std::uint32_t a_width, std::uint32_t a_height,
		bool a_characterVisualIsolation) noexcept
	{
		if (a_logicalSlot != 0u || a_plan.count > 2u)
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

}
