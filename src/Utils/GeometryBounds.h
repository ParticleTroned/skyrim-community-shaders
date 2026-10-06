#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace RE
{
	class BSGeometry;
}

namespace Util
{
	/** World-space enclosure; an invalid box must never establish offscreen coverage. */
	struct GeometryBounds
	{
		std::array<float, 3> minimum{}, maximum{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			for (std::size_t axis = 0; axis < 3; ++axis)
				if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) || minimum[axis] >= maximum[axis])
					return false;
			return true;
		}

		/** A lower bound on surface distance; uncertainty cannot reject a selected mesh. */
		[[nodiscard]] float NearestDistance(const std::array<float, 3>& a_point) const noexcept
		{
			if (!IsValid())
				return 0.0f;
			double squaredDistance = 0.0;
			for (std::size_t axis = 0; axis < 3; ++axis) {
				if (!std::isfinite(a_point[axis]))
					return 0.0f;
				const auto delta = std::max({ 0.0, static_cast<double>(minimum[axis]) - a_point[axis],
					static_cast<double>(a_point[axis]) - maximum[axis] });
				squaredDistance += delta * delta;
			}
			return static_cast<float>(std::min(std::sqrt(squaredDistance), static_cast<double>(std::numeric_limits<float>::max())));
		}

		/** Outward rounding keeps conversion and transform error outside the enclosure. */
		[[nodiscard]] static GeometryBounds FromCenterExtent(
			const std::array<double, 3>& a_center, const std::array<double, 3>& a_extent) noexcept
		{
			GeometryBounds result;
			for (std::size_t axis = 0; axis < 3; ++axis) {
				if (!std::isfinite(a_center[axis]) || !std::isfinite(a_extent[axis]) || a_extent[axis] <= 0.0)
					return {};
				result.minimum[axis] = std::nextafter(static_cast<float>(a_center[axis] - a_extent[axis]), -std::numeric_limits<float>::infinity());
				result.maximum[axis] = std::nextafter(static_cast<float>(a_center[axis] + a_extent[axis]), std::numeric_limits<float>::infinity());
			}
			return result.IsValid() ? result : GeometryBounds{};
		}

		/** Include complete bone enclosures before projection to also cover blended vertices. */
		void Include(const GeometryBounds& a_other) noexcept
		{
			if (!IsValid()) {
				*this = a_other;
				return;
			}
			for (std::size_t axis = 0; axis < 3; ++axis) {
				minimum[axis] = std::min(minimum[axis], a_other.minimum[axis]);
				maximum[axis] = std::max(maximum[axis], a_other.maximum[axis]);
			}
		}
	};

	/** Enclose an affine-transformed sphere, including scale, shear and reflection. */
	[[nodiscard]] inline GeometryBounds TransformGeometrySphere(
		const std::array<float, 3>& a_center, float a_radius,
		const std::array<std::array<float, 3>, 3>& a_rotation,
		const std::array<float, 3>& a_translation, float a_scale) noexcept
	{
		if (!std::isfinite(a_radius) || a_radius <= 0.0f || !std::isfinite(a_scale) || a_scale == 0.0f)
			return {};
		std::array<double, 3> center{}, extent{};
		for (std::size_t row = 0; row < 3; ++row) {
			double value = 0.0, squaredLength = 0.0;
			for (std::size_t column = 0; column < 3; ++column) {
				const double coefficient = a_rotation[row][column];
				value += coefficient * a_center[column];
				squaredLength += coefficient * coefficient;
			}
			center[row] = value * a_scale + a_translation[row];
			extent[row] = std::sqrt(squaredLength) * std::abs(static_cast<double>(a_scale)) * a_radius;
		}
		return GeometryBounds::FromCenterExtent(center, extent);
	}

	/** Bounds work is capped independently of actor count; exhaustion retains geometry bounds. */
	struct GeometryBoundsBudget
	{
		static constexpr std::uint32_t kMaximumBonesPerGeometry = 256;
		static constexpr std::uint32_t kMaximumBonesPerFrame = 4096;
		std::uint32_t reserved = 0;

		[[nodiscard]] bool Reserve(std::uint32_t a_count) noexcept
		{
			if (!a_count || a_count > kMaximumBonesPerGeometry || reserved > kMaximumBonesPerFrame ||
				a_count > kMaximumBonesPerFrame - reserved)
				return false;
			reserved += a_count;
			return true;
		}
	};

	/** Incomplete skins retain the entire fallback, never the successfully read subset. */
	template <class ReadBone>
	[[nodiscard]] GeometryBounds RefineGeometryBounds(
		const GeometryBounds& a_fallback, std::uint32_t a_boneCount,
		GeometryBoundsBudget& a_budget, ReadBone&& a_readBone, bool& a_refined) noexcept
	{
		a_refined = false;
		if (!a_budget.Reserve(a_boneCount))
			return a_fallback;
		GeometryBounds result;
		for (std::uint32_t index = 0; index < a_boneCount; ++index) {
			const auto bone = a_readBone(index);
			if (!bone.IsValid())
				return a_fallback;
			result.Include(bone);
		}
		a_refined = true;
		return result;
	}

	/** Render-thread snapshot of animated skin bounds; unsupported data retains worldBound. */
	[[nodiscard]] GeometryBounds ReadGeometryBounds(
		const RE::BSGeometry& a_geometry, GeometryBoundsBudget& a_budget,
		bool a_refineSkin, bool& a_refined) noexcept;
}
