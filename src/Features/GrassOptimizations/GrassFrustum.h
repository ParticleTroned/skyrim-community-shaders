#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <span>

namespace GrassFrustum
{
	using Matrix = std::array<float, 16>;
	using Planes = std::array<std::array<float, 4>, 6>;
	inline constexpr float kEdgePadding = 128.0f;

	/** @brief Compose camera projection and representative translation in the shader's matrix convention. */
	inline Matrix Compose(const Matrix& camera, const Matrix& world)
	{
		Matrix result{};
		for (size_t row = 0; row < 4; ++row)
			for (size_t column = 0; column < 4; ++column)
				for (size_t term = 0; term < 4; ++term)
					result[row * 4 + column] += camera[row * 4 + term] * world[term * 4 + column];
		return result;
	}

	/** @brief Use cached unjittered data only when its current projection matches the bound geometry. */
	inline Matrix SelectProjection(const Matrix& bound, const Matrix& world, const Matrix& current, const Matrix& unjittered)
	{
		const auto expected = Compose(current, world);
		for (size_t i = 0; i < bound.size(); ++i)
			if (!std::isfinite(bound[i]) || !std::isfinite(expected[i]) || !std::isfinite(unjittered[i]) ||
				std::abs(expected[i] - bound[i]) > 1e-5f * std::max(1.0f, std::abs(bound[i])))
				return bound;
		return Compose(unjittered, world);
	}

	/** @brief Extract inward normalized planes from the same row-major projection used by the GPU. */
	inline std::optional<Planes> Extract(const Matrix& matrix)
	{
		for (float value : matrix)
			if (!std::isfinite(value))
				return std::nullopt;
		Planes result{};
		bool usable = false;
		for (size_t i = 0; i < result.size(); ++i) {
			const size_t row = i / 2;
			for (size_t axis = 0; axis < 4; ++axis)
				result[i][axis] = i == 4 ? matrix[8 + axis] : matrix[12 + axis] + (i % 2 ? -1.0f : 1.0f) * matrix[row * 4 + axis];
			const float length = std::hypot(result[i][0], result[i][1], result[i][2]);
			if (!std::isfinite(length) || !std::isfinite(result[i][3]))
				return std::nullopt;
			if (length < 1e-8f) {
				result[i] = { 0, 0, 0, 1 };
				continue;
			}
			usable = true;
			for (auto& value : result[i])
				value /= length;
			result[i][3] += kEdgePadding;
		}
		return usable ? std::optional<Planes>(result) : std::nullopt;
	}

	/** @brief Retain invalid bounds and any box intersecting the padded frustum. */
	inline bool Visible(const Planes& planes, const std::array<float, 3>& lo, const std::array<float, 3>& hi)
	{
		for (size_t i = 0; i < lo.size(); ++i)
			if (!std::isfinite(lo[i]) || !std::isfinite(hi[i]) || lo[i] > hi[i])
				return true;
		for (const auto& plane : planes) {
			float distance = plane[3];
			for (size_t axis = 0; axis < lo.size(); ++axis)
				distance += plane[axis] * (plane[axis] >= 0 ? hi[axis] : lo[axis]);
			if (distance < 0)
				return false;
		}
		return true;
	}
	struct BoundsRelation
	{
		bool intersects = true;
		bool contains = false;
	};

