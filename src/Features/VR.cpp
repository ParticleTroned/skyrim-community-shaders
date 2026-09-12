#include "VR.h"
#include "Diagnostics/VRPipelineDiagnostics.h"
#include "DynamicCubemaps.h"
#include "FoveatedCommon.h"
#include "GpuPass.h"
#include "LocationContext.h"
#include "Menu.h"
#include "Menu/FeatureListRenderer.h"
#include "Menu/Fonts.h"
#include "Menu/OverlayPolicy.h"
#include "Menu/OverlayRenderer.h"
#include "RE/B/BSOpenVR.h"
#include "RE/B/BSOpenVRControllerDevice.h"
#include "RE/N/NiPoint3.h"
#include "RE/P/PlayerCharacter.h"
#include "ScreenSpaceGI.h"
#include "ScreenSpaceShadows.h"
#include "ShaderCache.h"
#include "SubsurfaceScattering.h"
#include "Upscaling.h"
#include "VR/MenuPositioningPolicy.h"
#include "VRDepthCullingCacheRefreshPolicy.h"
#include "VRDepthCullingEnablePolicy.h"
#include "VRDepthCullingTemporal.h"
#include "WaterEffects.h"
#include "WetnessEffects.h"
#include "Wetterness.h"
#include <openvr.h>

#include "Globals.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/Game.h"
#include "Utils/PerfUtils.h"
#include "Utils/UI.h"
#include "Utils/VRUtils.h"
#include <DirectXMath.h>
#include <SimpleMath.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <imgui_impl_dx11.h>
#include <imgui_internal.h>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <windows.h>

using AttachMode = VR::Settings::OverlayAttachMode;

bool VR::OverlayRenderContext::IsValid() const
{
	return gameOverlay && cleanOverlay && openvr && openvr->vrSystem;
}

namespace
{
	void EmitVRPipelineEnvironmentDiagnosticsOnce(const VR& a_vr)
	{
		static std::mutex startupDiagnosticsMutex;
		static bool settingsLatched = false;
		static bool diagnosticsRequested = false;
		static bool structuredRequested = false;
		static bool textCaptured = false;
		static bool structuredCaptured = false;
		std::scoped_lock lock(startupDiagnosticsMutex);
		if (!settingsLatched) {
			const auto& diagnosticSettings = globals::features::upscaling.settings;
			diagnosticsRequested = diagnosticSettings.pipelineDiagnostics;
			structuredRequested = diagnosticSettings.pipelineDiagnosticsStructured;
			settingsLatched = true;
		}
		if (!diagnosticsRequested || (textCaptured && (!structuredRequested || structuredCaptured)))
			return;

		try {
			const auto& openVRInfo = a_vr.openVRInfo;
			nlohmann::json fields;
			fields["mode"] = REL::Module::IsVR() ? "vr" : "flat";
			fields["runtime"] = VRDetection::RuntimeTypeToString(openVRInfo.runtimeType);
			fields["openVR"] = {
				{ "available", openVRInfo.isAvailable },
				{ "compatible", openVRInfo.isCompatible },
				{ "dllPath", openVRInfo.dllPath },
				{ "version", openVRInfo.version },
				{ "fileSize", openVRInfo.fileSize },
				{ "modified", openVRInfo.modificationTime },
				{ "hasOverlayInterface", openVRInfo.hasOverlayInterface },
				{ "hasSystemInterface", openVRInfo.hasSystemInterface },
				{ "hasCompositorInterface", openVRInfo.hasCompositorInterface },
				{ "probingSucceeded", openVRInfo.probingSucceeded }
			};

			const bool structuredSucceeded = VRPipelineDiagnostics::Emit(
				{ VRPipelineDiagnostics::Source::CS, "ENV", "startup", std::move(fields) },
				structuredRequested && !structuredCaptured,
				std::format(
					"reason=startup mode={} runtime={} openvrAvailable={} compatible={} dll=\"{}\" version={} size={} modified=\"{}\" overlay={} system={} compositor={}",
					REL::Module::IsVR() ? "vr" : "flat",
					VRDetection::RuntimeTypeToString(openVRInfo.runtimeType),
					openVRInfo.isAvailable,
					openVRInfo.isCompatible,
					openVRInfo.dllPath,
					openVRInfo.version,
					openVRInfo.fileSize,
					openVRInfo.modificationTime,
					openVRInfo.hasOverlayInterface,
					openVRInfo.hasSystemInterface,
					openVRInfo.hasCompositorInterface),
				!textCaptured);
			textCaptured = true;
			if (structuredRequested && structuredSucceeded)
				structuredCaptured = true;
		} catch (const std::exception& e) {
			logger::warn("[VRPIPE v1][CS][ERROR] startup diagnostics failed: {}", e.what());
		} catch (...) {
			logger::warn("[VRPIPE v1][CS][ERROR] startup diagnostics failed");
		}
	}

	bool IsWetternessActiveForDynamicCubemapVisibilityThrottle()
	{
		const auto& wetterness = globals::features::wetterness;
		return wetterness.IsRuntimeActive();
	}

	void DisableDynamicCubemapVisibilityThrottleForWetterness(VR::Settings& a_settings)
	{
		if (!a_settings.EnableDynamicCubemapVisibilityThrottle || !IsWetternessActiveForDynamicCubemapVisibilityThrottle()) {
			return;
		}

		logger::info("Disabling Low-Visibility Cubemap Throttle because Wetterness is active.");
		a_settings.EnableDynamicCubemapVisibilityThrottle = false;
	}

	bool IsRenderScaleDesktopMirrorQualityAvailable()
	{
		return globals::game::isVR && globals::features::upscaling.IsVRRenderScaleModeActive();
	}

	bool IsClosedMenuStatusOverlayActive()
	{
		return globals::menu && globals::menu->HasClosedMenuOverlay();
	}

	bool BeginTabItemWithFont(const char* label, Menu::FontRole role, ImGuiTabItemFlags flags = ImGuiTabItemFlags_None)
	{
		return MenuFonts::BeginTabItemWithFont(label, role, flags);
	}

	void ScaleOverlayTransform(vr::HmdMatrix34_t& transform, float width, float height)
	{
		for (int row = 0; row < 3; ++row) {
			transform.m[row][0] *= width;
			transform.m[row][1] *= height;
		}
	}

	ImVec2 GetTabChildSizeWithRestoreButtonReserve()
	{
		const float reserveHeight = FeatureListRenderer::GetRestoreDefaultsButtonReserveHeight();
		ImVec2 size = ImGui::GetContentRegionAvail();
		size.y = size.y > reserveHeight ? size.y - reserveHeight : 1.0f;
		return size;
	}

	HWND GetGameWindowHandle()
	{
		if (!globals::d3d::swapChain) {
			return nullptr;
		}

		DXGI_SWAP_CHAIN_DESC desc{};
		if (FAILED(globals::d3d::swapChain->GetDesc(&desc))) {
			return nullptr;
		}

		return desc.OutputWindow;
	}

	bool IsWindowTopmost(HWND hwnd)
	{
		return hwnd &&
		       IsWindow(hwnd) &&
		       (GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
	}

	bool ShouldRenderShaderCompilationInHMD()
	{
		const auto* menu = globals::menu;
		return menu &&
		       OverlayPolicy::ShouldRouteShaderCompilationStatusToHMD(
				   OverlayRenderer::ShouldShowShaderCompilationStatus(*menu),
				   menu->GetSettings().ShowCompilationHUDInVR);
	}

	bool ShouldShowShaderCompilationInHMD()
	{
		return globals::shaderCache &&
		       globals::shaderCache->IsCompiling() &&
		       ShouldRenderShaderCompilationInHMD();
	}

	bool IsDrawListOwnedByWindow(const ImDrawList* drawList, const char* windowName)
	{
		return drawList && drawList->_OwnerName && std::strcmp(drawList->_OwnerName, windowName) == 0;
	}

	ImDrawData* FilterShaderCompilationWindowFromHMD(ImDrawData* drawData, ImDrawData& filteredDrawData)
	{
		if (!drawData) {
			return drawData;
		}

		const bool hideShaderCompilationWindow = !ShouldRenderShaderCompilationInHMD();
		bool removedAny = false;
		int totalIdxCount = 0;
		int totalVtxCount = 0;

		filteredDrawData = *drawData;
		filteredDrawData.CmdLists.clear();
		filteredDrawData.CmdLists.reserve(drawData->CmdListsCount);
		for (int i = 0; i < drawData->CmdListsCount; ++i) {
			auto* cmdList = drawData->CmdLists[i];
			if (hideShaderCompilationWindow && IsDrawListOwnedByWindow(cmdList, "ShaderCompilationInfo")) {
				removedAny = true;
				continue;
			}
			filteredDrawData.CmdLists.push_back(cmdList);
			totalIdxCount += cmdList->IdxBuffer.Size;
			totalVtxCount += cmdList->VtxBuffer.Size;
		}

		if (!removedAny) {
			return drawData;
		}

		filteredDrawData.CmdListsCount = filteredDrawData.CmdLists.Size;
		filteredDrawData.TotalIdxCount = totalIdxCount;
		filteredDrawData.TotalVtxCount = totalVtxCount;
		return &filteredDrawData;
	}

	float GetCustomVRCursorDotRadius()
	{
		const float toggleHeight = std::max(10.0f, std::round(ImGui::GetTextLineHeight() * 0.80f));
		return std::max(2.0f, std::round(toggleHeight * 0.25f));
	}

	bool ShouldDrawCustomVRCursorDot(bool a_visible, const ImVec2& a_cursorPos, const ImVec2& a_displaySize)
	{
		if (!a_visible ||
			!std::isfinite(a_cursorPos.x) ||
			!std::isfinite(a_cursorPos.y) ||
			a_cursorPos.x < 0.0f ||
			a_cursorPos.y < 0.0f ||
			a_cursorPos.x > a_displaySize.x ||
			a_cursorPos.y > a_displaySize.y) {
			return false;
		}

		return true;
	}

	void AppendCustomVRCursorDot(ImDrawList& drawList, const ImVec2& center)
	{
		const float radius = GetCustomVRCursorDotRadius();
		const ImU32 glowColor = IM_COL32(72, 240, 230, 92);
		const ImU32 outerColor = IM_COL32(44, 222, 236, 220);
		const ImU32 innerColor = IM_COL32(186, 255, 248, 255);

		drawList.AddCircleFilled(center, radius * 1.9f, glowColor, 24);
		drawList.AddCircleFilled(center, radius, outerColor, 20);
		drawList.AddCircleFilled(center, radius * 0.42f, innerColor, 16);
	}

	bool CenterWindowOnCurrentMonitorTopmost(HWND hwnd)
	{
		if (!hwnd || !IsWindow(hwnd)) {
			return false;
		}

		RECT windowRect{};
		if (!GetWindowRect(hwnd, &windowRect)) {
			return false;
		}

		HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO monitorInfo{ sizeof(MONITORINFO) };
		if (!monitor || !GetMonitorInfo(monitor, &monitorInfo)) {
			return false;
		}

		const int windowWidth = windowRect.right - windowRect.left;
		const int windowHeight = windowRect.bottom - windowRect.top;
		const RECT& workArea = monitorInfo.rcWork;
		const int workWidth = workArea.right - workArea.left;
		const int workHeight = workArea.bottom - workArea.top;
		const int centeredX = workArea.left + std::max(0, (workWidth - windowWidth) / 2);
		const int centeredY = workArea.top + std::max(0, (workHeight - windowHeight) / 2);

		return SetWindowPos(
				   hwnd,
				   HWND_TOPMOST,
				   centeredX,
				   centeredY,
				   0,
				   0,
				   SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW) != FALSE;
	}

	void ResetDesktopWindowManagementState(VR& vr)
	{
		vr.desktopWindowManagementApplied = false;
		vr.desktopWindowWasTopmost = false;
		vr.desktopWindowManagedHandle = nullptr;
		vr.lastDesktopWindowManagementAttemptSecs = 0.0;
	}

	constexpr int kVRControllerBindingsVersion = 1;

	void LoadVRControllerBinding(const json& source, const char* keyName, std::vector<ButtonCombo>& target)
	{
		if (!source.is_object() || !source.contains(keyName)) {
			return;
		}

		const auto& bindingJson = source.at(keyName);
		std::vector<ButtonCombo> parsedBindings = target;
		InputCombo::ComboList::from_device_json(bindingJson, parsedBindings, InputDeviceType::Primary);

		const bool explicitlyUnbound =
			(bindingJson.is_number_integer() && bindingJson.get<int64_t>() == 0) ||
			(bindingJson.is_array() && bindingJson.empty());

		if (!parsedBindings.empty() || explicitlyUnbound) {
			target = std::move(parsedBindings);
			return;
		}

		logger::warn("VR: ignoring invalid '{}' controller binding entry; keeping current/default binding", keyName);
	}

	void SaveVRControllerBinding(json& target, const char* keyName, const std::vector<ButtonCombo>& binding)
	{
		InputCombo::ComboList::to_device_json(target[keyName], binding);
	}

	void PopulateMissingVRControllerBindingDefaults(const json& source, VR::Settings& settings)
	{
		const bool hasSourceObject = source.is_object();
		auto missingBinding = [&](const char* keyName) {
			return !hasSourceObject || !source.contains(keyName);
		};

		if (missingBinding("VRMenuOpenKeys")) {
			settings.VRMenuOpenKeys = VR::Settings::DefaultVRMenuOpenKeys();
		}
		if (missingBinding("VRMenuCloseKeys")) {
			settings.VRMenuCloseKeys = VR::Settings::DefaultVRMenuCloseKeys();
		}
		if (missingBinding("VROverlayOpenKeys")) {
			settings.VROverlayOpenKeys = VR::Settings::DefaultVROverlayOpenKeys();
		}
		if (missingBinding("VROverlayCloseKeys")) {
			settings.VROverlayCloseKeys = VR::Settings::DefaultVROverlayCloseKeys();
		}
	}

	void MigrateLegacyBindingDefaults(const json& source, VR::Settings& settings)
	{
		int sourceVersion = 0;
		if (source.is_object()) {
			const auto versionIt = source.find("VRControllerBindingsVersion");
			if (versionIt != source.end() && versionIt->is_number_integer()) {
				sourceVersion = versionIt->get<int>();
			}
		}
		if (sourceVersion >= kVRControllerBindingsVersion) {
			return;
		}

		const std::vector<ButtonCombo> legacyMenuOpen = {
			ButtonCombo::Secondary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kXA)),
			ButtonCombo::Secondary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kBY))
		};
		if (settings.VRMenuOpenKeys == legacyMenuOpen) {
			settings.VRMenuOpenKeys = VR::Settings::DefaultVRMenuOpenKeys();
		}

		const std::vector<ButtonCombo> previousOverlayOpen = {
			ButtonCombo::Primary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger))
		};
		const std::vector<ButtonCombo> previousOverlayClose = {
			ButtonCombo::Secondary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger))
		};
		const std::vector<ButtonCombo> legacyOverlayOpen = {
			ButtonCombo::Secondary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger))
		};
		const std::vector<ButtonCombo> legacyOverlayClose = {
			ButtonCombo::Primary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger))
		};

		if (settings.VROverlayOpenKeys == previousOverlayOpen ||
			settings.VROverlayOpenKeys == legacyOverlayOpen) {
			settings.VROverlayOpenKeys = VR::Settings::DefaultVROverlayOpenKeys();
		}
		if (settings.VROverlayCloseKeys == previousOverlayClose ||
			settings.VROverlayCloseKeys == legacyOverlayClose) {
			settings.VROverlayCloseKeys = VR::Settings::DefaultVROverlayCloseKeys();
		}
	}

}

constexpr int kOverlayWidth = VR::Config::kOverlayWidth;
constexpr int kOverlayHeight = VR::Config::kOverlayHeight;
constexpr int kHMDOverlayWidth = VR::Config::kHMDOverlayWidth;
constexpr int kHMDOverlayHeight = VR::Config::kHMDOverlayHeight;
constexpr const char* kMenuOverlayKey = "communityshaders.menu";
constexpr const char* kMenuOverlayName = "CSX Menu";
constexpr const char* kControllerOverlayKey = "communityshaders.menu.controller";
constexpr const char* kControllerOverlayName = "CSX Menu (Controller)";
constexpr const char* kCaptureIndicatorOverlayKey = "communityshaders.capture.indicator";
constexpr const char* kCaptureIndicatorOverlayName = "CSX Capture Indicator";
constexpr float kLegacyDefaultHMDOffsetZ = -0.41f;
constexpr float kPreviousDefaultHMDOffsetZ = -0.5125f;
constexpr float kCurrentDefaultHMDOffsetZ = -1.025f;
constexpr float kRecentDefaultHMDOffsetZ = -0.76875f;
constexpr float kPreviousDefaultMenuScale = 1.0f;
constexpr float kDefaultOffsetEpsilon = 0.0001f;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	VR::Settings,
	EnableDepthBufferCullingInterior,
	EnableDepthBufferCullingExterior,
	DepthCullingPerformanceMode,
	DepthCullingLegacyMode,
	MinOccludeeBoxExtent,
	UnlockMenuPositionAndSize,
	VRMenuScale,
	VRMenuPositioningMethod,
	attachMode,
	VRMenuAttachController,
	VRMenuOffsetX,
	VRMenuOffsetY,
	VRMenuOffsetZ,
	VRMenuControllerOffsetX,
	VRMenuControllerOffsetY,
	VRMenuControllerOffsetZ,
	mouseDeadzone,
	mouseSpeed,
	dragHighlightColor,
	KeepDesktopWindowFocusedForVRMenu,
	StabilizeRenderScaleDesktopMirror,
	VRMenuOpenKeys,
	VRMenuCloseKeys,
	VROverlayOpenKeys,
	VROverlayCloseKeys,
	comboTimeout,
	EnableDragToReposition,
	kAutoHideSeconds,
	VRMenuAutoResetDistance,
	UseRuntimeDefaultMenuNavigation,
	EnableWandPointing,
	WandAimPitchTrimDegrees,
	EnableStereoBlend,
	StereoBlendDepthSigma,
	StereoBlendMaxFactor,
	StereoBlendColorThreshold,
	EnableLightingFoveation,
	EnableLightingFoveationHardCutoff,
	EnableSSRFoveation,
	EnableSSRFoveationHardCutoff,
	EnableWaterParallaxFoveation,
	EnableWaterParallaxFoveationHardCutoff,
	EnableWetternessFoveation,
	EnableWetternessFoveationHardCutoff,
	EnableDynamicCubemapFoveation,
	EnableDynamicCubemapVisibilityThrottle,
	menuOverlayPath)

//=============================================================================
// FEATURE BASE CLASS OVERRIDES
//=============================================================================

void VR::LoadSettings(json& o_json)
{
	settings = o_json.get<Settings>();
	if (!settings.UnlockMenuPositionAndSize &&
		o_json.is_object() &&
		o_json.contains("VRMenuScale") &&
		std::abs(o_json.value("VRMenuScale", Config::kDefaultMenuScale) - kPreviousDefaultMenuScale) < kDefaultOffsetEpsilon) {
		settings.VRMenuScale = Config::kDefaultMenuScale;
	}
	PopulateMissingVRControllerBindingDefaults(o_json, settings);
	LoadVRControllerBinding(o_json, "VRMenuOpenKeys", settings.VRMenuOpenKeys);
	LoadVRControllerBinding(o_json, "VRMenuCloseKeys", settings.VRMenuCloseKeys);
	LoadVRControllerBinding(o_json, "VROverlayOpenKeys", settings.VROverlayOpenKeys);
	LoadVRControllerBinding(o_json, "VROverlayCloseKeys", settings.VROverlayCloseKeys);
	if (!settings.UnlockMenuPositionAndSize &&
		o_json.is_object() &&
		o_json.contains("VRMenuOffsetZ") &&
		(std::abs(o_json.value("VRMenuOffsetZ", Config::kDefaultHMDOffsetZ) - kLegacyDefaultHMDOffsetZ) < kDefaultOffsetEpsilon ||
			std::abs(o_json.value("VRMenuOffsetZ", Config::kDefaultHMDOffsetZ) - kPreviousDefaultHMDOffsetZ) < kDefaultOffsetEpsilon ||
			std::abs(o_json.value("VRMenuOffsetZ", Config::kDefaultHMDOffsetZ) - kCurrentDefaultHMDOffsetZ) < kDefaultOffsetEpsilon ||
			std::abs(o_json.value("VRMenuOffsetZ", Config::kDefaultHMDOffsetZ) - kRecentDefaultHMDOffsetZ) < kDefaultOffsetEpsilon)) {
		settings.VRMenuOffsetZ = Config::kDefaultHMDOffsetZ;
	}
	if (o_json.is_object() &&
		o_json.contains("EnableWandPointing") &&
		!o_json.contains("UseRuntimeDefaultMenuNavigation") &&
		!o_json.value("EnableWandPointing", true)) {
		settings.UseRuntimeDefaultMenuNavigation = false;
	}
	MigrateLegacyBindingDefaults(o_json, settings);
	// Validate and clamp loaded settings to ensure they're within valid ranges
	settings.ClampToValidRanges();
	DisableDynamicCubemapVisibilityThrottleForWetterness(settings);
	ApplyDepthCullingMode();
}

void VR::SaveSettings(json& o_json)
{
	DisableDynamicCubemapVisibilityThrottleForWetterness(settings);
	o_json = settings;
	SaveVRControllerBinding(o_json, "VRMenuOpenKeys", settings.VRMenuOpenKeys);
	SaveVRControllerBinding(o_json, "VRMenuCloseKeys", settings.VRMenuCloseKeys);
	SaveVRControllerBinding(o_json, "VROverlayOpenKeys", settings.VROverlayOpenKeys);
	SaveVRControllerBinding(o_json, "VROverlayCloseKeys", settings.VROverlayCloseKeys);
	o_json["VRControllerBindingsVersion"] = kVRControllerBindingsVersion;
}

void VR::RestoreDefaultSettings()
{
	ReleaseMenuDesktopWindowManagement();
	settings = Settings{};
	settings.ClampToValidRanges();
	DisableDynamicCubemapVisibilityThrottleForWetterness(settings);
	ApplyDepthCullingMode();
	UpdateDepthBufferCulling();

	if (gMinOccludeeBoxExtent) {
		*gMinOccludeeBoxExtent = settings.MinOccludeeBoxExtent;
	}

	overlayDragState = OverlayDragState{};
	fixedWorldOverlayPosition = OverlayWorldPosition{};
	savedUnlockedFixedWorldOverlayPosition = OverlayWorldPosition{};
	fixedWorldOverlayReanchorRequested = true;
	wandState = WandIntersectionState{};
	autoHideOverlayStartTimeSecs = 0.0;
	primaryControllerState = {};
	secondaryControllerState = {};
	menuOpenCombo = {};
	menuCloseCombo = {};
	savedPlayerWorldPos = {};
	isCapturingCombo = false;
	currentComboType = ComboType::None;
	currentComboName = nullptr;
	recordedCombo.clear();
	comboStartTime = 0.0;
	recordingButtonControllers.clear();
}

bool VR::SupportsPerformanceCostMeasurement() const
{
	return REL::Module::IsVR();
}

bool VR::IsPerformanceCostMeasurementEnabled() const
{
	const auto& screenSpaceShadows = globals::features::screenSpaceShadows;
	const auto& screenSpaceGI = globals::features::screenSpaceGI;
	const bool screenSpaceShadowsFoveatedActive =
		screenSpaceShadows.loaded &&
		screenSpaceShadows.bendSettings.Enable != 0 &&
		screenSpaceShadows.bendSettings.EnableFoveated != 0;
	const bool screenSpaceShadowsStereoSyncActive =
		screenSpaceShadows.loaded &&
		screenSpaceShadows.bendSettings.Enable != 0 &&
		screenSpaceShadows.enableStereoSync;
	const bool screenSpaceGIFoveatedActive =
		screenSpaceGI.loaded &&
		screenSpaceGI.settings.Enabled &&
		screenSpaceGI.settings.EnableFoveated;
	const bool screenSpaceGIStereoSyncActive =
		screenSpaceGI.loaded &&
		screenSpaceGI.settings.Enabled &&
		screenSpaceGI.settings.EnableStereoSync;
	return settings.EnableDepthBufferCullingExterior ||
	       screenSpaceShadowsFoveatedActive ||
	       screenSpaceShadowsStereoSyncActive ||
	       screenSpaceGIFoveatedActive ||
	       screenSpaceGIStereoSyncActive ||
	       settings.EnableStereoBlend ||
	       settings.EnableLightingFoveation ||
	       settings.EnableLightingFoveationHardCutoff ||
	       settings.EnableSSRFoveation ||
	       settings.EnableSSRFoveationHardCutoff ||
	       settings.EnableWaterParallaxFoveation ||
	       settings.EnableWaterParallaxFoveationHardCutoff ||
	       settings.EnableWetternessFoveation ||
	       settings.EnableWetternessFoveationHardCutoff ||
	       settings.EnableDynamicCubemapFoveation ||
	       settings.EnableDynamicCubemapVisibilityThrottle ||
	       (IsRenderScaleDesktopMirrorQualityAvailable() && settings.StabilizeRenderScaleDesktopMirror);
}

