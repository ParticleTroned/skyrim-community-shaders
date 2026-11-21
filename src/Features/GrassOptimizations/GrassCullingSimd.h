#pragma once

#include "GrassFrustum.h"
#include <bit>
#include <cstdint>
#include <xmmintrin.h>

namespace GrassCullingSimd
{
	inline __m128 Abs(__m128 value) { return _mm_andnot_ps(_mm_set1_ps(-0.0f), value); }
	inline __m128 Splat(double value) { return _mm_set1_ps(float(value)); }

	struct Interval
	{
		__m128 low, high, magnitude;
	};

	/** @brief Transform four boxes without assuming an orthogonal camera or centered projection. */
	template <class Batch>
	inline Interval Project(const Batch& boxes, const float* row)
	{
		Interval result{ Splat(row[3]), Splat(row[3]), Splat(std::abs(row[3])) };
		for (size_t axis = 0; axis < 3; ++axis) {
			if (row[axis] == 0)
				continue;
			const auto low = _mm_load_ps(boxes.lo[axis].data()), high = _mm_load_ps(boxes.hi[axis].data());
			const auto coefficient = Splat(row[axis]);
			result.low = _mm_add_ps(result.low, _mm_mul_ps(coefficient, row[axis] < 0 ? high : low));
			result.high = _mm_add_ps(result.high, _mm_mul_ps(coefficient, row[axis] < 0 ? low : high));
			result.magnitude = _mm_add_ps(result.magnitude, _mm_mul_ps(Abs(coefficient), _mm_max_ps(Abs(low), Abs(high))));
		}
		return result;
	}

	struct Comparison
	{
		uint32_t below, above;
	};

	inline Comparison Compare(__m128 value, __m128 limit, __m128 error)
	{
		const auto finite = _mm_and_ps(_mm_cmple_ps(error, Splat(std::numeric_limits<float>::max())),
			_mm_and_ps(_mm_cmple_ps(Abs(value), Splat(std::numeric_limits<float>::max())), _mm_cmple_ps(Abs(limit), Splat(std::numeric_limits<float>::max()))));
		return { uint32_t(_mm_movemask_ps(_mm_and_ps(finite, _mm_cmplt_ps(value, _mm_sub_ps(limit, error))))),
			uint32_t(_mm_movemask_ps(_mm_and_ps(finite, _mm_cmpgt_ps(value, _mm_add_ps(limit, error))))) };
	}

	struct Relations
	{
		std::array<GrassFrustum::BoundsRelation, 4> lanes{};
	};

	/** @brief Resolve wide comparisons in SIMD; use double precision at uncertain boundaries. */
	template <bool Containment, class Scalar>
	inline Relations Resolve(uint32_t active, Comparison visible, Comparison contained, Scalar&& scalar)
	{
		Relations result;
		for (size_t lane = 0; lane < result.lanes.size(); ++lane) {
			const uint32_t bit = 1u << lane;
			if (!(active & bit))
				continue;
			const bool known = (visible.below | visible.above) & bit;
			if (known && (!Containment || ((contained.below | contained.above) & bit)))
				result.lanes[lane] = { bool(visible.below & bit), Containment && bool(contained.below & bit) };
			else {
				result.lanes[lane] = scalar(lane);
			}
		}
		return result;
	}

