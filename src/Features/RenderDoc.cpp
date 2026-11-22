#include "Menu/SettingsPage.h"
#include "Menu/SettingsTabRenderer.h"
// RenderDoc feature implementation providing in-application graphics debugging capabilities
#include "Features/RenderDoc.h"

#include "Globals.h"
#include "Utils/DeviceContextState.h"
#include "Utils/FileSystem.h"
#include "Utils/Format.h"
#include "Utils/StringUtils.h"
// Include additional core headers required by the feature implementation
#include "Menu.h"
#include "Plugin.h"
#include "State.h"
#include "Utils/UI.h"
#include <imgui.h>

// Include the real RenderDoc API and Windows headers only in the implementation
#include <Renderdoc/renderdoc_app.h>
#include <algorithm>
#include <chrono>
#include <cwchar>
#include <dxgi.h>
#include <filesystem>
#include <format>
#include <shellapi.h>
#include <thread>
#include <vector>
#include <windows.h>

namespace
{
	bool IsCaptureFile(const std::filesystem::directory_entry& a_entry)
	{
		return a_entry.is_regular_file() && _wcsicmp(a_entry.path().extension().c_str(), L".rdc") == 0;
	}
}

// Global feature instance implementation
RenderDoc* RenderDoc::GetSingleton()
{
	return &globals::features::renderDoc;
}

void RenderDoc::Load()
{
	if (std::exchange(loadAttempted, true))
		return;

	// Injection owns its module; keep it loaded even if API negotiation fails.
	auto module = GetModuleHandleW(L"renderdoc.dll");
	if (!module && enableRenderDocCapture) {
		const auto path = GetRenderDocDllPath();
		if (!path.empty() && std::filesystem::exists(path))
			module = LoadLibraryW(path.c_str());
		if (!module)
			logger::warn("[RenderDoc] Could not load configured capture library");
	}
	renderDocModule = module;
	if (!module)
		return;
	wchar_t loadedPath[MAX_PATH]{};
	if (GetModuleFileNameW(module, loadedPath, static_cast<DWORD>(std::size(loadedPath))) != 0) {
		const auto loadedVersion = Util::GetDllVersion(loadedPath);
		logger::info("[RenderDoc] Loaded {} from {}", loadedVersion ? Util::GetFormattedVersion(*loadedVersion) : "unknown version", Util::WStringToString(loadedPath));
	}

	using StateCompatibilityVersion = uint32_t(__cdecl*)();
	const auto stateVersion = reinterpret_cast<StateCompatibilityVersion>(GetProcAddress(module, "RENDERDOC_GetD3D11StateCompatibilityVersion"));
	safeStateRestoration = stateVersion && stateVersion() >= 2;
	Util::UseManualDeviceContextState(safeStateRestoration);
	const auto externalWriteVersion = reinterpret_cast<pRENDERDOC_GetD3D11ExternalWriteVersion>(GetProcAddress(module, "RENDERDOC_GetD3D11ExternalWriteVersion"));
	if (externalWriteVersion && externalWriteVersion() == 1) {
		captureExternalWrite = reinterpret_cast<pRENDERDOC_CaptureD3D11ExternalWrite>(GetProcAddress(module, "RENDERDOC_CaptureD3D11ExternalWrite"));
		failExternalWrite = reinterpret_cast<pRENDERDOC_FailD3D11ExternalWrite>(GetProcAddress(module, "RENDERDOC_FailD3D11ExternalWrite"));
		if (!failExternalWrite)
			captureExternalWrite = nullptr;
	}
	if (!captureExternalWrite)
		logger::warn("[RenderDoc] Vendor-output capture is unavailable; DLSS/NR replay cannot be qualified");
	if (!safeStateRestoration)
		logger::warn("[RenderDoc] Capture library lacks safe D3D11 state restoration; VR upscaling remains disabled");
	const auto getAPI = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"));
	if (!getAPI || getAPI(eRENDERDOC_API_Version_1_3_0, reinterpret_cast<void**>(&renderDocApi)) != 1 || !renderDocApi) {
		renderDocApi = nullptr;
		logger::warn("[RenderDoc] API 1.3 is required for vendor passthrough; upscaling remains disabled");
		return;
	}
	vendorPassthrough = renderDocApi->SetCaptureOptionU32(eRENDERDOC_Option_AllowUnsupportedVendorExtensions, 0x10DE) == 1;
	if (!vendorPassthrough)
		logger::warn("[RenderDoc] NvAPI passthrough rejected; upscaling remains disabled");
	if (safeStateRestoration) {
		RENDERDOC_API_1_7_0* stereoAPI = nullptr;
		if (getAPI(eRENDERDOC_API_Version_1_4_0, reinterpret_cast<void**>(&stereoAPI)) == 1 && stereoAPI)
			renderDocApi = stereoAPI;
		else {
			safeStateRestoration = false;
			logger::warn("[RenderDoc] API 1.4 is required for stereo capture cancellation");
		}
	}
	lastCaptureCount = renderDocApi->GetNumCaptures();
	// External capture sessions retain their paths, overlay and hotkeys.
	if (!enableRenderDocCapture)
		return;

	// Build a base capture file path template including the Skyrim runtime and version so captures are easy to identify
	try {
		auto capturesDir = GetCapturesPath();
		Util::FileHelpers::EnsureDirectoryExists(capturesDir);

		// Format runtime + game version into filename base
		auto runtimeName = std::string{ magic_enum::enum_name(REL::Module::GetRuntime()) };
		auto gameVersion = Util::GetFormattedVersion(REL::Module::get().version());

		// RenderDoc only checks capture names within the current process.
		SYSTEMTIME sessionStart{};
		GetSystemTime(&sessionStart);
		std::filesystem::path fileBase = capturesDir / std::format(
														   "Skyrim_{}_{}_{:04}{:02}{:02}_{:02}{:02}{:02}_{:03}Z_{}",
														   runtimeName, gameVersion,
														   sessionStart.wYear, sessionStart.wMonth, sessionStart.wDay,
														   sessionStart.wHour, sessionStart.wMinute, sessionStart.wSecond,
														   sessionStart.wMilliseconds, GetCurrentProcessId());
		renderDocApi->SetCaptureFilePathTemplate(Util::PathToUtf8(fileBase).c_str());
	} catch (const std::exception& e) {
		logger::warn("[RenderDoc] Failed to prepare capture directory/template: {}", e.what());
	}

	renderDocApi->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);
	// Menu input owns capture hotkeys so they respect the configured frame count.
	renderDocApi->SetCaptureKeys(nullptr, 0);

	// Initialize capture count tracking
	lastCaptureCount = renderDocApi->GetNumCaptures();

	logger::info("[RenderDoc] Successfully initialized");
}

