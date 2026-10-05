#include "Features/GrassOptimizations/GrassPolicy.h"
#include <iostream>
#include <limits>
#include <stdexcept>
int main()
{
	try {
		const auto require = [](bool condition) {if(!condition)throw std::runtime_error("Grass settings policy failed"); };
		GrassPolicy::Settings settings;
		require(settings.Valid());
		require(GrassPolicy::BatchCapacityValid(GrassPolicy::kMaxBatchInstances, GrassPolicy::kMaxBatchSlices));
		require(!GrassPolicy::BatchCapacityValid(GrassPolicy::kMaxBatchInstances + 1ull, 1));
		require(!GrassPolicy::BatchCapacityValid(1, GrassPolicy::kMaxBatchSlices + 1ull));
		require(!GrassPolicy::BatchCapacityValid(0, 1));
		require(!GrassPolicy::BatchCapacityValid(1, 0));
		require(!GrassPolicy::BatchCapacityValid(UINT64_MAX, UINT64_MAX));
		require(GrassPolicy::MeshStride(0x8000000000000087ull) == 28);
		require(GrassPolicy::MeshStride(0x8000000000000080ull) == 0);
		require(GrassPolicy::OcclusionAllowed(true, 0));
		require(GrassPolicy::OcclusionAllowed(true, 2));
		require(!GrassPolicy::OcclusionAllowed(true, 3));
		require(GrassPolicy::OcclusionAllowed(false, 3));
		require(GrassPolicy::NativeCountMatches(false, 10, 10));
		require(!GrassPolicy::NativeCountMatches(false, 10, 20));
		require(GrassPolicy::NativeCountMatches(true, 10, 20));
		require(!GrassPolicy::NativeCountMatches(true, 10, 21));
		require(!GrassPolicy::NativeCountMatches(true, UINT32_MAX, UINT32_MAX - 1));
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
				 &GrassPolicy::Settings::SimpleShadingPixelSize }) {
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
