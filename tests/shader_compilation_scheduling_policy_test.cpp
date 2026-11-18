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
	constexpr bool CoversPreparationWindows()
	{
		for (const auto mode : { PreparationMode::StartupOnly, PreparationMode::Transitions, PreparationMode::Gameplay }) {
			for (bool startup : { false, true }) {
				for (bool ready : { false, true }) {
					for (bool transition : { false, true }) {
						const bool expected = startup || (ready && (mode == PreparationMode::Gameplay || (mode == PreparationMode::Transitions && transition)));
						if (CanPrepareOptional(mode, startup, ready, transition) != expected)
							return false;
					}
				}
			}
		}
		return ValidatePreparationMode(-1) == PreparationMode::StartupOnly &&
		       ValidatePreparationMode(3) == PreparationMode::StartupOnly &&
		       ValidatePreparationMode(INT64_MAX) == PreparationMode::StartupOnly &&
		       ValidatePreparationMode(1) == PreparationMode::Transitions &&
		       ValidatePreparationMode(2) == PreparationMode::Gameplay;
	}
	static_assert(DispatchLimit(12, 8, false, 12) == 12);
	static_assert(DispatchLimit(12, 8, true, 12) == 8);
	static_assert(DispatchLimit(32, 32, false, 12) == 12);
	static_assert(DispatchLimit(-1, 0, true, 12) == 1);
	static_assert(DispatchLimit(12, 8, false, 0) == 0);
	static_assert(CoversPreparationWindows());
	static_assert(IsBlockingStartup(false, false, false));
	static_assert(!IsBlockingStartup(true, false, false));
	static_assert(!IsBlockingStartup(false, true, false));
	static_assert(!IsBlockingStartup(false, false, true));
	static_assert(!IsBlockingStartup(false, true, true));
	static_assert(!IsBlockingStartup(true, true, true));
}

int main()
{
	return CoversStartupCompilationCpuShare() &&
	               CoversCooperativePrioritySelection() &&
	               CoversWorkerPriorityLifecycle() ?
	           0 :
	           1;
}