void VR::SetPerformanceCostMeasurementEnabled(bool a_enabled)
{
	const Settings defaults{};
	const ScreenSpaceShadows::BendSettings screenSpaceShadowsDefaults{};
	const ScreenSpaceGI::Settings screenSpaceGIDefaults{};
	const bool renderScaleDesktopMirrorAvailable = IsRenderScaleDesktopMirrorQualityAvailable();
	auto& screenSpaceShadows = globals::features::screenSpaceShadows;
	auto& screenSpaceGI = globals::features::screenSpaceGI;
	settings.EnableDepthBufferCullingExterior = a_enabled ? defaults.EnableDepthBufferCullingExterior : false;
	settings.EnableDepthBufferCullingInterior = a_enabled ? defaults.EnableDepthBufferCullingInterior : false;
	settings.DepthCullingPerformanceMode = a_enabled ? defaults.DepthCullingPerformanceMode : true;
	settings.DepthCullingLegacyMode = false;
	ApplyDepthCullingMode();
	screenSpaceShadows.bendSettings.EnableFoveated = a_enabled ? screenSpaceShadowsDefaults.EnableFoveated : 0u;
	screenSpaceShadows.enableStereoSync = false;
	screenSpaceShadows.useStereoReproject = false;
	screenSpaceGI.settings.EnableFoveated = a_enabled ? screenSpaceGIDefaults.EnableFoveated : false;
	screenSpaceGI.settings.EnableStereoSync = a_enabled ? screenSpaceGIDefaults.EnableStereoSync : false;
	screenSpaceGI.settings.UseStereoReproject = a_enabled ? screenSpaceGIDefaults.UseStereoReproject : false;
	settings.EnableStereoBlend = a_enabled ? defaults.EnableStereoBlend : false;
	settings.EnableLightingFoveation = a_enabled ? defaults.EnableLightingFoveation : false;
	settings.EnableLightingFoveationHardCutoff = a_enabled ? defaults.EnableLightingFoveationHardCutoff : false;
	settings.EnableSSRFoveation = a_enabled ? defaults.EnableSSRFoveation : false;
	settings.EnableSSRFoveationHardCutoff = a_enabled ? defaults.EnableSSRFoveationHardCutoff : false;
	settings.EnableWaterParallaxFoveation = a_enabled ? defaults.EnableWaterParallaxFoveation : false;
	settings.EnableWaterParallaxFoveationHardCutoff = a_enabled ? defaults.EnableWaterParallaxFoveationHardCutoff : false;
	settings.EnableWetternessFoveation = a_enabled ? defaults.EnableWetternessFoveation : false;
	settings.EnableWetternessFoveationHardCutoff = a_enabled ? defaults.EnableWetternessFoveationHardCutoff : false;
	settings.EnableDynamicCubemapFoveation = a_enabled ? defaults.EnableDynamicCubemapFoveation : false;
	settings.EnableDynamicCubemapVisibilityThrottle = a_enabled ? defaults.EnableDynamicCubemapVisibilityThrottle : false;
	if (renderScaleDesktopMirrorAvailable)
		settings.StabilizeRenderScaleDesktopMirror = a_enabled ? defaults.StabilizeRenderScaleDesktopMirror : false;
	settings.ClampToValidRanges();
	DisableDynamicCubemapVisibilityThrottleForWetterness(settings);
	UpdateDepthBufferCulling();
}

json VR::CapturePerformanceCostMeasurementState() const
{
	return {
		{ "EnableDepthBufferCullingExterior", settings.EnableDepthBufferCullingExterior },
		{ "EnableDepthBufferCullingInterior", settings.EnableDepthBufferCullingInterior },
		{ "DepthCullingPerformanceMode", settings.DepthCullingPerformanceMode },
		{ "DepthCullingLegacyMode", settings.DepthCullingLegacyMode },
		{ "EnableSSShadowsFoveated", globals::features::screenSpaceShadows.bendSettings.EnableFoveated != 0 },
		{ "EnableSSShadowsStereoSync", globals::features::screenSpaceShadows.enableStereoSync },
		{ "EnableSSShadowsStereoReproject", globals::features::screenSpaceShadows.useStereoReproject },
		{ "EnableSSGIFoveated", globals::features::screenSpaceGI.settings.EnableFoveated },
		{ "EnableSSGIStereoSync", globals::features::screenSpaceGI.settings.EnableStereoSync },
		{ "EnableSSGIStereoReproject", globals::features::screenSpaceGI.settings.UseStereoReproject },
		{ "EnableStereoBlend", settings.EnableStereoBlend },
		{ "EnableLightingFoveation", settings.EnableLightingFoveation },
		{ "EnableLightingFoveationHardCutoff", settings.EnableLightingFoveationHardCutoff },
		{ "EnableSSRFoveation", settings.EnableSSRFoveation },
		{ "EnableSSRFoveationHardCutoff", settings.EnableSSRFoveationHardCutoff },
		{ "EnableWaterParallaxFoveation", settings.EnableWaterParallaxFoveation },
		{ "EnableWaterParallaxFoveationHardCutoff", settings.EnableWaterParallaxFoveationHardCutoff },
		{ "EnableWetternessFoveation", settings.EnableWetternessFoveation },
		{ "EnableWetternessFoveationHardCutoff", settings.EnableWetternessFoveationHardCutoff },
		{ "EnableDynamicCubemapFoveation", settings.EnableDynamicCubemapFoveation },
		{ "EnableDynamicCubemapVisibilityThrottle", settings.EnableDynamicCubemapVisibilityThrottle },
		{ "StabilizeRenderScaleDesktopMirror", settings.StabilizeRenderScaleDesktopMirror }
	};
}

void VR::RestorePerformanceCostMeasurementState(const json& a_state)
{
	if (!a_state.is_object())
		return;

	settings.EnableDepthBufferCullingExterior = a_state.value("EnableDepthBufferCullingExterior", settings.EnableDepthBufferCullingExterior);
	settings.EnableDepthBufferCullingInterior = a_state.value("EnableDepthBufferCullingInterior", settings.EnableDepthBufferCullingInterior);
	settings.DepthCullingPerformanceMode = a_state.value("DepthCullingPerformanceMode", settings.DepthCullingPerformanceMode);
	settings.DepthCullingLegacyMode = a_state.value("DepthCullingLegacyMode", settings.DepthCullingLegacyMode);
	globals::features::screenSpaceShadows.bendSettings.EnableFoveated =
		a_state.value("EnableSSShadowsFoveated", globals::features::screenSpaceShadows.bendSettings.EnableFoveated != 0) ? 1u : 0u;
	globals::features::screenSpaceShadows.enableStereoSync = a_state.value("EnableSSShadowsStereoSync", globals::features::screenSpaceShadows.enableStereoSync);
	globals::features::screenSpaceShadows.useStereoReproject =
		a_state.value("EnableSSShadowsStereoReproject", globals::features::screenSpaceShadows.useStereoReproject);
	globals::features::screenSpaceGI.settings.EnableFoveated =
		a_state.value("EnableSSGIFoveated", globals::features::screenSpaceGI.settings.EnableFoveated);
	globals::features::screenSpaceGI.settings.EnableStereoSync = a_state.value("EnableSSGIStereoSync", globals::features::screenSpaceGI.settings.EnableStereoSync);
	globals::features::screenSpaceGI.settings.UseStereoReproject =
		a_state.value("EnableSSGIStereoReproject", globals::features::screenSpaceGI.settings.UseStereoReproject);
	settings.EnableStereoBlend = a_state.value("EnableStereoBlend", settings.EnableStereoBlend);
	settings.EnableLightingFoveation = a_state.value("EnableLightingFoveation", settings.EnableLightingFoveation);
	settings.EnableLightingFoveationHardCutoff = a_state.value("EnableLightingFoveationHardCutoff", settings.EnableLightingFoveationHardCutoff);
	settings.EnableSSRFoveation = a_state.value("EnableSSRFoveation", settings.EnableSSRFoveation);
	settings.EnableSSRFoveationHardCutoff = a_state.value("EnableSSRFoveationHardCutoff", settings.EnableSSRFoveationHardCutoff);
	settings.EnableWaterParallaxFoveation = a_state.value("EnableWaterParallaxFoveation", settings.EnableWaterParallaxFoveation);
	settings.EnableWaterParallaxFoveationHardCutoff = a_state.value("EnableWaterParallaxFoveationHardCutoff", settings.EnableWaterParallaxFoveationHardCutoff);
	settings.EnableWetternessFoveation = a_state.value("EnableWetternessFoveation", settings.EnableWetternessFoveation);
	settings.EnableWetternessFoveationHardCutoff = a_state.value("EnableWetternessFoveationHardCutoff", settings.EnableWetternessFoveationHardCutoff);
	settings.EnableDynamicCubemapFoveation = a_state.value("EnableDynamicCubemapFoveation", settings.EnableDynamicCubemapFoveation);
	settings.EnableDynamicCubemapVisibilityThrottle = a_state.value("EnableDynamicCubemapVisibilityThrottle", settings.EnableDynamicCubemapVisibilityThrottle);
	settings.StabilizeRenderScaleDesktopMirror = a_state.value("StabilizeRenderScaleDesktopMirror", settings.StabilizeRenderScaleDesktopMirror);
	settings.ClampToValidRanges();
	DisableDynamicCubemapVisibilityThrottleForWetterness(settings);
	ApplyDepthCullingMode();
	UpdateDepthBufferCulling();
}

void VR::SetupResources()
{
	// Detect OpenVR version and compatibility early to avoid CTDs
	DetectOpenVRInfo();

	// Log OpenVR information
	if (openVRInfo.isAvailable) {
		logger::info("OpenVR DLL detected:");
		logger::info("  Path: {}", openVRInfo.dllPath);
		logger::info("  Version: {}", openVRInfo.version);
		logger::info("  Size: {} bytes", openVRInfo.fileSize);
		logger::info("  Modified: {}", openVRInfo.modificationTime);
		logger::info("  Runtime: {}", VRDetection::RuntimeTypeToString(openVRInfo.runtimeType));
		logger::info("  Interfaces: overlay={}, system={}, compositor={}",
			openVRInfo.hasOverlayInterface ? "yes" : "no",
			openVRInfo.hasSystemInterface ? "yes" : "no",
			openVRInfo.hasCompositorInterface ? "yes" : "no");
		logger::info("  Compatible: {}", openVRInfo.isCompatible ? "Yes" : "No");

		if (!openVRInfo.isCompatible) {
			logger::info("Required OpenVR system/compositor interfaces are unavailable.");
			logger::info("CSX VR menus will be disabled for stability");
		}
	} else {
		logger::info("OpenVR DLL not available in current process");
	}

	EmitVRPipelineEnvironmentDiagnosticsOnce(*this);
}

void VR::ClearShaderCache()
{
	stereoBlendCS = nullptr;
}

bool VR::AnyScreenSpaceEffectActive()
{
	const auto& ssgi = globals::features::screenSpaceGI;
	const auto& shadows = globals::features::screenSpaceShadows;
	const auto& dynamicCubemaps = globals::features::dynamicCubemaps;
	const auto& sss = globals::features::subsurfaceScattering;

	bool ssgiActive = false;
	if (ssgi.loaded && ssgi.settings.Enabled) {
		const auto location = LocationContext::Get();
		const bool ssgiAOActive = LocationContext::AllowsInteriorOnly(ssgi.settings.AOInteriorsOnly, location);
		const bool ssgiGIActive = ssgi.IsGIActive() && LocationContext::AllowsInteriorOnly(ssgi.settings.ILInteriorsOnly, location);
		ssgiActive = ssgiAOActive || ssgiGIActive;
	}

	const auto* sky = globals::game::sky;
	const bool shadowsActive = shadows.loaded &&
	                           shadows.bendSettings.Enable != 0 &&
	                           sky &&
	                           sky->mode.get() == RE::Sky::Mode::kFull;

	const bool dynamicSSRActive = dynamicCubemaps.IsSSRRuntimeActive();

	return ssgiActive ||
	       shadowsActive ||
	       dynamicSSRActive ||
	       sss.loaded;
}

bool VR::EnsureStereoBlendResources()
{
	if (!globals::d3d::device || !globals::game::renderer)
		return false;

	if (!stereoBlendCS) {
		std::vector<std::pair<const char*, const char*>> defines = { { "VR", "" }, { "FRAMEBUFFER", "" } };
		auto* shader = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\VR\\StereoBlendCS.hlsl", defines, "cs_5_0"));
		if (!shader)
			return false;
		stereoBlendCS.attach(shader);
	}

	auto renderer = globals::game::renderer;
	auto main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	if (!main.texture || !main.UAV)
		return false;

	D3D11_TEXTURE2D_DESC mainDesc{};
	main.texture->GetDesc(&mainDesc);
	if (mainDesc.ArraySize != 1 || mainDesc.SampleDesc.Count != 1)
		return false;

	const bool copyMatches =
		stereoBlendCopyTex &&
		stereoBlendCopyTex->desc.Width == mainDesc.Width &&
		stereoBlendCopyTex->desc.Height == mainDesc.Height &&
		stereoBlendCopyTex->desc.MipLevels == mainDesc.MipLevels &&
		stereoBlendCopyTex->desc.ArraySize == mainDesc.ArraySize &&
		stereoBlendCopyTex->desc.SampleDesc.Count == mainDesc.SampleDesc.Count &&
		stereoBlendCopyTex->desc.SampleDesc.Quality == mainDesc.SampleDesc.Quality &&
		stereoBlendCopyTex->desc.Format == mainDesc.Format;

	if (!copyMatches) {
		D3D11_TEXTURE2D_DESC copyDesc = mainDesc;
		copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		copyDesc.CPUAccessFlags = 0;
		copyDesc.MiscFlags = 0;
		copyDesc.Usage = D3D11_USAGE_DEFAULT;

		stereoBlendCopyTex = eastl::make_unique<Texture2D>(copyDesc, "VR::StereoBlendCopy");

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = copyDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		stereoBlendCopyTex->CreateSRV(srvDesc);
	}

	if (!stereoBlendCB)
		stereoBlendCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<StereoBlendCB>(), "VR::StereoBlendCB");

	return stereoBlendCS && stereoBlendCopyTex && stereoBlendCopyTex->srv && stereoBlendCB;
}

void VR::DrawStereoBlend()
{
	if (!loaded)
		return;

	if (!REL::Module::IsVR() || !settings.EnableStereoBlend)
		return;

	if (settings.StereoBlendMaxFactor <= Config::kMinStereoBlendMaxFactor)
		return;

	if (!AnyScreenSpaceEffectActive())
		return;

	if (!EnsureStereoBlendResources())
		return;

	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;
	if (!context || !renderer)
		return;

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	auto* depthSRV = Util::GetCurrentSceneDepthSRV();
	if (!main.texture || !main.UAV || !depthSRV)
		return;

	const bool submitStageSceneDomain = globals::features::upscaling.loaded && globals::features::upscaling.IsSubmitStageUpscalingActive();
	float2 resolution = Util::ConvertToDynamic(globals::state->screenSize, submitStageSceneDomain);
	if (resolution.x <= 0.0f || resolution.y <= 0.0f)
		return;

	ZoneScoped;
	CS_GPU_PASS("VR::StereoBlend");

	// Deferred composite leaves kMAIN bound as a UAV. Unbind before copying it as the source texture.
	ID3D11UnorderedAccessView* nullUavs[3]{ nullptr, nullptr, nullptr };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUavs), nullUavs, nullptr);

	context->CopyResource(stereoBlendCopyTex->resource.get(), main.texture);

	StereoBlendCB cbData{};
	cbData.FrameDim[0] = resolution.x;
	cbData.FrameDim[1] = resolution.y;
	cbData.RcpFrameDim[0] = 1.0f / resolution.x;
	cbData.RcpFrameDim[1] = 1.0f / resolution.y;
	cbData.DepthSigma = settings.StereoBlendDepthSigma;
	cbData.MaxBlendFactor = settings.StereoBlendMaxFactor;
	cbData.ColorDiffThreshold = settings.StereoBlendColorThreshold;
	stereoBlendCB->Update(cbData);

	Util::BindGlobalConstantBuffersForCS(context);

	auto dispatchCount = Util::GetScreenDispatchCount(true, submitStageSceneDomain);
	auto* cbPtr = stereoBlendCB->CB();
	ID3D11ShaderResourceView* srvs[2]{ stereoBlendCopyTex->srv.get(), depthSRV };
	ID3D11UnorderedAccessView* uavs[1]{ main.UAV };

	context->CSSetConstantBuffers(1, 1, &cbPtr);
	context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
	context->CSSetShader(stereoBlendCS.get(), nullptr, 0);

	{
		CS_GPU_PASS("StereoBlend::Bilateral");
		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
	}

	ID3D11ShaderResourceView* nullSrvs[2]{ nullptr, nullptr };
	uavs[0] = nullptr;
	cbPtr = nullptr;
	context->CSSetShaderResources(0, ARRAYSIZE(nullSrvs), nullSrvs);
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
	context->CSSetConstantBuffers(1, 1, &cbPtr);
	context->CSSetShader(nullptr, nullptr, 0);
}

void VR::PostPostLoad()
{
	gDepthBufferCulling = reinterpret_cast<bool*>(REL::Offset(0x1EC6B88).address());
	if (!gDepthBufferCulling) {
		static bool s_defaultDepthBufferCulling = false;  // safe fallback
		gDepthBufferCulling = &s_defaultDepthBufferCulling;
		logger::warn("VR: gDepthBufferCulling address not found - using fallback default (false)");
	}

	gMinOccludeeBoxExtent = reinterpret_cast<float*>(REL::Offset(0x1ED64E8).address());
	if (!gMinOccludeeBoxExtent) {
		static float s_defaultMinOccludeeBoxExtent = 10.0f;
		gMinOccludeeBoxExtent = &s_defaultMinOccludeeBoxExtent;
		logger::warn("VR: gMinOccludeeBoxExtent address not found - using fallback default (10.0)");
	}

	// Patches BSGeometry::CopyTransformAndBounds to copy the model-bound translation across correctly instead of overwriting it with the bounding sphere centre
	REL::safe_write(REL::RelocationID(0, 0, 69528).address() + REL::Relocate(0, 0, 0xD9) + 0x2, 0x148);
	REL::safe_write(REL::RelocationID(0, 0, 69528).address() + REL::Relocate(0, 0, 0xE5) + 0x2, 0x14C);
	REL::safe_write(REL::RelocationID(0, 0, 69528).address() + REL::Relocate(0, 0, 0xF1) + 0x2, 0x150);

	ApplyDepthCullingMode();
	VRDepthCullingTemporal::Install();
}

void VR::DataLoaded()
{
	// Initialize occlusion culling based on user settings and current interior/exterior state.
	UpdateDepthBufferCulling();
	TryApplyDepthBufferCullingCacheRefresh();

	if (gMinOccludeeBoxExtent) {
		*gMinOccludeeBoxExtent = settings.MinOccludeeBoxExtent;
	} else {
		logger::warn("VR::DataLoaded: gMinOccludeeBoxExtent is null, skipping assignment");
	}
}

void VR::EarlyPrepass()
{
	// Apply culling setting each prepass based on current interior/exterior state.
	UpdateDepthBufferCulling();
	TryApplyDepthBufferCullingCacheRefresh();
}

//=============================================================================
// OVERLAY FEATURE OVERRIDES
//=============================================================================

bool VR::ShouldShowAutoHideOverlay() const
{
	const bool eligible = settings.kAutoHideSeconds > 0 &&
	                      globals::state &&
	                      globals::state->isMainMenuOpen &&
	                      globals::menu &&
	                      !globals::menu->IsEnabled;
	if (!eligible) {
		return false;
	}

	if (autoHideOverlayStartTimeSecs <= 0.0) {
		if (GetEffectiveMenuAttachMode() != AttachMode::None) {
			return true;
		}
		autoHideOverlayStartTimeSecs = Util::GetNowSecs();
	}

	return Util::GetNowSecs() - autoHideOverlayStartTimeSecs < static_cast<double>(settings.kAutoHideSeconds);
}

void VR::MarkAutoHideOverlayPresented()
{
	if (autoHideOverlayStartTimeSecs > 0.0 ||
		settings.kAutoHideSeconds <= 0 ||
		!globals::state ||
		!globals::state->isMainMenuOpen ||
		!globals::menu ||
		globals::menu->IsEnabled) {
		return;
	}

	autoHideOverlayStartTimeSecs = Util::GetNowSecs();
}

bool VR::ShouldPresentOverlayInHeadset() const
{
	return globals::menu &&
	       (globals::menu->IsEnabled ||
			   IsClosedMenuStatusOverlayActive() ||
			   globals::menu->overlayVisible ||
			   ShouldShowAutoHideOverlay() ||
			   ShouldShowShaderCompilationInHMD());
}

bool VR::ShouldUseInSceneOverlay() const
{
	if (!openVRInfo.isCompatible) {
		return false;
	}

	switch (settings.menuOverlayPath) {
	case Settings::MenuOverlayPath::IVROverlay:
		return false;
	case Settings::MenuOverlayPath::InScene:
		return true;
	case Settings::MenuOverlayPath::Auto:
	default:
		// At the Skyrim main menu the render-scale controller can legitimately
		// remain pending until a world safe point exists. Presenting through the
		// eye-submit copy is deterministic there and does not depend on an
		// IVROverlay transform anchored to a game world that does not yet exist.
		return (globals::state && globals::state->isMainMenuOpen) ||
		       openVRInfo.runtimeType == VRDetection::RuntimeType::OpenComposite ||
		       !openVRInfo.hasOverlayInterface;
	}
}

bool VR::CanOpenMenuFromWorld() const
{
	return openVRInfo.isCompatible &&
	       !ShouldUseInSceneOverlay() &&
	       openVRInfo.hasOverlayInterface &&
	       GetEffectiveMenuAttachMode() != AttachMode::None;
}

bool VR::IsOverlayVisible() const
{
	return openVRInfo.isCompatible && ShouldShowAutoHideOverlay();
}

