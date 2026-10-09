#pragma once

#include "FoveatedMaskVisualization.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace FoveatedMaskCalibration
{
	constexpr float kMinimumOffset = FoveatedCommon::kMaskOffsetMin;
	constexpr float kMaximumOffset = FoveatedCommon::kMaskOffsetMax;
	constexpr double kCoverageMargin = 1e-5;
	constexpr float kMinimumPercent = 70.0f;
	constexpr float kMaximumPercent = 130.0f;
	struct Point
	{
		double x = 0.0, y = 0.0;
	};
	using Projection = std::array<double, 9>;
	using Polygon = std::vector<Point>;

	struct Geometry
	{
		float scale = 1.0f;
		float horizontalScale = 1.0f;
		std::array<float, 4> centers{ 0.5f, 0.5f, 0.5f, 0.5f };
		bool operator==(const Geometry&) const = default;
	};

	/** Immutable outer-edge reference; inward mask edges are intentionally not constraints. */
	struct Reference
	{
		uint32_t version = 0;
		Geometry outer;
		Projection leftToRight{};
		bool peripheryTaa = false;
		bool fullImage = false;
		float centerScale = 0.3f;
		float feather = FoveatedCommon::kCenterFeather;
		bool operator==(const Reference&) const = default;
	};

	struct Solution
	{
		Geometry geometry;
		float areaPercent = 100.0f;
		bool fullImage = false;
	};

	/** Converts the preview's direction-only clip projection to eye UV coordinates. */
	template <class ClipMatrix>
	[[nodiscard]] inline Projection FromClipProjection(const ClipMatrix& m)
	{
		return {
			m._11 + m._41, -m._12 - m._42, 0.5 * (-m._11 + m._12 + 0.5 * m._13 + m._14 - m._41 + m._42 + 0.5 * m._43 + m._44),
			-m._21 + m._41, m._22 - m._42, 0.5 * (m._21 - m._22 - 0.5 * m._23 - m._24 - m._41 + m._42 + 0.5 * m._43 + m._44),
			2.0 * m._41, -2.0 * m._42, -m._41 + m._42 + 0.5 * m._43 + m._44
		};
	}

	[[nodiscard]] inline std::optional<Point> Project(const Projection& matrix, Point point)
	{
		const double w = matrix[6] * point.x + matrix[7] * point.y + matrix[8];
		if (!std::isfinite(w) || w <= 1e-6)
			return std::nullopt;
		Point result{ (matrix[0] * point.x + matrix[1] * point.y + matrix[2]) / w,
			(matrix[3] * point.x + matrix[4] * point.y + matrix[5]) / w };
		return std::isfinite(result.x) && std::isfinite(result.y) ? std::optional{ result } : std::nullopt;
	}

	[[nodiscard]] inline std::optional<Projection> Inverse(const Projection& m)
	{
		for (double value : m)
			if (!std::isfinite(value) || std::abs(value) > 100.0)
				return std::nullopt;
		Projection result{ m[4] * m[8] - m[5] * m[7], m[2] * m[7] - m[1] * m[8], m[1] * m[5] - m[2] * m[4],
			m[5] * m[6] - m[3] * m[8], m[0] * m[8] - m[2] * m[6], m[2] * m[3] - m[0] * m[5],
			m[3] * m[7] - m[4] * m[6], m[1] * m[6] - m[0] * m[7], m[0] * m[4] - m[1] * m[3] };
		const double determinant = m[0] * result[0] + m[1] * result[3] + m[2] * result[6];
		if (!std::isfinite(determinant) || std::abs(determinant) < 1e-6)
			return std::nullopt;
		for (double& value : result)
			value /= determinant;
		return result;
	}

	[[nodiscard]] inline bool IsValid(const Reference& reference)
	{
		const auto finiteRange = [](float value, float minimum, float maximum) {
			return std::isfinite(value) && value >= minimum && value <= maximum;
		};
		if (reference.version != 1 || !Inverse(reference.leftToRight) ||
			!finiteRange(reference.outer.scale, 0.25f, 1.2f) ||
			!finiteRange(reference.outer.horizontalScale, 1.0f, 2.0f) ||
			!finiteRange(reference.centerScale, 0.25f, 1.0f) || !finiteRange(reference.feather, 0.0f, 0.1f))
			return false;
		for (float value : reference.outer.centers)
			if (!finiteRange(value, 0.5f + kMinimumOffset, 0.5f + kMaximumOffset))
				return false;
		return true;
	}

	/** Changing mode or feather ownership invalidates the fitted reference. */
	[[nodiscard]] inline bool MatchesProfile(const Reference& reference, bool peripheryTaa, float centerScale, float feather)
	{
		return IsValid(reference) && reference.peripheryTaa == peripheryTaa && std::isfinite(feather) &&
		       std::abs(reference.feather - feather) <= 1e-6f &&
		       (!peripheryTaa || (std::isfinite(centerScale) && std::abs(reference.centerScale - centerScale) <= 1e-6f));
	}

	inline Polygon ClipHalfPlane(Polygon polygon, double nx, double ny, double offset)
	{
		Polygon result;
		if (polygon.empty())
			return result;
		const auto distance = [&](Point point) { return nx * point.x + ny * point.y + offset; };
		Point previous = polygon.back();
		double previousDistance = distance(previous);
		for (Point point : polygon) {
			const double currentDistance = distance(point);
			if ((currentDistance >= 0.0) != (previousDistance >= 0.0)) {
				const double t = previousDistance / (previousDistance - currentDistance);
				result.push_back({ previous.x + t * (point.x - previous.x), previous.y + t * (point.y - previous.y) });
			}
			if (currentDistance >= 0.0)
				result.push_back(point);
			previous = point;
			previousDistance = currentDistance;
		}
		return result;
	}

	inline Polygon Clip(Polygon polygon, unsigned axis, double boundary, bool keepGreater)
	{
		const double direction = keepGreater ? 1.0 : -1.0;
		return ClipHalfPlane(std::move(polygon), axis == 0 ? direction : 0.0, axis == 1 ? direction : 0.0, -boundary * direction);
	}

	inline Polygon ClipEye(Polygon polygon)
	{
		for (unsigned axis = 0; axis < 2; ++axis) {
			polygon = Clip(std::move(polygon), axis, 0.0, true);
			polygon = Clip(std::move(polygon), axis, 1.0, false);
		}
		return polygon;
	}

	/** Clip in homogeneous space before dividing; monocular directions may lie behind the peer eye. */
	inline Polygon ClipProjectedEye(Polygon polygon, const Projection& matrix)
	{
		polygon = ClipHalfPlane(std::move(polygon), matrix[6], matrix[7], matrix[8] - 2e-6);
		for (unsigned row : { 0u, 3u }) {
			polygon = ClipHalfPlane(std::move(polygon), matrix[row], matrix[row + 1], matrix[row + 2]);
			polygon = ClipHalfPlane(std::move(polygon), matrix[6] - matrix[row], matrix[7] - matrix[row + 1], matrix[8] - matrix[row + 2]);
		}
		return polygon;
	}

	[[nodiscard]] inline Projection Multiply(const Projection& a, const Projection& b)
	{
		Projection result{};
		for (unsigned row = 0; row < 3; ++row)
			for (unsigned column = 0; column < 3; ++column)
				for (unsigned i = 0; i < 3; ++i)
					result[row * 3 + column] += a[row * 3 + i] * b[i * 3 + column];
		return result;
	}

	/** Saved mapping may be reused only while both eyes remain inside the fit margin. */
	[[nodiscard]] inline bool MatchesProjection(const Projection& saved, const Projection& current)
	{
		const auto savedInverse = Inverse(saved), currentInverse = Inverse(current);
		if (!savedInverse || !currentInverse)
			return false;
		const auto withinMargin = [](const Projection& saved, const Projection& current) {
			const auto normalize = [](Projection m) {
				const double w = m[6] * 0.5 + m[7] * 0.5 + m[8];
				for (double& value : m)
					value /= w;
				return m;
			};
			if (!Project(saved, { 0.5, 0.5 }) || !Project(current, { 0.5, 0.5 }))
				return false;
			const auto a = normalize(saved), b = normalize(current);
			double minW = 1.0, deltaW = 0.0;
			Point delta{}, maximum{};
			const Polygon eye{ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
			auto overlap = ClipProjectedEye(eye, a);
			const auto currentOverlap = ClipProjectedEye(eye, b);
			overlap.insert(overlap.end(), currentOverlap.begin(), currentOverlap.end());
			if (overlap.empty())
				return a == b;
			for (Point point : overlap) {
				if (!Project(saved, point) || !Project(current, point))
					return false;
				const auto mapped = Project(a, point);
				if (!mapped)
					return false;
				maximum.x = std::max(maximum.x, std::abs(mapped->x));
				maximum.y = std::max(maximum.y, std::abs(mapped->y));
				minW = std::min(minW, b[6] * point.x + b[7] * point.y + b[8]);
				deltaW = std::max(deltaW, std::abs((b[6] - a[6]) * point.x + (b[7] - a[7]) * point.y + b[8] - a[8]));
				delta.x = std::max(delta.x, std::abs((b[0] - a[0]) * point.x + (b[1] - a[1]) * point.y + b[2] - a[2]));
				delta.y = std::max(delta.y, std::abs((b[3] - a[3]) * point.x + (b[4] - a[4]) * point.y + b[5] - a[5]));
			}
			// Linear denominators attain extrema at overlap vertices, bounding every shared direction.
			return (delta.x + maximum.x * deltaW) / minW <= kCoverageMargin * 0.25 &&
			       (delta.y + maximum.y * deltaW) / minW <= kCoverageMargin * 0.25;
		};
		return withinMargin(saved, current) && withinMargin(*savedInverse, *currentInverse);
	}

	inline Polygon ConvexHull(Polygon points)
	{
		if (points.size() < 3)
			return points;
		std::sort(points.begin(), points.end(), [](Point a, Point b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
		const auto cross = [](Point a, Point b, Point c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
		Polygon hull;
		for (Point point : points) {
			while (hull.size() >= 2 && cross(hull[hull.size() - 2], hull.back(), point) <= 0.0)
				hull.pop_back();
			hull.push_back(point);
		}
		const auto lowerSize = hull.size();
		for (auto it = points.rbegin() + 1; it != points.rend(); ++it) {
			while (hull.size() > lowerSize && cross(hull[hull.size() - 2], hull.back(), *it) <= 0.0)
				hull.pop_back();
			hull.push_back(*it);
		}
		if (!hull.empty())
			hull.pop_back();
		return hull;
	}

	/** Circumscribed support lines preserve the curved outer contour between samples. */
	inline Polygon OuterHalf(const Reference& reference, unsigned eye)
	{
		const double cx = reference.outer.centers[eye * 2], cy = reference.outer.centers[eye * 2 + 1];
		const double rx = reference.outer.scale * reference.outer.horizontalScale * 0.5;
		const double ry = reference.outer.scale * 0.5;
		Polygon polygon = reference.fullImage ? Polygon{ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } } :
		                                        Polygon{ { cx - rx, cy - ry }, { cx + rx, cy - ry }, { cx + rx, cy + ry }, { cx - rx, cy + ry } };
		if (!reference.fullImage) {
			constexpr unsigned kSupportLines = 64;
			for (unsigned i = 0; i < kSupportLines; ++i) {
				const double angle = i * 6.283185307179586 / kSupportLines;
				const double nx = std::cos(angle), ny = std::sin(angle);
				const double support = std::pow(std::pow(std::abs(nx * rx), 4.0 / 3.0) + std::pow(std::abs(ny * ry), 4.0 / 3.0), 0.75);
				polygon = ClipHalfPlane(std::move(polygon), -nx, -ny, support + nx * cx + ny * cy);
			}
		}
		return ClipEye(Clip(std::move(polygon), 0, cx, eye != 0));
	}

	/** Joins outward contours in a common view, filling inward gaps without retaining old overlap. */
	[[nodiscard]] inline std::optional<std::array<Polygon, 2>> BuildTargets(const Reference& reference, float percent)
	{
		if (!IsValid(reference) || !std::isfinite(percent) || percent < kMinimumPercent || percent > kMaximumPercent)
			return std::nullopt;
		if (reference.fullImage && percent >= 100.0f)
			return std::array<Polygon, 2>{ Polygon{ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } }, Polygon{ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } } };
		const auto inverse = *Inverse(reference.leftToRight);
		const double centerW = reference.leftToRight[6] * 0.5 + reference.leftToRight[7] * 0.5 + reference.leftToRight[8];
		if (!std::isfinite(centerW) || centerW <= 1e-6)
			return std::nullopt;
		Projection leftToCommon = reference.leftToRight;
		for (unsigned i = 0; i < leftToCommon.size(); ++i)
			leftToCommon[i] = (leftToCommon[i] / centerW + (i % 4 == 0 ? 1.0 : 0.0)) * 0.5;
		const auto commonToLeft = Inverse(leftToCommon);
		if (!commonToLeft)
			return std::nullopt;
		const auto rightToCommon = Multiply(leftToCommon, inverse);
		Polygon common;
		for (unsigned eye = 0; eye < 2; ++eye) {
			for (Point point : OuterHalf(reference, eye)) {
				const auto mapped = Project(eye == 0 ? leftToCommon : rightToCommon, point);
				if (!mapped)
					return std::nullopt;
				common.push_back(*mapped);
			}
		}
		if (common.size() < 3)
			return std::nullopt;
		common = ConvexHull(std::move(common));
		const auto leftCenter = Project(leftToCommon, { 0.5, 0.5 }), rightCenter = Project(rightToCommon, { 0.5, 0.5 });
		if (!leftCenter || !rightCenter)
			return std::nullopt;
		const Point anchor{ (leftCenter->x + rightCenter->x) * 0.5, (leftCenter->y + rightCenter->y) * 0.5 };
		for (Point& point : common) {
			point.x = anchor.x + (point.x - anchor.x) * percent / 100.0;
			point.y = anchor.y + (point.y - anchor.y) * percent / 100.0;
		}
		std::array<Polygon, 2> targets;
		for (unsigned eye = 0; eye < 2; ++eye) {
			const auto matrix = eye == 0 ? *commonToLeft : Multiply(reference.leftToRight, *commonToLeft);
			for (Point point : ClipProjectedEye(common, matrix)) {
				const auto mapped = Project(matrix, point);
				if (!mapped)
					return std::nullopt;
				targets[eye].push_back(*mapped);
			}
		}
		if (targets[0].size() < 3 || targets[1].size() < 3)
			return std::nullopt;
		return targets;
	}

	/** Fits a convex target inside a fixed superellipse within both offset limits. */
	inline std::optional<Point> FitEye(const Polygon& points, double radiusY, double horizontalScale, double centerScale, unsigned eye)
	{
		const double outward = centerScale * 0.5 * (horizontalScale - 1.0) * (eye == 0 ? -1.0 : 1.0);
		const double minX = std::max(0.5 + kMinimumOffset, 0.5 + kMinimumOffset + outward), maxX = std::min(0.5 + kMaximumOffset, 0.5 + kMaximumOffset + outward);
		double minY = 0.5 + kMinimumOffset, maxY = 0.5 + kMaximumOffset;
		for (Point point : points) {
			minY = std::max(minY, point.y - radiusY);
			maxY = std::min(maxY, point.y + radiusY);
		}
		if (minX > maxX || minY > maxY)
			return std::nullopt;
		const auto interval = [&](double cy) {
			double left = minX, right = maxX;
			for (Point point : points) {
				const double y = (point.y - cy) / radiusY;
				const double halfWidth = radiusY * horizontalScale * std::sqrt(std::sqrt(std::max(0.0, 1.0 - y * y * y * y)));
				left = std::max(left, point.x - halfWidth);
				right = std::min(right, point.x + halfWidth);
			}
			return std::array{ left, right };
		};
		// The gap between intersected centre intervals is convex in centre Y.
		for (unsigned step = 0; step < 28; ++step) {
			const double a = (2.0 * minY + maxY) / 3.0, b = (minY + 2.0 * maxY) / 3.0;
			const auto ia = interval(a), ib = interval(b);
			if (ia[0] - ia[1] < ib[0] - ib[1])
				maxY = b;
			else
				minY = a;
		}
		const double cy = (minY + maxY) * 0.5;
		const auto range = interval(cy);
		if (range[0] > range[1])
			return std::nullopt;
		return Point{ (range[0] + range[1]) * 0.5, cy };
	}

	/** Bounded width search with a convex position fit and binary scale refinement. */
	[[nodiscard]] inline std::optional<Solution> Solve(const Reference& reference, float percent)
	{
		const auto targets = BuildTargets(reference, percent);
		if (!targets)
			return std::nullopt;
		Solution best{ .geometry = {}, .areaPercent = 100.0f, .fullImage = true };
		const float minimumScale = reference.peripheryTaa ? std::min(1.0f, std::max(0.3f, reference.centerScale)) : 0.25f;
		const auto fit = [&](float scale, float horizontal) -> std::optional<Solution> {
			const float center = reference.peripheryTaa ? reference.centerScale : scale;
			const float visible = reference.peripheryTaa ? std::max(scale, center + 2.0f * std::max(reference.feather, FoveatedCommon::kMinimumFeather)) : scale + 2.0f * std::max(reference.feather, FoveatedCommon::kMinimumFeather);
			Solution result{ .geometry = { scale, horizontal, {} }, .areaPercent = 0.0f };
			for (unsigned eye = 0; eye < 2; ++eye) {
				const auto position = FitEye((*targets)[eye], visible * 0.5 - kCoverageMargin, horizontal, center, eye);
				if (!position)
					return std::nullopt;
				result.geometry.centers[eye * 2] = static_cast<float>(position->x);
				result.geometry.centers[eye * 2 + 1] = static_cast<float>(position->y);
			}
			return result;
		};
		float firstWidth = 1.0f, lastWidth = 2.0f, increment = 0.025f;
		for (unsigned pass = 0; pass < 3; ++pass) {
			for (float horizontal = firstWidth; horizontal <= lastWidth + increment * 0.1f; horizontal += increment) {
				horizontal = std::min(horizontal, 2.0f);
				float low = minimumScale, high = reference.peripheryTaa ? 1.0f : 0.9989f;
				auto candidate = fit(high, horizontal);
				if (!candidate)
					continue;
				for (unsigned step = 0; step < 15; ++step) {
					const float middle = (low + high) * 0.5f;
					if (const auto current = fit(middle, horizontal)) {
						high = middle;
						candidate = current;
					} else {
						low = middle;
					}
				}
				const float center = reference.peripheryTaa ? reference.centerScale : candidate->geometry.scale;
				for (unsigned eye = 0; eye < 2; ++eye)
					candidate->areaPercent += FoveatedMaskVisualization::MeasureCoverage(center, reference.feather, horizontal,
												  candidate->geometry.centers[eye * 2] - 0.5f, candidate->geometry.centers[eye * 2 + 1] - 0.5f,
												  reference.peripheryTaa, candidate->geometry.scale)
					                              .totalPercent *
					                          0.5f;
				// A full-image TAA fit is valid even when it saves no area.
				if (candidate->areaPercent < best.areaPercent || (reference.peripheryTaa && best.fullImage))
					best = *candidate;
			}
			firstWidth = std::max(1.0f, best.geometry.horizontalScale - increment);
			lastWidth = std::min(2.0f, best.geometry.horizontalScale + increment);
			increment /= 5.0f;
		}
		if (best.fullImage && reference.peripheryTaa && FoveatedCommon::IsActiveCoverage(reference.centerScale))
			return std::nullopt;
		return best;
	}
}
