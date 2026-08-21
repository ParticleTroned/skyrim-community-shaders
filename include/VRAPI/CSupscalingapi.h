#pragma once

#include <cstdint>
#include <limits>

namespace CSX::UpscalingAPI
{
	inline constexpr char ServiceName[] = "csx.upscaling";
	inline constexpr std::uint32_t ServiceMajor = 1;
	inline constexpr std::uint32_t ServiceMinor = 0;
	inline constexpr std::uint32_t SchemaRevision = 1;
	inline constexpr std::uint64_t AnyStateRevision = std::numeric_limits<std::uint64_t>::max();
	inline constexpr std::uint32_t MaximumClientIdLength = 128;
	inline constexpr std::uint32_t MaximumCommandIdLength = 128;
	inline constexpr std::uint32_t MaximumReasonLength = 256;

	/** Function-call status. A successful preflight may still decide to block. */
	enum class Status : std::uint32_t
	{
		kSuccess = 0,
		kInvalidArgument = 1,
		kStructureTooSmall = 2,
		kUnsupportedRuntime = 3,
		kUnsupportedProfile = 4,
		kServiceUnavailable = 5,
		kStateConflict = 6,
		kBlocked = 7,
		kBusy = 8,
		kIdempotencyConflict = 9,
		kOperationNotFound = 10,
		kBufferTooSmall = 11,
		kInternalError = 12
	};

	enum class RuntimeKind : std::uint32_t
	{
		kUnknown = 0,
		kSkyrimSE = 1,
		kSkyrimAE = 2,
		kSkyrimVR = 3
	};

	/** Stable values; support and current availability are reported separately. */
	enum class Method : std::uint32_t
	{
		kNone = 0,
		kTAA = 1,
		kFSR = 2,
		kDLSS = 3
	};

	/** Shared DLSS/FSR/FSR4 quality selection. */
	enum class QualityMode : std::uint32_t
	{
		kNativeAA = 0,
		kHoshipa = 1,
		kUltraQuality = 2,
		kQuality = 3,
		kBalanced = 4,
		kPerformance = 5,
		kUltraPerformance = 6
	};

	/** DLSS model-preset selection. All six values are part of schema 1. */
	enum class DLSSProfile : std::uint32_t
	{
		kJ = 0,
		kK = 1,
		kL = 2,
		kM = 3,
		kF = 4,
		kE = 5
	};

	/** FSR4 is an FSR runtime selection, not a separate upscale method. */
	enum class FSRRuntime : std::uint32_t
	{
		kFSR3 = 0,
		kFSR4 = 1
	};

	enum class RenderScaleStatus : std::uint32_t
	{
		kDisabled = 0,
		kIneligibleMethod = 1,
		kNativeQuality = 2,
		kRuntimeBlocked = 3,
		kPendingRelatch = 4,
		kActive = 5,
		kRestartRequired = 6
	};

	enum class TransitionState : std::uint32_t
	{
		kIdle = 0,
		kRequested = 1,
		kWaitingForSafePoint = 2,
		kPreparing = 3,
		kApplying = 4,
		kStabilizing = 5,
		kActive = 6
	};

	enum class RequestPurpose : std::uint32_t
	{
		kDirect = 0,
		/** Allows CSX to admit a valid environment-profile loading-door handoff. */
		kEnvironmentProfileTransition = 1
	};

	enum class PersistencePolicy : std::uint32_t
	{
		kRuntimeOnly = 0,
		/** Persist only after the requested physical state becomes stable. */
		kPersistWhenStable = 1
	};

	enum class PreflightDecision : std::uint32_t
	{
		kNoChange = 0,
		kApplySynchronously = 1,
		kQueue = 2,
		kBlocked = 3,
		kUnsupported = 4
	};

	enum class AdmissionRoute : std::uint32_t
	{
		kNone = 0,
		kDirect = 1,
		kDeferredSafePoint = 2,
		kLoadingDoorHandoff = 3
	};

	enum class ApplyDisposition : std::uint32_t
	{
		kRejected = 0,
		kNoChange = 1,
		kAppliedSynchronously = 2,
		kQueued = 3
	};

	enum class OperationState : std::uint32_t
	{
		kQueued = 0,
		kWaitingForSafePoint = 1,
		kPreparing = 2,
		kApplying = 3,
		kStabilizing = 4,
		kCompleted = 5,
		kFailed = 6,
		kSuperseded = 7
	};

	enum class EventType : std::uint32_t
	{
		kAccepted = 0,
		kQueued = 1,
		kStateChanged = 2,
		kCompleted = 3,
		kFailed = 4,
		kSuperseded = 5,
		kPersisted = 6
	};

	/** Detailed service abilities. Runtime availability remains dynamic. */
	enum Capability : std::uint64_t
	{
		kCapabilityNone = 0,
		kCapabilityInspection = 1ull << 0,
		kCapabilityAtomicProfileMutation = 1ull << 1,
		kCapabilityPersistentMutation = 1ull << 2,
		kCapabilityAsynchronousTransitions = 1ull << 3,
		kCapabilityOptimisticConcurrency = 1ull << 4,
		kCapabilityIdempotentCommands = 1ull << 5,
		kCapabilityEventJournal = 1ull << 6,
		kCapabilityVRRenderScaleMode = 1ull << 7,
		kCapabilityFSRRuntimeSelection = 1ull << 8
	};