void VR::DrawOverlay()
{
	if (!openVRInfo.isCompatible)
		return;

	bool shouldShow = ShouldShowAutoHideOverlay();

	if (!shouldShow) {
		return;
	}

	double elapsed = 0.0;
	if (autoHideOverlayStartTimeSecs > 0.0) {
		elapsed = std::max(0.0, Util::GetNowSecs() - autoHideOverlayStartTimeSecs);
	}
	const double autoHideSeconds = static_cast<double>(settings.kAutoHideSeconds);
	const int secondsLeft = std::max(1, static_cast<int>(std::ceil(autoHideSeconds - elapsed)));

	ImGuiIO& io = ImGui::GetIO();
	const float scale = Util::GetUIScale();
	ImVec2 overlaySize(480.0f * scale, 0);  // width, height auto
	ImVec2 overlayPos = ImVec2((io.DisplaySize.x - overlaySize.x) * 0.5f, 80.0f * scale);
	ImGui::SetNextWindowPos(overlayPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(overlaySize, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.92f);

	ImGui::Begin("HowToUseOverlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
	ImGui::TextUnformatted("How to Use VR CSX:");
	ImGui::Separator();
	if (CanOpenMenuFromWorld()) {
		ImGui::TextWrapped("With the current SteamVR overlay path, CSX settings bindings work during gameplay and menus.");
	} else {
		ImGui::TextWrapped("Open the Skyrim Main Menu or Tween Menu before using the CSX settings bindings.");
	}
	ImGui::Spacing();
	ImGui::TextUnformatted("Open CSX Settings:");
	ImGui::SameLine();
	Util::DrawButtonCombo(settings.VRMenuOpenKeys, true);
	ImGui::TextUnformatted("Close CSX Settings:");
	ImGui::SameLine();
	Util::DrawButtonCombo(settings.VRMenuCloseKeys, true);
	ImGui::Spacing();
	ImGui::TextWrapped("Performance Overlay bindings work during gameplay and menus.");
	ImGui::TextUnformatted("Show Performance Overlay:");
	ImGui::SameLine();
	Util::DrawButtonCombo(settings.VROverlayOpenKeys, true);
	ImGui::TextUnformatted("Hide Performance Overlay:");
	ImGui::SameLine();
	Util::DrawButtonCombo(settings.VROverlayCloseKeys, true);
	ImGui::Spacing();
	ImGui::TextDisabled("(This message will auto-hide in %d seconds)", secondsLeft);
	ImGui::TextDisabled("(Configure bindings and this message under VR settings)");
	ImGui::End();
}

namespace
{
	void DrawControllerInputInstructions();
	void DrawGeneralVRSettings();
	void DrawMenuSettings();
	void DrawMouseSettings();
	void DrawStereoSettings();
	void DrawStereoSyncSettings();
	void DrawStereoBlendSettings();
	void DrawFoveationSettings();
	void DrawVRFpsStabilizerSettings();
	void DrawKeyBindings();
	void DrawDebugSection();
}

void VR::DrawSettings()
{
	auto menu = globals::menu;
	if (!menu)
		return;
	if (ImGui::BeginTabBar("##VRTabs", ImGuiTabBarFlags_None)) {
		// General Settings Tab
		if (BeginTabItemWithFont("General", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##VRGeneralFrame", GetTabChildSizeWithRestoreButtonReserve(), true)) {
				DrawGeneralVRSettings();
				DrawControllerInputInstructions();
				DrawMenuSettings();
				DrawMouseSettings();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (BeginTabItemWithFont("FOV", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##VRFoveatedFrame", GetTabChildSizeWithRestoreButtonReserve(), true)) {
				DrawFoveationSettings();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (BeginTabItemWithFont("VR Stabilizer", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##VRFpsStabilizerFrame", GetTabChildSizeWithRestoreButtonReserve(), true)) {
				DrawVRFpsStabilizerSettings();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (BeginTabItemWithFont("Stereo", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##VRStereoFrame", GetTabChildSizeWithRestoreButtonReserve(), true)) {
				DrawStereoSettings();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		// Key Bindings Tab
		if (openVRInfo.isCompatible) {
			if (BeginTabItemWithFont("Bindings", Menu::FontRole::Subheading)) {
				if (ImGui::BeginChild("##VRBindingsFrame", GetTabChildSizeWithRestoreButtonReserve(), true)) {
					DrawKeyBindings();
				}
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
		}

		// Debug Tab (existing debug functionality)
		if (BeginTabItemWithFont("Debug", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##VRDebugFrame", GetTabChildSizeWithRestoreButtonReserve(), true)) {
				DrawDebugSection();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	// Combo recording popup
	if (this->isCapturingCombo) {
		ImGui::OpenPopup("Record Combo");
		if (auto popup = Util::CenteredPopupModal("Record Combo")) {
			auto applyRecordedCombo = [&]() {
				if (this->recordedCombo.empty())
					return;

				switch (this->currentComboType) {
				case VR::ComboType::MenuOpen:
					settings.VRMenuOpenKeys = this->recordedCombo;
					break;
				case VR::ComboType::MenuClose:
					settings.VRMenuCloseKeys = this->recordedCombo;
					break;
				case VR::ComboType::OverlayOpen:
					settings.VROverlayOpenKeys = this->recordedCombo;
					break;
				case VR::ComboType::OverlayClose:
					settings.VROverlayCloseKeys = this->recordedCombo;
					break;
				default:
					return;
				}
				globals::menu->RequestSettingsDirtyCheck();
			};

			// Helper function to get button name
			auto GetButtonName = [](uint32_t key) -> const char* {
				switch (key) {
				case static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kTrigger):
					return "Trigger";
				case static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kGrip):
					return "Grip";
				case static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kTouchpadClick):
					return "Touchpad";
				case static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger):
					return "Stick Click";
				case static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kXA):
					return "A/X";
				case static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kBY):
					return "B/Y";
				default:
					return "Unknown";
				}
			};

			ImGui::Text("Recording combo for: %s", this->currentComboName ? this->currentComboName : "Unknown");
			ImGui::Spacing();

			ImGui::TextDisabled("(During recording, any controller's buttons can be used. Requirement is only enforced during use.)");

			ImGui::Spacing();

			// Show countdown timer with color
			double remainingTime = this->comboTimeout - (Util::GetNowSecs() - this->comboStartTime);
			ImVec4 timerColor = remainingTime > 2.0 ? Util::Colors::GetTimerGood() :
			                    remainingTime > 1.0 ? Util::Colors::GetTimerWarning() :
			                                          Util::Colors::GetTimerCritical();
			ImGui::TextColored(timerColor, "Time remaining: %.1f seconds", remainingTime);

			ImGui::Spacing();

			// Show recorded buttons
			if (this->recordedCombo.empty()) {
				ImGui::Text("Press buttons to record combo...");
			} else {
				ImGui::Text("Recorded buttons:");
				// Create a sorted list of decoded buttons for consistent display
				std::vector<ButtonCombo> sortedRecordedCombos;
				for (size_t i = 0; i < this->recordedCombo.size(); ++i) {
					sortedRecordedCombos.push_back(this->recordedCombo[i]);
				}
				std::sort(sortedRecordedCombos.begin(), sortedRecordedCombos.end(),
					[](const ButtonCombo& a, const ButtonCombo& b) {
						return a.GetKey() < b.GetKey();
					});

				Util::DrawButtonCombo(sortedRecordedCombos, false);
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			// Instructions
			ImGui::Text("Press ENTER to accept, ESC to cancel");

			// Handle button recording
			// Check for VR controller button presses - record them (any controller allowed during recording)
			bool buttonPressed = false;
			uint32_t pressedKey = 0;
			ControllerDevice pressedDevice = ControllerDevice::Both;  // Default to Both, will set below

			// Check primary controller buttons
			for (const auto& [keyCode, buttonState] : primaryControllerState.GetActiveButtons()) {
				if (buttonState->isPressed) {
					pressedKey = keyCode;
					buttonPressed = true;
					pressedDevice = ControllerDevice::Primary;
					break;
				}
			}

			// Check secondary controller buttons if primary didn't have any
			if (!buttonPressed) {
				for (const auto& [keyCode, buttonState] : secondaryControllerState.GetActiveButtons()) {
					if (buttonState->isPressed) {
						pressedKey = keyCode;
						buttonPressed = true;
						pressedDevice = ControllerDevice::Secondary;
						break;
					}
				}
			}

			// Record button press
			if (buttonPressed) {
				// Check if this button is already in the combo (avoid duplicates)
				auto it = recordingButtonControllers.find(pressedKey);
				if (it == recordingButtonControllers.end()) {
					// Not yet recorded, add with the current device
					recordingButtonControllers[pressedKey] = pressedDevice;
				} else {
					// Already recorded, if the other controller is now pressed, set to BOTH
					if (it->second != pressedDevice && it->second != ControllerDevice::Both) {
						it->second = ControllerDevice::Both;
					}
				}
				// Update the recordedCombo vector to match the map
				this->recordedCombo.clear();
				for (const auto& [key, device] : recordingButtonControllers) {
					this->recordedCombo.push_back(ButtonCombo(device, key));
				}
			}

			// Handle ENTER key to accept combo
			if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
				applyRecordedCombo();

				// Reset recording state
				this->isCapturingCombo = false;
				this->currentComboType = VR::ComboType::None;
				this->currentComboName = nullptr;
				this->recordedCombo.clear();
				this->comboStartTime = 0.0;
				recordingButtonControllers.clear();
				ImGui::CloseCurrentPopup();
			}

			// Handle ESC key to cancel
			if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				// Reset recording state
				this->isCapturingCombo = false;
				this->currentComboType = VR::ComboType::None;
				this->currentComboName = nullptr;
				this->recordedCombo.clear();
				this->comboStartTime = 0.0;
				recordingButtonControllers.clear();
				ImGui::CloseCurrentPopup();
			}

			// Handle timeout - auto-accept if buttons were pressed, auto-cancel if not
			if (remainingTime <= 0.0) {
				applyRecordedCombo();
				// Auto-cancel if no buttons were pressed (do nothing, just close)

				// Reset recording state
				this->isCapturingCombo = false;
				this->currentComboType = VR::ComboType::None;
				this->currentComboName = nullptr;
				this->recordedCombo.clear();
				this->comboStartTime = 0.0;
				recordingButtonControllers.clear();
				ImGui::CloseCurrentPopup();
			}
		}
	}
}

namespace
{
	constexpr std::array<const char*, 4> kVRFpsStabilizerMethodNames{ "None", "TAA", "AMD FSR", "NVIDIA DLSS" };
	constexpr std::array<const char*, 7> kVRFpsStabilizerPresetNames{
		"Native AA",
		"Hoshipa",
		"Ultra Quality",
		"Quality",
		"Balanced",
		"Performance",
		"Ultra Performance"
	};
	constexpr std::array<const char*, 5> kVRFpsStabilizerDLSSProfileNames{ "J", "K", "L", "M", "F" };

	struct VRFpsStabilizerUIState
	{
		bool initialized = false;
		bool dirty = false;
		bool restartRequired = false;
		bool loadFailed = false;
		bool messageIsError = false;
		bool profilesDefinedInIni = false;
		std::string message;
		Upscaling::VRFpsStabilizerConfig config;
		Upscaling::VRFpsStabilizerConfig baselineConfig;
	};

	bool HasVRFpsStabilizerProfileRows(const Upscaling::VRFpsStabilizerConfig& config)
	{
		return config.HasAnyProfile() ||
		       config.interior.invalidSettingCount > 0 ||
		       config.exterior.invalidSettingCount > 0;
	}

	bool MatchesVRFpsStabilizerEditableProfile(
		const Upscaling::VRFpsStabilizerProfile& lhs,
		const Upscaling::VRFpsStabilizerProfile& rhs)
	{
		return lhs.upscaleMethod == rhs.upscaleMethod &&
		       lhs.qualityMode == rhs.qualityMode &&
		       lhs.dlssPreset == rhs.dlssPreset &&
		       lhs.renderScaleMode == rhs.renderScaleMode &&
		       lhs.screenSpaceShadowsEnabled == rhs.screenSpaceShadowsEnabled &&
		       lhs.screenSpaceGIEnabled == rhs.screenSpaceGIEnabled &&
		       lhs.volumetricLightingExteriorEnabled == rhs.volumetricLightingExteriorEnabled &&
		       lhs.contactShadowsEnabled == rhs.contactShadowsEnabled;
	}

	bool HasVRFpsStabilizerEditableChanges(const VRFpsStabilizerUIState& state)
	{
		const bool startedNewProfileDefinition =
			!state.profilesDefinedInIni && state.config.HasAnyProfile();
		return startedNewProfileDefinition ||
		       state.config.upscalingSwitchingEnabled != state.baselineConfig.upscalingSwitchingEnabled ||
		       state.config.fadeDuration != state.baselineConfig.fadeDuration ||
		       !MatchesVRFpsStabilizerEditableProfile(state.config.interior, state.baselineConfig.interior) ||
		       !MatchesVRFpsStabilizerEditableProfile(state.config.exterior, state.baselineConfig.exterior);
	}

	void RefreshVRFpsStabilizerUIStateDirty(VRFpsStabilizerUIState& state)
	{
		state.dirty = HasVRFpsStabilizerEditableChanges(state);
	}

	void LoadVRFpsStabilizerUIState(VRFpsStabilizerUIState& state)
	{
		state.message.clear();
		state.loadFailed = !globals::features::upscaling.LoadVRFpsStabilizerConfig(state.config, state.message);
		state.messageIsError = state.loadFailed;
		state.profilesDefinedInIni = !state.loadFailed && HasVRFpsStabilizerProfileRows(state.config);
		if (!state.profilesDefinedInIni) {
			state.config.upscalingSwitchingEnabled = false;
			state.config.interior = {};
			state.config.exterior = {};
		}
		state.initialized = true;
		state.baselineConfig = state.config;
		RefreshVRFpsStabilizerUIStateDirty(state);
	}

	void HandleVRFpsStabilizerUIEdit(VRFpsStabilizerUIState& state)
	{
		RefreshVRFpsStabilizerUIStateDirty(state);
		state.message.clear();
		state.messageIsError = false;
	}

	bool DrawVRFpsStabilizerUpscaleMethod(Upscaling::VRFpsStabilizerProfile& profile)
	{
		bool changed = false;
		const int method = std::clamp(
			static_cast<int>(profile.upscaleMethod),
			static_cast<int>(Upscaling::UpscaleMethod::kNONE),
			static_cast<int>(Upscaling::UpscaleMethod::kDLSS));
		const bool methodConfigured = profile.hasUpscaleMethod || profile.hasLegacyMethodSelection;
		const char* preview = methodConfigured ? kVRFpsStabilizerMethodNames[method] : "Not configured";
		ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
		if (ImGui::BeginCombo("##UpscaleMethod", preview)) {
			for (int option = 0; option < static_cast<int>(kVRFpsStabilizerMethodNames.size()); ++option) {
				const bool selected = methodConfigured && option == method;
				if (ImGui::Selectable(kVRFpsStabilizerMethodNames[option], selected)) {
					changed = !selected || profile.hasLegacyMethodSelection;
					profile.upscaleMethod = static_cast<Upscaling::UpscaleMethod>(option);
					profile.hasUpscaleMethod = true;
					profile.hasLegacyMethodSelection = false;
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("AMD FSR profiles keep the current AMD FSR3 or AMD FSR4 selection.");
		}

		return changed;
	}

	bool DrawVRFpsStabilizerUpscalePreset(Upscaling::VRFpsStabilizerProfile& profile)
	{
		if (!profile.hasUpscaleMethod && !profile.hasLegacyMethodSelection) {
			ImGui::TextDisabled("Not configured");
			return false;
		}

		bool changed = false;
		const bool vendorUpscaling =
			profile.upscaleMethod == Upscaling::UpscaleMethod::kFSR ||
			profile.upscaleMethod == Upscaling::UpscaleMethod::kDLSS;
		const char* nativePresetName = profile.upscaleMethod == Upscaling::UpscaleMethod::kDLSS ? "DLAA" : "Native AA";
		auto presetNames = kVRFpsStabilizerPresetNames;
		presetNames[0] = nativePresetName;
		int qualityMode = static_cast<int>(std::min(profile.qualityMode, Upscaling::kQualityModeMaxIndex));
		{
			auto disabledGuard = Util::DisableGuard(!vendorUpscaling);
			ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
			if (ImGui::Combo("##UpscalePreset", &qualityMode, presetNames.data(), static_cast<int>(presetNames.size()))) {
				profile.qualityMode = static_cast<uint32_t>(qualityMode);
				profile.hasQualityMode = true;
				changed = true;
			}
		}
		return changed;
	}

	bool DrawVRFpsStabilizerDLSSProfile(Upscaling::VRFpsStabilizerProfile& profile)
	{
		if (!profile.hasUpscaleMethod && !profile.hasLegacyMethodSelection) {
			ImGui::TextDisabled("Not configured");
			return false;
		}

		bool changed = false;
		int dlssPreset = static_cast<int>(std::min(profile.dlssPreset, Upscaling::kDLSSPresetF));
		{
			auto disabledGuard = Util::DisableGuard(profile.upscaleMethod != Upscaling::UpscaleMethod::kDLSS);
			ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
			if (ImGui::Combo("##DLSSProfile", &dlssPreset, kVRFpsStabilizerDLSSProfileNames.data(), static_cast<int>(kVRFpsStabilizerDLSSProfileNames.size()))) {
				profile.dlssPreset = static_cast<uint32_t>(dlssPreset);
				profile.hasDLSSPreset = true;
				changed = true;
			}
		}
		return changed;
	}

	bool DrawVRFpsStabilizerRenderScale(Upscaling::VRFpsStabilizerProfile& profile)
	{
		bool changed = false;
		const bool vendorUpscaling =
			profile.upscaleMethod == Upscaling::UpscaleMethod::kFSR ||
			profile.upscaleMethod == Upscaling::UpscaleMethod::kDLSS;
		const bool renderScaleEligible = vendorUpscaling && profile.qualityMode > 0;
		if (!renderScaleEligible && profile.renderScaleMode) {
			profile.renderScaleMode = false;
			profile.hasRenderScaleMode = true;
			changed = true;
		}
		{
			auto disabledGuard = Util::DisableGuard(!renderScaleEligible);
			if (ImGui::Checkbox("Enable##RenderScale", &profile.renderScaleMode)) {
				profile.hasRenderScaleMode = true;
				changed = true;
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Available with AMD FSR or NVIDIA DLSS and a below-native preset.");
		}
		return changed;
	}

	bool DrawVRFpsStabilizerFeatureToggle(
		const char* id,
		const char* label,
		bool& enabled,
		bool& hasSetting)
	{
		ImGui::PushID(id);
		const bool changed = ImGui::Checkbox(label, &enabled);
		ImGui::PopID();
		if (changed)
			hasSetting = true;
		return changed;
	}

	bool DrawVRFpsStabilizerNotConfiguredToggle(const char* id, const char* label)
	{
		bool enabled = false;
		auto disabledGuard = Util::DisableGuard(true);
		ImGui::PushID(id);
		ImGui::Checkbox(label, &enabled);
		ImGui::PopID();
		return false;
	}

	bool DrawVRFpsStabilizerNotConfiguredText()
	{
		ImGui::TextDisabled("Not configured");
		return false;
	}

	void SetupVRFpsStabilizerProfileTableColumns(bool currentCellIsInterior)
	{
		const char* interiorHeader = currentCellIsInterior ?
		                                 "Interior Profile (Current Location)###InteriorProfile" :
		                                 "Interior Profile###InteriorProfile";
		const char* exteriorHeader = currentCellIsInterior ?
		                                 "Exterior Profile###ExteriorProfile" :
		                                 "Exterior Profile (Current Location)###ExteriorProfile";
		ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 0.85f);
		ImGui::TableSetupColumn(interiorHeader, ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn(exteriorHeader, ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableHeadersRow();
	}

	void DrawVRFpsStabilizerProfileRowLabel(const char* label)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
	}

	bool DrawVRFpsStabilizerProfileEditors(
		Upscaling::VRFpsStabilizerConfig& config,
		bool currentCellIsInterior,
		bool showNotConfigured)
	{
		bool changed = false;
		constexpr auto tableFlags =
			ImGuiTableFlags_Borders |
			ImGuiTableFlags_RowBg |
			ImGuiTableFlags_SizingStretchProp |
			ImGuiTableFlags_PadOuterX |
			ImGuiTableFlags_NoSavedSettings;

		const auto drawProfileCells = [&](auto&& drawControl) {
			ImGui::TableSetColumnIndex(1);
			ImGui::PushID("InteriorProfile");
			changed |= drawControl(config.interior);
			ImGui::PopID();
			ImGui::TableSetColumnIndex(2);
			ImGui::PushID("ExteriorProfile");
			changed |= drawControl(config.exterior);
			ImGui::PopID();
		};

		ImGui::SeparatorText("Upscaling");
		if (ImGui::BeginTable("##VRFpsStabilizerUpscalingProfiles", 3, tableFlags)) {
			SetupVRFpsStabilizerProfileTableColumns(currentCellIsInterior);

			DrawVRFpsStabilizerProfileRowLabel("Method");
			if (showNotConfigured)
				drawProfileCells([](auto&) { return DrawVRFpsStabilizerNotConfiguredText(); });
			else
				drawProfileCells([](auto& profile) { return DrawVRFpsStabilizerUpscaleMethod(profile); });

			DrawVRFpsStabilizerProfileRowLabel("Upscale Preset");
			if (showNotConfigured)
				drawProfileCells([](auto&) { return DrawVRFpsStabilizerNotConfiguredText(); });
			else
				drawProfileCells([](auto& profile) { return DrawVRFpsStabilizerUpscalePreset(profile); });

			DrawVRFpsStabilizerProfileRowLabel("DLSS Profile");
			if (showNotConfigured)
				drawProfileCells([](auto&) { return DrawVRFpsStabilizerNotConfiguredText(); });
			else
				drawProfileCells([](auto& profile) { return DrawVRFpsStabilizerDLSSProfile(profile); });

			DrawVRFpsStabilizerProfileRowLabel("Render Scale");
			if (showNotConfigured)
				drawProfileCells([](auto&) { return DrawVRFpsStabilizerNotConfiguredToggle("RenderScale", "Enable"); });
			else
				drawProfileCells([](auto& profile) { return DrawVRFpsStabilizerRenderScale(profile); });

			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::SeparatorText("Features");
		if (ImGui::BeginTable("##VRFpsStabilizerFeatureProfiles", 3, tableFlags)) {
			SetupVRFpsStabilizerProfileTableColumns(currentCellIsInterior);

			DrawVRFpsStabilizerProfileRowLabel("Screen Space Shadows");
			if (showNotConfigured) {
				drawProfileCells([](auto&) { return DrawVRFpsStabilizerNotConfiguredToggle("ScreenSpaceShadows", "Enable"); });
			} else {
				drawProfileCells([](auto& profile) {
					return DrawVRFpsStabilizerFeatureToggle(
						"ScreenSpaceShadows",
						"Enable",
						profile.screenSpaceShadowsEnabled,
						profile.hasScreenSpaceShadows);
				});
			}

			DrawVRFpsStabilizerProfileRowLabel("Screen Space GI");
			if (showNotConfigured) {
				drawProfileCells([](auto&) { return DrawVRFpsStabilizerNotConfiguredToggle("ScreenSpaceGI", "Enable"); });
			} else {
				drawProfileCells([](auto& profile) {
					return DrawVRFpsStabilizerFeatureToggle(
						"ScreenSpaceGI",
						"Enable",
						profile.screenSpaceGIEnabled,
						profile.hasScreenSpaceGI);
				});
			}

			DrawVRFpsStabilizerProfileRowLabel("Point Light Contact Shadows");
			if (showNotConfigured) {
				drawProfileCells([](auto&) { return DrawVRFpsStabilizerNotConfiguredToggle("PointLightContactShadows", "Enable"); });
			} else {
				drawProfileCells([](auto& profile) {
					return DrawVRFpsStabilizerFeatureToggle(
						"PointLightContactShadows",
						"Enable",
						profile.contactShadowsEnabled,
						profile.hasContactShadows);
				});
			}

			DrawVRFpsStabilizerProfileRowLabel("Volumetric Lighting");
			ImGui::TableSetColumnIndex(2);
			ImGui::PushID("ExteriorProfile");
			if (showNotConfigured) {
				changed |= DrawVRFpsStabilizerNotConfiguredToggle("VolumetricLighting", "Enable in Exteriors");
			} else {
				changed |= DrawVRFpsStabilizerFeatureToggle(
					"VolumetricLighting",
					"Enable in Exteriors",
					config.exterior.volumetricLightingExteriorEnabled,
					config.exterior.hasVolumetricLightingExterior);
			}
			ImGui::PopID();

			ImGui::EndTable();
		}

		return changed;
	}

	void DrawVRFpsStabilizerSettings()
	{
		auto& upscaling = globals::features::upscaling;
		static VRFpsStabilizerUIState uiState;
		if (!uiState.initialized)
			LoadVRFpsStabilizerUIState(uiState);

		if (uiState.dirty) {
			Util::Text::WrappedError(
				"VR FPS Stabilizer settings have changed. Use Save INI below to write them to VRFpsStabilizer.ini.");
			ImGui::Spacing();
		}

		ImGui::TextUnformatted("Interior and Exterior Profiles");
		ImGui::TextWrapped("Choose the VR FPS Stabilizer settings for each location type.");
		ImGui::Spacing();
		const bool currentCellIsInterior = Util::IsInterior();
		ImGui::TextDisabled(
			"Current location: %s. The matching profile column is marked below.",
			currentCellIsInterior ? "Interior" : "Exterior");
		if (!uiState.config.path.empty())
			ImGui::TextDisabled("INI file: %s", uiState.config.path.string().c_str());

		if (!uiState.loadFailed && !uiState.profilesDefinedInIni) {
			ImGui::Spacing();
			if (uiState.config.upscalingSwitchingEnabled) {
				Util::Text::WrappedWarning(
					"No VR FPS Stabilizer Interior/Exterior profile settings are defined in this INI yet. Choose a Method for both profiles and configure the remaining settings, then use Save INI. The new profiles take effect after restarting Skyrim VR.");
			} else {
				Util::Text::WrappedWarning(
					"No VR FPS Stabilizer Interior/Exterior profile settings are defined in this INI. Switching remains inactive and no profile values are being applied. Enable switching to begin configuring them.");
			}
		}

		ImGui::Spacing();
		{
			auto disabledGuard = Util::DisableGuard(uiState.loadFailed);
			if (ImGui::Checkbox(
					"Enable VR FPS Stabilizer Interior/Exterior switching",
					&uiState.config.upscalingSwitchingEnabled)) {
				HandleVRFpsStabilizerUIEdit(uiState);
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Controls switching between the Interior and Exterior profiles shown below.");
			ImGui::TextUnformatted("It does not disable VR FPS Stabilizer or edit its other settings and conditional profiles.");
		}
		if (!uiState.config.upscalingSwitchingEnabled && uiState.profilesDefinedInIni) {
			Util::Text::WrappedWarning(
				"Interior/Exterior switching is off. Saving disables the managed profile group; other VR FPS Stabilizer settings are preserved.");
		}

		if (uiState.loadFailed) {
			ImGui::Spacing();
			Util::Text::WrappedError("%s", uiState.message.c_str());
			ImGui::Spacing();
			if (ImGui::Button("Reload INI"))
				LoadVRFpsStabilizerUIState(uiState);
			return;
		}

		ImGui::Spacing();
		{
			auto disabledGuard = Util::DisableGuard(!uiState.config.upscalingSwitchingEnabled);
			const bool showNotConfigured =
				!uiState.profilesDefinedInIni &&
				!uiState.config.upscalingSwitchingEnabled &&
				!uiState.config.HasAnyProfile();
			if (DrawVRFpsStabilizerProfileEditors(uiState.config, currentCellIsInterior, showNotConfigured))
				HandleVRFpsStabilizerUIEdit(uiState);
		}

		ImGui::Spacing();
		const bool openCompositeBlocksUpscaling = upscaling.IsOpenCompositeUpscalingBlocked();
		const auto& sessionConfig = upscaling.GetVRFpsStabilizerSessionConfig();
		if (openCompositeBlocksUpscaling) {
			Util::Text::WrappedWarning(
				"VR FPS Stabilizer profile sync: Inactive because Open Composite owns upscaling for this session.");
		} else if (upscaling.IsVRFpsStabilizerSyncActive()) {
			ImGui::TextColored(
				Util::Colors::GetSuccess(),
				"VR FPS Stabilizer profile sync: Active for this session.");
		} else if (!sessionConfig.fileExists) {
			ImGui::TextDisabled("VR FPS Stabilizer profile sync: Inactive; VRFpsStabilizer.ini was not found at startup.");
		} else if (!sessionConfig.fileReadable) {
			ImGui::TextDisabled("VR FPS Stabilizer profile sync: Inactive; VRFpsStabilizer.ini was not readable at startup.");
		} else if (!sessionConfig.upscalingSwitchingEnabled) {
			ImGui::TextDisabled("VR FPS Stabilizer profile sync: Inactive because Interior/Exterior switching was off at startup.");
		} else {
			ImGui::TextDisabled("VR FPS Stabilizer profile sync: Inactive; no Interior or Exterior upscaling profile was configured at startup.");
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Profile sync activates automatically when the INI contains an active Interior or Exterior upscaling profile.");
			ImGui::TextUnformatted("The startup state is authoritative for this game session; saved or manual INI changes take effect after restarting Skyrim VR.");
		}

		const bool completeConfig = uiState.config.HasCompleteSettings();
		const bool completeProfiles = uiState.config.HasCompleteProfiles();
		const uint32_t invalidSettingCount = uiState.config.GetInvalidSettingCount();
		const bool configNeedsNormalization =
			!completeConfig || invalidSettingCount > 0 || uiState.config.hasMixedUpscalingSwitchingActivation;
		if (uiState.config.hasMixedUpscalingSwitchingActivation) {
			ImGui::Spacing();
			Util::Text::WrappedWarning(
				"The managed Interior/Exterior profile rows and transition fade contain a mix of active and UI-disabled entries. Active entries take precedence; saving will make the whole group match this toggle.");
		}
		if (invalidSettingCount > 0) {
			ImGui::Spacing();
			Util::Text::WrappedWarning(
				"%u recognized profile value(s) or combination(s) are invalid or outside the supported range. Safe resolved values are shown; saving will normalize those rows.",
				invalidSettingCount);
		}
		if (!completeProfiles && uiState.profilesDefinedInIni) {
			ImGui::Spacing();
			Util::Text::WrappedWarning(
				"Missing profile values inherit the current runtime settings. Saving will write complete upscaling and feature settings for both profiles.");
		}
		if (!uiState.config.hasFadeDuration && uiState.profilesDefinedInIni) {
			ImGui::Spacing();
			Util::Text::WrappedWarning(
				"The Render Scale transition fade duration is missing. CSX owns transition coverage; 0 seconds is recommended and will be written when saved.");
		}

		ImGui::Spacing();
		ImGui::SeparatorText("Render Scale Transition Fade");
		{
			auto disabledGuard = Util::DisableGuard(!uiState.config.upscalingSwitchingEnabled);
			float fadeDuration = uiState.config.fadeDuration;
			if (ImGui::InputFloat("Fade-to-black duration (seconds)", &fadeDuration, 0.25f, 1.0f, "%.2f")) {
				if (std::isfinite(fadeDuration)) {
					uiState.config.fadeDuration = std::max(fadeDuration, 0.0f);
					uiState.config.hasFadeDuration = true;
					HandleVRFpsStabilizerUIEdit(uiState);
				}
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("VR FPS Stabilizer's separate timed fade for Interior/Exterior Render Scale profile changes.");
			ImGui::TextUnformatted("Set this to 0 seconds because CSX owns transition coverage and releases Skyrim's loading fade when stereo presentation is coherent.");
		}

		ImGui::Spacing();
		if (ImGui::Button(uiState.dirty ? "Discard & Reload" : "Reload INI"))
			LoadVRFpsStabilizerUIState(uiState);
		ImGui::SameLine();
		{
			const bool newProfilesReady =
				(uiState.config.interior.hasUpscaleMethod || uiState.config.interior.hasLegacyMethodSelection) &&
				(uiState.config.exterior.hasUpscaleMethod || uiState.config.exterior.hasLegacyMethodSelection);
			const bool saveAvailable =
				uiState.profilesDefinedInIni ?
					(uiState.dirty || configNeedsNormalization) :
					(uiState.dirty && newProfilesReady);
			auto disabledGuard = Util::DisableGuard(!saveAvailable);
			const bool saveRequested = uiState.dirty ? Util::WarningButton("Save INI") : ImGui::Button("Save INI");
			if (saveRequested) {
				uiState.message.clear();
				if (upscaling.SaveVRFpsStabilizerConfig(uiState.config, uiState.message)) {
					uiState.config.MarkSettingsComplete();
					uiState.profilesDefinedInIni = true;
					uiState.baselineConfig = uiState.config;
					RefreshVRFpsStabilizerUIStateDirty(uiState);
					uiState.restartRequired = true;
					uiState.messageIsError = false;
					uiState.message = uiState.config.upscalingSwitchingEnabled ?
					                      "VR FPS Stabilizer Interior/Exterior switching enabled in VRFpsStabilizer.ini." :
					                      "VR FPS Stabilizer Interior/Exterior switching disabled in VRFpsStabilizer.ini.";
				} else {
					uiState.loadFailed = false;
					uiState.messageIsError = true;
					RefreshVRFpsStabilizerUIStateDirty(uiState);
				}
			}
		}

		if (!uiState.message.empty()) {
			ImGui::Spacing();
			if (uiState.messageIsError) {
				Util::Text::WrappedError("%s", uiState.message.c_str());
			} else {
				ImGui::TextColored(Util::Colors::GetSuccess(), "%s", uiState.message.c_str());
			}
		}
		if (uiState.restartRequired) {
			ImGui::Spacing();
			Util::Text::WrappedWarning("Restart Skyrim VR so VR FPS Stabilizer reloads the edited INI.");
		}
		Util::Text::WrappedDisabled(
			"Only the managed Interior/Exterior profile group and the Render Scale transition fade are edited. Other VR FPS Stabilizer settings and conditional profiles are preserved.");
	}
}

namespace
{
	void DrawDepthCullingSettings(VR& a_vr, const char* a_id)
	{
		auto& settings = a_vr.settings;
		ImGui::PushID(a_id);
		ImGui::SeparatorText("Depth Culling");
		bool exteriorChanged = false;
		bool interiorChanged = false;
		if (ImGui::BeginTable("##Options", 2, ImGuiTableFlags_SizingStretchSame)) {
			ImGui::TableNextColumn();
			exteriorChanged = ImGui::Checkbox("Depth Buffer Culling", &settings.EnableDepthBufferCullingExterior);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Master switch for native GPU depth culling. When enabled, it applies in exteriors and controls whether interior culling can run.");
			}

			ImGui::TableNextColumn();
			{
				auto guard = Util::DisableGuard(!settings.EnableDepthBufferCullingExterior);
				interiorChanged = ImGui::Checkbox("Enable in Interiors", &settings.EnableDepthBufferCullingInterior);
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Enabled by default. It improves indoor culling; Balanced mode limits one-frame missing-object faults during head motion.");
			}
			ImGui::EndTable();
		}

		ImGui::TextUnformatted("Temporal Policy");
		{
			auto guard = Util::DisableGuard(!settings.EnableDepthBufferCullingExterior);
			auto mode = a_vr.GetDepthCullingMode();
			if (ImGui::BeginTable("##TemporalPolicy", 3, ImGuiTableFlags_SizingStretchSame)) {
				ImGui::TableNextColumn();
				if (ImGui::RadioButton("Balanced (Default)", mode == VRDepthCullingTemporal::Mode::Balanced)) {
					mode = VRDepthCullingTemporal::Mode::Balanced;
					a_vr.SetDepthCullingMode(mode);
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::TextUnformatted("On a motion-envelope miss, test conservative OBB bounds and recover at most 64 high-risk objects.");
				}

				ImGui::TableNextColumn();
				if (ImGui::RadioButton("Performance", mode == VRDepthCullingTemporal::Mode::Performance)) {
					mode = VRDepthCullingTemporal::Mode::Performance;
					a_vr.SetDepthCullingMode(mode);
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::TextUnformatted("Accept the native one-frame-late result while keeping the producer pose warm for an immediate switch back to Balanced.");
					ImGui::TextUnformatted("This skips the recovery scan but can briefly hide newly visible objects during head motion.");
				}

				ImGui::TableNextColumn();
				if (ImGui::RadioButton("Legacy", mode == VRDepthCullingTemporal::Mode::Legacy)) {
					mode = VRDepthCullingTemporal::Mode::Legacy;
					a_vr.SetDepthCullingMode(mode);
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::TextUnformatted("Off by default. Use native results without temporal pose capture or recovery.");
				}
				ImGui::EndTable();
			}
		}

		if (exteriorChanged || interiorChanged)
			a_vr.UpdateDepthBufferCulling();

		ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
		if (ImGui::SliderFloat("Min Occludee Box Extent", &settings.MinOccludeeBoxExtent, 0.0f, 1000.0f, "%.1f")) {
			if (a_vr.gMinOccludeeBoxExtent)
				*a_vr.gMinOccludeeBoxExtent = settings.MinOccludeeBoxExtent;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Minimum bounding-box extent eligible for occlusion culling. Lower values cull more small objects but can make faults more noticeable.");
		}
		ImGui::PopID();
	}
}

void VR::DrawPerformanceSettings(bool a_advanced)
{
	if (!REL::Module::IsVR()) {
		ImGui::TextDisabled("VR performance settings are available only in VR.");
		return;
	}

	DrawDepthCullingSettings(*this, "PerformanceSettings");

	if (!a_advanced)
		return;

	auto& screenSpaceShadows = globals::features::screenSpaceShadows;
	auto& screenSpaceGI = globals::features::screenSpaceGI;
	const bool screenSpaceShadowsEnabled = screenSpaceShadows.loaded && screenSpaceShadows.bendSettings.Enable != 0;
	const bool screenSpaceGIEnabled = screenSpaceGI.loaded && screenSpaceGI.settings.Enabled;

	ImGui::SeparatorText("Stereo");
	{
		auto guard = Util::DisableGuard(!screenSpaceShadowsEnabled);
		ImGui::Checkbox("Stereo Sync SSS", &screenSpaceShadows.enableStereoSync);
	}
	{
		auto guard = Util::DisableGuard(!screenSpaceShadowsEnabled || !screenSpaceShadows.enableStereoSync);
		ImGui::Checkbox("Stereo Reproject SSS", &screenSpaceShadows.useStereoReproject);
	}
	{
		auto guard = Util::DisableGuard(!screenSpaceGIEnabled);
		ImGui::Checkbox("Stereo Sync SSGI", &screenSpaceGI.settings.EnableStereoSync);
	}
	{
		auto guard = Util::DisableGuard(!screenSpaceGIEnabled || !screenSpaceGI.settings.EnableStereoSync);
		ImGui::Checkbox("Stereo Reproject SSGI", &screenSpaceGI.settings.UseStereoReproject);
	}
	ImGui::Checkbox("Blend Between Eyes", &settings.EnableStereoBlend);

	ImGui::SeparatorText("Shader FOV");
	auto& dynamicCubemaps = globals::features::dynamicCubemaps;
	auto& waterEffects = globals::features::waterEffects;
	auto& wetnessEffects = globals::features::wetnessEffects;
	auto& wetterness = globals::features::wetterness;
	const bool ssrAvailable = dynamicCubemaps.IsSSRRuntimeActive();
	const bool waterParallaxAvailable = waterEffects.loaded;
	const bool wetnessEffectsRuntimeActive = wetnessEffects.IsRuntimeActive();
	const bool wetternessAvailable = wetterness.loaded && wetterness.IsRuntimeActive() && !wetnessEffectsRuntimeActive;
	const bool dynamicCubemapsAvailable = dynamicCubemaps.loaded;

	ImGui::Checkbox("Lighting", &settings.EnableLightingFoveation);
	{
		auto guard = Util::DisableGuard(!ssrAvailable);
		ImGui::Checkbox("SSR", &settings.EnableSSRFoveation);
	}
	{
		auto guard = Util::DisableGuard(!waterParallaxAvailable);
		ImGui::Checkbox("Water Parallax Detail", &settings.EnableWaterParallaxFoveation);
	}
	{
		auto guard = Util::DisableGuard(!wetternessAvailable);
		ImGui::Checkbox("Wetterness", &settings.EnableWetternessFoveation);
	}
	if (!ssrAvailable)
		ImGui::TextDisabled("SSR foveation requires runtime-active Screen Space Reflections.");
	if (!waterParallaxAvailable)
		ImGui::TextDisabled("Water Parallax Detail requires Water Effects.");
	if (!wetternessAvailable)
		ImGui::TextDisabled(wetnessEffectsRuntimeActive ? "Wetterness foveation is not available with legacy Wetness Effects." : "Wetterness foveation requires Wetterness to be enabled.");
	{
		auto guard = Util::DisableGuard(!dynamicCubemapsAvailable);
		ImGui::Checkbox("Dynamic Cubemap Cadence", &settings.EnableDynamicCubemapFoveation);
	}

	DisableDynamicCubemapVisibilityThrottleForWetterness(settings);
	const bool dynamicCubemapVisibilityThrottleBlockedByWetterness = IsWetternessActiveForDynamicCubemapVisibilityThrottle();
	{
		auto guard = Util::DisableGuard(!dynamicCubemapsAvailable || dynamicCubemapVisibilityThrottleBlockedByWetterness);
		ImGui::Checkbox("Low-Visibility Cubemap Throttle", &settings.EnableDynamicCubemapVisibilityThrottle);
	}
	if (!dynamicCubemapsAvailable)
		ImGui::TextDisabled("Dynamic Cubemap FOV controls require Dynamic Cubemaps.");
	if (dynamicCubemapVisibilityThrottleBlockedByWetterness)
		ImGui::TextDisabled("Low-Visibility Cubemap Throttle is disabled while Wetterness is active.");

	if (IsRenderScaleDesktopMirrorQualityAvailable()) {
		ImGui::SeparatorText("Desktop Mirror");
		ImGui::Checkbox("Improve Render-Scale Desktop Mirror Quality", &settings.StabilizeRenderScaleDesktopMirror);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Improves the desktop mirror image when VR Render Scale Mode lowers the source resolution.");
			ImGui::TextUnformatted("Only the desktop view changes. This can cost a little performance while render scale is active.");
		}
	}
}

namespace
{
	bool CanConfigureMenuLayout()
	{
		return REL::Module::IsVR();
	}

	void DrawKeepDesktopWindowFocusedForVRMenuSetting();
	void DrawStabilizeRenderScaleDesktopMirrorSetting();
	void DrawCSMenuNavigationSettings();
	void DrawMenuLayoutUnlockSetting();
	void DrawKeyBindings();
	void DrawControllerBindingSummary(bool a_includeAutoHideSetting, const char* a_idPrefix);
}

void VR::DrawEssentialSettings()
{
	DrawCSMenuNavigationSettings();

	if (CanConfigureMenuLayout()) {
		ImGui::SeparatorText("Menu Layout");
		DrawMenuLayoutUnlockSetting();
	}

	ImGui::SeparatorText("Desktop");
	DrawKeepDesktopWindowFocusedForVRMenuSetting();
	DrawStabilizeRenderScaleDesktopMirrorSetting();

	if (openVRInfo.isCompatible) {
		ImGui::SeparatorText("Bindings");
		DrawKeyBindings();
	}
}

json VR::CapturePerformanceSettingsState() const
{
	json state = CapturePerformanceCostMeasurementState();
	state["MinOccludeeBoxExtent"] = settings.MinOccludeeBoxExtent;
	return state;
}

namespace
{
	void DrawCSMenuNavigationSettings()
	{
		auto& vr = globals::features::vr;
		if (!vr.openVRInfo.isCompatible)
			return;

		auto& settings = vr.settings;
		ImGui::SeparatorText("CSX Menu Navigation");

		const bool effectiveWandNavigation = vr.CanUseWandPointing();
		auto setWandNavigation = [&](bool a_enabled) {
			const bool modeChanged = vr.CanUseWandPointing() != a_enabled;
			settings.UseRuntimeDefaultMenuNavigation = false;
			settings.EnableWandPointing = a_enabled;
			if (modeChanged) {
				vr.ResetWandPointingRuntimeState();
			}
		};

		bool mouseNavigation = !effectiveWandNavigation;
		if (ImGui::Checkbox("Mouse Navigation", &mouseNavigation)) {
			setWandNavigation(false);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Use thumbstick-driven cursor navigation for the CSX menu.");
		}

		ImGui::SameLine();

		bool wandNavigation = effectiveWandNavigation;
		if (ImGui::Checkbox("Wand Navigation", &wandNavigation)) {
			setWandNavigation(true);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Use controller ray-cast pointing for the CSX menu.");
		}

		if (effectiveWandNavigation) {
			ImGui::SetNextItemWidth(220.0f);
			ImGui::SliderFloat(
				"Wand Aim Pitch Trim",
				&settings.WandAimPitchTrimDegrees,
				VR::Config::kMinWandAimPitchTrimDegrees,
				VR::Config::kMaxWandAimPitchTrimDegrees,
				"%+.1f deg");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Rotates the pointer around the controller aim pose's local X axis.");
				ImGui::TextUnformatted("Positive values pitch the pointer from local forward toward local up.");
			}
		}
	}

	void DrawControllerBindingSummary(bool a_includeAutoHideSetting, const char* a_idPrefix)
	{
		auto& vr = globals::features::vr;
		auto& settings = vr.settings;
		ImGui::PushID(a_idPrefix);

		if (a_includeAutoHideSetting) {
			ImGui::SliderInt("Welcome Message Timeout", &settings.kAutoHideSeconds, 0, VR::Config::kMaxAutoHideSeconds,
				settings.kAutoHideSeconds <= 0 ? "Hidden" : "%d seconds");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Set to 0 to hide the startup controller instructions, or choose how long to show them.");
			}
		}

		if (vr.CanOpenMenuFromWorld()) {
			ImGui::TextWrapped("CSX settings menu (available during gameplay and menus with the current SteamVR overlay path):");
		} else {
			ImGui::TextWrapped("CSX settings menu (open from the Skyrim Main Menu or Tween Menu):");
		}
		if (ImGui::BeginTable("MenuInstructionsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Open CSX Menu:");
			ImGui::TableSetColumnIndex(1);
			Util::DrawButtonCombo(settings.VRMenuOpenKeys, true);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Close CSX Menu:");
			ImGui::TableSetColumnIndex(1);
			Util::DrawButtonCombo(settings.VRMenuCloseKeys, true);
			ImGui::EndTable();
		}

		ImGui::TextWrapped("Performance Overlay (available during gameplay and menus):");
		if (ImGui::BeginTable("OverlayInstructionsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Show Performance Overlay:");
			ImGui::TableSetColumnIndex(1);
			Util::DrawButtonCombo(settings.VROverlayOpenKeys, true);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Hide Performance Overlay:");
			ImGui::TableSetColumnIndex(1);
			Util::DrawButtonCombo(settings.VROverlayCloseKeys, true);
			ImGui::EndTable();
		}

		ImGui::TextWrapped("Menu Controller Input:");
		if (ImGui::BeginTable("ControllerInputTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextColored(Util::GetControllerBothColor(), "Trigger (Both Controllers)");
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("Left mouse button");
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextColored(Util::GetControllerBothColor(), "Grip (Both Controllers)");
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("Right mouse button");
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextColored(Util::GetControllerBothColor(), "Touchpad Click (Both Controllers)");
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("Middle mouse button");
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextColored(Util::GetControllerBothColor(), "Stick Click (Both Controllers)");
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("Middle mouse button");
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextColored(Util::GetControllerBothColor(), "A/X (Both Controllers)");
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("Enter");
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextColored(Util::GetControllerPrimaryColor(), "B/Y (Primary Controller)");
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("Tab");
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextColored(Util::GetControllerSecondaryColor(), "B/Y (Secondary Controller)");
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("Shift+Tab");
			ImGui::EndTable();
		}

		const auto attachMode = globals::features::vr.GetEffectiveMenuAttachMode();
		const bool useAttachedControllerForCursor =
			attachMode == VR::Settings::OverlayAttachMode::ControllerOnly ||
			attachMode == VR::Settings::OverlayAttachMode::Both;
		if (ImGui::BeginTable("ThumbstickInstructionsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
			if (globals::features::vr.CanUseWandPointing()) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextColored(Util::GetControllerPrimaryColor(), "Primary Controller Thumbstick");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("Scroll");
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextColored(Util::GetControllerSecondaryColor(), "Secondary Controller Thumbstick");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("Scroll");
			} else if (useAttachedControllerForCursor) {
				if (settings.VRMenuAttachController == ControllerDevice::Primary) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextColored(Util::GetControllerPrimaryColor(), "Primary Controller Thumbstick");
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("Mouse movement (attached controller)");
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextColored(Util::GetControllerSecondaryColor(), "Secondary Controller Thumbstick");
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("Scroll");
				} else {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextColored(Util::GetControllerPrimaryColor(), "Primary Controller Thumbstick");
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("Scroll");
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextColored(Util::GetControllerSecondaryColor(), "Secondary Controller Thumbstick");
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("Mouse movement (attached controller)");
				}
			} else {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextColored(Util::GetControllerPrimaryColor(), "Primary Controller Thumbstick");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("Mouse movement (HMD mode)");
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextColored(Util::GetControllerSecondaryColor(), "Secondary Controller Thumbstick");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("Scroll");
			}
			ImGui::EndTable();
		}

		ImGui::PopID();
	}

	void DrawControllerInputInstructions()
	{
		auto& vr = globals::features::vr;
		if (!vr.openVRInfo.isCompatible)
			return;
		if (ImGui::CollapsingHeader("Controller Input Instructions")) {
			DrawControllerBindingSummary(true, "ControllerInputInstructions");
		}
	}

	void DrawKeepDesktopWindowFocusedForVRMenuSetting()
	{
		auto& vr = globals::features::vr;
		auto& settings = vr.settings;
		if (ImGui::Checkbox("Keep Desktop Game Window Focused for VR Menu", &settings.KeepDesktopWindowFocusedForVRMenu)) {
			if (settings.KeepDesktopWindowFocusedForVRMenu) {
				vr.UpdateMenuDesktopWindowManagement(true);
			} else {
				vr.ReleaseMenuDesktopWindowManagement();
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("While the CSX menu is open in VR, keep the game window centered, foregrounded, and above other desktop windows.");
			ImGui::Text("Only applies when Attach Mode presents the menu in VR.");
			ImGui::Text("Disable this to move the game window aside or use other desktop applications while the menu stays open.");
		}
	}

	void DrawStabilizeRenderScaleDesktopMirrorSetting()
	{
		auto& settings = globals::features::vr.settings;
		const bool available = IsRenderScaleDesktopMirrorQualityAvailable();
		{
			auto guard = Util::DisableGuard(!available);
			ImGui::Checkbox("Improve Render-Scale Desktop Mirror Quality", &settings.StabilizeRenderScaleDesktopMirror);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Improves the desktop mirror image when VR Render Scale Mode lowers the source resolution.");
			ImGui::TextUnformatted("Only the desktop view changes. This can cost a little performance while render scale is active.");
			if (!available)
				ImGui::TextUnformatted("Available only while VR Render Scale Mode is active.");
		}
	}

	void DrawGeneralVRSettings()
	{
		auto& vr = globals::features::vr;
		DrawCSMenuNavigationSettings();
		DrawKeepDesktopWindowFocusedForVRMenuSetting();
		DrawStabilizeRenderScaleDesktopMirrorSetting();
		ImGui::Separator();
		if (ImGui::CollapsingHeader("General Settings")) {
			DrawDepthCullingSettings(vr, "GeneralSettings");
		}
	}

	void DrawMenuLayoutUnlockSetting()
	{
		auto& vr = globals::features::vr;
		bool layoutUnlocked = vr.settings.UnlockMenuPositionAndSize;
		if (ImGui::Checkbox("Unlock Menu Position and Size", &layoutUnlocked))
			vr.SetMenuLayoutUnlocked(layoutUnlocked);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("Allows the desktop CSX window to move, resize, and dock. In the headset, it restores custom placement and controller grip dragging. Locking the layout again preserves the saved headset settings.");
		}
	}

	void DrawMenuSettings()
	{
		auto& vr = globals::features::vr;
		auto& settings = vr.settings;
		if (!CanConfigureMenuLayout())
			return;
		if (ImGui::CollapsingHeader("Menu Settings")) {
			DrawMenuLayoutUnlockSetting();
			if (!vr.openVRInfo.isCompatible) {
				ImGui::TextDisabled("Headset controls require a compatible VR runtime; desktop layout unlocking remains available.");
				return;
			}

			if (!settings.UnlockMenuPositionAndSize) {
				ImGui::TextWrapped("The headset menu opens 2.25 metres ahead at eye height. It remains vertical and turns to face you.");
			} else {
				ImGui::SliderFloat("Menu Scale", &settings.VRMenuScale, VR::Config::kMinMenuScale, VR::Config::kMaxMenuScale, "%.2f");
				ImGui::TextWrapped("Move or resize the desktop window directly. Hold a controller grip to move the headset menu; Menu Scale controls its size.");
				ImGui::Checkbox("Enable Controller Grip Drag", &settings.EnableDragToReposition);

				const char* positioningMethods[] = { "HMD Relative", "Fixed World Position" };
				if (ImGui::Combo("Headset Positioning", &settings.VRMenuPositioningMethod, positioningMethods, IM_ARRAYSIZE(positioningMethods)) &&
					settings.VRMenuPositioningMethod == 1) {
					vr.SetFixedOverlayToCurrentHMD();
				}

				const char* attachModes[] = { "HMD Only", "Controller Only", "Both", "Desktop Only" };
				int attachMode = static_cast<int>(settings.attachMode);
				if (ImGui::Combo("Headset Presentation", &attachMode, attachModes, IM_ARRAYSIZE(attachModes))) {
					settings.attachMode = static_cast<VR::Settings::OverlayAttachMode>(attachMode);
					vr.InvalidatePresentedMenuSurfaces();
					vr.ResetMenuInputRuntimeState();
				}

				const bool showOnHMD = settings.attachMode == VR::Settings::OverlayAttachMode::HMDOnly ||
				                       settings.attachMode == VR::Settings::OverlayAttachMode::Both;
				if (showOnHMD && settings.VRMenuPositioningMethod == 0) {
					ImGui::SeparatorText("HMD-relative offset");
					ImGui::SliderFloat("Horizontal##HMDMenuOffset", &settings.VRMenuOffsetX, VR::Config::kMinMenuOffset, VR::Config::kMaxMenuOffset, "%.2f m");
					ImGui::SliderFloat("Vertical##HMDMenuOffset", &settings.VRMenuOffsetY, VR::Config::kMinMenuOffset, VR::Config::kMaxMenuOffset, "%.2f m");
					ImGui::SliderFloat("Depth##HMDMenuOffset", &settings.VRMenuOffsetZ, VR::Config::kMinMenuOffset, VR::Config::kMaxMenuOffset, "%.2f m");
				} else if (showOnHMD) {
					if (ImGui::Button("Recenter Headset Menu")) {
						vr.SetFixedOverlayToCurrentHMD();
					}
				}

				const bool showOnController = settings.attachMode == VR::Settings::OverlayAttachMode::ControllerOnly ||
				                              settings.attachMode == VR::Settings::OverlayAttachMode::Both;
				if (showOnController) {
					const char* controllers[] = { "Primary Controller", "Secondary Controller" };
					int controller = static_cast<int>(settings.VRMenuAttachController);
					if (ImGui::Combo("Attach to Controller", &controller, controllers, IM_ARRAYSIZE(controllers)))
						settings.VRMenuAttachController = static_cast<ControllerDevice>(controller);
					ImGui::SeparatorText("Controller-relative offset");
					ImGui::SliderFloat("Horizontal##ControllerMenuOffset", &settings.VRMenuControllerOffsetX, VR::Config::kMinMenuOffset, VR::Config::kMaxMenuOffset, "%.2f m");
					ImGui::SliderFloat("Vertical##ControllerMenuOffset", &settings.VRMenuControllerOffsetY, VR::Config::kMinMenuOffset, VR::Config::kMaxMenuOffset, "%.2f m");
					ImGui::SliderFloat("Depth##ControllerMenuOffset", &settings.VRMenuControllerOffsetZ, VR::Config::kMinMenuOffset, VR::Config::kMaxMenuOffset, "%.2f m");
				}
			}

			const char* menuOverlayPaths[] = { "Auto", "IVROverlay", "In-scene" };
			int menuOverlayPath = static_cast<int>(settings.menuOverlayPath);
			if (ImGui::Combo("Menu Overlay Path", &menuOverlayPath, menuOverlayPaths, IM_ARRAYSIZE(menuOverlayPaths))) {
				settings.menuOverlayPath = static_cast<VR::Settings::MenuOverlayPath>(menuOverlayPath);
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Auto uses in-scene for OpenComposite, IVROverlay for SteamVR when available.");
				ImGui::Text("Use IVROverlay only to force the compositor overlay path for troubleshooting.");
				ImGui::Text("In-scene is rendered into submitted eye textures and may appear in desktop VR mirror views.");
			}
		}
	}

	void DrawMouseSettings()
	{
		auto& vr = globals::features::vr;
		if (!vr.openVRInfo.isCompatible)
			return;
		VR::Settings& settings = vr.settings;
		if (ImGui::CollapsingHeader("Input Settings")) {
			ImGui::Text("Joystick Settings");
			ImGui::SliderFloat("Mouse Deadzone", &settings.mouseDeadzone, 0.0f, 1.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				if (vr.CanUseWandPointing()) {
					ImGui::TextUnformatted("Thumbstick deadzone for CSX menu scrolling while Wand Navigation is active.");
				} else {
					ImGui::TextUnformatted("Thumbstick deadzone for CSX menu cursor movement and scrolling while Mouse Navigation is active.");
				}
			}
			ImGui::SliderFloat("Mouse Speed", &settings.mouseSpeed, 0.1f, 50.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Speed multiplier for CSX menu cursor movement while Mouse Navigation is active.");
			}
		}
	}

	void DrawStereoSyncSettings()
	{
		const bool isVR = REL::Module::IsVR();
		auto& screenSpaceShadows = globals::features::screenSpaceShadows;
		auto& screenSpaceGI = globals::features::screenSpaceGI;
		const bool screenSpaceShadowsEnabled = isVR && screenSpaceShadows.loaded && screenSpaceShadows.bendSettings.Enable != 0;
		const bool screenSpaceGIEnabled = isVR && screenSpaceGI.loaded && screenSpaceGI.settings.Enabled;

		if (ImGui::CollapsingHeader("Screen Space Sync")) {
			auto drawSyncToggle =
				[](const char* a_label,
					bool& a_enabled,
					bool a_available,
					const char* a_summary,
					const char* a_benefit,
					const char* a_cost,
					const char* a_requirement) {
					auto guard = Util::DisableGuard(!a_available);
					ImGui::Checkbox(a_label, &a_enabled);
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::TextUnformatted(a_summary);
						ImGui::TextUnformatted(a_benefit);
						ImGui::TextUnformatted(a_cost);
						if (!a_available)
							ImGui::TextUnformatted(a_requirement);
					}
				};

			drawSyncToggle(
				"Sync Screen Space Shadows",
				screenSpaceShadows.enableStereoSync,
				screenSpaceShadowsEnabled,
				"Keeps screen-space shadows more consistent between both eyes.",
				"Can reduce VR shimmer or left/right mismatch.",
				"Costs some performance while Screen Space Shadows is active.",
				"Requires VR and active Screen Space Shadows.");
			{
				auto guard = Util::DisableGuard(!screenSpaceShadowsEnabled || !screenSpaceShadows.enableStereoSync);
				ImGui::Indent();
				ImGui::Checkbox("Reproject Screen Space Shadows", &screenSpaceShadows.useStereoReproject);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::TextUnformatted("Transfers Eye 0 (left) shadow into Eye 1 (right).");
					ImGui::TextUnformatted("Usually faster than bilateral sync because it skips the Eye 1 shadow raymarch.");
					ImGui::TextUnformatted("Eye-1-only disoccluded pixels fall back to unshadowed.");
					if (!screenSpaceShadowsEnabled)
						ImGui::TextUnformatted("Requires VR and active Screen Space Shadows.");
					else if (!screenSpaceShadows.enableStereoSync)
						ImGui::TextUnformatted("Requires Screen Space Shadows sync to be enabled.");
				}
				ImGui::Unindent();
			}
			drawSyncToggle(
				"Sync SSGI",
				screenSpaceGI.settings.EnableStereoSync,
				screenSpaceGIEnabled,
				"Keeps ambient shadowing more consistent between both eyes.",
				"Can reduce VR shimmer or left/right mismatch.",
				"Costs some performance while SSGI is active.",
				"Requires VR and active SSGI.");
			{
				auto guard = Util::DisableGuard(!screenSpaceGIEnabled || !screenSpaceGI.settings.EnableStereoSync);
				ImGui::Indent();
				ImGui::Checkbox("Reproject SSGI", &screenSpaceGI.settings.UseStereoReproject);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::TextUnformatted("Transfers exact Eye 0 (left) SSGI/AO results into Eye 1 (right).");
					ImGui::TextUnformatted("Usually faster than bilateral sync because many Eye 1 pixels skip the GI march.");
					ImGui::TextUnformatted("Falls back to the current sync path for foveated SSGI and unsupported modes.");
					ImGui::TextUnformatted("HQ Specular IL disables the reprojection optimization for full GI.");
					if (!screenSpaceGIEnabled)
						ImGui::TextUnformatted("Requires VR and active SSGI.");
					else if (!screenSpaceGI.settings.EnableStereoSync)
						ImGui::TextUnformatted("Requires SSGI sync to be enabled.");
				}
				ImGui::Unindent();
			}

			if (!isVR)
				ImGui::TextDisabled("VR-only.");
		}
	}

	void DrawStereoSettings()
	{
		DrawStereoSyncSettings();
		ImGui::Spacing();
		DrawStereoBlendSettings();
	}

	void DrawStereoBlendSettings()
	{
		auto& vr = globals::features::vr;
		auto& settings = vr.settings;
		const bool screenSpaceEffectActive = VR::AnyScreenSpaceEffectActive();
		const bool blendCanRun = settings.EnableStereoBlend && settings.StereoBlendMaxFactor > VR::Config::kMinStereoBlendMaxFactor && screenSpaceEffectActive;

		if (ImGui::CollapsingHeader("Stereo Blending")) {
			ImGui::TextWrapped("Advanced fallback for VR screen-space mismatches. It is default-off and only runs when a supported screen-space effect is active.");
			ImGui::Spacing();

			ImGui::Checkbox("Blend Between Eyes", &settings.EnableStereoBlend);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Helps hide left/right eye mismatches from some effects.");
				ImGui::TextUnformatted("Use it only if you notice artifacts, because it can cost performance.");
			}

			ImGui::Text("Available: %s", screenSpaceEffectActive ? "yes" : "no supported effect is active");
			ImGui::Text("Current state: %s", blendCanRun ? "active" : "off");
			ImGui::Spacing();

			ImGui::BeginDisabled(!settings.EnableStereoBlend);

			ImGui::SliderFloat("Max Blend Strength", &settings.StereoBlendMaxFactor, VR::Config::kMinStereoBlendMaxFactor, VR::Config::kMaxStereoBlendMaxFactor, "%.3f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Controls how strongly the two eyes are blended. Lower is safer.");
			}

			ImGui::SliderFloat("Depth Match Tolerance", &settings.StereoBlendDepthSigma, VR::Config::kMinStereoBlendDepthSigma, VR::Config::kMaxStereoBlendDepthSigma, "%.3f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Controls how closely the two eyes must match before blending.");
				ImGui::TextUnformatted("Lower values reduce halo risk.");
			}

			ImGui::SliderFloat("Color Mismatch Threshold", &settings.StereoBlendColorThreshold, VR::Config::kMinStereoBlendColorThreshold, VR::Config::kMaxStereoBlendColorThreshold, "%.3f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Only blends when the two eyes differ enough.");
				ImGui::TextUnformatted("Higher values blend less often.");
			}

			if (ImGui::Button("Reset Blending Defaults")) {
				settings.StereoBlendDepthSigma = VR::Config::kDefaultStereoBlendDepthSigma;
				settings.StereoBlendMaxFactor = VR::Config::kDefaultStereoBlendMaxFactor;
				settings.StereoBlendColorThreshold = VR::Config::kDefaultStereoBlendColorThreshold;
			}

			ImGui::EndDisabled();

			ImGui::TextDisabled("Performance: costs extra while active.");
			ImGui::Spacing();
			ImGui::TextWrapped("This is a broad fallback. Use the cost check to decide if it is worth keeping on.");
		}
	}

	void DrawFoveationSettings()
	{
		auto& vr = globals::features::vr;
		auto& settings = vr.settings;
		auto& upscaling = globals::features::upscaling;
		auto& dynamicCubemaps = globals::features::dynamicCubemaps;
		auto& screenSpaceGI = globals::features::screenSpaceGI;
		auto& screenSpaceShadows = globals::features::screenSpaceShadows;
		auto& waterEffects = globals::features::waterEffects;
		auto& wetnessEffects = globals::features::wetnessEffects;
		auto& wetterness = globals::features::wetterness;
		const bool isVR = REL::Module::IsVR();
		if (!isVR) {
			ImGui::TextDisabled("VR foveation controls are available only in VR.");
			return;
		}
		DisableDynamicCubemapVisibilityThrottleForWetterness(settings);

		auto drawSection = [](const char* a_label) {
			ImGui::Spacing();
			MenuFonts::FontRoleGuard headingFont(Menu::FontRole::Subheading);
			ImGui::SeparatorText(a_label);
		};

		auto drawDetailBudget = [](const char* a_label, bool& a_enabled, const char* a_hardCutoffLabel, bool& a_hardCutoff,
									const char* a_line0, const char* a_line1, const char* a_line2,
									const char* a_hardLine0, const char* a_hardLine1, const char* a_hardLine2) {
			auto drawTooltipLine = [](const char* a_text) {
				if (a_text && a_text[0] != '\0')
					ImGui::TextUnformatted(a_text);
			};

			ImGui::Checkbox(a_label, &a_enabled);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				drawTooltipLine(a_line0);
				drawTooltipLine(a_line1);
				drawTooltipLine(a_line2);
			}

			ImGui::BeginDisabled(!a_enabled);
			ImGui::Checkbox(a_hardCutoffLabel, &a_hardCutoff);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				drawTooltipLine(a_hardLine0);
				drawTooltipLine(a_hardLine1);
				drawTooltipLine(a_hardLine2);
			}
			ImGui::EndDisabled();
		};

		upscaling.DrawFoveatedSetupInstructions();
		drawSection("Shared FOV Mask");
		upscaling.DrawFoveatedSettings();

		const auto profile = upscaling.loaded ? upscaling.GetActiveUpscalingFoveatedProfile() : Upscaling::ActiveUpscalingFoveatedProfile{};
		const bool foveatedProfileActive = profile.available && FoveatedCommon::IsActiveCoverage(profile.sharedVisibleScale);
		const bool ssrAvailable = dynamicCubemaps.IsSSRRuntimeActive();
		const bool waterParallaxAvailable = waterEffects.loaded;
		const bool wetnessEffectsRuntimeActive = wetnessEffects.IsRuntimeActive();
		const bool wetternessFeatureAvailable = wetterness.loaded && !wetnessEffectsRuntimeActive;
		const bool wetternessSettingsAvailable = wetternessFeatureAvailable && wetterness.IsRuntimeActive();
		const bool wetternessFoveationRuntimeActive = wetterness.IsRuntimeProcessingActive() && !wetnessEffectsRuntimeActive;
		const bool screenSpaceShadowsRuntimeActive = screenSpaceShadows.loaded && screenSpaceShadows.bendSettings.Enable != 0;
		const bool screenSpaceGIFeatureAvailable = screenSpaceGI.loaded;
		const bool screenSpaceGIRuntimeActive = screenSpaceGIFeatureAvailable && screenSpaceGI.settings.Enabled;
		const bool dynamicCubemapsRuntimeActive = dynamicCubemaps.loaded;
		const bool lightingFoveationAvailable = foveatedProfileActive;
		const bool ssrFoveationAvailable = foveatedProfileActive && ssrAvailable;
		const bool waterParallaxFoveationAvailable = foveatedProfileActive && waterParallaxAvailable;
		const bool wetternessFoveationAvailable = foveatedProfileActive && wetternessSettingsAvailable;
		const bool dynamicCubemapFoveationAvailable = foveatedProfileActive && dynamicCubemapsRuntimeActive;
		const bool anySharedMaskConsumerEnabled =
			settings.EnableLightingFoveation ||
			(settings.EnableSSRFoveation && ssrAvailable) ||
			(settings.EnableWaterParallaxFoveation && waterParallaxAvailable) ||
			(settings.EnableWetternessFoveation && wetternessFoveationRuntimeActive) ||
			(settings.EnableDynamicCubemapFoveation && dynamicCubemapsRuntimeActive) ||
			(settings.EnableDynamicCubemapVisibilityThrottle && dynamicCubemapsRuntimeActive) ||
			(screenSpaceShadowsRuntimeActive && screenSpaceShadows.bendSettings.EnableFoveated != 0) ||
			(screenSpaceGIRuntimeActive && screenSpaceGI.settings.EnableFoveated);

		if (profile.available) {
			ImGui::Text("FOV mode: %s", Upscaling::GetFoveatedUpscalingModeName(profile.mode));
			if (profile.mode == Upscaling::FoveatedUpscalingMode::PeripheralTAA)
				ImGui::Text("Vendor center scale: %.2f", profile.vendorCenterScale);
			ImGui::Text("Shared visible scale: %.2f", profile.sharedVisibleScale);
			ImGui::Text("Horizontal scale: %.2f", profile.centerHorizontalScale);
			if (anySharedMaskConsumerEnabled && !foveatedProfileActive)
				ImGui::TextDisabled("Shared-mask consumers require shared visible scale below 1.00.");
		} else if (anySharedMaskConsumerEnabled) {
			ImGui::TextDisabled("Shared-mask consumers require active foveated upscaling.");
		}

		const bool ssrFoveationEnabled = settings.EnableSSRFoveation && ssrAvailable;
		const bool waterParallaxFoveationEnabled = settings.EnableWaterParallaxFoveation && waterParallaxAvailable;
		const bool wetternessFoveationEnabled = settings.EnableWetternessFoveation && wetternessFoveationRuntimeActive;
		const bool dynamicCubemapCadenceEnabled = settings.EnableDynamicCubemapFoveation && dynamicCubemapsRuntimeActive;
		const bool dynamicCubemapVisibilityEnabled = settings.EnableDynamicCubemapVisibilityThrottle && dynamicCubemapsRuntimeActive;
		const bool screenSpaceShadowsEnabled = screenSpaceShadowsRuntimeActive && screenSpaceShadows.bendSettings.EnableFoveated != 0;
		const bool screenSpaceGIEnabled = screenSpaceGIRuntimeActive && screenSpaceGI.settings.EnableFoveated;

		drawSection("Screen-Space Effects");
		ImGui::BeginDisabled(!foveatedProfileActive || !screenSpaceShadowsRuntimeActive);
		screenSpaceShadows.DrawFoveationSettings();
		ImGui::EndDisabled();
		if (!screenSpaceShadows.loaded)
			ImGui::TextDisabled("Screen Space Shadows FOV requires Screen Space Shadows.");
		else if (screenSpaceShadows.bendSettings.Enable == 0)
			ImGui::TextDisabled("Screen Space Shadows FOV requires Screen Space Shadows to be enabled.");
		ImGui::Separator();
		ImGui::BeginDisabled(!foveatedProfileActive || !screenSpaceGIRuntimeActive);
		screenSpaceGI.DrawFoveationSettings();
		ImGui::EndDisabled();
		if (!foveatedProfileActive)
			ImGui::TextDisabled("Screen-space foveation requires active foveated upscaling with shared visible scale below 1.00.");
		if (!screenSpaceGIFeatureAvailable)
			ImGui::TextDisabled("SSGI FOV requires Screen Space GI.");
		else if (!screenSpaceGI.settings.Enabled)
			ImGui::TextDisabled("SSGI FOV requires Screen Space GI to be enabled.");

		drawSection("Shader FOV");
		{
			struct FoveationToggleRef
			{
				bool available = false;
				bool* enabled = nullptr;
			};

			struct FoveationFeatureCounts
			{
				int available = 0;
				int enabled = 0;
			};

			const std::array<FoveationToggleRef, 6> boolFoveationToggles{
				FoveationToggleRef{ lightingFoveationAvailable, &settings.EnableLightingFoveation },
				FoveationToggleRef{ ssrFoveationAvailable, &settings.EnableSSRFoveation },
				FoveationToggleRef{ waterParallaxFoveationAvailable, &settings.EnableWaterParallaxFoveation },
				FoveationToggleRef{ wetternessFoveationAvailable, &settings.EnableWetternessFoveation },
				FoveationToggleRef{ dynamicCubemapFoveationAvailable, &settings.EnableDynamicCubemapFoveation },
				FoveationToggleRef{ dynamicCubemapFoveationAvailable, &settings.EnableDynamicCubemapVisibilityThrottle },
			};

			auto getFoveationFeatureCounts = [&]() {
				FoveationFeatureCounts counts{};
				auto countFoveationFeature = [&](bool a_available, bool a_enabled) {
					if (!a_available)
						return;
					++counts.available;
					if (a_enabled)
						++counts.enabled;
				};

				for (const auto& toggle : boolFoveationToggles) {
					countFoveationFeature(toggle.available, toggle.enabled && *toggle.enabled);
				}
				return counts;
			};

			FoveationFeatureCounts foveationFeatureCounts = getFoveationFeatureCounts();

			const bool anyFoveationFeatureAvailable = foveationFeatureCounts.available > 0;
			bool allAvailableFoveationFeaturesEnabled =
				anyFoveationFeatureAvailable &&
				foveationFeatureCounts.enabled == foveationFeatureCounts.available;

			{
				auto masterGuard = Util::DisableGuard(!foveatedProfileActive || !anyFoveationFeatureAvailable);
				if (ImGui::Checkbox("Toggle ALL", &allAvailableFoveationFeaturesEnabled)) {
					const bool enableFoveationFeatures = allAvailableFoveationFeaturesEnabled;
					auto applyMasterToggle = [&](const FoveationToggleRef& a_toggle) {
						if (!a_toggle.enabled)
							return;
						if (enableFoveationFeatures) {
							if (a_toggle.available)
								*a_toggle.enabled = true;
						} else {
							*a_toggle.enabled = false;
						}
					};

					for (const auto& toggle : boolFoveationToggles) {
						applyMasterToggle(toggle);
					}

					foveationFeatureCounts = getFoveationFeatureCounts();
				}
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::TextUnformatted("Turns on all available FOV performance options in this section.");
				ImGui::TextUnformatted("Screen Space Shadows and SSGI have their own FOV toggles.");
				ImGui::TextUnformatted("Turn it off to clear these options.");
			}
			ImGui::SameLine();
			if (anyFoveationFeatureAvailable)
				ImGui::TextDisabled("%d/%d available enabled", foveationFeatureCounts.enabled, foveationFeatureCounts.available);
			else
				ImGui::TextDisabled("No Shader FOV features available");
		}
		ImGui::Separator();
		if (!foveatedProfileActive)
			ImGui::TextDisabled("Lighting, SSR, Water, and Wetterness shader budgets require active foveated upscaling with shared visible scale below 1.00.");

		ImGui::BeginDisabled(!foveatedProfileActive);
		drawDetailBudget(
			"Lighting Auxiliary Detail",
			settings.EnableLightingFoveation,
			"Hard Cutoff Outside FOV##Lighting",
			settings.EnableLightingFoveationHardCutoff,
			"Reduces extra lighting detail near the edge of your VR view.",
			"Can improve performance with little change in the center.",
			"",
			"Stronger version of the setting above.",
			"Saves more performance, but edge changes may be more visible.",
			"");
		ImGui::Separator();

		ImGui::BeginDisabled(!ssrAvailable);
		drawDetailBudget(
			"SSR Raymarch",
			settings.EnableSSRFoveation,
			"Hard Cutoff Outside FOV##SSR",
			settings.EnableSSRFoveationHardCutoff,
			"Reduces reflection detail near the edge of your VR view.",
			"Can improve performance while keeping reflections strongest where you look.",
			"",
			"Stronger version of the setting above.",
			"Saves more performance, but reflections may change more at the edge.",
			"");
		ImGui::EndDisabled();
		if (!ssrAvailable) {
			ImGui::TextDisabled("SSR foveation requires Dynamic Cubemaps SSR.");
			if (dynamicCubemaps.loaded && dynamicCubemaps.settings.EnabledSSR != 0 && !dynamicCubemaps.enabledAtBoot)
				ImGui::TextDisabled("VR SSR must be enabled before startup.");
		}
		ImGui::Separator();

		ImGui::BeginDisabled(!waterParallaxAvailable);
		drawDetailBudget(
			"Water Parallax Detail",
			settings.EnableWaterParallaxFoveation,
			"Hard Cutoff Outside FOV##WaterParallax",
			settings.EnableWaterParallaxFoveationHardCutoff,
			"Reduces small water detail near the edge of your VR view.",
			"Can improve performance while keeping water detail strongest where you look.",
			"",
			"Stronger version of the setting above.",
			"Saves more performance, but water detail may change more at the edge.",
			"");
		ImGui::EndDisabled();
		if (!waterParallaxAvailable)
			ImGui::TextDisabled("Water parallax foveation requires Water Effects.");
		ImGui::Separator();

		ImGui::BeginDisabled(!wetternessSettingsAvailable);
		drawDetailBudget(
			"Wetterness Dynamic Detail",
			settings.EnableWetternessFoveation,
			"Hard Cutoff Outside FOV##Wetterness",
			settings.EnableWetternessFoveationHardCutoff,
			"Reduces small rain and wetness detail near the edge of your VR view.",
			"Can improve performance while keeping wet detail strongest where you look.",
			"",
			"Stronger version of the setting above.",
			"Saves more performance, but wet detail may change more at the edge.",
			"");
		ImGui::EndDisabled();
		if (wetnessEffectsRuntimeActive)
			ImGui::TextDisabled("Wetterness dynamic-detail foveation is only available with Wetterness. Wetness Effects is not supported.");
		else if (!wetternessFeatureAvailable)
			ImGui::TextDisabled("Wetterness dynamic-detail foveation requires Wetterness.");
		else if (!wetternessSettingsAvailable)
			ImGui::TextDisabled("Wetterness dynamic-detail foveation requires Wetterness to be enabled.");
		else if (settings.EnableWetternessFoveation && !wetternessFoveationRuntimeActive)
			ImGui::TextDisabled("Wetterness dynamic-detail foveation is idle until rain, wetness, drying, or debug overrides are active.");
		ImGui::EndDisabled();

		drawSection("Dynamic Cubemaps");
		const bool dynamicCubemapVisibilityThrottleBlockedByWetterness = IsWetternessActiveForDynamicCubemapVisibilityThrottle();
		if (dynamicCubemapVisibilityThrottleBlockedByWetterness) {
			settings.EnableDynamicCubemapVisibilityThrottle = false;
		}

		ImGui::BeginDisabled(!foveatedProfileActive || !dynamicCubemapsRuntimeActive);
		ImGui::Checkbox("Dynamic Cubemap Cadence", &settings.EnableDynamicCubemapFoveation);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Updates cubemap reflections less often when they are less visible.");
			ImGui::TextUnformatted("Can improve performance, but reflections may react a little slower.");
		}
		ImGui::EndDisabled();

		ImGui::BeginDisabled(!foveatedProfileActive || !dynamicCubemapsRuntimeActive || dynamicCubemapVisibilityThrottleBlockedByWetterness);
		ImGui::Checkbox("Low-Visibility Cubemap Throttle", &settings.EnableDynamicCubemapVisibilityThrottle);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Skips extra cubemap reflection work when it is unlikely to be noticed.");
			ImGui::TextUnformatted("Can improve performance in low-reflection scenes.");
		}
		ImGui::EndDisabled();
		if (dynamicCubemapVisibilityThrottleBlockedByWetterness)
			ImGui::TextDisabled("Low-Visibility Cubemap Throttle is disabled while Wetterness is active.");
		if (!foveatedProfileActive)
			ImGui::TextDisabled("Dynamic Cubemap foveation requires active foveated upscaling with shared visible scale below 1.00.");
		if (!dynamicCubemapsRuntimeActive)
			ImGui::TextDisabled("Dynamic Cubemap foveation requires Dynamic Cubemaps.");

		ImGui::Spacing();
		ImGui::SetNextItemOpen(false, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Status##VRFoveationStatus")) {
			const bool statusLightingActive = settings.EnableLightingFoveation && foveatedProfileActive;
			const bool statusSSRActive = ssrFoveationEnabled && foveatedProfileActive;
			const bool statusWaterParallaxActive = waterParallaxFoveationEnabled && foveatedProfileActive;
			const bool statusWetternessActive = wetternessFoveationEnabled && foveatedProfileActive;
			const bool statusCubemapCadenceActive = dynamicCubemapCadenceEnabled && foveatedProfileActive;
			const bool statusCubemapVisibilityActive = dynamicCubemapVisibilityEnabled && foveatedProfileActive;
			const bool statusScreenSpaceShadowsEnabled = screenSpaceShadowsEnabled;
			const bool statusSsgiFoveatedEnabled = screenSpaceGIEnabled;
			const auto statusLightingMode = FoveatedCommon::GetDetailMode(settings.EnableLightingFoveation, settings.EnableLightingFoveationHardCutoff);
			const auto statusSSRMode = FoveatedCommon::GetDetailMode(ssrFoveationEnabled, settings.EnableSSRFoveationHardCutoff);
			const auto statusWaterParallaxMode = FoveatedCommon::GetDetailMode(waterParallaxFoveationEnabled, settings.EnableWaterParallaxFoveationHardCutoff);
			const auto statusWetternessMode = FoveatedCommon::GetDetailMode(wetternessFoveationEnabled, settings.EnableWetternessFoveationHardCutoff);
			const bool statusAnyCubemapFoveationEnabled =
				settings.EnableDynamicCubemapFoveation ||
				settings.EnableDynamicCubemapVisibilityThrottle;
			ImGui::Text("Shared FOV area: %s", foveatedProfileActive ? "active" : profile.available ? "full visible coverage" :
																									  "unavailable");
			ImGui::Text("Lighting auxiliary detail: %s (%s)", statusLightingActive ? "active" : "inactive", FoveatedCommon::GetDetailModeName(statusLightingMode));
			ImGui::Text("SSR reflections: %s (%s)", statusSSRActive ? "active" : "inactive", FoveatedCommon::GetDetailModeName(statusSSRMode));
			ImGui::Text("Water parallax detail: %s (%s)", statusWaterParallaxActive ? "active" : "inactive", FoveatedCommon::GetDetailModeName(statusWaterParallaxMode));
			ImGui::Text("Wetterness dynamic detail: %s (%s)", statusWetternessActive ? "active" : "inactive", FoveatedCommon::GetDetailModeName(statusWetternessMode));
			ImGui::Text("Screen Space Shadows: %s", statusScreenSpaceShadowsEnabled && foveatedProfileActive ? "active" : "inactive");
			ImGui::Text("Screen Space GI: %s", statusSsgiFoveatedEnabled && foveatedProfileActive ? "active" : "inactive");
			ImGui::Text("Dynamic cubemap cadence: %s", statusCubemapCadenceActive ? "active" : "inactive");
			ImGui::Text("Dynamic cubemap visibility throttle: %s", statusCubemapVisibilityActive ? "active" : "inactive");
			if (statusAnyCubemapFoveationEnabled && !dynamicCubemaps.loaded)
				ImGui::TextDisabled("Dynamic Cubemap foveation requires Dynamic Cubemaps.");
			if (settings.EnableSSRFoveation && !ssrAvailable) {
				ImGui::TextDisabled("SSR foveation requires Dynamic Cubemaps SSR.");
				if (dynamicCubemaps.loaded && dynamicCubemaps.settings.EnabledSSR != 0 && !dynamicCubemaps.enabledAtBoot)
					ImGui::TextDisabled("VR SSR must be enabled before startup.");
			}
			if (settings.EnableWaterParallaxFoveation && !waterParallaxAvailable)
				ImGui::TextDisabled("Water parallax foveation requires Water Effects.");
			if (screenSpaceShadows.bendSettings.EnableFoveated != 0 && !screenSpaceShadowsRuntimeActive)
				ImGui::TextDisabled("Screen Space Shadows FOV requires active Screen Space Shadows.");
			if (screenSpaceGI.settings.EnableFoveated && !screenSpaceGIRuntimeActive)
				ImGui::TextDisabled("SSGI FOV requires active Screen Space GI.");
			if (settings.EnableWetternessFoveation && wetnessEffectsRuntimeActive)
				ImGui::TextDisabled("Wetterness dynamic-detail foveation is only available with Wetterness. Wetness Effects is not supported.");
			else if (settings.EnableWetternessFoveation && !wetternessFeatureAvailable)
				ImGui::TextDisabled("Wetterness dynamic-detail foveation requires Wetterness.");
			else if (settings.EnableWetternessFoveation && !wetternessSettingsAvailable)
				ImGui::TextDisabled("Wetterness dynamic-detail foveation requires Wetterness to be enabled.");
			else if (settings.EnableWetternessFoveation && !wetternessFoveationRuntimeActive)
				ImGui::TextDisabled("Wetterness dynamic-detail foveation is idle until rain, wetness, drying, or debug overrides are active.");
		}
	}

	void DrawKeyBindings()
	{
		auto& vr = globals::features::vr;
		auto& settings = vr.settings;
		struct VRKeyBindingConfig
		{
			const char* label;
			std::vector<ButtonCombo>* combos;
			VR::ComboType comboType;
			const char* description;
		};
		const std::array<VRKeyBindingConfig, 4> keyBindingConfigs = {
			VRKeyBindingConfig{ "Open CSX Menu", &settings.VRMenuOpenKeys, VR::ComboType::MenuOpen, "Open CSX settings from Main or Tween; also during gameplay with the SteamVR overlay path." },
			VRKeyBindingConfig{ "Close CSX Menu", &settings.VRMenuCloseKeys, VR::ComboType::MenuClose, "Close CSX settings while its VR menu session is open." },
			VRKeyBindingConfig{ "Show Performance Overlay", &settings.VROverlayOpenKeys, VR::ComboType::OverlayOpen, "Show the standalone performance panel during gameplay or menus." },
			VRKeyBindingConfig{ "Hide Performance Overlay", &settings.VROverlayCloseKeys, VR::ComboType::OverlayClose, "Hide the standalone performance panel during gameplay or menus." }
		};
		std::array<const char*, 4> comboTypes{};
		for (size_t i = 0; i < keyBindingConfigs.size(); ++i) {
			comboTypes[i] = keyBindingConfigs[i].label;
		}

		ImGui::TextWrapped("CSX settings bindings open the configuration menu from the Skyrim Main Menu or Tween Menu.");
		if (vr.CanOpenMenuFromWorld()) {
			ImGui::TextWrapped("The current SteamVR overlay path also allows CSX settings to open during gameplay.");
		}
		ImGui::TextWrapped("Performance Overlay bindings work during gameplay and menus, independently of the CSX settings menu.");
		ImGui::TextDisabled("Unbound actions are disabled. Save settings to persist binding changes.");
		ImGui::Spacing();

		// Combo Settings
		if (ImGui::CollapsingHeader("Combo Settings")) {
			ImGui::SliderFloat("Combo Timeout", &settings.comboTimeout, 1.0f, 10.0f, "%.1f seconds");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Time limit for recording button combinations.");
			}
		}
		ImGui::Separator();
		// Combo box for selecting which combo to record
		static int selectedComboIndex = 0;
		ImGui::Text("Select Combo to Record:");
		ImGui::SameLine();
		if (ImGui::Combo("##ComboSelector", &selectedComboIndex, comboTypes.data(), static_cast<int>(comboTypes.size()))) {
			vr.ResetComboRecordingState();
		}
		auto& selectedConfig = keyBindingConfigs[static_cast<size_t>(selectedComboIndex)];
		if (ImGui::Button("Record Selected Combo")) {
			vr.ResetComboRecordingState();
			vr.isCapturingCombo = true;
			vr.currentComboType = selectedConfig.comboType;
			vr.currentComboName = selectedConfig.label;
			vr.comboStartTime = Util::GetNowSecs();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Start recording a controller combination for the selected action.");
		}
		ImGui::SameLine();
		if (ImGui::Button("Unbind Selected Action")) {
			selectedConfig.combos->clear();
			vr.ResetComboRecordingState();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Disable the selected controller action. Save settings to persist the change.");
		}
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		// Table for displaying current key bindings
		if (ImGui::BeginTable("##VRBindingsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Action");
			ImGui::TableSetupColumn("Current Binding");
			ImGui::TableSetupColumn("Description");
			ImGui::TableHeadersRow();
			for (size_t row = 0; row < keyBindingConfigs.size(); ++row) {
				const auto& config = keyBindingConfigs[row];
				ImGui::TableNextRow();
				// Highlight the selected row
				if (row == static_cast<size_t>(selectedComboIndex)) {
					ImU32 highlight = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 0.0f, 0.15f));
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, highlight);
				}
				ImGui::TableSetColumnIndex(0);
				bool rowSelected = (row == static_cast<size_t>(selectedComboIndex));
				if (ImGui::Selectable(config.label, rowSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
					selectedComboIndex = static_cast<int>(row);
					vr.ResetComboRecordingState();
				}
				// Current Binding column
				ImGui::TableSetColumnIndex(1);
				Util::DrawButtonCombo(*config.combos, true);
				// Description column
				ImGui::TableSetColumnIndex(2);
				ImGui::TextWrapped("%s", config.description);
			}
			ImGui::EndTable();
		}
		ImGui::Spacing();
		// Reset to defaults button
		if (ImGui::Button("Reset to Defaults")) {
			settings.VRMenuOpenKeys = VR::Settings::DefaultVRMenuOpenKeys();
			settings.VRMenuCloseKeys = VR::Settings::DefaultVRMenuCloseKeys();
			settings.VROverlayOpenKeys = VR::Settings::DefaultVROverlayOpenKeys();
			settings.VROverlayCloseKeys = VR::Settings::DefaultVROverlayCloseKeys();
			vr.ResetComboRecordingState();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Reset all VR bindings. Performance Overlay actions are unbound by default to avoid accidental stick-click activation.");
		}
	}
	void DrawDebugSection()
	{
		auto& vr = globals::features::vr;
		auto& settings = vr.settings;
		auto menu = globals::menu;

		// OpenVR Version Information
		if (ImGui::CollapsingHeader("OpenVR Information")) {
			auto& info = vr.openVRInfo;
			if (info.isAvailable) {
				ImGui::Text("OpenVR System: %s", info.isCompatible ? "Active & Compatible" : "Active but INCOMPATIBLE");
				if (!info.isCompatible) {
					ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Reason: Required OpenVR system/compositor interfaces are unavailable. VR menus disabled.");
				}
				ImGui::Text("DLL Path: %s", info.dllPath.c_str());
				ImGui::Text("DLL Version: %s", info.version.c_str());
				ImGui::Text("DLL Size: %llu bytes", info.fileSize);
				ImGui::Text("Modified: %s", info.modificationTime.c_str());
				ImGui::Text("Runtime: %s", VRDetection::RuntimeTypeToString(info.runtimeType));
				ImGui::Text("Interfaces: overlay=%s system=%s compositor=%s",
					info.hasOverlayInterface ? "yes" : "no",
					info.hasSystemInterface ? "yes" : "no",
					info.hasCompositorInterface ? "yes" : "no");
				ImGui::Text("Menu Path: %s", vr.ShouldUseInSceneOverlay() ? "In-scene" : "IVROverlay");
			} else {
				ImGui::Text("OpenVR system not available");
			}
		}

		// Controller Diagnostics Section
		if (ImGui::CollapsingHeader("Controller Diagnostics")) {
			if (ImGui::Checkbox("Test Mode: Disable controller menu input (except scroll controller and triggers)", &settings.VRMenuControllerDiagnosticsTestMode)) {
				ImGui::SetScrollHereY(0.0f);  // Scroll to top of the window when toggled
			}
			ImGui::SeparatorText("Button State");
			double nowSecs = Util::GetNowSecs();
			// Get highlight color from theme
			ImVec4 highlightColor = menu->GetTheme().StatusPalette.InfoColor;
			ImU32 highlightColorU32 = ImGui::ColorConvertFloat4ToU32(highlightColor);

			// Determine display order based on handedness
			bool isLeftHanded = vr.lastKnownLeftHandedMode;  // Use cached handedness

			if (ImGui::BeginTable("vr_input_state_table", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn("Button");
				if (isLeftHanded) {
					// Left-handed: Primary (left hand) on left, Secondary (right hand) on right
					ImGui::TableSetupColumn("Primary State");
					ImGui::TableSetupColumn("Primary Held (s)");
					ImGui::TableSetupColumn("Primary Type");
					ImGui::TableSetupColumn("Secondary State");
					ImGui::TableSetupColumn("Secondary Held (s)");
					ImGui::TableSetupColumn("Secondary Type");
				} else {
					// Right-handed: Secondary (left hand) on left, Primary (right hand) on right
					ImGui::TableSetupColumn("Secondary State");
					ImGui::TableSetupColumn("Secondary Held (s)");
					ImGui::TableSetupColumn("Secondary Type");
					ImGui::TableSetupColumn("Primary State");
					ImGui::TableSetupColumn("Primary Held (s)");
					ImGui::TableSetupColumn("Primary Type");
				}
				ImGui::TableHeadersRow();
				// Helper for button type text
				auto DrawButtonType = [](const RE::ButtonState& state) {
					if (!state.isPressed) {
						if (state.IsClick())
							ImGui::TextUnformatted("Click");
						else if (state.IsHold())
							ImGui::TextUnformatted("Hold");
						else
							ImGui::TextUnformatted("-");
					} else {
						ImGui::TextUnformatted("Held");
					}
				};
				// Helper for printing a row with left/right cell highlight
				auto printRow = [&](const char* label, const RE::ButtonState& left, const RE::ButtonState& right) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(label);
					ImGui::TableSetColumnIndex(1);
					if (left.isPressed)
						ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, highlightColorU32);
					ImGui::TextUnformatted(left.isPressed ? "Pressed" : "Released");
					ImGui::TableSetColumnIndex(2);
					if (left.isPressed)
						ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, highlightColorU32);
					ImGui::Text("%.2f", left.GetCurrentHeldTime(nowSecs));
					ImGui::TableSetColumnIndex(3);
					if (left.isPressed)
						ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, highlightColorU32);
					DrawButtonType(left);
					ImGui::TableSetColumnIndex(4);
					if (right.isPressed)
						ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, highlightColorU32);
					ImGui::TextUnformatted(right.isPressed ? "Pressed" : "Released");
					ImGui::TableSetColumnIndex(5);
					if (right.isPressed)
						ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, highlightColorU32);
					ImGui::Text("%.2f", right.GetCurrentHeldTime(nowSecs));
					ImGui::TableSetColumnIndex(6);
					if (right.isPressed)
						ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, highlightColorU32);
					DrawButtonType(right);
				};

				// Helper to determine the correct order for display based on handedness
				auto printRowWithHandedness = [&](const char* label, auto key) {
					auto& primary = vr.primaryControllerState[key];
					auto& secondary = vr.secondaryControllerState[key];
					if (isLeftHanded) {
						// Left-handed: Primary (left hand) on left, Secondary (right hand) on right
						printRow(label, primary, secondary);
					} else {
						// Right-handed: Secondary (left hand) on left, Primary (right hand) on right
						printRow(label, secondary, primary);
					}
				};

				printRowWithHandedness("Trigger", RE::BSOpenVRControllerDevice::Keys::kTrigger);
				printRowWithHandedness("Grip", RE::BSOpenVRControllerDevice::Keys::kGrip);
				printRowWithHandedness("GripAlt", RE::BSOpenVRControllerDevice::Keys::kGripAlt);
				printRowWithHandedness("Stick Click", RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger);
				printRowWithHandedness("Touchpad Click", RE::BSOpenVRControllerDevice::Keys::kTouchpadClick);
				printRowWithHandedness("Touchpad Alt", RE::BSOpenVRControllerDevice::Keys::kTouchpadAlt);
				printRowWithHandedness("B/Y", RE::BSOpenVRControllerDevice::Keys::kBY);
				printRowWithHandedness("A/X", RE::BSOpenVRControllerDevice::Keys::kXA);
				ImGui::EndTable();
			}
			ImGui::SeparatorText("VR Thumbstick State");
			// Helper to draw a thumbstick quadrant visualization (returns ImVec2 for label alignment)
			auto DrawThumbstickPad = [&](float x, float y, ImU32 highlightCol) -> ImVec2 {
				ImVec2 padSize = ImVec2(80, 80);
				ImVec2 cursor = ImGui::GetCursorScreenPos();
				ImDrawList* drawList = ImGui::GetWindowDrawList();
				ImVec2 center = ImVec2(cursor.x + padSize.x / 2, cursor.y + padSize.y / 2);
				float radius = padSize.x / 2 - 4;
				ImU32 borderCol = ImGui::GetColorU32(ImGuiCol_Border);
				ImU32 axisCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
				ImU32 dotCol = ImGui::GetColorU32(ImGuiCol_Text);
				// Draw background
				drawList->AddRectFilled(cursor, ImVec2(cursor.x + padSize.x, cursor.y + padSize.y), ImGui::GetColorU32(ImGuiCol_FrameBg));
				// Draw border
				drawList->AddRect(cursor, ImVec2(cursor.x + padSize.x, cursor.y + padSize.y), borderCol, 4.0f, 0, 2.0f);
				// Draw axes
				drawList->AddLine(ImVec2(center.x, cursor.y + 4), ImVec2(center.x, cursor.y + padSize.y - 4), axisCol, 1.0f);
				drawList->AddLine(ImVec2(cursor.x + 4, center.y), ImVec2(cursor.x + padSize.x - 4, center.y), axisCol, 1.0f);
				// Determine quadrant
				int quad = 0;
				if (x > 0 && y > 0)
					quad = 1;  // top-right
				else if (x < 0 && y > 0)
					quad = 2;  // top-left
				else if (x < 0 && y < 0)
					quad = 3;  // bottom-left
				else if (x > 0 && y < 0)
					quad = 4;  // bottom-right
				// Highlight quadrant
				if (quad != 0) {
					ImVec2 q0 = center;
					ImVec2 q1 = center;
					ImVec2 q2 = center;
					ImVec2 q3 = center;
					if (quad == 1) {  // top-right
						q1.x += radius;
						q1.y -= radius;
						q2.x += radius;
						q2.y += 0;
						q3.x += 0;
						q3.y -= radius;
					} else if (quad == 2) {  // top-left
						q1.x -= radius;
						q1.y -= radius;
						q2.x -= radius;
						q2.y += 0;
						q3.x += 0;
						q3.y -= radius;
					} else if (quad == 3) {  // bottom-left
						q1.x -= radius;
						q1.y += radius;
						q2.x -= radius;
						q2.y += 0;
						q3.x += 0;
						q3.y += radius;
					} else if (quad == 4) {  // bottom-right
						q1.x += radius;
						q1.y += radius;
						q2.x += radius;
						q2.y += 0;
						q3.x += 0;
						q3.y += radius;
					}
					ImVec2 poly[4] = { center, q1, q2, q3 };
					drawList->AddConvexPolyFilled(poly, 4, highlightCol);
				}
				// Draw stick position dot
				ImVec2 dot = ImVec2(center.x + x * radius, center.y - y * radius);
				drawList->AddCircleFilled(dot, 5.0f, dotCol);
				// Return size for label alignment
				return padSize;
			};
			ImU32 highlightCol = ImGui::ColorConvertFloat4ToU32(menu->GetTheme().StatusPalette.InfoColor);
			if (ImGui::BeginTable("##VRThumbstickTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
				if (isLeftHanded) {
					// Left-handed: Primary (left hand) on left, Secondary (right hand) on right
					ImGui::TableSetupColumn("Primary Controller", ImGuiTableColumnFlags_WidthFixed, 200.0f);
					ImGui::TableSetupColumn("Secondary Controller", ImGuiTableColumnFlags_WidthFixed, 200.0f);
				} else {
					// Right-handed: Secondary (left hand) on left, Primary (right hand) on right
					ImGui::TableSetupColumn("Secondary Controller", ImGuiTableColumnFlags_WidthFixed, 200.0f);
					ImGui::TableSetupColumn("Primary Controller", ImGuiTableColumnFlags_WidthFixed, 200.0f);
				}
				ImGui::TableHeadersRow();

				// Left column content
				ImGui::TableSetColumnIndex(0);
				ImGui::BeginGroup();
				if (isLeftHanded) {
					// Left-handed: Show primary controller in left column
					ImVec2 padSizeL = DrawThumbstickPad(vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y, highlightCol);
					ImGui::Dummy(padSizeL);
					ImGui::SetNextItemWidth(160.0f);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
					ImGui::Text("X: %+1.3f  Y: %+1.3f  [%s]", vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y, RE::GetQuadrantName(vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y));
				} else {
					// Right-handed: Show secondary controller in left column
					ImVec2 padSizeL = DrawThumbstickPad(vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].x, vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].y, highlightCol);
					ImGui::Dummy(padSizeL);
					ImGui::SetNextItemWidth(160.0f);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
					ImGui::Text("X: %+1.3f  Y: %+1.3f  [%s]", vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].x, vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].y, RE::GetQuadrantName(vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].x, vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].y));
				}
				ImGui::EndGroup();

				// Right column content
				ImGui::TableSetColumnIndex(1);
				ImGui::BeginGroup();
				if (isLeftHanded) {
					// Left-handed: Show secondary controller in right column
					ImVec2 padSizeR = DrawThumbstickPad(vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].x, vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].y, highlightCol);
					ImGui::Dummy(padSizeR);
					ImGui::SetNextItemWidth(160.0f);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
					ImGui::Text("X: %+1.3f  Y: %+1.3f  [%s]", vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].x, vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].y, RE::GetQuadrantName(vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].x, vr.secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].y));
				} else {
					// Right-handed: Show primary controller in right column
					ImVec2 padSizeR = DrawThumbstickPad(vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y, highlightCol);
					ImGui::Dummy(padSizeR);
					ImGui::SetNextItemWidth(160.0f);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
					ImGui::Text("X: %+1.3f  Y: %+1.3f  [%s]", vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y, RE::GetQuadrantName(vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr.primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y));
				}
				ImGui::EndGroup();
				ImGui::EndTable();
			}
			ImGui::SeparatorText("Recent VR Controller Events");
			ImGui::TextDisabled("Note: For thumbstick events, KeyCode/Value columns show X/Y floats.");
			if (ImGui::BeginTable("eventlog", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
				ImGui::TableSetupColumn("Device", ImGuiTableColumnFlags_WidthFixed, 60.0f);
				ImGui::TableSetupColumn("KeyCode/X", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Value/Y", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Pressed", ImGuiTableColumnFlags_WidthFixed, 70.0f);
				ImGui::TableSetupColumn("Known Mapping", ImGuiTableColumnFlags_WidthFixed, 120.0f);
				ImGui::TableSetupColumn("Event Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
				ImGui::TableHeadersRow();
				for (const auto& e : vr.vrControllerEventLog) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%d", e.device);
					ImGui::TableSetColumnIndex(1);
					if (e.heldSource == "thumbstick") {
						ImGui::Text("%.3f", e.thumbstickX);
					} else {
						ImGui::Text("%d", e.keyCode);
					}
					ImGui::TableSetColumnIndex(2);
					if (e.heldSource == "thumbstick") {
						ImGui::Text("%.3f", e.thumbstickY);
					} else {
						ImGui::Text("%d", e.value);
					}
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%s", e.pressed ? "Pressed" : "Released");
					ImGui::TableSetColumnIndex(4);
					if (e.heldSource == "thumbstick") {
						ImGui::TextUnformatted(e.controllerRole.c_str());
					} else {
						ImGui::TextUnformatted(RE::GetOpenVRButtonName(e.keyCode));
					}
					ImGui::TableSetColumnIndex(5);
					if (e.heldSource == "thumbstick") {
						ImGui::TextUnformatted("-");
					} else {
						// Show click/hold for release events if available
						if (!e.pressed) {
							if (e.heldTime > 0.0) {
								if (e.heldTime < 0.5) {
									ImGui::Text("Click (%.2fs)", e.heldTime);
								} else {
									ImGui::Text("Hold (%.2fs)", e.heldTime);
								}
							} else {
								ImGui::Text("Release");
							}
						} else if (e.pressed) {
							if (e.heldTime > 0.0) {
								ImGui::Text("Held for %.2fs", e.heldTime);
							} else {
								ImGui::Text("Press");
							}
						}
					}
				}
				ImGui::EndTable();
			}

			// Wand Pointing Diagnostics
			ImGui::SeparatorText("Wand Pointing State");
			if (ImGui::BeginTable("##WandPointingState", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 200.0f);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();
				const auto controllerName = [](ControllerDevice a_controller) {
					return a_controller == ControllerDevice::Primary   ? "Primary" :
					       a_controller == ControllerDevice::Secondary ? "Secondary" :
					                                                     "None";
				};

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Navigation Source");
				ImGui::TableSetColumnIndex(1);
				if (settings.UseRuntimeDefaultMenuNavigation) {
					ImGui::Text("%s", vr.CanUseWandPointing() ? "Runtime Default (Wand)" : "Runtime Default (Mouse)");
				} else {
					ImGui::Text("%s", settings.EnableWandPointing ? "User Override (Wand)" : "User Override (Mouse)");
				}

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Wand Pointing Active");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s", vr.CanUseWandPointing() ? "Yes" : "No");

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Active Hand");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s", controllerName(vr.activeWandController));

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Captured Hand");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s", controllerName(vr.capturedWandController));

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Aim Pose Source");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s", vr.wandState.usingOCUAimPose ? "OCU OpenXR aim via render-model tip" : "Raw controller pose fallback");

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Aim Pitch Trim");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%+.1f degrees", settings.WandAimPitchTrimDegrees);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Surface Source");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s", vr.wandState.usingPresentedSurface ? "Presented world-space vertices" : "Reconstructed startup/legacy fallback");

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Hand Intersections");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text(
					"Primary: %s | Secondary: %s",
					vr.wandHandStates[0].isIntersecting ? "Hit" : "Miss",
					vr.wandHandStates[1].isIntersecting ? "Hit" : "Miss");

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Intersecting Overlay");
				ImGui::TableSetColumnIndex(1);
				if (vr.wandState.isIntersecting) {
					ImGui::TextColored(menu->GetTheme().StatusPalette.InfoColor, "YES");
				} else {
					ImGui::Text("No");
				}

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("UV Coordinates");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("(%.3f, %.3f)", vr.wandState.uvCoordinates.x, vr.wandState.uvCoordinates.y);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Controller Index");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", vr.wandState.controllerIndex);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Ray Origin");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("(%.2f, %.2f, %.2f)", vr.wandState.rayOrigin.x, vr.wandState.rayOrigin.y, vr.wandState.rayOrigin.z);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Ray Direction");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("(%.2f, %.2f, %.2f)", vr.wandState.rayDirection.x, vr.wandState.rayDirection.y, vr.wandState.rayDirection.z);

				ImGui::EndTable();
			}
		}

		// Debugging addresses for copy/paste
		if (ImGui::CollapsingHeader("OpenVR Addresses")) {
			auto openvr = RE::BSOpenVR::GetSingleton();
			auto overlay = openvr && vr.openVRInfo.hasOverlayInterface ? RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) : nullptr;
			auto vrSystem = openvr ? openvr->vrSystem : nullptr;
			ADDRESS_NODE(openvr)
			ADDRESS_NODE(overlay)
			ADDRESS_NODE(vrSystem)
		}
	}
}  // namespace

