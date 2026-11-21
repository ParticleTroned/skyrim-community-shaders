#ifdef NDEBUG
#	undef NDEBUG
#endif

#include "Features/LightLimitFix/EffectSpotlight.h"

#include <cassert>
#include <initializer_list>
#include <limits>

namespace
{
	using LightLimitFixDetail::EffectSpotlight;
	using LightLimitFixDetail::TryBuildEffectSpotlight;

	void CheckNativeProjectionPacking()
	{
		// Rotated, translated and asymmetric: a transpose mistake cannot pass as identity.
		const float matrix[4][4] = {
			{ 0, 0, 1, 1 },
			{ 2, 0, 0, 0 },
			{ 0, 3, 0, 0 },
			{ -40, -90, -10, -10 }
		};
		EffectSpotlight projection{};
		float halfFalloff = 0.0f;
		assert(TryBuildEffectSpotlight(matrix, 4.0f, projection, halfFalloff));
		assert((projection.clipX == std::array<float, 4>{ 0, 2, 0, -40 }));
		assert((projection.clipY == std::array<float, 4>{ 0, 0, 3, -90 }));
		assert((projection.clipW == std::array<float, 4>{ 1, 0, 0, -10 }));
		assert(halfFalloff == 2.0f);
	}

	void CheckInvalidNativeData()
	{
		float matrix[4][4] = {
			{ 1, 0, 0, 0 },
			{ 0, 1, 0, 0 },
			{ 0, 0, 1, 1 },
			{ 0, 0, -1, 0 }
		};
		EffectSpotlight projection{};
		float halfFalloff = 0.0f;
		assert(TryBuildEffectSpotlight(matrix, 2.0f, projection, halfFalloff));
		assert(halfFalloff == 1.0f);
		const auto validProjection = projection;
		for (float falloff : { 0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
			assert(!TryBuildEffectSpotlight(matrix, falloff, projection, halfFalloff));
		for (float invalid : { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
			matrix[3][0] = invalid;
			assert(!TryBuildEffectSpotlight(matrix, 2.0f, projection, halfFalloff));
		}
		matrix[3][0] = 0.0f;
		matrix[2][3] = 0.0f;
		assert(!TryBuildEffectSpotlight(matrix, 2.0f, projection, halfFalloff));
		matrix[2][3] = 1.0f;
		matrix[1][1] = 0.0f;
		matrix[0][1] = 1.0f;
		assert(!TryBuildEffectSpotlight(matrix, 2.0f, projection, halfFalloff));
		assert(projection.clipX == validProjection.clipX);
		assert(projection.clipY == validProjection.clipY);
		assert(projection.clipW == validProjection.clipW);
		assert(halfFalloff == 1.0f);
	}
}

int main()
{
	CheckNativeProjectionPacking();
	CheckInvalidNativeData();
}
