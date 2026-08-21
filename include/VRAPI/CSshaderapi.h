#pragma once

#include <cstdint>

namespace CSX::ShaderAPI
{
	// Native shader-control ABI. Discover this process-lifetime function table
	// through CSserviceapi.h; do not link to CSX implementation symbols directly.
	// All calls are main-thread-affine. Returned strings are provider-owned and
	// remain valid only until the next call to the same function on that thread;
	// consumers must copy any string they retain.
	inline constexpr char ServiceName[] = "csx.shader";
	inline constexpr std::uint32_t ServiceMajor = 1;
	inline constexpr std::uint32_t ServiceMinor = 0;
	inline constexpr std::uint32_t SchemaRevision = 1;

	enum class Status : std::uint32_t
	{
		kSuccess = 0,
		kInvalidArgument = 1,
		kStructureTooSmall = 2,
		kUnavailable = 3,
		kWrongThread = 4,
		kRevisionConflict = 5,
		kPreflightRequired = 6,
		kPreflightExpired = 7,
		kPreflightMismatch = 8,
		kFeatureNotFound = 9,
		kBusy = 10,
		kBlocked = 11,
		kPersistenceFailed = 12,
		kInternalError = 13
	};

	enum class MutationAction : std::uint32_t
	{
		kSetCustomShaders = 1,
		kSetDiskCache = 2,
		kSetAsyncCompilation = 3,
		kSetSkipUnchangedShaders = 4,
		kSetFeatureDisabledAtBoot = 5,
		kClearMemoryCache = 6,
		kClearDiskCache = 7,
		kClearAllCaches = 8,
		kRestorePreviousDiskCache = 9,
		kAcceptCacheRebuild = 10,
		kStopCompilation = 11,
		kCaptureActiveShaders = 12
	};

	enum MutationFlag : std::uint64_t
	{
		kMutationNone = 0,
		kMutationPersist = 1ull << 0,
		kMutationAllowDisruptive = 1ull << 1,
		kMutationAllowDestructive = 1ull << 2
	};

	enum ServiceCapability : std::uint64_t
	{
		kCapabilitySnapshot = 1ull << 0,
		kCapabilityFeatureCatalog = 1ull << 1,
		kCapabilityRuntimeSettings = 1ull << 2,
		kCapabilityPersistentFeatureState = 1ull << 3,
		kCapabilityCacheLifecycle = 1ull << 4,
		kCapabilityCompilationControl = 1ull << 5,
		kCapabilityPreflightTokens = 1ull << 6
	};

	inline constexpr std::uint64_t ServiceCapabilities =
		kCapabilitySnapshot |
		kCapabilityFeatureCatalog |
		kCapabilityRuntimeSettings |
		kCapabilityPersistentFeatureState |
		kCapabilityCacheLifecycle |
		kCapabilityCompilationControl |
		kCapabilityPreflightTokens;

	struct Snapshot001
	{
		std::uint32_t structSize = sizeof(Snapshot001);
		std::uint32_t available = 0;
		std::uint64_t stateRevision = 0;
		std::uint64_t capabilities = 0;

		std::uint32_t customShadersRequested = 0;
		std::uint32_t customShadersEffective = 0;
		std::uint32_t customShaderTransitionPending = 0;
		std::uint32_t diskCacheRequested = 0;
		std::uint32_t diskCacheActive = 0;
		std::uint32_t diskCacheHeld = 0;
		std::uint32_t asyncCompilation = 0;
		std::uint32_t skipUnchangedShaders = 0;
		std::uint32_t compiling = 0;
		std::uint32_t activeShaderCapture = 0;
		std::uint32_t previousCacheAvailable = 0;
		std::uint32_t featureSetChanged = 0;
		std::uint32_t featureSetRevertPending = 0;
		std::uint32_t persistentMutationBlocked = 0;
		std::uint32_t saveLoadSafeModeActive = 0;

