#pragma once

#include "../../Buffer.h"
#include "../../State.h"
#include "StreamlineFrameTokenPublication.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <d3d11_4.h>
#include <directx/d3d12.h>
#include <mutex>
#include <vector>

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
	std::atomic_bool initialized{ false };
	std::atomic_bool triedInitialization{ false };
	std::atomic_bool featureCheckComplete{ false };

	std::atomic_bool featureDLSS{ false };
	std::atomic_bool featureReflex{ false };
	std::atomic_bool featurePCL{ false };
	std::atomic_bool reflexSupportedOnCurrentAdapter{ false };

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

	using FrameTokenCoordinator =
		StreamlineFrameTokenPublication::Coordinator<sl::FrameToken*>;
	using FrameTokenSnapshot = FrameTokenCoordinator::Snapshot;
	FrameTokenCoordinator frameTokenCoordinator;

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
		uint32_t extentInLeft = 0;
		uint32_t extentInTop = 0;
		uint32_t extentOutWidth = 0;
		uint32_t extentOutHeight = 0;
		uint32_t extentOutLeft = 0;
		uint32_t extentOutTop = 0;
		int32_t viewportScaleXQ = 0;
		int32_t viewportScaleYQ = 0;
		int32_t pinholeOffsetXQ = 0;
		int32_t pinholeOffsetYQ = 0;
		int32_t jitterXQ = 0;
		int32_t jitterYQ = 0;
		bool historyResetRequested = false;
		uint64_t constantsIdentity = 0;
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

