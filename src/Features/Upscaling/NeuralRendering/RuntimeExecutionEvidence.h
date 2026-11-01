#pragma once
#include <cstdint>
#include <optional>
namespace NeuralRendering
{
	struct RuntimeExecutionEvidence
	{
		bool createAttempted = false, createSucceeded = false;
		bool evaluateAttempted = false, evaluateSucceeded = false;
		const char* createReason = nullptr;
		std::optional<std::uint32_t> createResult, evaluateResult;
		std::optional<std::uint64_t> createCpuMicroseconds, evaluateCpuMicroseconds;
	};
}
