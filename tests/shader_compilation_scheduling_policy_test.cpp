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
		constexpr std::array<std::pair<std::int32_t, std::int32_t>, 9> cases{ {
			{ -1, 1 },
			{ 0, 1 },
			{ 1, 1 },
			{ 2, 1 },
			{ 4, 3 },
			{ 8, 6 },
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
	static_assert(CoversCooperativePrioritySelection());
	static_assert(CoversWorkerPriorityLifecycle());
}

int main()
{
	return CoversStartupCompilationCpuShare() &&
	               CoversCooperativePrioritySelection() &&
	               CoversWorkerPriorityLifecycle() ?
	           0 :
	           1;
}