//=============================================================================
// VR-SPECIFIC PUBLIC API
//=============================================================================

bool VR::UseFixedWorldMenuPositioning() const
{
	if (IsClosedMenuStatusOverlayActive())
		return false;

	// Fixed-world mode uses OpenVR's standing tracking space, which is already
	// available at the Skyrim main menu. The first valid HMD pose establishes a
	// recoverable anchor; subsequent updates preserve translation and change yaw
	// only, so the menu faces the player without following the headset.
	const int positioningMethod = VRMenuPositioningPolicy::SelectEffectiveValue(
		settings.UnlockMenuPositionAndSize,
		settings.VRMenuPositioningMethod,
		1);
	return VRMenuPositioningPolicy::UseFixedWorld(positioningMethod);
}

VR::Settings::OverlayAttachMode VR::GetEffectiveMenuAttachMode() const
{
	if (IsClosedMenuStatusOverlayActive())
		return Settings::OverlayAttachMode::HMDOnly;

	return VRMenuPositioningPolicy::SelectEffectiveValue(
		settings.UnlockMenuPositionAndSize,
		settings.attachMode,
		Settings::OverlayAttachMode::HMDOnly);
}

ControllerDevice VR::GetEffectiveMenuAttachController() const
{
	return settings.VRMenuAttachController;
}

