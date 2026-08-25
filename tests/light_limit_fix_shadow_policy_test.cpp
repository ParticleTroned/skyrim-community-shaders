#include "Features/LightLimitFix/ShadowLightPolicy.h"

namespace
{
	using namespace LightLimitFixShadowPolicy;

	constexpr bool CoversShadowAdmissionPolicy()
	{
		if (!IsValidShadowMask(0) || !IsValidShadowMask(3) || IsValidShadowMask(4) || IsValidShadowMask(255))
			return false;

		if (ResolveEffectiveLodDimmer(false, true, 0.0f) != 0.0f)
			return false;

		if (ResolveEffectiveLodDimmer(true, true, 0.25f) != 0.25f)
			return false;

		if (ResolveEffectiveLodDimmer(true, true, 0.0f) != 1.0f)
			return false;

		if (ResolveEffectiveLodDimmer(true, false, 0.0f) != 0.0f)
			return false;

		return ResolveEffectiveLodDimmer(true, true, 0.4f) == 0.4f;
	}

	static_assert(CoversShadowAdmissionPolicy());
}

int main()
{
	return CoversShadowAdmissionPolicy() ? 0 : 1;
}
