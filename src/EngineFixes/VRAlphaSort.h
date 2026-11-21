#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <nlohmann/json_fwd.hpp>
#endif

namespace VRAlphaSort
{
	/** Install an atomic, VR 1.4.15-only distance-ordering candidate for native alpha queues. */
	void Install();
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Inspect installation and ordering coverage without changing live queue semantics. */
	nlohmann::json GetDiagnostics();
#endif
}
