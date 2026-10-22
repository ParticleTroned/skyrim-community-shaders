#pragma once

namespace VRShadowBatch
{
	/** Startup-only: optionally install VR shadow isolation before any native submissions. */
	void Install(bool a_enabled);
	/** Reports the installed state for this session; settings changes require a restart. */
	bool IsInstalled() noexcept;
}
