#pragma once

#include "../../Buffer.h"
#include "../../State.h"
#include "DLSSViewportCrop.h"
#include "FrameTelemetryRing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d11_4.h>
#include <directx/d3d12.h>
#include <mutex>

#define NV_WINDOWS

#pragma warning(push)
#pragma warning(disable: 4471)
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_matrix_helpers.h>
#include <sl_reflex.h>
#include <sl_version.h>
#pragma warning(pop)

class Streamline
{
public:
	static constexpr const wchar_t* PluginDir = L"Data\\Shaders\\Upscaling\\Streamline";

	Streamline() = default;
	~Streamline();

	inline std::string GetShortName() { return "Streamline"; }

	bool enabledAtBoot = false;
	bool initialized = false;
	bool triedInitialization = false;
	bool featureCheckComplete = false;

	bool featureDLSS = false;
	bool featureReflex = false;
	bool featurePCL = false;
	bool reflexSupportedOnCurrentAdapter = false;

	sl::ViewportHandle viewport{ 0 };
	sl::ViewportHandle viewportRight{ 1 };
	enum class DLSSViewportRole : uint8_t
	{
		FullEye = 0,
		FoveatedCenter,
		// Submit-stage foveated DLSS needs isolated Streamline viewport state.
		SubmitStageFoveatedCenter,
		Count
	};
	enum class DLSSPassRoute : uint8_t
	{
		Main,
		Submit,
		Count
	};
	static constexpr std::size_t kDLSSPassRouteCount =
		static_cast<std::size_t>(DLSSPassRoute::Count);
	static constexpr std::size_t kDLSSPassEyeCount = 2;
	static constexpr std::size_t kDLSSPassTelemetryFrameCount = 4;
	struct DLSSPassTelemetrySnapshot
	{
		bool valid = false;
		uint32_t frame = 0;
		std::array<std::array<uint32_t, kDLSSPassEyeCount>, kDLSSPassRouteCount> attempts{};
		std::array<std::array<uint32_t, kDLSSPassEyeCount>, kDLSSPassRouteCount> successes{};
	};
	[[nodiscard]] DLSSPassTelemetrySnapshot GetDLSSPassTelemetrySnapshot(
		uint32_t a_frame) const noexcept;
	struct DLSSViewportCropTelemetrySnapshot
	{
		bool valid = false;
		bool evaluationSucceeded = false;
		uint32_t frame = 0;
		uint32_t eyeIndex = 0;
		DLSSViewportRole viewportRole = DLSSViewportRole::FullEye;
		uint32_t viewport = UINT32_MAX;
		uint64_t generation = 0;
		UpscalingDLSS::ViewportCrop current{};
		UpscalingDLSS::ViewportCrop previous{};
		bool continuous = false;
		bool sameFrameReplay = false;
		bool cropReset = true;
		bool effectiveReset = true;
		UpscalingDLSS::CropHistoryResetReason resetReason =
			UpscalingDLSS::CropHistoryResetReason::InvalidDescriptor;
		float motionVectorScaleX = 1.0f;
		float motionVectorScaleY = 1.0f;
	};
	[[nodiscard]] DLSSViewportCropTelemetrySnapshot
	GetDLSSViewportCropTelemetrySnapshot(
		DLSSViewportRole a_role,
		uint32_t a_eyeIndex) const noexcept;
	static constexpr uint32_t kVRDLSSViewportRoleCount = static_cast<uint32_t>(DLSSViewportRole::Count);
	static constexpr uint32_t kVRDLSSViewportSlotCount = 2;
	static constexpr uint32_t kVRDLSSSlotViewportBase = 0x1000;
	static constexpr uint32_t kVRDLSSSlotViewportRoleStride = 0x100;
	static constexpr uint32_t kVRDLSSSlotViewportEyeStride = 2;
	static constexpr uint32_t GetDLSSViewportRoleIndex(DLSSViewportRole a_role)
	{
		const auto roleIndex = static_cast<uint32_t>(a_role);
		return roleIndex < kVRDLSSViewportRoleCount ? roleIndex : static_cast<uint32_t>(DLSSViewportRole::FullEye);
	}
	static constexpr uint32_t MAX_RESOLUTION = 8192;
	mutable std::mutex dlssPassTelemetryMutex;
	UpscalingTelemetry::FrameTelemetryRing<
		DLSSPassTelemetrySnapshot,
		kDLSSPassTelemetryFrameCount>
		dlssPassTelemetryFrames;
	mutable std::mutex dlssViewportCropTelemetryMutex;
	std::array<
		std::array<DLSSViewportCropTelemetrySnapshot, kDLSSPassEyeCount>,
		kVRDLSSViewportRoleCount>
		dlssViewportCropTelemetry{};
	HMODULE interposer = NULL;

