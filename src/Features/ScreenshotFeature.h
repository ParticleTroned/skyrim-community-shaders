#pragma once

#include "Feature.h"
#include "Utils/Subrect.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

class ScreenshotApi;
namespace MenuUI
{
	struct ActionFeedback;
}

/** Owns desktop acquisition and encoding; the service owns request and sequence state. */
struct ScreenshotFeature : public Feature
{
	ScreenshotFeature();
	~ScreenshotFeature();
	std::string GetName() override { return "Screenshot"; }
	std::string GetDisplayName() override { return T("feature.screenshot.name", "Screenshot"); }
	std::string GetShortName() override { return "Screenshot"; }
	std::string_view GetCategory() const override { return FeatureCategories::kUtility; }
	bool IsInMenu() const override { return true; }
	void DrawSettingsEnabledControl() override;
	void DrawSettings() override;
	bool HasEssentialSettings() const override { return true; }
	void DrawEssentialSettings() override { DrawSettings(); }
	void LoadSettings(json&) override;
	void SaveSettings(json&) override;
	void Reset() override;
	void PostPostLoad() override;

	/** Queue a settings-based still capture for the next accepted game Present. */
	void Capture();
	void RequestUiCapture();
	/** Capture after HDR composition/Present using the source frame from before State::Reset. */
	void ProcessCaptureRequest(uint32_t sourceFrame);
	/** Mutating API requests are dispatched by the versioned service on the game thread. */
	json HandleApiRequest(const json&);
	json RequestApiCapture(std::string_view origin = "csx_menu");
#ifdef DEVBENCH_BRIDGE_ENABLED
	json HandleReferenceCapture(const json&, std::function<void(const json&)> completion);
	void SetBurstDeferral(bool);
#endif
	bool IsRuntimeEnabled() const noexcept { return loaded && enabled.load(std::memory_order_acquire); }
	void SetEnabled(bool);
	std::size_t GetOutstandingCaptureJobCount() const;
	std::string GetActiveCaptureRequestId() const;

	bool applyCropToScreenshot = true;
	std::string screenshotPath = "Screenshots";
	std::string frameCapturePath = "Frame Captures";
	unsigned int hdrPngBitDepth = 11;
	bool sdrUsePng = true;
	bool frameCaptureUsePng = false;
	bool copyToClipboard = false;
	struct SequenceDefaults
	{
		uint32_t frameCount = 30;
		uint32_t intervalFrames = 6;
		uint32_t previewFramesPerSecond = 15;
		bool writePreviewVideo = false;
	};
	SequenceDefaults sequenceDefaults{};
	std::atomic_bool captureRequested{ false };

private:
	friend class ScreenshotApi;
	enum class CaptureStartResult
	{
		Started, SourceBusy, EncoderBackpressure, FeatureDisabled, SourceUnavailable, InvalidDescriptor
	};
	struct CaptureState;
	std::shared_ptr<CaptureState> captureState;
	std::shared_ptr<ScreenshotApi> screenshotApi;
	std::thread screenshotWorker;
	std::jthread deadlineWorker;
	std::atomic_bool enabled{ true };
	Util::Subrect::Controller subrect;
	winrt::com_ptr<ID3D11Texture2D> previewCacheTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> previewCacheSRV;
	std::string uiSequenceRequestId;
	std::chrono::steady_clock::time_point nextUiSequencePoll{};

	json BuildCaptureDescriptor(bool usePng, bool clipboard) const;
	CaptureStartResult TryStartApiCapture(const std::string& requestId, const json& descriptor,
		const std::string& parentRequestId, uint32_t ordinal);
	bool CancelApiCapture(std::string_view);
	void AcquirePendingCapture(uint32_t sourceFrame);
	static void ScreenshotWorkerLoop(std::shared_ptr<CaptureState>, std::shared_ptr<ScreenshotApi>);
	static void DeadlineWorkerLoop(std::stop_token, std::shared_ptr<CaptureState>, std::shared_ptr<ScreenshotApi>);
	void DrawCaptureSource(MenuUI::ActionFeedback&);
	void DrawCaptureOutput(MenuUI::ActionFeedback&);
	void DrawCaptureFolder(bool sequence, MenuUI::ActionFeedback&);
	void DrawCaptureSequence(MenuUI::ActionFeedback&, MenuUI::ActionFeedback&);
	void DrawCaptureFraming();
	void PollUiSequence(MenuUI::ActionFeedback&);
	void EnsurePreviewCache(ID3D11Texture2D*);
	static void ShowInGameNotification(std::string);
};
