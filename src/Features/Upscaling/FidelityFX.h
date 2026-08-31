#pragma once

#include <d3d11_4.h>
#include <directx/d3d12.h>
#include <winrt/base.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <FidelityFX/host/backends/dx11/ffx_dx11.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_interface.h>

#include <FidelityFX/api/include/dx12/ffx_api_dx12.hpp>

#include <FidelityFX/api/include/ffx_api.hpp>
#include <FidelityFX/api/include/ffx_api_loader.h>
#include <FidelityFX/framegeneration/include/dx12/ffx_api_framegeneration_dx12.hpp>
#include <FidelityFX/framegeneration/include/ffx_framegeneration.hpp>
#include <FidelityFX/upscalers/include/ffx_upscale.hpp>

#include "../../Buffer.h"
#include "../../State.h"

class WrappedResource;

class FidelityFX
{
public:
	enum class LifecycleResult : uint8_t
	{
		Ready,
		Pending,
		Failed,
		DeviceLost,
		RuntimeDeviceLost
	};

	enum class Fsr4AdapterSupport
	{
		Unsupported,
		RadeonRx7000,
		RadeonRx9000
	};
	enum class RuntimeUpscalerFramePath : uint8_t
	{
		kInactive = 0,
		kHostFsr31 = 1,
		kRuntimeFsr31 = 2,
		kRuntimeFsr4 = 3,
		kHostFsr31Fallback = 4
	};
	enum class StereoUpscaleResult : uint8_t
	{
		NotHandled,
		Ready,
		Failed
	};
	/** @brief Complete resource and active-extent contract for one FSR context. */
	struct UpscaleRegionParameters
	{
		uint32_t contextIndex = 0;
		ID3D11Resource* color = nullptr;
		ID3D11Resource* depth = nullptr;
		ID3D11Resource* motionVectors = nullptr;
		ID3D11Resource* reactiveMask = nullptr;
		ID3D11Resource* transparencyCompositionMask = nullptr;
		ID3D11Resource* output = nullptr;
		uint32_t renderWidth = 0;
		uint32_t renderHeight = 0;
		uint32_t displayWidth = 0;
		uint32_t displayHeight = 0;
		float motionVectorScaleX = 0.0f;
		float motionVectorScaleY = 0.0f;
		float sharpness = 0.0f;
	};
#ifdef DEVBENCH_BRIDGE_ENABLED
	struct RuntimeUpscalerDispatchSnapshot
	{
		bool valid = false;
		uint32_t frame = 0;
		RuntimeUpscalerFramePath path = RuntimeUpscalerFramePath::kInactive;
		uint64_t serial = 0;
	};
#endif

	static constexpr const wchar_t* PluginDir = L"Data\\Shaders\\Upscaling\\FidelityFX";
	static constexpr uint32_t Fsr3Version = FFX_UPSCALER_MAKE_VERSION(FFX_FSR3_VERSION_MAJOR, FFX_FSR3_VERSION_MINOR, FFX_FSR3_VERSION_PATCH);
	static constexpr std::wstring_view RuntimeUpscalerDllName = L"amd_fidelityfx_upscaler_dx12.dll";
	static constexpr std::string_view RuntimeUpscalerDllNameUtf8 = "amd_fidelityfx_upscaler_dx12.dll";
	~FidelityFX();

	HMODULE module = nullptr;

	ffx::Context swapChainContext{};
	ffx::Context frameGenContext{};
	bool swapChainContextValid = false;
	bool frameGenContextValid = false;
	FfxFsr3Context fsrContext[2];

	bool featureFSR3FG = false;
	bool featureRuntimeUpscaler = false;

	// Track if FidelityFX is currently being used for frame generation
	bool isFrameGenActive = false;

	// Cached DLL version info for FidelityFX plugin directory
	static std::vector<std::pair<std::string, std::string>> dllVersions;

	void LoadFFX();
	bool IsFrameGenerationRuntimeReady() const noexcept;
	bool IsFrameGenerationQuarantined() const noexcept;
	bool CreateFrameGenerationContext(ffx::Context& a_context, ffxCreateContextDescHeader* a_desc) noexcept;
	bool SetupFrameGeneration();
	bool ResetFrameGenerationContexts() noexcept;
	bool Present(bool a_useFrameGeneration) noexcept;
	ffxReturnCode_t DispatchFrameGenerationCallback(ffxDispatchDescFrameGeneration* a_parameters) noexcept;

	LifecycleResult CreateFSRResources();