	/** Coherent snapshot fields which are currently authoritative. */
	enum ProfilePresence : std::uint32_t
	{
		kProfileConfigured = 1u << 0,
		kProfileRequested = 1u << 1,
		kProfileApplying = 1u << 2,
		kProfileEffective = 1u << 3,
		kProfileStable = 1u << 4,
		kProfilePersisted = 1u << 5
	};

	enum SnapshotFlag : std::uint64_t
	{
		kSnapshotProviderCheckComplete = 1ull << 0,
		kSnapshotTransitionActive = 1ull << 1,
		kSnapshotRestartRequired = 1ull << 2,
		kSnapshotRenderScaleRequested = 1ull << 3,
		kSnapshotRenderScaleLatched = 1ull << 4,
		kSnapshotRenderScaleActive = 1ull << 5,
		kSnapshotPersistedStateKnown = 1ull << 6
	};

	enum OperationFlag : std::uint64_t
	{
		kOperationPersistenceRequested = 1ull << 0,
		kOperationPhysicalStateStable = 1ull << 1,
		kOperationPersisted = 1ull << 2
	};

	/**
	 * Conditions are observations, not all unconditional blockers. For example,
	 * a loading transition can be admitted through the loading-door handoff route.
	 */
	enum Condition : std::uint64_t
	{
		kConditionNone = 0,
		kConditionRaceSexMenu = 1ull << 0,
		kConditionRaceSexStartupTail = 1ull << 1,
		kConditionLoadingTransition = 1ull << 2,
		kConditionRelatchPending = 1ull << 3,
		kConditionTransitionPending = 1ull << 4,
		kConditionOpenCompositeUpscaling = 1ull << 5,
		kConditionFirstWorldFramePending = 1ull << 6,
		kConditionPostLoadRecovery = 1ull << 7,
		kConditionProviderCheckPending = 1ull << 8,
		kConditionProviderUnavailable = 1ull << 9,
		kConditionRestartRequired = 1ull << 10,
		kConditionPersistenceUnavailable = 1ull << 11,
		kConditionResourceRecovery = 1ull << 12
	};

	/** A complete configuration. Backend selections remain meaningful while inactive. */
	struct Profile001
	{
		std::uint32_t structSize = sizeof(Profile001);
		Method method = Method::kNone;
		QualityMode qualityMode = QualityMode::kNativeAA;
		std::uint32_t renderScaleMode = 0;
		DLSSProfile dlssProfile = DLSSProfile::kK;
		FSRRuntime fsrRuntime = FSRRuntime::kFSR3;
	};

	struct Capabilities001
	{
		std::uint32_t structSize = sizeof(Capabilities001);
		std::uint64_t revision = 0;
		RuntimeKind runtime = RuntimeKind::kUnknown;
		std::uint64_t capabilities = kCapabilityNone;
		std::uint64_t supportedMethodMask = 0;
		std::uint64_t availableMethodMask = 0;
		std::uint64_t pendingMethodMask = 0;
		std::uint64_t supportedQualityModeMask = 0;
		std::uint64_t supportedDLSSProfileMask = 0;
		std::uint64_t supportedFSRRuntimeMask = 0;
		/** One entry per Method value (None, TAA, FSR, DLSS). */
		std::uint64_t methodUnavailableConditions[4]{};
		/** One entry per FSRRuntime value (FSR3, FSR4). */
		std::uint64_t fsrRuntimeUnavailableConditions[2]{};
		/** Actual render scale for each QualityMode value. */
		float qualityResolutionScales[7]{};
		std::uint32_t maximumEventPageSize = 0;
		std::uint32_t eventRetentionCapacity = 0;
		std::uint64_t commandRetentionMilliseconds = 0;
	};

	struct Snapshot001
	{
		std::uint32_t structSize = sizeof(Snapshot001);
		std::uint64_t stateRevision = 0;
		std::uint64_t capabilityRevision = 0;
		std::uint32_t profilePresence = 0;
		std::uint64_t flags = 0;
		std::uint64_t observedConditions = kConditionNone;
		TransitionState transitionState = TransitionState::kIdle;
		RenderScaleStatus renderScaleStatus = RenderScaleStatus::kDisabled;
		std::uint64_t activeOperationId = 0;
		Profile001 configured{};
		Profile001 requested{};
		Profile001 applying{};
		Profile001 effective{};
		Profile001 stable{};
		Profile001 persisted{};
		std::uint32_t displayEyeWidth = 0;
		std::uint32_t displayEyeHeight = 0;
		std::uint32_t renderEyeWidth = 0;
		std::uint32_t renderEyeHeight = 0;
	};

	struct PreflightRequest001
	{
		std::uint32_t structSize = sizeof(PreflightRequest001);
		std::uint64_t expectedStateRevision = AnyStateRevision;
		RequestPurpose purpose = RequestPurpose::kDirect;
		PersistencePolicy persistence = PersistencePolicy::kRuntimeOnly;
		Profile001 target{};
	};