Feature::SettingsHeaderStatus RenderDoc::GetSettingsHeaderStatus() const
{
	if (enableRenderDocCapture && !IsAvailable())
		return { "Restart required", true, "Save settings and restart to load RenderDoc. If it remains unavailable, check that Data/Renderdoc/renderdoc.dll is installed and loads.", true };
	if (!enableRenderDocCapture && IsAvailable())
		return { "Restart required", true, "RenderDoc remains loaded for this session. Save settings and restart without external injection to unload it.", true };
	if (IsAvailable() && GetVendorOutputFailures() != 0)
		return { "Capture error", true, std::format("Vendor output capture failed (0x{:08X}); affected captures were discarded. Check CSX.log before retrying.", static_cast<uint32_t>(GetVendorOutputError())) };
	if (IsAvailable() && (!HasVendorOutputCapture() || !HasSafeStateRestoration()))
		return { "Limited capture", true, "This RenderDoc DLL lacks required support for vendor upscaling captures. Install the CSX-compatible RenderDoc fork and restart." };
	return IsAvailable() ? SettingsHeaderStatus{} : SettingsHeaderStatus{ "Off", true, "Enable RenderDoc, save settings and restart before capturing. Existing captures remain available below." };
}

void RenderDoc::DrawSettings()
{
	static MenuUI::ActionFeedback feedback;
	const bool captureReady = enableRenderDocCapture && IsAvailable();
	const auto showFeedback = SKSE::stl::scope_exit([&] { feedback.Draw(); });
	{
		SettingsTabRenderer::RenderCaptureShortcuts();
		MenuUI::SectionHeading("Capture Settings", "DLSS/NR outputs are captured at opaque boundaries. CSX passes replay normally; edits do not propagate through vendor inference, and inference internals/timing are unavailable.");
		const auto disabled = Util::DisableGuard(!captureReady);

		// Comments input for next capture
		static char commentsBuffer[kCommentsBufferSize] = { 0 };

		Util::Widgets::InputTextWithHint("Comments", "Additional comments for next capture (optional)", commentsBuffer, sizeof(commentsBuffer));
		Util::AddTooltip("Additional comments will be appended to automatic metadata and embedded in the .rdc file");

		int captureFrameCountUI = static_cast<int>(GetCaptureFrameCount());
		if (Util::Widgets::SliderInt("Capture Frames", &captureFrameCountUI, static_cast<int>(kMinCaptureFrameCount), static_cast<int>(kMaxCaptureFrameCount), "%d", ImGuiSliderFlags_AlwaysClamp)) {
			SetCaptureFrameCount(static_cast<uint32_t>(captureFrameCountUI));
		}
		Util::AddTooltip("Number of consecutive frames to capture. Compatible VR captures include both submit eyes in each frame.");

		if (ImGui::Button("Create Capture")) {
			// Check available disk space before allowing capture
			try {
				if (!HasSufficientDiskSpaceForConfiguredCapture()) {
					ImGui::OpenPopup("Not enough disk space##RenderDoc");
				} else {
					// Set comments if provided
					if (strlen(commentsBuffer) > 0) {
						// Build complete comments with automatic metadata plus user input
						std::string userComments = std::string(commentsBuffer);

						std::string completeComments = BuildAutomaticCaptureComments(userComments);

						SetPendingCaptureComments(completeComments);
						memset(commentsBuffer, 0, sizeof(commentsBuffer));  // Clear the buffer
					}

					// Actual capture logic
					logger::info("[RenderDoc] Manual capture triggered by user");
					feedback = TriggerConfiguredCapture(false) ?
					               MenuUI::ActionFeedback{ "Capture requested." } :
					               MenuUI::ActionFeedback{ "Capture could not start. Check RenderDoc availability and free disk space.", true };
				}
			} catch (const std::exception& e) {
				logger::error("[RenderDoc] Exception during capture logic: {}", e.what());
				feedback = { e.what(), true };
			}
		}

		if (ImGui::BeginPopup("Not enough disk space##RenderDoc")) {
			ImGui::Text("Not enough free disk space to create a capture.");
			ImGui::Text("At least %llu MB of free space is required.", static_cast<unsigned long long>(GetRequiredCaptureSpaceBytes() / (1024 * 1024)));
			if (ImGui::Button("OK")) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	{
		MenuUI::SectionHeading("Storage");
		if (ImGui::Button("Open Capture Directory")) {
			// Open the directory where captures are saved
			try {
				auto capturesDir = GetCapturesDirectory();
				if (!Util::OpenInShell(capturesDir, feedback.message))
					throw std::runtime_error(feedback.message);
				feedback = { "Opened capture directory." };
			} catch (const std::exception& e) {
				logger::error("[RenderDoc] Exception while trying to open captures directory: {}", e.what());
				feedback = { e.what(), true };
			}
		}

		ImGui::TextDisabled("Capture Directory: %s", GetCapturesDirectory().c_str());
		Util::AddTooltip("Right-click to copy the directory path.");

		if (ImGui::BeginPopupContextItem()) {
			if (ImGui::MenuItem("Copy Directory Path")) {
				// Copy the captures directory path to clipboard
				try {
					auto capturesDir = GetCapturesDirectory();
					ImGui::SetClipboardText(capturesDir.c_str());
					logger::info("[RenderDoc] Copied captures directory path to clipboard: {}", capturesDir);
				} catch (const std::exception& e) {
					logger::error("[RenderDoc] Exception while copying directory path: {}", e.what());
					feedback = { e.what(), true };
				}
			}
			ImGui::EndPopup();
		}

		uint32_t diskUsageMB = CalculateCapturesDiskUsage();
		float diskUsageGB = static_cast<float>(diskUsageMB) / 1024.0f;

		// Use color-coded value display for disk usage
		Util::ColorCodedValueConfig diskUsageConfig = Util::ColorCodedValueConfig::HighIsBad(0.1f, 1.0f, 5.0f);
		diskUsageConfig.tooltipText = "Total size of all capture files in the captures directory";

		Util::DrawColorCodedValue("Capture Size", diskUsageGB, std::format("{:.2f} GB", diskUsageGB), diskUsageConfig);

		if (diskUsageMB > 0) {
			ImGui::SameLine();
			if (ImGui::Button("Clear All Captures")) {
				ImGui::OpenPopup("Confirm Clear Captures##RenderDoc");
			}
		}

		if (ImGui::BeginPopup("Confirm Clear Captures##RenderDoc")) {
			ImGui::Text("Are you sure you want to delete all capture files?");
			ImGui::Text("This will permanently remove %u MB of capture data.", diskUsageMB);
			ImGui::Separator();

			if (ImGui::Button("Yes, Delete All")) {
				ClearFrameCaptures();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::Spacing();
		// Get cached capture files (auto-refreshes every 5 seconds)
		const auto& captureFiles = GetCachedCaptureFiles();

		// Refresh button
		if (ImGui::Button("Refresh List")) {
			ClearFailedDeletions();
			RefreshCaptureFileCache();
		}

		ImGui::SameLine();
		ImGui::TextDisabled("(%zu files)", captureFiles.size());

		if (captureFiles.empty()) {
			ImGui::TextDisabled("No capture files found.");
		} else {
			// Display custom table with double-click and hover support
			if (ImGui::BeginTable("##RenderDocCaptures", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate)) {
				// Setup headers
				ImGui::TableSetupColumn("Filename", ImGuiTableColumnFlags_DefaultSort);
				ImGui::TableSetupColumn("Size");
				ImGui::TableSetupColumn("Created", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending);
				ImGui::TableHeadersRow();

				// Create a sorted copy of the capture files for display
				static std::vector<CaptureFileInfo> sortedCaptureFiles;
				static std::chrono::steady_clock::time_point sortedCacheLastUpdate = std::chrono::steady_clock::time_point::min();
				static ImGuiTableSortSpecs* sortSpecs = nullptr;

				// Update sorted copy if cache has been refreshed or sorting specs changed
				bool needsSortUpdate = (cacheLastUpdate != sortedCacheLastUpdate) || (sortedCaptureFiles.size() != cachedCaptureFiles.size());

				// Handle sorting
				if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
					sortSpecs = specs;
					if (specs->SpecsDirty || needsSortUpdate) {
						// Copy the current cache and sort it
						sortedCaptureFiles = cachedCaptureFiles;
						std::sort(sortedCaptureFiles.begin(), sortedCaptureFiles.end(),
							[specs](const CaptureFileInfo& a, const CaptureFileInfo& b) {
								for (int i = 0; i < specs->SpecsCount; ++i) {
									const ImGuiTableColumnSortSpecs* spec = &specs->Specs[i];
									int col = spec->ColumnIndex;
									bool ascending = spec->SortDirection == ImGuiSortDirection_Ascending;

									int cmp = 0;
									switch (col) {
									case 0:  // Filename
										cmp = a.filename.compare(b.filename);
										break;
									case 1:  // Size
										cmp = (a.fileSize < b.fileSize) ? -1 : (a.fileSize > b.fileSize) ? 1 :
									                                                                       0;
										break;
									case 2:  // Created (time)
										cmp = (a.lastWriteTime < b.lastWriteTime) ? -1 : (a.lastWriteTime > b.lastWriteTime) ? 1 :
									                                                                                           0;
										break;
									}

									if (cmp != 0) {
										return ascending ? (cmp < 0) : (cmp > 0);
									}
								}
								return false;
							});

						specs->SpecsDirty = false;
						sortedCacheLastUpdate = cacheLastUpdate;
					}
				} else if (needsSortUpdate) {
					// No sorting specs, just copy the cache (newest first by default)
					sortedCaptureFiles = cachedCaptureFiles;
					sortedCacheLastUpdate = cacheLastUpdate;
				}

				// Display rows from the sorted copy
				for (size_t i = 0; i < sortedCaptureFiles.size(); ++i) {
					const auto& file = sortedCaptureFiles[i];
					ImGui::TableNextRow();

					// Filename column with double-click and hover
					ImGui::TableSetColumnIndex(0);
					bool isSelected = false;

					// Push red color for failed deletions
					if (file.deletionFailed) {
						ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetError());
					}

					if (ImGui::Selectable(file.filename.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
						if (ImGui::IsMouseDoubleClicked(0)) {
							// Double-clicked - open the file
							try {
								if (!Util::OpenInShell(file.fullPath, feedback.message))
									throw std::runtime_error(feedback.message);
								feedback = { std::format("Opened {}.", file.filename) };
								logger::info("[RenderDoc] Opened capture file: {}", file.fullPath.string());
							} catch (const std::exception& e) {
								logger::error("[RenderDoc] Failed to open capture file '{}': {}", file.fullPath.string(), e.what());
								feedback = { e.what(), true };
							}
						}
					}

					// Pop color if we pushed it
					if (file.deletionFailed) {
						ImGui::PopStyleColor();
					}

					// Hover tooltip for filename
					if (ImGui::IsItemHovered()) {
						// Calculate time ago dynamically for tooltip
						std::string currentTimeAgo = Util::FormatTimeAgo(file.lastWriteTime);
						std::string tooltip = std::format("File: {}\nSize: {}\nCreated: {}\nDouble-click to open this capture.",
							file.fullPath.string(), file.sizeStr, currentTimeAgo);

						// Add deletion error message if applicable
						if (file.deletionFailed && !file.deletionErrorMessage.empty()) {
							tooltip += std::format("\n\nDeletion Failed: {}", file.deletionErrorMessage);
						}

						ImGui::SetTooltip("%s", tooltip.c_str());
					}

					// Size column
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(file.sizeStr.c_str());

					// Created column - calculate time ago dynamically
					ImGui::TableSetColumnIndex(2);
					std::string currentTimeAgo = Util::FormatTimeAgo(file.lastWriteTime);
					ImGui::TextUnformatted(currentTimeAgo.c_str());
				}

				ImGui::EndTable();
			}
		}
	}
}

std::string RenderDoc::GetCapturesDirectory() const
{
	auto path = GetCapturesPath();
	Util::FileHelpers::EnsureDirectoryExists(path);
	return path.string();
}

uint32_t RenderDoc::CalculateCapturesDiskUsage()
{
	// Share the file list's refresh interval instead of walking the directory every frame.
	uint64_t totalSize = 0;
	for (const auto& capture : GetCachedCaptureFiles())
		totalSize += capture.fileSize;
	return static_cast<uint32_t>(std::min<uint64_t>(totalSize / (1024 * 1024), UINT32_MAX));
}

void RenderDoc::ClearFrameCaptures()
{
	try {
		auto path = GetCapturesPath();

		// Clear previous failed deletion tracking for a fresh start
		failedDeletions.clear();

		// Keep unrelated files in the capture directory.
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
			if (ec) {
				logger::warn("[RenderDoc] Failed to iterate captures directory '{}': {}", path.string(), ec.message());
				break;
			}

			if (IsCaptureFile(entry)) {
				// Use safe delete with proper error handling and logging
				auto deleteResult = Util::FileHelpers::SafeDelete(entry.path().string(), "capture file");
				if (!deleteResult.success) {
					logger::warn("[RenderDoc] Failed to delete capture file '{}': {}", entry.path().string(), deleteResult.errorMessage);

					// Track this file as failed deletion
					failedDeletions[entry.path()] = deleteResult.errorMessage;
				}
			}
		}

		// Invalidate cache to refresh the UI and show failed deletions
		InvalidateCaptureCache();
	} catch (const std::exception& e) {
		logger::error("[RenderDoc] Exception in ClearFrameCaptures: {}", e.what());
	}
}

std::filesystem::path RenderDoc::GetCapturesPath() const
{
	return Util::PathHelpers::GetCommunityShaderPath() / "Captures";
}

std::filesystem::path RenderDoc::GetRenderDocDllPath() const
{
	// RenderDoc DLL should be in Data/Renderdoc/
	return Util::PathHelpers::GetDataPath() / "Renderdoc" / "renderdoc.dll";
}

void RenderDoc::SetupResources()
{
	// RenderDoc doesn't need any D3D resources
}

void RenderDoc::SaveSettings(json& o_json)
{
	o_json["Enable RenderDoc Capture"] = enableRenderDocCapture;
	o_json["Capture Frame Count"] = GetCaptureFrameCount();
}

void RenderDoc::LoadSettings(json& o_json)
{
	if (o_json.contains("Enable RenderDoc Capture") && o_json["Enable RenderDoc Capture"].is_boolean()) {
		SetCaptureEnabled(o_json["Enable RenderDoc Capture"].get<bool>());
	}
	if (!o_json.contains("Capture Frame Count")) {
		return;
	}

	const auto& frameCountJson = o_json["Capture Frame Count"];
	if (frameCountJson.is_number_unsigned()) {
		const auto frameCount = std::min(frameCountJson.get<uint64_t>(), static_cast<uint64_t>(kMaxCaptureFrameCount));
		SetCaptureFrameCount(static_cast<uint32_t>(frameCount));
	} else if (frameCountJson.is_number_integer()) {
		const auto frameCount = std::clamp(
			frameCountJson.get<int64_t>(),
			static_cast<int64_t>(kMinCaptureFrameCount),
			static_cast<int64_t>(kMaxCaptureFrameCount));
		SetCaptureFrameCount(static_cast<uint32_t>(frameCount));
	}
}

void RenderDoc::SetCaptureEnabled(bool a_enabled)
{
	if (auto* state = globals::state) {
		if (a_enabled) {
			if (!enableRenderDocCapture || !state->frameAnnotations)
				state->useFrameAnnotations = state->frameAnnotations;
			state->frameAnnotations = true;
		} else if (enableRenderDocCapture && state->frameAnnotations) {
			state->frameAnnotations = state->useFrameAnnotations;
		}
	}
	enableRenderDocCapture = a_enabled;
	if (!a_enabled)
		vrFrameCapture.CancelPending();
}

void RenderDoc::DrawSettingsEnabledControl()
{
	bool enabled = enableRenderDocCapture;
	if (Util::Widgets::Checkbox("Enabled", &enabled))
		SetCaptureEnabled(enabled);

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Enable RenderDoc frame capture for providing debug captures to the CSX maintainers.");
		ImGui::Text("Capture enables frame annotations for debugging. Disabling it restores the previous annotation setting.");
	}
}

void RenderDoc::RestoreDefaultSettings()
{
	SetCaptureEnabled(false);
	SetCaptureFrameCount(1);
}

void RenderDoc::ClearShaderCache()
{
	// RenderDoc doesn't have shaders to clear
}

void RenderDoc::RefreshCaptureFileCache()
{
	cachedCaptureFiles.clear();
	cacheValid = false;

	try {
		auto capturesPath = GetCapturesPath();
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(capturesPath, ec)) {
			if (ec || !IsCaptureFile(entry))
				continue;

			CaptureFileInfo info;
			info.fullPath = entry.path();
			info.filename = entry.path().filename().string();
			info.fileSize = entry.file_size(ec);
			if (ec)
				continue;

			info.sizeStr = Util::FormatFileSize(info.fileSize);

			info.lastWriteTime = entry.last_write_time(ec);

			// Check if this file previously failed to delete
			auto failedIt = failedDeletions.find(info.fullPath);
			if (failedIt != failedDeletions.end()) {
				info.deletionFailed = true;
				info.deletionErrorMessage = failedIt->second;
			}

			cachedCaptureFiles.push_back(info);
		}

		// Sort by modification time (newest first)
		std::sort(cachedCaptureFiles.begin(), cachedCaptureFiles.end(),
			[](const CaptureFileInfo& a, const CaptureFileInfo& b) {
				return a.lastWriteTime > b.lastWriteTime;
			});

		cacheLastUpdate = std::chrono::steady_clock::now();
		cacheValid = true;

		// Apply automatic comments to any new captures
		ApplyAutomaticCommentsToNewCaptures();
	} catch (const std::exception& e) {
		logger::warn("[RenderDoc] Failed to refresh capture file cache: {}", e.what());
		cacheValid = false;
	}
}

