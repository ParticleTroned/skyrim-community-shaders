#pragma once

#include "../../Buffer.h"
#include "../../State.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <d3d11_4.h>
#include <directx/d3d12.h>
#include <mutex>
#include <string_view>
#include <vector>
#include <winrt/base.h>

#define NV_WINDOWS

#pragma warning(push)
#pragma warning(disable: 4471)
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_matrix_helpers.h>
#include <sl_reflex.h>
#include <sl_version.h>
#pragma warning(pop)

using PFun_slSetTagCompat = sl::Result(const sl::ViewportHandle& viewport,
	const sl::ResourceTag* tags,
	uint32_t numTags,
	sl::CommandBuffer* cmdBuffer);

class Streamline
{
public:
	static constexpr const wchar_t* PluginDir = L"Data\\Shaders\\Upscaling\\Streamline";
	static constexpr const wchar_t* DLSSGPluginDir = L"Data\\Shaders\\Upscaling\\StreamlineDX12";
	static constexpr UINT kNvidiaVendorId = 0x10DE;

	explicit Streamline(
		sl::RenderAPI a_renderAPI = sl::RenderAPI::eD3D11,
		const wchar_t* a_pluginDir = PluginDir) :
		renderAPI(a_renderAPI), pluginDir(a_pluginDir)
	{}

	inline std::string GetShortName() { return "Streamline"; }

	bool enabledAtBoot = false;
	bool initialized = false;
	bool triedInitialization = false;

	bool featureDLSS = false;
	bool featureDLSSG = false;
	bool featureReflex = false;
	bool featurePCL = false;
	bool reflexSupportedOnCurrentAdapter = false;

	sl::ViewportHandle viewport{ 0 };
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
	PFun_slSetTagCompat* slSetTag{};
	PFun_slSetTagForFrame* slSetTagForFrame{};
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
	PFun_slDLSSGGetState* slDLSSGGetState{};
	PFun_slDLSSGSetOptions* slDLSSGSetOptions{};

	// Reflex specific functions
	PFun_slReflexGetState* slReflexGetState{};
	PFun_slReflexSleep* slReflexSleep{};
	PFun_slReflexSetOptions* slReflexSetOptions{};
	PFun_slPCLSetMarker* slPCLSetMarker{};

	uint32_t constantsFrame = UINT32_MAX;
	uint32_t constantsViewport = UINT32_MAX;
	bool constantsResult = false;

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
	DLSSOptionsCache dlssOptionsCache{};

	struct ReflexOptionsCache
	{
		bool valid = false;
		sl::ReflexMode mode = sl::ReflexMode::eOff;
		uint32_t frameLimitUs = 0;
		bool useMarkersToOptimize = false;
	};
	ReflexOptionsCache reflexOptionsCache{};
	uint32_t lastReflexSleepFrame = UINT32_MAX;
	uint32_t dlssgSimulationStartFrame = UINT32_MAX;

	struct DLSSGState
	{
		sl::DLSSGStatus status = sl::DLSSGStatus::eOk;
		uint32_t minimumDimension = 0;
		uint32_t maximumFramesToGenerate = 0;
		uint32_t framesActuallyPresented = 0;
		uint32_t consecutiveMissingMarkerStartFrames = 0;
		uint32_t consecutiveNoGeneratedPresents = 0;
		bool optionsApplied = false;
		bool optionsEnabled = false;
		bool optionsTransitionPending = false;
		bool active = false;
		bool disablePending = false;
		bool tagsCleared = true;
	};
	DLSSGState dlssgState{};
	static constexpr uint32_t kDLSSGDiagnosticFrameThreshold = 120;

	struct DLSSGPresentSync
	{
		winrt::com_ptr<ID3D12Fence> inputsCompletionFence;
		uint64_t inputsCompletionValue = 0;
	};

	enum class DLSSGTagResult
	{
		kTagged,
		kCleared,
		kFailed
	};

	// Helper: Execute DLSS for a single viewport with given resources
	bool EvaluateDLSS(sl::ViewportHandle vp,
		ID3D11Resource* colorIn, ID3D11Resource* colorOut, ID3D11Resource* depth,
		ID3D11Resource* mvec, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask,
		const sl::Extent& extentIn, const sl::Extent& extentOut, uint32_t outputWidth);

	// Cached DLL version info for Streamline plugin directory
	static std::vector<std::pair<std::string, std::string>> dllVersions;
	static std::vector<std::pair<std::string, std::string>> dllVersionsDX12;

	void LoadInterposer();
	/** @brief Shuts down Streamline without clearing ownership on failure. */
	[[nodiscard]] bool Shutdown();
	/** @brief Returns whether DLSS-G still requires an ordered Present boundary. */
	[[nodiscard]] bool RequiresDLSSGPresentBoundary() const;

