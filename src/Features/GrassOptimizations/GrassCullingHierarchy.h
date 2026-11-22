#pragma once

#include "GrassCullingSimd.h"
#include "GrassFrustum.h"
#include <cstdint>
#include <vector>
#include <xmmintrin.h>

// SIMD batches and their owners require padding to retain aligned loads.
#pragma warning(push)
#pragma warning(disable: 4324)

namespace GrassCulling
{
	inline constexpr size_t kLanes = 4, kRunsPerBlock = 16, kMinimumBlockedRuns = 64;
	inline constexpr uint8_t kAllPlanes = 0x3F, kOutside = 0x80;

	struct Bounds
	{
		std::array<float, 3> lo{}, hi{};
		float reach = 0, minimumRadius = 0, maximumRadius = 0;
		bool known = false;
	};

	struct Query
	{
		std::array<GrassFrustum::Planes, 2> planes{};
		std::array<GrassFrustum::Matrix, 2> world{};
		std::array<GrassFrustum::PixelProjection, 2> pixels{};
		uint32_t eyes = 1;
		bool frustum = false, pixelSize = false;
		float distance = 0, minimumPixels = 0, wind = 0, placement = 0;
		float collisionDistance = 0, collisionStrength = 1;
		float costMinimumPixels = 0, costRampEnd = 0;
	};

	struct Filter
	{
		std::array<uint8_t, 2> planes{};
		bool distance = true, pixels = false, collision = false, rejected = false;
		bool Complete() const { return !distance && !pixels && (planes[0] == 0 || planes[1] == 0); }
	};

	/** @brief Store four immutable bounds together for shared SIMD plane tests. */
	struct alignas(16) Batch
	{
		std::array<std::array<float, kLanes>, 3> lo{}, hi{};
		std::array<float, kLanes> reach{}, minimumRadius{}, maximumRadius{};
		uint8_t known = 0;

		void Store(size_t lane, const Bounds& bounds)
		{
			bool valid = bounds.known && std::isfinite(bounds.reach) && bounds.reach >= 0;
			for (size_t axis = 0; axis < 3; ++axis) {
				lo[axis][lane] = bounds.lo[axis];
				hi[axis][lane] = bounds.hi[axis];
				valid &= std::isfinite(bounds.lo[axis]) && std::isfinite(bounds.hi[axis]) && bounds.lo[axis] <= bounds.hi[axis];
			}
			reach[lane] = bounds.reach;
			minimumRadius[lane] = bounds.minimumRadius;
			maximumRadius[lane] = bounds.maximumRadius;
			known = uint8_t((known & ~(1u << lane)) | (uint32_t(valid) << lane));
		}

		Bounds Load(size_t lane) const
		{
			return { { lo[0][lane], lo[1][lane], lo[2][lane] }, { hi[0][lane], hi[1][lane], hi[2][lane] },
				reach[lane], minimumRadius[lane], maximumRadius[lane], bool(known & (1u << lane)) };
		}
	};

	inline Bounds Union(const Bounds& a, const Bounds& b)
	{
		Bounds result = a;
		for (size_t axis = 0; axis < 3; ++axis) {
			result.lo[axis] = std::min(a.lo[axis], b.lo[axis]);
			result.hi[axis] = std::max(a.hi[axis], b.hi[axis]);
		}
		result.reach = std::max(a.reach, b.reach);
		result.known = a.known && b.known;
		const bool radiiKnown = a.minimumRadius > 0 && b.minimumRadius > 0 &&
		                        std::isfinite(a.maximumRadius) && std::isfinite(b.maximumRadius);
		result.minimumRadius = radiiKnown ? std::min(a.minimumRadius, b.minimumRadius) : 0;
		result.maximumRadius = radiiKnown ? std::max(a.maximumRadius, b.maximumRadius) : 0;
		return result;
	}

	inline Bounds Expand(Bounds bounds, const Query& query, Filter& filter)
	{
		if (!bounds.known)
			return bounds;
		bounds.lo[2] -= query.placement;
		bounds.hi[2] += query.placement;
		if (filter.collision)
			filter.collision = GrassFrustum::WithinDistance(query.world[0], bounds.lo, bounds.hi, query.collisionDistance + bounds.reach);
		const float padding = GrassFrustum::DeformationPadding(bounds.reach, query.wind, filter.collision, query.collisionStrength);
		bounds.known = std::isfinite(padding) && padding >= 0 && std::isfinite(query.placement) && query.placement >= 0;
		for (size_t axis = 0; axis < 3; ++axis) {
			bounds.lo[axis] -= padding;
			bounds.hi[axis] += padding;
			bounds.known &= std::isfinite(bounds.lo[axis]) && std::isfinite(bounds.hi[axis]);
		}
		return bounds;
	}