const std::vector<CaptureFileInfo>& RenderDoc::GetCachedCaptureFiles()
{
	// Check if cache needs refresh (invalidate after 5 seconds or if not valid)
	auto now = std::chrono::steady_clock::now();
	auto cacheAge = now - cacheLastUpdate;
	auto maxAge = std::chrono::seconds(kCacheRefreshIntervalSeconds);

	if (cacheInvalidated.exchange(false) || !cacheValid || cacheAge > maxAge) {
		RefreshCaptureFileCache();
	}

	return cachedCaptureFiles;
}

void RenderDoc::InvalidateCaptureCache()
{
	cacheInvalidated.store(true);
}

bool RenderDoc::IsFrameCapturing() const
{
	return renderDocApi && renderDocApi->IsFrameCapturing() != 0;
}

void RenderDoc::RecordVendorOutput(ID3D11DeviceContext* context, ID3D11Resource* output,
	const char* label, std::span<ID3D11Resource* const> inputs, const std::string& metadata)
{
	if (!captureExternalWrite) {
		if (IsFrameCapturing())
			RejectVendorCapture(static_cast<int32_t>(E_NOINTERFACE), "loaded RenderDoc lacks vendor-output capture");
		return;
	}
	if (inputs.size() > 16) {
		RejectVendorCapture(static_cast<int32_t>(E_INVALIDARG), "vendor input count exceeds the capture contract", context);
		return;
	}
	std::array<void*, 16> inputResources{};
	std::copy(inputs.begin(), inputs.end(), inputResources.begin());
	const auto result = captureExternalWrite(context, output, 0, label, metadata.c_str(),
		static_cast<uint32_t>(inputs.size()), inputResources.data());
	if (result == S_OK)
		++vendorOutputCount;
	else if (FAILED(result) && IsFrameCapturing())
		RejectVendorCapture(result, label);
}

