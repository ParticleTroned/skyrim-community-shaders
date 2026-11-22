#pragma once

#include "Feature.h"
#include "Features/VR/RenderDocFrameCapture.h"
#include <Renderdoc/renderdoc_d3d11.h>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

using json = nlohmann::json;

// Forward declarations
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Resource;
struct RENDERDOC_API_1_7_0;

// Structure to hold capture file information for UI display
struct CaptureFileInfo
{
	std::string filename;
	std::string sizeStr;
	uint64_t fileSize;
	std::filesystem::file_time_type lastWriteTime;
	std::filesystem::path fullPath;
	bool deletionFailed = false;
	std::string deletionErrorMessage;
};

class RenderDoc : public Feature
{
public:
	static RenderDoc* GetSingleton();

	// Core RenderDoc functionality
	bool IsAvailable() const { return renderDocApi != nullptr; }
	/** Allows VR upscaling only after vendor passthrough and the creation bridge succeed. */
	bool ShouldBlockUpscaling() const { return (enableRenderDocCapture || renderDocModule) && !upscalingCompatible; }
	bool IsUpscalingCompatible() const { return upscalingCompatible; }
	bool HasVendorPassthrough() const { return vendorPassthrough; }
	bool HasSafeStateRestoration() const { return safeStateRestoration; }
	bool HasVendorOutputCapture() const { return captureExternalWrite != nullptr; }
	bool IsFrameCapturing() const;
	uint64_t GetVendorOutputCount() const { return vendorOutputCount.load(); }
	uint64_t GetVendorOutputFailures() const { return vendorOutputFailures.load(); }
	int32_t GetVendorOutputError() const { return vendorOutputError.load(); }
	/** Records real output at an opaque vendor boundary; evaluation metadata is built only during capture. */
	template <class Describe>
	void CaptureVendorOutput(ID3D11DeviceContext* context, ID3D11Resource* output,
		const char* label, std::span<ID3D11Resource* const> inputs, Describe&& describe)
	{
		if (!renderDocModule)
			return;
		try {
			const auto metadata = IsFrameCapturing() ? describe().dump(-1, ' ', false, json::error_handler_t::replace) : std::string{};
			RecordVendorOutput(context, output, label, inputs, metadata);
		} catch (const std::exception& error) {
			RejectVendorCapture(static_cast<int32_t>(0x80004005u), error.what(), context);
		}
	}
	/** Records the verified startup bridge before the game creates its device. */
	void SetUpscalingCompatible(bool compatible) { upscalingCompatible = compatible; }
	/** Bounds VR captures around complete compositor cycles, including both submit eyes. */
	void BeginVRFrameCapture(uint64_t cycle, bool posesReady);
	void CompleteVRFrameCapture(uint64_t cycle);
	const RenderDocFrameCapture& GetVRCaptureState() const { return vrFrameCapture; }
	void TriggerCapture();
	void TriggerMultiFrameCapture(uint32_t a_frameCount);
	bool HandleCaptureHotkey(uint32_t a_vkKey);
	void SetCaptureFilePathTemplate(const std::string& a_template);
	std::string GetCapturesDirectory() const;
	std::string GetCapturePath(uint32_t a_index);
	uint32_t GetNumCaptures() const;
	uint32_t CalculateCapturesDiskUsage();
	void ClearFrameCaptures();
	void SetPendingCaptureComments(std::string a_comments);
	void ApplyPendingCaptureComments(uint32_t a_index);

	// Feature overrides
	std::string GetName() override { return "RenderDoc"; }
	std::string GetShortName() override { return "RenderDoc"; }
	std::string_view GetCategory() const override { return FeatureCategories::kUtility; }
	bool IsCore() const override { return true; }
	bool IsInMenu() const override { return true; }
	std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { "Capture rendering frames and manage recorded files for inspection in RenderDoc.",
			{ "Capture setup and frame count", "Capture comments and file location", "Capture storage usage and recorded-file management" } };
	}
	bool SupportsVR() override { return true; }
	std::string_view GetShaderDefineName() override { return ""; }
	bool HasShaderDefine(RE::BSShader::Type) override { return false; };

	// Settings & UI
	void DrawSettingsEnabledControl() override;
	bool SupportsPerformanceProfileToggle() const override { return true; }
	bool IsPerformanceCostMeasurementEnabled() const override { return enableRenderDocCapture; }
	void SetPerformanceCostMeasurementEnabled(bool value) override { SetCaptureEnabled(value); }
	SettingsHeaderStatus GetSettingsHeaderStatus() const override;
	void DrawSettings() override;
	void RestoreDefaultSettings() override;
	void LoadSettings(json& o_json) override;
	void SaveSettings(json& o_json) override;

	// Resources
	void SetupResources() override;
	void ClearShaderCache() override;

	// Lifecycle
	void Load() override;

	// Helper methods
	std::filesystem::path GetCapturesPath() const;
	std::filesystem::path GetRenderDocDllPath() const;
	std::string BuildAutomaticCaptureComments(const std::string& userComments) const;
	void ApplyAutomaticCommentsToNewCaptures();
	void ClearFailedDeletions();

	/**
	 * Gets the warning message to display when RenderDoc capture is active
	 * @return Formatted warning message for overlay display
	 */
	std::string GetOverlayWarningMessage() const;

