#pragma once

#include "Feature.h"
#include "Upscaling/DX12SwapChain.h"
#include "Upscaling/FidelityFX.h"
#include "Upscaling/FoveatedRegionPlan.h"
#include "Upscaling/LumaSharpen/LumaSharpen.h"
#include "Upscaling/RCAS/RCAS.h"
#include "Upscaling/Streamline.h"
#include <array>
#include <atomic>
#include <d3d11_4.h>
#include <directx/d3d12.h>
#include <filesystem>
#include <limits>
#include <mutex>
#include <openvr.h>
#include <optional>
#include <vector>
#include <winrt/base.h>

namespace RE
{
	class MapMenu;
	class RaceSexMenu;
}

/**
 * @brief Provides upscaling functionality including DLSS, FSR and TAA.
 *
 * This feature handles various upscaling methods and frame generation technologies
 * to improve performance while maintaining visual quality.
 */
struct Upscaling : Feature
{
public:
	// Feature interface
	virtual inline std::string GetName() override { return "Upscaling"; }
	virtual inline std::string GetShortName() override { return "Upscaling"; }
	virtual inline bool SupportsVR() override { return true; }
	virtual inline bool IsCore() const override { return true; }
	virtual inline std::string_view GetCategory() const override { return FeatureCategories::kDisplay; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Advanced upscaling and frame generation technologies for improved performance",
			{ "DLSS (Deep Learning Super Sampling) support",
				"FSR (FidelityFX Super Resolution) support",
				"TAA (Temporal Anti-Aliasing) support",
				"Frame generation for supported systems" }
		};
	}

	float2 jitter = { 0, 0 };

	enum class UpscaleMethod
	{
		kNONE,
		kTAA,
		kFSR,
		kDLSS
	};

	enum class ResolutionOwner : uint8_t
	{
		Native,
		VendorDynamicResolution,
		VRRenderScaleMode,
		PerfMode = VRRenderScaleMode  // Legacy alias for older source compatibility.
	};

	enum class UpscalingOutputTarget : uint8_t
	{
		Main,
		Sharpener,
		SubmitStageIntermediate
	};

	enum class VRRenderScaleStatus : uint8_t
	{
		Disabled,
		IneligibleMethod,
		NativeQuality,
		RuntimeBlocked,
		PendingRelatch,
		Active,
		RestartRequired
	};

	enum class VRRenderScaleTransitionState : uint8_t
	{
		Idle,
		Requested,
		WaitingForSafePoint,
		Preparing,
		Applying,
		Stabilizing,
		Active
	};

	enum class VRUpscalingTransitionOrigin : uint8_t
	{
		CSMenu,
		VRAPI,  // Legacy external API entrypoints remain separate from direct CS menu changes.
		RecoveryRelatch,
		PostLoadSync
	};

	enum class DLSSSharpenerMode : uint8_t
	{
		Off,
		RCAS,
		LumaUnsharp
	};

	// Shared DLSS/FSR/FSR4.1 render-scale presets:
	// 0=Native AA/DLAA, 1=Hoshipa, 2=Ultra Quality, 3=Quality,
	// 4=Balanced, 5=Performance, 6=Ultra Performance
	static constexpr uint32_t kQualityModeMaxIndex = 6;
	static constexpr uint32_t kDLSSPresetJ = 0;
	static constexpr uint32_t kDLSSPresetK = 1;
	static constexpr uint32_t kDLSSPresetL = 2;
	static constexpr uint32_t kDLSSPresetM = 3;
	static constexpr uint32_t kDLSSPresetF = 4;
	static constexpr uint32_t kDLSSPresetE = 5;
	static constexpr uint32_t kDLSSPresetMaxIndex = kDLSSPresetE;
	static constexpr float kVRFpsStabilizerDefaultFadeDuration = 6.0f;
	static constexpr uint32_t kDLSSSharpenerModeMaxIndex = 2;
	// Explicit profile changes remain blocked while RaceSex owns presentation or its handoff tail.
	static constexpr uint32_t kVRUpscalingApplyBlockRaceSexMenu = 1u << 0;
	static constexpr uint32_t kVRUpscalingApplyBlockRaceSexStartupTail = 1u << 1;
	static constexpr uint32_t kVRUpscalingApplyBlockLoadingMenu = 1u << 2;
	static constexpr uint32_t kVRUpscalingApplyBlockRelatchPending = 1u << 3;
	static constexpr uint32_t kVRUpscalingApplyBlockTransitionPending = 1u << 4;
	static constexpr uint32_t ClampDLSSPresetUInt(uint32_t a_preset)
	{
		return a_preset <= kDLSSPresetMaxIndex ? a_preset : kDLSSPresetMaxIndex;
	}

	static constexpr float GetQualityModeResolutionScale(uint32_t a_qualityMode)
	{
		switch (a_qualityMode) {
		case 1:
			return 0.85f;
		case 2:
			return 1.0f / 1.3f;
		case 3:
			return 1.0f / 1.5f;
		case 4:
			return 1.0f / 1.7f;
		case 5:
			return 0.5f;
		case 6:
			return 1.0f / 3.0f;
		default:
			return 1.0f;
		}
	}

	struct Settings
	{
		uint upscaleMethod = (uint)UpscaleMethod::kDLSS;
		uint upscaleMethodNoDLSS = (uint)UpscaleMethod::kFSR;
		uint qualityMode = 3;            // Shared upscaler preset; defaults to Quality
		uint dlssPreset = kDLSSPresetK;  // Settings ids: J, K, L, M, F, E (default K)
		uint renderScaleMode = 1;
		bool vrFpsStabilizerSync = false;
		uint perfMode = 1;
		uint frameLimitMode = 1;
		uint frameGenerationMode = 0;  // Disabled by default
		uint frameGenerationForceEnable = 0;
		bool frameGenerationAllowInMenus = false;
		uint streamlineLogLevel = 0;  // 0=Off, 1=Default, 2=Verbose
		float sharpnessFSR = 0.9f;
		float sharpnessDLSS = 0.9f;
		uint dlssSharpener = static_cast<uint>(DLSSSharpenerMode::RCAS);
		bool fsr4RuntimeEnable = true;
		bool foveatedVendorDispatch = false;
		float foveatedCenterArea = 0.6f;
		float foveatedCenterHorizontalScale = 1.0f;
		float foveatedLeftEyeMaskOffsetX = 0.0f;
		float foveatedLeftEyeMaskOffsetY = 0.0f;
		float foveatedRightEyeMaskOffsetX = 0.0f;
		float foveatedRightEyeMaskOffsetY = 0.0f;
		float periphery_taa_center_area = 0.6f;
		bool foveatedPeripheryMaskVisualization = false;
		bool periphery_taa_enable = false;
		float periphery_taa_outer_scale = 0.70f;
		float periphery_taa_center_blend_feather = 0.05f;
		bool reflexLowLatencyMode = true;
		bool reflexLowLatencyBoost = false;
		bool reflexUseMarkersToOptimize = true;
		bool reflexUseFPSLimit = false;
		float reflexFPSLimit = 60.0f;
	};

	Settings settings;

	/** @brief One unconditional Interior or Exterior Community Shaders profile from VRFpsStabilizer.ini. */
	struct VRFpsStabilizerProfile
	{
		bool hasUpscaleMethod = false;
		UpscaleMethod upscaleMethod = UpscaleMethod::kNONE;
		bool hasLegacyMethodSelection = false;
		bool hasQualityMode = false;
		uint32_t qualityMode = 0;
		bool hasDLSSPreset = false;
		uint32_t dlssPreset = kDLSSPresetK;
		bool hasRenderScaleMode = false;
		bool renderScaleMode = false;
		uint32_t invalidSettingCount = 0;

		/** @return True when the INI supplied any recognized setting for this profile. */
		[[nodiscard]] bool HasAnySetting() const
		{
			return hasUpscaleMethod || hasLegacyMethodSelection || hasQualityMode || hasDLSSPreset || hasRenderScaleMode;
		}

		/** @return True when the profile contains every canonical editable setting. */
		[[nodiscard]] bool HasCompleteSettings() const
		{
			return (hasUpscaleMethod || hasLegacyMethodSelection) && hasQualityMode && hasDLSSPreset && hasRenderScaleMode;
		}

		/** @brief Marks the resolved profile as canonical after a successful save. */
		void MarkSettingsComplete()
		{
			hasUpscaleMethod = true;
			hasLegacyMethodSelection = false;
			hasQualityMode = true;
			hasDLSSPreset = true;
			hasRenderScaleMode = true;
			invalidSettingCount = 0;
		}
	};

	/** @brief Editable VR FPS Stabilizer settings owned by the VR Stabilizer menu tab. */
	struct VRFpsStabilizerConfig
	{
		std::filesystem::path path;
		bool fileExists = false;
		bool fileReadable = false;
		bool upscalingSwitchingEnabled = true;
		bool hasMixedUpscalingSwitchingActivation = false;
		bool hasFadeDuration = false;
		float fadeDuration = kVRFpsStabilizerDefaultFadeDuration;
		uint32_t invalidFadeSettingCount = 0;
		VRFpsStabilizerProfile interior;
		VRFpsStabilizerProfile exterior;

		/** @return True when either Interior or Exterior supplied a recognized setting. */
		[[nodiscard]] bool HasAnyProfile() const
		{
			return interior.HasAnySetting() || exterior.HasAnySetting();
		}

		/** @return True when both Interior and Exterior contain every editable setting. */
		[[nodiscard]] bool HasCompleteProfiles() const
		{
			return interior.HasCompleteSettings() && exterior.HasCompleteSettings();
		}

		/** @return True when the fade and both profiles contain every editable setting. */
		[[nodiscard]] bool HasCompleteSettings() const
		{
			return hasFadeDuration && HasCompleteProfiles();
		}

		/** @return Number of recognized values or combinations that need normalization. */
		[[nodiscard]] uint32_t GetInvalidSettingCount() const
		{
			return invalidFadeSettingCount + interior.invalidSettingCount + exterior.invalidSettingCount;
		}

		/** @return True when the INI supplied any valid or invalid managed upscaling setting. */
		[[nodiscard]] bool HasAnyManagedSetting() const
		{
			return hasFadeDuration || HasAnyProfile() || GetInvalidSettingCount() > 0;
		}

		/** @brief Marks all resolved settings as canonical after a successful save. */
		void MarkSettingsComplete()
		{
			hasMixedUpscalingSwitchingActivation = false;
			hasFadeDuration = true;
			invalidFadeSettingCount = 0;
			interior.MarkSettingsComplete();
			exterior.MarkSettingsComplete();
		}
	};

	struct RuntimeResolutionPlan
	{
		UpscaleMethod upscaleMethod = UpscaleMethod::kNONE;
		ResolutionOwner owner = ResolutionOwner::Native;
		UpscalingOutputTarget outputTarget = UpscalingOutputTarget::Main;
		uint32_t qualityMode = 0;
		float2 trueHMDDisplaySize{ 0.0f, 0.0f };
		float2 engineRenderSize{ 0.0f, 0.0f };
		float2 finalOutputSize{ 0.0f, 0.0f };
		bool vendorMethod = false;
		bool foveatedActive = false;
		bool peripheryTAAActive = false;
		bool menuContextActive = false;
		bool knownMenuContextActive = false;
		bool loadingMenuActive = false;
		bool perfModeRestartRequired = false;
		FoveatedRegionPlan foveatedRegion{};
	};

	/** @brief Complete, immutable target captured for one deferred VR render-scale request. */
	struct VRRenderScaleDesiredProfile
	{
		bool pending = false;
		uint64_t requestID = 0;
		uint64_t transitionEpoch = 0;
		UpscaleMethod method = UpscaleMethod::kNONE;
		uint32_t qualityMode = 0;
		bool renderScaleModeEnabled = false;
		uint32_t dlssPreset = kDLSSPresetK;
		bool perfModeEnabled = false;
		bool fsr4RuntimeEnabled = false;
		uint32_t dlssSharpener = static_cast<uint32_t>(DLSSSharpenerMode::RCAS);
		float dlssSharpness = 0.0f;
		float fsrSharpness = 0.0f;
		uint32_t queuedFrame = 0;
		VRUpscalingTransitionOrigin origin = VRUpscalingTransitionOrigin::CSMenu;

		bool HasPendingSettings() const
		{
			return pending;
		}
	};

	struct VRRenderScaleRelatchSignature
	{
		bool valid = false;
		UpscaleMethod method = UpscaleMethod::kNONE;
		bool renderScaleActive = false;
		uint32_t qualityMode = 0;
		uint32_t dlssPreset = kDLSSPresetK;
		uint32_t renderEyeWidth = 0;
		uint32_t renderEyeHeight = 0;
		uint32_t displayEyeWidth = 0;
		uint32_t displayEyeHeight = 0;
	};

	enum class VRRenderScaleBackendKind : uint8_t
	{
		None,
		DLSS,
		FSRHost,
		FSRRuntime,
		FSR4Runtime
	};

	enum class VRRenderScaleResourceChange : uint32_t
	{
		None = 0,
		RenderTargets = 1u << 0,
		VendorRuntime = 1u << 1,
		Presentation = 1u << 2,
		Options = 1u << 3
	};

	/** @brief Backend-neutral identity for resources owned by one physical render-scale contract. */
	struct VRRenderScaleResourceKey
	{
		bool valid = false;
		bool active = false;
		UpscaleMethod method = UpscaleMethod::kNONE;
		VRRenderScaleBackendKind backend = VRRenderScaleBackendKind::None;
		uint32_t qualityMode = 0;
		uint32_t dlssPreset = kDLSSPresetK;
		uint32_t displayEyeWidth = 0;
		uint32_t displayEyeHeight = 0;
		uint32_t renderEyeWidth = 0;
		uint32_t renderEyeHeight = 0;
		uint32_t contextCount = 0;
		bool foveatedVendorDispatch = false;
		bool peripheryTAA = false;
	};

	struct VRRenderScaleResourceCompatibility
	{
		uint32_t changeMask = static_cast<uint32_t>(VRRenderScaleResourceChange::None);
		bool exact = false;
		bool canReuseRenderTargets = false;
		bool canReuseVendorRuntime = false;
		bool canReusePresentation = false;
	};

	enum class VRRenderScaleRelatchAction : uint32_t
	{
		None = 0,
		RecreateRenderTargets = 1u << 0,
		ResetDLSS = 1u << 1,
		ResetFSR = 1u << 2,
		RecreateFSR = 1u << 3,
		RefreshPresentation = 1u << 4,
		UpdateOptions = 1u << 5,
		RetireTransientResources = 1u << 6
	};

	enum class VRRenderScaleMemoryPressure : uint8_t
	{
		Unknown,
		Normal,
		Elevated,
		High,
		Critical
	};

	enum class VRRenderScaleMemoryTrimReason : uint8_t
	{
		None,
		RapidRelatch,
		Pressure,
		PostLoad,
		NativeRestore
	};

	/** @brief Immutable physical work plan admitted for one transition epoch. */
	struct VRRenderScaleRelatchPlan
	{
		bool valid = false;
		uint64_t transitionEpoch = 0;
		uint32_t contractGeneration = 0;
		VRUpscalingTransitionOrigin origin = VRUpscalingTransitionOrigin::CSMenu;
		VRRenderScaleResourceKey current{};
		VRRenderScaleResourceKey target{};
		VRRenderScaleResourceCompatibility compatibility{};
		uint32_t actionMask = static_cast<uint32_t>(VRRenderScaleRelatchAction::None);
		bool reuseRenderTargets = false;
		bool reuseStableRenderTargets = false;
		bool renderTargetDimensionsMatch = false;
		bool stableContractEvidenceMatches = false;
		bool stateScreenDimensionsMatch = false;
		uint64_t renderTargetMissingMask = 0;
		uint64_t renderTargetRequiredMissingMask = 0;
		uint64_t renderTargetDimensionMismatchMask = 0;
		uint64_t stableContractEvidenceBlockers = 0;
		uint32_t stateScreenWidth = 0;
		uint32_t stateScreenHeight = 0;
		bool vendorDimensionsUnchanged = false;
		bool reuseSharedSubmitResources = false;
		bool preserveDLSSResources = false;
		bool preserveFSRResources = false;
		bool reuseCompatibleFSRResources = false;
		bool preserveCompatibleFSRIntermediates = false;
		bool retainWarmDLSSResources = false;
		bool retainWarmFSRResources = false;
		bool reuseWarmTargetRuntime = false;
		bool destroyDLSSResources = false;
		bool destroyFSRResources = false;
		bool recreateFSRResources = false;
		bool waitForFSRDrain = false;
		bool lowPeakNativeRestore = false;
		VRRenderScaleMemoryPressure memoryPressure = VRRenderScaleMemoryPressure::Unknown;
		uint64_t estimatedCurrentBytes = 0;
		uint64_t estimatedTargetBytes = 0;
		uint64_t estimatedAdditionalBytes = 0;
		uint64_t projectedAdditionalBytes = 0;
		uint64_t projectedUsageBytes = 0;
		uint64_t admissionUsageLimitBytes = 0;
		uint64_t postTrimAdmissionUsageLimitBytes = 0;
		uint64_t projectedSystemCommitAdditionalBytes = 0;
		uint64_t projectedSystemCommitBytes = 0;
		uint64_t systemCommitAdmissionLimitBytes = 0;
		bool pressureCleanupRequired = false;
		bool projectedResidencyGuardActive = false;
		bool projectedResidencyPostTrimRelaxed = false;
		bool projectedResidencyDeferred = false;
		bool systemCommitGuardActive = false;
		bool systemCommitDeferred = false;
		bool pressureDeferred = false;
	};

	struct VRRenderScaleRetirementSnapshot
	{
		uint32_t pendingSets = 0;
		uint64_t oldestEpoch = 0;
		uint64_t newestEpoch = 0;
		uint32_t nextCleanupFrame = 0;
		bool fencePending = false;
		bool capacityBlocked = false;
	};

	struct VRRenderScaleMemorySnapshot
	{
		bool valid = false;
		uint32_t sampleFrame = 0;
		uint64_t transitionEpoch = 0;
		uint64_t budgetBytes = 0;
		uint64_t currentUsageBytes = 0;
		uint64_t currentReservationBytes = 0;
		uint64_t availableForReservationBytes = 0;
		uint64_t headroomBytes = 0;
		double usageRatio = 0.0;
		bool systemCommitValid = false;
		uint64_t systemCommitBytes = 0;
		uint64_t systemCommitLimitBytes = 0;
		uint64_t systemCommitHeadroomBytes = 0;
		double systemCommitRatio = 0.0;
		bool processPrivateUsageValid = false;
		uint64_t processPrivateUsageBytes = 0;
		VRRenderScaleMemoryPressure observedPressure = VRRenderScaleMemoryPressure::Unknown;
		VRRenderScaleMemoryPressure pressure = VRRenderScaleMemoryPressure::Unknown;
		uint32_t pressureSinceFrame = 0;
		uint32_t recoverySamples = 0;
	};

	struct VRRenderScaleMemoryTrimSnapshot
	{
		bool pending = false;
		VRRenderScaleMemoryTrimReason reason = VRRenderScaleMemoryTrimReason::None;
		uint64_t ownerEpoch = 0;
		uint32_t requestedFrame = 0;
		uint32_t completedFrame = 0;
		uint32_t fenceFailures = 0;
		uint32_t completedCount = 0;
		uint32_t failures = 0;
		bool lastSucceeded = false;
		uint32_t preRecreateDrainCount = 0;
		uint32_t preRecreateDrainFailures = 0;
		uint32_t lastOfferedResourceCount = 0;
		bool lastOfferUsedDecommit = false;
	};

	struct VRRenderScalePostLoadRecoverySnapshot
	{
		bool active = false;
		uint64_t recoveryEpoch = 0;
		uint64_t transitionEpoch = 0;
		uint32_t startFrame = 0;
		uint32_t lastSampleFrame = 0;
		uint32_t lastSettledFrame = 0;
		uint32_t settledSamples = 0;
		uint64_t baselineUsageBytes = 0;
		uint64_t peakUsageBytes = 0;
		uint64_t baselineSystemCommitBytes = 0;
		uint64_t peakSystemCommitBytes = 0;
		uint64_t baselineProcessPrivateUsageBytes = 0;
		uint64_t peakProcessPrivateUsageBytes = 0;
		VRRenderScaleMemoryPressure peakPressure = VRRenderScaleMemoryPressure::Unknown;
		bool cleanupArmed = false;
		bool cleanupDrained = false;
		bool trimArmed = false;
		bool trimCompleted = false;
		bool trimSucceeded = false;
		bool relatchAdmitted = false;
	};

	enum class VRVendorRuntimeLifecyclePhase : uint8_t
	{
		Inactive,
		Dirty,
		WaitingForDrain,
		Destroying,
		Creating,
		Ready,
		Failed
	};

	enum class VRVendorResourceResetResult : uint8_t
	{
		Ready,
		Pending,
		Failed
	};

	struct VRVendorRuntimeLifecycleSnapshot
	{
		UpscaleMethod method = UpscaleMethod::kNONE;
		VRRenderScaleBackendKind backend = VRRenderScaleBackendKind::None;
		VRVendorRuntimeLifecyclePhase phase = VRVendorRuntimeLifecyclePhase::Inactive;
		uint64_t transitionEpoch = 0;
		uint32_t requestedGeneration = 0;
		uint32_t runtimeGeneration = 0;
		uint32_t stateFrame = 0;
		uint32_t attempts = 0;
		uint32_t deferrals = 0;
		uint32_t failures = 0;
		bool resourcesPresent = false;
		bool readyForContract = false;
	};

	enum class VRRenderScaleRetryKind : uint8_t
	{
		Other,
		Pressure,
		Retirement,
		Backend
	};

	enum class VRRenderScaleFailureKind : uint8_t
	{
		None,
		Unknown,
		Backend,
		OutOfMemory,
		DeviceLost
	};

	struct VRRenderScaleTransitionMetrics
	{
		bool valid = false;
		bool completed = false;
		bool superseded = false;
		uint64_t transitionEpoch = 0;
		uint64_t requestID = 0;
		uint32_t contractGeneration = 0;
		VRUpscalingTransitionOrigin origin = VRUpscalingTransitionOrigin::CSMenu;
		UpscaleMethod method = UpscaleMethod::kNONE;
		VRRenderScaleResourceKey resources{};
		uint32_t requestedFrame = 0;
		uint32_t preparingFrame = 0;
		uint32_t applyingFrame = 0;
		uint32_t appliedFrame = 0;
		uint32_t stableFrame = 0;
		uint32_t totalFrames = 0;
		uint32_t retries = 0;
		uint32_t pressureDeferrals = 0;
		uint32_t retirementDeferrals = 0;
		uint32_t backendDeferrals = 0;
		uint32_t failures = 0;
		uint32_t outOfMemoryFailures = 0;
		uint32_t deviceLostFailures = 0;
		uint32_t fidelityMismatches = 0;
		VRRenderScaleFailureKind lastFailure = VRRenderScaleFailureKind::None;
		VRRenderScaleMemoryPressure peakPressure = VRRenderScaleMemoryPressure::Unknown;
		uint64_t peakUsageBytes = 0;
		uint64_t peakSystemCommitBytes = 0;
		uint64_t peakProcessPrivateUsageBytes = 0;
		uint32_t peakRetiredSets = 0;
		uint32_t memoryTrimCount = 0;
		uint32_t memoryTrimFailures = 0;
		uint32_t memoryPreRecreateDrainCount = 0;
		uint32_t memoryPreRecreateDrainFailures = 0;
	};

	struct VRRenderScaleMetricsSnapshot
	{
		VRRenderScaleTransitionMetrics current{};
		std::array<VRRenderScaleTransitionMetrics, 16> recent{};
		uint32_t nextIndex = 0;
		uint32_t count = 0;
	};

	enum class VRRenderScaleFidelityMismatch : uint32_t
	{
		None = 0,
		Evaluation = 1u << 0,
		Epoch = 1u << 1,
		Method = 1u << 2,
		Generation = 1u << 3,
		InputDimensions = 1u << 4,
		OutputDimensions = 1u << 5,
		EyeAsymmetry = 1u << 6
	};

	struct VRRenderScaleFidelityEyeSnapshot
	{
		uint32_t frame = 0;
		uint32_t generation = 0;
		uint32_t inputWidth = 0;
		uint32_t inputHeight = 0;
		uint32_t outputWidth = 0;
		uint32_t outputHeight = 0;
		bool evaluated = false;
		bool valid = false;
	};

	struct VRRenderScaleFidelitySnapshot
	{
		bool active = false;
		uint64_t transitionEpoch = 0;
		uint32_t contractGeneration = 0;
		UpscaleMethod method = UpscaleMethod::kNONE;
		VRRenderScaleBackendKind backend = VRRenderScaleBackendKind::None;
		uint32_t expectedInputWidth = 0;
		uint32_t expectedInputHeight = 0;
		uint32_t expectedOutputWidth = 0;
		uint32_t expectedOutputHeight = 0;
		uint32_t observationEyeMask = 0;
		uint32_t evaluationEyeMask = 0;
		uint32_t invariantEyeMask = 0;
		uint32_t lastMismatchMask = static_cast<uint32_t>(VRRenderScaleFidelityMismatch::None);
		uint32_t mismatchCount = 0;
		uint32_t consecutiveValidFrames = 0;
		uint32_t lastBothEyesFrame = 0;
		bool bothEyesValid = false;
		std::array<VRRenderScaleFidelityEyeSnapshot, 2> eyes{};
	};

	enum class VRRenderScalePresentationPath : uint8_t
	{
		Unknown,
		VendorEvaluated,
		PresentationStretch,
		VendorFailureStretch,
		BoundsMismatchOriginalFallback
	};

	struct VRRenderScalePresentationObservation
	{
		bool valid = false;
		VRRenderScalePresentationPath path = VRRenderScalePresentationPath::Unknown;
		uint32_t eyeIndex = 0;
		uint32_t frame = 0;
		uint32_t contractGeneration = 0;
		UpscaleMethod method = UpscaleMethod::kNONE;
		uint32_t inputWidth = 0;
		uint32_t inputHeight = 0;
		uint32_t expectedInputWidth = 0;
		uint32_t expectedInputHeight = 0;
		uint32_t outputWidth = 0;
		uint32_t outputHeight = 0;
		bool loadingOrMenuContext = false;
		bool transitionCooldown = false;
	};

	struct VRRenderScalePresentationEyeSnapshot
	{
		bool valid = false;
		VRRenderScalePresentationPath path = VRRenderScalePresentationPath::Unknown;
		uint32_t frame = 0;
		uint64_t transitionEpoch = 0;
		uint32_t contractGeneration = 0;
		UpscaleMethod method = UpscaleMethod::kNONE;
		uint32_t inputWidth = 0;
		uint32_t inputHeight = 0;
		uint32_t expectedInputWidth = 0;
		uint32_t expectedInputHeight = 0;
		uint32_t outputWidth = 0;
		uint32_t outputHeight = 0;
		uint32_t consecutiveFrames = 0;
		bool loadingOrMenuContext = false;
		bool transitionCooldown = false;
	};

	struct VRRenderScalePresentationSnapshot
	{
		std::array<VRRenderScalePresentationEyeSnapshot, 2> eyes{};
		uint32_t lastBothEyesVendorFrame = 0;
		uint32_t consecutiveBothEyesVendorFrames = 0;
		uint32_t lastFallbackFrame = 0;
		uint32_t maximumConsecutivePresentationStretchFrames = 0;
		uint64_t vendorEvaluatedEyeObservations = 0;
		uint64_t presentationStretchEyeObservations = 0;
		uint64_t vendorFailureStretchEyeObservations = 0;
		uint64_t boundsMismatchOriginalFallbackEyeObservations = 0;
	};

	enum class VRRenderScaleStressEventType : uint8_t
	{
		SessionStarted,
		Request,
		Retry,
		Applied,
		Stable,
		Failure,
		SessionStopped
	};

	struct VRRenderScaleStressEvent
	{
		uint64_t sequence = 0;
		uint64_t sessionID = 0;
		uint32_t frame = 0;
		VRRenderScaleStressEventType type = VRRenderScaleStressEventType::Request;
		uint64_t requestID = 0;
		uint64_t transitionEpoch = 0;
		VRUpscalingTransitionOrigin origin = VRUpscalingTransitionOrigin::CSMenu;
		UpscaleMethod method = UpscaleMethod::kNONE;
		bool active = false;
		uint32_t qualityMode = 0;
		uint32_t dlssPreset = kDLSSPresetK;
		VRRenderScaleTransitionState state = VRRenderScaleTransitionState::Idle;
		VRRenderScaleMemoryPressure pressure = VRRenderScaleMemoryPressure::Unknown;
		VRRenderScaleRetryKind retryKind = VRRenderScaleRetryKind::Other;
		VRRenderScaleFailureKind failureKind = VRRenderScaleFailureKind::None;
		uint64_t usageBytes = 0;
		uint64_t systemCommitBytes = 0;
		uint64_t processPrivateUsageBytes = 0;
		uint32_t retries = 0;
		uint32_t failures = 0;
		uint32_t fidelityMismatches = 0;
	};

	struct VRRenderScaleStressSessionSnapshot
	{
		bool active = false;
		uint64_t sessionID = 0;
		uint32_t startFrame = 0;
		uint32_t endFrame = 0;
		uint64_t nextSequence = 1;
		uint32_t nextIndex = 0;
		uint32_t count = 0;
		uint32_t overwrittenEvents = 0;
		uint64_t baselineVendorEvaluatedEyeObservations = 0;
		uint64_t baselinePresentationStretchEyeObservations = 0;
		uint64_t baselineVendorFailureStretchEyeObservations = 0;
		uint64_t baselineBoundsMismatchOriginalFallbackEyeObservations = 0;
		std::array<VRRenderScaleStressEvent, 128> events{};
	};

	/** @brief Immutable controller-visible profile at one transition milestone. */
	struct VRRenderScaleProfileSnapshot
	{
		bool valid = false;
		bool active = false;
		uint64_t requestID = 0;
		uint64_t transitionEpoch = 0;
		uint32_t contractGeneration = 0;
		UpscaleMethod method = UpscaleMethod::kNONE;
		uint32_t qualityMode = 0;
		uint32_t dlssPreset = kDLSSPresetK;
		uint32_t dlssSharpener = static_cast<uint32_t>(DLSSSharpenerMode::RCAS);
		float dlssSharpness = 0.0f;
		float fsrSharpness = 0.0f;
		float renderScale = 1.0f;
		bool renderScaleModeEnabled = false;
		bool perfModeEnabled = false;
		bool fsr4RuntimeEnabled = false;
		uint32_t displayEyeWidth = 0;
		uint32_t displayEyeHeight = 0;
		uint32_t renderEyeWidth = 0;
		uint32_t renderEyeHeight = 0;
		uint32_t queuedFrame = 0;
		VRUpscalingTransitionOrigin origin = VRUpscalingTransitionOrigin::CSMenu;
		VRRenderScaleResourceKey resources{};
	};

	/** @brief Coherent read model for the complete render-scale transition controller. */
	struct VRRenderScaleTransitionSnapshot
	{
		VRRenderScaleTransitionState state = VRRenderScaleTransitionState::Idle;
		uint64_t targetEpoch = 0;
		uint32_t transitionStartFrame = 0;
		uint32_t stateFrame = 0;
		uint64_t revision = 0;
		VRRenderScaleProfileSnapshot requested{};
		VRRenderScaleProfileSnapshot applying{};
		VRRenderScaleProfileSnapshot applied{};
		VRRenderScaleProfileSnapshot stable{};
		VRRenderScaleRelatchPlan relatchPlan{};
		VRRenderScaleRetirementSnapshot retirement{};
		VRRenderScaleMemorySnapshot memory{};
		VRRenderScaleMemoryTrimSnapshot memoryTrim{};
		VRRenderScalePostLoadRecoverySnapshot postLoadRecovery{};
		VRVendorRuntimeLifecycleSnapshot dlssLifecycle{};
		VRVendorRuntimeLifecycleSnapshot fsrLifecycle{};
		VRRenderScaleMetricsSnapshot metrics{};
		VRRenderScaleFidelitySnapshot fidelity{};
		VRRenderScalePresentationSnapshot presentation{};
	};

	struct PerfModeState
	{
		struct BootSnapshot
		{
			bool valid = false;
			bool active = false;
			UpscaleMethod method = UpscaleMethod::kNONE;
			uint32_t qualityMode = 0;
			uint32_t dlssPreset = kDLSSPresetK;
			float renderScale = 1.0f;
			uint32_t displayEyeWidth = 0;
			uint32_t displayEyeHeight = 0;
			uint32_t renderEyeWidth = 0;
			uint32_t renderEyeHeight = 0;
			bool renderScaleEnabled = false;
			bool perfModeEnabled = false;
			bool submitStageVendorAllowed = false;
			uint32_t generation = 0;
		};

		void ResetBootLatch();
		void RestoreBootLatch(const BootSnapshot& a_snapshot);
		void RecordTrueHMDSize(uint32_t a_eyeWidth, uint32_t a_eyeHeight);
		bool IsRequested(const Settings& a_settings) const;
		bool IsEligible(const Settings& a_settings, UpscaleMethod a_method) const;
		void UpdateRestartRequiredState(const Settings& a_settings, UpscaleMethod a_method);
		bool EnsureBootLatch(const Settings& a_settings, UpscaleMethod a_method, bool a_allowCreate, uint32_t a_generation = 0);
		bool IsActive(const Settings& a_settings, UpscaleMethod a_method) const;
		bool TryGetOpenVRRenderTargetSize(const Settings& a_settings, UpscaleMethod a_method, uint32_t& a_width, uint32_t& a_height, bool a_allowCreate, uint32_t a_generation = 0);
		void SetSubmitStageVendorAllowed(bool a_allowed);
		float2 GetDisplayScreenSize() const;
		float2 GetRenderScreenSize() const;
		const BootSnapshot& GetBootSnapshot() const { return boot; }
		bool HasKnownHMDSize() const { return trueHMDEyeWidth != 0 && trueHMDEyeHeight != 0; }
		bool HasRestartRequiredChange() const { return restartRequired; }

		uint32_t trueHMDEyeWidth = 0;
		uint32_t trueHMDEyeHeight = 0;
		BootSnapshot boot{};
		bool restartRequired = false;
		bool displaySizeChanged = false;
	};

	PerfModeState perfMode;
	RuntimeResolutionPlan runtimeResolutionPlan;
	/** @brief Returns the pending request, or a non-pending snapshot of current settings. */
	VRRenderScaleDesiredProfile GetPendingVRRenderScaleDesiredProfile() const;
	/** @brief Publishes one complete latest-wins request. Returns zero when origin priority rejects it. */
	uint64_t QueueVRRenderScaleRequest(UpscaleMethod a_method, bool a_renderScaleModeEnabled, uint32_t a_qualityMode, uint32_t a_dlssPreset, VRUpscalingTransitionOrigin a_origin = VRUpscalingTransitionOrigin::CSMenu);
	/** @brief Atomically removes and returns the complete pending request. */
	std::optional<VRRenderScaleDesiredProfile> TakePendingVRRenderScaleRequest();
	/** @brief Rejects a request that was cleared or superseded before application began. */
	bool IsLatestVRRenderScaleRequest(uint64_t a_requestID) const;
	/** @brief Returns one lock-consistent copy of requested, applying, applied, and stable state. */
	VRRenderScaleTransitionSnapshot GetVRRenderScaleTransitionSnapshot() const;
	VRRenderScaleStressSessionSnapshot GetVRRenderScaleStressSessionSnapshot() const;
	void StartVRRenderScaleStressSession();
	void StopVRRenderScaleStressSession();
	void ResetVRRenderScaleStressSession();
	json BuildVRRenderScaleIterationRecord() const;
	bool WriteVRRenderScaleIterationRecord() const;
	/** @brief Returns a stable diagnostic name for a controller state. */
	static const char* GetVRRenderScaleTransitionStateName(VRRenderScaleTransitionState a_state);
	static const char* GetVRRenderScaleMemoryPressureName(VRRenderScaleMemoryPressure a_pressure);
	static const char* GetVRRenderScaleMemoryTrimReasonName(VRRenderScaleMemoryTrimReason a_reason);
	static const char* GetVRRenderScalePresentationPathName(VRRenderScalePresentationPath a_path);
	static const char* GetVRVendorRuntimeLifecyclePhaseName(VRVendorRuntimeLifecyclePhase a_phase);
	VRRenderScaleResourceKey BuildVRRenderScaleResourceKey(const VRRenderScaleProfileSnapshot& a_profile) const;
	static VRRenderScaleResourceCompatibility CompareVRRenderScaleResourceKeys(
		const VRRenderScaleResourceKey& a_current,
		const VRRenderScaleResourceKey& a_target);
	static uint64_t EstimateVRRenderScaleResourceBytes(const VRRenderScaleResourceKey& a_key);
	uint32_t GetActiveVRRenderScaleContractGeneration() const;
	bool IsVendorRuntimeReadyForActiveContract(UpscaleMethod a_upscaleMethod) const;
	void MarkVendorRuntimeResourcesDirty(UpscaleMethod a_upscaleMethod, uint32_t a_generation = 0);
	void MarkVendorRuntimeResourcesReady(UpscaleMethod a_upscaleMethod, uint32_t a_generation = 0);
	void ClearVendorRuntimeResourcesDirty(UpscaleMethod a_upscaleMethod, bool a_clearRuntimeGeneration = false);

	struct JitterCB
	{
		float2 jitter;
		float useWideKernel;
		float pad0;
	};

	struct UpscalingDataCB
	{
		float2 dispatchDim;      // Current dispatch/output dimensions (per-eye in VR, full in flat)
		float2 trueSamplingDim;  // BufferDim.xy * ResolutionScale
		float2 invTrueSamplingDim;
		float seamCenterX;
		float seamHalfWidthPx;
		float maskDepthThreshold;
		float vrSeamHardening;
		float2 sourceOffset;  // Source offset in combined stereo inputs
		float2 outputOffset;  // Output offset in per-eye intermediates
		float2 pad;
	};

	struct DynamicResolutionStretchCB
	{
		float2 inputSize;
		float2 outputSize;
		float2 sourceTextureSize;
		float2 padding;
	};

	struct VRMenuLayerCompositeCB
	{
		float2 sourceScale;
		float2 sourceOffset;
	};

	struct FoveatedPeripheryCB
	{
		float2 outputDim;
		float2 invOutputDim;
		float2 invSourceDim;
		float2 sourceScale;
		float2 sourceOffset;
		float2 dispatchDim;
		float2 outputOffset;
		float2 jitter;
		float4 centerAndMask;  // xy=centerOffset, z=visualizeMask, w=showThreeZoneMask
		float4 tuning0;        // x=centerScale, y=centerFeather, z=centerHorizontalScale, w=taaOuterScale
	};

	struct FoveatedCenterBlendCB
	{
		float2 invOutputDim;
		float centerScale;
		float centerFeather;
		float2 centerOffset;
		float2 outputOffset;
		float2 dispatchDim;
		float2 sourceOffset;
		float2 invSourceDim;
		float centerHorizontalScale;
		float centerHorizontalScalePadding;
	};

	struct PeripheryTAACB
	{
		float2 outputDim;
		float2 invOutputDim;
		float2 inputDim;
		float2 invInputDim;
		float2 inputTextureScale;
		float2 inputTextureOffset;
		float2 dispatchDim;
		float2 outputOffset;
		float2 jitter;
		float2 centerOffset;
		float4 tuning0;  // x=centerScale, y=centerFeather, z=resetHistory, w=taaOuterScale
		float4 tuning1;  // x=historyValid, y=centerHorizontalScale, z=tileDispatch, w=tileDispatchWidth
		float4 tuning2;  // x=reactivityScale, y=instabilityScale, z=velocityScale, w=lockDecay
		float4 tuning3;  // xy=min output color-write bounds, zw=max output color-write bounds
		float4x4 currentViewProjInverse;
		float4x4 previousViewProj;
		float4 currentCameraPosAdjust;
		float4 previousCameraPosAdjust;
	};

	struct CameraMotionVectorsCB
	{
		float4x4 curViewProjUnjitteredInverse[2];  // index 1 unused in flat
		float4x4 prevViewProjUnjittered[2];
	};

	static_assert(sizeof(JitterCB) == 16, "JitterCB layout changed; update HLSL cbuffer.");
	static_assert(sizeof(UpscalingDataCB) == 64, "UpscalingDataCB layout changed; update HLSL cbuffer.");
	static_assert(sizeof(DynamicResolutionStretchCB) == 32, "DynamicResolutionStretchCB layout changed; update HLSL cbuffer.");
	static_assert(sizeof(VRMenuLayerCompositeCB) == 16, "VRMenuLayerCompositeCB layout changed; update HLSL cbuffer.");
	static_assert(sizeof(FoveatedPeripheryCB) == 96, "FoveatedPeripheryCB layout changed; update HLSL cbuffer.");
	static_assert(sizeof(FoveatedCenterBlendCB) == 64, "FoveatedCenterBlendCB layout changed; update HLSL cbuffer.");
	static_assert(sizeof(PeripheryTAACB) == 304, "PeripheryTAACB layout changed; update HLSL cbuffer.");
	static_assert(sizeof(CameraMotionVectorsCB) == 256, "CameraMotionVectorsCB layout changed; update HLSL cbuffer.");

	struct FoveatedDispatchRect
	{
		uint outputOffsetX = 0;
		uint outputOffsetY = 0;
		uint outputWidth = 0;
		uint outputHeight = 0;
		uint inputOffsetX = 0;
		uint inputOffsetY = 0;
		uint inputWidth = 0;
		uint inputHeight = 0;
	};

	struct PeripheryTAATile
	{
		uint32_t x = 0;
		uint32_t y = 0;
	};

	struct PeripheryTAATileCacheKey
	{
		uint32_t outputWidth = 0;
		uint32_t outputHeight = 0;
		uint32_t coveragePadding = 0;
		int32_t centerScaleQ = 0;
		int32_t taaOuterScaleQ = 0;
		int32_t centerHorizontalScaleQ = 0;
		int32_t centerOffsetXQ = 0;
		int32_t centerOffsetYQ = 0;
	};

	struct PeripheryTAATileCacheState
	{
		bool valid = false;
		bool uploaded = false;
		uint32_t tileCount = 0;
		PeripheryTAATileCacheKey key{};
		std::vector<PeripheryTAATile> tiles;
	};

	struct FoveatedRectCacheState
	{
		uint inputWidthPerEye = 0;
		uint inputHeight = 0;
		uint outputWidthPerEye = 0;
		uint outputHeight = 0;
		bool isVR = false;
		float centerScale = -1.0f;
		float centerFeather = -1.0f;
		float centerHorizontalScale = 1.0f;
		float peripheryTAAOuterScale = 0.0f;
		std::array<float2, 2> centerOffsets{};
		std::array<FoveatedDispatchRect, 2> rects{};
		FoveatedRegionPlan plan{};
	} foveatedRectCache;

	struct FoveatedEncodeRegion
	{
		uint32_t minX = 0;
		uint32_t minY = 0;
		uint32_t maxX = 0;
		uint32_t maxY = 0;
		bool valid = false;
	};

	ConstantBuffer* jitterCB = nullptr;
	ConstantBuffer* upscalingDataCB = nullptr;
	ConstantBuffer* cameraMotionVectorsCB = nullptr;
	ConstantBuffer* dynamicResolutionStretchCB = nullptr;
	ConstantBuffer* vrMenuLayerCompositeCB = nullptr;
	ConstantBuffer* foveatedPeripheryCB = nullptr;
	ConstantBuffer* foveatedCenterBlendCB = nullptr;
	ConstantBuffer* peripheryTAACB = nullptr;

	// Runtime state
	bool isWindowed = false;
	bool lowRefreshRate = false;
	bool d3d12SwapChainActive = false;

	// Timing and scaling
	double refreshRate = 0.0f;
	float2 resolutionScale = { 1.0f, 1.0f };
	LARGE_INTEGER qpf;

	// FG FPS Measurement for Overlay
	bool IsFrameGenerationDx12PathActive() const;
	bool ShouldUseFrameGenerationThisFrame() const;
	bool IsFrameGenerationActive() const;
	float GetFrameGenerationFrameTime() const;
	bool IsUpscalingActive() const;

	// Feature interface overrides
	virtual void DrawSettings() override;
	virtual bool HasEssentialSettings() const override { return true; }
	virtual void DrawEssentialSettings() override;
	virtual bool HasPerformanceSettings() const override { return true; }
	virtual void DrawPerformanceSettings(bool a_advanced) override;
	virtual json CapturePerformanceSettingsState() const override;
	virtual bool SupportsPerformanceCostMeasurement() const override;
	virtual bool IsPerformanceCostMeasurementEnabled() const override;
	virtual bool UsesTotalPerformanceCostMeasurement() const override { return true; }
	virtual void SetPerformanceCostMeasurementEnabled(bool a_enabled) override;
	virtual bool IsPerformanceCostMeasurementReady() const override;
	virtual const char* GetPerformanceCostMeasurementWaitText() const override;
	virtual bool RequiresMenuCloseForPerformanceCostMeasurement(bool a_targetEnabled) const override;
	virtual bool RequiresMenuCloseForPerformanceCostMeasurementRestore(const json& a_state) const override;
	virtual json CapturePerformanceCostMeasurementState() const override;
	virtual void RestorePerformanceCostMeasurementState(const json& a_state) override;
	void DrawFoveatedSetupInstructions();
	void DrawFoveatedSettings(bool a_essentialsLayout = false);
	virtual void SaveSettings(json& o_json) override;
	virtual void LoadSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual void DataLoaded() override;

	/**
	 * @brief Installs Direct3D-related hooks for device and factory creation.
	 *
	 * Loads FidelityFX support and patches the import address table (IAT) to redirect D3D11 device and DXGI factory creation functions to custom hook implementations.
	**/
	virtual void Load() override;
	virtual void PostPostLoad() override;
	virtual void SetupResources() override;
	virtual void SetupRenderTargetResources() override;

	UpscaleMethod GetUpscaleMethod() const;
	UpscaleMethod GetConfiguredUpscaleMethodForTransition() const;
	UpscaleMethod GetLegacyDLSSPreferredUpscaleMethodForAPI() const;
	UpscaleMethod GetRuntimeUpscaleMethod() const;
	uint32_t GetRuntimeQualityMode() const;
	uint32_t GetRuntimeDLSSPreset() const;
	DLSSSharpenerMode GetDLSSSharpenerMode() const;
	bool ShouldApplyDLSSSharpening() const;
	const RuntimeResolutionPlan& GetRuntimeResolutionPlan() const;
	/** @brief Resolve material mip bias from the active resolution owner or OpenComposite Unleashed. */
	float ResolveRuntimeMipBias(bool a_temporal);
	// Refresh both the cached plan and restart-required state from the current VR render-scale settings.
	void RefreshRuntimeResolutionState();
	// Read-side helper: avoid rebuilding the cached plan multiple times in one frame.
	void EnsureRuntimeResolutionStateCurrent();
	void InvalidateFrameScopedUpscalingState();
	// Rebuild only the cached plan from already-latched state; backend dispatch code must only read the cached plan.
	void RefreshRuntimeResolutionPlan();
	bool IsRenderScaleModeRequested() const;
	bool GetVRRenderScaleModeRequested() const;
	bool CanUseVRRenderScaleMode() const;
	bool IsVRRenderScaleModeLatched() const;
	bool IsVRRenderScaleModeActive() const;
	VRRenderScaleStatus GetVRRenderScaleModeStatus() const;
	static const char* GetVRRenderScaleModeStatusName(VRRenderScaleStatus a_status);
	void SetVRRenderScaleModeRequested(bool a_enabled, const char* a_reason = nullptr, bool a_allowDefer = false, VRUpscalingTransitionOrigin a_origin = VRUpscalingTransitionOrigin::CSMenu);
	bool IsPerfModeActive() const;
	bool IsPerfModePresentationActive() const;
	bool IsPresentationUpscalingActive() const;
	bool GetPerfModeRequested() const;
	void SetPerfModeRequested(bool a_enabled, const char* a_reason = nullptr, bool a_allowDefer = false, VRUpscalingTransitionOrigin a_origin = VRUpscalingTransitionOrigin::CSMenu);
	void ApplyCSMenuUpscalingTransition(UpscaleMethod a_targetMethod, bool a_renderScaleModeEnabled, uint32_t a_qualityMode, uint32_t a_dlssPreset, const char* a_reason = nullptr, VRUpscalingTransitionOrigin a_origin = VRUpscalingTransitionOrigin::CSMenu);
	void SetVRUpscalingTransitionProfile(bool a_renderScaleModeEnabled, uint32_t a_qualityMode, uint32_t a_dlssPreset, const char* a_reason = nullptr, VRUpscalingTransitionOrigin a_origin = VRUpscalingTransitionOrigin::CSMenu);
	uint32_t GetVRUpscalingApplyBlockReasonsForAPI() const;
	void RequestPerfModeRenderTargetRecreate(const char* a_reason = nullptr, VRUpscalingTransitionOrigin a_origin = VRUpscalingTransitionOrigin::CSMenu, const PerfModeState::BootSnapshot* a_recoverySnapshot = nullptr);
	bool ApplyPendingPerfModeRenderTargetRecreate(const char* a_caller = nullptr);
	void RecordTrueHMDRenderTargetSize(uint32_t a_eyeWidth, uint32_t a_eyeHeight);
	bool TryGetPerfModeOpenVRRenderTargetSize(uint32_t& a_width, uint32_t& a_height, bool a_allowCreate = false);
	bool ConsumePerfModeBootLatchCreate();
	bool AdjustVRRenderScaleRenderTargetProperties(RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties) const;
	bool UseActiveFoveatedPeripheryTAAProfile() const;
	bool IsActiveUpscalingFoveatedProfileAvailable() const;
	enum class FoveatedUpscalingMode : uint8_t
	{
		Disabled,
		CenterOnly,
		PeripheralTAA
	};
	struct ActiveUpscalingFoveatedProfile
	{
		bool available = false;
		FoveatedUpscalingMode mode = FoveatedUpscalingMode::Disabled;
		bool usesPeripheryTAAOuterMask = false;
		// Actual DLSS/FSR foveated center dispatch scale.
		float vendorCenterScale = 1.0f;
		// Shared HMD-visible/protected boundary used by VR foveation consumers.
		float sharedVisibleScale = 1.0f;
		float centerHorizontalScale = 1.0f;
		std::array<float2, 2> centerOffsets{};
	};
	static const char* GetFoveatedUpscalingModeName(FoveatedUpscalingMode a_mode);
	ActiveUpscalingFoveatedProfile GetActiveUpscalingFoveatedProfile() const;
	float GetActiveFoveatedSharedVisibleScale() const;
	float GetActiveFoveatedCenterHorizontalScale() const;
	std::array<float2, 2> GetActiveResolvedFoveatedMaskCenterOffsets() const;

	bool CheckResources(UpscaleMethod a_upscalemethod);
	bool EnsureResourcesCurrent(UpscaleMethod a_upscalemethod);
	void CreateUpscalingTextureResources(UpscaleMethod a_upscalemethod);
	void DestroyUpscalingTextureResources(UpscaleMethod a_upscalemethod);

	winrt::com_ptr<ID3D11ComputeShader> encodeTexturesCS[5];          // One for each UpscaleMethod
	winrt::com_ptr<ID3D11ComputeShader> encodeTexturesCSDepthOutput;  // FSR + VR: converts copied per-eye depth to R32_FLOAT for FidelityFX
	ID3D11ComputeShader* GetEncodeTexturesCS();

	winrt::com_ptr<ID3D11PixelShader> depthRefractionUpscalePS;
	ID3D11PixelShader* GetDepthRefractionUpscalePS();

	winrt::com_ptr<ID3D11PixelShader> underwaterMaskUpscalePS;
	winrt::com_ptr<ID3D11PixelShader> underwaterMaskUpscaleDynamicDepthNoStencilPS;
	winrt::com_ptr<ID3D11PixelShader> underwaterMaskUpscaleRawDepthNoStencilPS;
	enum class UnderwaterMaskUpscaleVariant : uint8_t
	{
		Default,
		DynamicDepthNoStencil,
		RawDepthNoStencil
	};
	ID3D11PixelShader* GetUnderwaterMaskUpscalePS(UnderwaterMaskUpscaleVariant a_variant = UnderwaterMaskUpscaleVariant::Default);

	winrt::com_ptr<ID3D11PixelShader> cameraMotionVectorsPS;
	ID3D11PixelShader* GetCameraMotionVectorsPS();

	/**
	 * @brief Writes camera-derived motion vectors into kMOTION_VECTOR from depth and the
	 *        unjittered view-proj delta. Valid only when nothing but the camera moves
	 *        (the main menu, where no geometry pass writes motion vectors).
	 *        Sets menuCameraMVsValid on success.
	 */
	void FillMenuCameraMotionVectors();
	void PrepareMenuCameraMotionVectors();

	winrt::com_ptr<ID3D11VertexShader> upscaleVS;
	ID3D11VertexShader* GetUpscaleVS();

	winrt::com_ptr<ID3D11ComputeShader> foveatedPeripheryCS;
	ID3D11ComputeShader* GetFoveatedPeripheryCS();

	winrt::com_ptr<ID3D11ComputeShader> foveatedCenterBlendCS;
	ID3D11ComputeShader* GetFoveatedCenterBlendCS();

	winrt::com_ptr<ID3D11ComputeShader> peripheryTAACS;
	ID3D11ComputeShader* GetPeripheryTAACS();

	winrt::com_ptr<ID3D11ComputeShader> submitStageStretchCS;
	ID3D11ComputeShader* GetSubmitStageStretchCS();

	winrt::com_ptr<ID3D11PixelShader> vrDesktopMirrorBlitPS;
	ID3D11PixelShader* GetVRDesktopMirrorBlitPS();
	winrt::com_ptr<ID3D11RenderTargetView> vrDesktopMirrorBlitRTV;
	ID3D11Texture2D* vrDesktopMirrorBlitTarget = nullptr;

	winrt::com_ptr<ID3D11PixelShader> vrMenuLayerCompositePS;
	ID3D11PixelShader* GetVRMenuLayerCompositePS();

	winrt::com_ptr<ID3D11DepthStencilState> upscaleDepthStencilState;
	winrt::com_ptr<ID3D11DepthStencilState> vrMenuCaptureDepthDisabledState;
	winrt::com_ptr<ID3D11BlendState> upscaleBlendState;
	winrt::com_ptr<ID3D11BlendState> vrMenuCompositeBlendState;
	winrt::com_ptr<ID3D11BlendState> vrMenuLayerCaptureBlendState;
	winrt::com_ptr<ID3D11RasterizerState> upscaleRasterizerState;

	// Shared VR HMD Mask Clearing
	winrt::com_ptr<ID3D11ComputeShader> vrClearHMDMaskCS;
	winrt::com_ptr<ID3D11Buffer> vrClearHMDMaskCB;
	// Helper to dispatch mask clearing for a single eye region
	void ClearHMDMask(ID3D11UnorderedAccessView* colorUAV, ID3D11ShaderResourceView* depthSRV,
		uint32_t depthWidth, uint32_t depthHeight, uint32_t colorWidth, uint32_t colorHeight,
		uint32_t depthOffsetX, uint32_t colorOffsetX, uint32_t depthOffsetY = 0, uint32_t colorOffsetY = 0);

	// Shared VR Per-Eye Intermediate Buffers
	// Owned here so both Streamline (DLSS) and FidelityFX (FSR) can use them.
	eastl::unique_ptr<Texture2D> vrIntermediateColorIn[2];            // per-eye render resolution
	eastl::unique_ptr<Texture2D> vrIntermediateColorOut[2];           // per-eye output resolution
	eastl::unique_ptr<Texture2D> vrIntermediateDepth[2];              // per-eye render resolution (R24G8_TYPELESS, shared depth copy)
	eastl::unique_ptr<Texture2D> vrIntermediateLinearDepth[2];        // per-eye render resolution (R32_FLOAT, FSR input)
	eastl::unique_ptr<Texture2D> vrIntermediateMotionVectors[2];      // per-eye render resolution
	eastl::unique_ptr<Texture2D> vrIntermediateReactiveMask[2];       // per-eye render resolution
	eastl::unique_ptr<Texture2D> vrIntermediateTransparencyMask[2];   // per-eye render resolution
	eastl::unique_ptr<Texture2D> submitStageDLSSSharpenerTexture[2];  // per-eye output resolution
	struct RetiredVRIntermediateTextures
	{
		uint32_t retireFrame = 0;
		uint64_t transitionEpoch = 0;
		uint32_t contractGeneration = 0;
		eastl::unique_ptr<Texture2D> colorIn[2];
		eastl::unique_ptr<Texture2D> colorOut[2];
		eastl::unique_ptr<Texture2D> depth[2];
		eastl::unique_ptr<Texture2D> linearDepth[2];
		eastl::unique_ptr<Texture2D> motionVectors[2];
		eastl::unique_ptr<Texture2D> reactiveMask[2];
		eastl::unique_ptr<Texture2D> transparencyMask[2];
		eastl::unique_ptr<Texture2D> submitStageDLSSSharpener[2];
	};
	// Retired submit-stage intermediates stay alive briefly so in-flight GPU work can drain.
	std::vector<RetiredVRIntermediateTextures> retiredVRIntermediateTextures;
	uint32_t deferredVRIntermediateTextureCleanupFrame = 0;
	winrt::com_ptr<ID3D11Query> vrIntermediateTextureCleanupFence;
	std::atomic_bool vrIntermediateRetirementCapacityLogged{ false };
	winrt::com_ptr<IDXGIAdapter3> vrRenderScaleMemoryAdapter;
	ID3D11Device* vrRenderScaleMemoryAdapterDevice = nullptr;
	uint32_t vrRenderScaleMemoryLastSampleFrame = 0;
	winrt::com_ptr<ID3D11Query> vrRenderScaleMemoryTrimFence;
	uint64_t vrRenderScaleMemoryTrimOwnerEpoch = 0;
	VRRenderScaleMemoryTrimReason vrRenderScaleMemoryTrimPendingReason = VRRenderScaleMemoryTrimReason::None;
	uint32_t vrRenderScaleMemoryTrimRequestedFrame = 0;
	uint32_t vrRenderScaleMemoryTrimFenceFailures = 0;

	struct VRIntermediateTextureCache
	{
		uint32_t inWidth = 0;
		uint32_t inHeight = 0;
		uint32_t outWidth = 0;
		uint32_t outHeight = 0;
		uint32_t generation = 0;
		eastl::unique_ptr<Texture2D> colorIn[2];
		eastl::unique_ptr<Texture2D> colorOut[2];
		eastl::unique_ptr<Texture2D> depth[2];
		eastl::unique_ptr<Texture2D> linearDepth[2];
		eastl::unique_ptr<Texture2D> motionVectors[2];
		eastl::unique_ptr<Texture2D> reactiveMask[2];
		eastl::unique_ptr<Texture2D> transparencyMask[2];
	};
	// Single alternate-size reuse cache; validated against the current layout before reuse.
	VRIntermediateTextureCache cachedVRIntermediateTextures;
	uint32_t vrIntermediateTextureGeneration = 0;

	// Helper to create/resize per-eye buffers matching source formats
	void CreateVRIntermediateTextures(uint32_t inWidth, uint32_t inHeight, uint32_t outWidth, uint32_t outHeight,
		ID3D11Resource* colorSrc, ID3D11Resource* mvecSrc, ID3D11Resource* reactiveSrc, ID3D11Resource* transparencySrc, uint32_t contractGeneration = 0);
	void EnsureVRIntermediateTextures(uint32_t inWidth, uint32_t inHeight, uint32_t outWidth, uint32_t outHeight,
		ID3D11Resource* colorSrc, ID3D11Resource* mvecSrc, ID3D11Resource* reactiveSrc, ID3D11Resource* transparencySrc, uint32_t contractGeneration = 0);
	bool EnsureVRPresentationTextures(uint32_t inWidth, uint32_t inHeight, uint32_t outWidth, uint32_t outHeight,
		ID3D11Resource* colorSrc);

	// Helper: Create a Texture2D matching source format at a given size
	static eastl::unique_ptr<Texture2D> CreateTextureFromSource(ID3D11Resource* src, uint32_t width, uint32_t height,
		bool copyBindFlags = false, bool createSRV = false, bool createUAV = false, const char* name = nullptr, bool createRTV = false);

	// Shared Pipeline Steps
	bool PreparePerEyeInputs(ID3D11Resource* colorSrc, ID3D11Resource* depthSrc, ID3D11Resource* mvecSrc,
		ID3D11Resource* reactiveSrc, ID3D11Resource* transparencySrc, bool copyAuxiliaryInputs = true, bool copyDepthInput = true);
	bool AreVRPerEyeUpscalingResourcesReady(bool requireDepth, bool requireLinearDepth) const;
	bool AreVRIntermediateTexturesCompatibleForFSR(uint32_t a_displayEyeWidth, uint32_t a_displayEyeHeight) const;
	void FinalizePerEyeOutputs(ID3D11Resource* colorDst);
	bool BlitVRRenderScaleDesktopMirror(ID3D11Texture2D* a_targetTexture, const D3D11_TEXTURE2D_DESC& a_targetDesc,
		uint32_t a_eyeWidth, uint32_t a_eyeHeight, Texture2D* const* a_eyeSources = nullptr,
		bool a_compositeCommittedMenuLayer = false);
	void PresentVRMenuDesktopMirror(IDXGISwapChain* a_swapChain);
	bool EnsureSubmitStageDLSSSharpenerTexture(uint32_t eyeIndex, const Texture2D& colorOutput);
	bool ApplySubmitStageDLSSSharpening(uint32_t eyeIndex, const Texture2D& sharpenInput);

	void ConfigureTAA();
	void ConfigureUpscaling(RE::BSGraphics::State* a_state);
	bool ApplyLockedFullResolutionDynamicResolutionState(RE::BSGraphics::State* a_state);
	bool ApplyDynamicResolutionState(RE::BSGraphics::State* a_state);
	void PrepareFullResolutionPostProcessing(RE::BSGraphics::State* a_state = nullptr, bool a_resetProjection = false);
	VRVendorResourceResetResult ResetVRSubmitStageState(bool a_destroyDLSSResources = true, bool a_destroySharedResources = true, bool a_preserveVRIntermediateTextures = false);
	void RequestVRSubmitStageHistoryReset();
	bool IsSubmitStageUpscalingActive() const;
	bool IsSubmitStageDeviceLost() const;
	void RecordVRDLSSFullEyeEvaluation(uint32_t a_eyeIndex, bool a_success);
	bool ShouldSuppressVRInSceneOverlaySubmit() const;
	bool IsVRProtectedFullSizeSubmitTexture(const vr::Texture_t* a_texture) const;
	bool ShouldSuppressVRRenderScaleOriginalSubmitFallback(const vr::Texture_t* a_texture) const;
	bool SubmitVRUpscaledFrame(vr::EVREye a_eye, const vr::Texture_t* a_inputTexture, const vr::VRTextureBounds_t* a_inputBounds,
		vr::Texture_t& a_outputTexture, vr::VRTextureBounds_t& a_outputBounds, VRRenderScalePresentationObservation& a_presentationObservation);
	void RecordVRRenderScalePresentationObservation(const VRRenderScalePresentationObservation& a_observation);
	static bool ShouldTraceVRMenuBridgeDirectDrawCandidate(UINT a_indexCount, UINT a_instanceCount,
		UINT a_startIndexLocation, INT a_baseVertexLocation, UINT a_startInstanceLocation,
		const char** a_decisionReason = nullptr);
	static bool ShouldTraceVRMenuBridgeDrawOperation(const char** a_decisionReason = nullptr);
	static bool TraceVRMenuBridgeDrawOperation(ID3D11DeviceContext* a_context, UINT a_indexCount, UINT a_instanceCount,
		UINT a_startIndexLocation, INT a_baseVertexLocation, UINT a_startInstanceLocation, uint32_t a_callerRva,
		const char** a_decisionReason = nullptr);
	static bool BeginVRMenuAccumulatorTrace(void* a_accumulator, uint32_t a_firstPass, uint32_t a_lastPass,
		uint32_t a_renderFlags, int a_groupIndex);
	static void EndVRMenuAccumulatorTrace(void* a_accumulator, uint32_t a_firstPass, uint32_t a_lastPass,
		uint32_t a_renderFlags, int a_groupIndex);
	static void TraceVRMenuPresentationOpenVRSubmit(const char* a_path, vr::EVREye a_eye,
		const vr::Texture_t* a_texture, const vr::VRTextureBounds_t* a_bounds, vr::EVRSubmitFlags a_flags,
		vr::EVRCompositorError a_result) noexcept;
	static void InstallVRMenuPresentationTraceD3DHooks(ID3D11DeviceContext* a_context);
	static void DisableVRMenuPresentationTraceDiagnostics() noexcept;
	bool IsVRMenuParallelBridgeDrawInProgress() const noexcept;
	enum class DynamicResolutionUpsampleStage : uint8_t
	{
		Render,
		Dispatch
	};
	bool TryReplaceVanillaDynamicResolutionUpsample(const char* a_passName, DynamicResolutionUpsampleStage a_stage);
	void Upscale();
	void NotifyGameLoadStarted(bool a_newGame);
	void RequestPostLoadRuntimeReset();
	bool ApplyPendingPostLoadRuntimeReset(UpscaleMethod a_upscaleMethod);

	// D3D11 textures
	Texture2D* reactiveMaskTexture = nullptr;
	Texture2D* transparencyCompositionMaskTexture = nullptr;
	Texture2D* motionVectorCopyTexture = nullptr;
	Texture2D* sharpenerTexture = nullptr;
	bool dlssUpscaleOutputInSharpenerTexture = false;
	eastl::unique_ptr<Texture2D> foveatedCenterColorIn[2];
	eastl::unique_ptr<Texture2D> foveatedCenterColorOut[2];
	eastl::unique_ptr<Texture2D> foveatedCenterDepth[2];
	eastl::unique_ptr<Texture2D> foveatedCenterMotionVectors[2];
	eastl::unique_ptr<Texture2D> foveatedCenterReactiveMask[2];
	eastl::unique_ptr<Texture2D> foveatedCenterTransparencyMask[2];
	eastl::unique_ptr<Texture2D> peripheryTAAHistoryColor[2][2];
	eastl::unique_ptr<Texture2D> peripheryTAAVelocityHistory[2][2];
	eastl::unique_ptr<Texture2D> peripheryTAALockHistory[2][2];
	eastl::unique_ptr<Buffer> peripheryTAATileBuffer[2];
	uint32_t peripheryTAATileCapacity[2] = {};
	std::array<PeripheryTAATileCacheState, 2> peripheryTAATileCache{};
	uint32_t peripheryTAAHistoryReadIndex = 0;
	bool peripheryTAAHistoryValid = false;

	virtual void ClearShaderCache() override;

	// Static instances instead of singletons
	static inline Streamline streamline;
	static inline FidelityFX fidelityFX;  ///< AMD FidelityFX runtime for FSR upscaling and frame generation
	static inline DX12SwapChain dx12SwapChain;
	static inline RCAS rcas;                ///< Standalone RCAS sharpening for DLSS
	static inline LumaSharpen lumaSharpen;  ///< Luma-only adaptive unsharp mask for DLSS

	winrt::com_ptr<ID3D11PixelShader> copyDepthToSharedBufferPS;

	float projectionPosScaleX = 0.0f;
	float projectionPosScaleY = 0.0f;

	float dynamicResolutionWidthRatio = 1.0f;
	float dynamicResolutionHeightRatio = 1.0f;

	bool previousVendorUpscalerSelected = false;
	bool depthUpscaleUseWideKernel = false;
	bool historyResetRequested = true;
	bool historyResetThisFrame = false;
	bool menuCameraMVsValid = false;
	uint32_t historyResetLatchedFrame = std::numeric_limits<uint32_t>::max();
	uint32_t menuCameraMVsPreparedFrame = std::numeric_limits<uint32_t>::max();
	uint32_t runtimeResolutionStateLastRefreshFrame = std::numeric_limits<uint32_t>::max();
	uint32_t resourceCheckLastCompletedFrame = std::numeric_limits<uint32_t>::max();
	UpscaleMethod resourceCheckLastCompletedMethod = UpscaleMethod::kNONE;
	bool resourceCheckStable = false;
	UpscaleMethod resourceCheckStableMethod = UpscaleMethod::kNONE;
	uint64_t resourceCheckStableKey = 0;
	bool historyResetTrackingInitialized = false;
	float2 previousHistoryScreenSize = { 0.0f, 0.0f };
	float2 previousHistoryResolutionScale = { 1.0f, 1.0f };
	float2 previousHistoryEngineRenderSize = { 0.0f, 0.0f };
	float2 previousHistoryFinalOutputSize = { 0.0f, 0.0f };
	ResolutionOwner previousHistoryResolutionOwner = ResolutionOwner::Native;
	uint32_t previousHistoryQualityMode = std::numeric_limits<uint32_t>::max();
	bool previousHistoryInWorld = false;
	bool previousHistoryInMapMenu = false;
	UpscaleMethod previousHistoryUpscaleMethod = UpscaleMethod::kNONE;
	bool previousHistoryFoveatedDispatch = false;
	float previousHistoryFoveatedCenterScale = 1.0f;
	float previousHistoryFoveatedCenterHorizontalScale = 1.0f;
	std::array<float2, 2> previousHistoryFoveatedCenterOffsets = {};
	bool previousHistoryPeripheryTAA = false;
	bool previousHistoryPeripheryTAAPathActive = false;
	float previousHistoryPeripheryTAAOuterScale = 0.70f;
	float previousHistoryPeripheryTAACenterBlendFeather = 0.05f;
	bool previousHistoryFSRRuntimePathActive = false;
	bool previousHistoryFSRRuntimeFsr4Active = false;
	std::atomic<bool> postLoadRuntimeResetPending{ false };
	std::atomic<uint64_t> nextVRRenderScalePostLoadRecoveryEpoch{ 1 };
	std::atomic<uint64_t> pendingPostLoadRuntimeResetEpoch{ 0 };
	std::atomic<bool> pendingDLSSHistoryReset{ false };
	mutable std::mutex pendingVRRenderScaleRequestMutex;
	std::optional<VRRenderScaleDesiredProfile> pendingVRRenderScaleRequest;
	std::atomic<uint64_t> nextVRRenderScaleRequestID{ 1 };
	std::atomic<uint64_t> latestVRRenderScaleRequestID{ 0 };
	std::atomic<uint64_t> nextVRRenderScaleTransitionEpoch{ 1 };
	mutable std::mutex vrRenderScaleTransitionControllerMutex;
	VRRenderScaleTransitionSnapshot vrRenderScaleTransitionController{};
	mutable std::mutex vrRenderScaleStressSessionMutex;
	VRRenderScaleStressSessionSnapshot vrRenderScaleStressSession{};
	std::atomic<uint64_t> nextVRRenderScaleStressSessionID{ 1 };
	std::atomic<uint32_t> pendingVRFpsStabilizerSyncFrame{ 0 };
	std::atomic<uint32_t> vrFpsStabilizerSyncResolvedFrame{ 0 };
	std::atomic<bool> delayedVRPerfModeBootLatchForDLSS{ false };
	std::atomic<bool> pendingDLSSReset{ false };
	std::atomic<bool> pendingFSRReset{ false };
	std::atomic<uint32_t> pendingDLSSResetGeneration{ 0 };
	std::atomic<uint32_t> pendingFSRResetGeneration{ 0 };
	uint32_t vrDLSSRuntimeResourceGeneration = 0;
	uint32_t vrFSRRuntimeResourceGeneration = 0;
	std::atomic<bool> pendingPerfModeRenderTargetRecreate{ false };
	std::atomic<uint32_t> pendingPerfModeRenderTargetRecreateFrame{ 0 };
	std::atomic<uint32_t> pendingPerfModeRenderTargetRecreateDelayFrames{ 0 };
	std::atomic<bool> pendingPerfModeRenderTargetRecreatePostLoadSettle{ false };
	std::atomic<uint32_t> pendingPerfModeRenderTargetRecreateOrigin{ static_cast<uint32_t>(VRUpscalingTransitionOrigin::CSMenu) };
	std::atomic<uint64_t> pendingPerfModeRenderTargetRecreateEpoch{ 0 };
	std::atomic<uint64_t> pendingPerfModeRenderTargetRecreateRecoveryEpoch{ 0 };
	std::atomic<bool> perfModeRenderTargetRecreateInProgress{ false };
	std::atomic<bool> perfModeAllowBootLatchCreate{ true };
	std::atomic<uint32_t> vrRenderScaleNextContractGeneration{ 1 };
	std::atomic<uint32_t> pendingVRRenderScaleContractGeneration{ 0 };
	PerfModeState::BootSnapshot pendingVRRenderScaleRecoverySnapshot{};
	std::atomic<bool> vrDLSSSettingsRelatched{ false };
	mutable std::atomic_bool submitStageDeviceLost{ false };
	uint32_t submitStagePreparedFrame = std::numeric_limits<uint32_t>::max();
	uint32_t submitStagePreparedGeneration = 0;
	bool submitStagePreparedFramePresentationOnly = false;
	bool submitStagePreparedFrameFoveatedRegionEncode = false;
	struct SubmitStageVendorEyeState
	{
		bool ready = false;
		bool usedFoveatedVendorPath = false;
		bool usedDLSSSharpening = false;
		bool usedMenuFinalComposite = false;
		uint64_t menuLayerGeneration = 0;
		uint32_t method = static_cast<uint32_t>(UpscaleMethod::kNONE);
		uint32_t generation = 0;
		uint32_t inputWidth = 0;
		uint32_t inputHeight = 0;
		uint32_t outputWidth = 0;
		uint32_t outputHeight = 0;
		UINT sourceSubresource = 0;
		uint32_t sourceBoxLeft = 0;
		uint32_t sourceBoxTop = 0;
		uint32_t sourceBoxRight = 0;
		uint32_t sourceBoxBottom = 0;
		uint32_t depthWidth = 0;
		uint32_t depthHeight = 0;
		uint32_t depthOffsetX = 0;
		uint32_t depthOffsetY = 0;
	};
	struct SubmitStageFoveatedCenterState
	{
		bool ready = false;
		uint32_t frame = std::numeric_limits<uint32_t>::max();
		uint32_t method = static_cast<uint32_t>(UpscaleMethod::kNONE);
		uint32_t generation = 0;
		uint32_t qualityMode = 0;
		uint32_t dlssPreset = kDLSSPresetK;
		uint32_t inputWidth = 0;
		uint32_t inputHeight = 0;
		uint32_t outputWidth = 0;
		uint32_t outputHeight = 0;
		uint32_t motionVectorWidth = 0;
		uint32_t motionVectorHeight = 0;
		uint32_t colorInputBaseOffsetX = 0;
		uint32_t depthInputBaseOffsetX = 0;
		uint32_t auxInputBaseOffsetX = 0;
		uint32_t rectInputOffsetX = 0;
		uint32_t rectInputOffsetY = 0;
		uint32_t rectOutputOffsetX = 0;
		uint32_t rectOutputOffsetY = 0;
		UINT submitSourceSubresource = 0;
		uint32_t submitSourceBoxLeft = 0;
		uint32_t submitSourceBoxTop = 0;
		uint32_t submitSourceBoxRight = 0;
		uint32_t submitSourceBoxBottom = 0;
		float pinholeOffsetX = 0.0f;
		float pinholeOffsetY = 0.0f;
		ID3D11Resource* colorIn = nullptr;
		ID3D11Resource* depthIn = nullptr;
		ID3D11Resource* motionVectorsIn = nullptr;
		ID3D11Resource* reactiveMaskIn = nullptr;
		ID3D11Resource* transparencyMaskIn = nullptr;
		ID3D11Resource* colorOut = nullptr;
	};
	uint32_t submitStageVendorOutputFrame = std::numeric_limits<uint32_t>::max();
	uint32_t submitStageVendorOutputGeneration = 0;
	ID3D11Texture2D* submitStageVendorOutputSourceTexture = nullptr;
	std::array<SubmitStageVendorEyeState, 2> submitStageVendorEyeState = {};
	std::array<SubmitStageFoveatedCenterState, 2> submitStageFoveatedCenterState = {};
	bool submitStageForceFullEyeVendorFallback = false;
	std::atomic<uint32_t> submitStageVendorResumeFrame{ 0 };
	std::atomic<uint32_t> submitStageVendorResumeStartFrame{ 0 };
	std::atomic<uint32_t> submitStageVendorResumeStableFrames{ 0 };
	std::atomic<uint32_t> submitStageVendorResumeLastStableFrame{ 0 };
	std::atomic<uint64_t> submitStageVendorResumeStableEyeMaskState{ 0 };
	std::atomic<uint32_t> submitStageFoveatedVendorRetryFrame{ 0 };
	std::atomic<uint32_t> submitStageFoveatedVendorRetryMethod{ static_cast<uint32_t>(UpscaleMethod::kNONE) };
	std::atomic<uint32_t> vrFSRRelatchDrainGeneration{ 0 };
	std::atomic_bool vrFSRRelatchDrainLogged{ false };
	std::atomic<uint32_t> vrRenderScaleRapidRelatchFrame{ 0 };
	std::atomic<uint32_t> vrRenderScaleRapidRelatchCount{ 0 };
	std::atomic<uint32_t> vrRenderScaleMemoryReliefEndFrame{ 0 };
	std::atomic<uint32_t> vrRenderScaleMemoryReliefCleanEyeMask{ 0 };
	std::atomic_bool vrRenderScaleMemoryReliefLogged{ false };
	std::atomic_bool vrLowPeakNativeRestoreCleanupActive{ false };
	VRRenderScaleRelatchSignature vrRenderScaleLastRapidRelatchSignature{};
	std::atomic<uint32_t> vrDLSSRapidRenderScaleFlipFrame{ 0 };
	std::atomic<uint32_t> vrDLSSRapidRenderScaleFlipCount{ 0 };
	std::atomic<uint32_t> vrDLSSRapidTransitionGuardEndFrame{ 0 };
	std::atomic<uint32_t> vrDLSSRapidTransitionCleanEyeMask{ 0 };
	std::atomic_bool vrDLSSRapidTransitionGuardLogged{ false };
	uint32_t submitStageMirrorFrame = std::numeric_limits<uint32_t>::max();
	std::array<bool, 2> submitStageMirrorEyeReady = {};
	ID3D11Texture2D* submitStageMirrorSourceTexture = nullptr;
	uint32_t submitStageFoveatedPeripheryTAAFrame = std::numeric_limits<uint32_t>::max();
	std::array<bool, 2> submitStageFoveatedPeripheryTAAEyeReady = {};
	std::atomic_bool vrRenderScaleResourceTrackingSyncPending{ false };
	void CopySharedD3D12Resources();
	void PostDisplay();
	void PerformUpscaling();
	void UpscaleDepth();
	void RefreshSubmitStageUnderwaterMask();
	void RequestHistoryReset();
	void RecordVRRenderScaleTransitionRequested(const VRRenderScaleDesiredProfile& a_request);
	bool RecordVRRenderScaleTransitionPreparing(const VRRenderScaleDesiredProfile& a_request);
	uint64_t AllocateVRRenderScaleTransitionEpoch();
	void BindVRRenderScaleRelatchEpoch(uint64_t a_epoch);
	bool IsVRRenderScaleTransitionEpochCurrent(uint64_t a_epoch) const;
	bool RecordVRRenderScaleRelatchPlan(const VRRenderScaleRelatchPlan& a_plan);
	void SetVRRenderScaleTransitionState(VRRenderScaleTransitionState a_state, const char* a_reason = nullptr);
	void PublishVRRenderScaleTransitionApplied(VRUpscalingTransitionOrigin a_origin, bool a_requiresStabilization, uint64_t a_epoch);
	void PublishVRRenderScaleTransitionStable();
	void ResetVRRenderScaleTransitionController(const char* a_reason = nullptr);
	void BeginVRRenderScaleInfoTransition(const char* a_reason = nullptr);
	void CompleteVRRenderScaleInfoTransition(const char* a_phase, bool a_active, UpscaleMethod a_method, const float2& a_displaySize, const float2& a_renderSize);
	void ClearVRRenderScaleInfoTransition();
	void RecordVRRenderScaleRelatch(const VRRenderScaleRelatchSignature& a_signature, bool a_previousActive, UpscaleMethod a_previousMethod, VRUpscalingTransitionOrigin a_origin, uint32_t a_frame);
	void MaybeArmVRRenderScaleMemoryRelief(const VRRenderScaleRelatchSignature& a_signature, VRUpscalingTransitionOrigin a_origin, uint32_t a_frame);
	bool IsVRRenderScaleMemoryReliefActive();
	bool HasPendingVRIntermediateTextureCleanup() const;
	bool CanAdmitVRIntermediateRetirement(uint64_t a_epoch);
	void UpdateVRIntermediateRetirementSnapshot(bool a_capacityBlocked = false);
	bool SampleVRRenderScaleMemory(bool a_force = false, const char* a_reason = nullptr);
	void PrepareVRRenderScaleCommonTargetResidencyDrain(uint64_t a_ownerEpoch, VRRenderScaleMemoryTrimReason a_reason);
	bool ArmVRRenderScaleMemoryTrim(uint64_t a_ownerEpoch, VRRenderScaleMemoryTrimReason a_reason);
	bool ServiceVRRenderScaleMemoryTrim(const char* a_reason = nullptr);
	bool HasPendingVRRenderScaleMemoryTrim() const;
	uint64_t BeginVRRenderScalePostLoadRecovery();
	void PrepareVRRenderScalePostLoadRecovery(uint64_t a_recoveryEpoch);
	bool CanAdmitVRRenderScalePostLoadRecoveryRelatch(uint64_t a_recoveryEpoch, uint64_t a_transitionEpoch);
	void CompleteVRRenderScalePostLoadRecovery(uint64_t a_recoveryEpoch, uint64_t a_transitionEpoch);
	void RecordVRVendorRuntimeLifecycle(UpscaleMethod a_upscaleMethod, VRVendorRuntimeLifecyclePhase a_phase, uint32_t a_generation = 0, const char* a_reason = nullptr);
	void RecordVRRenderScaleTransitionRetry(VRRenderScaleRetryKind a_kind);
	void RecordVRRenderScaleTransitionFailure(VRRenderScaleFailureKind a_kind);
	void ArchiveVRRenderScaleTransitionMetricsLocked(bool a_completed, bool a_superseded, uint32_t a_frame);
	void RecordVRRenderScaleStressEvent(VRRenderScaleStressEventType a_type, VRRenderScaleRetryKind a_retryKind = VRRenderScaleRetryKind::Other, VRRenderScaleFailureKind a_failureKind = VRRenderScaleFailureKind::None);
	bool HasVRRenderScaleMemoryReliefCleanupPending() const;
	void ClearVRRenderScaleMemoryRelief();
	void ApplyVRRenderScaleMemoryReliefTransitionCleanup(const char* a_reason = nullptr, bool a_preserveVRIntermediateTextures = false);
	void RecordVRRenderScaleFullEyeEvaluation(UpscaleMethod a_upscaleMethod, uint32_t a_eyeIndex, bool a_success);
	bool RecordVRRenderScaleFidelityObservation(UpscaleMethod a_upscaleMethod, uint32_t a_eyeIndex, bool a_success, uint32_t a_generation, uint32_t a_inputWidth, uint32_t a_inputHeight, uint32_t a_outputWidth, uint32_t a_outputHeight, bool a_evaluated);
	void RecordVRDLSSRenderScaleRelatch(bool a_previousActive, bool a_currentActive, UpscaleMethod a_previousMethod, UpscaleMethod a_currentMethod, VRUpscalingTransitionOrigin a_origin, uint32_t a_frame);
	bool ShouldBypassVRDLSSFoveatedForRapidTransition();
	void ClearVRDLSSRapidTransitionGuard();
	uint32_t GetEffectiveUpscalingQualityMode() const;
	uint32_t GetEffectiveDLSSQualityMode() const;
	uint32_t GetEffectiveDLSSPreset() const;
	void MarkVRUpscalingTransitionQueued(VRUpscalingTransitionOrigin a_origin = VRUpscalingTransitionOrigin::CSMenu);
	void ClearPendingVRUpscalingTransition();
	bool HasPendingVRUpscalingTransition() const;
	bool HasPendingVRRenderScaleTransition() const;
	void QueueVRFpsStabilizerLoadSync(uint32_t a_frame);
	void ApplyPendingVRFpsStabilizerLoadSync();
	bool ShouldStageVRRenderScaleTransition(bool a_renderScaleModeEnabled, uint32_t a_qualityMode) const;
	bool ShouldDeferVRUpscalingTransitionSettings() const;
	bool ShouldWaitForVRUpscalingTransitionDelay() const;
	void MarkPerfModeRenderTargetRecreateQueued(uint32_t a_delayFrames = 0);
	bool ShouldWaitForPerfModeRenderTargetRecreateDelay() const;
	void ApplyPendingVRUpscalingTransition();
	bool ShouldResetHistoryThisFrame() const;
	void UpdateHistoryResetState(UpscaleMethod a_upscaleMethod);
	void LatchHistoryResetForCurrentFrame();
	bool IsFSRRuntimePathActive(UpscaleMethod a_upscaleMethod) const;
	bool IsFSRRuntimeFsr4PathActive(UpscaleMethod a_upscaleMethod) const;
	bool IsFoveatedVendorDispatchEnabled(UpscaleMethod a_upscaleMethod) const;
	bool IsPeripheryTAAEnabled(UpscaleMethod a_upscaleMethod) const;
	bool IsPeripheryTAAPathActive(UpscaleMethod a_upscaleMethod) const;
	float2 GetDefaultFoveatedMaskCenterOffset(uint32_t eyeIndex) const;
	float2 GetResolvedFoveatedMaskCenterOffset(uint32_t eyeIndex, bool usePeripheryTAAProfile = false) const;
	std::array<float2, 2> GetResolvedFoveatedMaskCenterOffsets(bool usePeripheryTAAProfile = false) const;
	bool GetRuntimeFoveatedRegionDimensions(uint32_t& a_inputWidthPerEye, uint32_t& a_inputHeight, uint32_t& a_outputWidthPerEye, uint32_t& a_outputHeight) const;
	bool BuildFoveatedDispatchRects(uint32_t inputWidthPerEye, uint32_t inputHeight, uint32_t outputWidthPerEye, uint32_t outputHeight, bool isVR, float centerScale, float centerFeather, float centerHorizontalScale, bool usePeripheryTAAProfile = false);
	bool GetFoveatedEncodeRegions(uint32_t inputWidthPerEye, uint32_t inputHeight, uint32_t outputWidthPerEye, uint32_t outputHeight, bool usePeripheryTAAProfile, bool usePeripheryTAAPath, std::array<FoveatedEncodeRegion, 2>& outRegions);
	bool EncodeSubmitStageVRInputs(ID3D11Resource* colorSource, ID3D11Resource* motionVectors, ID3D11Resource* depthSource, uint32_t inputWidthPerEye, uint32_t inputHeight, uint32_t outputWidthPerEye, uint32_t outputHeight, bool copyDepthInput = true, bool allowFoveatedRegionEncode = false, bool* encodedFoveatedRegions = nullptr, uint32_t contractGeneration = 0);
	bool StretchSubmitStageEyeOutput(uint32_t eyeIndex, uint32_t inputWidth, uint32_t inputHeight, uint32_t outputWidth, uint32_t outputHeight);
	bool EnsureFoveatedTexture(eastl::unique_ptr<Texture2D>& texture, ID3D11Resource* source, uint32_t width, uint32_t height, bool copyBindFlags, bool createSRV, bool createUAV, bool createRTV, const char* name);
	void DestroySubmitStageDLSSSharpenerTextures();
	void DestroyCommonUpscalingTextures();
	void DestroyVRIntermediateTextures(bool a_clearRapidTransitionGuard = true);
	void UnbindUpscalingResources();
	void DestroyFoveatedResources();
	bool EnsurePeripheryTAAResources(uint32_t outputWidthPerEye, uint32_t outputHeight, ID3D11Resource* colorSource);
	bool EnsurePeripheryTAATileBuffer(uint32_t eyeIndex, uint32_t tileCapacity);
	bool BuildPeripheryTAATileList(uint32_t eyeIndex, uint32_t outputWidth, uint32_t outputHeight, float centerScale, float taaOuterScale, float centerHorizontalScale, float centerOffsetX, float centerOffsetY, uint32_t coveragePadding, uint32_t& outTileCount);
	void DestroyPeripheryTAAResources();
	bool DispatchFoveatedVendorUpscaling(UpscaleMethod a_upscaleMethod, ID3D11Resource* colorTexture, ID3D11Resource* depthTexture, ID3D11Resource* motionVectors, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask, ID3D11Resource* colorOutput = nullptr);
	bool DispatchSubmitStageFoveatedVendorEye(UpscaleMethod a_upscaleMethod, uint32_t eyeIndex, uint32_t inputWidthPerEye, uint32_t inputHeight, uint32_t outputWidthPerEye, uint32_t outputHeight, ID3D11Resource* outputResource = nullptr, ID3D11UnorderedAccessView* outputUAV = nullptr, UINT submitSourceSubresource = 0, const D3D11_BOX* submitSourceBox = nullptr);
	struct FoveatedEyeDispatchParams
	{
		uint32_t inputWidthPerEye = 0;
		uint32_t inputHeight = 0;
		uint32_t outputWidthPerEye = 0;
		uint32_t outputHeight = 0;
		float centerScale = 1.0f;
		float centerHorizontalScale = 1.0f;
		float centerBlendFeather = 0.0f;
		bool usePeripheryTAA = false;
		bool usePeripheryTAAProfile = false;
		bool visualizeMask = false;
		bool resetPeripheryTAA = false;
		uint32_t peripheryTAAHistoryReadIndex = 0;
		uint32_t peripheryTAAHistoryWriteIndex = 0;
		ID3D11ShaderResourceView* peripherySourceSRV = nullptr;
		uint32_t peripherySourceWidth = 0;
		uint32_t peripherySourceHeight = 0;
		float peripherySourceScaleX = 1.0f;
		float peripherySourceScaleY = 1.0f;
		float peripherySourceOffsetX = 0.0f;
		float peripherySourceOffsetY = 0.0f;
		ID3D11Resource* centerColorInput = nullptr;
		ID3D11Resource* centerDepthInput = nullptr;
		ID3D11Resource* centerMotionVectorsInput = nullptr;
		ID3D11Resource* centerReactiveMaskInput = nullptr;
		ID3D11Resource* centerTransparencyMaskInput = nullptr;
		ID3D11UnorderedAccessView* outputUAV = nullptr;
		uint32_t centerColorInputBaseOffsetX = 0;
		uint32_t centerDepthInputBaseOffsetX = 0;
		uint32_t centerAuxInputBaseOffsetX = 0;
		UINT submitSourceSubresource = 0;
		D3D11_BOX submitSourceBox{};
		bool submitSourceBoxValid = false;
		Streamline::DLSSViewportRole dlssViewportRole = Streamline::DLSSViewportRole::FoveatedCenter;
	};
	void ConfigureFoveatedPeripherySourceRegion(FoveatedEyeDispatchParams& params, const eastl::unique_ptr<Texture2D>& sourceTexture, uint32_t validWidth, uint32_t validHeight) const;
	bool DispatchFoveatedVendorEyeComposite(UpscaleMethod a_upscaleMethod, uint32_t eyeIndex, const FoveatedEyeDispatchParams& params);
	bool DispatchSingleFoveatedVendorEye(UpscaleMethod a_upscaleMethod, uint32_t eyeIndex, ID3D11Resource* colorIn, ID3D11Resource* depthIn, ID3D11Resource* motionVectorsIn, ID3D11Resource* reactiveMaskIn, ID3D11Resource* transparencyMaskIn, uint32_t outputWidthPerEye, uint32_t outputHeight, uint32_t inputWidthPerEye, uint32_t inputHeight, float centerScale, float centerHorizontalScale, const float2& centerOffset, float centerFeather, uint32_t colorInputBaseOffsetX = 0, uint32_t depthInputBaseOffsetX = 0, uint32_t auxInputBaseOffsetX = 0, ID3D11UnorderedAccessView* outputUAV = nullptr, Streamline::DLSSViewportRole dlssViewportRole = Streamline::DLSSViewportRole::FoveatedCenter, UINT submitSourceSubresource = 0, const D3D11_BOX* submitSourceBox = nullptr);
	void DispatchFoveatedPeripheryPass(ID3D11ShaderResourceView* sourceSRV, ID3D11UnorderedAccessView* outputUAV, uint32_t sourceWidth, uint32_t sourceHeight, uint32_t outputWidth, uint32_t outputHeight, uint32_t outputOffsetX, uint32_t outputOffsetY, uint32_t dispatchWidth, uint32_t dispatchHeight, float centerScale, float centerHorizontalScale, bool keepBindingsBound = false, float sourceScaleX = 1.0f, float sourceScaleY = 1.0f, float sourceOffsetX = 0.0f, float sourceOffsetY = 0.0f, float centerOffsetX = 0.0f, float centerOffsetY = 0.0f);
	void DispatchPeripheryTAAPass(ID3D11ShaderResourceView* currentColorSRV, ID3D11ShaderResourceView* currentDepthSRV, ID3D11ShaderResourceView* currentMotionVectorSRV,
		ID3D11ShaderResourceView* currentReactiveSRV, ID3D11ShaderResourceView* currentTransparencySRV, ID3D11ShaderResourceView* historyColorSRV,
		ID3D11ShaderResourceView* historyVelocitySRV, ID3D11ShaderResourceView* historyLockSRV, ID3D11UnorderedAccessView* outputColorUAV, ID3D11UnorderedAccessView* outputHistoryColorUAV,
		ID3D11UnorderedAccessView* outputVelocityUAV, ID3D11UnorderedAccessView* outputLockUAV, ID3D11ShaderResourceView* tileListSRV, uint32_t tileCount,
		uint32_t inputWidth, uint32_t inputHeight,
		uint32_t outputWidth, uint32_t outputHeight, uint32_t outputOffsetX, uint32_t outputOffsetY, uint32_t dispatchWidth, uint32_t dispatchHeight,
		const float4x4& currentViewProjInverse, const float4x4& previousViewProj, const float4& currentCameraPosAdjust, const float4& previousCameraPosAdjust,
		bool resetHistory, float centerScale, float centerHorizontalScale, float centerOffsetX, float centerOffsetY,
		float inputTextureScaleX = 1.0f, float inputTextureScaleY = 1.0f, float inputTextureOffsetX = 0.0f, float inputTextureOffsetY = 0.0f);
	void DispatchFoveatedBlendPass(ID3D11ShaderResourceView* centerSRV, ID3D11UnorderedAccessView* outputUAV, uint32_t outputWidthPerEye, uint32_t outputHeight, const FoveatedDispatchRect& rect, uint32_t dispatchOffsetX, uint32_t dispatchOffsetY, uint32_t dispatchWidth, uint32_t dispatchHeight, float centerScale, float centerHorizontalScale, const float2& centerOffset, float centerFeather);

	/**
	 * @brief Applies the selected DLSS sharpening pass to the main render target after upscaling.
	 *
	 * Runs in HDR space before tonemapping. Only called when DLSS sharpening is enabled.
	 */
	void ApplySharpening();

	static void TimerSleepQPC(int64_t targetQPC);

	void FrameLimiter();

	static double GetRefreshRate(HWND a_window);

	// Unified interface methods - external code should use these instead of direct access
	void LoadUpscalingSDKs();  // Loads all SDKs at once
	void SetUIBuffer();
	HANDLE GetFrameLatencyWaitableObject() const;
	float GetFrameTime() const;

	// Backend interface methods
	bool IsBackendInitialized() const;
	void CheckBackendFeatures(IDXGIAdapter* adapter);
	void UpgradeBackendInterface(void** ppInterface);
	void SetBackendD3DDevice(ID3D11Device* device);
	void PostBackendDevice();

	// Module availability methods
	bool HasFrameGenModule() const;

	/** @brief Loads and resolves the unconditional Interior/Exterior CS rows from VRFpsStabilizer.ini. */
	bool LoadVRFpsStabilizerConfig(VRFpsStabilizerConfig& a_config, std::string& a_error) const;
	/** @brief Persists the editable stabilizer profile while preserving unrelated INI content. */
	bool SaveVRFpsStabilizerConfig(const VRFpsStabilizerConfig& a_config, std::string& a_error) const;

	// Proxy interface methods
	void SetProxyD3D11Device(ID3D11Device* device);
	void SetProxyD3D11DeviceContext(ID3D11DeviceContext* context);
	void CreateProxySwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC swapChainDesc);
	void CreateProxyInterop();
	IDXGISwapChain* GetProxySwapChain();
	bool IsOpenCompositeUpscalingBlocked(bool a_forceRefresh = false) const;
	void ClearVRDirectUpscaledEyeOutput(uint32_t eyeIndex, ID3D11UnorderedAccessView* colorUAV, ID3D11ShaderResourceView* depthSRV,
		uint32_t depthWidthPerEye, uint32_t depthHeight, uint32_t colorWidthPerEye, uint32_t colorHeight, uint32_t colorOffsetX = 0);