void RenderDoc::RejectVendorCapture(int32_t error, const char* detail, ID3D11DeviceContext* context)
{
	if (!IsFrameCapturing())
		return;
	if (context && failExternalWrite)
		failExternalWrite(context, error);
	vendorOutputError.store(error);
	++vendorOutputFailures;
	logger::error("[RenderDoc] Vendor boundary capture failed: {} (0x{:08X}); replay is invalid", detail, static_cast<uint32_t>(error));
	// External owners finalize their own capture; the fork marks readback failures invalid.
	vrFrameCapture.Fail(renderDocApi);
	InvalidateCaptureCache();
}

void RenderDoc::BeginVRFrameCapture(uint64_t cycle, bool posesReady)
{
	if (IsUpscalingCompatible())
		vrFrameCapture.Begin(renderDocApi, globals::d3d::device, cycle, posesReady);
}

void RenderDoc::CompleteVRFrameCapture(uint64_t cycle)
{
	if (IsUpscalingCompatible() && vrFrameCapture.IsActive()) {
		vrFrameCapture.Complete(renderDocApi, cycle);
		InvalidateCaptureCache();
	}
}

void RenderDoc::TriggerCapture()
{
	TriggerMultiFrameCapture(1);
}

void RenderDoc::TriggerMultiFrameCapture(uint32_t a_frameCount)
{
	if (!renderDocApi) {
		logger::warn("[RenderDoc] Cannot capture without the capture API");
		return;
	}
	const uint32_t frames = std::clamp(a_frameCount, kMinCaptureFrameCount, kMaxCaptureFrameCount);
	if (IsUpscalingCompatible()) {
		vrFrameCapture.Queue(frames);
		logger::info("[RenderDoc] Queued {} stereo capture(s) through both submit eyes", frames);
	} else {
		renderDocApi->TriggerMultiFrameCapture(frames);
	}
	InvalidateCaptureCache();
}

