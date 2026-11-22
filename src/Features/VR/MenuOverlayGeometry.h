#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace MenuOverlayGeometry
{
	constexpr std::size_t MaxVertices = 12;
	constexpr std::uint32_t MaxTextureExtent = 16384;
	constexpr double MinTriangleArea = 1e-5;

	/** Float-transform clip coordinates with normalized menu UVs, interpolated in double precision. */
	struct Vertex
	{
		double x, y, z, w, u, v;
		bool operator==(const Vertex&) const = default;
	};

	struct Viewport
	{
		float x, y, width, height;
	};

	struct alignas(16) Constants
	{
		std::uint32_t targetSize[2]{};
		std::uint32_t dispatchOrigin[2]{};
		std::uint32_t dispatchSize[2]{};
		std::uint32_t vertexCount = 0;
		std::uint32_t padding = 0;
		std::array<std::array<float, 4>, MaxVertices> polygonPixels{};
		// XY carries UV/w; Z holds the reciprocal area of fan triangle (0, i, i+1).
		std::array<std::array<float, 4>, MaxVertices> polygonUV{};
		float menuMipLevel = 0.0f;
		float padding2[3]{};
	};
	static_assert(alignof(Constants) == 16 && sizeof(Constants) % 16 == 0);

	/** Clips a convex, perimeter-ordered textured polygon against the D3D frustum. */
	inline bool ClipToFrustum(std::array<Vertex, MaxVertices>& polygon, std::size_t& count)
	{
		std::array<Vertex, MaxVertices> clipped{};
		// A convex quad gains at most one vertex per plane. Keep positive w as
		// well as the D3D clip volume so no projected corner can cross infinity.
		for (unsigned plane = 0; plane < 7 && count; ++plane) {
			const auto distance = [plane](const Vertex& p) {
				switch (plane) {
				case 0:
					return p.w - 1e-5;
				case 1:
					return p.z;
				case 2:
					return p.w - p.z;
				case 3:
					return p.w + p.x;
				case 4:
					return p.w - p.x;
				case 5:
					return p.w + p.y;
				default:
					return p.w - p.y;
				}
			};
			std::array<double, MaxVertices> distances{};
			std::size_t outside = 0;
			for (std::size_t i = 0; i < count; ++i) {
				distances[i] = distance(polygon[i]);
				outside += distances[i] < 0;
			}
			if (outside == count)
				return false;
			if (!outside)
				continue;
			std::size_t written = 0;
			const auto append = [&](const Vertex& p) {
				if (written && clipped[written - 1] == p)
					return true;
				if (written == clipped.size())
					return false;
				clipped[written++] = p;
				return true;
			};
			Vertex previous = polygon[count - 1];
			double previousDistance = distances[count - 1];
			for (std::size_t i = 0; i < count; ++i) {
				const auto current = polygon[i];
				const double currentDistance = distances[i];
				if ((previousDistance >= 0) != (currentDistance >= 0)) {
					const double t = previousDistance / (previousDistance - currentDistance);
					const auto mix = [t](double a, double b) { return a + t * (b - a); };
					Vertex intersection = previousDistance == 0 ? previous : currentDistance == 0 ? current :
					                                                                                Vertex{ mix(previous.x, current.x), mix(previous.y, current.y), mix(previous.z, current.z), mix(previous.w, current.w), mix(previous.u, current.u), mix(previous.v, current.v) };
					switch (plane) {
					case 0:
						intersection.w = 1e-5;
						break;
					case 1:
						intersection.z = 0;
						break;
					case 2:
						intersection.z = intersection.w;
						break;
					case 3:
						intersection.x = -intersection.w;
						break;
					case 4:
						intersection.x = intersection.w;
						break;
					case 5:
						intersection.y = -intersection.w;
						break;
					default:
						intersection.y = intersection.w;
						break;
					}
					if (!append(intersection))
						return false;
				}
				if (currentDistance >= 0 && !append(current))
					return false;
				previous = current;
				previousDistance = currentDistance;
			}
			if (written > 1 && clipped[0] == clipped[written - 1])
				--written;
			polygon = clipped;
			count = written;
		}
		return count >= 3;
	}

	/** Clip the menu before perspective division, retaining UVs and the selected eye's bounds. */
	inline bool Project(const std::array<Vertex, 4>& a_quad, const Viewport& a_viewport,
		std::uint32_t a_width, std::uint32_t a_height, Constants& a_output, float& a_pixelsPerUVArea)
	{
		a_output = {};
		a_pixelsPerUVArea = 0.0f;
		if (!a_width || !a_height || a_width > MaxTextureExtent || a_height > MaxTextureExtent ||
			!std::isfinite(a_viewport.x) || !std::isfinite(a_viewport.y) ||
			!std::isfinite(a_viewport.width) || !std::isfinite(a_viewport.height) ||
			a_viewport.width <= 0 || a_viewport.height <= 0 ||
			std::abs(a_viewport.x) > MaxTextureExtent || std::abs(a_viewport.y) > MaxTextureExtent ||
			a_viewport.width > MaxTextureExtent || a_viewport.height > MaxTextureExtent)
			return false;

		std::array<Vertex, MaxVertices> polygon{};
		std::size_t count = a_quad.size();
		for (std::size_t i = 0; i < count; ++i) {
			const auto& p = a_quad[i];
			if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
				!std::isfinite(p.w) || !std::isfinite(p.u) || !std::isfinite(p.v) ||
				std::abs(p.x) > std::numeric_limits<float>::max() || std::abs(p.y) > std::numeric_limits<float>::max() ||
				std::abs(p.z) > std::numeric_limits<float>::max() || std::abs(p.w) > std::numeric_limits<float>::max() ||
				p.u < 0 || p.u > 1 || p.v < 0 || p.v > 1)
				return false;
			polygon[i] = p;
		}

		if (!ClipToFrustum(polygon, count))
			return false;

		Constants result{};
		double minX = std::numeric_limits<double>::max(), minY = minX;
		double maxX = std::numeric_limits<double>::lowest(), maxY = maxX;
		double twicePixelArea = 0, twiceUVArea = 0;
		for (std::size_t i = 0; i < count; ++i) {
			const auto& p = polygon[i];
			const double x = a_viewport.x + (p.x / p.w * 0.5 + 0.5) * a_viewport.width;
			const double y = a_viewport.y + (0.5 - p.y / p.w * 0.5) * a_viewport.height;
			result.polygonPixels[i] = { float(x), float(y), float(1.0 / p.w), 0 };
			result.polygonUV[i] = { float(p.u / p.w), float(p.v / p.w), 0, 0 };
			for (std::size_t component = 0; component < 4; ++component) {
				if (!std::isfinite(result.polygonPixels[i][component]) || !std::isfinite(result.polygonUV[i][component]))
					return false;
			}
			minX = std::min(minX, x);
			minY = std::min(minY, y);
			maxX = std::max(maxX, x);
			maxY = std::max(maxY, y);
		}
		for (std::size_t i = 1; i + 1 < count; ++i) {
			const auto& a = result.polygonPixels[0];
			const auto& b = result.polygonPixels[i];
			const auto& c = result.polygonPixels[i + 1];
			const double area = (double(b[0]) - a[0]) * (double(c[1]) - a[1]) - (double(b[1]) - a[1]) * (double(c[0]) - a[0]);
			result.polygonUV[i][2] = std::abs(area) >= MinTriangleArea ? float(1.0 / area) : 0.0f;
			twicePixelArea += area;
			const auto& u = polygon[0];
			const auto& v = polygon[i];
			const auto& w = polygon[i + 1];
			twiceUVArea += (v.u - u.u) * (w.v - u.v) - (v.v - u.v) * (w.u - u.u);
		}
		if (!std::isfinite(twicePixelArea) || !std::isfinite(twiceUVArea) ||
			std::abs(twicePixelArea) < MinTriangleArea || std::abs(twiceUVArea) < 1e-12)
			return false;

		// Pixel centres, rather than whole pixel cells, determine viewport coverage.
		const double left = std::max(0.0, std::ceil(double(a_viewport.x) - 0.5));
		const double top = std::max(0.0, std::ceil(double(a_viewport.y) - 0.5));
		const double right = std::min(double(a_width), std::ceil(double(a_viewport.x) + a_viewport.width - 0.5));
		const double bottom = std::min(double(a_height), std::ceil(double(a_viewport.y) + a_viewport.height - 0.5));
		if (right <= left || bottom <= top)
			return false;
		const auto x0 = std::uint32_t(std::clamp(std::floor(minX) - 1, left, right));
		const auto y0 = std::uint32_t(std::clamp(std::floor(minY) - 1, top, bottom));
		const auto x1 = std::uint32_t(std::clamp(std::ceil(maxX) + 1, left, right));
		const auto y1 = std::uint32_t(std::clamp(std::ceil(maxY) + 1, top, bottom));
		if (x1 <= x0 || y1 <= y0)
			return false;
		result.targetSize[0] = a_width;
		result.targetSize[1] = a_height;
		result.dispatchOrigin[0] = x0;
		result.dispatchOrigin[1] = y0;
		result.dispatchSize[0] = x1 - x0;
		result.dispatchSize[1] = y1 - y0;
		result.vertexCount = static_cast<std::uint32_t>(count);
		const float pixelsPerUVArea = float(std::abs(twicePixelArea / twiceUVArea));
		if (!std::isfinite(pixelsPerUVArea) || pixelsPerUVArea <= 0)
			return false;
		a_pixelsPerUVArea = pixelsPerUVArea;
		a_output = result;
		return true;
	}
}
