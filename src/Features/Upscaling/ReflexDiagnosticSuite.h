#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <nlohmann/json_fwd.hpp>

namespace ReflexDiagnostics::Suite
{
	/** A guided local test and export; no DevBench host is required. */
	void DrawControls();
	bool Busy();
	/** Restore saved settings at the next hook if a task-queue restoration timed out. */
	void RestorePending();
	/** Diagnostic overrides must never become the user's saved configuration. */
	void PreservePersistedSettings(nlohmann::json& settings);
	/** Start, inspect or cancel the same guided test used by the menu. */
	nlohmann::json Dispatch(const nlohmann::json& args);
}
#endif