float VR::GetEffectiveMenuScale() const
{
	if (IsClosedMenuStatusOverlayActive())
		return Config::kDefaultMenuScale;

	return VRMenuPositioningPolicy::SelectEffectiveValue(
		settings.UnlockMenuPositionAndSize,
		settings.VRMenuScale,
		Config::kDefaultMenuScale);
}

Vector3 VR::GetEffectiveHMDMenuOffset() const
{
	if (IsClosedMenuStatusOverlayActive()) {
		return Vector3{
			Config::kDefaultHMDOffsetX,
			Config::kDefaultHMDOffsetY,
			Config::kDefaultHMDOffsetZ,
		};
	}

	return VRMenuPositioningPolicy::SelectEffectiveValue(
		settings.UnlockMenuPositionAndSize,
		Vector3{ settings.VRMenuOffsetX, settings.VRMenuOffsetY, settings.VRMenuOffsetZ },
		Vector3{ Config::kDefaultHMDOffsetX, Config::kDefaultHMDOffsetY, Config::kDefaultHMDOffsetZ });
}

Vector3 VR::GetEffectiveControllerMenuOffset() const
{
	return Vector3{
		settings.VRMenuControllerOffsetX,
		settings.VRMenuControllerOffsetY,
		settings.VRMenuControllerOffsetZ,
	};
}