	struct PreflightResult001
	{
		std::uint32_t structSize = sizeof(PreflightResult001);
		std::uint64_t evaluatedStateRevision = 0;
		PreflightDecision decision = PreflightDecision::kBlocked;
		AdmissionRoute admissionRoute = AdmissionRoute::kNone;
		std::uint64_t observedConditions = kConditionNone;
		std::uint64_t blockingConditions = kConditionNone;
		std::uint32_t retryable = 0;
		std::uint32_t requiresRestart = 0;
		std::uint32_t willPersist = 0;
		Profile001 normalizedTarget{};
		std::uint32_t predictedDisplayEyeWidth = 0;
		std::uint32_t predictedDisplayEyeHeight = 0;
		std::uint32_t predictedRenderEyeWidth = 0;
		std::uint32_t predictedRenderEyeHeight = 0;
	};

	/** clientId + commandId is the idempotency key. Input strings are copied. */
	struct ApplyRequest001
	{
		std::uint32_t structSize = sizeof(ApplyRequest001);
		const char* clientId = nullptr;
		const char* commandId = nullptr;
		const char* reason = nullptr;
		std::uint64_t expectedStateRevision = AnyStateRevision;
		RequestPurpose purpose = RequestPurpose::kDirect;
		PersistencePolicy persistence = PersistencePolicy::kRuntimeOnly;
		Profile001 target{};
	};

	struct ApplyResult001
	{
		std::uint32_t structSize = sizeof(ApplyResult001);
		Status status = Status::kInternalError;
		ApplyDisposition disposition = ApplyDisposition::kRejected;
		AdmissionRoute admissionRoute = AdmissionRoute::kNone;
		std::uint32_t idempotentReplay = 0;
		std::uint32_t retryable = 0;
		std::uint32_t requiresRestart = 0;
		std::uint32_t willPersist = 0;
		std::uint64_t admittedStateRevision = 0;
		std::uint64_t resultingStateRevision = 0;
		std::uint64_t operationId = 0;
		std::uint64_t observedConditions = kConditionNone;
		std::uint64_t blockingConditions = kConditionNone;
		Profile001 normalizedTarget{};
	};

	struct OperationSnapshot001
	{
		std::uint32_t structSize = sizeof(OperationSnapshot001);
		std::uint64_t operationId = 0;
		OperationState state = OperationState::kQueued;
		Status result = Status::kSuccess;
		std::uint64_t flags = 0;
		std::uint64_t acceptedStateRevision = 0;
		std::uint64_t latestStateRevision = 0;
		std::uint64_t observedConditions = kConditionNone;
		std::uint64_t blockingConditions = kConditionNone;
		std::uint64_t eventIndex = 0;
		Profile001 target{};
		Profile001 effective{};
	};

	struct EventQuery001
	{
		std::uint32_t structSize = sizeof(EventQuery001);
		std::uint64_t afterEventId = 0;
		std::uint64_t operationId = 0;
		std::uint32_t limit = 100;
	};

	struct Event001
	{
		std::uint32_t structSize = sizeof(Event001);
		std::uint64_t eventId = 0;
		std::uint64_t operationId = 0;
		std::uint64_t eventIndex = 0;
		std::uint64_t stateRevision = 0;
		EventType type = EventType::kAccepted;
		OperationState operationState = OperationState::kQueued;
		Status result = Status::kSuccess;
		std::uint64_t observedConditions = kConditionNone;
	};

	struct EventPage001
	{
		std::uint32_t structSize = sizeof(EventPage001);
		std::uint32_t returnedEventCount = 0;
		std::uint64_t oldestRetainedEventId = 0;
		std::uint64_t latestEventId = 0;
		std::uint64_t nextEventId = 0;
		std::uint32_t cursorExpired = 0;
		std::uint32_t moreAvailable = 0;
	};

	/**
	 * Process-lifetime function table returned by the CSX service registry.
	 * All functions are callable from any thread, retain no caller pointers, and
	 * never throw across the DLL boundary. Mutation is scheduled internally.
	 */
	struct Interface001
	{
		std::uint32_t structSize = sizeof(Interface001);
		std::uint32_t abiMajor = ServiceMajor;
		std::uint32_t abiMinor = ServiceMinor;
		const void* context = nullptr;

		Status (*GetCapabilities)(const void* context, Capabilities001* output) = nullptr;
		Status (*GetSnapshot)(const void* context, Snapshot001* output) = nullptr;
		Status (*PreflightProfile)(const void* context, const PreflightRequest001* request, PreflightResult001* output) = nullptr;
		Status (*ApplyProfile)(const void* context, const ApplyRequest001* request, ApplyResult001* output) = nullptr;
		Status (*GetOperation)(const void* context, std::uint64_t operationId, OperationSnapshot001* output) = nullptr;
		Status (*ReadEvents)(const void* context, const EventQuery001* query, Event001* events, std::uint32_t eventCapacity, EventPage001* outputPage) = nullptr;
	};
}