	LifecycleResult DestroyFSRResources(bool a_waitForIdle = true);
	bool HasFSRResources() const;
	bool AreFSRResourcesCompatible(uint32_t a_renderWidth, uint32_t a_renderHeight, uint32_t a_displayWidth, uint32_t a_displayHeight, uint32_t a_contextCount) const;
	/** @brief Proves a complete reusable runtime FSR provider generation. */
	bool AreRuntimeUpscalerResourcesCompatible(
		uint32_t a_fullRenderWidth,
		uint32_t a_fullRenderHeight,
		uint32_t a_fullDisplayWidth,
		uint32_t a_fullDisplayHeight,
		uint32_t a_contextCount,
		uint32_t a_requestedVersion) const;
	bool HasFSRResourcesPendingTeardown() const;
	[[nodiscard]] HRESULT GetLastFSRDeviceRemovedReason() const noexcept { return fsrLastDeviceRemovedReason; }
	LifecycleResult ProbeFSRDeviceStatus() noexcept { return RecordFSRDeviceStatus(); }
	LifecycleResult PollFSRResourceTeardownReady(const char* a_reason = nullptr);
	void ResetFSRIdleFence();
	LifecycleResult ResetRuntimeUpscalerResources(bool a_invalidateProviderCache = false);

	bool IsAmdAdapterDetected() const;
	bool IsNvidiaAdapterDetected() const;
	bool IsRuntimeUpscalerPresent() const;
	static Fsr4AdapterSupport GetFsr4AdapterSupport(const DXGI_ADAPTER_DESC& a_adapterDesc);
	Fsr4AdapterSupport GetFsr4AdapterSupport() const;
	bool IsRuntimeFsr4AutoEligible() const;
	bool IsRuntimeFsr4Available() const;
	bool ShouldRequestRuntimeFsr4() const;
	bool ShouldUseRuntimeUpscalerForFSR() const;
	bool HasRuntimeUpscalerSupportCheckResult() const;
	bool IsRuntimeUpscalerSupportConfirmed() const;
	bool IsRuntimeUpscalerProviderMatchingRequestedVersion() const;
	bool IsRuntimeUpscalerFailureLatched() const;
	bool IsRuntimeFsr4FailureLatched() const;
	/** @brief Validates that the provider which produced a dispatch proof remains usable. */
	bool IsRuntimeUpscalerDispatchProofUsable(RuntimeUpscalerFramePath a_path) const;
	const std::string& GetRuntimeUpscalerLastFramePathLabel() const;
	const std::string& GetConfiguredFsrPathLabel() const;
	const std::string& GetDisplayedFsrPathLabel() const;
	static const std::string& GetHostFsrSdkLabel();
	static const std::string& GetRuntimeUpscalerLabel(uint32_t a_version);
	std::string GetRuntimeUpscalerProviderName() const;
	std::string GetRuntimeUpscalerRequestedVersionString() const;
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** @brief Render-thread-only copy used to publish actual FSR path evidence under the controller lock. */
	RuntimeUpscalerDispatchSnapshot GetRuntimeUpscalerDispatchSnapshotForRenderThread() const;
#endif

	bool Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_depth, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, float a_sharpness);
	bool UpscaleRegion(uint32_t a_contextIndex, ID3D11Resource* a_color, ID3D11Resource* a_depth, ID3D11Resource* a_motionVectors,
		ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_output,
		uint32_t a_renderWidth, uint32_t a_renderHeight, uint32_t a_displayWidth, uint32_t a_displayHeight,
		float a_motionVectorScaleX, float a_motionVectorScaleY, float a_sharpness, bool* a_usedRuntimeUpscaler = nullptr);
	/** @brief Dispatch both ready VR runtime-FSR contexts in one interop transaction when eligible. */
	StereoUpscaleResult UpscaleStereoRegions(const std::array<UpscaleRegionParameters, 2>& a_regions);

