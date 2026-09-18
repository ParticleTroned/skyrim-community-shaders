#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Diagnostics/VRFrustumTelemetryPolicy.h"
#	include <nlohmann/json_fwd.hpp>

namespace VRFrustumTelemetry
{
	/** Install read-only native diagnostics only on the verified VR executable. */
	void Install();
	/** Toggle collection only; cumulative counters remain monotonic. */
	void SetEnabled(bool a_enabled);
	/** Toggle bounded detail samples independently of aggregate counts. */
	void SetDetailEnabled(bool a_enabled);
	/** Separate counter intervals when the evaluated path changes. */
	void AdvanceCollectionGeneration();
	/** Return bounded per-thread/pass/caller counters, coverage and attribution limits. */
	nlohmann::json GetStatus();
	/** Mark calling-thread pass context; renderer boundaries may sample the raw camera index. */
	ContextScope EnterScope(Pass a_pass, bool a_sampleCameraIndex = false);
}

#endif
