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
		RelatchSharedCleanup,
		OwnedReleaseConsumed,
		OwnedTargetPublished,
		OwnedProviderPrepared,
		OwnedReleaseEligibility
	};

	enum class FenceResult : uint8_t { NotPolled, Pending, Ready, Failed };
	enum class DrainFenceRole : uint8_t
	{
		FSRHost,
		FSRInterop,
		FSRRuntime,
		DLSSHost
	};
	enum OwnedReleaseObligation : uint32_t
	{
		OldProviderDrained = 1u << 0,
		ProviderResetCompleted = 1u << 1,
		DetachedRetirementOwned = 1u << 2,
		PhysicalContractPublished = 1u << 3,
		TargetProviderPrepared = 1u << 4,
		CoherentStereo = 1u << 5
	};

	/** Observations of existing fence operations; identities are opaque, never dereferenced. */
	struct DrainFenceObservation
	{
		bool observed = false;
		DrainFenceRole role = DrainFenceRole::FSRHost;
		FenceResult result = FenceResult::NotPolled;
		uint64_t issueQpc = 0;
		uint64_t readyQpc = 0;
		uint64_t deviceIdentity = 0;
		uint64_t contextIdentity = 0;
		uint64_t queueIdentity = 0;
		uint64_t fenceIdentity = 0;
		uint64_t fenceValue = 0;
	};

	/** Historical ownership certificate; recording it does not authorize reuse of a live drain proof. */
	struct OwnedReleaseObservation
	{
		bool observed = false;
		uint32_t sourceGeneration = 0;
		uint32_t requiredProviders = 0;  // FSR = 1, DLSS = 2.
		uint64_t fsrRevision = 0;
		uint64_t dlssRevision = 0;
		uint64_t targetFSRRevision = 0;
		uint64_t targetDLSSRevision = 0;
		uint64_t fsrTicketSerial = 0;
		uint64_t dlssTicketSerial = 0;
		uint64_t certificateSerial = 0;
		uint64_t targetQueueIdentity = 0;
		uint64_t targetFenceIdentity = 0;
		uint64_t deviceIdentity = 0;
		uint64_t contextIdentity = 0;
		uint64_t queueIdentity = 0;
		uint64_t requestQueuedQpc = 0;
		uint64_t blockingCleanupReadyQpc = 0;
		uint32_t requiredObligations = 0;
		uint32_t satisfiedObligations = 0;
		bool oldProofConsumed = false;
		bool targetPublished = false;
		bool providerPrepared = false;
		bool eligible = false;
		bool presentationEligibility = false;
	};

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
		OwnedReleaseObservation ownedRelease{};
		std::array<DrainFenceObservation, 4> drainFences{};
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
