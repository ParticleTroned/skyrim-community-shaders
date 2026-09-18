#pragma once

#include "../DLSSViewportCrop.h"
#include "CharacterMultiRoi.h"
#include "ComputeSubrect.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace Util
{
	class PassTimingCapture;
}

namespace NeuralRendering
{
	/** Exact identity of the contents prepared for one logical eye evaluation. */
	struct CharacterPreparationKey
	{
		std::uint32_t frame = 0, sourceWorldFrame = 0, eye = 0, featureSlot = 0;
		std::uint64_t generation = 0, contentSerial = 0, settingsKey = 0, captureEpoch = 0;
		UpscalingDLSS::ViewportCrop crop{};
		float jitterX = 0.0f, jitterY = 0.0f;
		bool operator==(const CharacterPreparationKey&) const = default;
	};

	enum class CharacterSupportState : std::uint8_t
	{
		Unavailable,
		Pending,
		Ready,
		Failed
	};
	struct CharacterMaskSupportSnapshot
	{
		CharacterSupportState state = CharacterSupportState::Unavailable;
		const char* reason = "coverage_readback_not_requested";
		std::uint64_t pixels = 0;
	};

	/** Delayed GPU coverage belongs to these immutable mask contents, never a current slot. */
	class CharacterMaskSupportCapture
	{
	public:
		explicit CharacterMaskSupportCapture(CharacterPreparationKey key) : key_(key) {}
		[[nodiscard]] const CharacterPreparationKey& Key() const noexcept { return key_; }
		[[nodiscard]] CharacterMaskSupportSnapshot Snapshot() const noexcept
		{
			if (publicationFailed_.load(std::memory_order_acquire))
				return { CharacterSupportState::Unavailable, "coverage_publication_failed", 0 };
			try {
				std::scoped_lock lock(mutex_);
				return value_;
			} catch (...) {
				return { CharacterSupportState::Unavailable, "coverage_snapshot_failed", 0 };
			}
		}
		void Pending() noexcept { Publish({ CharacterSupportState::Pending, "coverage_readback_pending", 0 }); }
		void Complete(std::uint64_t pixels) noexcept { Publish({ CharacterSupportState::Ready, "", pixels }); }
		void Fail(const char* reason) noexcept { Publish({ CharacterSupportState::Failed, reason, 0 }); }

	private:
		void Publish(CharacterMaskSupportSnapshot value) noexcept
		{
			try {
				std::scoped_lock lock(mutex_);
				if (value_.state == CharacterSupportState::Ready || value_.state == CharacterSupportState::Failed)
					return;
				if (value.state == CharacterSupportState::Ready && value_.state != CharacterSupportState::Pending)
					return;
				value_ = value;
			} catch (...) {
				publicationFailed_.store(true, std::memory_order_release);
			}
		}
		const CharacterPreparationKey key_;
		mutable std::mutex mutex_;
		CharacterMaskSupportSnapshot value_{};
		std::atomic_bool publicationFailed_{ false };
	};

	/** One pre-decal source producer; byte counts describe logical texels, not driver allocation. */
	struct CharacterSourceEvidence
	{
		std::uint32_t sourceWorldFrame = 0, eyeWidth = 0, height = 0, eyeCount = 0;
		std::uint64_t captureSerial = 0;
		std::uint64_t captureEpoch = 0;
		std::array<ComputeSubrect, 2> copiedRects{};
		float jitterX = 0.0f, jitterY = 0.0f;
		bool empty = false, detectionCpuAvailable = false;
		const char* detectionCpuScope = "actor_admission_and_geometry_observation";
		std::uint64_t detectionCalls = 0, dispatchedPixels = 0, dispatchedThreads = 0;
		std::uint64_t logicalTextureBytes = 0, copiedLogicalBytes = 0, boundsReadbackBytes = 0;
		std::uint64_t logicalBoundsBytes = 0, boundsDispatchedThreads = 0;
		double detectionCpuMs = 0.0, captureCpuMs = 0.0;
		std::shared_ptr<Util::PassTimingCapture> captureTiming, boundsTiming;
	};

	/** Frozen preparation facts; only explicitly separate GPU completion handles can advance. */
	struct CharacterPreparationEvidence
	{
		CharacterPreparationKey key{};
		std::shared_ptr<const CharacterSourceEvidence> source;
		std::shared_ptr<Util::PassTimingCapture> maskTiming;
		std::shared_ptr<CharacterMaskSupportCapture> support;
		ComputeSubrect computeSubrect{}, dirtyDispatchRect{};
		CharacterComputeRegionPlan computeRegions{};
		bool requiresEvaluation = true, reused = false, prepared = false;
		const char* outcome = "unavailable";
		bool roiPlanningCpuAvailable = false, boundsPollCpuAvailable = false;
		bool boundsReady = false, boundsUsed = false;
		/** No CPU wait is issued by this producer; absence is distinct from a measured zero. */
		bool waitAvailable = false;
		const char* waitReason = "no_wait_issued";
		const char* boundsStatus = "not_requested";
		double preparationCpuMs = 0.0, roiPlanningCpuMs = 0.0, boundsPollCpuMs = 0.0;
		std::uint64_t dispatchedPixels = 0, dispatchedThreads = 0, clearedPixels = 0;
		std::uint64_t logicalMaskBytes = 0, logicalDiagnosticBytes = 0, copiedReadbackBytes = 0;
	};
}