	void CheckFeatures(IDXGIAdapter* a_adapter);

	void PostDevice();
	/** @brief Binds the native device to this configured Streamline instance. */
	[[nodiscard]] bool SetD3DDevice(void* a_device);
	/** @brief Replaces a native manual-hook interface only when Streamline succeeds. */
	[[nodiscard]] bool UpgradeInterface(void** a_interface, std::string_view a_name);
	/** @brief Obtains the native interface paired with a Streamline proxy. */
	[[nodiscard]] bool GetNativeInterface(
		void* a_proxyInterface,
		void** a_nativeInterface,
		std::string_view a_name);

	bool EnsureFrameToken();
	[[nodiscard]] uint32_t GetLatestFrameTokenFrame() const;
	bool CheckFrameConstants(sl::ViewportHandle p_viewport);

	bool IsRTXAndBelow40Series(const DXGI_ADAPTER_DESC& a_adapterDesc);

	bool SetDLSSOptions(sl::ViewportHandle p_viewport, uint32_t width, uint32_t height, bool colorBuffersHDR);
	void InvalidateDLSSOptionsCache();
	void ResetFrameTracking();

	bool Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors);
	void UpdateReflex();
	[[nodiscard]] bool IsDLSSGReflexReadyForCurrentFrame() const;
	/** @brief Returns whether SimulationStart completed for a frame token. */
	[[nodiscard]] bool IsDLSSGFrameReady(uint32_t a_frame) const;
	[[nodiscard]] bool EmitPCLMarker(
		sl::PCLMarker a_marker,
		std::string_view a_name,
		sl::FrameToken* a_token);
	[[nodiscard]] bool EmitPCLMarkerForFrame(
		sl::PCLMarker a_marker,
		std::string_view a_name,
		uint32_t a_frame);
	/** @brief Applies DLSS-G options on the presenting thread. */
	[[nodiscard]] bool ConfigureDLSSG(
		bool a_enabled,
		uint32_t a_width,
		uint32_t a_height,
		uint32_t a_renderWidth,
		uint32_t a_renderHeight);
	/** @brief Tags one validated frame's immutable D3D12 interpolation inputs. */
	[[nodiscard]] DLSSGTagResult TagDLSSGResources(
		ID3D12GraphicsCommandList* a_commandList,
		ID3D12Resource* a_depth,
		ID3D12Resource* a_motionVectors,
		ID3D12Resource* a_hudLessColor,
		ID3D12Resource* a_uiColorAndAlpha,
		uint32_t a_renderWidth,
		uint32_t a_renderHeight,
		uint32_t a_frame);
	/** @brief Releases all retained DLSS-G input tags for one Present token. */
	[[nodiscard]] DLSSGTagResult ClearDLSSGResourceTags(uint32_t a_frame);
	void RequestDLSSGDisable();
	/** @brief Consumes the provider state produced by the preceding proxy Present. */
	[[nodiscard]] DLSSGPresentSync UpdateDLSSGStateAfterPresent(
		bool a_generationRequested,
		bool a_providerBoundaryCompleted,
		bool a_outputAccepted,
		bool a_nullTagsSubmitted);
	/** @brief Returns and clears a sticky asynchronous DXGI result from DLSS-G. */
	[[nodiscard]] HRESULT ConsumeDLSSGAPIError();
	void ResetDLSSGState();
	[[nodiscard]] bool IsDLSSGInstance() const { return renderAPI == sl::RenderAPI::eD3D12; }

	void DestroyDLSSResources();

private:
	struct FrameTokenSnapshot
	{
		sl::FrameToken* token = nullptr;
		uint32_t frame = UINT32_MAX;

		[[nodiscard]] bool Matches(uint32_t a_frame) const
		{
			return frame == a_frame && token &&
			       static_cast<uint32_t>(*token) == a_frame;
		}
	};
	mutable std::mutex frameTokenMutex;
	std::array<FrameTokenSnapshot, sl::MAX_FRAMES_IN_FLIGHT> frameTokens{};
	std::size_t nextFrameTokenSlot = 0;
	uint32_t latestFrameTokenFrame = UINT32_MAX;
	[[nodiscard]] sl::FrameToken* GetFrameTokenForFrame(uint32_t a_frame) const;
	sl::RenderAPI renderAPI;
	const wchar_t* pluginDir;
	uint32_t pclMarkerFailureLogMask = 0;
	bool loggedDLSSGOptionsFailure = false;
	bool loggedDLSSGResourceValidationFailure = false;
	bool loggedDLSSGTagFailure = false;
	bool loggedDLSSGNullTagFailure = false;
	bool loggedDLSSGStateFailure = false;
	std::atomic<HRESULT> dlssgAPIError = S_OK;
	static void OnDLSSGAPIError(const sl::APIError& a_error);
};