bool RenderDoc::TriggerConfiguredCapture(bool a_checkDiskSpace)
{
	if (!renderDocApi) {
		logger::warn("[RenderDoc] Cannot trigger configured capture - RenderDoc API not available");
		return false;
	}

	if (a_checkDiskSpace) {
		uint64_t requiredSpace = 0;
		if (!HasSufficientDiskSpaceForConfiguredCapture(&requiredSpace)) {
			logger::warn("[RenderDoc] Not enough free disk space to create a capture. At least {} MB required.", requiredSpace / (1024 * 1024));
			return false;
		}
	}

	const uint32_t frameCount = GetCaptureFrameCount();
	if (frameCount > 1) {
		TriggerMultiFrameCapture(frameCount);
	} else {
		TriggerCapture();
	}

	return true;
}

bool RenderDoc::HandleCaptureHotkey(uint32_t a_vkKey)
{
	if (a_vkKey != VK_F12 && a_vkKey != VK_SNAPSHOT) {
		return false;
	}

	constexpr int kKeyPressedMask = 0x8000;
	const bool modifierHeld =
		(GetAsyncKeyState(VK_CONTROL) & kKeyPressedMask) != 0 ||
		(GetAsyncKeyState(VK_SHIFT) & kKeyPressedMask) != 0 ||
		(GetAsyncKeyState(VK_MENU) & kKeyPressedMask) != 0;
	if (modifierHeld) {
		return false;
	}

	if (!enableRenderDocCapture || !IsAvailable()) {
		return false;
	}

	logger::info("[RenderDoc] Capture hotkey triggered");
	TriggerConfiguredCapture();
	return true;
}

