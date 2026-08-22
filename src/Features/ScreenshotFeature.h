#pragma once

#include "Feature.h"
#include "Utils/Subrect.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <openvr.h>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <nlohmann/json_fwd.hpp>

class ScreenshotApi;

struct ScreenshotFeature : public Feature
{
	ScreenshotFeature();
	enum class VRCaptureSource : uint8_t
	{
		HMDSubmission,
		HMDEye,
		DesktopMirror,
		FramedEye,
		FramedStereo
	};
	enum class VRFramedView : uint8_t
	{
		Left,
		Right,
		Combined
	};

	virtual ~ScreenshotFeature();
	virtual std::string GetName() override { return "Screenshot"; }
	virtual std::string GetShortName() override { return "Screenshot"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kUtility; }

	virtual bool SupportsVR() override { return true; }
	virtual bool IsInMenu() const override;

	virtual void DrawSettingsHeaderControls() override;
	virtual void DrawSettings() override;
	virtual bool HasEssentialSettings() const override { return true; }
	virtual void DrawEssentialSettings() override { DrawSettings(); }
	virtual void LoadSettings(json& a_json) override;
	virtual void SaveSettings(json& a_json) override;
	virtual void PostPostLoad() override;

	/** Requests one capture from the render thread using an immutable settings snapshot. */
	void RequestCapture();
	/** Executes one versioned screenshot API command. Mutating calls must run on the game thread. */
	nlohmann::json HandleApiRequest(const nlohmann::json& a_request);
	/** Legacy menu delegation, returning the accepted request receipt. */
	nlohmann::json RequestLegacyCapture(std::string_view a_origin = "csx_menu");
	/** Returns whether Community Shaders screenshot capture is enabled at runtime. */
	bool IsRuntimeEnabled() const noexcept { return loaded && enabled.load(std::memory_order_acquire); }
	/** Toggles new captures, cancelling active source acquisition while committed encoder work finishes. */
	void SetEnabled(bool a_enabled);
	/** Returns whether a source capture is awaiting Submit or Present processing. */
	bool HasPendingCapture() const noexcept { return capturePending.load(std::memory_order_acquire); }
	std::size_t GetOutstandingArtifactCount() const;
	std::string GetActiveCaptureRequestId() const;
	/**
	 * Observes one texture from a successful, screenshot-eligible OpenVR Submit.
	 * Called synchronously by the compositor hook while the texture is retained.
	 */
	void ObserveAcceptedVRSubmit(
		uint64_t a_compositorCycleToken,
		vr::EVREye a_eye,
		ID3D11Texture2D* a_texture,
		const vr::VRTextureBounds_t* a_bounds,
		vr::EColorSpace a_colorSpace);
	/** Maintains readback protection and services capture immediately before Present. */
	void OnBeforePresent(IDXGISwapChain* a_swapChain);

	bool applyCropToScreenshot = true;

	// Settings
	std::string screenshotPath = "Screenshots";
	bool sdrUsePng = true;
	bool copyToClipboard = false;
	VRCaptureSource vrCaptureSource = VRCaptureSource::HMDSubmission;
	VRFramedView vrFramedView = VRFramedView::Left;
	vr::EVREye vrFramedDominantEye = vr::Eye_Left;

	struct SequenceDefaults
	{
		uint32_t frameCount = 30;
		uint32_t intervalFrames = 6;
		uint32_t previewFramesPerSecond = 15;
		bool saveSeparateEyes = true;
		bool writePreviewVideo = false;
	};
	SequenceDefaults sequenceDefaults{};

private:
	friend class ScreenshotApi;
	struct StagedPlane
	{
		winrt::com_ptr<ID3D11Texture2D> stagingTexture;
		winrt::com_ptr<ID3D11DeviceContext> immediateContext;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		uint32_t width = 0;
		uint32_t height = 0;
		bool flipHorizontal = false;
		bool flipVertical = false;
		bool tonemapSceneHdr = false;
		vr::EColorSpace colorSpace = vr::ColorSpace_Auto;
	};

	enum class OutputView : uint8_t
	{
		SourceNative,
		LeftEye,
		RightEye,
		SideBySide,
		FramedLeft,
		FramedRight,
		FramedCombined
	};

	struct OutputPlan
	{
		OutputView view = OutputView::SourceNative;
		Util::Subrect::UVRegion cropUV{};
		bool applyCrop = false;
		std::filesystem::path outputPath;
		uint32_t width = 0;
		uint32_t height = 0;
		bool saveAsPng = true;
		bool copyToClipboard = false;
		vr::EVREye dominantEye = vr::Eye_Left;
	};

