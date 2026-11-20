#include "ShaderCompilationSchedulingPolicy.h"

#include <array>
#include <cstdint>
#include <limits>
#include <utility>

namespace
{
	using namespace ShaderCompilationSchedulingPolicy;

	constexpr bool CoversStartupCompilationCpuShare()
	{
		constexpr std::array<std::pair<std::int32_t, std::int32_t>, 13> cases{ {
			{ std::numeric_limits<std::int32_t>::min(), 1 },
			{ -1, 1 },
			{ 0, 1 },
			{ 1, 1 },
			{ 2, 1 },
			{ 3, 2 },
			{ 4, 3 },
			{ 8, 6 },
			{ 10, 7 },
			{ 11, 8 },
			{ 12, 9 },
			{ 16, 12 },
			{ 32, 24 },
		} };
		for (const auto& [logicalThreads, expectedWorkers] : cases) {
			if (CalculateDefaultCompilationThreadCount(logicalThreads) != expectedWorkers)
				return false;
		}
		return CalculateDefaultCompilationThreadCount(
				   std::numeric_limits<std::int32_t>::max()) ==
		       1610612735;
	}

	constexpr bool CoversCooperativePrioritySelection()
	{
		return SelectCooperativeThreadPriority(ProcessPriorityBand::Standard) ==
		           CooperativeThreadPriority::BelowNormal &&
		       SelectCooperativeThreadPriority(ProcessPriorityBand::AboveNormal) ==
		           CooperativeThreadPriority::Lowest &&
		       SelectCooperativeThreadPriority(ProcessPriorityBand::High) ==
		           CooperativeThreadPriority::Idle &&
		       SelectCooperativeThreadPriority(ProcessPriorityBand::Realtime) ==
		           CooperativeThreadPriority::Idle;
	}

	constexpr bool CoversCompilationCpuHeadroom()
	{
		constexpr std::array<std::pair<std::int32_t, std::int32_t>, 16> cases{ {
			{ std::numeric_limits<std::int32_t>::min(), 1 },
			{ -1, 1 },
			{ 0, 1 },
			{ 1, 1 },
			{ 2, 1 },
			{ 3, 2 },
			{ 4, 3 },
			{ 8, 7 },
			{ 10, 9 },
			{ 11, 9 },
			{ 12, 10 },
			{ 16, 14 },
			{ 32, 28 },
			{ 64, 57 },
			{ 128, 115 },
			{ std::numeric_limits<std::int32_t>::max(), 1932735282 },
		} };
		for (const auto& [logicalThreads, expectedWorkers] : cases) {
			const auto capacity = CalculateMaximumCompilationThreadCount(logicalThreads);
			if (capacity != expectedWorkers ||
				CalculateDefaultCompilationThreadCount(logicalThreads) > capacity ||
				(logicalThreads > 1 && capacity >= logicalThreads) ||
				DispatchLimit(logicalThreads, logicalThreads, false, capacity) != static_cast<std::size_t>(capacity) ||
				DispatchLimit(logicalThreads, logicalThreads, true, capacity) != static_cast<std::size_t>(capacity))
				return false;
		}
		const auto capacity = CalculateMaximumCompilationThreadCount(32);
		return DispatchLimit(28, 27, false, capacity) == 28 &&
		       DispatchLimit(28, 27, true, capacity) == 27;
	}

	constexpr bool CoversWorkerPriorityLifecycle()
	{
		return SelectCompilationPhase(false) == CompilationPhase::Startup &&
		       SelectCompilationPhase(true) == CompilationPhase::InGame &&
		       !ShouldCompleteStartupCompilation(false, false) &&
		       !ShouldCompleteStartupCompilation(false, true) &&
		       !ShouldCompleteStartupCompilation(true, true) &&
		       ShouldCompleteStartupCompilation(true, false) &&
		       SelectWorkerThreadPriorityMode(SelectCompilationPhase(false)) ==
		           WorkerThreadPriorityMode::CooperativeBackground &&
		       SelectWorkerThreadPriorityMode(SelectCompilationPhase(true)) ==
		           WorkerThreadPriorityMode::ProcessNormal;
	}

	static_assert(CoversStartupCompilationCpuShare());
	static_assert(CoversCompilationCpuHeadroom());
	static_assert(CoversCooperativePrioritySelection());
	static_assert(CoversWorkerPriorityLifecycle());
	static_assert(DispatchLimit(12, 8, false, 12) == 12);
	static_assert(DispatchLimit(12, 8, true, 12) == 8);
	static_assert(DispatchLimit(32, 32, false, 12) == 12);
	static_assert(DispatchLimit(-1, 0, true, 12) == 1);
	static_assert(DispatchLimit(12, 8, false, 0) == 0);
}

int main()
{
	return CoversStartupCompilationCpuShare() &&
	               CoversCompilationCpuHeadroom() &&
	               CoversCooperativePrioritySelection() &&
	               CoversWorkerPriorityLifecycle() ?
	           0 :
	           1;
}
