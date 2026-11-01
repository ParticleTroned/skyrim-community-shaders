#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

class Profiler;

namespace Util
{
	enum class PassTimingState : uint8_t
	{
		Unavailable,
		Pending,
		Ready,
		Failed
	};

	inline const char* PassTimingStateName(PassTimingState state)
	{
		switch (state) {
		case PassTimingState::Pending:
			return "pending";
		case PassTimingState::Ready:
			return "ready";
		case PassTimingState::Failed:
			return "failed";
		default:
			return "unavailable";
		}
	}

	struct PassTimingSnapshot
	{
		uint64_t captureId = 0;
		PassTimingState cpuState = PassTimingState::Unavailable;
		PassTimingState gpuState = PassTimingState::Unavailable;
		const char* cpuReason = "scope_not_entered";
		const char* gpuReason = "scope_not_entered";
		float cpuInclusiveMs = 0.0f;
		float cpuSelfMs = 0.0f;
		float gpuInclusiveMs = 0.0f;
		float gpuSelfMs = 0.0f;
		uint32_t capturedFrame = std::numeric_limits<uint32_t>::max();
		bool detailOnly = false;
	};

	/** @brief One invocation's retained result; caller owns identity and allocates only while capturing. */
	class PassTimingCapture
	{
	public:
		PassTimingCapture() noexcept
		{
			snapshot.captureId = captureId;
			if (!captureId) {
				snapshot.cpuState = snapshot.gpuState = PassTimingState::Failed;
				snapshot.cpuReason = snapshot.gpuReason = "capture_identity_exhausted";
			}
		}

		PassTimingSnapshot Read() const noexcept
		{
			try {
				const std::scoped_lock lock(mutex);
				if (!updateFailed.load(std::memory_order_relaxed))
					return snapshot;
			} catch (...) {
			}
			PassTimingSnapshot failed;
			failed.captureId = captureId;
			failed.cpuState = failed.gpuState = PassTimingState::Failed;
			failed.cpuReason = failed.gpuReason = "timing_capture_failed";
			return failed;
		}

	private:
		friend class ::Profiler;
		static uint64_t AllocateId() noexcept
		{
			static std::atomic<uint64_t> next{ 1 };
			auto candidate = next.load(std::memory_order_relaxed);
			while (candidate != std::numeric_limits<uint64_t>::max())
				if (next.compare_exchange_weak(candidate, candidate + 1, std::memory_order_relaxed))
					return candidate;
			return 0;
		}
		template <class Callback>
		void Update(Callback&& callback) noexcept
		{
			try {
				const std::scoped_lock lock(mutex);
				std::forward<Callback>(callback)(snapshot, claimed);
			} catch (...) {
				updateFailed.store(true, std::memory_order_relaxed);
			}
		}
		const uint64_t captureId = AllocateId();
		mutable std::mutex mutex;
		std::atomic_bool updateFailed{ false };
		PassTimingSnapshot snapshot;
		bool claimed = false;
	};

	using PassTimingHandle = std::shared_ptr<PassTimingCapture>;

	/** @brief Reads without querying or waiting for the GPU; null means capture was not armed. */
	inline PassTimingSnapshot ReadPassTiming(const PassTimingHandle& handle) noexcept
	{
		if (handle)
			return handle->Read();
		PassTimingSnapshot result;
		result.cpuReason = result.gpuReason = "capture_disabled";
		return result;
	}
}