	/** @brief Classify four distance bounds with an error envelope that includes cancellation. */
	template <bool Containment, class Batch>
	inline Relations Distance(const Batch& boxes, uint32_t active, const GrassFrustum::Matrix& world, float distance)
	{
		const auto scalar = [&](size_t lane) {
			const auto box = boxes.Load(lane);
			return GrassFrustum::ClassifyDistance<Containment>(world, box.lo, box.hi, distance);
		};
		if (std::has_single_bit(active))
			return Resolve<Containment>(active, {}, {}, scalar);
		if (!std::isfinite(distance) || distance <= 0 || world[12] != 0 || world[13] != 0 || world[14] != 0 || world[15] != 1 ||
			!std::all_of(world.begin(), world.end(), [](float value) { return std::isfinite(value); }))
			return {};
		auto nearestSquared = _mm_setzero_ps(), farthestSquared = nearestSquared, magnitudeSquared = nearestSquared;
		for (size_t row = 0; row < 3; ++row) {
			const auto interval = Project(boxes, world.data() + row * 4);
			const auto nearest = _mm_max_ps(_mm_setzero_ps(), _mm_max_ps(interval.low, _mm_sub_ps(_mm_setzero_ps(), interval.high)));
			nearestSquared = _mm_add_ps(nearestSquared, _mm_mul_ps(nearest, nearest));
			magnitudeSquared = _mm_add_ps(magnitudeSquared, _mm_mul_ps(interval.magnitude, interval.magnitude));
			if constexpr (Containment) {
				const auto farthest = _mm_max_ps(Abs(interval.low), Abs(interval.high));
				farthestSquared = _mm_add_ps(farthestSquared, _mm_mul_ps(farthest, farthest));
			}
		}
		const auto limit = Splat(double(distance + GrassFrustum::kEdgePadding) * (distance + GrassFrustum::kEdgePadding));
		// This covers dot products, squared sums, threshold conversion and the containment margin.
		const auto error = _mm_mul_ps(Splat(1e-4), _mm_add_ps(Splat(1), _mm_add_ps(magnitudeSquared, limit)));
		const auto visible = Compare(nearestSquared, limit, error);
		const auto contained = Containment ? Compare(_mm_mul_ps(farthestSquared, Splat(1.00001)), limit, error) : Comparison{};
		return Resolve<Containment>(active, visible, contained, scalar);
	}

	inline __m128 PixelDepth(__m128 radius, const GrassFrustum::PixelLimits& limits, bool contained)
	{
		const auto base = _mm_mul_ps(radius, Splat(contained ? limits.containedScale : limits.visibleScale));
		if (!std::isfinite(limits.rampDepth))
			return base;
		const auto cost = _mm_mul_ps(radius, Splat(contained ? limits.costContainedScale : limits.costVisibleScale));
		return _mm_min_ps(base, _mm_max_ps(cost, Splat(limits.rampDepth)));
	}

	/** @brief Compare four projected sizes without per-run division, retaining near-plane uncertainty. */
	template <bool Containment, class Batch>
	inline Relations PixelSize(const Batch& boxes, uint32_t active, const GrassFrustum::PixelProjection& projection, const GrassFrustum::PixelLimits& limits)
	{
		if (!limits.valid)
			return {};
		const auto scalar = [&](size_t lane) {
			const auto box = boxes.Load(lane);
			return GrassFrustum::ClassifyPixelSize<Containment>(projection, limits, box.lo, box.hi, box.maximumRadius, box.minimumRadius);
		};
		if (std::has_single_bit(active))
			return Resolve<Containment>(active, {}, {}, scalar);
		const auto interval = Project(boxes, projection.w.data());
		const auto maxRadius = _mm_load_ps(boxes.maximumRadius.data());
		const auto minRadius = _mm_load_ps(boxes.minimumRadius.data());
		const auto visibleDepth = PixelDepth(maxRadius, limits, false);
		const auto containedDepth = Containment ? PixelDepth(minRadius, limits, true) : _mm_setzero_ps();
		const auto depthError = _mm_mul_ps(Splat(2e-5), _mm_add_ps(Splat(1), interval.magnitude));
		const auto errorFor = [&](const auto depth) { return _mm_add_ps(depthError, _mm_mul_ps(Splat(1e-4), _mm_add_ps(Splat(1), Abs(depth)))); };
		auto visible = Compare(interval.low, visibleDepth, errorFor(visibleDepth));
		auto contained = Containment ? Compare(interval.high, containedDepth, errorFor(containedDepth)) : Comparison{};
		auto valid = _mm_and_ps(_mm_cmpgt_ps(maxRadius, _mm_setzero_ps()), _mm_cmple_ps(maxRadius, Splat(std::numeric_limits<float>::max())));
		valid = _mm_and_ps(valid, _mm_cmpgt_ps(_mm_sub_ps(interval.low, depthError), Splat(1e-4)));
		if constexpr (Containment)
			valid = _mm_and_ps(valid, _mm_and_ps(_mm_cmpgt_ps(minRadius, _mm_setzero_ps()), _mm_cmple_ps(minRadius, maxRadius)));
		const auto mask = uint32_t(_mm_movemask_ps(valid));
		visible.below &= mask;
		visible.above &= mask;
		contained.below &= mask;
		contained.above &= mask;
		return Resolve<Containment>(active, visible, contained, scalar);
	}
}
