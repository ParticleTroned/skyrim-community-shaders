#pragma once

#include "SnowMeltHistory.h"

struct SnowCover;

namespace WetternessSnowMelt
{
	/** Records Snow Cover's current world state without activating rain simulation or effects. */
	void Update(const SnowCover& snow, float snowRate);
	/** Retains the departing world's history through interior and worldspace transitions. */
	void SuspendWorld();
	/** Restores this world's snow clock so time spent travelling also advances melting. */
	void ResumeWorld(SnowCover& snow);
	/** Clears all world histories across save loads, config changes and explicit disablement. */
	void Reset();
	/** Includes recently melted snow when deciding whether a draw can skip snow evaluation. */
	bool HasHistory();
	/** Extends current bounds to include recently covered surfaces without disabling culling. */
	void ExpandSnowBounds(float& weatherSnow, float& seasonalAltitude);
	/** Thread-local snapshot, valid until the next call on this thread; inactive history is shared. */
	const PerFrame& GetCommonBufferData();
}
