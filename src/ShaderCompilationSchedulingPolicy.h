#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace ShaderCompilationSchedulingPolicy
{
	inline constexpr std::int32_t kStartupCompilationCpuSharePercent = 75;
	inline constexpr std::int32_t kMaximumCompilationCpuSharePercent = 90;
	static_assert(kStartupCompilationCpuSharePercent > 0 &&
				  kStartupCompilationCpuSharePercent <= kMaximumCompilationCpuSharePercent &&
				  kMaximumCompilationCpuSharePercent < 100);

	namespace detail
	{
		constexpr std::int32_t CalculateCpuShareThreadCount(
			std::int32_t a_logicalThreadCount, std::int32_t a_cpuSharePercent) noexcept
		{
			const auto logicalThreadCount = std::max(a_logicalThreadCount, 1);
			const auto workerCount = static_cast<std::int32_t>(
				static_cast<std::int64_t>(logicalThreadCount) * a_cpuSharePercent / 100);
			return std::max(workerCount, 1);
		}
	}

	/** Caps workers at 90% of logical processors, with a one-worker minimum. */
	[[nodiscard]] constexpr std::int32_t CalculateMaximumCompilationThreadCount(
		std::int32_t a_logicalThreadCount) noexcept
	{
		return detail::CalculateCpuShareThreadCount(a_logicalThreadCount, kMaximumCompilationCpuSharePercent);
	}

	[[nodiscard]] constexpr std::int32_t CalculateDefaultCompilationThreadCount(
		std::int32_t a_logicalThreadCount) noexcept
	{
		return detail::CalculateCpuShareThreadCount(a_logicalThreadCount, kStartupCompilationCpuSharePercent);
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

	/** The configured limit never grants more physical pool slots. */
	[[nodiscard]] constexpr std::size_t DispatchLimit(std::int32_t normal, std::int32_t background,
		bool backgroundMode, std::size_t poolCapacity) noexcept
	{
		return std::min(static_cast<std::size_t>(std::max(backgroundMode ? background : normal, 1)), poolCapacity);
	}

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
