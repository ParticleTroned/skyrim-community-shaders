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

	constexpr Vector NativeOverlayPoint(const Coordinate& a_point, const Coordinate& a_mouseScale,
		float a_width, float a_scaleX, float a_scaleY)
	{
		return { (a_point.u / a_mouseScale.u - 0.5f) * a_width * a_scaleX,
			(a_point.v - 0.5f * a_mouseScale.v) / a_mouseScale.u * a_width * a_scaleY, -2.0f };
	}

	constexpr bool CoversNativeOverlayAspect(float a_height)
	{
		const Coordinate mouseScale{ 1920.0f, a_height };
		std::array<Coordinate, 3> corners{};
		if (!TryGetOverlayCornerCoordinates(mouseScale, corners))
			return false;
		const float aspect = a_height / mouseScale.u;
		const Vector topLeft = NativeOverlayPoint(corners[0], mouseScale, 2.0f, 2.0f, 2.0f * aspect);
		const Surface surface{ topLeft,
			NativeOverlayPoint(corners[1], mouseScale, 2.0f, 2.0f, 2.0f * aspect) - topLeft,
			NativeOverlayPoint(corners[2], mouseScale, 2.0f, 2.0f, 2.0f * aspect) - topLeft };
		const float imageHeight = 4.0f * aspect * aspect;
		for (const float u : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
			for (const float v : { 0.0f, 0.125f, 0.5f, 0.875f, 1.0f }) {
				const Vector displayedDot{ (u - 0.5f) * 4.0f, (0.5f - v) * imageHeight, -2.0f };
				Hit hit{};
				if (!TryIntersect(surface, { displayedDot.x, displayedDot.y, 0.0f }, { 0.0f, 0.0f, -1.0f }, hit) ||
					!Near(hit.u, u) || !Near(hit.v, v))
					return false;
				const Vector endpoint = surface.topLeft + surface.right * hit.u + surface.down * hit.v;
				if (!Near(endpoint.x, displayedDot.x) || !Near(endpoint.y, displayedDot.y) || !Near(endpoint.z, displayedDot.z))
					return false;
			}
		}
		return true;
	}

	constexpr bool CoversHMDPresentationExtent(float a_scale, float a_height)
	{
		const Coordinate mouseScale{ 1920.0f, a_height };
		const float aspect = a_height / mouseScale.u;
		const Extent extent = GetNativeOverlayExtent(a_scale, a_scale, a_scale * aspect, aspect);
		const Vector nativeTopLeft = NativeOverlayPoint({ 0.0f, a_height }, mouseScale,
			a_scale, a_scale, a_scale * aspect);
		const Vector nativeTopRight = NativeOverlayPoint({ mouseScale.u, a_height }, mouseScale,
			a_scale, a_scale, a_scale * aspect);
		const Vector nativeBottomLeft = NativeOverlayPoint({ 0.0f, 0.0f }, mouseScale,
			a_scale, a_scale, a_scale * aspect);
		if (!Near(extent.width, nativeTopRight.x - nativeTopLeft.x) ||
			!Near(extent.height, nativeTopLeft.y - nativeBottomLeft.y))
			return false;
		const Surface inSceneSurface{ { -extent.width * 0.5f, extent.height * 0.5f, -2.0f },
			{ extent.width, 0.0f, 0.0f }, { 0.0f, -extent.height, 0.0f } };
		for (const float u : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
			for (const float v : { 0.0f, 0.125f, 0.5f, 0.875f, 1.0f }) {
				const Vector nativePoint = NativeOverlayPoint({ u * mouseScale.u, (1.0f - v) * mouseScale.v },
					mouseScale, a_scale, a_scale, a_scale * aspect);
				Hit hit{};
				if (!TryIntersect(inSceneSurface, { nativePoint.x, nativePoint.y, 0.0f },
						{ 0.0f, 0.0f, -1.0f }, hit) ||
					!Near(hit.u, u) || !Near(hit.v, v))
					return false;
			}
		}
		return true;
	}

	constexpr bool CoversHMDPresentationScales()
	{
		const Extent defaultExtent = GetNativeOverlayExtent(2.0f, 2.0f, 1.6875f, 0.84375f);
		if (!Near(defaultExtent.width, 4.0f) || !Near(defaultExtent.height, 2.84765625f))
			return false;
		for (const float scale : { 0.5f, 1.0f, 1.5f, 2.0f }) {
			if (!CoversHMDPresentationExtent(scale, 1620.0f) || !CoversHMDPresentationExtent(scale, 1080.0f))
				return false;
		}
		return true;
	}

	constexpr bool ReproducesDefaultMouseScaleDrift()
	{
		const Coordinate defaultScale{ 1.0f, 1.0f };
		std::array<Coordinate, 3> corners{};
		if (!TryGetOverlayCornerCoordinates(defaultScale, corners))
			return false;
		const Vector topLeft = NativeOverlayPoint(corners[0], defaultScale, 2.0f, 2.0f, 1.6875f);
		const Surface oldSurface{ topLeft,
			NativeOverlayPoint(corners[1], defaultScale, 2.0f, 2.0f, 1.6875f) - topLeft,
			NativeOverlayPoint(corners[2], defaultScale, 2.0f, 2.0f, 1.6875f) - topLeft };
		const float actualHalfHeight = 1.423828125f;
		const auto endpointY = [&](float a_v) { return (oldSurface.topLeft + oldSurface.down * a_v).y; };
		return Near(endpointY(0.5f), 0.0f) && endpointY(0.0f) > actualHalfHeight &&
		       endpointY(1.0f) < -actualHalfHeight;
	}

	constexpr bool RejectsInvalidCoordinateScales()
	{
		std::array<Coordinate, 3> corners{};
		return !TryGetOverlayCornerCoordinates({ 0.0f, 1620.0f }, corners) &&
		       !TryGetOverlayCornerCoordinates({ 1920.0f, -1.0f }, corners) &&
		       !TryGetOverlayCornerCoordinates({ std::numeric_limits<float>::infinity(), 1620.0f }, corners) &&
		       !TryGetOverlayCornerCoordinates({ 1920.0f, std::numeric_limits<float>::quiet_NaN() }, corners);
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
	static_assert(CoversNativeOverlayAspect(1620.0f));
	static_assert(CoversNativeOverlayAspect(1080.0f));
	static_assert(CoversHMDPresentationScales());
	static_assert(ReproducesDefaultMouseScaleDrift());
	static_assert(RejectsInvalidCoordinateScales());
}

int main()
{
	return CoversScaledSurface() && CoversSkewedSurface() && RejectsMisses() &&
	               CoversNativeOverlayAspect(1620.0f) && CoversNativeOverlayAspect(1080.0f) &&
	               CoversHMDPresentationScales() &&
	               ReproducesDefaultMouseScaleDrift() && RejectsInvalidCoordinateScales() ?
	           0 :
	           1;
}
