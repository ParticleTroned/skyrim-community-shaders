#include "Features/VR/WandSurfaceGeometry.h"

namespace
{
	using namespace WandSurfaceGeometry;

	constexpr bool Near(float a_left, float a_right, float a_tolerance = 1e-5f)
	{
		return Abs(a_left - a_right) <= a_tolerance;
	}

	constexpr bool CoversScaledSurface()
	{
		const Surface surface{ { -1.0f, 0.75f, -2.0f }, { 2.0f, 0.0f, 0.0f }, { 0.0f, -1.5f, 0.0f } };
		Hit hit{};
		return TryIntersect(surface, { 0.5f, 0.375f, 0.0f }, { 0.0f, 0.0f, -1.0f }, hit) &&
		       Near(hit.u, 0.75f) && Near(hit.v, 0.25f) && Near(hit.distance, 2.0f);
	}

	constexpr bool CoversSkewedSurface()
	{
		const Surface surface{ { 1.0f, 2.0f, -4.0f }, { 2.0f, 0.0f, 0.0f }, { 0.5f, -1.0f, 0.0f } };
		const Vector expected = surface.topLeft + surface.right * 0.2f + surface.down * 0.8f;
		Hit hit{};
		return TryIntersect(surface, { expected.x, expected.y, 1.0f }, { 0.0f, 0.0f, -1.0f }, hit) &&
		       Near(hit.u, 0.2f) && Near(hit.v, 0.8f) && Near(hit.distance, 5.0f);
	}

	constexpr bool RejectsMisses()
	{
		const Surface surface{ { -0.5f, 0.5f, -1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } };
		Hit hit{};
		return !TryIntersect(surface, { 0.75f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, hit) &&
		       !TryIntersect(surface, { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, hit) &&
		       !TryIntersect(surface, { 0.0f, 0.0f, -2.0f }, { 0.0f, 0.0f, -1.0f }, hit);
	}

	static_assert(CoversScaledSurface());
	static_assert(CoversSkewedSurface());
	static_assert(RejectsMisses());
}

int main()
{
	return CoversScaledSurface() && CoversSkewedSurface() && RejectsMisses() ? 0 : 1;
}