		std::uint64_t totalTasks = 0;
		std::uint64_t completedTasks = 0;
		std::uint64_t failedTasks = 0;
		std::uint64_t currentFailedShaders = 0;
		std::uint64_t memoryCacheHits = 0;
		std::uint64_t diskCacheHits = 0;
		std::uint64_t sourceCompiles = 0;
		std::uint64_t slowTasks = 0;
		std::uint64_t verySlowTasks = 0;
		std::uint32_t heavyTasksInFlight = 0;
		std::uint32_t foregroundThreadCount = 0;
		std::uint32_t backgroundThreadCount = 0;

		const char* buildId = nullptr;
		const char* shaderCacheAbiId = nullptr;
		const char* shaderCompilerIdentity = nullptr;
		const char* statisticsText = nullptr;
	};

	struct FeatureDescriptor001
	{
		std::uint32_t structSize = sizeof(FeatureDescriptor001);
		const char* shortName = nullptr;
		const char* displayName = nullptr;
		const char* version = nullptr;
		const char* category = nullptr;
		const char* shaderDefine = nullptr;
		const char* loadFailure = nullptr;
		std::uint32_t loaded = 0;
		std::uint32_t disabledAtBoot = 0;
		std::uint32_t runtimeDisabledByMissingDependency = 0;
		std::uint32_t core = 0;
		std::uint32_t visibleInMenu = 0;
		std::uint32_t hiddenFromUser = 0;
		std::uint32_t supportsVR = 0;
		std::uint32_t contributesShaderDefines = 0;
	};

	struct MutationRequest001
	{
		std::uint32_t structSize = sizeof(MutationRequest001);
		MutationAction action = MutationAction::kSetCustomShaders;
		std::uint64_t expectedStateRevision = 0;
		std::uint64_t flags = kMutationNone;
		std::uint32_t boolValue = 0;
		const char* featureName = nullptr;
		const char* preflightToken = nullptr;
	};

	struct Preflight001
	{
		std::uint32_t structSize = sizeof(Preflight001);
		Status status = Status::kInternalError;
		std::uint32_t allowed = 0;
		std::uint32_t disruptive = 0;
		std::uint32_t destructive = 0;
		std::uint32_t restartRequired = 0;
		std::uint32_t shaderRecompileExpected = 0;
		std::uint64_t stateRevision = 0;
		std::uint64_t requiredFlags = kMutationNone;
		const char* token = nullptr;
		const char* reasonCode = nullptr;
		const char* message = nullptr;
	};

	struct MutationReceipt001
	{
		std::uint32_t structSize = sizeof(MutationReceipt001);
		Status status = Status::kInternalError;
		std::uint32_t applied = 0;
		std::uint32_t changed = 0;
		std::uint32_t pending = 0;
		std::uint32_t restartRequired = 0;
		std::uint32_t shaderRecompileExpected = 0;
		std::uint32_t persistenceRequested = 0;
		std::uint32_t persisted = 0;
		std::uint64_t previousStateRevision = 0;
		std::uint64_t stateRevision = 0;
		const char* message = nullptr;
	};

	struct Interface001
	{
		std::uint32_t structSize = sizeof(Interface001);
		std::uint32_t major = ServiceMajor;
		std::uint32_t minor = ServiceMinor;
		std::uint32_t schemaRevision = SchemaRevision;
		std::uint64_t capabilities = 0;
		const void* context = nullptr;

		Status (*GetSnapshot)(const void* context, Snapshot001* output) = nullptr;
		std::uint32_t (*GetFeatureCount)(const void* context) = nullptr;
		Status (*GetFeatureDescriptor)(const void* context, std::uint32_t index, FeatureDescriptor001* output) = nullptr;
		Status (*Preflight)(const void* context, const MutationRequest001* request, Preflight001* output) = nullptr;
		Status (*Execute)(const void* context, const MutationRequest001* request, MutationReceipt001* output) = nullptr;
	};
}