	/** @brief Combine either-eye visibility and containment without testing already resolved lanes. */
	template <bool Containment>
	inline void ClassifyLimits(const Batch& boxes, size_t count, const Query& query,
		const std::array<GrassFrustum::PixelLimits, 2>& pixelLimits, Filter parent, std::array<Filter, kLanes>& filters)
	{
		for (size_t test = 0; test < 2; ++test) {
			if (!(test ? parent.pixels : parent.distance))
				continue;
			uint32_t active = boxes.known & ((1u << count) - 1), visible = 0, contained = 0;
			for (size_t lane = 0; lane < count; ++lane)
				if (filters[lane].rejected)
					active &= ~(1u << lane);
			for (uint32_t eye = 0; eye < query.eyes; ++eye) {
				const uint32_t pending = active & ~(Containment ? contained : visible);
				if (!pending)
					break;
				const auto result = test ? GrassCullingSimd::PixelSize<Containment>(boxes, pending, query.pixels[eye], pixelLimits[eye]) :
				                           GrassCullingSimd::Distance<Containment>(boxes, pending, query.world[eye], query.distance);
				for (size_t lane = 0; lane < count; ++lane) {
					const uint32_t bit = 1u << lane;
					if (pending & bit) {
						visible |= result.lanes[lane].intersects ? bit : 0;
						contained |= result.lanes[lane].contains ? bit : 0;
					}
				}
			}
			for (size_t lane = 0; lane < count; ++lane)
				if (active & (1u << lane)) {
					filters[lane].rejected = !(visible & (1u << lane));
					(test ? filters[lane].pixels : filters[lane].distance) = !(contained & (1u << lane));
				}
		}
	}

	/** @brief Classify four bounds conservatively; children inherit only proven parent tests. */
	inline std::array<Filter, kLanes> Classify(const Batch& roots, size_t count, const Query& query,
		const std::array<GrassFrustum::PixelLimits, 2>& pixelLimits, Filter parent, bool children)
	{
		std::array<Filter, kLanes> filters;
		filters.fill(parent);
		if (parent.Complete())
			return filters;
		Batch expanded;
		for (size_t lane = 0; lane < count; ++lane)
			expanded.Store(lane, Expand(roots.Load(lane), query, filters[lane]));
		const auto abs = GrassCullingSimd::Abs;
		for (uint32_t eye = 0; eye < query.eyes; ++eye) {
			uint32_t active = expanded.known & ((1u << count) - 1);
			for (size_t lane = 0; lane < count; ++lane)
				if (filters[lane].planes[eye] == kOutside || filters[lane].planes[0] == 0 || filters[lane].planes[1] == 0)
					active &= ~(1u << lane);
			for (uint32_t planeIndex = 0; planeIndex < 6; ++planeIndex) {
				if (!active)
					break;
				const uint8_t bit = uint8_t(1u << planeIndex);
				if (!(parent.planes[eye] & bit))
					continue;
				const auto& plane = query.planes[eye][planeIndex];
				auto nearDistance = _mm_set1_ps(plane[3]), farDistance = nearDistance, magnitude = abs(nearDistance);
				for (size_t axis = 0; axis < 3; ++axis) {
					const auto low = _mm_load_ps(expanded.lo[axis].data()), high = _mm_load_ps(expanded.hi[axis].data());
					const auto coefficient = _mm_set1_ps(plane[axis]);
					if (children)
						nearDistance = _mm_add_ps(nearDistance, _mm_mul_ps(coefficient, plane[axis] < 0 ? high : low));
					farDistance = _mm_add_ps(farDistance, _mm_mul_ps(coefficient, plane[axis] < 0 ? low : high));
					magnitude = _mm_add_ps(magnitude, _mm_mul_ps(abs(coefficient), _mm_max_ps(abs(low), abs(high))));
				}
				const auto error = _mm_mul_ps(_mm_set1_ps(1e-5f), _mm_add_ps(_mm_set1_ps(1), magnitude));
				const auto outside = uint32_t(_mm_movemask_ps(_mm_cmplt_ps(farDistance, _mm_sub_ps(_mm_setzero_ps(), error)))) & active;
				const auto inside = children ? uint32_t(_mm_movemask_ps(_mm_cmpgt_ps(nearDistance, error))) & active : 0;
				active &= ~outside;
				for (size_t lane = 0; lane < count; ++lane) {
					if (outside & (1u << lane))
						filters[lane].planes[eye] = kOutside;
					else if (inside & (1u << lane))
						filters[lane].planes[eye] &= uint8_t(~bit);
				}
			}
		}
		for (size_t lane = 0; lane < count; ++lane)
			filters[lane].rejected = filters[lane].planes[0] == kOutside && filters[lane].planes[1] == kOutside;
		if (children)
			ClassifyLimits<true>(expanded, count, query, pixelLimits, parent, filters);
		else
			ClassifyLimits<false>(expanded, count, query, pixelLimits, parent, filters);
		return filters;
	}

