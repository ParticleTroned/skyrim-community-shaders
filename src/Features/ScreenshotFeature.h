#pragma once

#include "Feature.h"
#include "Utils/Subrect.h"
#include "VRAPI/CScaptureapi.h"
#include <array>
#include <atomic>
#include <chrono>
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
	CSPluginAPI::CaptureResult001 RequestScreenshot(CSPluginAPI::CaptureEye001 a_eye);
	CSPluginAPI::CaptureResult001 StartFrameSequence(CSPluginAPI::CaptureEye001 a_eye);
	CSPluginAPI::CaptureResult001 StopFrameSequence();
	CSPluginAPI::CaptureResult001 GetCaptureStatus(CSPluginAPI::CaptureStatus001* a_status) const;
	CSPluginAPI::CaptureResult001 CopySequencePath(
		uint64_t a_sessionId,
		char* a_buffer,
		uint32_t a_bufferBytes,
		uint32_t* a_requiredBytes) const;
	bool IsFrameSequenceCapturing() const;
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
	/** Draws the recording indicator after capture commands have been queued for this frame. */
	void DrawPostCaptureIndicator();

	bool applyCropToScreenshot = true;

	// Settings
	std::string screenshotPath = "Screenshots";
	std::string frameCapturePath = "Frame Captures";
	bool sdrUsePng = true;
	bool frameCaptureUsePng = false;
	uint32_t frameCaptureIntervalFrames = 6;
	bool copyToClipboard = false;
	CSPluginAPI::CaptureEye001 screenshotEye = CSPluginAPI::CaptureEye001::kLeft;
	CSPluginAPI::CaptureEye001 frameCaptureEye = CSPluginAPI::CaptureEye001::kLeft;
	VRCaptureSource vrCaptureSource = VRCaptureSource::HMDSubmission;
	VRFramedView vrFramedView = VRFramedView::Left;
	vr::EVREye vrFramedDominantEye = vr::Eye_Left;

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
		CSPluginAPI::CaptureEye001 eye = CSPluginAPI::CaptureEye001::kLeft;
		uint64_t sequenceSessionId = 0;
		uint64_t sequenceFrameIndex = 0;
		uint64_t sequenceTimestampUs = 0;
		std::array<std::filesystem::path, 2> sequenceOutputPaths{};
		uint32_t sequenceOutputCount = 0;
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
		uint64_t sequenceSessionId = 0;
		uint64_t sequenceFrameIndex = 0;
		uint64_t sequenceTimestampUs = 0;
		std::array<std::filesystem::path, 2> sequenceOutputPaths{};
		uint32_t sequenceOutputCount = 0;
	};

	struct SequenceFrameRecord
	{
		uint64_t index = 0;
		uint64_t timestampUs = 0;
		std::array<std::filesystem::path, 2> paths{};
		uint32_t pathCount = 0;
		bool written = false;
		std::string error;
	};

	struct FrameSequence
	{
		CSPluginAPI::CaptureState001 state = CSPluginAPI::CaptureState001::kIdle;
		CSPluginAPI::CaptureEye001 eye = CSPluginAPI::CaptureEye001::kLeft;
		uint64_t sessionId = 0;
		uint64_t nextFrameIndex = 1;
		uint64_t framesScheduled = 0;
		uint64_t framesWritten = 0;
		uint64_t framesDropped = 0;
		uint64_t inFlight = 0;
		uint64_t gameFramesObserved = 0;
		uint32_t intervalFrames = 6;
		bool stopRequested = false;
		std::filesystem::path directory;
		std::chrono::steady_clock::time_point startedAt{};
		std::string startedUtc;
		std::vector<SequenceFrameRecord> frames;
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
	std::mutex captureStateMutex;
	ActiveCapture activeCapture;
	mutable std::mutex frameSequenceMutex;
	mutable std::mutex frameSequenceManifestMutex;
	FrameSequence frameSequence;

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
	CaptureOptions SnapshotCaptureOptions(CSPluginAPI::CaptureEye001 a_eye) const;
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
	CSPluginAPI::CaptureResult001 RequestCaptureInternal(CaptureOptions a_options, bool a_notifyBusy);
	void ScheduleNextSequenceFrame();
	void CompleteSequenceFrame(
		uint64_t a_sessionId,
		uint64_t a_frameIndex,
		uint64_t a_timestampUs,
		const std::array<std::filesystem::path, 2>& a_paths,
		uint32_t a_pathCount,
		bool a_written,
		std::string a_error = {});
	void FinalizeSequenceIfReady();
	bool WriteSequenceManifest(const FrameSequence& a_sequence, bool a_final) const;
	std::filesystem::path ResolveScreenshotDirectory() const;
	std::filesystem::path ResolveFrameCaptureDirectory() const;
	static void ShowInGameNotification(std::string message);
};
