#include "Features/GrassOptimizations/GrassFrustum.h"
#include "Features/GrassOptimizations/GrassPolicy.h"
#include <iostream>
#include <limits>
#include <stdexcept>
int main()
{
	try {
		const auto require = [](bool condition) {if(!condition)throw std::runtime_error("Grass settings policy failed"); };
		GrassFrustum::Matrix identity{};
		for (size_t i = 0; i < 4; ++i) identity[i * 5] = 1;
		const auto planes = GrassFrustum::Extract(identity);
		require(planes.has_value());
		require(GrassFrustum::Visible(*planes, { 0, 0, .1f }, { .1f, .1f, .9f }));
		require(GrassFrustum::Visible(*planes, { 128, 0, 0 }, { 129, 1, 1 }));
		require(!GrassFrustum::Visible(*planes, { 130, 0, 0 }, { 132, 1, 1 }));
		require(GrassFrustum::Visible(*planes, { NAN, 0, 0 }, { 1, 1, 1 }));
		require(!GrassFrustum::Extract({}).has_value());
		auto stale = identity;
		stale[3] = 5;
		require(GrassFrustum::SelectProjection(identity, identity, stale, identity) == identity);
		auto jittered = identity;
		jittered[3] = .001f;
		require(GrassFrustum::SelectProjection(jittered, identity, jittered, identity) == identity);
		GrassPolicy::Settings settings;
		require(settings.Valid());
		require(settings.Enabled && settings.CrossCellBatching && settings.FrustumCulling &&
				settings.DensityReduction && settings.EnableOcclusionCulling && !settings.EnableMeshLOD);
		require(settings.CollisionDistance == 2048.0f);
		for (float distance : { 0.0f, GrassPolicy::kMaxCollisionDistance }) {
			settings.CollisionDistance = distance;
			require(settings.Valid());
		}
		settings.CollisionDistance = GrassPolicy::kMaxCollisionDistance + 1;
		require(!settings.Valid());
		settings = {};
		require(GrassPolicy::BatchCapacityValid(GrassPolicy::kMaxBatchInstances, GrassPolicy::kMaxBatchSlices));
		require(!GrassPolicy::BatchCapacityValid(GrassPolicy::kMaxBatchInstances + 1ull, 1));
		require(!GrassPolicy::BatchCapacityValid(1, GrassPolicy::kMaxBatchSlices + 1ull));
		require(!GrassPolicy::BatchCapacityValid(0, 1));
		require(!GrassPolicy::BatchCapacityValid(1, 0));
		require(!GrassPolicy::BatchCapacityValid(UINT64_MAX, UINT64_MAX));
		require(GrassPolicy::SnapshotBudgetValid(GrassPolicy::kMaxCpuRecordBytes, 32, 32));
		require(!GrassPolicy::SnapshotBudgetValid(GrassPolicy::kMaxCpuRecordBytes, 32, 33));
		require(!GrassPolicy::SnapshotBudgetValid(0, 32, 0));
		require(!GrassPolicy::SnapshotBudgetValid(UINT64_MAX, 0, 0));
		GrassPolicy::Residency generated{ 10, {} };
		for (uint32_t frame = 10; frame <= 10 + GrassPolicy::kResidentIdleFrames; ++frame) {
			generated.Observe(frame, false);
			require(!generated.Expired(frame));
			require(!generated.VisibleInFrame(frame));
		}
		require(generated.Expired(11 + GrassPolicy::kResidentIdleFrames));
		generated.Observe(200, true);
		require(generated.VisibleInFrame(200) && !generated.Expired(200));
		generated.Observe(250, false);
		require(generated.IdleFrames(250) == 50 && !generated.VisibleInFrame(250));
		require(generated.Expired(201 + GrassPolicy::kResidentIdleFrames));
		GrassPolicy::Residency wrapped{ UINT32_MAX - 60, {} };
		require(!wrapped.Expired(59) && wrapped.Expired(60));
		wrapped.Observe(0, true);
		require(wrapped.VisibleInFrame(0) && !wrapped.Expired(0));
		require(GrassPolicy::MeshStride(0x8000000000000087ull) == 28);
		require(GrassPolicy::MeshStride(0x8000000000000080ull) == 0);
		require(GrassPolicy::OcclusionAllowed(true, 0));
		require(GrassPolicy::OcclusionAllowed(true, 2));
		require(!GrassPolicy::OcclusionAllowed(true, 3));
		require(GrassPolicy::OcclusionAllowed(false, 3));
		require(GrassPolicy::NativeCountMatches(10, 10));
		require(!GrassPolicy::NativeCountMatches(10, 20));
		require(!GrassPolicy::NativeCountMatches(10, 21));
		require(!GrassPolicy::NativeCountMatches(UINT32_MAX, UINT32_MAX - 1));
		for (float invalid : { -1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
			settings = {};
			settings.MinDensity = invalid;
			require(!settings.Valid());
		}
		settings = {};
		settings.FullDetailPixelSize = settings.MinPixelSize;
		require(!settings.Valid());
		settings = {};
		settings.FarLODPixelSize = settings.MidLODPixelSize + 1;
		require(!settings.Valid());
		settings = {};
		settings.OcclusionBias = .051f;
		require(!settings.Valid());
		settings = {};
		settings.MinDensity = 0;
		require(settings.Valid());
		settings.MinDensity = 1;
		require(settings.Valid());
		for (float GrassPolicy::Settings::* field : { &GrassPolicy::Settings::MeshCostBias,
				 &GrassPolicy::Settings::CostBiasStartDistance, &GrassPolicy::Settings::InvisibleFadeCull,
				 &GrassPolicy::Settings::RenderDistanceOverride, &GrassPolicy::Settings::EdgeFadeStart,
				 &GrassPolicy::Settings::SimpleShadingPixelSize, &GrassPolicy::Settings::CollisionDistance }) {
			settings = {};
			settings.*field = std::numeric_limits<float>::quiet_NaN();
			require(!settings.Valid());
			settings.*field = -1;
			require(!settings.Valid());
		}
		std::cout << "Grass bounded settings, threshold ordering and scene Hi-Z conflict policy passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