	/** @brief Cache source bounds; camera, collision, wind and placement remain draw-local. */
	class Hierarchy
	{
	public:
		using Key = std::array<uint32_t, 7>;

		template <class GetBounds>
		void Update(const Key& nextKey, size_t count, GetBounds&& getBounds)
		{
			if (valid && key == nextKey && runCount == count)
				return;
			valid = false;
			runCount = count;
			runs.assign((count + kLanes - 1) / kLanes, {});
			const size_t blockCount = count >= kMinimumBlockedRuns ? (count + kRunsPerBlock - 1) / kRunsPerBlock : 0;
			blocks.assign((blockCount + kLanes - 1) / kLanes, {});
			Bounds total{}, block{};
			double blockSpan = 0;
			for (size_t index = 0; index < count; ++index) {
				auto& batch = runs[index / kLanes];
				batch.Store(index % kLanes, getBounds(index));
				const auto bounds = batch.Load(index % kLanes);
				total = index ? Union(total, bounds) : bounds;
				if (blockCount) {
					block = index % kRunsPerBlock ? Union(block, bounds) : bounds;
					if ((index + 1) % kRunsPerBlock == 0 || index + 1 == count) {
						const auto blockIndex = index / kRunsPerBlock;
						blocks[blockIndex / kLanes].Store(blockIndex % kLanes, block);
						blockSpan += Span(block);
					}
				}
			}
			coarse.Store(0, total);
			// Scattered runs make block bounds as broad as the bucket and add traversal without useful rejection.
			blocked = blockCount && total.known && blockSpan < .75 * double(blockCount) * Span(total);
			key = nextKey;
			valid = true;
		}

		/** @brief Emit ordered accepted/rejected run ranges without allocating during traversal. */
		template <class Visit>
		void Cull(const Query& query, Visit&& visit) const
		{
			if (!runCount)
				return;
			if (!valid || (query.eyes != 1 && query.eyes != 2)) {
				visit(0, runCount, true);
				return;
			}
			std::array<GrassFrustum::PixelLimits, 2> pixelLimits{};
			if (query.pixelSize)
				for (uint32_t eye = 0; eye < query.eyes; ++eye)
					pixelLimits[eye] = GrassFrustum::PreparePixelLimits(query.pixels[eye], query.minimumPixels,
						query.costMinimumPixels, query.costRampEnd, { query.world.data(), query.eyes });
			Filter parent{ { query.frustum ? kAllPlanes : uint8_t(0), query.eyes == 2 ? (query.frustum ? kAllPlanes : uint8_t(0)) : kOutside },
				true, query.pixelSize, query.collisionDistance > 0, false };
			if (runCount > kLanes)
				parent = Classify(coarse, 1, query, pixelLimits, parent, true)[0];
			if (parent.rejected || parent.Complete()) {
				visit(0, runCount, !parent.rejected);
				return;
			}
			const auto visitRuns = [&](size_t first, size_t end, Filter filter) {
				if (filter.rejected || filter.Complete()) {
					visit(first, end, !filter.rejected);
					return;
				}
				for (size_t index = first; index < end; index += kLanes) {
					const auto count = std::min(kLanes, end - index);
					const auto filters = Classify(runs[index / kLanes], count, query, pixelLimits, filter, false);
					for (size_t lane = 0; lane < count; ++lane)
						visit(index + lane, index + lane + 1, !filters[lane].rejected);
				}
			};
			if (!blocked) {
				visitRuns(0, runCount, parent);
				return;
			}
			const size_t blockCount = (runCount + kRunsPerBlock - 1) / kRunsPerBlock;
			for (size_t index = 0; index < blockCount; index += kLanes) {
				const auto count = std::min(kLanes, blockCount - index);
				const auto filters = Classify(blocks[index / kLanes], count, query, pixelLimits, parent, true);
				for (size_t lane = 0; lane < count; ++lane) {
					const size_t first = (index + lane) * kRunsPerBlock;
					visitRuns(first, std::min(first + kRunsPerBlock, runCount), filters[lane]);
				}
			}
		}

	private:
		static double Span(const Bounds& bounds)
		{
			double result = 0;
			for (size_t axis = 0; axis < 3; ++axis)
				result += double(bounds.hi[axis]) - bounds.lo[axis];
			return result;
		}
		Key key{};
		size_t runCount = 0;
		bool valid = false, blocked = false;
		Batch coarse;
		std::vector<Batch> runs, blocks;
	};
}

#pragma warning(pop)
