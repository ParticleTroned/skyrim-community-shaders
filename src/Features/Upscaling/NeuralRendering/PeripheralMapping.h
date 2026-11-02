#pragma once

// Analytic curve adapted from Yuri Grib (BeliyG3), optimizer-fps-dlss5, MIT.
// See package/SKSE/Plugins/CommunityShaders/Licenses/NeuralRendering/PeripheralCompression.txt.

#include "FaceDetailMapping.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace NeuralRendering
{
	inline constexpr std::uint32_t kMaximumPeripheralCompressionPercent = 100;
	inline constexpr std::uint32_t kLegacyPeripheralCompressionPercent = 20;

	/** Invertible rectangular mapping shared by colour, guides and temporal identity. */
	struct PeripheralMapping
	{
		static constexpr std::uint32_t kVersion = 2;
		using Point = std::array<float, 2>;
		Point center{ 0.5f, 0.5f }, radii{ 0.5f, 0.5f };
		Point origin{}, work{ 1.0f, 1.0f };
		float protectedFraction = 1.0f, boundaryScale = 1.0f, edgeSlope = 1.0f;
		std::uint32_t reserved = 0;
#ifdef DEVBENCH_BRIDGE_ENABLED
		FaceDetailMapping faceDetail{};
#endif
		bool operator==(const PeripheralMapping&) const = default;

		/** The maximum axis distance preserves every corner of the rectangular source. */
		[[nodiscard]] float Distance(Point delta) const noexcept
		{
			const float x = std::abs(delta[0] / radii[0]), y = std::abs(delta[1] / radii[1]);
			return std::max(x, y);
		}
		[[nodiscard]] float ForwardRadius(float radius) const noexcept
		{
			if (radius <= protectedFraction)
				return radius;
			if (radius >= 1.0f)
				return boundaryScale + (radius - 1.0f) * edgeSlope;
			const float phase = (radius - protectedFraction) / (1.0f - protectedFraction);
			return protectedFraction + (boundaryScale - protectedFraction) * (2.0f * phase / (1.0f + phase));
		}
		[[nodiscard]] float InverseScale(float radius) const noexcept
		{
			if (radius <= protectedFraction)
				return 1.0f;
			if (radius >= boundaryScale)
				return (1.0f + (radius - boundaryScale) / edgeSlope) / radius;
			const float phase = (radius - protectedFraction) / (boundaryScale - protectedFraction);
			return (protectedFraction + (1.0f - protectedFraction) * phase / (2.0f - phase)) / radius;
		}
		[[nodiscard]] Point Forward(Point source) const noexcept
		{
			if (protectedFraction == 1.0f)
				return source;
			const Point delta{ source[0] - center[0], source[1] - center[1] };
			const float radius = Distance(delta);
			const float scale = radius > protectedFraction ? ForwardRadius(radius) / radius : 1.0f;
			Point result{ (center[0] + delta[0] * scale - origin[0]) / work[0],
				(center[1] + delta[1] * scale - origin[1]) / work[1] };
#ifdef DEVBENCH_BRIDGE_ENABLED
			for (unsigned axis = 0; axis < 2; ++axis)
				result[axis] = faceDetail.Forward(result[axis], axis);
#endif
			return result;
		}
		[[nodiscard]] Point Inverse(Point target) const noexcept
		{
			if (protectedFraction == 1.0f)
				return target;
#ifdef DEVBENCH_BRIDGE_ENABLED
			for (unsigned axis = 0; axis < 2; ++axis)
				target[axis] = faceDetail.Inverse(target[axis], axis);
#endif
			const Point delta{ origin[0] + target[0] * work[0] - center[0], origin[1] + target[1] * work[1] - center[1] };
			const float scale = InverseScale(Distance(delta));
			return { center[0] + delta[0] * scale, center[1] + delta[1] * scale };
		}
		/** Bounds contain the whole inverse-mapped cell, including crossings of either rectangle axis. */
		[[nodiscard]] std::pair<Point, Point> SourceBounds(Point lower, Point upper) const noexcept
		{
#ifdef DEVBENCH_BRIDGE_ENABLED
			for (unsigned axis = 0; axis < 2; ++axis) {
				lower[axis] = faceDetail.Inverse(lower[axis], axis);
				upper[axis] = faceDetail.Inverse(upper[axis], axis);
			}
#endif
			Point low{}, high{}, nearest{}, farthest{};
			for (unsigned axis = 0; axis < 2; ++axis) {
				low[axis] = origin[axis] + lower[axis] * work[axis] - center[axis];
				high[axis] = origin[axis] + upper[axis] * work[axis] - center[axis];
				nearest[axis] = low[axis] <= 0.0f && high[axis] >= 0.0f ? 0.0f : std::min(std::abs(low[axis]), std::abs(high[axis]));
				farthest[axis] = std::max(std::abs(low[axis]), std::abs(high[axis]));
			}
			const float nearScale = InverseScale(Distance(nearest)), farScale = InverseScale(Distance(farthest));
			for (unsigned axis = 0; axis < 2; ++axis) {
				lower[axis] = center[axis] + std::min(low[axis] * nearScale, low[axis] * farScale);
				upper[axis] = center[axis] + std::max(high[axis] * nearScale, high[axis] * farScale);
			}
			return { lower, upper };
		}
	};
#ifdef DEVBENCH_BRIDGE_ENABLED
	static_assert(sizeof(PeripheralMapping) == 192);
	static_assert(offsetof(PeripheralMapping, faceDetail) == 48);
#else
	static_assert(sizeof(PeripheralMapping) == 48);
#endif
}
