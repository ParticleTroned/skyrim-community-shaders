#pragma once

/** @brief Registers the profiler inspection tool with the optional external devbench host. */
namespace ProfilerDevBenchBridge
{
	void Install();
	bool IsBuilt();
	bool IsRegistered();
}