uint32_t RenderDoc::GetCaptureFrameCount() const
{
	return std::clamp(captureFrameCount, kMinCaptureFrameCount, kMaxCaptureFrameCount);
}

void RenderDoc::SetCaptureFrameCount(uint32_t a_frameCount)
{
	captureFrameCount = std::clamp(a_frameCount, kMinCaptureFrameCount, kMaxCaptureFrameCount);
}

uint64_t RenderDoc::GetRequiredCaptureSpaceBytes() const
{
	const uint64_t estimated = kObservedPerFrameBytes * GetCaptureFrameCount();
	return std::max(estimated, kMinCaptureSpaceBytes);
}

bool RenderDoc::HasSufficientDiskSpaceForConfiguredCapture(uint64_t* a_requiredSpaceBytes) const
{
	const uint64_t requiredSpace = GetRequiredCaptureSpaceBytes();
	if (a_requiredSpaceBytes) {
		*a_requiredSpaceBytes = requiredSpace;
	}

	try {
		auto capturesDir = GetCapturesDirectory();
		std::error_code ec;
		const uint64_t freeSpace = std::filesystem::space(capturesDir, ec).available;
		if (ec) {
			logger::warn("[RenderDoc] Failed to check available space in '{}': {}", capturesDir, ec.message());
			return false;
		}

		return freeSpace >= requiredSpace;
	} catch (const std::exception& e) {
		logger::error("[RenderDoc] Exception while checking capture disk space: {}", e.what());
		return false;
	}
}

