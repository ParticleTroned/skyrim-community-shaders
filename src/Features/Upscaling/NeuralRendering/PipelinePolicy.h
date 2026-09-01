#pragma once

#include <cstdint>

namespace NeuralRendering
{
	/** Fixed Feature 18 placement for this experimental branch. */
	enum class PipelineArrangement : std::uint32_t
	{
		DlssThenNeural = 0,
		NeuralThenDlss = 1,
	};

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
