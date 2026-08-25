#include "ShaderCompilationSchedulingPolicy.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace ShaderCompilationSchedulingPolicy;

TEST_CASE("startup shader compilation retains operating-system headroom")
{
	CHECK(CalculateDefaultCompilationThreadCount(-1) == 1);
	CHECK(CalculateDefaultCompilationThreadCount(0) == 1);
	CHECK(CalculateDefaultCompilationThreadCount(1) == 1);
	CHECK(CalculateDefaultCompilationThreadCount(2) == 1);
	CHECK(CalculateDefaultCompilationThreadCount(4) == 3);
	CHECK(CalculateDefaultCompilationThreadCount(8) == 6);
	CHECK(CalculateDefaultCompilationThreadCount(12) == 9);
	CHECK(CalculateDefaultCompilationThreadCount(16) == 12);
	CHECK(CalculateDefaultCompilationThreadCount(32) == 24);
	CHECK(CalculateDefaultCompilationThreadCount(std::numeric_limits<std::int32_t>::max()) == 1610612735);
}

TEST_CASE("shader compiler priority remains cooperative across process classes")
{
	CHECK(SelectCooperativeThreadPriority(ProcessPriorityBand::Standard) == CooperativeThreadPriority::BelowNormal);
	CHECK(SelectCooperativeThreadPriority(ProcessPriorityBand::AboveNormal) == CooperativeThreadPriority::Lowest);
	CHECK(SelectCooperativeThreadPriority(ProcessPriorityBand::High) == CooperativeThreadPriority::Idle);
	CHECK(SelectCooperativeThreadPriority(ProcessPriorityBand::Realtime) == CooperativeThreadPriority::Idle);
}

TEST_CASE("shader compiler workers restore normal relative priority after startup")
{
	CHECK(SelectWorkerThreadPriorityMode(CompilationPhase::Startup) == WorkerThreadPriorityMode::CooperativeBackground);
	CHECK(SelectWorkerThreadPriorityMode(CompilationPhase::InGame) == WorkerThreadPriorityMode::ProcessNormal);
}
