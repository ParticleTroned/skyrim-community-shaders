#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#include <array>
#include <cstdint>
#include <source_location>

namespace VRRenderScaleRetryTelemetry
{
	inline constexpr uint32_t kCapacity = 1024;
	inline constexpr uint32_t kViewportRoles = 3;
	inline constexpr uint32_t kNoSlot = UINT32_MAX;

	enum class EventType : uint8_t
	{
		Retry,
		RelatchAdmitted,
		Applied,
		Stable,
		Failure,
		ViewportReady,
		ViewportWaitBegin,
		ViewportWaitEnd,
		GuardArmed,
		ProofRevoked,
		SettleGuardSatisfied,
		PromotionCandidate,
		Promoted,
		GuardCleared,
		RelatchDrainBegin,
		RelatchDrainPending,
		RelatchDrainReady,
		RelatchDrainInvalidated,
		RelatchCommitBegin,
		RelatchSharedCleanup
	};

	enum class FenceResult : uint8_t { NotPolled, Pending, Ready, Failed };

	/** @brief Observations from existing viewport checks; never requests a GPU operation. */
	struct ViewportObservation
	{
		uint32_t role = 0;
		uint32_t slot = kNoSlot;
		bool cacheHit = false;
		bool victimValid = false;
		uint32_t victimQuality = 0;
		uint32_t victimPreset = 0;
		uint64_t victimLastUse = 0;
		bool fenceAlreadyPending = false;
		FenceResult fenceResult = FenceResult::NotPolled;
		const char* reason = "not_observed";
	};

	struct Context
	{
		uint64_t sessionID = 0;
		uint64_t requestID = 0;
		uint64_t transitionEpoch = 0;
		uint32_t method = 0;
		uint32_t qualityMode = 0;
		uint32_t dlssPreset = 0;
	};

	struct Event
	{
		Context context{};
		uint64_t sequence = 0;
		EventType type = EventType::Retry;
		const char* reason = "unspecified";
		const char* sourceFile = "";
		uint32_t sourceLine = 0;
		uint32_t retryKind = 0;
		uint64_t qpc = 0;
		uint32_t frame = 0;
		uint32_t generation = 0;
		uint64_t beginSequence = 0;
		uint64_t beginQpc = 0;
		uint32_t beginFrame = 0;
		uint32_t pendingObservations = 0;
		bool viewportObserved = false;
		ViewportObservation viewport{};
		uint32_t guardStartFrame = 0;
		uint32_t minimumSettleFrames = 0;
		uint32_t stableCycles = 0;
		uint32_t requiredStableCycles = 0;
		bool proofDrivenRelease = false;
		bool settleGuardRequired = false;
	};

	struct ViewportState
	{
		bool observed = false;
		bool pending = false;
		bool failed = false;
		uint32_t generation = 0;
		Event begin{};
		uint32_t pendingObservations = 0;
	};

	struct State
	{
		bool active = false;
		uint64_t sessionID = 0;
		uint64_t qpcFrequency = 0;
		uint64_t nextSequence = 1;
		uint32_t nextIndex = 0;
		uint32_t count = 0;
		uint64_t overwrittenEvents = 0;
		Context guardContext{};
		bool settleGuardObserved = false;
		std::array<ViewportState, kViewportRoles> viewports{};
		std::array<Event, kCapacity> events{};
	};
}
#endif
