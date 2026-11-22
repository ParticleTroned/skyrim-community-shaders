#pragma once

#include "Features/GrassOptimizations/GrassCullingHierarchy.h"
#include <limits>
#include <random>
#include <stdexcept>

inline void CheckCullingHierarchy()
{
	const auto require = [](bool condition) {
		if (!condition)
			throw std::runtime_error("Grass culling hierarchy lost coverage or cache ownership");
	};
	const auto referenceVisible = [](GrassCulling::Bounds box, const GrassCulling::Query& query) {
		if (!box.known || !std::isfinite(box.reach))
			return true;
		box.lo[2] -= query.placement;
		box.hi[2] += query.placement;
		const float padding = GrassFrustum::DeformationPadding(query.world[0], box.lo, box.hi, box.reach,
			query.wind, query.collisionDistance, query.collisionStrength);
		if (!std::isfinite(padding))
			return true;
		for (size_t axis = 0; axis < 3; ++axis) {
			box.lo[axis] -= padding;
			box.hi[axis] += padding;
		}
		bool frustum = !query.frustum, distance = false, pixels = !query.pixelSize;
		for (uint32_t eye = 0; eye < query.eyes; ++eye) {
			frustum |= GrassFrustum::Visible(query.planes[eye], box.lo, box.hi);
			distance |= GrassFrustum::WithinDistance(query.world[eye], box.lo, box.hi, query.distance);
			const auto limits = GrassFrustum::PreparePixelLimits(query.pixels[eye], query.minimumPixels,
				query.costMinimumPixels, query.costRampEnd, { query.world.data(), query.eyes });
			pixels |= GrassFrustum::ClassifyPixelSize<false>(query.pixels[eye], limits, box.lo, box.hi, box.maximumRadius, box.minimumRadius).intersects;
		}
		return frustum && distance && pixels;
	};
	const GrassFrustum::Matrix identity{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	const auto atDistance = GrassFrustum::ClassifyDistance<true>(identity, { 0, 0, 228 }, { 0, 0, 228 }, 100);
	const auto withinDistance = GrassFrustum::ClassifyDistance<true>(identity, { -1, -1, 200 }, { 1, 1, 201 }, 100);
	const auto beyondDistance = GrassFrustum::ClassifyDistance<true>(identity, { 0, 0, 230 }, { 1, 1, 231 }, 100);
	require(atDistance.intersects && !atDistance.contains && withinDistance.intersects && withinDistance.contains &&
			!beyondDistance.intersects && !beyondDistance.contains);
	const GrassFrustum::PixelProjection projection{ { 0, 0, 1, 0 }, 500, true };
	const auto atPixels = GrassFrustum::ClassifyPixelSize<true>(projection, { 0, 0, 1000 }, { 0, 0, 1000 }, 10, 10, 5);
	const auto mixedPixels = GrassFrustum::ClassifyPixelSize<true>(projection, { 0, 0, 100 }, { 0, 0, 200 }, 10, 1, 5);
	const auto abovePixels = GrassFrustum::ClassifyPixelSize<true>(projection, { 0, 0, 100 }, { 0, 0, 200 }, 10, 10, 5);
	const auto belowPixels = GrassFrustum::ClassifyPixelSize<true>(projection, { 0, 0, 2000 }, { 0, 0, 2001 }, 10, 10, 5);
	require(atPixels.intersects && !atPixels.contains && mixedPixels.intersects && !mixedPixels.contains &&
			abovePixels.intersects && abovePixels.contains && !belowPixels.intersects && !belowPixels.contains);
	GrassCulling::Query query;
	query.eyes = 2;
	query.frustum = query.pixelSize = true;
	query.distance = 2500;
	query.minimumPixels = 1;
	query.world.fill(identity);
	query.pixels.fill({ { 0, 0, 1, 0 }, 500, true });
	for (size_t eye = 0; eye < 2; ++eye) {
		query.planes[eye] = { { { 1, 0, 0, 500 }, { -1, 0, 0, 500 }, { 0, 1, 0, 500 },
			{ 0, -1, 0, 500 }, { 0, 0, 1, 128 }, { 0, 0, -1, 10000 } } };
		query.planes[eye][0][3] += eye ? 300.f : -300.f;
		query.planes[eye][1][3] += eye ? -300.f : 300.f;
		query.world[eye][3] = eye ? 300.f : -300.f;
	}
	std::mt19937 random(3162);
	std::uniform_real_distribution<float> coordinate(-2000, 12000), size(.01f, 80);
	std::vector<GrassCulling::Bounds> bounds;
	const auto check = [&](const GrassCulling::Query& condition) {
		GrassCulling::Hierarchy hierarchy;
		GrassCulling::Hierarchy::Key key{};
		size_t reads = 0;
		const auto get = [&](size_t index) { ++reads; return bounds[index]; };
		hierarchy.Update(key, bounds.size(), get);
		hierarchy.Update(key, bounds.size(), get);
		require(reads == bounds.size());
		++key[0];
		hierarchy.Update(key, bounds.size(), get);
		require(reads == bounds.size() * 2);
		size_t next = 0, rejected = 0;
		hierarchy.Cull(condition, [&](size_t first, size_t end, bool visible) {
			require(first == next && first < end && end <= bounds.size());
			for (size_t index = first; index < end; ++index) {
				require(visible || !referenceVisible(bounds[index], condition));
				rejected += !visible;
			}
			next = end;
		});
		require(next == bounds.size());
		return rejected;
	};
	for (size_t count : { 0u, 1u, 3u, 4u, 5u, 63u, 64u, 65u, 129u }) {
		bounds.resize(count);
		for (int iteration = 0; iteration < 32; ++iteration) {
			for (auto& box : bounds) {
				for (size_t axis = 0; axis < 3; ++axis) {
					box.lo[axis] = coordinate(random);
					box.hi[axis] = box.lo[axis] + size(random);
				}
				box.reach = size(random);
				box.minimumRadius = box.maximumRadius = size(random);
				box.known = true;
			}
			auto condition = query;
			condition.eyes = iteration % 2 ? 2 : 1;
			condition.wind = float(iteration);
			condition.placement = float(iteration * 3);
			condition.collisionDistance = iteration % 3 ? 4000.f : 0;
			condition.collisionStrength = float(iteration % 5);
			condition.costMinimumPixels = iteration % 4 ? 3.f : 0;
			condition.costRampEnd = 1500;
			check(condition);
			std::ranges::sort(bounds, {}, [](const auto& box) { return box.lo[0]; });
			check(condition);
		}
	}
	// These ordered blocks lie wholly outside; the right eye alone retains the last run.
	bounds.assign(129, { { 20000, 0, 1000 }, { 20001, 1, 1001 }, 1, 10, 10, true });
	bounds.back() = { { -700, 0, 1000 }, { -699, 1, 1001 }, 1, 10, 10, true };
	require(check(query) == 128);
	// Changing draw-local deformation must not reuse the previous draw's expanded bounds.
	GrassCulling::Hierarchy moving;
	const GrassCulling::Bounds root{ { 1000, 0, 1000 }, { 1001, 1, 1001 }, 100, 10, 10, true };
	moving.Update({}, 1, [&](size_t) { return root; });
	const auto visibleWith = [&](float wind, float collision) {
		auto condition = query;
		condition.wind = wind;
		condition.collisionDistance = collision;
		bool visible = false;
		moving.Cull(condition, [&](size_t first, size_t end, bool keep) {
			require(first == 0 && end == 1);
			visible = keep;
		});
		return visible;
	};
	require(!visibleWith(0, 0) && visibleWith(300, 0) && visibleWith(0, 4000) && !visibleWith(0, 0));
	for (size_t index = 0; index < bounds.size(); ++index) {
		switch (index % 4) {
		case 0:
			bounds[index].known = false;
			break;
		case 1:
			bounds[index].lo[0] = std::numeric_limits<float>::quiet_NaN();
			break;
		case 2:
			bounds[index].reach = std::numeric_limits<float>::infinity();
			break;
		case 3:
			bounds[index].hi[1] = bounds[index].lo[1] - 1;
			break;
		}
	}
	require(check(query) == 0);
	query.frustum = false;
	query.world[0][0] = std::numeric_limits<float>::quiet_NaN();
	query.pixels[0].valid = false;
	query.wind = std::numeric_limits<float>::quiet_NaN();
	require(check(query) == 0);
}