#ifdef DEVBENCH_BRIDGE_ENABLED
	static constexpr std::size_t kDLSSDevBenchTraceCapacity = 256;
	enum class DLSSDevBenchTraceStage : uint8_t
	{
		ConstantsCacheReuse,
		SetConstants,
		Evaluate
	};
	enum class DLSSDevBenchTraceSignatureField : uint8_t
	{
		Frame,
		FrameToken,
		RequestedViewport,
		ResolvedViewport,
		EyeIndex,
		ViewportRole,
		OutputWidth,
		OutputHeight,
		QualityMode,
		DLSSPreset,
		ExtentInLeft,
		ExtentInTop,
		ExtentInWidth,
		ExtentInHeight,
		ExtentOutLeft,
		ExtentOutTop,
		ExtentOutWidth,
		ExtentOutHeight,
		ViewportScaleX,
		ViewportScaleY,
		PinholeOffsetX,
		PinholeOffsetY,
		JitterX,
		JitterY,
		HistoryReset,
		ColorBuffersHDR,
		SubmitStageVR,
		ColorInput,
		ColorOutput,
		Depth,
		MotionVectors,
		ReactiveMask,
		TransparencyMask,
		CameraViewToClip,
		ClipToCameraView,
		ClipToLensClip,
		ClipToPrevClip,
		PrevClipToClip,
		ConstantsJitterOffset,
		MotionVectorScale,
		CameraPinholeOffset,
		CameraPosition,
		CameraUp,
		CameraRight,
		CameraForward,
		CameraNear,
		CameraFar,
		CameraFOV,
		CameraAspectRatio,
		MotionVectorsInvalidValue,
		DepthInverted,
		CameraMotionIncluded,
		MotionVectors3D,
		Reset,
		OrthographicProjection,
		MotionVectorsDilated,
		MotionVectorsJittered,
		MinRelativeLinearDepthObjectSeparation,
		Count
	};
	struct DLSSDevBenchConstantsPayload
	{
		std::array<uint32_t, 16> cameraViewToClip{};
		std::array<uint32_t, 16> clipToCameraView{};
		std::array<uint32_t, 16> clipToLensClip{};
		std::array<uint32_t, 16> clipToPrevClip{};
		std::array<uint32_t, 16> prevClipToClip{};
		std::array<uint32_t, 2> jitterOffset{};
		std::array<uint32_t, 2> motionVectorScale{};
		std::array<uint32_t, 2> cameraPinholeOffset{};
		std::array<uint32_t, 3> cameraPosition{};
		std::array<uint32_t, 3> cameraUp{};
		std::array<uint32_t, 3> cameraRight{};
		std::array<uint32_t, 3> cameraForward{};
		uint32_t cameraNear = 0;
		uint32_t cameraFar = 0;
		uint32_t cameraFOV = 0;
		uint32_t cameraAspectRatio = 0;
		uint32_t motionVectorsInvalidValue = 0;
		uint32_t minRelativeLinearDepthObjectSeparation = 0;
		uint8_t depthInverted = 0;
		uint8_t cameraMotionIncluded = 0;
		uint8_t motionVectors3D = 0;
		uint8_t reset = 0;
		uint8_t orthographicProjection = 0;
		uint8_t motionVectorsDilated = 0;
		uint8_t motionVectorsJittered = 0;
	};
	struct DLSSDevBenchTraceSignature
	{
		uint64_t traceSessionID = 0;
		uint32_t frame = 0;
		uint32_t frameToken = 0;
		uint64_t frameTokenAddress = 0;
		uint32_t requestedViewport = UINT32_MAX;
		uint32_t resolvedViewport = UINT32_MAX;
		uint32_t eyeIndex = 0;
		uint32_t viewportRole = 0;
		uint32_t outputWidth = 0;
		uint32_t outputHeight = 0;
		uint32_t qualityMode = 0;
		uint32_t dlssPreset = 0;
		uint32_t extentInLeft = 0;
		uint32_t extentInTop = 0;
		uint32_t extentInWidth = 0;
		uint32_t extentInHeight = 0;
		uint32_t extentOutLeft = 0;
		uint32_t extentOutTop = 0;
		uint32_t extentOutWidth = 0;
		uint32_t extentOutHeight = 0;
		int32_t viewportScaleXQ = 0;
		int32_t viewportScaleYQ = 0;
		int32_t pinholeOffsetXQ = 0;
		int32_t pinholeOffsetYQ = 0;
		int32_t jitterXQ = 0;
		int32_t jitterYQ = 0;
		bool historyResetRequested = false;
		bool colorBuffersHDR = false;
		bool submitStageVRDLSS = false;
		uint64_t colorIn = 0;
		uint64_t colorOut = 0;
		uint64_t depth = 0;
		uint64_t motionVectors = 0;
		uint64_t reactiveMask = 0;
		uint64_t transparencyMask = 0;
		DLSSDevBenchConstantsPayload constants{};
	};
	struct DLSSDevBenchTraceCall
	{
		uint64_t sequence = 0;
		uint64_t timestampQPC = 0;
		uint64_t compositorCycleToken = 0;
		uint32_t threadID = 0;
		int32_t resultCode = 0;
		DLSSDevBenchTraceStage stage = DLSSDevBenchTraceStage::SetConstants;
		std::array<char, 64> label{};
		DLSSDevBenchTraceSignature signature{};
	};
	struct DLSSDevBenchTraceRecord
	{
		uint64_t constantsChangedFieldMask = 0;
		uint64_t evaluationChangedFieldMask = 0;
		bool previousConstantsFound = false;
		bool previousEvaluationFound = false;
		DLSSDevBenchTraceCall current{};
		DLSSDevBenchTraceCall previousConstants{};
		DLSSDevBenchTraceCall previousEvaluation{};
	};
	struct DLSSDevBenchTraceSnapshot
	{
		bool active = false;
		uint64_t sessionID = 0;
		uint64_t timestampQPCFrequency = 0;
		uint64_t retainedRecords = 0;
		uint64_t totalRecords = 0;
		uint64_t overwrittenRecords = 0;
		uint64_t droppedRecords = 0;
		uint64_t constantsCacheReuses = 0;
		uint64_t setConstantsCalls = 0;
		uint64_t evaluateCalls = 0;
		uint64_t duplicatedConstantsFailures = 0;
		uint64_t evaluateFailures = 0;
		uint64_t lastDuplicatedConstantsFailureSequence = 0;
		uint64_t lastEvaluateFailureSequence = 0;
		bool lastDuplicatedConstantsFailureFound = false;
		bool lastEvaluateFailureFound = false;
		DLSSDevBenchTraceRecord lastDuplicatedConstantsFailure{};
		DLSSDevBenchTraceRecord lastEvaluateFailure{};
		std::vector<DLSSDevBenchTraceRecord> records;
	};

	/** Starts a cleared, fixed-capacity DLSS call trace for DevBench. */
	bool StartDLSSDevBenchTrace();
	/** Stops DLSS trace collection while preserving its records.
	 * A nonzero session ID fails closed if ownership changed. */
	bool StopDLSSDevBenchTrace(uint64_t a_expectedSessionID = 0);
	/** Clears a stopped DLSS trace. */
	bool ResetDLSSDevBenchTrace();
	/** Reports whether a DLSS trace session is accepting records. */
	[[nodiscard]] bool IsDLSSDevBenchTraceActive() const noexcept;
	/** Copies trace counters and, when requested, the retained records. */
	[[nodiscard]] DLSSDevBenchTraceSnapshot GetDLSSDevBenchTraceSnapshot(bool a_includeRecords = true) const;
	/** Sets this thread's compositor-cycle context and returns its prior value. */
	uint64_t SetDLSSDevBenchCompositorCycleContext(uint64_t a_compositorCycleToken) noexcept;