	/** @brief Share the box transform between rejection and optional descendant acceptance. */
	template <bool Containment>
	inline BoundsRelation ClassifyDistance(const Matrix& matrix, const std::array<float, 3>& lo, const std::array<float, 3>& hi, float distance)
	{
		if (!std::isfinite(distance) || distance <= 0 || matrix[12] != 0 || matrix[13] != 0 || matrix[14] != 0 || matrix[15] != 1)
			return {};
		double nearestSquared = 0, farthestSquared = 0;
		for (size_t row = 0; row < 3; ++row) {
			double center = matrix[row * 4 + 3], extent = 0;
			for (size_t axis = 0; axis < 3; ++axis) {
				if (!std::isfinite(lo[axis]) || !std::isfinite(hi[axis]) || lo[axis] > hi[axis] || !std::isfinite(matrix[row * 4 + axis]))
					return {};
				center += double(matrix[row * 4 + axis]) * (double(lo[axis]) + hi[axis]) * .5;
				extent += std::abs(double(matrix[row * 4 + axis])) * (double(hi[axis]) - lo[axis]) * .5;
			}
			if (!std::isfinite(center) || !std::isfinite(extent))
				return {};
			const auto nearest = std::max(0.0, std::abs(center) - extent);
			nearestSquared += nearest * nearest;
			if constexpr (Containment) {
				const auto farthest = std::abs(center) + extent;
				farthestSquared += farthest * farthest;
			}
		}
		const double limit = double(distance + kEdgePadding) * (distance + kEdgePadding);
		return { nearestSquared <= limit, Containment && farthestSquared * 1.00001 <= limit };
	}

	/** @brief Retain invalid bounds and any box intersecting the padded distance limit. */
	inline bool WithinDistance(const Matrix& matrix, const std::array<float, 3>& lo, const std::array<float, 3>& hi, float distance)
	{
		return ClassifyDistance<false>(matrix, lo, hi, distance).intersects;
	}

	struct PixelProjection
	{
		std::array<float, 4> w{};
		double focal = 0;
		bool valid = false;
	};

	struct PixelLimits
	{
		double visibleScale = 0, containedScale = 0, costVisibleScale = 0, costContainedScale = 0;
		double rampDepth = std::numeric_limits<double>::infinity();
		bool valid = false;

		double Depth(float radius, bool contained) const
		{
			const double base = radius * (contained ? containedScale : visibleScale);
			const double cost = radius * (contained ? costContainedScale : costVisibleScale);
			return std::min(base, std::max(cost, rampDepth));
		}
	};

	/** @brief Cache pixel cutoffs, bounding the nearest-eye cost ramp for translation-only world transforms. */
	inline PixelLimits PreparePixelLimits(const PixelProjection& projection, float pixels, float costPixels = 0,
		float rampEnd = 0, std::span<const Matrix> worlds = {})
	{
		if (!projection.valid || !std::isfinite(projection.focal) || projection.focal < 0 || !std::isfinite(pixels) || pixels <= 1e-5f)
			return {};
		PixelLimits limits{ projection.focal * 1.00001 / (double(pixels) - 1e-5), projection.focal * .99999 / (double(pixels) + 1e-5) };
		limits.costVisibleScale = limits.visibleScale;
		limits.costContainedScale = limits.containedScale;
		limits.valid = true;
		if (!std::isfinite(costPixels) || costPixels <= pixels || !std::isfinite(rampEnd) || rampEnd < 0 || worlds.empty())
			return limits;
		const double reach = std::hypot(double(projection.w[0]), double(projection.w[1]), double(projection.w[2])) * (double(rampEnd) + kEdgePadding);
		double rampDepth = std::numeric_limits<double>::lowest();
		// The shader uses the nearest eye's distance, so either eye can keep the cost ramp incomplete.
		for (const auto& world : worlds) {
			for (size_t row = 0; row < 4; ++row)
				for (size_t column = 0; column < 4; ++column)
					if (!std::isfinite(world[row * 4 + column]) || (column != 3 && world[row * 4 + column] != float(row == column)))
						return limits;
			if (world[15] != 1)
				return limits;
			double offset = projection.w[3], magnitude = reach + std::abs(offset);
			for (size_t axis = 0; axis < 3; ++axis) {
				const double translation = double(projection.w[axis]) * world[axis * 4 + 3];
				offset -= translation;
				magnitude += std::abs(translation);
			}
			rampDepth = std::max(rampDepth, reach + offset + 1e-5 * (1 + magnitude));
		}
		limits.rampDepth = rampDepth;
		limits.costVisibleScale = projection.focal * 1.00001 / (double(costPixels) - 1e-5);
		limits.costContainedScale = projection.focal * .99999 / (double(costPixels) + 1e-5);
		return limits;
	}