private:
	void QuarantineFrameGenerationForSession(const char* a_reason) noexcept;
	std::atomic_bool frameGenerationSessionQuarantined{ false };
	bool frameGenContextIndeterminate = false;
	bool swapChainContextIndeterminate = false;

	LifecycleResult RecordFSRDeviceStatus() noexcept;
	LifecycleResult RecordRuntimeUpscalerDeviceStatus() noexcept;
	LifecycleResult ResolveFSRLifecycleFailure(const char* a_operation);
	LifecycleResult ResolveRuntimeUpscalerLifecycleFailure(const char* a_operation);
	[[nodiscard]] bool IsRuntimeUpscalerOwnershipDetached() const noexcept;
	LifecycleResult GetQuarantinedHostFSRResult(const char* a_operation);
	LifecycleResult RetireRuntimeUpscalerWhileHostFSRQuarantined(const char* a_operation);
	LifecycleResult DestroyTrackedHostFSRContexts(const char* a_operation);
	void QuarantineHostFSRState(const char* a_reason);
	void QuarantineHostFSRContext(uint32_t a_contextIndex, const char* a_reason);
	LifecycleResult ReleaseHostFSRResources();

	// FSR scratch buffer - needs to be freed in DestroyFSRResources
	void* fsrScratchBuffer = nullptr;
	uint32_t fsrContextCount = 0;
	bool fsrContextValid[2]{};
	bool fsrContextIndeterminate[2]{};
	bool fsrHostStateQuarantined = false;
	HRESULT fsrLastDeviceRemovedReason = S_OK;
	HRESULT runtimeUpscalerLastDeviceRemovedReason = S_OK;
	uint32_t fsrContextMaxRenderWidth = 0;
	uint32_t fsrContextMaxRenderHeight = 0;
	uint32_t fsrContextDisplayWidth = 0;
	uint32_t fsrContextDisplayHeight = 0;

	uint32_t runtimeUpscalerContextCount = 0;
	uint32_t runtimeUpscalerMaxRenderWidth = 0;
	uint32_t runtimeUpscalerMaxRenderHeight = 0;
	uint32_t runtimeUpscalerMaxDisplayWidth = 0;
	uint32_t runtimeUpscalerMaxDisplayHeight = 0;
	uint32_t runtimeUpscalerRequestedVersion = 0;
	D3D11_TEXTURE2D_DESC runtimeColorSharedDesc{};
	D3D11_TEXTURE2D_DESC runtimeDepthSharedDesc{};
	D3D11_TEXTURE2D_DESC runtimeMotionSharedDesc{};
	D3D11_TEXTURE2D_DESC runtimeReactiveSharedDesc{};
	D3D11_TEXTURE2D_DESC runtimeTransparencySharedDesc{};
	D3D11_TEXTURE2D_DESC runtimeOutputSharedDesc{};
	ffx::Context runtimeUpscalerContexts[2]{};
	bool runtimeUpscalerContextIndeterminate[2]{};

	winrt::com_ptr<ID3D11Fence> runtimeD3D11Fence;
	winrt::com_ptr<ID3D12Fence> runtimeD3D12Fence;
	ID3D11Query* pendingFSRResourceFreeIdleFence = nullptr;
	uint64_t pendingRuntimeTeardownD3D11FenceValue = 0;
	uint64_t pendingRuntimeTeardownD3D12FenceValue = 0;
	uint64_t runtimeFenceValue = 1;

	static constexpr uint32_t kRuntimeCommandContextCount = 8;
	struct RuntimeCommandContext
	{
		winrt::com_ptr<ID3D12CommandAllocator> commandAllocator;
		winrt::com_ptr<ID3D12GraphicsCommandList4> commandList;
		uint64_t fenceValue = 0;
	};
	std::array<RuntimeCommandContext, kRuntimeCommandContextCount> runtimeCommandContexts;
	uint32_t runtimeCommandContextCursor = 0;

	using RuntimeWrappedResources = std::array<std::unique_ptr<WrappedResource>, 2>;
	RuntimeWrappedResources runtimeColorShared{};
	RuntimeWrappedResources runtimeDepthShared{};
	RuntimeWrappedResources runtimeMotionShared{};
	RuntimeWrappedResources runtimeReactiveShared{};
	RuntimeWrappedResources runtimeTransparencyShared{};
	RuntimeWrappedResources runtimeOutputShared{};

	HMODULE frameGenerationModule = nullptr;
	HMODULE runtimeUpscalerModule = nullptr;

	// Flag to prevent spamming the log with FSR3 dispatch crash messages
	bool fsrDispatchCrashLogged = false;

	bool runtimeUpscalerFailureLatched = false;
	bool runtimeFsr4FailureLatched = false;
	bool runtimeUpscalerSessionQuarantined = false;
	LifecycleResult runtimeUpscalerQuarantineRetirement = LifecycleResult::Ready;
	bool runtimeUpscalerQuarantineFrameValid = false;
	uint32_t runtimeUpscalerQuarantineFrame = 0;
	uint32_t runtimeFallbackResetDispatchesRemaining = 0;
	uint32_t runtimeResumeResetDispatchesRemaining = 0;
	bool runtimeHostFallbackActive = false;
	bool runtimeHostFallbackFrameValid = false;
	uint32_t runtimeHostFallbackFrame = 0;
	bool runtimeHostFallbackForFrame = false;
	bool runtimeUpscalerUsedForFrame = false;
	bool runtimeUpscalerLastFramePathValid = false;
	uint32_t runtimeUpscalerLastFrameIndex = 0;
	RuntimeUpscalerFramePath runtimeUpscalerLastFramePath = RuntimeUpscalerFramePath::kInactive;

	bool runtimeUpscalerSupportCheckKnown = false;
	bool runtimeUpscalerSupportConfirmed = false;
	uint64_t runtimeUpscalerProviderMatchedVersionId = 0;
	std::string runtimeUpscalerProviderMatchedVersionName;

	struct RuntimeDispatchPlan
	{
		bool valid = false;
		bool runtimeFsr4Requested = false;
		bool runtimeRequested = false;
		bool vendorLifecycleMutationDeferred = false;
		bool selected = false;
		uint32_t requestedVersion = 0;
		uint32_t contextCount = 0;
		uint32_t fullRenderWidth = 0;
		uint32_t fullRenderHeight = 0;
		uint32_t fullDisplayWidth = 0;
		uint32_t fullDisplayHeight = 0;
	};

	bool CanUseRuntimeUpscalerPath();
	RuntimeDispatchPlan ResolveRuntimeDispatchPlan();
	void ArmRuntimeHostFallback(uint32_t a_contextCount);
	uint32_t GetPreferredRuntimeUpscalerVersion() const;
	void ResetRuntimeUpscalerTracking(bool a_invalidateProviderCache);
	void LatchRuntimeFsr4Failure();
	void QuarantineRuntimeUpscalerForSession(const char* a_reason);
	RuntimeUpscalerFramePath GetRuntimeUpscalerProviderFramePath(uint32_t a_requestedVersion) const;
	void RecordRuntimeUpscalerFramePath(RuntimeUpscalerFramePath a_path);