void RenderDoc::SetCaptureFilePathTemplate(const std::string& a_template)
{
	if (!renderDocApi) {
		logger::warn("[RenderDoc] Cannot set capture template - RenderDoc API not available");
		return;
	}

	renderDocApi->SetCaptureFilePathTemplate(a_template.c_str());
	logger::info("[RenderDoc] Set capture file path template to: {}", a_template);
}

std::string RenderDoc::GetCapturePath(uint32_t a_index)
{
	if (!renderDocApi) {
		logger::warn("[RenderDoc] Cannot get capture path - RenderDoc API not available");
		return "";
	}

	uint32_t numCaptures = renderDocApi->GetNumCaptures();
	if (a_index >= numCaptures) {
		logger::warn("[RenderDoc] Capture index {} out of range ({} captures available)", a_index, numCaptures);
		return "";
	}

	// Get the required buffer size first
	uint32_t pathLength = 0;
	uint32_t result = renderDocApi->GetCapture(a_index, nullptr, &pathLength, nullptr);
	if (result == 0 || pathLength == 0) {
		logger::warn("[RenderDoc] Failed to get capture path length for index {}", a_index);
		return "";
	}

	// Allocate buffer and get the path
	std::vector<char> pathBuffer(pathLength + 1, 0);
	result = renderDocApi->GetCapture(a_index, pathBuffer.data(), &pathLength, nullptr);
	if (result == 0) {
		logger::warn("[RenderDoc] Failed to get capture path for index {}", a_index);
		return "";
	}

	return std::string(pathBuffer.data());
}

uint32_t RenderDoc::GetNumCaptures() const
{
	if (!renderDocApi)
		return 0;
	return renderDocApi->GetNumCaptures();
}

