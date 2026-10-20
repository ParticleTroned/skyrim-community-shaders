#include "Features/GrassOptimizations/GrassFadeHistory.h"
#include "Features/GrassOptimizations/GrassFrustum.h"
#include "Features/GrassOptimizations/GrassPolicy.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>
namespace
{
	void CheckSpawnFades()
	{
		const auto require = [](bool condition) {
			if (!condition)
				throw std::runtime_error("Grass spawn fade history failed");
		};
		GrassPolicy::FadeHistory<int> history;
		const auto fade = [&](int group, float now) {
			return std::clamp((now - history.Start(group)) / 2.f, 0.f, 1.f);
		};
		// Generation can precede GPU readiness and the first visible traversal by many frames.
		history.Generated(1, 10.f);
		require(fade(1, 10.f) == 0.f && fade(1, 11.f) == .5f);
		require(fade(1, 30.f) == 1.f && history.Capture(1) == 10.f);
		// Cache-loaded or already visible grass without a creation receipt must stay visible.
		require(fade(2, 30.f) == 1.f);
		require(history.Capture(2) == history.Start(2));
		require(fade(2, 30.f) == 1.f && fade(2, 60.f) == 1.f);
		// Finishing generation or recapturing must not restart surviving groups' fades.
		history.Generated(1, 40.f);
		history.Generated(2, 40.f);
		history.Generated(3, 40.f);
		require(fade(1, 40.f) == 1.f && fade(2, 40.f) == 1.f && fade(3, 40.f) == 0.f);
		for (int repeat = 0; repeat < 10; ++repeat) {
			require(history.Capture(1) == 10.f && history.Capture(3) == 40.f);
			require(history.Capture(2) == history.Start(2) && fade(2, 40.f) == 1.f);
		}
		require(fade(3, 42.f) == 1.f);
		// Reusing a removed group's native address is a new spawn, not a surviving group.
		history.Removed(1);
		history.Generated(1, 50.f);
		require(fade(1, 50.f) == 0.f && fade(1, 51.f) == .5f);
		require(fade(2, 51.f) == 1.f);
		// AddGroup may reuse storage without a separate observed removal.
		history.Generated(1, 55.f, true);
		require(fade(1, 55.f) == 0.f && fade(1, 56.f) == .5f);
		// Subtracting a short duration from a large float clock can round to the clock itself.
		require(std::clamp((65536.f - history.Start(4)) / .001f, 0.f, 1.f) == 1.f);
		history.Generated(4, std::numeric_limits<float>::quiet_NaN());
		require(fade(4, 50.f) == 1.f);
		// A fresh shape lifetime cannot inherit fade state from the previous save.
		history = {};
		require(fade(1, 70.f) == 1.f);
		history.Generated(1, 70.f);
		require(fade(1, 70.f) == 0.f);
	}

	void CheckWindHistory()
	{
		const auto require = [](bool condition) {
			if (!condition)
				throw std::runtime_error("Grass rendered wind history changed across passes or representatives");
		};
		GrassPolicy::WindHistory history;
		require(history.Previous(100, 10.f) == 10.f);
		// Preparation without submission cannot become the next frame's history.
		require(history.Previous(101, 11.f) == 11.f);
		history.Commit(101, 11.f);
		require(history.Previous(102, 12.f) == 11.f);
		history.Commit(102, 12.f);
		require(history.Previous(102, 12.f) == 11.f);
		history.Commit(102, 12.5f);
		require(history.Previous(102, 12.5f) == 11.f);
		require(history.Previous(103, 13.f) == 12.f);

		GrassPolicy::FrameQueue<int> queue;
		std::array<int, 2> cells{};
		for (uint32_t frame = 103; frame < 113; ++frame) {
			require(queue.Claim(frame, &cells[frame % 2]));
			const auto phase = float(frame - 90);
			require(history.Previous(frame, phase) == phase - 1);
			history.Commit(frame, phase);
			// Repreparing after a same-frame cache reset must retain the original previous phase.
			require(history.Previous(frame, phase) == phase - 1);
		}
		require(history.Previous(115, 25.f) == 25.f);
		history.Commit(115, 25.f);
		require(history.Previous(116, 26.f) == 25.f);
		history.Commit(116, std::numeric_limits<float>::quiet_NaN());
		require(history.Previous(117, 27.f) == 27.f);

		GrassPolicy::WindHistory wrapped;
		require(wrapped.Previous(UINT32_MAX, 30.f) == 30.f);
		wrapped.Commit(UINT32_MAX, 30.f);
		require(wrapped.Previous(0, 31.f) == 30.f);
		wrapped.Commit(0, 31.f);
		require(wrapped.Previous(0, 31.f) == 30.f);
		require(wrapped.Previous(1, 32.f) == 31.f);
		require(wrapped.Previous(3, 34.f) == 34.f);
	}
}