private:
	RenderDocFrameCapture vrFrameCapture;
	std::atomic_bool cacheInvalidated{ false };
	bool loadAttempted = false;
	bool vendorPassthrough = false;
	bool safeStateRestoration = false;
	bool upscalingCompatible = false;
	pRENDERDOC_CaptureD3D11ExternalWrite captureExternalWrite = nullptr;
	pRENDERDOC_FailD3D11ExternalWrite failExternalWrite = nullptr;
	std::atomic_uint64_t vendorOutputCount{ 0 }, vendorOutputFailures{ 0 };
	std::atomic_int32_t vendorOutputError{ 0 };
	void RecordVendorOutput(ID3D11DeviceContext* context, ID3D11Resource* output,
		const char* label, std::span<ID3D11Resource* const> inputs, const std::string& metadata);
	void RejectVendorCapture(int32_t error, const char* detail, ID3D11DeviceContext* context = nullptr);
	void SetCaptureEnabled(bool a_enabled);
	bool TriggerConfiguredCapture(bool a_checkDiskSpace = true);
	uint32_t GetCaptureFrameCount() const;
	void SetCaptureFrameCount(uint32_t a_frameCount);
	uint64_t GetRequiredCaptureSpaceBytes() const;
	bool HasSufficientDiskSpaceForConfiguredCapture(uint64_t* a_requiredSpaceBytes = nullptr) const;

	// Cache management for capture files
	void RefreshCaptureFileCache();
	const std::vector<CaptureFileInfo>& GetCachedCaptureFiles();
	void InvalidateCaptureCache();  // Force cache refresh

public:
	RenderDoc() = default;
	~RenderDoc() = default;

	// Delete copy/move operations
	RenderDoc(const RenderDoc&) = delete;
	RenderDoc& operator=(const RenderDoc&) = delete;
	RenderDoc(RenderDoc&&) = delete;
	RenderDoc& operator=(RenderDoc&&) = delete;

	// RenderDoc library and API
	void* renderDocModule = nullptr;
	RENDERDOC_API_1_7_0* renderDocApi = nullptr;

	// Pending comments to attach to the next capture (applied when a new capture is detected)
	std::string pendingCaptureComments;
	mutable std::mutex pendingCommentsMutex;

	// RenderDoc capture enable setting
	bool enableRenderDocCapture = false;
	uint32_t captureFrameCount = 1;

	// Track the last capture count we've processed for automatic comments
	uint32_t lastCaptureCount = 0;

	// Cache for capture file information to avoid frequent filesystem access
	std::vector<CaptureFileInfo> cachedCaptureFiles;
	std::chrono::steady_clock::time_point cacheLastUpdate;
	bool cacheValid = false;

	// Track files that failed to delete for UI feedback
	std::unordered_map<std::filesystem::path, std::string> failedDeletions;

	static constexpr uint64_t kMinCaptureSpaceBytes = 100ULL * 1024ULL * 1024ULL;   // 100 MB minimum free space floor
	static constexpr uint64_t kObservedPerFrameBytes = 256ULL * 1024ULL * 1024ULL;  // ~256 MB per frame observed for multi-frame captures
	static constexpr uint32_t kCacheRefreshIntervalSeconds = 5;                     // Cache refresh interval
	static constexpr size_t kCommentsBufferSize = 1024;                             // Size of comments input buffer
	static constexpr uint32_t kMinCaptureFrameCount = RenderDocFrameCapture::kMinFrames;
	static constexpr uint32_t kMaxCaptureFrameCount = RenderDocFrameCapture::kMaxFrames;
};
