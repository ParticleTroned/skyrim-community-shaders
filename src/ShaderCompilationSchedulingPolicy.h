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
		// Split the percentage calculation so a corrupt or synthetic large input
		// cannot overflow before the clamp. Real hardware counts are much smaller,
		// but this keeps the policy total over its complete integer input domain.
		const auto wholeHundreds = logicalThreadCount / 100;
		const auto remainder = logicalThreadCount % 100;
		const auto workerCount =
			wholeHundreds * kStartupCompilationCpuSharePercent +
			(remainder * kStartupCompilationCpuSharePercent) / 100;
		return std::max(
			workerCount,
			1);
	}

	enum class ProcessPriorityBand
	{
		Standard,
		AboveNormal,
		High,
		Realtime,
	};

	enum class CooperativeThreadPriority
	{
		BelowNormal,
		Lowest,
		Idle,
	};

	enum class CompilationPhase
	{
		Startup,
		InGame,
	};

	enum class WorkerThreadPriorityMode
	{
		CooperativeBackground,
		ProcessNormal,
	};

	[[nodiscard]] constexpr CompilationPhase SelectCompilationPhase(
		bool a_startupCompilationComplete) noexcept
	{
		return a_startupCompilationComplete ? CompilationPhase::InGame : CompilationPhase::Startup;
	}

	[[nodiscard]] constexpr bool ShouldCompleteStartupCompilation(
		bool a_dataLoaded,
		bool a_compilationActive) noexcept
	{
		return a_dataLoaded && !a_compilationActive;
	}

	[[nodiscard]] constexpr WorkerThreadPriorityMode SelectWorkerThreadPriorityMode(
		CompilationPhase a_phase) noexcept
	{
		return a_phase == CompilationPhase::Startup ?
		           WorkerThreadPriorityMode::CooperativeBackground :
		           WorkerThreadPriorityMode::ProcessNormal;
	}

	[[nodiscard]] constexpr CooperativeThreadPriority SelectCooperativeThreadPriority(
		ProcessPriorityBand a_processPriorityBand) noexcept
	{
		switch (a_processPriorityBand) {
		case ProcessPriorityBand::High:
		case ProcessPriorityBand::Realtime:
			return CooperativeThreadPriority::Idle;
		case ProcessPriorityBand::AboveNormal:
			return CooperativeThreadPriority::Lowest;
		default:
			return CooperativeThreadPriority::BelowNormal;
		}
	}
}
