#pragma once

#include "LookPack.h"

namespace Presets::LookSceneRuntime
{
	/** Replaces native scene rules; callers suspend base settings while switching profiles. */
	void Use(const LookPack* pack);
	/** Restores authored values for nested settings transactions. */
	void Suspend();
	void Resume();
	/** Evaluates cached scene rules after native weather settings, without filesystem access. */
	void Update();
	/** Removes transient scene values from saves, exports and performance snapshots. */
	void OverlayBase(const std::string& feature, Json& settings);
	bool Controls(const std::string& feature, const std::string& root = {});
	const std::string& Error();
}
