#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

namespace GrassOcclusionPolicy
{
	enum class ProjectionMode : uint32_t
	{
		Invalid,
		Orthographic,
		Perspective
	};

	struct Projection
	{
		float asymptote = 0;
		ProjectionMode mode = ProjectionMode::Invalid;
	};

	/** Classify uploaded column-vector depth once per bucket; oblique or invalid mappings fail open. */
	inline Projection ClassifyProjection(std::span<const float, 16> matrix)
	{
		for (const float value : matrix)
			if (!std::isfinite(value))
				return {};
		double wNorm = 0, zNorm = 0, product = 0;
		for (uint32_t axis = 0; axis < 3; ++axis) {
			const double z = matrix[8 + axis], w = matrix[12 + axis];
			wNorm += w * w;
			zNorm += z * z;
			product += z * w;
		}
		if (wNorm > std::numeric_limits<float>::max() || zNorm > std::numeric_limits<float>::max())
			return {};
		if (wNorm == 0)
			return matrix[15] > 0 && zNorm > 0 ? Projection{ 0, ProjectionMode::Orthographic } : Projection{};
		const float asymptote = static_cast<float>(product / wNorm);
		if (!std::isfinite(asymptote))
			return {};
		double residualSquared = 0;
		for (uint32_t axis = 0; axis < 3; ++axis) {
			const double residual = matrix[8 + axis] - double(asymptote) * matrix[12 + axis];
			residualSquared += residual * residual;
		}
		constexpr double epsilon = std::numeric_limits<float>::epsilon();
		if (residualSquared > 64 * epsilon * epsilon * zNorm)
			return {};
		return { asymptote, ProjectionMode::Perspective };
	}
}
