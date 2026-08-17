#pragma once

/** @brief Registers typed, metadata-bearing feature controls with the optional devbench host. */
namespace FeatureControlDevBenchBridge
{
	void Install();
	bool IsBuilt();
	bool IsRegistered();
}
