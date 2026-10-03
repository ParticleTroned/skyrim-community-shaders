#pragma once

namespace CSX::RenderMap::DevBenchBridge
{
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Register the optional bounded diagnostic adapter when DevBench is present. */
	void Install();
	/** Whether this process successfully published the adapter's tool. */
	bool IsRegistered();
#else
	/** Ordinary release excludes Render Map translation units. */
	inline void Install() {}
	/** No diagnostic adapter is registered in ordinary release. */
	inline bool IsRegistered() { return false; }
#endif
}
