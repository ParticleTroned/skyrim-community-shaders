#pragma once

#include <chrono>

namespace NeuralRendering
{
#ifdef DEVBENCH_BRIDGE_ENABLED
	inline constexpr bool kDevelopmentDiagnostics = true;
#else
	inline constexpr bool kDevelopmentDiagnostics = false;
#endif

	/** Production builds do not sample clocks for NR development telemetry. */
	[[nodiscard]] inline std::chrono::steady_clock::time_point DiagnosticNow() noexcept
	{
		if constexpr (kDevelopmentDiagnostics) {
			return std::chrono::steady_clock::now();
		} else {
			return {};
		}
	}
}
