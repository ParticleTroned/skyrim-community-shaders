#pragma once

#include "LightAttachPolicy.h"
#include <nlohmann/json_fwd.hpp>

namespace LightLimitFixDetail::LightAttach
{
	/** Verify the native dynamic call before installing the optional, initially inactive hook. */
	void Install();
	/** Stop admission immediately; retained references retire at the next frame update. */
	void Invalidate() noexcept;
	/** Publish settings and retire stale ownership outside engine light-queue locks. */
	void Update(bool a_enabled, int a_refreshFrames, bool a_featureEnabled);
	/** Expose the experimental controls with the same bounds used by configuration and the API. */
	void DrawSettings(bool& a_enabled, int& a_refreshFrames);
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Inspect or edit session settings on the main thread; omitted settings are read-only. */
	nlohmann::json Control(const nlohmann::json& a_request);
	/** Registered input and output contracts for the dedicated experimental action. */
	nlohmann::json GetControlSchemas();
#endif
}