	// SL Interposer Functions
	PFun_slInit* slInit{};
	PFun_slShutdown* slShutdown{};
	PFun_slIsFeatureSupported* slIsFeatureSupported{};
	PFun_slIsFeatureLoaded* slIsFeatureLoaded{};
	PFun_slSetFeatureLoaded* slSetFeatureLoaded{};
	PFun_slEvaluateFeature* slEvaluateFeature{};
	PFun_slAllocateResources* slAllocateResources{};
	PFun_slFreeResources* slFreeResources{};
	PFun_slGetFeatureRequirements* slGetFeatureRequirements{};
	PFun_slGetFeatureVersion* slGetFeatureVersion{};
	PFun_slUpgradeInterface* slUpgradeInterface{};
	PFun_slSetConstants* slSetConstants{};
	PFun_slGetNativeInterface* slGetNativeInterface{};
	PFun_slGetFeatureFunction* slGetFeatureFunction{};
	PFun_slGetNewFrameToken* slGetNewFrameToken{};
	PFun_slSetD3DDevice* slSetD3DDevice{};

	// DLSS specific functions
	PFun_slDLSSGetOptimalSettings* slDLSSGetOptimalSettings{};
	PFun_slDLSSGetState* slDLSSGetState{};
	PFun_slDLSSSetOptions* slDLSSSetOptions{};

	// Reflex specific functions
	PFun_slReflexGetState* slReflexGetState{};
	PFun_slReflexSleep* slReflexSleep{};
	PFun_slReflexSetOptions* slReflexSetOptions{};
	PFun_slPCLSetMarker* slPCLSetMarker{};

	Util::FrameChecker frameChecker;
	sl::FrameToken* frameToken = nullptr;

	bool isRTXBelow40series = false;
	struct DLSSOptionsCache
	{
		bool valid = false;
		uint32_t viewport = UINT32_MAX;
		uint32_t outputWidth = 0;
		uint32_t outputHeight = 0;
		uint32_t qualityMode = 0;
		uint32_t dlssPreset = 0;
		bool isHDR = false;
		bool useLegacyProfile = false;
	};
	struct DLSSFrameConstantsCache
	{
		bool valid = false;
		uint32_t frame = 0;
		std::uintptr_t frameToken = 0;
		uint32_t viewport = UINT32_MAX;
		uint32_t eyeIndex = 0;
		uint32_t viewportRole = 0;
		uint32_t outputWidth = 0;
		uint32_t outputHeight = 0;
		uint32_t qualityMode = 0;
		uint32_t dlssPreset = 0;
		uint32_t extentInWidth = 0;
		uint32_t extentInHeight = 0;
		uint32_t extentOutWidth = 0;
		uint32_t extentOutHeight = 0;
		UpscalingDLSS::ViewportCrop currentCrop{};
		UpscalingDLSS::ViewportCrop previousCrop{};
		uint64_t cropGeneration = 0;
		uint32_t cropResetReason = 0;
		int32_t motionVectorScaleXQ = 0;
		int32_t motionVectorScaleYQ = 0;
		bool cropContinuous = false;
		int32_t jitterXQ = 0;
		int32_t jitterYQ = 0;
		bool historyResetRequested = false;
	};

	struct VRDLSSViewportSlot
	{
		bool valid = false;
		uint32_t qualityMode = 0;
		uint32_t dlssPreset = 0;
		uint64_t lastUse = 0;
		uint64_t generation = 0;
		sl::ViewportHandle viewport[2] = { sl::ViewportHandle(0), sl::ViewportHandle(1) };
		bool resourcesAllocated[2] = { false, false };
		DLSSOptionsCache optionsCache[2]{};
		UpscalingDLSS::SuccessfulCropHistory cropHistory[2]{};
	};

