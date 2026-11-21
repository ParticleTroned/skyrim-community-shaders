#pragma once

#include "Features/GrassOptimizations/GrassCullingHierarchy.h"
#include "Features/GrassOptimizations/GrassPolicy.h"
#include <random>
#include <stdexcept>

inline void CheckGrassCostAndSimd()
{
	const auto require = [](bool condition) {
		if (!condition)
			throw std::runtime_error("Grass cost/SIMD culling changed conservative visibility");
	};
	const GrassFrustum::Matrix identity{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	const std::array<float, 4> weights{ 8, 4, 2, 1 };
	const auto tight = GrassPolicy::MakeCostCullLimits(20000, 2, 1, weights, { true, true, true }, true, 1, 1000);
	require(tight.distance > 10000 && tight.distance < 10001 && tight.minimumPixels == 1 && tight.costMinimumPixels > 1.99f);
	const auto absent = GrassPolicy::MakeCostCullLimits(20000, 2, 0, weights, { true, false, false }, true, 1, 1000);
	require(absent.minimumPixels == 2 && absent.costMinimumPixels > 15.99f);
	const auto late = GrassPolicy::MakeCostCullLimits(20000, 2, 1, weights, { true, true, true }, true, 1, 12000);
	require(late.distance == 20000);
	const auto zero = GrassPolicy::MakeCostCullLimits(20000, 2, 1, weights, { true, true, true }, true, 0, 0);
	require(zero.distance == 20000 && zero.costMinimumPixels == zero.minimumPixels);
	const auto invalid = GrassPolicy::MakeCostCullLimits(20000, 2, 1, weights, { true, true, true }, true, NAN, 0);
	require(invalid.distance == 20000 && invalid.costMinimumPixels == invalid.minimumPixels);

	// Compare CPU bounds against the shader's per-instance float ramp for every eligible tier.
	for (uint32_t mask : { 1u, 3u, 5u, 7u })
		for (bool farPolicy : { false, true })
			for (float bias : { 0.f, .4f, 1.f })
				for (float start : { 0.f, 1000.f, 12000.f }) {
					const std::array<bool, 3> enabled{ true, bool(mask & 2), bool(mask & 4) };
					const auto limits = GrassPolicy::MakeCostCullLimits(20000, 2, 1, weights, enabled, farPolicy, bias, start);
					std::array<GrassFrustum::Matrix, 2> world{ identity, identity };
					world[0][3] = -400;
					world[1][3] = 400;
					const std::array<GrassFrustum::PixelProjection, 2> projections{
						GrassFrustum::PixelProjection{ { .2f, 0, 1, -80 }, 1000, true },
						GrassFrustum::PixelProjection{ { -.2f, 0, 1, -80 }, 1050, true }
					};
					std::array<GrassFrustum::PixelLimits, 2> pixelLimits;
					for (size_t eye = 0; eye < 2; ++eye)
						pixelLimits[eye] = GrassFrustum::PreparePixelLimits(projections[eye], limits.minimumPixels, limits.costMinimumPixels, limits.rampEnd, world);
					for (float x : { -2000.f, 0.f, 2000.f })
						for (float z = 100; z < 24000; z += 137)
							for (float radius : { 1.f, 10.f, 100.f }) {
								const std::array<float, 3> root{ x, 0, z };
								float distance = std::numeric_limits<float>::max(), pixels = 0;
								bool cpuDistance = false, cpuPixels = false;
								for (size_t eye = 0; eye < 2; ++eye) {
									distance = std::min(distance, std::hypot(x + world[eye][3], z));
									const float w = projections[eye].w[0] * x + z + projections[eye].w[3];
									pixels = std::max(pixels, w > 1e-4f ? radius * float(projections[eye].focal) / w : 1e20f);
									cpuDistance |= GrassFrustum::WithinDistance(world[eye], root, root, limits.distance);
									cpuPixels |= GrassFrustum::ClassifyPixelSize<false>(projections[eye], pixelLimits[eye], root, root, radius, radius).intersects;
								}
								const float cost = bias * std::clamp((distance - start) / std::max(start, 1e-4f), 0.f, 1.f);
								for (size_t tier = 0; tier < enabled.size(); ++tier) {
									if (!enabled[tier])
										continue;
									const float maximumDistance = 20000 * (1 + (1 / weights[tier] - 1) * cost);
									const float minimumPixels = (farPolicy && tier == 2 ? 1.f : 2.f) * (1 + (weights[tier] - 1) * cost);
									if (distance <= maximumDistance && pixels >= minimumPixels)
										require(cpuDistance && cpuPixels);
								}
							}
				}

	const GrassFrustum::PixelProjection projection{ { .1f, 0, 1, 0 }, 500, true };
	const auto pixelLimits = GrassFrustum::PreparePixelLimits(projection, 2, 4, 2000, { &identity, 1 });
	const auto costPixels = GrassFrustum::PreparePixelLimits(projection, 1, 2, 2000, { &identity, 1 });
	require(GrassFrustum::AbovePixelThreshold(projection, { 0, 0, 4000 }, { 0, 0, 4000 }, 10, 1));
	require(!GrassFrustum::ClassifyPixelSize<false>(projection, costPixels, { 0, 0, 4000 }, { 0, 0, 4000 }, 10, 10).intersects);
	std::mt19937 random(316202);
	std::uniform_real_distribution<float> coordinate(-20000, 20000), extent(0, 50), radius(.01f, 100);
	for (uint32_t count : { 1u, 2u, 3u, 4u })
		for (size_t repeat = 0; repeat < 512; ++repeat) {
			GrassCulling::Batch boxes;
			auto world = identity;
			world[0] = .8f;
			world[1] = -.6f;
			world[4] = .6f;
			world[5] = .8f;
			world[3] = coordinate(random);
			for (size_t lane = 0; lane < count; ++lane) {
				GrassCulling::Bounds box;
				for (size_t axis = 0; axis < 3; ++axis) {
					box.lo[axis] = coordinate(random);
					box.hi[axis] = box.lo[axis] + extent(random);
				}
				box.minimumRadius = radius(random);
				box.maximumRadius = box.minimumRadius + radius(random);
				box.known = true;
				boxes.Store(lane, box);
			}
			const auto distance = GrassCullingSimd::Distance<true>(boxes, boxes.known, world, 10000);
			const auto pixels = GrassCullingSimd::PixelSize<true>(boxes, boxes.known, projection, pixelLimits);
			const auto leafDistance = GrassCullingSimd::Distance<false>(boxes, boxes.known, world, 10000);
			const auto leafPixels = GrassCullingSimd::PixelSize<false>(boxes, boxes.known, projection, pixelLimits);
			for (size_t lane = 0; lane < count; ++lane) {
				const auto box = boxes.Load(lane);
				const auto scalarDistance = GrassFrustum::ClassifyDistance<true>(world, box.lo, box.hi, 10000);
				const auto scalarPixels = GrassFrustum::ClassifyPixelSize<true>(projection, pixelLimits, box.lo, box.hi, box.maximumRadius, box.minimumRadius);
				require(distance.lanes[lane].intersects == scalarDistance.intersects && distance.lanes[lane].contains == scalarDistance.contains);
				require(pixels.lanes[lane].intersects == scalarPixels.intersects && pixels.lanes[lane].contains == scalarPixels.contains);
				require(leafDistance.lanes[lane].intersects == scalarDistance.intersects && leafPixels.lanes[lane].intersects == scalarPixels.intersects);
			}
		}

	GrassCulling::Batch edge;
	for (size_t lane = 0; lane < 4; ++lane)
		edge.Store(lane, { { 0, 0, 228 }, { 0, 0, 228 }, 0, 10, 10, true });
	const auto distanceBoundary = GrassCullingSimd::Distance<true>(edge, 15, identity, 100);
	require(distanceBoundary.lanes[0].intersects && !distanceBoundary.lanes[0].contains);
	for (size_t lane = 0; lane < 4; ++lane)
		edge.Store(lane, { { 0, 0, 2500 }, { 0, 0, 2500 }, 0, 10, 10, true });
	const auto basePixels = GrassFrustum::PreparePixelLimits(projection, 2);
	const auto pixelBoundary = GrassCullingSimd::PixelSize<true>(edge, 15, projection, basePixels);
	require(pixelBoundary.lanes[0].intersects && !pixelBoundary.lanes[0].contains);
	for (size_t lane = 0; lane < 4; ++lane)
		edge.Store(lane, { { 0, 0, 100 }, { 1, 1, 101 }, 0, 10, 10, true });
	require(GrassCullingSimd::Distance<true>(edge, 15, identity, 10000).lanes[0].contains);
	require(GrassCullingSimd::PixelSize<true>(edge, 15, projection, basePixels).lanes[0].contains);
	auto cancellation = identity;
	cancellation[3] = -1e8f;
	for (size_t lane = 0; lane < 4; ++lane)
		edge.Store(lane, { { 1e8f + 232, 0, 0 }, { 1e8f + 232, 0, 0 }, 0, 10, 10, true });
	const auto cancelled = GrassCullingSimd::Distance<true>(edge, 15, cancellation, 100);
	require(!cancelled.lanes[0].intersects);
	for (size_t lane = 0; lane < 4; ++lane)
		edge.Store(lane, { { 1e20f, 0, 0 }, { 1e20f, 0, 0 }, 0, 10, 10, true });
	const auto overflow = GrassCullingSimd::Distance<true>(edge, 15, identity, 100);
	require(!overflow.lanes[0].intersects);
	cancellation[0] = NAN;
	const auto unknown = GrassCullingSimd::Distance<true>(edge, 15, cancellation, 100);
	require(unknown.lanes[0].intersects && !unknown.lanes[0].contains);
	auto unusual = identity;
	unusual[0] = 2;
	require(!std::isfinite(GrassFrustum::PreparePixelLimits(projection, 2, 4, 2000, { &unusual, 1 }).rampDepth));

	// One eye supplies the larger pixel size while the other keeps the shared cost ramp at zero.
	GrassCulling::Query stereo;
	stereo.eyes = 2;
	stereo.pixelSize = true;
	stereo.minimumPixels = 1;
	stereo.costMinimumPixels = 2;
	stereo.costRampEnd = 2000;
	stereo.world.fill(identity);
	stereo.world[1][11] = -2000;
	stereo.pixels = { GrassFrustum::PixelProjection{ { 0, 0, 1, 0 }, 500, true },
		GrassFrustum::PixelProjection{ { 0, 0, 1, -2000 }, 50, true } };
	const auto otherEyeRamp = GrassFrustum::PreparePixelLimits(stereo.pixels[0], 1, 2, 2000, stereo.world);
	require(otherEyeRamp.rampDepth > 4000);
	for (size_t count : { 1u, 4u, 65u }) {
		GrassCulling::Hierarchy hierarchy;
		hierarchy.Update({}, count, [](size_t) { return GrassCulling::Bounds{ { 0, 0, 3000 }, { 0, 0, 3000 }, 0, 10, 10, true }; });
		hierarchy.Cull(stereo, [&](size_t, size_t, bool visible) { require(visible); });
	}
	stereo.world[1][0] = 2;
	require(!std::isfinite(GrassFrustum::PreparePixelLimits(stereo.pixels[0], 1, 2, 2000, stereo.world).rampDepth));
}
