#include "ShaderCompilationSchedulingPolicy.h"

#include <catch2/catch_test_macros.hpp>

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
}

TEST_CASE("shader compiler priority remains cooperative across process classes")
{
	CHECK(SelectCooperativeThreadPriority(ProcessPriorityBand::Standard) == CooperativeThreadPriority::BelowNormal);
	CHECK(SelectCooperativeThreadPriority(ProcessPriorityBand::AboveNormal) == CooperativeThreadPriority::Lowest);
	CHECK(SelectCooperativeThreadPriority(ProcessPriorityBand::HighOrRealtime) == CooperativeThreadPriority::Idle);
}
