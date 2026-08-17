#pragma once

#include "Feature.h"
#include "Features/ScreenshotCaptureSessionPolicy.h"
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

struct ScreenshotFeature : public Feature
{
	enum class VRCaptureSource : uint8_t
	{
		HMDSubmission,
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
	enum class CaptureSessionState : uint8_t
	{
		Idle,
		Capturing,
		Draining,
		Complete,
		Cancelled,
		Failed
	};
	struct CaptureSessionRequest
	{
		std::string label = "capture";
		VRCaptureSource source = VRCaptureSource::HMDSubmission;
		uint32_t frameCount = 30;
		uint32_t frameInterval = 6;
		uint32_t previewFramesPerSecond = 15;
		bool saveCombined = true;
		bool saveSeparateEyes = true;
		bool writePreviewVideo = true;
		std::string outputPath;
	};
	struct CaptureSessionFrameStatus
	{
		uint32_t index = 0;
		uint64_t compositorCycleToken = 0;
		bool finished = false;
		bool succeeded = false;
		std::filesystem::path combinedPath;
		std::array<std::filesystem::path, 2> eyePaths{};
		std::string error;
	};
	struct CaptureSessionStatus
	{
		uint64_t id = 0;
		CaptureSessionState state = CaptureSessionState::Idle;
		CaptureSessionRequest request{};
		std::filesystem::path outputDirectory;
		std::filesystem::path manifestPath;
		std::filesystem::path previewVideoPath;
		bool previewVideoFinished = false;
		bool previewVideoSucceeded = false;
		std::string previewVideoError;
		uint32_t framesQueued = 0;
		uint32_t framesFinished = 0;
		uint32_t framesSaved = 0;
		uint32_t framesFailed = 0;
		uint32_t backpressureDrops = 0;
		uint32_t incompleteStereoDrops = 0;
		bool cancelRequested = false;
		std::string error;
		std::vector<CaptureSessionFrameStatus> frames;
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
	/** Starts one bounded, manifest-backed submitted-eye capture session. */
	bool StartCaptureSession(const CaptureSessionRequest& a_request, std::string& a_error);
	/** Stops accepting new frames; already staged frames finish encoding. */
	bool CancelCaptureSession(std::string_view a_reason = "capture session cancelled");
	/** Returns a thread-safe copy of the latest capture-session state. */
	CaptureSessionStatus GetCaptureSessionStatus() const;
	static const char* GetCaptureSessionStateName(CaptureSessionState a_state);
	static const char* GetCaptureSourceName(VRCaptureSource a_source);
	/** Returns whether Community Shaders screenshot capture is enabled at runtime. */
	bool IsRuntimeEnabled() const noexcept { return loaded && enabled.load(std::memory_order_acquire); }
	/** Toggles new captures, cancelling active source acquisition while committed encoder work finishes. */
	void SetEnabled(bool a_enabled);
	/** Returns whether a source capture is awaiting Submit or Present processing. */
	bool HasPendingCapture() const noexcept { return capturePending.load(std::memory_order_acquire); }
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
	/**
	 * Stages one exact texture region for an explicitly armed developer diagnostic.
	 * The GPU copy is issued immediately; mapping and PNG encoding remain on the
	 * screenshot worker so the render thread never waits for readback.
	 */
	bool QueueDiagnosticTextureCapture(
		ID3D11Resource* a_sourceResource,
		const D3D11_BOX& a_sourceRegion,
		const std::filesystem::path& a_outputPath,
		bool a_tonemapSceneHdr = true,
		bool a_writeStatistics = false);

	bool applyCropToScreenshot = true;

	// Settings
	std::string screenshotPath = "Screenshots";
	bool sdrUsePng = true;
	bool copyToClipboard = false;
	VRCaptureSource vrCaptureSource = VRCaptureSource::HMDSubmission;
	VRFramedView vrFramedView = VRFramedView::Left;
	vr::EVREye vrFramedDominantEye = vr::Eye_Left;
	uint32_t sequenceFrameCount = 30;
	uint32_t sequenceFrameInterval = 6;
	uint32_t sequencePreviewFramesPerSecond = 15;
	bool sequenceSaveSeparateEyes = true;
	bool sequenceWritePreviewVideo = true;

private:
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
		bool writeDiagnosticStatistics = false;
		bool ownsQueueSlot = false;
		uint64_t sessionId = 0;
		uint32_t sessionFrameIndex = 0;
		uint64_t compositorCycleToken = 0;
		bool saveCombined = true;
		bool saveStereoEyesSeparately = false;
		std::array<std::filesystem::path, 2> eyeOutputPaths{};
		bool suppressNotification = false;
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
		uint64_t sessionId = 0;
	};

	struct CaptureSessionRuntime
	{
		CaptureSessionStatus status{};
		ScreenshotCaptureSessionPolicy::CycleGate cycleGate{};
		bool producerFinished = false;
		uint64_t manifestRevision = 0;
	};

	struct ReadbackContextProtection
	{
		winrt::com_ptr<ID3D11DeviceContext> context;
		bool restoreToUnprotected = false;
	};

	std::mutex screenshotQueueMutex;
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
	CaptureSessionRuntime captureSession;
	uint64_t nextCaptureSessionId = 1;
	mutable std::mutex captureManifestMutex;
	mutable std::filesystem::path lastCaptureManifestPath;
	mutable uint64_t lastCaptureManifestRevision = 0;

	// SRV-readable copy used when the capture source's own SRV can't be sampled
	// directly (kFRAMEBUFFER on flat aliases the swap-chain backbuffer).
	winrt::com_ptr<ID3D11Texture2D> previewCacheTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> previewCacheSRV;

	bool QueueScreenshot(PendingScreenshot&& screenshot);
	bool EnsureReadbackContextProtection(ID3D11DeviceContext* a_context);
	void RestoreReadbackContextProtectionIfIdle();
	bool TryReserveScreenshotSlot(std::size_t a_maxOutstanding = 2);
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
		StagedPlane& a_plane,
		const D3D11_BOX* a_exactSourceRegion = nullptr);
	bool QueueDesktopCapture(
		IDXGISwapChain* a_swapChain,
		const CaptureOptions& a_options,
		bool a_ownsQueueSlot);
	void ClearActiveCapture(ActiveCapture& a_capture);
	void CancelCaptureSessionLocked(std::string_view a_reason);
	void FinishCaptureSessionProducerLocked();
	void RecordCaptureSessionFrameResult(
		uint64_t a_sessionId,
		uint32_t a_frameIndex,
		bool a_succeeded,
		std::string a_error);
	void GenerateCaptureSessionPreviewVideo(uint64_t a_sessionId);
	json BuildCaptureSessionManifestLocked();
	bool WriteCaptureSessionManifest(const json& a_manifest, const std::filesystem::path& a_path) const;
	void FallBackToDesktopCapture(ActiveCapture& a_capture, std::string_view a_reason);
	static void ShowInGameNotification(std::string message);
};