	DLSSOptionsCache nonVRDLSSOptionsCache{};
	UpscalingDLSS::SuccessfulCropHistory nonVRDLSSCropHistory{};
	VRDLSSViewportSlot vrDLSSViewportSlots[kVRDLSSViewportRoleCount][kVRDLSSViewportSlotCount]{};
	static constexpr uint32_t kDLSSFrameConstantsCacheSize = 16;
	std::array<DLSSFrameConstantsCache, kDLSSFrameConstantsCacheSize> dlssFrameConstantsCache{};
	uint64_t vrDLSSViewportUseCounter = 0;
	uint64_t vrDLSSViewportGenerationCounter = 0;
	std::array<bool, 2> activeDLSSViewportResourcesAllocated = {};
	ID3D11Query* pendingDLSSResourceFreeIdleFence = nullptr;
	std::array<ID3D11Query*, kVRDLSSViewportRoleCount> pendingVRDLSSSlotRecycleIdleFences{};

	struct ReflexOptionsCache
	{
		bool valid = false;
		sl::ReflexMode mode = sl::ReflexMode::eOff;
		uint32_t frameLimitUs = 0;
		bool useMarkersToOptimize = false;
	};
	ReflexOptionsCache reflexOptionsCache{};
	uint32_t lastReflexSleepFrame = UINT32_MAX;
	bool lastDLSSFailureDuplicatedConstants = false;

	struct DLSSDispatchDiagnostics
	{
		const char* label = "DLSS Evaluate";
		uint32_t frame = 0;
		uint32_t eyeIndex = 0;
		sl::ViewportHandle requestedViewport{ 0 };
		sl::ViewportHandle resolvedViewport{ 0 };
		sl::Extent extentIn{};
		sl::Extent extentOut{};
		uint32_t outputWidth = 0;
		uint32_t outputHeight = 0;
		uint32_t qualityMode = 0;
		uint32_t dlssPreset = 0;
		DLSSViewportRole viewportRole = DLSSViewportRole::FullEye;
		float viewportScaleX = 1.0f;
		float viewportScaleY = 1.0f;
		bool croppedViewport = false;
		UpscalingDLSS::ViewportCrop currentCrop{};
		UpscalingDLSS::ViewportCrop previousCrop{};
		uint64_t cropGeneration = 0;
		UpscalingDLSS::CropHistoryResetReason cropResetReason =
			UpscalingDLSS::CropHistoryResetReason::InvalidDescriptor;
		bool cropContinuous = false;
		bool cropSameFrameReplay = false;
		bool cropReset = true;
		float motionVectorScaleX = 1.0f;
		float motionVectorScaleY = 1.0f;
		float jitterX = 0.0f;
		float jitterY = 0.0f;
		bool colorBuffersHDR = false;
		bool submitStageVRDLSS = false;
		bool presentationUpscalingActive = false;
		bool renderScaleActive = false;
		bool foveatedDispatchEnabled = false;
		bool peripheryTAAEnabled = false;
		bool historyResetRequested = false;
		bool optionsCacheValid = false;
		uint32_t optionsCacheViewport = UINT32_MAX;
		uint32_t optionsCacheOutputWidth = 0;
		uint32_t optionsCacheOutputHeight = 0;
		uint32_t optionsCacheQualityMode = 0;
		uint32_t optionsCacheDLSSPreset = 0;
		bool optionsCacheHDR = false;
		bool optionsCacheLegacyProfile = false;
		sl::FrameToken* frameToken = nullptr;
		ID3D11Resource* colorIn = nullptr;
		ID3D11Resource* colorOut = nullptr;
		ID3D11Resource* depth = nullptr;
		ID3D11Resource* motionVectors = nullptr;
		ID3D11Resource* reactiveMask = nullptr;
		ID3D11Resource* transparencyMask = nullptr;
	};

	enum class DLSSResourceTeardownResult : uint8_t
	{
		Ready,
		Pending,
		Failed
	};
	enum class DLSSViewportPreparationResult : uint8_t
	{
		Ready,
		Pending,
		Failed
	};

