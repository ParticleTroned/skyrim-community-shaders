#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace WandBeamGeometry
{
	struct ClipPoint
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float w = 1.0f;
	};

	struct Viewport
	{
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
	};

	struct ScreenBeam
	{
		std::array<float, 4> endpoints{};
		std::array<std::uint32_t, 2> origin{};
		std::array<std::uint32_t, 2> size{};
	};

	/** Clips before division so a near-eye wand cannot produce an unbounded draw. */
	inline bool Project(ClipPoint a_start, ClipPoint a_end, const Viewport& a_viewport,
		std::uint32_t a_targetWidth, std::uint32_t a_targetHeight, float a_radius, ScreenBeam& a_output)
	{
		const auto finitePoint = [](const ClipPoint& a_point) {
			return std::isfinite(a_point.x) && std::isfinite(a_point.y) && std::isfinite(a_point.z) && std::isfinite(a_point.w);
		};
		if (!finitePoint(a_start) || !finitePoint(a_end) ||
			!std::isfinite(a_viewport.x) || !std::isfinite(a_viewport.y) ||
			!std::isfinite(a_viewport.width) || !std::isfinite(a_viewport.height) ||
			!std::isfinite(a_radius) || a_radius <= 0.0f || a_radius > 16.0f ||
			a_viewport.width <= 0.0f || a_viewport.height <= 0.0f ||
			a_targetWidth == 0 || a_targetHeight == 0 || a_targetWidth > 16384 || a_targetHeight > 16384)
			return false;

		const auto distances = [](const ClipPoint& a_point) {
			return std::array<double, 7>{ double(a_point.w) - 1e-5, double(a_point.x) + a_point.w,
				double(a_point.w) - a_point.x, double(a_point.y) + a_point.w,
				double(a_point.w) - a_point.y, a_point.z, double(a_point.w) - a_point.z };
		};
		const auto startDistances = distances(a_start);
		const auto endDistances = distances(a_end);
		double first = 0.0;
		double last = 1.0;
		for (std::size_t plane = 0; plane < startDistances.size(); ++plane) {
			const double start = startDistances[plane];
			const double end = endDistances[plane];
			if (start < 0.0 && end < 0.0)
				return false;
			if (start < 0.0)
				first = std::max(first, start / (start - end));
			else if (end < 0.0)
				last = std::min(last, start / (start - end));
		}
		if (first > last)
			return false;

		const auto screenPoint = [&](double a_t) {
			const double w = double(a_start.w) + (double(a_end.w) - a_start.w) * a_t;
			const double x = double(a_start.x) + (double(a_end.x) - a_start.x) * a_t;
			const double y = double(a_start.y) + (double(a_end.y) - a_start.y) * a_t;
			return std::array<float, 2>{
				static_cast<float>(a_viewport.x + (x / w * 0.5 + 0.5) * a_viewport.width),
				static_cast<float>(a_viewport.y + (0.5 - y / w * 0.5) * a_viewport.height)
			};
		};
		const auto start = screenPoint(first);
		const auto end = screenPoint(last);
		if (!std::isfinite(start[0]) || !std::isfinite(start[1]) || !std::isfinite(end[0]) || !std::isfinite(end[1]))
			return false;
		const float left = std::max(0.0f, a_viewport.x);
		const float top = std::max(0.0f, a_viewport.y);
		const float right = std::min(float(a_targetWidth), a_viewport.x + a_viewport.width);
		const float bottom = std::min(float(a_targetHeight), a_viewport.y + a_viewport.height);
		if (std::floor(right) <= std::ceil(left) || std::floor(bottom) <= std::ceil(top))
			return false;
		const auto x0 = static_cast<std::uint32_t>(std::clamp(std::floor(std::min(start[0], end[0]) - a_radius - 1.0f), std::ceil(left), std::floor(right)));
		const auto y0 = static_cast<std::uint32_t>(std::clamp(std::floor(std::min(start[1], end[1]) - a_radius - 1.0f), std::ceil(top), std::floor(bottom)));
		const auto x1 = static_cast<std::uint32_t>(std::clamp(std::ceil(std::max(start[0], end[0]) + a_radius + 1.0f), std::ceil(left), std::floor(right)));
		const auto y1 = static_cast<std::uint32_t>(std::clamp(std::ceil(std::max(start[1], end[1]) + a_radius + 1.0f), std::ceil(top), std::floor(bottom)));
		if (x1 <= x0 || y1 <= y0)
			return false;
		a_output = { { start[0], start[1], end[0], end[1] }, { x0, y0 }, { x1 - x0, y1 - y0 } };
		return true;
	}
}