#endif

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

	bool LoadInterposer();
	void Shutdown();
	bool TryUpgradeInterface(void** a_interface);
	bool TrySetD3DDevice(ID3D11Device* a_device);
	void MarkAdapterUnavailable(const char* a_reason);

	bool CheckFeatures(IDXGIAdapter* a_adapter);

	bool PostDevice();

	bool CheckFrameConstants(sl::ViewportHandle p_viewport, sl::FrameToken* frameToken, uint32_t eyeIndex = 0, float viewportScaleX = 1.0f, float viewportScaleY = 1.0f, float pinholeOffsetX = 0.0f, float pinholeOffsetY = 0.0f, const DLSSDispatchDiagnostics* diagnostics = nullptr
#ifdef DEVBENCH_BRIDGE_ENABLED
		,
		DLSSDevBenchTraceSignature* outFrameConstantsSignature = nullptr
#endif
	);
	/** Acquires or reuses the token published for the exact logical frame. */
	[[nodiscard]] std::optional<FrameTokenSnapshot> AcquireFrameToken(
		uint32_t a_frame,
		const char* a_consumer);

	bool IsRTXAndBelow40Series(const DXGI_ADAPTER_DESC& a_adapterDesc) const;
	[[nodiscard]] bool IsFrameGenerationQuarantinedByReflex() const noexcept
	{
		return frameGenerationQuarantinedByReflex.load(std::memory_order_acquire);
	}

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
	/** @brief Reports whether the complete Streamline DLSS activation contract is live. */
	[[nodiscard]] bool IsDLSSRuntimeReady() const noexcept;
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
	/** @brief Enforces the same-frame Reflex exclusion required before frame generation admission. */
	bool EnsureReflexDisabledForFrameGeneration();
	void UpdateReflex();

	DLSSResourceTeardownResult DestroyDLSSResources();

	enum class LifecycleState : uint8_t
	{
		Uninitialized,
		Initializing,
		Initialized,
		Unavailable,
		ShuttingDown,
		ShutdownQuarantined,
	};
	std::mutex lifecycleMutex;
	std::atomic<LifecycleState> lifecycleState{ LifecycleState::Uninitialized };
	ID3D11Device* boundDeviceIdentity = nullptr;
	bool adapterSupportsDLSS = false;
	bool adapterSupportsReflex = false;
	bool adapterSupportsPCL = false;
	bool runtimeHasDLSS = false;
	bool runtimeHasReflex = false;
	bool runtimeHasPCL = false;
	std::atomic_bool frameGenerationQuarantinedByReflex{ false };
};