void RenderDoc::SetPendingCaptureComments(std::string a_comments)
{
	std::lock_guard<std::mutex> lock(pendingCommentsMutex);
	pendingCaptureComments = std::move(a_comments);
	logger::info("[RenderDoc] Set pending capture comments: {}", pendingCaptureComments);
}

void RenderDoc::ApplyPendingCaptureComments(uint32_t a_index)
{
	if (!renderDocApi) {
		logger::warn("[RenderDoc] Cannot apply capture comments - RenderDoc API not available");
		return;
	}

	std::lock_guard<std::mutex> lock(pendingCommentsMutex);
	if (pendingCaptureComments.empty()) {
		return;
	}

	uint32_t numCaptures = renderDocApi->GetNumCaptures();
	if (a_index >= numCaptures) {
		logger::warn("[RenderDoc] Cannot apply comments to capture {} - index out of range ({} captures available)", a_index, numCaptures);
		return;
	}

	// Get the capture file path
	std::string capturePath = GetCapturePath(a_index);
	if (capturePath.empty()) {
		logger::warn("[RenderDoc] Cannot get capture path for index {}", a_index);
		return;
	}

	// Use RenderDoc's built-in SetCaptureFileComments API to embed comments in the .rdc file
	renderDocApi->SetCaptureFileComments(capturePath.c_str(), pendingCaptureComments.c_str());
	logger::info("[RenderDoc] Applied comments to capture {}: {}", a_index, pendingCaptureComments);

	pendingCaptureComments.clear();
}

std::string RenderDoc::BuildAutomaticCaptureComments(const std::string& userComments) const
{
	std::string comments;

	// Runtime information
	auto runtime = REL::Module::GetRuntime();
	auto runtimeName = std::string{ magic_enum::enum_name(runtime) };
	auto gameVersion = Util::GetFormattedVersion(REL::Module::get().version());
	comments += std::format("Skyrim {} {}\n", runtimeName, gameVersion);

	// Plugin version
	comments += std::format("{}\n", Plugin::BUILD_LABEL);

	// Enabled features
	const auto& features = Feature::GetFeatureList();
	std::vector<std::string> enabledFeatures;
	for (auto* feature : features) {
		if (feature->loaded) {
			std::string featVersion = feature->version.empty() ? "unknown" : feature->version;
			enabledFeatures.push_back(std::format("{} ({})", feature->GetShortName(), featVersion));
		}
	}

	// Sort features alphabetically for easier comparison
	std::sort(enabledFeatures.begin(), enabledFeatures.end());

	if (!enabledFeatures.empty()) {
		comments += "Enabled Features:\n";
		for (const auto& feature : enabledFeatures) {
			comments += std::format("- {}\n", feature);
		}
	}

	if (HasVendorOutputCapture()) {
		comments +=
			"\nDLSS/NR: actual outputs restored at labelled opaque boundaries. "
			"Vendor inference is not re-executed. Shader edits do not propagate "
			"through it; internal accesses and inference timing are unavailable.\n";
	}

	// Add user comments if provided
	if (!userComments.empty()) {
		comments += "\nUser Comments:\n";
		comments += userComments;
	}

	return comments;
}

void RenderDoc::ApplyAutomaticCommentsToNewCaptures()
{
	if (!renderDocApi) {
		return;
	}

	uint32_t numCaptures = renderDocApi->GetNumCaptures();
	if (numCaptures <= lastCaptureCount) {
		return;  // No new captures
	}

	// Build automatic comments for new captures
	std::string automaticComments = BuildAutomaticCaptureComments("");

	// Keep comments and the retry position until a capture path is available.
	std::lock_guard<std::mutex> lock(pendingCommentsMutex);
	for (uint32_t i = lastCaptureCount; i < numCaptures; ++i) {
		std::string capturePath = GetCapturePath(i);
		if (capturePath.empty()) {
			break;
		}

		const std::string& commentsToApply = !pendingCaptureComments.empty() ? pendingCaptureComments : automaticComments;
		renderDocApi->SetCaptureFileComments(capturePath.c_str(), commentsToApply.c_str());
		logger::info("[RenderDoc] Applied comments to capture {}: {}", i, commentsToApply);
		pendingCaptureComments.clear();
		lastCaptureCount = i + 1;
	}
}

std::string RenderDoc::GetOverlayWarningMessage() const
{
	return IsUpscalingCompatible() ?
	           "RenderDoc is active and affects performance. VR upscaling capture is enabled; Frame Generation is disabled." :
	           "RenderDoc is active and affects performance. The compatible VR upscaling path is unavailable; Upscaling and Frame Generation are disabled.";
}

void RenderDoc::ClearFailedDeletions()
{
	failedDeletions.clear();
	logger::info("[RenderDoc] Cleared failed deletion tracking");
}