int main()
{
	try {
		CheckSpawnFades();
		CheckWindHistory();
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
		const auto dense = GrassPolicy::PlanCullCapacity({}, 32769, 1);
		require(dense && *dense == GrassPolicy::CullCapacity{ 65536, 1 });
		const auto stereoLOD = GrassPolicy::PlanCullCapacity(*dense, 64, 6);
		require(stereoLOD && *stereoLOD == GrassPolicy::CullCapacity{ 65536, 6 });
		require(GrassPolicy::PlanCullCapacity(*stereoLOD, 32769, 1) == stereoLOD);
		require(!GrassPolicy::PlanCullCapacity({}, 0, 1));
		require(!GrassPolicy::PlanCullCapacity({}, 1, 0));
		require(!GrassPolicy::PlanCullCapacity({}, UINT64_MAX, 1));
		require(!GrassPolicy::PlanCullCapacity({}, 1, 7));
		require(!GrassPolicy::PlanCullCapacity({ UINT32_MAX, 1 }, 1, 1));
		require(GrassPolicy::ReplacementFitsBudget(40, 60, 100));
		require(!GrassPolicy::ReplacementFitsBudget(40, 61, 100));
		require(!GrassPolicy::ReplacementFitsBudget(101, 0, 100));
		require(GrassPolicy::ReplacementFitsBudget(UINT64_MAX - 1, 1, UINT64_MAX));
		require(!GrassPolicy::ReplacementFitsBudget(UINT64_MAX, 1, UINT64_MAX));
		GrassPolicy::FrameQueue<int> queue;
		std::array<int, 16> shapes{};
		for (uint32_t frame : { UINT32_MAX - 1, UINT32_MAX, 0u, 1u }) {
			std::atomic_uint winners{ 0 };
			std::vector<std::thread> workers;
			for (auto& shape : shapes)
				workers.emplace_back([&queue, &winners, frame, identity = &shape]() {
					if (queue.Claim(frame, identity))
						++winners;
				});
			for (auto& worker : workers) worker.join();
			require(winners == 1 && queue.queuedFrame == frame);
			require(queue.representative >= shapes.data() && queue.representative < shapes.data() + shapes.size());
			require(!queue.Claim(frame, &shapes.front()));
		}
		require(GrassFrustum::WithinDistance(identity, { 0, 0, 0 }, { 10, 10, 10 }, 100));
		require(GrassFrustum::WithinDistance(identity, { 227, 0, 0 }, { 229, 1, 1 }, 100));
		require(!GrassFrustum::WithinDistance(identity, { 230, 0, 0 }, { 240, 1, 1 }, 100));
		auto rotated = identity;
		rotated[0] = rotated[5] = 0;
		rotated[1] = -1;
		rotated[4] = 1;
		rotated[3] = -200;
		require(GrassFrustum::WithinDistance(rotated, { 0, -210, 0 }, { 10, -190, 10 }, 100));
		require(!GrassFrustum::WithinDistance(rotated, { 1000, 0, 0 }, { 1010, 10, 10 }, 100));
		require(GrassFrustum::WithinDistance(rotated, { NAN, 0, 0 }, { 1, 1, 1 }, 100));
		rotated[15] = 0;
		require(GrassFrustum::WithinDistance(rotated, { 1000, 0, 0 }, { 1010, 10, 10 }, 100));
		require(GrassFrustum::DeformationPadding(identity, { 0, 0, 0 }, { 10, 10, 10 }, 20, 5, 100) == 65);
		require(GrassFrustum::DeformationPadding(identity, { 0, 0, 0 }, { 10, 10, 10 }, 20, 5, 100, 0) == 65);
		require(GrassFrustum::DeformationPadding(identity, { 0, 0, 0 }, { 10, 10, 10 }, 20, 5, 100, 3) == 145);
		require(GrassFrustum::DeformationPadding(identity, { 0, 0, 0 }, { 10, 10, 10 }, 20, 5, 100, 5) == 225);
		require(GrassFrustum::DeformationPadding(identity, { 1000, 0, 0 }, { 1010, 10, 10 }, 20, 5, 100, 5) == 25);
		require(GrassFrustum::DeformationPadding(identity, { 1000, 0, 0 }, { 1010, 10, 10 }, 20, 5, 100) == 25);
		require(GrassFrustum::DeformationPadding(identity, { 0, 0, 0 }, { 10, 10, 10 }, 20, 5, 0) == 25);
		require(GrassFrustum::DeformationPadding(identity, { NAN, 0, 0 }, { 1, 1, 1 }, 20, 5, 100) == 65);
		require(GrassFrustum::DeformationPadding(rotated, { 1000, 0, 0 }, { 1010, 10, 10 }, 20, 5, 100) == 65);
		// Every sampled root that can collide must retain the full displacement bound.
		for (int translation : { -500, 0, 500 }) {
			auto world = identity;
			world[3] = float(translation);
			for (int x = -1000; x <= 1000; x += 37) {
				const float lo = float(x), hi = lo + 100;
				const float padding = GrassFrustum::DeformationPadding(world, { lo, -10, -10 }, { hi, 10, 10 }, 20, 5, 100);
				require(padding == 25 || padding == 65);
				for (float root = lo; root <= hi; root += 5)
					if (std::abs(root + translation) <= 120)
						require(padding == 65);
			}
		}
		using Basis = std::array<std::array<float, 3>, 3>;
		const Basis unit{ std::array{ 1.f, 0.f, 0.f }, std::array{ 0.f, 1.f, 0.f }, std::array{ 0.f, 0.f, 1.f } };
		require(GrassPolicy::BasisNorm(unit) >= 1 && GrassPolicy::BasisNorm(unit) < 1.0001f);
		for (const auto& basis : { unit, Basis{ std::array{ 1.f, 2.f, -.5f }, std::array{ 0.f, .1f, 1.f }, std::array{ 3.f, 0.f, .1f } } }) {
			const float bound = GrassPolicy::BasisNorm(basis);
			for (int x = -4; x <= 4; ++x)
				for (int y = -4; y <= 4; ++y)
					for (int z = -4; z <= 4; ++z) {
						if (!x && !y && !z)
							continue;
						const float length = std::sqrt(float(x * x + y * y + z * z));
						float squared = 0;
						for (const auto& row : basis) {
							const float transformed = (row[0] * x + row[1] * y + row[2] * z) / length;
							squared += transformed * transformed;
						}
						require(std::sqrt(squared) <= bound);
					}
		}
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
		generated.Observe(200, true);
		require(generated.VisibleInFrame(200));
		generated.Observe(250, false);
		require(generated.IdleFrames(250) == 50 && !generated.VisibleInFrame(250));
		require(generated.IdleFrames(250, 249) == 1);
		require(generated.IdleFrames(250, 150) == 50);
		require(generated.IdleFrames(250, UINT64_MAX) == 50);
		GrassPolicy::Residency wrapped{ UINT32_MAX - 60, {} };
		require(wrapped.IdleFrames(59) == 120);
		require(wrapped.IdleFrames(59, UINT32_MAX) == 60);
		require(wrapped.IdleFrames(59, 58) == 1);
		wrapped.Observe(0, true);
		require(wrapped.VisibleInFrame(0) && wrapped.IdleFrames(0) == 0);
		require(GrassPolicy::MeshStride(0x8000000000000087ull) == 28);
		require(GrassPolicy::MeshStride(0x8000000000000080ull) == 0);
		require(GrassPolicy::MeshWeight(0) == 1);
		require(GrassPolicy::MeshWeight(24) == 1);
		require(GrassPolicy::MeshWeight(96) == 2);
		require(GrassPolicy::MeshWeight(384) == 4);
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
		std::cout << "Grass spawn fades, rendered wind history, bounded settings and threshold ordering passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