	/** @brief Cache camera-only pixel-size work once per bucket and eye. */
	inline PixelProjection PreparePixelProjection(const Matrix& projection, float height)
	{
		if (!std::isfinite(height) || height <= 0)
			return {};
		for (float value : projection)
			if (!std::isfinite(value))
				return {};
		return { { projection[12], projection[13], projection[14], projection[15] },
			std::hypot(double(projection[4]), double(projection[5]), double(projection[6])) * height * .5, true };
	}

	/** @brief Share depth bounds between possible visibility and optional descendant acceptance. */
	template <bool Containment>
	inline BoundsRelation ClassifyPixelSize(const PixelProjection& projection, const PixelLimits& limits, const std::array<float, 3>& lo,
		const std::array<float, 3>& hi, float maximumRadius, float minimumRadius)
	{
		if (!limits.valid || !std::isfinite(maximumRadius) || maximumRadius <= 0)
			return {};
		double minimumW = projection.w[3], maximumW = minimumW, magnitude = std::abs(minimumW);
		for (size_t axis = 0; axis < 3; ++axis) {
			if (!std::isfinite(lo[axis]) || !std::isfinite(hi[axis]) || lo[axis] > hi[axis])
				return {};
			const double coefficient = projection.w[axis];
			minimumW += coefficient * (coefficient < 0 ? hi[axis] : lo[axis]);
			if constexpr (Containment)
				maximumW += coefficient * (coefficient < 0 ? lo[axis] : hi[axis]);
			magnitude += std::abs(coefficient) * std::max(std::abs(double(lo[axis])), std::abs(double(hi[axis])));
		}
		const double error = 1e-5 * (1 + magnitude);
		minimumW -= error;
		BoundsRelation result;
		if (minimumW > 1e-4) {
			const double depth = limits.Depth(maximumRadius, false);
			result.intersects = !std::isfinite(depth) || minimumW <= depth;
		}
		if constexpr (Containment) {
			maximumW += error;
			if (maximumW > 1e-4 && std::isfinite(minimumRadius) && minimumRadius > 0) {
				const double depth = limits.Depth(minimumRadius, true);
				result.contains = std::isfinite(depth) && maximumW <= depth;
			}
		}
		return result;
	}

	template <bool Containment>
	inline BoundsRelation ClassifyPixelSize(const PixelProjection& projection, const std::array<float, 3>& lo,
		const std::array<float, 3>& hi, float maximumRadius, float minimumRadius, float minimumPixels)
	{
		return ClassifyPixelSize<Containment>(projection, PreparePixelLimits(projection, minimumPixels), lo, hi, maximumRadius, minimumRadius);
	}

	/** @brief Retain invalid bounds and any run that can reach the pixel-size threshold. */
	inline bool AbovePixelThreshold(const PixelProjection& projection, const std::array<float, 3>& lo,
		const std::array<float, 3>& hi, float maximumRadius, float minimumPixels)
	{
		return ClassifyPixelSize<false>(projection, lo, hi, maximumRadius, 0, minimumPixels).intersects;
	}

	/** @brief Bound vertex reach using a conservative collision-overlap result. */
	inline float DeformationPadding(float reach, float wind, bool collision, float collisionStrength)
	{
		return reach * (collision ? 1.0f + 2.0f * std::max(1.0f, collisionStrength) : 1.0f) + wind;
	}

	/** @brief Expand for collision only when some root can reach its activation distance. */
	inline float DeformationPadding(const Matrix& world, const std::array<float, 3>& lo,
		const std::array<float, 3>& hi, float reach, float wind, float collisionDistance, float collisionStrength = 1.0f)
	{
		const bool collision = collisionDistance > 0 && WithinDistance(world, lo, hi, collisionDistance + reach);
		return DeformationPadding(reach, wind, collision, collisionStrength);
	}

}