void VR::SetMenuLayoutUnlocked(bool a_unlocked)
{
	if (settings.UnlockMenuPositionAndSize == a_unlocked)
		return;

	const bool preserveUnlockedFixedWorldPosition =
		settings.UnlockMenuPositionAndSize &&
		UseFixedWorldMenuPositioning() &&
		fixedWorldOverlayPosition.initialized;
	if (!a_unlocked && preserveUnlockedFixedWorldPosition)
		savedUnlockedFixedWorldOverlayPosition = fixedWorldOverlayPosition;

	settings.UnlockMenuPositionAndSize = a_unlocked;
	if (a_unlocked) {
		settings.EnableDragToReposition = true;
		if (UseFixedWorldMenuPositioning() && savedUnlockedFixedWorldOverlayPosition.initialized) {
			fixedWorldOverlayPosition = savedUnlockedFixedWorldOverlayPosition;
			fixedWorldOverlayReanchorRequested = false;
		}
	}
	settings.ClampToValidRanges();
	overlayDragState = {};
	InvalidatePresentedMenuSurfaces();
	ResetMenuInputRuntimeState();

	if (!a_unlocked) {
		SetFixedOverlayToCurrentHMD();
	}
}

void VR::UpdateVROverlayPosition()
{
	Util::OpenVRContext ctx(true);
	if (!ctx.HasOverlay())
		return;

	if (menuOverlayHandle == vr::k_ulOverlayHandleInvalid) {
		return;
	}

	// Determine positioning strategy based on settings
	const auto attachMode = GetEffectiveMenuAttachMode();
	const bool showOnController = attachMode == AttachMode::ControllerOnly || attachMode == AttachMode::Both;
	const bool showOnHMD = attachMode == AttachMode::HMDOnly || attachMode == AttachMode::Both;

	// Texture size
	float baseWidth = 1.0f;
	const float menuScale = GetEffectiveMenuScale();
	float overlayWidth = baseWidth * menuScale;
	float hmdOverlayHeight = overlayWidth * VR::Config::kHMDOverlayAspect;
	float controllerOverlayHeight = overlayWidth * VR::Config::kOverlayAspect;
	const Vector3 hmdOffset = GetEffectiveHMDMenuOffset();
	const bool showingClosedMenuOverlay = IsClosedMenuStatusOverlayActive();
	if (showingClosedMenuOverlay) {
		// Keep the status widget in view without consuming or replacing the
		// user's saved fixed-world menu anchor.
		const auto statusTransform = Util::CreateControllerOverlayTransform(
			hmdOffset.x,
			hmdOffset.y,
			hmdOffset.z,
			overlayWidth,
			hmdOverlayHeight);

		Util::SetOverlayInputFlags(ctx.overlay, menuOverlayHandle);
		ctx.overlay->SetOverlayTransformTrackedDeviceRelative(
			menuOverlayHandle,
			vr::k_unTrackedDeviceIndex_Hmd,
			&statusTransform);
		ctx.overlay->SetOverlayWidthInMeters(menuOverlayHandle, overlayWidth);
		return;
	}

	const bool useFixedWorldPositioning = UseFixedWorldMenuPositioning();
	static bool lastUsedFixedWorldPositioning = false;
	bool justSwitchedToFixed = !lastUsedFixedWorldPositioning && useFixedWorldPositioning;
	lastUsedFixedWorldPositioning = useFixedWorldPositioning;

	// Handle HMD positioning
	if (showOnHMD) {
		if (!useFixedWorldPositioning) {
			// HMD Relative positioning
			// Use a tracked-device-relative transform so the runtime owns the final head
			// motion application. That avoids the subtle frame-to-frame instability from
			// rebuilding an absolute world transform from noisy HMD poses every frame.
			vr::HmdMatrix34_t hmdRelativeTransform = Util::CreateControllerOverlayTransform(
				hmdOffset.x,
				hmdOffset.y,
				hmdOffset.z,
				overlayWidth,
				hmdOverlayHeight);

			Util::SetOverlayInputFlags(ctx.overlay, menuOverlayHandle);
			ctx.overlay->SetOverlayTransformTrackedDeviceRelative(menuOverlayHandle, vr::k_unTrackedDeviceIndex_Hmd, &hmdRelativeTransform);
			ctx.overlay->SetOverlayWidthInMeters(menuOverlayHandle, baseWidth * menuScale);
		}

		if (useFixedWorldPositioning) {
			// Fixed World Position
			if (justSwitchedToFixed) {
				SetFixedOverlayToCurrentHMD();
			}
			UpdateFixedWorldPositioning();

			// Scale the overlay based on width/height (same as relative HMD mode)
			vr::HmdMatrix34_t fixedTransform = Util::MatrixToHmdMatrix34(fixedWorldOverlayPosition.m);
			ScaleOverlayTransform(fixedTransform, overlayWidth, hmdOverlayHeight);

			Util::SetOverlayInputFlags(ctx.overlay, menuOverlayHandle);
			ctx.overlay->SetOverlayTransformAbsolute(menuOverlayHandle, vr::TrackingUniverseStanding, &fixedTransform);
			ctx.overlay->SetOverlayWidthInMeters(menuOverlayHandle, baseWidth * menuScale);
		}
	}

	// Handle controller positioning separately (can be shown alongside HMD)
	if (showOnController) {
		// Get the VR controller overlay handle from Menu.cpp
		if (menuControllerOverlayHandle == vr::k_ulOverlayHandleInvalid) {
			return;
		}

		// Attach to controller
		const auto attachController = GetEffectiveMenuAttachController();
		const Vector3 controllerOffset = GetEffectiveControllerMenuOffset();
		vr::TrackedDeviceIndex_t controllerIndex = Util::GetControllerIndexForDevice(attachController, lastKnownLeftHandedMode);

		if (controllerIndex != vr::k_unTrackedDeviceIndexInvalid) {
			// Position relative to controller using offset settings
			vr::HmdMatrix34_t transform = Util::CreateControllerOverlayTransform(
				controllerOffset.x,
				controllerOffset.y,
				controllerOffset.z,
				overlayWidth,
				controllerOverlayHeight);

			Util::SetOverlayInputFlags(ctx.overlay, menuControllerOverlayHandle);
			ctx.overlay->SetOverlayTransformTrackedDeviceRelative(menuControllerOverlayHandle, controllerIndex, &transform);

			// Update the overlay width to match the calculated size
			ctx.overlay->SetOverlayWidthInMeters(menuControllerOverlayHandle, overlayWidth);

			// Update controller overlay flags for input interaction
			Util::SetOverlayInputFlags(ctx.overlay, menuControllerOverlayHandle);
		}
	}

	// Update overlay flags for input interaction
	Util::SetOverlayInputFlags(ctx.overlay, menuOverlayHandle);
}

