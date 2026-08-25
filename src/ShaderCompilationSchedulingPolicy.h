#pragma once

#include <algorithm>
#include <cstdint>

namespace ShaderCompilationSchedulingPolicy
{
	inline constexpr std::int32_t kStartupCompilationCpuSharePercent = 75;

	[[nodiscard]] constexpr std::int32_t CalculateDefaultCompilationThreadCount(
		std::int32_t a_logicalThreadCount) noexcept
	{
		const auto logicalThreadCount = std::max(a_logicalThreadCount, 1);
		return std::max(
			(logicalThreadCount * kStartupCompilationCpuSharePercent) / 100,
			1);
	}

	enum class ProcessPriorityBand
	{
		Standard,
		AboveNormal,
		HighOrRealtime,
	};

	enum class CooperativeThreadPriority
	{
		BelowNormal,
		Lowest,
		Idle,
	};

	[[nodiscard]] constexpr CooperativeThreadPriority SelectCooperativeThreadPriority(
		ProcessPriorityBand a_processPriorityBand) noexcept
	{
		switch (a_processPriorityBand) {
		case ProcessPriorityBand::HighOrRealtime:
			return CooperativeThreadPriority::Idle;
		case ProcessPriorityBand::AboveNormal:
			return CooperativeThreadPriority::Lowest;
		default:
			return CooperativeThreadPriority::BelowNormal;
		}
	}
}
