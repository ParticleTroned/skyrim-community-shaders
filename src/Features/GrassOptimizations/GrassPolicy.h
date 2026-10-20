#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>

namespace GrassPolicy
{
	inline constexpr uint32_t kRecordBytes = 32;
	inline constexpr uint32_t kSurvivorIndexBytes = sizeof(float);
	inline constexpr uint32_t kSurvivorBytes = kSurvivorIndexBytes + 6 * sizeof(float);
	inline constexpr uint32_t kMaxBatchInstances = 262144;
	static_assert(kMaxBatchInstances <= (1u << 24));
	inline constexpr uint32_t kMaxBatchSlices = 8192;
	inline constexpr uint32_t kMaxFrameSources = 4096;
	inline constexpr uint64_t kMaxResidentRecordBytes = 128ull * 1024 * 1024;
	inline constexpr uint64_t kMaxCpuRecordBytes = 128ull * 1024 * 1024;
	inline constexpr uint64_t kMaxCullResultBytes = 256ull * 1024 * 1024;
	inline constexpr uint32_t kMaxFrameSlices = 131072;
	inline constexpr uint64_t kMaxDormantRecordBytes = 16ull * 1024 * 1024;
	inline constexpr float kMaxCollisionDistance = 20480.0f;
	/** @brief Bound arbitrary packed bases tightly without assuming an orthogonal rotation. */
	inline float BasisNorm(const std::array<std::array<float, 3>, 3>& rows)
	{
		float trace = 0, maximum = 0;
		for (size_t row = 0; row < 3; ++row) {
			float sum = 0;
			for (size_t column = 0; column < 3; ++column) {
				float product = 0;
				for (size_t axis = 0; axis < 3; ++axis) product += rows[row][axis] * rows[column][axis];
				if (row == column)
					trace += product;
				sum += std::abs(product);
			}
			maximum = std::max(maximum, sum);
		}
		return std::sqrt(std::min(trace, maximum) * 1.00001f);
	}
	/** @brief The native descriptor stores mesh stride in four-byte units in its low nibble. */
	constexpr uint32_t MeshStride(uint64_t descriptor) { return uint32_t(descriptor & 0xF) * 4; }
	/** @brief Density and distance costs follow the drawable mesh selected for each LOD slot. */
	inline float MeshWeight(uint32_t indexCount) { return std::sqrt(std::max(1.0f, indexCount / 24.0f)); }
	/** @brief Unsupported batch sizes retain native rendering before scratch allocation. */
	constexpr bool BatchCapacityValid(uint64_t instances, uint64_t slices)
	{
		return instances > 0 && instances <= kMaxBatchInstances && slices > 0 && slices <= kMaxBatchSlices;
	}
	struct CullCapacity
	{
		uint32_t instances = 0, slots = 0;
		bool operator==(const CullCapacity&) const = default;
	};
	/** @brief Retained culling storage grows independently for instances and eye/LOD slots. */
	constexpr std::optional<CullCapacity> PlanCullCapacity(CullCapacity current, uint64_t instances, uint64_t slots)
	{
		if (!instances || instances > kMaxBatchInstances || !slots || slots > 6 ||
			current.instances > kMaxBatchInstances || current.slots > 6)
			return std::nullopt;
		return CullCapacity{ std::max(current.instances, std::bit_ceil(static_cast<uint32_t>(instances))),
			std::max(current.slots, static_cast<uint32_t>(slots)) };
	}
	/** @brief Replacement allocations coexist with retained buffers until the new bundle is complete. */
	constexpr bool ReplacementFitsBudget(uint64_t resident, uint64_t replacement, uint64_t budget)
	{
		return resident <= budget && replacement <= budget - resident;
	}
	/** @brief Float offset of the native wind vector in flat or stereo geometry constants. */
	constexpr uint32_t WindVectorOffset(bool vr) { return (vr ? 33u : 17u) * 4; }

	/** @brief Track rendered wind independently of the cell selected to represent a batch. */
	struct WindHistory
	{
		uint64_t renderedFrame = UINT64_MAX;
		float renderedTimer = 0, previousTimer = 0;

		/** @brief Reuse consecutive rendered history; reset motion after a gap. */
		float Previous(uint32_t frame, float currentTimer) const
		{
			if (renderedFrame == frame)
				return previousTimer;
			return renderedFrame <= UINT32_MAX && frame - uint32_t(renderedFrame) == 1 ? renderedTimer : currentTimer;
		}
		/** @brief Advance once after submission, preserving the previous phase for later passes. */
		void Commit(uint32_t frame, float currentTimer)
		{
			if (!std::isfinite(currentTimer)) {
				renderedFrame = UINT64_MAX;
				return;
			}
			if (renderedFrame == frame)
				return;
			previousTimer = Previous(frame, currentTimer);
			renderedTimer = currentTimer;
			renderedFrame = frame;
		}
	};

	/** @brief Native visibility workers queue one representative before rendering starts. */
	template <class Shape>
	struct FrameQueue
	{
		std::atomic_uint64_t queuedFrame{ UINT64_MAX };
		std::atomic<Shape*> representative{ nullptr };
		bool Claim(uint32_t frame, Shape* shape)
		{
			auto previous = queuedFrame.load(std::memory_order_acquire);
			while (previous != frame) {
				if (!queuedFrame.compare_exchange_weak(previous, frame, std::memory_order_acq_rel))
					continue;
				representative.store(shape, std::memory_order_release);
				return true;
			}
			return false;
		}
	};
	/** @brief Replacing a resident snapshot must retain the bounded CPU record budget. */
	constexpr bool SnapshotBudgetValid(uint64_t resident, uint64_t previous, uint64_t incoming)
	{
		return previous <= resident && resident <= kMaxCpuRecordBytes && incoming <= kMaxCpuRecordBytes - (resident - previous);
	}
	/** @brief Native observations guide pressure eviction; generation hooks own snapshot lifetime. */
	struct Residency
	{
		uint32_t createdFrame = 0;
		std::optional<uint32_t> lastVisibleFrame;

		void Observe(uint32_t frame, bool nativeVisible)
		{
			if (nativeVisible)
				lastVisibleFrame = frame;
		}
		uint32_t IdleFrames(uint32_t frame, uint64_t observedFrame = UINT64_MAX) const
		{
			const auto nativeAge = frame - lastVisibleFrame.value_or(createdFrame);
			return observedFrame <= UINT32_MAX ? std::min(nativeAge, frame - uint32_t(observedFrame)) : nativeAge;
		}
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

	/** @brief Native draw calls receive logical counts; VR expands them inside the draw function. */
	constexpr bool NativeCountMatches(uint32_t captured, uint32_t submitted)
	{
		return captured == submitted;
	}
}
