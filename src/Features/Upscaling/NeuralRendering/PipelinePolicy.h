#pragma once

#include <cstdint>
#include <limits>

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
		PipelineArrangement::NeuralThenDlss;

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
}
