#pragma once

#include <cstdint>

namespace CSX::WeatherAPI
{
	// Discover this process-lifetime function table through CSserviceapi.h.
	// Calls are main-thread-affine. Returned strings are provider-owned and are
	// valid only until the next call to the same function on that thread.
	inline constexpr char ServiceName[] = "csx.weather";
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
		kWeatherNotFound = 9,
		kFeatureNotFound = 10,
		kVariableNotFound = 11,
		kBusy = 12,
		kBlocked = 13,
		kPersistenceFailed = 14,
		kInvalidOverride = 15,
		kInternalError = 16
	};

	enum class MutationAction : std::uint32_t
	{
		kSetWeather = 1,
		kPreviewWeather = 2,
		kResetWeather = 3,
		kLockWeather = 4,
		kUnlockWeather = 5,
		kSetFeaturePaused = 6,
		kReloadOverrides = 7,
		kSetFeatureOverride = 8,
		kRemoveFeatureOverride = 9
	};

	enum MutationFlag : std::uint64_t
	{
		kMutationNone = 0,
		kMutationPersist = 1ull << 0,
		kMutationApplyLive = 1ull << 1,
		kMutationAllowDisruptive = 1ull << 2,
		kMutationAllowDestructive = 1ull << 3
	};

	enum ServiceCapability : std::uint64_t
	{
		kCapabilitySnapshot = 1ull << 0,
		kCapabilityWeatherCatalog = 1ull << 1,
		kCapabilityFeatureCatalog = 1ull << 2,
		kCapabilityVariableCatalog = 1ull << 3,
		kCapabilityOverrideInspection = 1ull << 4,
		kCapabilityRuntimeSelection = 1ull << 5,
		kCapabilityWeatherLock = 1ull << 6,
		kCapabilityRuntimeOverrides = 1ull << 7,
		kCapabilityPersistentOverrides = 1ull << 8,
		kCapabilityPreflightTokens = 1ull << 9
	};

	inline constexpr std::uint64_t ServiceCapabilities =
		kCapabilitySnapshot |
		kCapabilityWeatherCatalog |
		kCapabilityFeatureCatalog |
		kCapabilityVariableCatalog |
		kCapabilityOverrideInspection |
		kCapabilityRuntimeSelection |
		kCapabilityWeatherLock |
		kCapabilityRuntimeOverrides |
		kCapabilityPersistentOverrides |
		kCapabilityPreflightTokens;

	enum class ValueKind : std::uint32_t
	{
		kUnknown = 0,
		kBoolean = 1,
		kInteger = 2,
		kNumber = 3,
		kString = 4,
		kArray = 5,
		kObject = 6
	};

	struct Snapshot001
	{
		std::uint32_t structSize = sizeof(Snapshot001);
		std::uint32_t available = 0;
		std::uint32_t skyAvailable = 0;
		std::uint64_t stateRevision = 0;
		std::uint64_t capabilities = 0;
		std::uint32_t transitionActive = 0;
		float transitionFactor = 1.0f;
		std::uint32_t weatherLocked = 0;
		std::uint32_t lockHooksInstalled = 0;
		std::uint32_t persistentMutationBlocked = 0;
		std::uint32_t saveLoadSafeModeActive = 0;
		std::uint32_t weatherCount = 0;
		std::uint32_t weatherFeatureCount = 0;
		const char* currentWeatherKey = nullptr;
		const char* lastWeatherKey = nullptr;
		const char* defaultWeatherKey = nullptr;
		const char* overrideWeatherKey = nullptr;
		const char* lockedWeatherKey = nullptr;
		const char* buildId = nullptr;
	};

	struct WeatherDescriptor001
	{
		std::uint32_t structSize = sizeof(WeatherDescriptor001);
		const char* weatherKey = nullptr;
		const char* editorId = nullptr;
		const char* displayName = nullptr;
		const char* pluginName = nullptr;
		std::uint32_t runtimeFormId = 0;
		std::uint32_t localFormId = 0;
		std::uint32_t weatherFlags = 0;
		std::uint32_t isCurrent = 0;
		std::uint32_t isLast = 0;
		std::uint32_t isDefault = 0;
		std::uint32_t isOverride = 0;
		std::uint32_t isLocked = 0;
	};

	struct FeatureDescriptor001
	{
		std::uint32_t structSize = sizeof(FeatureDescriptor001);
		const char* shortName = nullptr;
		const char* displayName = nullptr;
		std::uint32_t loaded = 0;
		std::uint32_t paused = 0;
		std::uint32_t variableCount = 0;
		std::uint32_t activeOverrideCount = 0;
	};

	struct VariableDescriptor001
	{
		std::uint32_t structSize = sizeof(VariableDescriptor001);
		const char* featureName = nullptr;
		const char* variableName = nullptr;
		const char* displayName = nullptr;
		const char* tooltip = nullptr;
		ValueKind valueKind = ValueKind::kUnknown;
		std::uint32_t activeOverride = 0;
		std::uint32_t hasNumericRange = 0;
		double minimumValue = 0.0;
		double maximumValue = 0.0;
		const char* currentValueJson = nullptr;
		const char* userValueJson = nullptr;
	};

	struct OverrideSnapshot001
	{
		std::uint32_t structSize = sizeof(OverrideSnapshot001);
		const char* weatherKey = nullptr;
		const char* featureName = nullptr;
		std::uint32_t effectivePresent = 0;
		std::uint32_t persistedPresent = 0;
		const char* effectiveValueJson = nullptr;
		const char* persistedValueJson = nullptr;
	};

	struct MutationRequest001
	{
		std::uint32_t structSize = sizeof(MutationRequest001);
		MutationAction action = MutationAction::kSetWeather;
		std::uint64_t expectedStateRevision = 0;
		std::uint64_t flags = kMutationNone;
		std::uint32_t boolValue = 0;
		std::uint32_t accelerate = 0;
		const char* weatherKey = nullptr;
		const char* featureName = nullptr;
		const char* valueJson = nullptr;
		const char* preflightToken = nullptr;
	};

	struct Preflight001
	{
		std::uint32_t structSize = sizeof(Preflight001);
		Status status = Status::kInternalError;
		std::uint32_t allowed = 0;
		std::uint32_t disruptive = 0;
		std::uint32_t destructive = 0;
		std::uint32_t willPersist = 0;
		std::uint32_t willApplyLive = 0;
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
		std::uint32_t persisted = 0;
		std::uint32_t liveApplied = 0;
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
		std::uint32_t (*GetWeatherCount)(const void* context) = nullptr;
		Status (*GetWeatherDescriptor)(const void* context, std::uint32_t index, WeatherDescriptor001* output) = nullptr;
		std::uint32_t (*GetFeatureCount)(const void* context) = nullptr;
		Status (*GetFeatureDescriptor)(const void* context, std::uint32_t index, FeatureDescriptor001* output) = nullptr;
		std::uint32_t (*GetVariableCount)(const void* context, const char* featureName) = nullptr;
		Status (*GetVariableDescriptor)(const void* context, const char* featureName, std::uint32_t index, VariableDescriptor001* output) = nullptr;
		Status (*GetOverride)(const void* context, const char* weatherKey, const char* featureName, OverrideSnapshot001* output) = nullptr;
		Status (*Preflight)(const void* context, const MutationRequest001* request, Preflight001* output) = nullptr;
		Status (*Execute)(const void* context, const MutationRequest001* request, MutationReceipt001* output) = nullptr;
	};
}