	// Helper: Execute DLSS for a single viewport with given resources
	bool EvaluateDLSS(sl::ViewportHandle vp, uint32_t eyeIndex,
		ID3D11Resource* colorIn, ID3D11Resource* colorOut, ID3D11Resource* depth,
		ID3D11Resource* mvec, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask,
		const sl::Extent& extentIn, const sl::Extent& extentOut, uint32_t outputWidth,
		const char* label = "DLSS Evaluate",
		DLSSViewportRole viewportRole = DLSSViewportRole::FullEye,
		const UpscalingDLSS::ViewportCrop& viewportCrop = {});

	// Cached DLL version info for Streamline plugin directory
	static std::vector<std::pair<std::string, std::string>> dllVersions;

	void LoadInterposer();

	void CheckFeatures(IDXGIAdapter* a_adapter);

	void PostDevice();

	bool CheckFrameConstants(
		sl::ViewportHandle p_viewport,
		uint32_t eyeIndex,
		const UpscalingDLSS::ViewportCrop& currentCrop,
		const UpscalingDLSS::CropContinuityDecision& cropContinuity,
		const DLSSDispatchDiagnostics* diagnostics = nullptr);
	bool EnsureFrameToken();

	bool IsRTXAndBelow40Series(IDXGIAdapter* a_adapter);

	/** @brief Makes the bounded VR viewport slot for a DLSS profile safe to use without dispatching DLSS. */
	DLSSViewportPreparationResult PrepareVRDLSSViewport(DLSSViewportRole viewportRole, uint32_t qualityMode, uint32_t dlssPreset);
	bool ResolveDLSSViewport(DLSSViewportRole viewportRole, sl::ViewportHandle p_viewport, uint32_t eyeIndex, uint32_t qualityMode, uint32_t dlssPreset, sl::ViewportHandle& outViewport);
	int FindVRDLSSViewportSlot(DLSSViewportRole viewportRole, uint32_t qualityMode, uint32_t dlssPreset) const;
	int ChooseVRDLSSViewportSlotForAllocation(DLSSViewportRole viewportRole) const;
	bool FreeDLSSViewportResources(sl::ViewportHandle a_viewport, uint32_t a_eyeIndex, bool a_logFailures);
	bool FreeVRDLSSViewportSlot(DLSSViewportRole viewportRole, uint32_t slotIndex, bool logFailures);
	DLSSOptionsCache& GetDLSSOptionsCache(DLSSViewportRole viewportRole, uint32_t eyeIndex, uint32_t qualityMode, uint32_t dlssPreset);
	UpscalingDLSS::SuccessfulCropHistory* GetDLSSCropHistory(
		DLSSViewportRole viewportRole,
		uint32_t eyeIndex,
		uint32_t qualityMode,
		uint32_t dlssPreset);
	uint64_t GetDLSSViewportGeneration(
		DLSSViewportRole viewportRole,
		uint32_t qualityMode,
		uint32_t dlssPreset) const;
	bool SetDLSSOptions(DLSSViewportRole viewportRole, sl::ViewportHandle p_viewport, uint32_t eyeIndex, uint32_t width, uint32_t height, bool colorBuffersHDR, uint32_t qualityMode, uint32_t dlssPreset, const DLSSDispatchDiagnostics* diagnostics = nullptr);
	void InvalidateDLSSOptionsCache();
	void InvalidateDLSSCropHistory();
	void ResetDLSSIdleFences();
	void ResetFrameTracking();
	void ClearLastDLSSFailureState() { lastDLSSFailureDuplicatedConstants = false; }
	bool WasLastDLSSFailureDuplicatedConstants() const { return lastDLSSFailureDuplicatedConstants; }
	bool HasDLSSResourcesPendingTeardown() const;

	void Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors);
	bool UpscaleRegion(uint32_t eyeIndex, ID3D11Resource* colorIn, ID3D11Resource* colorOut, ID3D11Resource* depth,
		ID3D11Resource* mvec, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask,
		uint32_t renderWidth, uint32_t renderHeight, uint32_t outputWidth, uint32_t outputHeight,
		const UpscalingDLSS::ViewportCrop& viewportCrop = {});
	void UpdateReflex();

	DLSSResourceTeardownResult DestroyDLSSResources();
};
