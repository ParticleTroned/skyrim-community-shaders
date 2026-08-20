#include "ShaderCompilationSchedulingPolicy.h"

#include <array>
#include <cstdint>
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
		return true;
	}

	constexpr bool CoversCooperativePrioritySelection()
	{
		return SelectCooperativeThreadPriority(ProcessPriorityBand::Standard) ==
		           CooperativeThreadPriority::BelowNormal &&
		       SelectCooperativeThreadPriority(ProcessPriorityBand::AboveNormal) ==
		           CooperativeThreadPriority::Lowest &&
		       SelectCooperativeThreadPriority(ProcessPriorityBand::HighOrRealtime) ==
		           CooperativeThreadPriority::Idle;
	}

	static_assert(CoversStartupCompilationCpuShare());
	static_assert(CoversCooperativePrioritySelection());
}

int main()
{
	return CoversStartupCompilationCpuShare() && CoversCooperativePrioritySelection() ? 0 : 1;
}