	struct CaptureOptions
	{
		std::string screenshotPath;
		Util::Subrect::UVRegion cropUV{};
		bool applyCrop = true;
		bool saveAsPng = true;
		bool copyToClipboard = false;
		vr::EVREye framedEye = vr::Eye_Left;
		std::array<std::array<float, 4>, 2> eyeProjectionTangents{};
		std::array<vr::HmdMatrix34_t, 2> eyeToHeadTransforms{};
		std::array<std::vector<vr::HmdVector2_t>, 2> hiddenAreaMeshes{};
		bool stereoProjectionValid = false;
		std::string requestId;
		std::string parentRequestId;
		uint32_t sequenceOrdinal = 0;
		std::filesystem::path explicitOutputPath;
		bool allowDesktopFallback = true;
		std::vector<OutputPlan> outputs;
	};

	struct PendingScreenshot
	{
		std::array<StagedPlane, 2> planes{};
		uint32_t planeCount = 0;
		Util::Subrect::UVRegion cropUV{};
		bool applyCrop = true;
		std::filesystem::path outputPath;
		uint32_t aspectFillWidth = 0;
		uint32_t aspectFillHeight = 0;
		bool combineFramedEyes = false;
		vr::EVREye dominantEye = vr::Eye_Left;
		std::array<std::array<float, 4>, 2> eyeProjectionTangents{};
		std::array<vr::HmdMatrix34_t, 2> eyeToHeadTransforms{};
		std::array<std::vector<vr::HmdVector2_t>, 2> hiddenAreaMeshes{};
		bool stereoProjectionValid = false;
		bool saveAsPng = true;
		bool copyToClipboard = false;
		bool ownsQueueSlot = false;
		std::string requestId;
		std::string parentRequestId;
		uint32_t sequenceOrdinal = 0;
		std::vector<OutputPlan> outputs;
	};

	struct ActiveCapture
	{
		bool pending = false;
		VRCaptureSource source = VRCaptureSource::HMDSubmission;
		CaptureOptions options{};
		uint64_t compositorCycleToken = 0;
		uint8_t eyeMask = 0;
		std::array<StagedPlane, 2> eyes{};
		uint32_t presentsWaited = 0;
		bool ownsQueueSlot = false;
	};

	struct ReadbackContextProtection
	{
		winrt::com_ptr<ID3D11DeviceContext> context;
		bool restoreToUnprotected = false;
	};

	mutable std::mutex screenshotQueueMutex;
	std::condition_variable screenshotQueueCV;
	std::queue<PendingScreenshot> screenshotQueue;
	std::thread screenshotWorker;
	std::mutex screenshotWorkerLifecycleMutex;
	bool screenshotWorkerRunning = false;
	std::size_t outstandingScreenshotCount = 0;
	std::vector<ReadbackContextProtection> readbackContextProtections;
	std::atomic_bool readbackProtectionCleanupPending{ false };
	Util::Subrect::Controller subrect;

	std::atomic_bool enabled{ true };
	std::atomic_bool capturePending{ false };
	mutable std::mutex captureStateMutex;
	ActiveCapture activeCapture;
	std::unique_ptr<ScreenshotApi> screenshotApi;

	// SRV-readable copy used when the capture source's own SRV can't be sampled
	// directly (kFRAMEBUFFER on flat aliases the swap-chain backbuffer).
	winrt::com_ptr<ID3D11Texture2D> previewCacheTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> previewCacheSRV;

	bool QueueScreenshot(PendingScreenshot&& screenshot);
	bool EnsureReadbackContextProtection(ID3D11DeviceContext* a_context);
	void RestoreReadbackContextProtectionIfIdle();
	bool TryReserveScreenshotSlot();
	void ReleaseScreenshotSlot();
	void StopWorkerThread();
	void ScreenshotWorkerLoop();
	void EnsurePreviewCache(ID3D11Texture2D* sourceTexture);
	CaptureOptions SnapshotCaptureOptions() const;
	bool SnapshotStereoGeometry(CaptureOptions& a_options) const;
	bool StageTexturePlane(
		ID3D11Texture2D* a_sourceTexture,
		const vr::VRTextureBounds_t* a_bounds,
		uint32_t a_eyeIndex,
		vr::EColorSpace a_colorSpace,
		bool a_tonemapSceneHdr,
		StagedPlane& a_plane);
	bool QueueDesktopCapture(
		IDXGISwapChain* a_swapChain,
		const CaptureOptions& a_options,
		bool a_ownsQueueSlot);
	void ClearActiveCapture(ActiveCapture& a_capture);
	void FallBackToDesktopCapture(ActiveCapture& a_capture, std::string_view a_reason);
	bool TryStartApiCapture(
		std::string a_requestId,
		const nlohmann::json& a_effectiveDescriptor,
		std::string a_parentRequestId = {},
		uint32_t a_sequenceOrdinal = 0);
	bool CancelApiCapture(std::string_view a_requestId);
	void EnsureScreenshotApi();
	static void ShowInGameNotification(std::string message);
};
