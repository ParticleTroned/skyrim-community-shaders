#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace GrassPolicy
{
	inline constexpr uint32_t kRecordBytes = 32;
	inline constexpr uint32_t kMaxBatchInstances = 262144;
	inline constexpr uint32_t kMaxBatchSlices = 8192;
	inline constexpr uint32_t kMaxFrameSources = 4096;
	inline constexpr uint32_t kResidentIdleFrames = 120;
	inline constexpr uint64_t kMaxResidentRecordBytes = 128ull * 1024 * 1024;
	inline constexpr uint64_t kMaxCpuRecordBytes = 128ull * 1024 * 1024;
	inline constexpr uint64_t kMaxDormantRecordBytes = 16ull * 1024 * 1024;
	inline constexpr int kSceneHiZMode = 3;
	inline constexpr float kMaxCollisionDistance = 20480.0f;
	/** @brief The native descriptor stores mesh stride in four-byte units in its low nibble. */
	constexpr uint32_t MeshStride(uint64_t descriptor) { return uint32_t(descriptor & 0xF) * 4; }
	/** @brief Unsupported batch sizes retain native rendering before scratch allocation. */
	constexpr bool BatchCapacityValid(uint64_t instances, uint64_t slices)
	{
		return instances > 0 && instances <= kMaxBatchInstances && slices > 0 && slices <= kMaxBatchSlices;
	}
	/** @brief Replacing a resident snapshot must retain the bounded CPU record budget. */
	constexpr bool SnapshotBudgetValid(uint64_t resident, uint64_t previous, uint64_t incoming)
	{
		return previous <= resident && resident <= kMaxCpuRecordBytes && incoming <= kMaxCpuRecordBytes - (resident - previous);
	}
	/** @brief Only confirmed native visibility renews residency; generated sources receive an initial grace period. */
	struct Residency
	{
		uint32_t createdFrame = 0;
		std::optional<uint32_t> lastVisibleFrame;

		void Observe(uint32_t frame, bool nativeVisible)
		{
			if (nativeVisible)
				lastVisibleFrame = frame;
		}
		uint32_t IdleFrames(uint32_t frame) const { return frame - lastVisibleFrame.value_or(createdFrame); }
		bool Expired(uint32_t frame) const { return IdleFrames(frame) > kResidentIdleFrames; }
		bool VisibleInFrame(uint32_t frame) const { return lastVisibleFrame && *lastVisibleFrame == frame; }
	};

	struct Settings
	{
		bool Enabled = true;
		bool CrossCellBatching = true;
		bool FrustumCulling = true;
		bool DensityReduction = true;
		float MinPixelSize = 2.0f;
		float FullDetailPixelSize = 16.0f;
		float MinDensity = 0.03f;
		float MeshCostBias = 0.4f;
		float CostBiasStartDistance = 6000.0f;
		float InvisibleFadeCull = 0.0f;
		float RenderDistanceOverride = 0.0f;
		float EdgeFadeStart = 0.85f;
		float SimpleShadingPixelSize = 0.0f;
		float CollisionDistance = 2048.0f;
		bool EnableMeshLOD = false;
		bool EnableMidLOD = true;
		bool EnableFarLOD = true;
		float MidLODPixelSize = 8.0f;
		float FarLODPixelSize = 4.0f;
		float MeshLODBandPixels = 3.0f;
		bool EnableOcclusionCulling = true;
		float OcclusionBias = 0.001f;

		bool Valid() const
		{
			const auto range = [](float value, float lo, float hi) { return std::isfinite(value) && value >= lo && value <= hi; };
			return range(MinPixelSize, 0.0f, 64.0f) && range(FullDetailPixelSize, 0.01f, 256.0f) &&
			       FullDetailPixelSize > MinPixelSize && range(MinDensity, 0.0f, 1.0f) &&
			       range(MidLODPixelSize, 0.01f, 128.0f) && range(FarLODPixelSize, 0.01f, MidLODPixelSize) &&
			       range(MeshLODBandPixels, 0.01f, 32.0f) && range(OcclusionBias, 0.0f, 0.05f) &&
			       range(MeshCostBias, 0.0f, 1.0f) && range(CostBiasStartDistance, 0.0f, 20000.0f) &&
			       range(InvisibleFadeCull, 0.0f, 1.0f) && range(RenderDistanceOverride, 0.0f, 100000.0f) &&
			       range(EdgeFadeStart, 0.0f, 1.0f) && range(SimpleShadingPixelSize, 0.0f, 32.0f) &&
			       range(CollisionDistance, 0.0f, kMaxCollisionDistance);
		}
	};

	/** @brief Scene Hi-Z owns a separate depth path; permit grass occlusion only with native scene policies. */
	constexpr bool OcclusionAllowed(bool vr, int sceneMode) { return !vr || sceneMode != kSceneHiZMode; }
	/** @brief Native draw calls receive logical counts; VR expands them inside the draw function. */
	constexpr bool NativeCountMatches(uint32_t captured, uint32_t submitted)
	{
		return captured == submitted;
	}
}
