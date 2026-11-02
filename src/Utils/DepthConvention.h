#pragma once

#include <cmath>
#include <cstdint>

namespace Depth
{
	/** Encoding of normalized scene device depth; linear distances never reverse. */
	enum class Convention : std::uint8_t
	{
		Standard,
		Reversed
	};
	constexpr float Near(Convention order) { return order == Convention::Reversed ? 1.0f : 0.0f; }
	constexpr float Far(Convention order) { return 1.0f - Near(order); }
	/** The cached upload describes the frame's encoding, including standard startup menus. */
	template <class Matrix>
	Convention FromUploadedProjection(const Matrix& matrix, bool reverseZActive)
	{
		return reverseZActive && matrix.m[2][2] * matrix.m[3][2] < 0.0f ? Convention::Reversed : Convention::Standard;
	}
	/** Spatial rows retain perspective depth orientation through view rotation and translation. */
	template <class Matrix>
	float UploadedDepthForward(const Matrix& matrix)
	{
		float forward = 0;
		for (int axis = 0; axis < 3; ++axis)
			forward += matrix.m[2][axis] * matrix.m[3][axis];
		return forward;
	}
	/** A previous camera can have a different convention from the current camera. */
	template <class Matrix>
	Convention FromUploadedViewProjection(const Matrix& matrix, bool reverseZActive)
	{
		return reverseZActive && UploadedDepthForward(matrix) < 0.0f ? Convention::Reversed : Convention::Standard;
	}
	inline bool IsValidRange(double nearPlane, double farPlane)
	{
		return std::isfinite(nearPlane) && std::isfinite(farPlane) && nearPlane > 0 && farPlane > nearPlane;
	}
	constexpr double Linearize(double depth, double nearPlane, double farPlane, Convention order)
	{
		return nearPlane * farPlane / (order == Convention::Reversed ? nearPlane + depth * (farPlane - nearPlane) : farPlane - depth * (farPlane - nearPlane));
	}
	/** Construct the reverse depth row directly, avoiding 1-A cancellation. */
	template <class Matrix>
	void SetReverseProjection(Matrix& matrix, double nearPlane, double farPlane)
	{
		const auto scale = -nearPlane / (farPlane - nearPlane);
		const auto offset = nearPlane * farPlane / (farPlane - nearPlane);
		for (int row = 0; row < 4; ++row)
			matrix.m[row][2] = static_cast<float>(scale * matrix.m[row][3] + (row == 3 ? offset : 0));
	}
	/** Uploaded column-vector matrices use the transpose of the engine representation. */
	template <class Matrix>
	void SetReverseUpload(Matrix& matrix, double nearPlane, double farPlane)
	{
		const auto scale = -nearPlane / (farPlane - nearPlane);
		const auto offset = nearPlane * farPlane / (farPlane - nearPlane);
		for (int col = 0; col < 4; ++col)
			matrix.m[2][col] = static_cast<float>(scale * matrix.m[3][col] + (col == 3 ? offset : 0));
	}
	/** Converts a native row-vector projection to a standard-depth SDK boundary. */
	template <class Matrix>
	void ToStandardProjection(Matrix& matrix, Convention order)
	{
		if (order == Convention::Reversed)
			for (int row = 0; row < 4; ++row)
				matrix.m[row][2] = matrix.m[row][3] - matrix.m[row][2];
	}
	/** Converts a cached column-vector camera transform to standard device depth. */
	template <class Matrix>
	void ToStandardUpload(Matrix& matrix, Convention order)
	{
		if (order == Convention::Reversed)
			for (int col = 0; col < 4; ++col)
				matrix.m[2][col] = matrix.m[3][col] - matrix.m[2][col];
	}
}
