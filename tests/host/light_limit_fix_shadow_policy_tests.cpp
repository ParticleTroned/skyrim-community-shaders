#include "Features/LightLimitFix/ShadowLightPolicy.h"

#include <catch2/catch_test_macros.hpp>

using namespace LightLimitFixShadowPolicy;

TEST_CASE("shadow mask accepts only the four available channels")
{
	CHECK(IsValidShadowMask(0));
	CHECK(IsValidShadowMask(3));
	CHECK_FALSE(IsValidShadowMask(4));
	CHECK_FALSE(IsValidShadowMask(255));
}

TEST_CASE("shadow LOD recovery preserves engine-controlled fades")
{
	CHECK(ResolveEffectiveLodDimmer(false, true, 0.0f) == 0.0f);
	CHECK(ResolveEffectiveLodDimmer(true, true, 0.25f) == 0.25f);
	CHECK(ResolveEffectiveLodDimmer(true, true, 0.0f) == 1.0f);
	CHECK(ResolveEffectiveLodDimmer(true, false, 0.0f) == 0.0f);
	CHECK(ResolveEffectiveLodDimmer(true, true, 0.4f) == 0.4f);
}
