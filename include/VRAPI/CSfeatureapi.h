#pragma once

#include <cstdint>

namespace CSX::FeatureAPI
{
	inline constexpr char ServiceName[] = "csx.features";
	inline constexpr std::uint32_t ServiceMajor = 1;
	inline constexpr std::uint32_t ServiceMinor = 0;
	inline constexpr std::uint32_t SchemaRevision = 1;

	enum class Status : std::uint32_t
	{
		kSuccess = 0, kInvalidArgument = 1, kStructureTooSmall = 2, kUnavailable = 3,
		kWrongThread = 4, kRevisionConflict = 5, kPreflightRequired = 6,
		kPreflightExpired = 7, kPreflightMismatch = 8, kFeatureNotFound = 9,
		kBlocked = 10, kPersistenceFailed = 11, kInternalError = 12
	};

	enum class MutationAction : std::uint32_t { kSetDisabledAtBoot = 1 };
	enum MutationFlag : std::uint64_t
	{
		kMutationNone = 0,
		kMutationPersist = 1ull << 0,
		kMutationAllowDisruptive = 1ull << 1
	};
	enum ServiceCapability : std::uint64_t
	{
		kCapabilitySnapshot = 1ull << 0,
		kCapabilityCatalog = 1ull << 1,
		kCapabilitySettingsInspection = 1ull << 2,
		kCapabilityConstraintInspection = 1ull << 3,
		kCapabilityBootConfiguration = 1ull << 4,
		kCapabilityPreflightTokens = 1ull << 5
	};
	inline constexpr std::uint64_t ServiceCapabilities = kCapabilitySnapshot | kCapabilityCatalog |
		kCapabilitySettingsInspection | kCapabilityConstraintInspection | kCapabilityBootConfiguration |
		kCapabilityPreflightTokens;

	struct Snapshot001
	{
		std::uint32_t structSize = sizeof(Snapshot001);
		std::uint32_t available = 0;
		std::uint32_t persistentMutationBlocked = 0;
		std::uint32_t saveLoadSafeModeActive = 0;
		std::uint32_t featureCount = 0;
		std::uint32_t loadedFeatureCount = 0;
		std::uint32_t activeConstraintCount = 0;
		std::uint64_t stateRevision = 0;
		std::uint64_t capabilities = 0;
		const char* buildId = nullptr;
	};

	struct FeatureDescriptor001
	{
		std::uint32_t structSize = sizeof(FeatureDescriptor001);
		const char* shortName = nullptr;
		const char* name = nullptr;
		const char* displayName = nullptr;
		const char* category = nullptr;
		const char* installedVersion = nullptr;
		const char* requiredVersion = nullptr;
		const char* shaderDefineName = nullptr;
		const char* failureMessage = nullptr;
		const char* summary = nullptr;
		const char* summaryItemsJson = nullptr;
		std::uint32_t loaded = 0;
		std::uint32_t core = 0;
		std::uint32_t inMenu = 0;
		std::uint32_t supportsVR = 0;
		std::uint32_t disabledAtBoot = 0;
		std::uint32_t runtimeDisabledByMissingDependency = 0;
		std::uint32_t hiddenFromUserView = 0;
		std::uint32_t hiddenInEssentialsMode = 0;
		std::uint32_t hasFeatureSettings = 0;
		std::uint32_t hasShaderDefine = 0;
		std::uint32_t activeConstraintCount = 0;
	};

	struct ConstraintDescriptor001
	{
		std::uint32_t structSize = sizeof(ConstraintDescriptor001);
		const char* sourceFeatureShortName = nullptr;
		const char* targetFeatureShortName = nullptr;
		const char* targetSettingPath = nullptr;
		const char* forcedValueJson = nullptr;
		const char* reason = nullptr;
		std::uint32_t recommendDisableAtBoot = 0;
	};

	struct SettingsSnapshot001
	{
		std::uint32_t structSize = sizeof(SettingsSnapshot001);
		const char* featureShortName = nullptr;
		const char* settingsJson = nullptr;
	};

	struct MutationRequest001
	{
		std::uint32_t structSize = sizeof(MutationRequest001);
		MutationAction action = MutationAction::kSetDisabledAtBoot;
		std::uint64_t expectedStateRevision = 0;
		std::uint64_t flags = kMutationNone;
		const char* featureShortName = nullptr;
		std::uint32_t disabled = 0;
		const char* preflightToken = nullptr;
	};

	struct Preflight001
	{
		std::uint32_t structSize = sizeof(Preflight001);
		Status status = Status::kInternalError;
		std::uint32_t allowed = 0;
		std::uint32_t disruptive = 0;
		std::uint32_t willPersist = 0;
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
		Status (*GetSnapshot)(const void*, Snapshot001*) = nullptr;
		std::uint32_t (*GetFeatureCount)(const void*) = nullptr;
		Status (*GetFeatureDescriptor)(const void*, std::uint32_t, FeatureDescriptor001*) = nullptr;
		Status (*GetFeatureSettings)(const void*, const char*, SettingsSnapshot001*) = nullptr;
		std::uint32_t (*GetConstraintCount)(const void*) = nullptr;
		Status (*GetConstraintDescriptor)(const void*, std::uint32_t, ConstraintDescriptor001*) = nullptr;
		Status (*Preflight)(const void*, const MutationRequest001*, Preflight001*) = nullptr;
		Status (*Execute)(const void*, const MutationRequest001*, MutationReceipt001*) = nullptr;
	};
}
