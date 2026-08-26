#pragma once

#include "../../Buffer.h"
#include "../../State.h"

#include <array>
#include <cstdint>
#include <d3d11_4.h>
#include <directx/d3d12.h>

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
		int32_t viewportScaleXQ = 0;
		int32_t viewportScaleYQ = 0;
		int32_t pinholeOffsetXQ = 0;
		int32_t pinholeOffsetYQ = 0;
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
		sl::ViewportHandle viewport[2] = { sl::ViewportHandle(0), sl::ViewportHandle(1) };
		bool resourcesAllocated[2] = { false, false };
		DLSSOptionsCache optionsCache[2]{};
	};

	DLSSOptionsCache nonVRDLSSOptionsCache{};
	VRDLSSViewportSlot vrDLSSViewportSlots[kVRDLSSViewportRoleCount][kVRDLSSViewportSlotCount]{};
	static constexpr uint32_t kDLSSFrameConstantsCacheSize = 16;
	std::array<DLSSFrameConstantsCache, kDLSSFrameConstantsCacheSize> dlssFrameConstantsCache{};
	uint64_t vrDLSSViewportUseCounter = 0;
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
		float pinholeOffsetX = 0.0f;
		float pinholeOffsetY = 0.0f;
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
		Failed,
		FailedAfterMutation
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
		float pinholeOffsetX = 0.0f, float pinholeOffsetY = 0.0f, const char* label = "DLSS Evaluate",
		DLSSViewportRole viewportRole = DLSSViewportRole::FullEye,
		bool useAuthoritativeProfile = false,
		uint32_t authoritativeQualityMode = 0,
		uint32_t authoritativeDLSSPreset = 1);

	// Cached DLL version info for Streamline plugin directory
	static std::vector<std::pair<std::string, std::string>> dllVersions;

	void LoadInterposer();

	void CheckFeatures(IDXGIAdapter* a_adapter);

	void PostDevice();

	bool CheckFrameConstants(sl::ViewportHandle p_viewport, uint32_t eyeIndex = 0, float viewportScaleX = 1.0f, float viewportScaleY = 1.0f, float pinholeOffsetX = 0.0f, float pinholeOffsetY = 0.0f, const DLSSDispatchDiagnostics* diagnostics = nullptr);
	bool EnsureFrameToken();

	bool IsRTXAndBelow40Series(IDXGIAdapter* a_adapter);

	/** @brief Makes the bounded VR viewport slot for a DLSS profile safe to use without dispatching DLSS. */
	DLSSViewportPreparationResult PrepareVRDLSSViewport(DLSSViewportRole viewportRole, uint32_t qualityMode, uint32_t dlssPreset);
	bool ResolveDLSSViewport(DLSSViewportRole viewportRole, sl::ViewportHandle p_viewport, uint32_t eyeIndex, uint32_t qualityMode, uint32_t dlssPreset, sl::ViewportHandle& outViewport);
	int FindVRDLSSViewportSlot(DLSSViewportRole viewportRole, uint32_t qualityMode, uint32_t dlssPreset) const;
	bool TryResolveExistingVRDLSSViewport(
		DLSSViewportRole a_viewportRole,
		uint32_t a_eyeIndex,
		uint32_t a_qualityMode,
		uint32_t a_dlssPreset,
		uint32_t a_outputWidth,
		uint32_t a_outputHeight,
		ID3D11Resource* a_colorInput,
		sl::ViewportHandle& a_viewport) const;
	int ChooseVRDLSSViewportSlotForAllocation(DLSSViewportRole viewportRole) const;
	bool FreeDLSSViewportResources(sl::ViewportHandle a_viewport, uint32_t a_eyeIndex, bool a_logFailures);
	bool FreeVRDLSSViewportSlot(DLSSViewportRole viewportRole, uint32_t slotIndex, bool logFailures);
	DLSSOptionsCache& GetDLSSOptionsCache(DLSSViewportRole viewportRole, uint32_t eyeIndex, uint32_t qualityMode, uint32_t dlssPreset);
	bool SetDLSSOptions(DLSSViewportRole viewportRole, sl::ViewportHandle p_viewport, uint32_t eyeIndex, uint32_t width, uint32_t height, bool colorBuffersHDR, uint32_t qualityMode, uint32_t dlssPreset, const DLSSDispatchDiagnostics* diagnostics = nullptr);
	void InvalidateDLSSOptionsCache();
	void ResetDLSSIdleFences();
	void ResetFrameTracking();
	void ClearLastDLSSFailureState() { lastDLSSFailureDuplicatedConstants = false; }
	bool WasLastDLSSFailureDuplicatedConstants() const { return lastDLSSFailureDuplicatedConstants; }
	bool HasDLSSResourcesPendingTeardown() const;
	/** @brief Verifies one eye's exact cached DLSS option contract. */
	[[nodiscard]] bool IsVRDLSSViewportResourceCompatible(
		const VRDLSSViewportSlot& a_slot,
		uint32_t a_eyeIndex,
		uint32_t a_qualityMode,
		uint32_t a_dlssPreset,
		uint32_t a_outputWidth,
		uint32_t a_outputHeight,
		ID3D11Resource* a_colorInput) const noexcept;
	[[nodiscard]] bool HasCompleteVRDLSSViewportResources() const noexcept
	{
		if (activeDLSSViewportResourcesAllocated[0] &&
			activeDLSSViewportResourcesAllocated[1]) {
			return true;
		}

		for (const auto& roleSlots : vrDLSSViewportSlots) {
			for (const auto& slot : roleSlots) {
				if (slot.valid &&
					slot.resourcesAllocated[0] && slot.resourcesAllocated[1] &&
					slot.optionsCache[0].valid && slot.optionsCache[1].valid) {
					return true;
				}
			}
		}
		return false;
	}
	/** @brief Proves exact option identity and ownership for both eyes of one slot. */
	[[nodiscard]] bool HasCompleteVRDLSSViewportResources(
		DLSSViewportRole a_viewportRole,
		uint32_t a_qualityMode,
		uint32_t a_dlssPreset,
		uint32_t a_outputWidth,
		uint32_t a_outputHeight,
		ID3D11Resource* a_colorInput) const noexcept;

	bool Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors);
	bool UpscaleRegion(uint32_t eyeIndex, ID3D11Resource* colorIn, ID3D11Resource* colorOut, ID3D11Resource* depth,
		ID3D11Resource* mvec, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask,
		uint32_t renderWidth, uint32_t renderHeight, uint32_t outputWidth, uint32_t outputHeight,
		float pinholeOffsetX = 0.0f, float pinholeOffsetY = 0.0f);
	void UpdateReflex();

	DLSSResourceTeardownResult DestroyDLSSResources();
};
