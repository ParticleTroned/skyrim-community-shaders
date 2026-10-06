#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

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
}