#ifdef DEVBENCH_BRIDGE_ENABLED
	void RecordDevBenchSuccessfulDispatch(RuntimeUpscalerFramePath a_path);
	RuntimeUpscalerDispatchSnapshot devBenchSuccessfulDispatch{};
	uint64_t devBenchSuccessfulDispatchSerial = 0;
#endif
	LifecycleResult EnsureRuntimeUpscalerInterop();
	LifecycleResult EnsureRuntimeCommandContexts();
	LifecycleResult AcquireRuntimeCommandContext(RuntimeCommandContext*& a_commandContext, uint32_t a_requiredFreeContexts = 1);
	void ResetRuntimeCommandContexts();
	void ReleaseIdleRuntimeUpscalerInterop();
	bool HasRuntimeUpscalerResources() const;
	bool HasCompleteRuntimeUpscalerSharedResources(uint32_t a_contextCount) const;
	bool AreRuntimeUpscalerContextsCompatible(uint32_t a_fullRenderWidth, uint32_t a_fullRenderHeight, uint32_t a_fullDisplayWidth, uint32_t a_fullDisplayHeight, uint32_t a_contextCount, uint32_t a_requestedVersion) const;
	LifecycleResult PollRuntimeUpscalerTeardownReady(const char* a_reason = nullptr);
	LifecycleResult EnsureRuntimeUpscalerContexts(uint32_t a_fullRenderWidth, uint32_t a_fullRenderHeight, uint32_t a_fullDisplayWidth, uint32_t a_fullDisplayHeight, uint32_t a_contextCount, uint32_t a_requestedVersion);
	LifecycleResult PollRuntimeUpscalerTeardownIdle(const char* a_reason);
	LifecycleResult EnsureRuntimeUpscalerSharedResources(uint32_t a_contextCount, uint32_t a_fullRenderWidth, uint32_t a_fullRenderHeight, uint32_t a_fullDisplayWidth, uint32_t a_fullDisplayHeight,
		const D3D11_TEXTURE2D_DESC& a_colorDesc,
		const D3D11_TEXTURE2D_DESC& a_depthDesc,
		const D3D11_TEXTURE2D_DESC& a_motionDesc,
		const D3D11_TEXTURE2D_DESC& a_reactiveDesc,
		const D3D11_TEXTURE2D_DESC& a_transparencyDesc,
		const D3D11_TEXTURE2D_DESC& a_outputDesc);
	LifecycleResult ExecuteRuntimeUpscalerBatch(const RuntimeDispatchPlan& a_plan, std::span<const UpscaleRegionParameters> a_regions);
	LifecycleResult DispatchRuntimeUpscalerBatch(std::span<const UpscaleRegionParameters> a_regions);
	LifecycleResult DestroyRuntimeUpscalerContexts(bool a_waitForIdle = true);
	LifecycleResult DestroyRuntimeUpscalerResources(bool a_waitForIdle = true);
	LifecycleResult RetireQuarantinedRuntimeUpscalerResources();
};
