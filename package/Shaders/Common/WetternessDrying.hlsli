#ifndef WETTERNESS_DRYING_ENVELOPE_HLSLI
#define WETTERNESS_DRYING_ENVELOPE_HLSLI

#ifdef __cplusplus
#	include <algorithm>
#endif

namespace WetternessDrying
{
	// Callers supply a positive duration in the same units as elapsed time.
#ifdef __cplusplus
	inline
#endif
		float Remaining(float elapsed, float duration)
	{
		float remaining = 1.0f - elapsed / duration;
#ifdef __cplusplus
		return std::clamp(remaining, 0.0f, 1.0f);
#else
		return saturate(remaining);
#endif
	}
}

#endif
