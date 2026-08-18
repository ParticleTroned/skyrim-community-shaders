#include "Features/VRDepthCullingPolicy.h"

namespace
{
	using namespace VRDepthCullingPolicy;

	constexpr bool CoversTemporalValidityBoundary()
	{
		return !IsViewCoherent(false, 1.0, 0.0f) &&
		       IsViewCoherent(true, kMinimumCoherentRotationCosine, kMaximumCoherentTranslationSquared) &&
		       !IsViewCoherent(true, kMinimumCoherentRotationCosine - 1e-12, 0.0f) &&
		       !IsViewCoherent(true, 1.0, kMaximumCoherentTranslationSquared + 1e-6f);
	}

	constexpr bool CoversConservativeVisibility()
	{
		return ResolveVisibility(0, false) == 1 &&
		       ResolveVisibility(0, true) == 0 &&
		       ResolveVisibility(1, false) == 1 &&
		       ResolveVisibility(17, false) == 17;
	}

	static_assert(CoversTemporalValidityBoundary());
	static_assert(CoversConservativeVisibility());
}

int main()
{
	return CoversTemporalValidityBoundary() && CoversConservativeVisibility() ? 0 : 1;
}