void VR::UpdateVROverlayControllerPosition()
{
	Util::OpenVRContext ctx(true);
	if (!ctx.HasOverlay())
		return;

	// Get the VR controller overlay handle from Menu.cpp
	if (menuControllerOverlayHandle == vr::k_ulOverlayHandleInvalid) {
		return;
	}

	// Texture size based on preset
	float baseWidth = 1.0f;
	float overlayWidth = baseWidth * GetEffectiveMenuScale();
	float overlayHeight = overlayWidth * VR::Config::kOverlayAspect;

	// Find the appropriate controller for the controller overlay
	const auto attachController = GetEffectiveMenuAttachController();
	const Vector3 controllerOffset = GetEffectiveControllerMenuOffset();
	vr::TrackedDeviceIndex_t controllerIndex = Util::GetControllerIndexForDevice(attachController, lastKnownLeftHandedMode);
	if (controllerIndex == vr::k_unTrackedDeviceIndexInvalid) {
		ctx.overlay->HideOverlay(menuControllerOverlayHandle);
		return;
	}

	// Position relative to controller using offset settings
	vr::HmdMatrix34_t transform = Util::CreateControllerOverlayTransform(
		controllerOffset.x,
		controllerOffset.y,
		controllerOffset.z,
		overlayWidth,
		overlayHeight);

	Util::SetOverlayInputFlags(ctx.overlay, menuControllerOverlayHandle);
	ctx.overlay->SetOverlayTransformTrackedDeviceRelative(menuControllerOverlayHandle, controllerIndex, &transform);

	// Update the overlay width to match the calculated size
	ctx.overlay->SetOverlayWidthInMeters(menuControllerOverlayHandle, overlayWidth);

	// Update controller overlay flags for input interaction
	Util::SetOverlayInputFlags(ctx.overlay, menuControllerOverlayHandle);
}

// Add overlay management methods for VR menu overlays
void VR::EnsureOverlayInitialized()
{
	// Check OpenVR compatibility first
	if (!openVRInfo.isCompatible) {
		logger::warn("Required OpenVR system/compositor interfaces are unavailable.");
		return;
	}

	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	static bool loggedOpenVRContext = false;
	if (!loggedOpenVRContext) {
		logger::debug("BSOpenVR: 0x{:X}", reinterpret_cast<uintptr_t>(openvr));
	}
	if (!openvr) {
		logger::error("BSOpenVR::GetSingleton() returned nullptr");
		return;
	}
	auto* vrSystem = openvr->vrSystem;
	const auto attachMode = GetEffectiveMenuAttachMode();
	const bool wantsControllerOverlay =
		attachMode == Settings::OverlayAttachMode::ControllerOnly ||
		attachMode == Settings::OverlayAttachMode::Both;
	RecreateOverlayTexturesIfNeeded(wantsControllerOverlay);

	auto* overlay = openVRInfo.hasOverlayInterface ? RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) : nullptr;
	if (!loggedOpenVRContext) {
		logger::debug("openVR->vrSystem: 0x{:X}", reinterpret_cast<uintptr_t>(vrSystem));
		logger::debug("openVR->vrContext: 0x{:X}", reinterpret_cast<uintptr_t>(&openvr->vrContext));
		logger::debug("openVR->vrContext.vrOverlay: 0x{:X}", reinterpret_cast<uintptr_t>(openvr->vrContext.vrOverlay));
		logger::debug("openVR->hmdDeviceType: {} ({})", static_cast<int>(openvr->hmdDeviceType), magic_enum::enum_name(openvr->hmdDeviceType));
		for (int i = 0; i < RE::BSVRInterface::Hand::kTotal; ++i) {
			logger::debug("openVR->controllerNodes[{}]: 0x{:X}", i, reinterpret_cast<uintptr_t>(openvr->controllerNodes[i].get()));
			if (openvr->controllerNodes[i] && reinterpret_cast<uintptr_t>(openvr->controllerNodes[i].get()) < 0x1000) {
				logger::warn("controllerNodes[{}] is suspiciously low (0x{:X})", i, reinterpret_cast<uintptr_t>(openvr->controllerNodes[i].get()));
			}
		}
		loggedOpenVRContext = true;
	}
	logger::debug("menuOverlayHandle: 0x{:X}", menuOverlayHandle);
	logger::debug("menuControllerOverlayHandle: 0x{:X}", menuControllerOverlayHandle);
	if (!overlay) {
		if (settings.menuOverlayPath == Settings::MenuOverlayPath::IVROverlay) {
			logger::error("IVROverlay is unavailable for forced IVROverlay menu path");
		} else {
			logger::debug("IVROverlay is unavailable; using in-scene menu path");
		}
		return;
	}

	auto ensureOverlayHandle = [&](const char* key, const char* name, vr::VROverlayHandle_t& handle) {
		if (handle != vr::k_ulOverlayHandleInvalid) {
			return;
		}

		vr::EVROverlayError err = overlay->FindOverlay(key, &handle);
		if (err == vr::VROverlayError_None) {
			logger::debug("FindOverlay succeeded for {}: 0x{:X}", key, handle);
			Util::SetOverlayInputFlags(overlay, handle);
			overlay->SetOverlayWidthInMeters(handle, 1.0f);
			return;
		}

		err = overlay->CreateOverlay(key, name, &handle);
		if (err == vr::VROverlayError_None) {
			logger::debug("CreateOverlay succeeded for {}: 0x{:X}", key, handle);
			Util::SetOverlayInputFlags(overlay, handle);
			overlay->SetOverlayWidthInMeters(handle, 1.0f);
			return;
		}

		handle = vr::k_ulOverlayHandleInvalid;
		logger::error("CreateOverlay failed for {}: {} ({})", key, static_cast<int>(err), magic_enum::enum_name(err));
	};

	ensureOverlayHandle(kMenuOverlayKey, kMenuOverlayName, menuOverlayHandle);
	ensureOverlayHandle(kControllerOverlayKey, kControllerOverlayName, menuControllerOverlayHandle);
}

//=============================================================================
// PRIVATE IMPLEMENTATION
//=============================================================================

void VR::DestroyOverlay()
{
	ReleaseMenuDesktopWindowManagement();

	if (!openVRInfo.hasOverlayInterface) {
		menuOverlayHandle = vr::k_ulOverlayHandleInvalid;
		menuControllerOverlayHandle = vr::k_ulOverlayHandleInvalid;
		captureIndicatorOverlayHandle = vr::k_ulOverlayHandleInvalid;
		captureIndicatorTexture = nullptr;
		return;
	}

	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* overlay = openvr ? RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) : nullptr;
	if (!overlay) {
		logger::debug("DestroyOverlay: IVROverlay is unavailable");
		menuOverlayHandle = vr::k_ulOverlayHandleInvalid;
		menuControllerOverlayHandle = vr::k_ulOverlayHandleInvalid;
		captureIndicatorOverlayHandle = vr::k_ulOverlayHandleInvalid;
		captureIndicatorTexture = nullptr;
		return;
	}
	if (menuOverlayHandle != vr::k_ulOverlayHandleInvalid) {
		overlay->DestroyOverlay(menuOverlayHandle);
		menuOverlayHandle = vr::k_ulOverlayHandleInvalid;
	}
	if (menuControllerOverlayHandle != vr::k_ulOverlayHandleInvalid) {
		overlay->DestroyOverlay(menuControllerOverlayHandle);
		menuControllerOverlayHandle = vr::k_ulOverlayHandleInvalid;
	}
	if (captureIndicatorOverlayHandle != vr::k_ulOverlayHandleInvalid) {
		overlay->DestroyOverlay(captureIndicatorOverlayHandle);
		captureIndicatorOverlayHandle = vr::k_ulOverlayHandleInvalid;
	}
	captureIndicatorTexture = nullptr;
}

bool VR::GetMenuCanvasSize(uint32_t& a_width, uint32_t& a_height) const
{
	a_width = 0;
	a_height = 0;
	const auto attachMode = GetEffectiveMenuAttachMode();
	if (!globals::game::isVR || attachMode == Settings::OverlayAttachMode::None)
		return false;

	const bool controllerCanvas = attachMode == Settings::OverlayAttachMode::ControllerOnly;
	ID3D11Texture2D* canvasTexture = controllerCanvas ? menuControllerTexture.get() : menuTexture.get();
	if (canvasTexture) {
		D3D11_TEXTURE2D_DESC desc{};
		canvasTexture->GetDesc(&desc);
		if (desc.Width > 0 && desc.Height > 0) {
			a_width = desc.Width;
			a_height = desc.Height;
			return true;
		}
	}

	if (controllerCanvas) {
		a_width = static_cast<uint32_t>(Config::kOverlayWidth);
		a_height = static_cast<uint32_t>(Config::kOverlayHeight);
	} else {
		a_width = static_cast<uint32_t>(Config::kHMDOverlayWidth);
		a_height = static_cast<uint32_t>(Config::kHMDOverlayHeight);
	}
	return true;
}

void VR::RecreateOverlayTexturesIfNeeded(bool needsControllerTexture)
{
	if (!globals::d3d::device) {
		static bool warnedMissingDevice = false;
		if (!warnedMissingDevice) {
			logger::warn("RecreateOverlayTexturesIfNeeded: D3D11 device is unavailable");
			warnedMissingDevice = true;
		}
		return;
	}

	auto isTextureValid = [](ID3D11Texture2D* texture, ID3D11RenderTargetView* rtv, int width, int height) {
		if (!texture || !rtv) {
			return false;
		}

		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);
		return desc.Width == static_cast<UINT>(width) &&
		       desc.Height == static_cast<UINT>(height) &&
		       desc.ArraySize == 1 &&
		       desc.MipLevels > 1 &&
		       (desc.MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS) != 0;
	};

	if (!isTextureValid(menuTexture.get(), menuRTV.get(), kHMDOverlayWidth, kHMDOverlayHeight)) {
		menuSamplingSRV = nullptr;
		inSceneResources.menuSRV = nullptr;
		inSceneResources.cachedMenuTexture = nullptr;
		Util::CreateOverlayTextureAndRTV(globals::d3d::device, kHMDOverlayWidth, kHMDOverlayHeight, menuTexture.put(), menuRTV.put());
	}
	if (menuTexture && !menuSamplingSRV &&
		FAILED(globals::d3d::device->CreateShaderResourceView(menuTexture.get(), nullptr, menuSamplingSRV.put()))) {
		logger::error("VR: Failed to create mipmapped HMD menu texture SRV");
	}

	if (needsControllerTexture && !isTextureValid(menuControllerTexture.get(), menuControllerRTV.get(), kOverlayWidth, kOverlayHeight)) {
		menuControllerSamplingSRV = nullptr;
		inSceneResources.menuControllerSRV = nullptr;
		inSceneResources.cachedMenuControllerTexture = nullptr;
		Util::CreateOverlayTextureAndRTV(globals::d3d::device, kOverlayWidth, kOverlayHeight, menuControllerTexture.put(), menuControllerRTV.put());
	}
	if (needsControllerTexture && menuControllerTexture && !menuControllerSamplingSRV &&
		FAILED(globals::d3d::device->CreateShaderResourceView(menuControllerTexture.get(), nullptr, menuControllerSamplingSRV.put()))) {
		logger::error("VR: Failed to create mipmapped controller menu texture SRV");
	}
}

void VR::HideAllOverlays(vr::IVROverlay* gameOverlay)
{
	if (!gameOverlay) {
		return;
	}

	if (menuOverlayHandle != vr::k_ulOverlayHandleInvalid) {
		gameOverlay->HideOverlay(menuOverlayHandle);
	}
	if (menuControllerOverlayHandle != vr::k_ulOverlayHandleInvalid) {
		gameOverlay->HideOverlay(menuControllerOverlayHandle);
	}
}

void VR::HideOverlaysIfPresent()
{
	InvalidatePresentedMenuSurfaces();
	ReleaseMenuDesktopWindowManagement();

	if (!openVRInfo.isCompatible || !openVRInfo.hasOverlayInterface) {
		return;
	}

	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* gameOverlay = openvr ? RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) : nullptr;
	HideAllOverlays(gameOverlay);
}

void VR::UpdateMenuDesktopWindowManagement(bool force)
{
	const bool menuActive = globals::menu && globals::menu->IsEnabled;
	const bool wantsVROverlay = GetEffectiveMenuAttachMode() != AttachMode::None;
	const bool shouldManage =
		settings.KeepDesktopWindowFocusedForVRMenu &&
		openVRInfo.isCompatible &&
		menuActive &&
		wantsVROverlay;

	if (!shouldManage) {
		ReleaseMenuDesktopWindowManagement();
		return;
	}

	HWND hwnd = GetGameWindowHandle();
	if (!hwnd || !IsWindow(hwnd)) {
		ReleaseMenuDesktopWindowManagement();
		return;
	}

	if (desktopWindowManagementApplied &&
		desktopWindowManagedHandle &&
		desktopWindowManagedHandle != hwnd) {
		ReleaseMenuDesktopWindowManagement();
	}

	const double nowSecs = Util::GetNowSecs();
	constexpr double kStableReassertIntervalSecs = 10.0;
	constexpr double kUnstableRetryIntervalSecs = 1.0;
	const bool windowIsTopmost = IsWindowTopmost(hwnd);
	const bool windowIsForeground = GetForegroundWindow() == hwnd;
	const bool windowStateStable = desktopWindowManagementApplied && windowIsTopmost && windowIsForeground;
	const double reassertIntervalSecs = windowStateStable ? kStableReassertIntervalSecs : kUnstableRetryIntervalSecs;
	const bool recentlyAttempted = nowSecs - lastDesktopWindowManagementAttemptSecs < reassertIntervalSecs;

	if (!force && recentlyAttempted) {
		return;
	}

	if (IsIconic(hwnd)) {
		ShowWindow(hwnd, SW_RESTORE);
	}

	if (!desktopWindowManagementApplied) {
		desktopWindowWasTopmost = windowIsTopmost;
	}

	lastDesktopWindowManagementAttemptSecs = nowSecs;

	if (CenterWindowOnCurrentMonitorTopmost(hwnd)) {
		BringWindowToTop(hwnd);
		SetForegroundWindow(hwnd);
		SetActiveWindow(hwnd);
		SetFocus(hwnd);
		desktopWindowManagementApplied = true;
		desktopWindowManagedHandle = hwnd;
	}
}

