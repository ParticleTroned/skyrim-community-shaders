#pragma once

#include <cstdint>

namespace CSX::EditorAPI
{
	inline constexpr char ServiceName[] = "csx.editor";
	inline constexpr std::uint32_t ServiceMajor = 1;
	inline constexpr std::uint32_t MinimumServiceMinor = 0;
	inline constexpr std::uint32_t LegacySchemaRevision = 1;
	inline constexpr std::uint32_t ServiceMinor = 1;
	inline constexpr std::uint32_t SchemaRevision = 2;

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
		kBlocked = 9,
		kInternalError = 10
	};

	enum class PreviewMode : std::uint32_t
	{
		kNone = 0,
		kFreeCamera = 1,
		kFreeCameraLocked = 2,
		kPlayMode = 3
	};

	enum class MutationAction : std::uint32_t
	{
		kOpen = 1,
		kClose = 2,
		kToggle = 3,
		kResetLayout = 4,
		kExitPreview = 5,
		kOpenLightEditor = 6,
		kBeginLightPick = 7,
		kCancelLightPick = 8
	};

	enum MutationFlag : std::uint64_t
	{
		kMutationNone = 0,
		kMutationAllowDisruptive = 1ull << 0
	};

	enum ServiceCapability : std::uint64_t
	{
		kCapabilitySnapshot = 1ull << 0,
		kCapabilityWindowControl = 1ull << 1,
		kCapabilityLayoutReset = 1ull << 2,
		kCapabilityPreviewInspection = 1ull << 3,
		kCapabilityPreviewExit = 1ull << 4,
		kCapabilityPreflightTokens = 1ull << 5,
		kCapabilityLightPickerControl = 1ull << 6
	};

	inline constexpr std::uint64_t ServiceCapabilities =
		kCapabilitySnapshot | kCapabilityWindowControl | kCapabilityLayoutReset |
		kCapabilityPreviewInspection | kCapabilityPreviewExit | kCapabilityPreflightTokens |
		kCapabilityLightPickerControl;
	inline constexpr std::uint64_t LegacyServiceCapabilities =
		ServiceCapabilities & ~kCapabilityLightPickerControl;

	[[nodiscard]] constexpr bool SupportsMutationAction(
		std::uint64_t a_capabilities,
		MutationAction a_action) noexcept
	{
		switch (a_action) {
		case MutationAction::kOpen:
		case MutationAction::kClose:
		case MutationAction::kToggle:
		case MutationAction::kResetLayout:
		case MutationAction::kExitPreview:
			return true;
		case MutationAction::kOpenLightEditor:
		case MutationAction::kBeginLightPick:
		case MutationAction::kCancelLightPick:
			return (a_capabilities & kCapabilityLightPickerControl) != 0;
		default:
			return false;
		}
	}

	struct Snapshot001
	{
		std::uint32_t structSize = sizeof(Snapshot001);
		std::uint32_t available = 0;
		std::uint32_t dataAvailable = 0;
		std::uint32_t canOpen = 0;
		std::uint32_t resourcesInitialized = 0;
		std::uint32_t editorOpen = 0;
		std::uint32_t menuSessionOpen = 0;
		std::uint32_t mainMenuOpen = 0;
		std::uint32_t loadingMenuOpen = 0;
		std::uint32_t persistentMutationBlocked = 0;
		std::uint32_t saveLoadSafeModeActive = 0;
		std::uint32_t weatherLocked = 0;
		std::uint32_t timePaused = 0;
		std::uint32_t undoAvailable = 0;
		PreviewMode previewMode = PreviewMode::kNone;
		std::uint64_t stateRevision = 0;
		std::uint64_t capabilities = 0;
		const char* unavailableReason = nullptr;
		const char* buildId = nullptr;
	};

	struct MutationRequest001
	{
		std::uint32_t structSize = sizeof(MutationRequest001);
		MutationAction action = MutationAction::kOpen;
		std::uint64_t expectedStateRevision = 0;
		std::uint64_t flags = kMutationNone;
		const char* preflightToken = nullptr;
	};

	struct Preflight001
	{
		std::uint32_t structSize = sizeof(Preflight001);
		Status status = Status::kInternalError;
		std::uint32_t allowed = 0;
		std::uint32_t disruptive = 0;
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
		Status (*Preflight)(const void* context, const MutationRequest001* request, Preflight001* output) = nullptr;
		Status (*Execute)(const void* context, const MutationRequest001* request, MutationReceipt001* output) = nullptr;
	};
}
