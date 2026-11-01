#pragma once

#ifdef TRACY_SUPPORT
#	include <Tracy/Tracy.hpp>
#	include <atomic>

namespace Util::TracyZones
{
	namespace detail
	{
		inline std::atomic_bool perDrawZones{ false };
	}

	/** @brief Returns the per-draw CPU zone preference; snapshot once per draw. */
	inline bool PerDrawZonesEnabled()
	{
		return detail::perDrawZones.load(std::memory_order_relaxed);
	}

	/** @brief Changes future draw snapshots without affecting open Tracy scopes. */
	inline void SetPerDrawZones(bool a_enabled)
	{
		detail::perDrawZones.store(a_enabled, std::memory_order_relaxed);
	}
}

/** @brief Uses a draw's captured preference for a balanced, static CPU zone. */
#	define CS_DRAW_ZONE(variable, name, active) ZoneNamedN(variable, name, active)
#else
#	define CS_DRAW_ZONE(variable, name, active)
#endif