void VR::ReleaseMenuDesktopWindowManagement()
{
	if (!desktopWindowManagementApplied) {
		ResetDesktopWindowManagementState(*this);
		return;
	}

	HWND hwnd = desktopWindowManagedHandle ? desktopWindowManagedHandle : GetGameWindowHandle();
	if (hwnd && IsWindow(hwnd)) {
		if (!desktopWindowWasTopmost) {
			SetWindowPos(
				hwnd,
				HWND_NOTOPMOST,
				0,
				0,
				0,
				0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
		}
	}

	ResetDesktopWindowManagementState(*this);
}

void VR::SubmitCaptureIndicator(bool a_visible)
{
	captureIndicatorVisible.store(a_visible, std::memory_order_release);
	if (IsOpenCompositeRuntime()) {
		// OpenComposite exposes IVROverlay, but its tracked-device-relative overlays
		// are presented in Skyrim's scene space. Composite the indicator into each
		// submitted eye after capture instead, which makes its screen position
		// genuinely headset locked while keeping it out of saved frames.
		if (a_visible) {
			InstallSubmitHook();
			if (!inSceneResources.initialized) {
				InitInSceneResources();
			}
			EnsureInSceneOverlaySubmitCopyResources();
		}
		if (captureIndicatorOverlayHandle != vr::k_ulOverlayHandleInvalid) {
			if (auto* openvr = RE::BSOpenVR::GetSingleton()) {
				if (auto* overlay = RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext)) {
					overlay->HideOverlay(captureIndicatorOverlayHandle);
				}
			}
		}
		return;
	}

	if (!openVRInfo.isCompatible || !openVRInfo.hasOverlayInterface) {
		return;
	}

	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* gameOverlay = openvr ? RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) : nullptr;
	auto* cleanOverlay = RE::BSOpenVR::GetCleanIVROverlay();
	if (!gameOverlay || !cleanOverlay) {
		return;
	}

	if (!a_visible) {
		if (captureIndicatorOverlayHandle != vr::k_ulOverlayHandleInvalid) {
			gameOverlay->HideOverlay(captureIndicatorOverlayHandle);
		}
		return;
	}

	if (captureIndicatorOverlayHandle == vr::k_ulOverlayHandleInvalid) {
		auto error = gameOverlay->FindOverlay(kCaptureIndicatorOverlayKey, &captureIndicatorOverlayHandle);
		if (error != vr::VROverlayError_None) {
			error = gameOverlay->CreateOverlay(
				kCaptureIndicatorOverlayKey,
				kCaptureIndicatorOverlayName,
				&captureIndicatorOverlayHandle);
		}
		if (error != vr::VROverlayError_None) {
			captureIndicatorOverlayHandle = vr::k_ulOverlayHandleInvalid;
			logger::error(
				"Could not create the CSX capture indicator overlay: {} ({})",
				static_cast<int>(error),
				magic_enum::enum_name(error));
			return;
		}
		gameOverlay->SetOverlayWidthInMeters(captureIndicatorOverlayHandle, 0.035f);
		gameOverlay->SetOverlaySortOrder(captureIndicatorOverlayHandle, 255);

		vr::HmdMatrix34_t transform{};
		transform.m[0][0] = 1.0f;
		transform.m[1][1] = 1.0f;
		transform.m[2][2] = 1.0f;

		constexpr float kIndicatorDistanceMetres = 1.0f;
		constexpr float kFallbackHorizontalOffsetMetres = -0.25f;
		constexpr float kFallbackVerticalOffsetMetres = 0.25f;
		float horizontalOffsetMetres = kFallbackHorizontalOffsetMetres;
		float verticalOffsetMetres = kFallbackVerticalOffsetMetres;
		float depthOffsetMetres = -kIndicatorDistanceMetres;
		if (openvr->vrSystem) {
			float accumulatedTargetX = 0.0f;
			float accumulatedTargetY = 0.0f;
			float accumulatedTargetZ = 0.0f;
			std::size_t validEyeCount = 0;
			for (const auto eye : { vr::Eye_Left, vr::Eye_Right }) {
				float left = 0.0f;
				float right = 0.0f;
				float bottom = 0.0f;
				float top = 0.0f;
				openvr->vrSystem->GetProjectionRaw(eye, &left, &right, &bottom, &top);
				const float projectionWidth = right - left;
				const float projectionHeight = top - bottom;
				const auto eyeToHead = openvr->vrSystem->GetEyeToHeadTransform(eye);
				bool eyeTransformValid = true;
				for (const auto& row : eyeToHead.m) {
					for (const float value : row) {
						eyeTransformValid = eyeTransformValid && std::isfinite(value);
					}
				}
				if (std::isfinite(projectionWidth) && projectionWidth > 0.1f && projectionWidth < 10.0f &&
					std::isfinite(projectionHeight) && projectionHeight > 0.1f && projectionHeight < 10.0f &&
					eyeTransformValid) {
					const float eyeTargetX =
						(((left + right) * 0.5f) - (projectionWidth / 8.0f)) * kIndicatorDistanceMetres;
					const float eyeTargetY = ((bottom + top) * 0.5f) * kIndicatorDistanceMetres;
					const float eyeTargetZ = -kIndicatorDistanceMetres;
					accumulatedTargetX +=
						eyeToHead.m[0][0] * eyeTargetX + eyeToHead.m[0][1] * eyeTargetY +
						eyeToHead.m[0][2] * eyeTargetZ + eyeToHead.m[0][3];
					accumulatedTargetY +=
						eyeToHead.m[1][0] * eyeTargetX + eyeToHead.m[1][1] * eyeTargetY +
						eyeToHead.m[1][2] * eyeTargetZ + eyeToHead.m[1][3];
					accumulatedTargetZ +=
						eyeToHead.m[2][0] * eyeTargetX + eyeToHead.m[2][1] * eyeTargetY +
						eyeToHead.m[2][2] * eyeTargetZ + eyeToHead.m[2][3];
					++validEyeCount;
				}
			}
			if (validEyeCount != 0) {
				const float eyeCount = static_cast<float>(validEyeCount);
				horizontalOffsetMetres = accumulatedTargetX / eyeCount;
				verticalOffsetMetres = accumulatedTargetY / eyeCount;
				depthOffsetMetres = accumulatedTargetZ / eyeCount;
			}
		}

		transform.m[0][3] = horizontalOffsetMetres;
		transform.m[1][3] = verticalOffsetMetres;
		transform.m[2][3] = depthOffsetMetres;
		const auto transformError = gameOverlay->SetOverlayTransformTrackedDeviceRelative(
			captureIndicatorOverlayHandle,
			vr::k_unTrackedDeviceIndex_Hmd,
			&transform);
		if (transformError != vr::VROverlayError_None) {
			logger::error(
				"Could not position the CSX capture indicator overlay: {} ({})",
				static_cast<int>(transformError),
				magic_enum::enum_name(transformError));
			return;
		}
		logger::info(
			"VR: capture indicator positioned at optical eye line, offset=({:.3f}, {:.3f}, {:.3f}) m",
			horizontalOffsetMetres,
			verticalOffsetMetres,
			depthOffsetMetres);
	}

	if (!captureIndicatorTexture && globals::d3d::device) {
		constexpr UINT kTextureSize = 64;
		std::array<std::uint32_t, kTextureSize * kTextureSize> pixels{};
		const float centre = static_cast<float>(kTextureSize - 1) * 0.5f;
		const float radius = static_cast<float>(kTextureSize) * 0.38f;
		const float radiusSquared = radius * radius;
		for (UINT y = 0; y < kTextureSize; ++y) {
			for (UINT x = 0; x < kTextureSize; ++x) {
				const float dx = static_cast<float>(x) - centre;
				const float dy = static_cast<float>(y) - centre;
				if (dx * dx + dy * dy <= radiusSquared) {
					pixels[y * kTextureSize + x] = 0xFF2626EBu;
				}
			}
		}

		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = kTextureSize;
		desc.Height = kTextureSize;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA initialData{};
		initialData.pSysMem = pixels.data();
		initialData.SysMemPitch = kTextureSize * sizeof(std::uint32_t);
		if (FAILED(globals::d3d::device->CreateTexture2D(
				&desc,
				&initialData,
				captureIndicatorTexture.put()))) {
			logger::error("Could not create the CSX capture indicator texture");
			return;
		}
	}

	if (!captureIndicatorTexture) {
		return;
	}
	vr::Texture_t texture = {
		captureIndicatorTexture.get(),
		vr::TextureType_DirectX,
		vr::ColorSpace_Auto
	};
	const auto textureError = cleanOverlay->SetOverlayTexture(captureIndicatorOverlayHandle, &texture);
	const auto showError = textureError == vr::VROverlayError_None ?
	                           gameOverlay->ShowOverlay(captureIndicatorOverlayHandle) :
	                           textureError;
	if (textureError != vr::VROverlayError_None || showError != vr::VROverlayError_None) {
		logger::error(
			"Could not present the CSX capture indicator overlay: texture={} show={}",
			static_cast<int>(textureError),
			static_cast<int>(showError));
	}
}

void VR::SubmitOverlayFrame()
{
	// Skip overlay operations if OpenVR is incompatible
	if (!openVRInfo.isCompatible) {
		ReleaseMenuDesktopWindowManagement();
		return;
	}

	if (!globals::menu || !globals::d3d::context) {
		ReleaseMenuDesktopWindowManagement();
		return;
	}

	const bool shouldUseInSceneOverlay = ShouldUseInSceneOverlay();
	const bool presentationUpscalingActive = globals::features::upscaling.IsPresentationUpscalingActive();
	if (shouldUseInSceneOverlay || presentationUpscalingActive) {
		InstallSubmitHook();
	}
	const bool useInSceneOverlay =
		shouldUseInSceneOverlay &&
		inSceneResources.submitHookInstalled;
	const bool useIVROverlay = !useInSceneOverlay;

	if (useIVROverlay && !openVRInfo.hasOverlayInterface) {
		static bool loggedMissingOverlayInterface = false;
		if (!loggedMissingOverlayInterface) {
			logger::error("VR: IVROverlay menu path is forced, but the runtime does not expose IVROverlay");
			loggedMissingOverlayInterface = true;
		}
		ReleaseMenuDesktopWindowManagement();
		return;
	}

	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	if (!openvr || !openvr->vrSystem) {
		logger::error("SubmitOverlayFrame: BSOpenVR or vrSystem is nullptr");
		ReleaseMenuDesktopWindowManagement();
		return;
	}

	const bool hasOverlayHandles =
		menuOverlayHandle != vr::k_ulOverlayHandleInvalid ||
		menuControllerOverlayHandle != vr::k_ulOverlayHandleInvalid;
	auto* gameOverlay = openVRInfo.hasOverlayInterface && (useIVROverlay || hasOverlayHandles) ?
	                        RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) :
	                        nullptr;
	auto* cleanOverlay = useIVROverlay ? RE::BSOpenVR::GetCleanIVROverlay() : nullptr;

	static bool cleanOverlayLogged = false;
	if (useIVROverlay && !cleanOverlayLogged) {
		if (cleanOverlay) {
			logger::debug("VR: Successfully acquired clean IVROverlay interface via CommonLib: 0x{:X}", reinterpret_cast<uintptr_t>(cleanOverlay));
		} else {
			logger::error("VR: Failed to get clean IVROverlay interface via CommonLib");
		}
		cleanOverlayLogged = true;
	}

	if (useIVROverlay && (!gameOverlay || !cleanOverlay)) {
		ReleaseMenuDesktopWindowManagement();
		return;
	}

	// Update drag logic for all modes - only when overlay is visible
	auto& enabled = globals::menu->IsEnabled;
	const bool shouldRenderOverlay = ShouldPresentOverlayInHeadset();
	static bool wasMenuEnabled = false;
	const bool menuJustOpened = enabled && !wasMenuEnabled;
	const bool menuJustClosed = !enabled && wasMenuEnabled;
	wasMenuEnabled = enabled;

	if (menuJustOpened || menuJustClosed) {
		ResetMenuInputRuntimeState();
	}

	UpdateMenuDesktopWindowManagement(menuJustOpened);

	// Locked mode re-anchors on every open. If no HMD pose is available yet, the
	// first render pose completes the pending request.
	const auto attachMode = GetEffectiveMenuAttachMode();
	if (menuJustOpened &&
		VRMenuPositioningPolicy::ShouldReanchorOnOpen(settings.UnlockMenuPositionAndSize) &&
		UseFixedWorldMenuPositioning() &&
		(attachMode == AttachMode::HMDOnly || attachMode == AttachMode::Both)) {
		SetFixedOverlayToCurrentHMD();
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			savedPlayerWorldPos = player->GetPosition();
		}
	}
	UpdateOverlayDrag();

	const bool wantsHMDOverlay = attachMode == AttachMode::HMDOnly || attachMode == AttachMode::Both;
	const bool wantsControllerOverlay = attachMode == AttachMode::ControllerOnly || attachMode == AttachMode::Both;
	const bool wantsAnyVROverlay = wantsHMDOverlay || wantsControllerOverlay;

	if (shouldRenderOverlay && wantsAnyVROverlay) {
		RecreateOverlayTexturesIfNeeded((useIVROverlay || useInSceneOverlay) && wantsControllerOverlay);
	}

	if (shouldRenderOverlay && useIVROverlay && openVRInfo.hasOverlayInterface) {
		const bool missingRequiredHandles =
			(wantsHMDOverlay && menuOverlayHandle == vr::k_ulOverlayHandleInvalid) ||
			(wantsControllerOverlay && menuControllerOverlayHandle == vr::k_ulOverlayHandleInvalid);
		if (missingRequiredHandles) {
			EnsureOverlayInitialized();
		}
	}

	const bool hasMenuTexture = menuTexture.get() && menuRTV.get();
	const bool hasControllerTexture = menuControllerTexture.get() && menuControllerRTV.get();
	const bool controllerTextureRequired = wantsControllerOverlay && (useIVROverlay || useInSceneOverlay);
	const bool hasRequiredTextures = hasMenuTexture && (!controllerTextureRequired || hasControllerTexture);
	const bool hasRequiredOverlayHandles =
		(!wantsHMDOverlay || menuOverlayHandle != vr::k_ulOverlayHandleInvalid) &&
		(!wantsControllerOverlay || menuControllerOverlayHandle != vr::k_ulOverlayHandleInvalid);
	const bool canUseIVROverlay = useIVROverlay && gameOverlay && cleanOverlay && hasRequiredOverlayHandles;
	if (shouldRenderOverlay && wantsAnyVROverlay && useInSceneOverlay) {
		if (!inSceneResources.initialized) {
			InitInSceneResources();
		}
		EnsureInSceneOverlaySubmitCopyResources();
	}

	if (shouldRenderOverlay && wantsAnyVROverlay && (useInSceneOverlay || canUseIVROverlay) && hasRequiredTextures) {
		ID3D11RenderTargetView* oldRTV = nullptr;
		globals::d3d::context->OMGetRenderTargets(1, &oldRTV, nullptr);
		float clearColor[4] = { 0, 0, 0, 0 };
		ImGuiIO& io = ImGui::GetIO();
		const bool useCustomVRCursorDot = customVRCursorVisible;
		const ImVec2 customCursorPos = customVRCursorPos;
		const OverlayType customCursorOverlayType = customVRCursorOverlayType;

		auto renderImGuiToTexture = [&](ID3D11RenderTargetView* targetRTV, OverlayType targetOverlayType) {
			if (!targetRTV) {
				return;
			}

			ID3D11RenderTargetView* targetRTVPtr = targetRTV;
			globals::d3d::context->OMSetRenderTargets(1, &targetRTVPtr, nullptr);
			globals::d3d::context->ClearRenderTargetView(targetRTV, clearColor);
			const bool previousMouseDrawCursor = io.MouseDrawCursor;
			io.MouseDrawCursor = false;
			ImGui::Render();
			io.MouseDrawCursor = previousMouseDrawCursor;
			ImDrawData filteredDrawData;
			ImDrawData* renderDrawData = FilterShaderCompilationWindowFromHMD(ImGui::GetDrawData(), filteredDrawData);
			if (useCustomVRCursorDot &&
				targetOverlayType == customCursorOverlayType &&
				ShouldDrawCustomVRCursorDot(useCustomVRCursorDot, customCursorPos, io.DisplaySize) &&
				io.Fonts &&
				io.Fonts->IsBuilt()) {
				ImDrawList cursorDrawList(ImGui::GetDrawListSharedData());
				cursorDrawList._OwnerName = "CustomVRCursorDot";
				cursorDrawList._ResetForNewFrame();
				cursorDrawList.PushTextureID(io.Fonts->TexID);
				cursorDrawList.PushClipRectFullScreen();
				AppendCustomVRCursorDot(cursorDrawList, customCursorPos);
				cursorDrawList.PopClipRect();
				cursorDrawList.PopTextureID();
				cursorDrawList._PopUnusedDrawCmd();

				ImDrawData cursorAugmentedDrawData = *renderDrawData;
				cursorAugmentedDrawData.CmdLists = renderDrawData->CmdLists;
				cursorAugmentedDrawData.CmdLists.push_back(&cursorDrawList);
				cursorAugmentedDrawData.CmdListsCount = cursorAugmentedDrawData.CmdLists.Size;
				cursorAugmentedDrawData.TotalIdxCount += cursorDrawList.IdxBuffer.Size;
				cursorAugmentedDrawData.TotalVtxCount += cursorDrawList.VtxBuffer.Size;
				ImGui_ImplDX11_RenderDrawData(&cursorAugmentedDrawData);
			} else {
				ImGui_ImplDX11_RenderDrawData(renderDrawData);
			}
			globals::d3d::context->OMSetRenderTargets(1, &oldRTV, nullptr);
			ID3D11ShaderResourceView* mipSRV = targetOverlayType == OverlayType::HMD ?
			                                       menuSamplingSRV.get() :
			                                       menuControllerSamplingSRV.get();
			if (mipSRV) {
				globals::d3d::context->GenerateMips(mipSRV);
			}
		};

		renderImGuiToTexture(menuRTV.get(), OverlayType::HMD);

		const bool controllerTextureUsedByIVROverlay = useIVROverlay && wantsControllerOverlay;
		const bool controllerTextureUsedByInScene = useInSceneOverlay && wantsControllerOverlay;
		const bool shouldRenderControllerTexture =
			menuControllerTexture &&
			menuControllerRTV &&
			(controllerTextureUsedByIVROverlay || controllerTextureUsedByInScene);
		if (shouldRenderControllerTexture) {
			renderImGuiToTexture(menuControllerRTV.get(), OverlayType::Controller);
		}

		if (useInSceneOverlay) {
			// The submit hook renders menuTexture into each eye. Keep the legacy
			// IVROverlay handles hidden or the menu appears twice with a stereo offset.
			UpdateFixedWorldPositioning();
			HideAllOverlays(gameOverlay);
		} else {
			bool vrOverlayPresented = false;
			// Update overlay position and submit to SteamVR
			UpdateVROverlayPosition();
			vr::Texture_t tex = { menuTexture.get(), vr::TextureType_DirectX, vr::ColorSpace_Auto };
			if (wantsHMDOverlay) {
				Util::SetOverlayInputFlags(cleanOverlay, menuOverlayHandle);
				const vr::EVROverlayError textureError = cleanOverlay->SetOverlayTexture(menuOverlayHandle, &tex);
				if (textureError != vr::VROverlayError_None) {
					logger::error("SetOverlayTexture failed for menu overlay: {} ({})", static_cast<int>(textureError), magic_enum::enum_name(textureError));
				}
				const vr::EVROverlayError showError = cleanOverlay->ShowOverlay(menuOverlayHandle);
				if (showError != vr::VROverlayError_None) {
					logger::error("ShowOverlay failed for menu overlay: {} ({})", static_cast<int>(showError), magic_enum::enum_name(showError));
				}
				vrOverlayPresented = textureError == vr::VROverlayError_None && showError == vr::VROverlayError_None;
			} else if (menuOverlayHandle != vr::k_ulOverlayHandleInvalid) {
				cleanOverlay->HideOverlay(menuOverlayHandle);
			}
			// Controller overlay
			if (wantsControllerOverlay) {
				// Position controller overlay and submit
				UpdateVROverlayControllerPosition();

				vr::Texture_t controllerTex = { menuControllerTexture.get(), vr::TextureType_DirectX, vr::ColorSpace_Auto };
				Util::SetOverlayInputFlags(cleanOverlay, menuControllerOverlayHandle);
				const vr::EVROverlayError textureError = cleanOverlay->SetOverlayTexture(menuControllerOverlayHandle, &controllerTex);
				if (textureError != vr::VROverlayError_None) {
					logger::error("SetOverlayTexture failed for controller overlay: {} ({})", static_cast<int>(textureError), magic_enum::enum_name(textureError));
				}
				const vr::EVROverlayError showError = cleanOverlay->ShowOverlay(menuControllerOverlayHandle);
				if (showError != vr::VROverlayError_None) {
					logger::error("ShowOverlay failed for controller overlay: {} ({})", static_cast<int>(showError), magic_enum::enum_name(showError));
				}
				vrOverlayPresented = vrOverlayPresented ||
				                     (textureError == vr::VROverlayError_None && showError == vr::VROverlayError_None);
			} else if (menuControllerOverlayHandle != vr::k_ulOverlayHandleInvalid) {
				cleanOverlay->HideOverlay(menuControllerOverlayHandle);
			}
			if (vrOverlayPresented) {
				MarkAutoHideOverlayPresented();
			}
		}

		// Release oldRTV after all usage is complete to prevent use-after-free
		if (oldRTV)
			oldRTV->Release();
	} else {
		HideAllOverlays(gameOverlay);
	}
}

// Helper to centralize VR depth buffer culling logic, reducing duplication between DataLoaded, EarlyPrepass, and Settings UI.
void VR::UpdateDepthBufferCulling()
{
	if (!gDepthBufferCulling) {
		return;
	}

	const bool desired = VRDepthCullingEnablePolicy::IsEnabled(
		LocationContext::HasInteriorCell(),
		settings.EnableDepthBufferCullingExterior,
		settings.EnableDepthBufferCullingInterior);

	const bool previous = *gDepthBufferCulling;
	*gDepthBufferCulling = desired;
	VRDepthCullingTemporal::SetCullingEnabled(desired);
	using namespace VRDepthCullingCacheRefreshPolicy;
	if (!desired) {
		// A request may have been queued while background compilation was active.
		// Do not refresh after the effective location policy has switched culling off.
		depthCullingCacheRefreshPending.store(false, std::memory_order_release);
	} else if (ShouldRequest(
			desired,
			depthCullingCacheRefreshCompleted.load(std::memory_order_acquire),
			depthCullingCacheRefreshPending.load(std::memory_order_acquire))) {
		depthCullingCacheRefreshPending.store(true, std::memory_order_release);
	}

	if (previous != desired) {
		logger::info("VR depth buffer culling set to {}", desired);
	}
}

void VR::SetDepthCullingMode(VRDepthCullingTemporal::Mode a_mode)
{
	settings.DepthCullingPerformanceMode = a_mode == VRDepthCullingTemporal::Mode::Performance;
	settings.DepthCullingLegacyMode = a_mode == VRDepthCullingTemporal::Mode::Legacy;
	VRDepthCullingTemporal::SetMode(VRDepthCullingTemporal::SelectMode(
		settings.DepthCullingPerformanceMode,
		settings.DepthCullingLegacyMode));
}

VRDepthCullingTemporal::Mode VR::GetDepthCullingMode() const
{
	return VRDepthCullingTemporal::SelectMode(
		settings.DepthCullingPerformanceMode,
		settings.DepthCullingLegacyMode);
}

void VR::SetDepthCullingPerformanceMode(bool a_enabled)
{
	auto mode = GetDepthCullingMode();
	if (a_enabled)
		mode = VRDepthCullingTemporal::Mode::Performance;
	else if (mode == VRDepthCullingTemporal::Mode::Performance)
		mode = VRDepthCullingTemporal::Mode::Balanced;
	SetDepthCullingMode(mode);
}

void VR::SetDepthCullingLegacyMode(bool a_enabled)
{
	auto mode = GetDepthCullingMode();
	if (a_enabled)
		mode = VRDepthCullingTemporal::Mode::Legacy;
	else if (mode == VRDepthCullingTemporal::Mode::Legacy)
		mode = VRDepthCullingTemporal::Mode::Balanced;
	SetDepthCullingMode(mode);
}

void VR::ApplyDepthCullingMode()
{
	SetDepthCullingMode(GetDepthCullingMode());
}

void VR::TryApplyDepthBufferCullingCacheRefresh()
{
	using namespace VRDepthCullingCacheRefreshPolicy;
	auto* shaderCache = globals::shaderCache;
	if (!shaderCache)
		return;

	const VRDepthCullingCacheRefreshPolicy::State state{
		.requested = depthCullingCacheRefreshPending.load(std::memory_order_acquire),
		.diskCacheActive = shaderCache->IsDiskCacheActive(),
		.shaderCompilationActive = shaderCache->IsCompiling()
	};

	switch (SelectAction(state)) {
	case Action::None:
	case Action::WaitForCompiler:
		return;
	case Action::ConsumeWithoutRefresh:
		depthCullingCacheRefreshCompleted.store(true, std::memory_order_release);
		depthCullingCacheRefreshPending.store(false, std::memory_order_release);
		logger::info(
			"[ShaderCacheAction] action=vr-depth-culling-refresh result=not-required reason=disk-cache-inactive");
		return;
	case Action::Apply:
		if (depthCullingCacheRefreshCompleted.exchange(true, std::memory_order_acq_rel)) {
			depthCullingCacheRefreshPending.store(false, std::memory_order_release);
			return;
		}
		if (!depthCullingCacheRefreshPending.exchange(false, std::memory_order_acq_rel))
			return;

		// This is an in-memory compatibility rebind, not persistent invalidation.
		// The 2024 VR workaround fixes ImageSpace objects created before depth
		// culling reaches its effective CSX state. Keep valid disk blobs intact and
		// perform the refresh at most once per process.
		logger::info(
			"[ShaderCacheAction] action=vr-depth-culling-refresh result=apply scope=memory-imagespace diskCacheAction=none");
		shaderCache->Clear(RE::BSShader::Type::ImageSpace);
		return;
	}
}

//=============================================================================
// OPENVR VERSION DETECTION AND COMPATIBILITY
//=============================================================================

void VR::DetectOpenVRInfo()
{
	openVRInfo = VRDetection::Detect();
}

bool VR::IsOpenVRCompatible() const
{
	return openVRInfo.isAvailable && openVRInfo.isCompatible;
}
