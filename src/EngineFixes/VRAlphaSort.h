#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <nlohmann/json_fwd.hpp>
#endif

namespace VRAlphaSort
{
	/** Install the guarded VR 1.4.15 fallback unless Engine Fixes already provides distance ordering. */
	void Install();
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Inspect the active provider and local ordering counters without changing live queue semantics. */
	nlohmann::json GetDiagnostics();
#endif
}