private:
	std::atomic<uint32_t> submitStageBoundsFallbackStartFrame{ 0 };
	std::atomic<uint32_t> submitStageBoundsFallbackLastFrame{ 0 };
	std::atomic<uint32_t> submitStageBoundsFallbackRecoveryFrame{ 0 };
	std::atomic<uint32_t> submitStageBoundsFallbackMethod{ static_cast<uint32_t>(UpscaleMethod::kNONE) };
	std::atomic<uint32_t> submitStageBoundsFallbackGeneration{ 0 };
	std::atomic<uint32_t> submitStageBoundsFallbackActualWidth{ 0 };
	std::atomic<uint32_t> submitStageBoundsFallbackActualHeight{ 0 };
	std::atomic<uint32_t> submitStageBoundsFallbackExpectedWidth{ 0 };
	std::atomic<uint32_t> submitStageBoundsFallbackExpectedHeight{ 0 };

	void ArmSubmitStageVendorResumeCooldown(uint32_t a_currentFrame);
	void ClearSubmitStageVendorResumeCooldown();
	void ClearSubmitStageVendorResumeStability();
	bool TryPromoteVRRenderScaleSubmitStageContract(uint32_t a_currentFrame, uint32_t a_eyeIndex, bool a_stableCandidate, UpscaleMethod a_upscaleMethod, uint32_t a_generation, uint32_t a_inputWidth, uint32_t a_inputHeight, uint32_t a_outputWidth, uint32_t a_outputHeight);
	void RecordSubmitStageBoundsFallback(UpscaleMethod a_upscaleMethod, uint32_t a_currentFrame, uint32_t a_generation, uint32_t a_actualWidth, uint32_t a_actualHeight, uint32_t a_expectedWidth, uint32_t a_expectedHeight);
	void ClearSubmitStageBoundsFallbackWatchdog();
	void ServiceSubmitStageBoundsFallbackWatchdog(bool a_forceRecovery = false);
	void ArmSubmitStageFoveatedVendorRetryBackoff(uint32_t a_currentFrame, UpscaleMethod a_upscaleMethod);
	void ClearSubmitStageFoveatedVendorRetryBackoff();
	void ClearVRFSRRelatchDrainGuard();
	void MarkSubmitStageDeviceLost(HRESULT a_result, const char* a_context);
	bool MarkSubmitStageDeviceLostIfNeeded(const std::exception& a_exception, const char* a_context);
	bool MarkSubmitStageDeviceLostIfDeviceRemoved(const char* a_context);
	VRVendorResourceResetResult HandleVRDLSSResourceTeardownResult(Streamline::DLSSResourceTeardownResult a_result, uint32_t a_generation, const char* a_lifecycleReason, const char* a_deviceLostContext);
	void ScheduleVRIntermediateTextureCleanup();
	void ServiceVRIntermediateTextureCleanup(bool a_forceFence = false);
	VRVendorResourceResetResult ResetVRVendorRuntimeResources(bool a_destroyDLSSResources, bool a_destroyPeripheryTAAResources, bool a_destroyFSRResources = true, bool a_waitForFSRIdleTeardown = false, bool a_fsrTeardownAlreadyReady = false, bool a_destroySharedResources = true, bool a_preserveVRIntermediateTextures = false);
	void RecreateVendorRuntimeResources(UpscaleMethod a_upscaleMethod, bool a_recreateTemporalResources);
	bool AreCommonVendorTexturesReady(UpscaleMethod a_upscaleMethod) const;
	bool IsVRRenderScalePhysicalContractConverged(UpscaleMethod a_upscaleMethod, uint32_t a_qualityMode) const;
	bool ApplyPendingVendorRuntimeReset(UpscaleMethod a_upscaleMethod, const char* a_context);
	void UpdateDepthUpscaleKernelState(JitterCB& a_jitterData, bool a_enableWideKernelLogic);
	enum class HMDMaskClearPhase : uint8_t
	{
		PerEyeInput,
		PerEyeOutput,
		SubmitStageOutput,
		SubmitStageFoveatedOutput
	};
	bool ShouldClearHMDMaskInPhase(HMDMaskClearPhase a_phase) const;
	void ClearHMDMaskForEye(HMDMaskClearPhase a_phase, ID3D11UnorderedAccessView* colorUAV, ID3D11ShaderResourceView* depthSRV,
		uint32_t depthWidth, uint32_t depthHeight, uint32_t colorWidth, uint32_t colorHeight,
		uint32_t depthOffsetX, uint32_t colorOffsetX, uint32_t depthOffsetY = 0, uint32_t colorOffsetY = 0);
	struct VendorEyeDispatchParams
	{
		uint32_t eyeIndex = 0;
		uint32_t inputWidth = 0;
		uint32_t inputHeight = 0;
		uint32_t outputWidth = 0;
		uint32_t outputHeight = 0;
		float motionVectorScaleX = 1.0f;
		float motionVectorScaleY = 1.0f;
		float pinholeOffsetX = 0.0f;
		float pinholeOffsetY = 0.0f;
		ID3D11Resource* colorIn = nullptr;
		ID3D11Resource* depth = nullptr;
		ID3D11Resource* motionVectors = nullptr;
		ID3D11Resource* reactiveMask = nullptr;
		ID3D11Resource* transparencyMask = nullptr;
		ID3D11Resource* colorOut = nullptr;
		const char* label = "vendor eye dispatch";
		Streamline::DLSSViewportRole dlssViewportRole = Streamline::DLSSViewportRole::FullEye;
	};
	bool DispatchVendorEyeRegion(UpscaleMethod a_upscaleMethod, const VendorEyeDispatchParams& params);
	bool EnsureHMDMaskClearResources();
	bool EnsureFoveatedDispatchShaders(bool usePeripheryTAA, bool visualizeMask, const char* context, const char* fallbackAction);
	void BeginVRMenuFinalCompositeFrame(uint32_t a_frame);
	void ResetVRMenuFinalCompositeLayer();
	bool EnsureVRMenuFinalCompositeLayer(uint32_t a_width, uint32_t a_height, DXGI_FORMAT a_format);
	bool PrewarmVRMenuFinalCompositeResources(DXGI_FORMAT a_layerFormat = DXGI_FORMAT_UNKNOWN);
	void BeginVRMenuDrawInterface();
	void EndVRMenuDrawInterface();
	bool BeginVRMenuSemanticEpoch(void* a_accumulator, uint32_t a_firstPass, uint32_t a_lastPass,
		uint32_t a_renderFlags, int a_groupIndex, uint32_t a_renderMode);
	void EndVRMenuSemanticEpoch(void* a_accumulator, uint32_t a_firstPass, uint32_t a_lastPass,
		uint32_t a_renderFlags, int a_groupIndex);
	bool IsVRMenuTransportContractPresent() const;
	bool IsVRMenuSemanticAdapterEligible() const;
	bool IsVRMenuSemanticBridgeOperationActive() const;
	bool IsVRMapMenuPresentationActive() const;
	bool EnsureVRMapMenuUISupersampling();
	void ReleaseVRMapMenuUISupersampling();
	void RecordVRMenuSemanticCapture(bool a_suppressed);
	void PoisonVRMenuFrameTransaction(const char* a_reason);
	void InvalidateVRMenuCommittedLayer(const char* a_reason);
	void NotifyVRMenuPresentationContextChange(const char* a_reason);
	bool SealVRMenuFrameTransaction(uint32_t a_frame);
	bool EnsureVRMenuFullResolutionDepth(uint32_t a_width, uint32_t a_height);
	bool BeginVRMenuDisplayResolutionPass();
	void EndVRMenuDisplayResolutionPass();
	bool CaptureVRMapMenuLayer(uint32_t a_frame);
	void StretchVRMapMenuCopyIfNeeded();
	void ResetVRMenuDesktopEyePairState();
	bool PublishVRMenuDesktopEye(uint32_t a_eyeIndex, const Texture2D& a_outputTexture, uint32_t a_frame);
	bool IsVRMenuDesktopEyePairCompatible(const Texture2D& a_sourceTexture, uint32_t a_eyeWidth, uint32_t a_eyeHeight,
		const eastl::unique_ptr<Texture2D> (&a_eyePair)[2]) const;
	bool EnsureVRMenuDesktopEyePair(DXGI_FORMAT a_format, uint32_t a_eyeWidth, uint32_t a_eyeHeight);
	bool DrawVRMenuBridgeIntoFinalCompositeLayer(ID3D11DeviceContext* a_context, DXGI_FORMAT a_format, UINT a_indexCount,
		UINT a_instanceCount, UINT a_startIndexLocation, INT a_baseVertexLocation, UINT a_startInstanceLocation,
		uint32_t a_renderWidth, uint32_t a_renderHeight, uint32_t a_finalWidth, uint32_t a_finalHeight);
	bool TryCaptureAndSuppressVRMenuBridgeDraw(ID3D11DeviceContext* a_context, UINT a_indexCount, UINT a_instanceCount,
		UINT a_startIndexLocation, INT a_baseVertexLocation, UINT a_startInstanceLocation, uint32_t a_callerRva,
		const char** a_decisionReason = nullptr);
	bool ApplyKnownGameMenuFinalComposite(uint32_t a_eyeIndex, Texture2D& a_outputTexture, uint32_t a_eyeWidth, uint32_t a_eyeHeight, uint32_t a_frame);
	static constexpr uint32_t kVRMenuTransactionMaxEpochs = 16;
	struct VRMenuFrameTransaction
	{
		uint32_t frame = std::numeric_limits<uint32_t>::max();
		uint32_t planGeneration = 0;
		std::array<uint64_t, kVRMenuTransactionMaxEpochs> epochIds{};
		uint32_t epochCount = 0;
		uint32_t recognizedOperations = 0;
		uint32_t capturedOperations = 0;
		uint32_t suppressedOperations = 0;
		uint32_t mapDisplayEpochs = 0;
		uint32_t drawInterfaceDepth = 0;
		bool renderComplete = false;
		bool presentationStarted = false;
		bool sealed = false;
		bool poisoned = false;
		bool menuLayerRequired = false;
		bool mapLayerRequired = false;
		bool mapLayerCapture = false;
		const char* failureReason = nullptr;
	};
	uint32_t vrMenuFinalCompositeFrame = std::numeric_limits<uint32_t>::max();
	eastl::unique_ptr<Texture2D> vrMenuFinalCompositeLayer;
	eastl::unique_ptr<Texture2D> vrMenuCommittedCompositeLayer;
	winrt::com_ptr<ID3D11Texture2D> vrMenuFullResolutionDepth;
	winrt::com_ptr<ID3D11DepthStencilView> vrMenuFullResolutionDSV;
	std::array<winrt::com_ptr<ID3D11DepthStencilView>, 8> vrMenuFullResolutionDepthViews{};
	std::array<winrt::com_ptr<ID3D11DepthStencilView>, 8> vrMenuFullResolutionReadOnlyDepthViews{};
	uint32_t vrMenuFinalCompositeLayerWidth = 0;
	uint32_t vrMenuFinalCompositeLayerHeight = 0;
	DXGI_FORMAT vrMenuFinalCompositeLayerFormat = DXGI_FORMAT_UNKNOWN;
	uint32_t vrMenuFullResolutionDepthWidth = 0;
	uint32_t vrMenuFullResolutionDepthHeight = 0;
	std::uintptr_t vrMenuFullResolutionDepthSourceIdentity = 0;
	uint32_t vrMenuFullResolutionDepthClearedFrame = std::numeric_limits<uint32_t>::max();
	uint32_t vrMenuFinalCompositeLayerClearedFrame = std::numeric_limits<uint32_t>::max();
	uint32_t vrMenuFinalCompositeLayerDrawCount = 0;
	uint64_t vrMenuNextSemanticEpochId = 1;
	uint64_t vrMenuCommittedLayerGeneration = 0;
	uint32_t vrMenuCommittedLayerPlanGeneration = 0;
	uint32_t vrMenuCommittedLayerFrame = std::numeric_limits<uint32_t>::max();
	uint32_t vrMenuCommittedLayerOperationCount = 0;
	bool vrMenuCommittedLayerValid = false;
	bool vrMenuCommittedLayerOpaque = false;
	uint32_t vrMenuDrawInterfaceDepth = 0;
	VRMenuFrameTransaction vrMenuFrameTransaction{};
	uint32_t vrMenuAdapterPreflightFailureFrame = std::numeric_limits<uint32_t>::max();
	bool vrMenuParallelBridgeDrawInProgress = false;
	eastl::unique_ptr<Texture2D> vrMapMenuUISupersampleColor;
	winrt::com_ptr<ID3D11Texture2D> vrMapMenuUISupersampleDepth;
	std::array<winrt::com_ptr<ID3D11DepthStencilView>, 8> vrMapMenuUISupersampleDepthViews{};
	std::array<winrt::com_ptr<ID3D11DepthStencilView>, 8> vrMapMenuUISupersampleReadOnlyDepthViews{};
	winrt::com_ptr<ID3D11ShaderResourceView> vrMapMenuUISupersampleDepthSRV;
	winrt::com_ptr<ID3D11ShaderResourceView> vrMapMenuUISupersampleStencilSRV;
	ID3D11Texture2D* vrMapMenuSavedHUDTexture = nullptr;
	ID3D11Texture2D* vrMapMenuSavedHUDTextureCopy = nullptr;
	ID3D11RenderTargetView* vrMapMenuSavedHUDRTV = nullptr;
	ID3D11ShaderResourceView* vrMapMenuSavedHUDSRV = nullptr;
	ID3D11ShaderResourceView* vrMapMenuSavedHUDSRVCopy = nullptr;
	ID3D11UnorderedAccessView* vrMapMenuSavedHUDUAV = nullptr;
	ID3D11Texture2D* vrMapMenuSavedHUDDepthTexture = nullptr;
	std::array<ID3D11DepthStencilView*, 8> vrMapMenuSavedHUDDepthViews{};
	std::array<ID3D11DepthStencilView*, 8> vrMapMenuSavedHUDReadOnlyDepthViews{};
	ID3D11ShaderResourceView* vrMapMenuSavedHUDDepthSRV = nullptr;
	ID3D11ShaderResourceView* vrMapMenuSavedHUDStencilSRV = nullptr;
	uint32_t vrMapMenuUINativeWidth = 0;
	uint32_t vrMapMenuUINativeHeight = 0;
	uint32_t vrMapMenuUISupersampleWidth = 0;
	uint32_t vrMapMenuUISupersampleHeight = 0;
	bool vrMapMenuUISupersamplingActive = false;
	eastl::unique_ptr<Texture2D> vrMenuDesktopEyePair[2];
	eastl::unique_ptr<Texture2D> vrMenuDesktopRetainedEyePair[2];
	uint64_t vrMenuDesktopPairGeneration = 0;
	uint32_t vrMenuDesktopPairFrame = std::numeric_limits<uint32_t>::max();
	uint32_t vrMenuDesktopPairPlanGeneration = 0;
	uint32_t vrMenuDesktopPairReadyMask = 0;
	bool vrMenuDesktopPairPendingPresent = false;
	uint32_t vrMenuDesktopRetainedPairPlanGeneration = 0;
	bool vrMenuDesktopRetainedPairValid = false;

	struct VRMapMenuCopyRenderHook
	{
		static void thunk(void* a_imageSpaceShader, RE::BSTriShape* a_shape, RE::ImageSpaceEffectParam* a_param);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct VRMapMenuPostDisplayHook
	{
		static void thunk(RE::MapMenu* a_menu);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct OpenCompositeUpscalingBlocker
	{
		bool active = false;
		std::string settingName;
		std::string configPath;
	};

	OpenCompositeUpscalingBlocker GetOpenCompositeUpscalingBlocker(bool a_forceRefresh = false) const;
	void ApplyOpenCompositeUpscalingBlocker(bool a_forceRefresh = false);

	bool openCompositeUpscalingBackendSkipLogged = false;
	bool renderDocUpscalingBackendSkipLogged = false;

	struct Main_UpdateJitter
	{
		static void thunk(RE::BSGraphics::State* a_state);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct MenuManagerDrawInterfaceStartHook
	{
		static void thunk(int64_t a1);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_PostProcessing
	{
		static void thunk(RE::ImageSpaceManager* a_this, uint32_t a3, RE::RENDER_TARGET a_target, void* a_4, bool a_5);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct SetScissorRect
	{
		static void thunk(RE::BSGraphics::Renderer* This, int a_left, int a_top, int a_right, int a_bottom);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderPrecipitation
	{
		static void thunk();
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSFaceGenManager_UpdatePendingCustomizationTextures
	{
		static void thunk();
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct RaceSexMenu_ChangeName
	{
		static void thunk(RE::RaceSexMenu* a_this, const char* a_name);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	class MenuOpenCloseEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;
		static bool Register();
	};
};
