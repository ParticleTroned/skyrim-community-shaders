#include "Menu.h"

#ifndef DIRECTINPUT_VERSION
#	define DIRECTINPUT_VERSION 0x0800
#endif
#include <Psapi.h>
#include <algorithm>
#include <cmath>
#include <dinput.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <iomanip>
#include <limits>
#include <string>
#include <vector>

#include "Deferred.h"
#include "Feature.h"
#include "FeatureIssues.h"
#include "FeatureVersions.h"
#include "Features/RenderDoc.h"
#include "Features/Upscaling.h"
#include "Menu/AdvancedSettingsRenderer.h"
#include "Menu/BackgroundBlur.h"
#include "Menu/FeatureListRenderer.h"
#include "Menu/Fonts.h"
#include "Menu/HomePageRenderer.h"
#include "Menu/IconLoader.h"
#include "Menu/MenuHeaderRenderer.h"
#include "Menu/OverlayRenderer.h"
#include "Menu/PerformanceTuningRenderer.h"
#include "Menu/SettingsTabRenderer.h"
#include "Menu/ThemeManager.h"
#include "Plugin.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"
#include "Utils/UI.h"

#include "CSEditor/EditorWindow.h"
#include "Features/CSEditor.h"
#include "Features/LightLimitFix/ParticleLights.h"
#include "Features/PerformanceOverlay.h"
#include "Features/PerformanceOverlay/ABTesting/ABTestAggregator.h"
#include "Features/PerformanceOverlay/ABTesting/ABTesting.h"
#include "Features/ScreenshotFeature.h"
#include "Features/VR.h"
#include "Features/VR/MenuPositioningPolicy.h"

#pragma comment(lib, "Psapi.lib")

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::PaletteColors,
	Background,
	Text,
	WindowBorder,
	FrameBorder,
	Separator,
	ResizeGrip)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::StatusPaletteColors,
	Disable,
	Error,
	Warning,
	RestartNeeded,
	CurrentHotkey,
	SuccessColor,
	InfoColor)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::FeatureHeadingColors,
	ColorDefault,
	ColorHovered,
	MinimizedFactor,
	FeatureTitleScale)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::ScrollbarOpacitySettings,
	Background,
	Thumb,
	ThumbHovered,
	ThumbActive)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::FontRoleSettings,
	Family,
	Style,
	File,
	SizeScale)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ImGuiStyle,
	Alpha,
	DisabledAlpha,
	WindowPadding,
	WindowRounding,
	WindowBorderSize,
	WindowMinSize,
	ChildRounding,
	ChildBorderSize,
	PopupRounding,
	PopupBorderSize,
	FramePadding,
	FrameRounding,
	FrameBorderSize,
	ItemSpacing,
	ItemInnerSpacing,
	CellPadding,
	IndentSpacing,
	ColumnsMinSpacing,
	ScrollbarSize,
	ScrollbarRounding,
	GrabMinSize,
	GrabRounding,
	LogSliderDeadzone,
	ImageRounding,
	ImageBorderSize,
	TabRounding,
	TabBorderSize,
	TabCloseButtonMinWidthSelected,
	TabCloseButtonMinWidthUnselected,
	TabBarBorderSize,
	TabBarOverlineSize,
	TableAngledHeadersAngle,
	TableAngledHeadersTextAlign,
	TreeLinesSize,
	TreeLinesRounding,
	DragDropTargetRounding,
	DragDropTargetBorderSize,
	DragDropTargetPadding,
	ColorMarkerSize,
	ColorButtonPosition,
	ButtonTextAlign,
	SelectableTextAlign,
	SeparatorTextBorderSize,
	SeparatorTextAlign,
	SeparatorTextPadding,
	DockingSeparatorSize,
	MouseCursorScale)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings,
	FontSize,
	FontName,
	GlobalScale,
	FontRoles,
	UseSimplePalette,
	ShowActionIcons,
	UseMonochromeIcons,
	UseMonochromeLogo,
	ShowFooter,
	CenterHeader,
	TooltipHoverDelay,
	BackgroundBlurEnabled,
	ScrollbarOpacity,
	Palette,
	StatusPalette,
	FeatureHeading,
	Style)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::Settings,
	ToggleKey,
	SkipCompilationKey,
	EffectToggleKey,
	OverlayToggleKey,
	ShaderBlockPrevKey,
	ShaderBlockNextKey,
	CSEditorToggleKey,
	EnableShaderBlocking,
	FirstTimeSetupCompleted,
	SkipClearCacheConfirmation,
	SmartClearShaderCacheDefault,
	BackgroundShaderCompilationOnBoot,
	ShowCompilationHUDInVR,
	AutoHideFeatureList,
	SkipConstraintWarning,
	RequireShiftToDock,
	UseResolutionFont,
	Theme,
	SelectedThemePreset)

bool IsEnabled = false;

namespace
{
	constexpr std::uint64_t kBytesPerGiB = 1024ull * 1024ull * 1024ull;
	constexpr std::uint64_t kRecommendedSystemCommitBytes = 128ull * kBytesPerGiB;

	struct SystemCommitSample
	{
		bool valid = false;
		std::uint64_t currentBytes = 0;
		std::uint64_t totalBytes = 0;
	};

	SystemCommitSample QuerySystemCommit() noexcept
	{
		PERFORMANCE_INFORMATION information{};
		information.cb = sizeof(information);
		if (!::GetPerformanceInfo(&information, sizeof(information)) || information.PageSize == 0)
			return {};

		constexpr auto maximumBytes = std::numeric_limits<std::uint64_t>::max();
		if (information.CommitTotal > maximumBytes / information.PageSize ||
			information.CommitLimit > maximumBytes / information.PageSize) {
			return {};
		}

		const auto totalBytes = static_cast<std::uint64_t>(information.CommitLimit) * information.PageSize;
		if (totalBytes == 0)
			return {};

		return {
			.valid = true,
			.currentBytes = static_cast<std::uint64_t>(information.CommitTotal) * information.PageSize,
			.totalBytes = totalBytes,
		};
	}

	double BytesToGiB(std::uint64_t a_bytes) noexcept
	{
		return static_cast<double>(a_bytes) / static_cast<double>(kBytesPerGiB);
	}

	constexpr const char* UI_MODE_SETTING_KEY = "UI Mode";
	constexpr const char* LEGACY_PERFORMANCE_UI_MODE_SETTING_KEY = "PerformanceUiMode";
	constexpr const char* SHOW_COMPILATION_HUD_IN_VR_SETTING_KEY = "ShowCompilationHUDInVR";
	constexpr const char* LEGACY_HIDE_COMPILATION_HUD_IN_VR_SETTING_KEY = "HideCompilationHUDInVR";
	constexpr int ESSENTIALS_UI_MODE = 0;
	constexpr int ADVANCED_UI_MODE = 1;

	struct SettingsWindowLayout
	{
		ImVec2 center;
		ImVec2 size;
		bool constrainedByTopStatusWindow = false;
	};

	float GetVRMenuSafePadding()
	{
		return std::max(2.0f, ThemeManager::Constants::OVERLAY_WINDOW_POSITION * Util::GetUIScale() * 0.5f);
	}

	float GetVRSettingsWindowAspect()
	{
		switch (globals::features::vr.GetEffectiveMenuAttachMode()) {
		case VR::Settings::OverlayAttachMode::ControllerOnly:
			return VR::Config::kOverlayAspect;
		default:
			return VR::Config::kHMDMenuAspect;
		}
	}

	bool ExcludeTopStatusWindowsFromLayout(ImVec2& a_availableMin, const ImVec2& a_availableMax, float& a_topStatusLeft)
	{
		const char* topStatusWindows[] = {
			"ShaderCompilationInfo",
			"UWCacheCreationInfo",
			"ShaderBlockingInfo"
		};

		bool foundTopStatusWindow = false;
		for (const char* windowName : topStatusWindows) {
			auto* statusWindow = ImGui::FindWindowByName(windowName);
			if (!statusWindow || !statusWindow->Active || statusWindow->Hidden)
				continue;

			const float statusBottom = statusWindow->Pos.y + statusWindow->Size.y + ImGui::GetStyle().ItemSpacing.y;
			if (statusBottom > a_availableMin.y)
				a_availableMin.y = std::min(statusBottom, a_availableMax.y);
			a_topStatusLeft = foundTopStatusWindow ? std::min(a_topStatusLeft, statusWindow->Pos.x) : statusWindow->Pos.x;
			foundTopStatusWindow = true;
		}

		return foundTopStatusWindow;
	}

	ImVec2 FitSizeToAspect(ImVec2 a_availableSize, float a_heightOverWidth)
	{
		a_availableSize.x = std::max(a_availableSize.x, 1.0f);
		a_availableSize.y = std::max(a_availableSize.y, 1.0f);
		a_heightOverWidth = std::isfinite(a_heightOverWidth) && a_heightOverWidth > 0.0f ? a_heightOverWidth : 1.0f;

		float width = a_availableSize.x;
		float height = width * a_heightOverWidth;
		if (height > a_availableSize.y) {
			height = a_availableSize.y;
			width = height / a_heightOverWidth;
		}

		return ImVec2(width, height);
	}

	SettingsWindowLayout GetDefaultSettingsWindowLayout()
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		if (!viewport) {
			return {
				.center = ImVec2(0.0f, 0.0f),
				.size = ImVec2(1.0f, 1.0f)
			};
		}

		const ImVec2 viewportSize = viewport->Size;
		if (REL::Module::IsVR()) {
			const float safePadding = GetVRMenuSafePadding();
			ImVec2 availableMin(
				viewport->WorkPos.x + safePadding,
				viewport->WorkPos.y + safePadding);
			ImVec2 availableMax(
				viewport->WorkPos.x + viewport->WorkSize.x - safePadding,
				viewport->WorkPos.y + viewport->WorkSize.y - safePadding);
			if (availableMax.x <= availableMin.x || availableMax.y <= availableMin.y) {
				return {
					.center = ImVec2(0.0f, 0.0f),
					.size = ImVec2(1.0f, 1.0f)
				};
			}

			float topStatusLeft = availableMin.x;
			const bool constrainedByTopStatusWindow = ExcludeTopStatusWindowsFromLayout(availableMin, availableMax, topStatusLeft);
			availableMin.y = std::min(availableMin.y, availableMax.y);

			const ImVec2 availableSpan(
				std::max(availableMax.x - availableMin.x, 0.0f),
				std::max(availableMax.y - availableMin.y, 0.0f));
			const ImVec2 size = FitSizeToAspect(availableSpan, GetVRSettingsWindowAspect());
			const float centeredLeft = (availableMin.x + availableMax.x - size.x) * 0.5f;
			const float maxLeft = std::max(availableMin.x, availableMax.x - size.x);
			const float left = constrainedByTopStatusWindow ?
			                       std::clamp(topStatusLeft, availableMin.x, maxLeft) :
			                       std::clamp(centeredLeft, availableMin.x, maxLeft);
			return {
				.center = ImVec2(left + size.x * 0.5f, (availableMin.y + availableMax.y) * 0.5f),
				.size = size,
				.constrainedByTopStatusWindow = constrainedByTopStatusWindow
			};
		}

		return {
			.center = Util::GetNativeViewportSizeScaled(0.5f),
			.size = ImVec2(viewportSize.x * 0.8f, viewportSize.y * 0.8f)
		};
	}
}

// Pad FontRoles JSON array with defaults if shorter than FontRole::Count.
// Prevents deserialization failure when loading old settings with fewer font roles.
static void SanitizeFontRolesJson(json& themeJson)
{
	if (!themeJson.contains("FontRoles") || !themeJson["FontRoles"].is_array())
		return;

	auto& fontRoles = themeJson["FontRoles"];
	const size_t expected = static_cast<size_t>(Menu::FontRole::Count);

	if (fontRoles.size() < expected) {
		auto defaults = Menu::ThemeSettings{}.FontRoles;
		for (size_t i = fontRoles.size(); i < expected; ++i) {
			fontRoles.push_back(defaults[i]);
		}
	}
}

// Serialize palette as named color map. Resilient to ImGui enum reordering.
void Menu::PaletteToJson(json& themeJson, const std::array<ImVec4, ImGuiCol_COUNT>& palette)
{
	json colors = json::object();
	for (int i = 0; i < ImGuiCol_COUNT; i++)
		colors[ImGui::GetStyleColorName(i)] = palette[i];
	themeJson["Colors"] = colors;
}

// Deserialize palette from named color map (preferred) or legacy positional array.
void Menu::PaletteFromJson(const json& themeJson, std::array<ImVec4, ImGuiCol_COUNT>& palette)
{
	ThemeSettings defaults;
	palette = defaults.FullPalette;

	auto loadVec4 = [](const json& c) -> ImVec4 {
		if (c.is_array() && c.size() >= 4)
			return c.get<ImVec4>();
		return ImVec4(0, 0, 0, 0);
	};

	if (themeJson.contains("Colors") && themeJson["Colors"].is_object()) {
		// Named color map: look up each color by ImGui's style color name
		const auto& colors = themeJson["Colors"];
		for (int i = 0; i < ImGuiCol_COUNT; i++) {
			const char* name = ImGui::GetStyleColorName(i);
			if (colors.contains(name) && colors[name].is_array())
				palette[i] = loadVec4(colors[name]);
		}
	} else if (themeJson.contains("FullPalette") && themeJson["FullPalette"].is_array()) {
		// Legacy positional array
		const auto& arr = themeJson["FullPalette"];

		if (arr.size() == 55) {
			// Migrate from ImGui 1.90 (55 entries) to 1.92+ (62 entries).
			// Tab/TabHovered swapped, 7 new slots inserted mid-enum.
			for (int i = 0; i <= 32; i++)
				palette[i] = loadVec4(arr[i]);
			// [33] InputTextCursor: stays default
			palette[34] = loadVec4(arr[34]);  // old TabHovered → TabHovered
			palette[35] = loadVec4(arr[33]);  // old Tab → Tab (swapped)
			palette[36] = loadVec4(arr[35]);  // old TabActive → TabSelected
			// [37] TabSelectedOverline: stays default
			palette[38] = loadVec4(arr[36]);  // old TabUnfocused → TabDimmed
			palette[39] = loadVec4(arr[37]);  // old TabUnfocusedActive → TabDimmedSelected
			// [40] TabDimmedSelectedOverline: stays default
			for (int i = 38; i <= 48; i++)
				palette[i + 3] = loadVec4(arr[i]);
			// [52] TextLink: stays default
			palette[53] = loadVec4(arr[49]);  // TextSelectedBg
			// [54] TreeLines: stays default
			palette[55] = loadVec4(arr[50]);  // DragDropTarget
			// [56] DragDropTargetBg: stays default
			// [57] UnsavedMarker: stays default
			for (int i = 51; i <= 54; i++)
				palette[i + 7] = loadVec4(arr[i]);
		} else {
			// Direct positional load (matching or close size)
			size_t count = std::min(arr.size(), static_cast<size_t>(ImGuiCol_COUNT));
			for (size_t i = 0; i < count; i++)
				palette[i] = loadVec4(arr[i]);
		}
	}
}

std::optional<Menu::FontRole> Menu::ResolveFontRole(std::string_view key)
{
	for (size_t i = 0; i < FontRoleDescriptors.size(); ++i) {
		if (FontRoleDescriptors[i].key == key) {
			return static_cast<FontRole>(i);
		}
	}
	return std::nullopt;
}

std::string Menu::BuildFontSignature(float baseFontSize) const
{
	return MenuFonts::BuildFontSignature(settings.Theme, baseFontSize);
}

const Menu::ThemeSettings::FontRoleSettings& Menu::GetDefaultFontRole(FontRole role)
{
	return MenuFonts::GetDefaultRole(role);
}

Menu::~Menu()
{  // Release icon textures if loaded
	uiIcons.saveSettings.Release();
	uiIcons.loadSettings.Release();
	uiIcons.deleteSettings.Release();
	uiIcons.clearCache.Release();
	uiIcons.logo.Release();
	uiIcons.featureSettingRevert.Release();
	uiIcons.applyToGame.Release();
	uiIcons.pauseTime.Release();
	uiIcons.undo.Release();
	uiIcons.discord.Release();
	uiIcons.characters.Release();
	uiIcons.display.Release();
	uiIcons.foliage.Release();
	uiIcons.lighting.Release();
	uiIcons.sky.Release();
	uiIcons.landscape.Release();
	uiIcons.water.Release();
	uiIcons.debug.Release();
	uiIcons.materials.Release();
	uiIcons.postProcessing.Release();
	uiIcons.freeCamera.Release();
	uiIcons.playMode.Release();
	uiIcons.search.Release();

	// Clean up blur resources
	BackgroundBlur::Cleanup();

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	dxgiAdapter3 = nullptr;
}

void Menu::Load(json& o_json)
{
	// Store current Theme state before loading config
	auto currentTheme = settings.Theme;

	settings = o_json;

	// The legacy negative toggle never routed compilation status to the HMD in
	// either state. Do not reinterpret its saved value as consent to show a new
	// headset overlay; existing users must explicitly opt in with the new key.
	if (!o_json.contains(SHOW_COMPILATION_HUD_IN_VR_SETTING_KEY) &&
		o_json.contains(LEGACY_HIDE_COMPILATION_HUD_IN_VR_SETTING_KEY)) {
		settings.ShowCompilationHUDInVR = false;
	}

	// Restore Theme - don't load it from config, only from theme preset files
	settings.Theme = currentTheme;

	// "PerformanceUiMode" was the original persisted name, but the setting
	// controls the entire UI's Essentials/Advanced presentation rather than
	// performance alone. Prefer the clearer user-facing key while continuing
	// to accept existing configs.
	auto loadUiMode = [&](const char* key) {
		const auto it = o_json.find(key);
		if (it == o_json.end())
			return false;

		if (!it->is_number_integer()) {
			logger::warn("Invalid '{}', expected an integer; using Essentials UI mode.", key);
			settings.UiMode = ESSENTIALS_UI_MODE;
			return true;
		}

		settings.UiMode = std::clamp(it->get<int>(), ESSENTIALS_UI_MODE, ADVANCED_UI_MODE);
		return true;
	};
	if (!loadUiMode(UI_MODE_SETTING_KEY))
		loadUiMode(LEGACY_PERFORMANCE_UI_MODE_SETTING_KEY);

	// Migration: Convert legacy uint32_t keys to InputCombo vectors if needed
	auto migrateKey = [](json& j, const char* keyName, std::vector<InputCombo>& target) {
		if (j.contains(keyName) && j[keyName].is_number_integer()) {
			uint32_t legacyKey = j[keyName].get<uint32_t>();
			target.clear();
			if (legacyKey != 0) {
				target.push_back(InputCombo::Keyboard(legacyKey));
			}
		}
	};

	migrateKey(o_json, "ToggleKey", settings.ToggleKey);
	migrateKey(o_json, "SkipCompilationKey", settings.SkipCompilationKey);
	migrateKey(o_json, "EffectToggleKey", settings.EffectToggleKey);
	migrateKey(o_json, "OverlayToggleKey", settings.OverlayToggleKey);
	migrateKey(o_json, "ShaderBlockPrevKey", settings.ShaderBlockPrevKey);
	migrateKey(o_json, "ShaderBlockNextKey", settings.ShaderBlockNextKey);
	if (!o_json.contains("CSEditorToggleKey") && o_json.contains("WeatherEditorToggleKey")) {
		o_json["CSEditorToggleKey"] = o_json["WeatherEditorToggleKey"];
	}
	migrateKey(o_json, "CSEditorToggleKey", settings.CSEditorToggleKey);
	migrateKey(o_json, "ScreenshotKey", settings.ScreenshotKey);

	// Helper for new smart serialization with error handling
	auto loadComboList = [](const json& j, const char* keyName, std::vector<InputCombo>& target) {
		if (j.contains(keyName) && j[keyName].is_array()) {
			try {
				InputCombo::ComboList::from_json(j[keyName], target);
			} catch (const std::exception& e) {
				logger::warn("Failed to load combo list '{}': {}, using default", keyName, e.what());
				// Leave target unchanged (keeps default or migrated value)
			}
		}
	};

	loadComboList(o_json, "ToggleKey", settings.ToggleKey);
	loadComboList(o_json, "SkipCompilationKey", settings.SkipCompilationKey);
	loadComboList(o_json, "EffectToggleKey", settings.EffectToggleKey);
	loadComboList(o_json, "OverlayToggleKey", settings.OverlayToggleKey);
	loadComboList(o_json, "ShaderBlockPrevKey", settings.ShaderBlockPrevKey);
	loadComboList(o_json, "ShaderBlockNextKey", settings.ShaderBlockNextKey);
	loadComboList(o_json, "CSEditorToggleKey", settings.CSEditorToggleKey);
	loadComboList(o_json, "ScreenshotKey", settings.ScreenshotKey);

	// Legacy support: If old config has Theme data and no SelectedThemePreset, load it
	if (o_json.contains("Theme") && o_json["Theme"].is_object() && settings.SelectedThemePreset.empty()) {
		bool hasFontRoles = o_json["Theme"].contains("FontRoles");
		SanitizeFontRolesJson(o_json["Theme"]);
		settings.Theme = o_json["Theme"];
		PaletteFromJson(o_json["Theme"], settings.Theme.FullPalette);
		MenuFonts::NormalizeFontRoles(settings.Theme, hasFontRoles);

		auto& bodyRole = settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)];
		if (!Util::ValidateFont(bodyRole.File)) {
			const auto& defaults = Menu::GetDefaultFontRole(FontRole::Body);
			logger::warn("Font '{}' not found while loading settings, falling back to default font '{}'",
				bodyRole.File, defaults.File);
			settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)] = defaults;
			settings.Theme.FontName = defaults.File;
		}
		logger::info("Loaded legacy Theme data from config (no SelectedThemePreset)");
	}

	// Apply Default Dark theme on first launch if no theme is selected
	if (!settings.FirstTimeSetupCompleted && settings.SelectedThemePreset.empty()) {
		// Ensure default themes are created/available
		CreateDefaultThemes();

		// Load the Default Dark theme and mark it as selected to prevent override
		if (LoadThemePreset("Default")) {
			settings.SelectedThemePreset = "Default";  // Mark as selected to prevent State::LoadTheme override
			logger::info("Applied Default Dark theme on first launch");
		} else {
			logger::warn("Failed to load Default Dark theme on first launch");
		}
	} else if (!settings.SelectedThemePreset.empty()) {
		// Load the previously selected theme preset (including custom themes)
		if (LoadThemePreset(settings.SelectedThemePreset)) {
			logger::info("Loaded saved theme preset: {}", settings.SelectedThemePreset);
		} else {
			logger::warn("Failed to load saved theme preset '{}', falling back to Default", settings.SelectedThemePreset);
			if (LoadThemePreset("Default")) {
				settings.SelectedThemePreset = "Default";
			}
		}
	}

	if (settings.BackgroundShaderCompilationOnBoot)
		globals::shaderCache->backgroundCompilation = true;
}

void Menu::Save(json& o_json)
{
	settings.Theme.FontName = settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)].File;
	settings.UiMode = std::clamp(settings.UiMode, ESSENTIALS_UI_MODE, ADVANCED_UI_MODE);

	// Save all settings except Theme values
	// Theme values should only be saved in theme preset files, not in the main config
	o_json = settings;
	o_json[UI_MODE_SETTING_KEY] = settings.UiMode;

	// Remove Theme object from config, only keep SelectedThemePreset
	o_json.erase("Theme");

	// Manually save input combos using the smart serializer
	InputCombo::ComboList::to_json(o_json["ToggleKey"], settings.ToggleKey);
	InputCombo::ComboList::to_json(o_json["SkipCompilationKey"], settings.SkipCompilationKey);
	InputCombo::ComboList::to_json(o_json["EffectToggleKey"], settings.EffectToggleKey);
	InputCombo::ComboList::to_json(o_json["OverlayToggleKey"], settings.OverlayToggleKey);
	InputCombo::ComboList::to_json(o_json["ShaderBlockPrevKey"], settings.ShaderBlockPrevKey);
	InputCombo::ComboList::to_json(o_json["ShaderBlockNextKey"], settings.ShaderBlockNextKey);
	InputCombo::ComboList::to_json(o_json["CSEditorToggleKey"], settings.CSEditorToggleKey);
	InputCombo::ComboList::to_json(o_json["ScreenshotKey"], settings.ScreenshotKey);
}

bool Menu::CaptureCurrentSettingsSnapshot(json& a_snapshot)
{
	if (!globals::state)
		return false;

	try {
		a_snapshot = json::object();
		globals::state->SaveToJson(a_snapshot, false);
		return true;
	} catch (const std::exception& e) {
		logger::warn("Could not capture current settings for unsaved-change detection: {}", e.what());
	} catch (...) {
		logger::warn("Could not capture current settings for unsaved-change detection due to an unknown error");
	}
	return false;
}

bool Menu::EnsureSettingsDirtyBaseline()
{
	if (settingsDirtyBaselineInitialized)
		return true;

	json snapshot;
	if (!CaptureCurrentSettingsSnapshot(snapshot))
		return false;

	settingsDirtyBaseline = std::move(snapshot);
	settingsDirtyBaselineInitialized = true;
	settingsDirty = false;
	return true;
}

void Menu::ResetSettingsDirtyState()
{
	settingsDirtyBaseline = json::object();
	settingsDirtyBaselineInitialized = false;
	settingsDirty = false;
	settingsDirtyCheckRequested = false;
	ClearSettingsSaveResult();
}

void Menu::ReportSettingsSaveResult(bool a_success, std::string a_message)
{
	settingsSaveMessage = std::move(a_message);
	settingsSaveMessageIsError = !a_success;
}

void Menu::ClearSettingsSaveResult()
{
	settingsSaveMessage.clear();
	settingsSaveMessageIsError = false;
}

void Menu::AcceptCurrentFeatureSettingsAsClean(const std::string& a_featureSettingsName)
{
	if (a_featureSettingsName.empty() || !EnsureSettingsDirtyBaseline())
		return;

	json currentSettings;
	if (!CaptureCurrentSettingsSnapshot(currentSettings))
		return;

	if (currentSettings.contains(a_featureSettingsName)) {
		settingsDirtyBaseline[a_featureSettingsName] = currentSettings[a_featureSettingsName];
	} else {
		settingsDirtyBaseline.erase(a_featureSettingsName);
	}
	settingsDirty = currentSettings != settingsDirtyBaseline;
	ClearSettingsSaveResult();
}

void Menu::UpdateSettingsDirtyState()
{
	if (!settingsDirtyCheckRequested)
		return;

	settingsDirtyCheckRequested = false;
	if (!EnsureSettingsDirtyBaseline())
		return;

	json currentSettings;
	if (CaptureCurrentSettingsSnapshot(currentSettings)) {
		settingsDirty = currentSettings != settingsDirtyBaseline;
		if (settingsDirty && !settingsSaveMessageIsError)
			ClearSettingsSaveResult();
	}
}

void Menu::LoadTheme(json& o_json)
{
	if (o_json["Theme"].is_object()) {
		bool hasFontRoles = o_json["Theme"].contains("FontRoles");
		SanitizeFontRolesJson(o_json["Theme"]);
		settings.Theme = o_json["Theme"];
		PaletteFromJson(o_json["Theme"], settings.Theme.FullPalette);
		MenuFonts::NormalizeFontRoles(settings.Theme, hasFontRoles);

		auto& bodyRole = settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)];
		if (!Util::ValidateFont(bodyRole.File)) {
			const auto& defaults = Menu::GetDefaultFontRole(FontRole::Body);
			logger::warn("Font '{}' not found, falling back to default font '{}'",
				bodyRole.File, defaults.File);
			settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)] = defaults;
			settings.Theme.FontName = defaults.File;
		}

		// Apply background blur enabled state from theme
		BackgroundBlur::SetEnabled(settings.Theme.BackgroundBlurEnabled);
	}
}
void Menu::SaveTheme(json& o_json)
{
	settings.Theme.FontName = settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)].File;

	if (!Util::ValidateFont(settings.Theme.FontName)) {
		const auto& defaults = Menu::GetDefaultFontRole(FontRole::Body);
		logger::warn("Font '{}' not found during save, falling back to default font '{}'",
			settings.Theme.FontName, defaults.File);
		settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)] = defaults;
		settings.Theme.FontName = defaults.File;
	}

	o_json["Theme"] = settings.Theme;
	PaletteToJson(o_json["Theme"], settings.Theme.FullPalette);
}

std::vector<std::string> Menu::DiscoverThemes()
{
	auto themeManager = ThemeManager::GetSingleton();
	if (themeManager) {
		themeManager->DiscoverThemes();
		return themeManager->GetThemeNames();
	}
	return {};
}

bool Menu::LoadThemePreset(const std::string& themeName)
{
	if (themeName.empty()) {
		// Empty theme name means custom/user theme
		settings.SelectedThemePreset = "";
		return true;
	}

	auto themeManager = ThemeManager::GetSingleton();
	json themeSettings;

	if (themeManager->LoadTheme(themeName, themeSettings)) {
		// Create a backup of current theme in case loading fails
		ThemeSettings backupTheme = settings.Theme;
		bool hasFontRoles = themeSettings.contains("FontRoles");

		SanitizeFontRolesJson(themeSettings);

		try {
			settings.Theme = themeSettings;
			PaletteFromJson(themeSettings, settings.Theme.FullPalette);

			MenuFonts::NormalizeFontRoles(settings.Theme, hasFontRoles);
			auto& bodyRole = settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)];
			if (!Util::ValidateFont(bodyRole.File)) {
				const auto& defaults = Menu::GetDefaultFontRole(FontRole::Body);
				logger::warn("Font '{}' from theme '{}' not found, falling back to default font '{}'",
					bodyRole.File, themeName, defaults.File);
				settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)] = defaults;
				settings.Theme.FontName = defaults.File;
			}

			settings.SelectedThemePreset = themeName;

			// Schedule deferred font reload if font has changed
			if (settings.Theme.FontName != cachedFontName) {
				pendingFontReload = true;
			}

			// Schedule deferred icon reload to apply theme-specific icon overrides
			pendingIconReload = true;

			// Apply background blur enabled state from theme
			BackgroundBlur::SetEnabled(settings.Theme.BackgroundBlurEnabled);

			logger::info("Applied theme preset: {}", themeName);
			return true;
		} catch (const std::exception& e) {
			logger::warn("Error loading theme '{}': {}", themeName, e.what());
			settings.Theme = backupTheme;
			return false;
		}
	} else {
		logger::warn("Failed to load theme preset: {}", themeName);
		return false;
	}
}

void Menu::CreateDefaultThemes()
{
	auto themeManager = ThemeManager::GetSingleton();
	themeManager->CreateDefaultThemeFiles();
}

void Menu::Init()
{
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	// IMPORTANT: Immediately override ImGui's default styles with our Default.json theme
	// This prevents hardcoded ImGui defaults from ever showing through
	auto* themeManager = ThemeManager::GetSingleton();
	json defaultThemeSettings;
	if (themeManager->LoadTheme("Default", defaultThemeSettings)) {
		// Temporarily create a minimal theme structure to apply defaults
		json tempSettings;
		tempSettings["Theme"] = defaultThemeSettings;
		LoadTheme(tempSettings);
		logger::info("Applied Default.json theme immediately after ImGui context creation");
	} else {
		logger::warn("Could not load Default.json theme - trying direct force application");
		// Last resort: Apply Default.json colors directly to ImGui
		ThemeManager::ForceApplyDefaultTheme();
	}

	// Re-apply user-selected preset after defaults are applied (covers Default and custom)
	if (!settings.SelectedThemePreset.empty()) {
		auto themeManagerSingleton = ThemeManager::GetSingleton();
		if (themeManagerSingleton && !themeManagerSingleton->IsDiscovered()) {
			themeManagerSingleton->DiscoverThemes();
		}

		if (!LoadThemePreset(settings.SelectedThemePreset)) {
			logger::warn("Failed to re-apply preset '{}' during Menu::Init. Keeping Default.", settings.SelectedThemePreset);
		} else {
			logger::info("Re-applied preset '{}' during Menu::Init", settings.SelectedThemePreset);
		}
	}

	auto& imgui_io = ImGui::GetIO();
	imgui_io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_DockingEnable;
	// Skyrim provides frame-sized input batches; ImGui trickling can backlog high-rate device input.
	// Consume each complete batch at the next NewFrame to prevent stale input replay.
	imgui_io.ConfigInputTrickleEventQueue = false;
	imgui_io.ConfigDockingWithShift = settings.RequireShiftToDock;
	imgui_io.BackendFlags = ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_HasGamepad;

	cachedIniPath = Util::PathHelpers::GetImGuiIniPath().string();
	imgui_io.IniFilename = cachedIniPath.c_str();

	// Register settings handler to persist display size for cross-session resolution change detection
	ImGuiSettingsHandler handler{};
	handler.TypeName = "CommunityShaders";
	handler.TypeHash = ImHashStr("CommunityShaders");
	handler.UserData = &lastDisplaySize;
	handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char*) -> void* { return (void*)1; };
	handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler* h, void*, const char* line) {
		float w, ht;
		if (sscanf(line, "DisplaySize=%f,%f", &w, &ht) == 2)
			*static_cast<float2*>(h->UserData) = { w, ht };
	};
	handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* h, ImGuiTextBuffer* buf) {
		auto& ds = ImGui::GetIO().DisplaySize;
		buf->appendf("[%s][Data]\nDisplaySize=%g,%g\n\n", h->TypeName, ds.x, ds.y);
	};
	ImGui::GetCurrentContext()->SettingsHandlers.push_back(handler);

	DXGI_SWAP_CHAIN_DESC desc{};
	globals::d3d::swapChain->GetDesc(&desc);

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(desc.OutputWindow);
	ImGui_ImplDX11_Init(globals::d3d::device, globals::d3d::context);

	ThemeManager::ReloadFont(*this, cachedFontSize);

	{
		winrt::com_ptr<IDXGIDevice> dxgiDevice;
		if (!FAILED(globals::d3d::device->QueryInterface(dxgiDevice.put()))) {
			winrt::com_ptr<IDXGIAdapter> dxgiAdapter;
			if (!FAILED(dxgiDevice->GetAdapter(dxgiAdapter.put()))) {
				dxgiAdapter->QueryInterface(dxgiAdapter3.put());
			}
		}
	}
	// Load UI icons
	if (!Util::InitializeMenuIcons(this)) {
		logger::warn("Menu::Init() - Failed to load UI icons. Will fallback to text buttons");
	}

	// Initialize background blur system
	if (!BackgroundBlur::Initialize()) {
		logger::warn("Menu::Init() - Failed to initialize background blur system");
	}

	if (globals::features::vr.IsOpenVRCompatible()) {
		globals::features::vr.EnsureOverlayInitialized();
	}

	initialized = true;
}

/**
 * @brief Main UI rendering coordinator for the CSX menu
 *
 * This method serves as the primary entry point for rendering the entire menu interface.
 * It handles window setup, docking configuration, and delegates rendering to specialized
 * renderer components for better separation of concerns.
 *
 * The method manages:
 * - ImGui docking space and window positioning
 * - Focus change handling
 * - Dynamic window flags based on docking state
 * - Header, navigation tabs, and settings panels coordination
 */
void Menu::DrawSettings()
{
	EnsureSettingsDirtyBaseline();

	if (focusChanged) {
		OnFocusChanged();
		focusChanged = false;
	}

	// Apply theme styling with universal contrast enhancement
	ThemeManager::SetupImGuiStyle(*this);
	ImGui::GetIO().ConfigDockingWithShift = settings.RequireShiftToDock;

	const bool useOpenCompositeStableHeader =
		REL::Module::IsVR() &&
		globals::features::vr.openVRInfo.runtimeType == VRDetection::RuntimeType::OpenComposite;
	const bool useSteamVRWindowControls =
		REL::Module::IsVR() &&
		globals::features::vr.openVRInfo.isAvailable &&
		globals::features::vr.openVRInfo.runtimeType == VRDetection::RuntimeType::SteamVR;
	const bool vrMenuLayoutUnlocked = globals::features::vr.settings.UnlockMenuPositionAndSize;
	if (useSteamVRWindowControls) {
		ImGui::GetIO().ConfigDockingWithShift = false;
	}
	const ImGuiID mainDockSpaceId = ImGui::DockSpaceOverViewport(0, NULL, ImGuiDockNodeFlags_PassthruCentralNode);

	auto baseTitle = std::string{ Plugin::MENU_TITLE };
	// Use ### to keep a stable window ID regardless of build suffix, preserving docking state
	auto title = std::format("{}###CommunityShaders", baseTitle);

	// Check if this will be docked (we need to peek at the docking state)
	static bool wasDocked = false;
	static bool steamVRUndockedFirstOpenLayoutApplied = false;
	bool willBeDocked = wasDocked;

	const auto layoutCond = resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
	const SettingsWindowLayout defaultWindowLayout = GetDefaultSettingsWindowLayout();
	const ImVec2 defaultWindowPos = defaultWindowLayout.center;
	const ImVec2 defaultWindowSize = defaultWindowLayout.size;
	const ImVec2 centeredPivot(0.5f, 0.5f);
	ImVec2 windowPos = defaultWindowPos;
	ImVec2 windowSizeForOverlap = defaultWindowSize;
	bool repairSteamVRLegacyWindowSize = false;
	static bool steamVRLegacyWindowSizeRepaired = false;
	if (auto* menuWin = ImGui::FindWindowByName(title.c_str())) {
		willBeDocked = menuWin->DockIsActive;
		if (menuWin->Size.x > 0.0f && menuWin->Size.y > 0.0f) {
			windowPos = ImVec2(menuWin->Pos.x + menuWin->Size.x * centeredPivot.x, menuWin->Pos.y + menuWin->Size.y * centeredPivot.y);
			windowSizeForOverlap = menuWin->Size;
			const float windowAspect = menuWin->Size.y / menuWin->Size.x;
			constexpr float kLegacyAspectRepairTolerance = 0.25f;
			repairSteamVRLegacyWindowSize =
				useSteamVRWindowControls &&
				!vrMenuLayoutUnlocked &&
				!willBeDocked &&
				!steamVRLegacyWindowSizeRepaired &&
				(std::abs(windowAspect - GetVRSettingsWindowAspect()) > kLegacyAspectRepairTolerance);
			if (repairSteamVRLegacyWindowSize) {
				steamVRLegacyWindowSizeRepaired = true;
				windowSizeForOverlap = defaultWindowSize;
			}
		}
	}

	// SteamVR's undocked first open should start from the current default centered
	// layout instead of inheriting stale saved placement from an older layout.
	const bool forceSteamVRFirstUndockedLayout =
		useSteamVRWindowControls &&
		!vrMenuLayoutUnlocked &&
		!willBeDocked &&
		!steamVRUndockedFirstOpenLayoutApplied;
	if (forceSteamVRFirstUndockedLayout) {
		windowPos = defaultWindowPos;
		windowSizeForOverlap = defaultWindowSize;
	}

	static bool menuWasOffsetForTopStatusWindow = false;
	static bool vrTopStatusWindowLayoutWasActive = false;
	static ImVec2 preTopStatusWindowPos;
	bool restoreAfterTopStatusWindow = false;
	bool autoOffsetForTopStatusWindow = false;
	if (!willBeDocked) {
		const bool useVRTopStatusWindowLayout =
			REL::Module::IsVR() &&
			!vrMenuLayoutUnlocked &&
			defaultWindowLayout.constrainedByTopStatusWindow;
		if (useVRTopStatusWindowLayout || (REL::Module::IsVR() && vrTopStatusWindowLayoutWasActive)) {
			windowPos = defaultWindowPos;
			windowSizeForOverlap = defaultWindowSize;
			autoOffsetForTopStatusWindow = useVRTopStatusWindowLayout;
			restoreAfterTopStatusWindow = !useVRTopStatusWindowLayout && vrTopStatusWindowLayoutWasActive;
			vrTopStatusWindowLayoutWasActive = useVRTopStatusWindowLayout;
		} else if (!REL::Module::IsVR()) {
			const ImVec2 originalWindowPos = windowPos;
			autoOffsetForTopStatusWindow =
				OverlayRenderer::MoveWindowBelowShaderCompilationStatus(windowPos, windowSizeForOverlap, centeredPivot);
			if (autoOffsetForTopStatusWindow && !menuWasOffsetForTopStatusWindow) {
				preTopStatusWindowPos = originalWindowPos;
				menuWasOffsetForTopStatusWindow = true;
			} else if (!autoOffsetForTopStatusWindow && menuWasOffsetForTopStatusWindow) {
				windowPos = preTopStatusWindowPos;
				restoreAfterTopStatusWindow = true;
				menuWasOffsetForTopStatusWindow = false;
			}
		}
		if (REL::Module::IsVR()) {
			menuWasOffsetForTopStatusWindow = false;
		}
	} else {
		menuWasOffsetForTopStatusWindow = false;
		vrTopStatusWindowLayoutWasActive = false;
	}

	const bool lockVRMenuToCanvas = VRMenuPositioningPolicy::ShouldLockDesktopCanvas(
		REL::Module::IsVR(),
		vrMenuLayoutUnlocked);
	if (lockVRMenuToCanvas) {
		windowPos = defaultWindowPos;
		windowSizeForOverlap = defaultWindowSize;
		willBeDocked = false;
		if (auto* existingWindow = ImGui::FindWindowByName(title.c_str());
			existingWindow && existingWindow->DockIsActive) {
			// A docked window must be detached before fixed canvas geometry can apply.
			ImGui::DockContextProcessUndockWindow(ImGui::GetCurrentContext(), existingWindow);
		}
	}
	const auto windowPosCond = (lockVRMenuToCanvas || autoOffsetForTopStatusWindow || restoreAfterTopStatusWindow || forceSteamVRFirstUndockedLayout) ? ImGuiCond_Always : layoutCond;
	ImGui::SetNextWindowPos(windowPos, windowPosCond, centeredPivot);
	const auto windowSizeCond = (lockVRMenuToCanvas || repairSteamVRLegacyWindowSize || autoOffsetForTopStatusWindow || restoreAfterTopStatusWindow || forceSteamVRFirstUndockedLayout) ? ImGuiCond_Always : layoutCond;
	ImGui::SetNextWindowSize(defaultWindowSize, windowSizeCond);
	if (forceSteamVRFirstUndockedLayout) {
		steamVRUndockedFirstOpenLayoutApplied = true;
	}
	resetLayout = false;

	// Determine window flags based on docking state
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	if (lockVRMenuToCanvas) {
		windowFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
	}
	const bool steamVRUndockedWindow = useSteamVRWindowControls && !willBeDocked;

	// Only hide title bar when not docked
	if (!willBeDocked) {
		windowFlags |= ImGuiWindowFlags_NoTitleBar;
	}
	if (steamVRUndockedWindow) {
		windowFlags |= ImGuiWindowFlags_NoResize;
	}

	ImGui::Begin(title.c_str(), &IsEnabled, windowFlags);
	{
		ImGui::SetScrollX(0.0f);
		ImGui::SetScrollY(0.0f);

		// Update docking state tracking
		const bool actualDocked = ImGui::IsWindowDocked();
		const bool isDocked = actualDocked;
		wasDocked = actualDocked;
		const bool showSteamVRWindowControls = useSteamVRWindowControls && vrMenuLayoutUnlocked && !isDocked;

		float globalScale = settings.Theme.GlobalScale;

		// Use default global scale (0.0) for built-in themes when GlobalScale equals the default
		if (std::abs(globalScale - ThemeManager::Constants::DEFAULT_GLOBAL_SCALE) < 0.001f) {
			globalScale = ThemeManager::Constants::DEFAULT_GLOBAL_SCALE;  // Ensure built-in themes stay at 0.0
		}

		const float uiScale = exp2(globalScale);  // User's manual GlobalScale for header icons
		// Check if we can show icons - require setting enabled and at least some icons loaded (for undocked)
		// For docked mode, always show icons if textures are available
		bool canShowIcons = settings.Theme.ShowActionIcons &&
		                    (uiIcons.saveSettings.texture ||
								uiIcons.loadSettings.texture ||
								uiIcons.clearCache.texture);  // Always show logo if available, regardless of action icons setting
		bool showLogo = uiIcons.logo.texture != nullptr;

		// Render header using extracted component
		MenuHeaderRenderer::RenderHeader(
			isDocked,
			showLogo,
			canShowIcons,
			uiScale,
			uiIcons,
			useOpenCompositeStableHeader,
			showSteamVRWindowControls,
			mainDockSpaceId);

		// Main content starts here - no additional separator needed as it's already handled in the conditions above

		float footer_height = settings.Theme.ShowFooter ?
		                          (ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 3) :
		                          0.0f;

		// Static storage for menu state - must persist across frames
		static size_t selectedMenu = 0;
		static std::map<std::string, bool> categoryExpansionStates;

		// Render feature list using extracted component
		FeatureListRenderer::RenderFeatureList(
			footer_height,
			selectedMenu,
			featureSearch,
			pendingFeatureSelection,
			categoryExpansionStates,
			[&]() { DrawGeneralSettings(); },
			[&]() { DrawAdvancedSettings(); });

		if (settings.Theme.ShowFooter) {
			ImGui::Spacing();
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, ThemeManager::Constants::SEPARATOR_THICKNESS);
			ImGui::Spacing();
			DrawFooter();
		}

		// Draw global popups (needs to be called once per frame)
		Util::DrawClearShaderCacheConfirmation();

		if (showSteamVRWindowControls) {
			MenuHeaderRenderer::RenderSteamVRResizeHandles(uiScale);
		}
	}
	ImGui::End();

	const auto* imguiContext = ImGui::GetCurrentContext();
	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) ||
		ImGui::IsMouseReleased(ImGuiMouseButton_Right) ||
		(imguiContext &&
			(imguiContext->ActiveIdHasBeenEditedThisFrame ||
				imguiContext->NavActivateId != 0))) {
		RequestSettingsDirtyCheck();
	}
	UpdateSettingsDirtyState();

	if (!IsEnabled) {
		systemCommitLastRefreshTime = -1.0;
		PerformanceTuningRenderer::NotifyMenuClosed();
	}
}

/**
 * @brief Renders the General settings tab content
 *
 * Delegates rendering to SettingsTabRenderer for the general configuration panel,
 * which includes Shaders, Keybindings, and Interface sub-tabs. This method provides
 * the callback for key-to-string conversion while maintaining separation of concerns.
 */
void Menu::DrawGeneralSettings()
{
	// Prepare settings state for the renderer
	SettingsTabRenderer::SettingsState state{
		.settingToggleKey = settingToggleKey,
		.settingsEffectsToggle = settingsEffectsToggle,
		.settingSkipCompilationKey = settingSkipCompilationKey,
		.settingOverlayToggleKey = settingOverlayToggleKey,
		.settingShaderBlockPrevKey = settingShaderBlockPrevKey,
		.settingShaderBlockNextKey = settingShaderBlockNextKey,
		.settingCSEditorToggleKey = settingCSEditorToggleKey,
		.settingScreenshotKey = settingScreenshotKey
	};

	// Render settings using extracted component
	SettingsTabRenderer::RenderGeneralSettings(state);
}

/**
 * @brief Renders the Advanced settings tab content
 *
 * Delegates rendering to AdvancedSettingsRenderer for developer and advanced user
 * settings. Uses lambda callbacks to access private Menu methods while maintaining
 * encapsulation and proper separation of concerns.
 */
void Menu::DrawAdvancedSettings()
{
	// Render advanced settings using extracted component
	AdvancedSettingsRenderer::RenderAdvancedSettings(
		[this]() { DrawDisableAtBootSettings(); });
}

void Menu::DrawDisableAtBootSettings()
{
	auto state = globals::state;
	auto& disabledFeatures = state->GetDisabledFeatures();

	ImGui::Text(
		"Select features to disable at boot. "
		"This is the same as deleting a feature.ini file. "
		"Restart will be required to reenable.");

	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Features")) {
		// Prepare a sorted list of feature pointers
		auto featureList = Feature::GetFeatureList();
		std::sort(featureList.begin(), featureList.end(), [](Feature* a, Feature* b) {
			return a->GetShortName() < b->GetShortName();
		});

		// Display sorted features
		for (auto* feature : featureList) {
			if (feature->IsHiddenFromUserView())
				continue;

			const std::string featureName = feature->GetShortName();
			bool isDisabled = disabledFeatures.contains(featureName) && disabledFeatures[featureName];

			if (ImGui::Checkbox(featureName.c_str(), &isDisabled)) {
				// Update the disabledFeatures map based on user interaction
				disabledFeatures[featureName] = isDisabled;
			}
		}
	}
}

void Menu::DrawFooter()
{
	ImGui::BulletText(std::format("Game Version: {} {}", magic_enum::enum_name(REL::Module::GetRuntime()), Util::GetFormattedVersion(REL::Module::get().version()).c_str()).c_str());
	ImGui::SameLine();
	ImGui::BulletText(std::format("D3D12 Swap Chain: {}", globals::features::upscaling.d3d12SwapChainActive ? "Active" : "Inactive").c_str());
	ImGui::SameLine();

	const double now = ImGui::GetTime();
	if (systemCommitLastRefreshTime < 0.0 || now < systemCommitLastRefreshTime || now - systemCommitLastRefreshTime >= 1.0) {
		const auto sample = QuerySystemCommit();
		systemCommitSampleValid = sample.valid;
		systemCommitCurrentBytes = sample.currentBytes;
		systemCommitTotalBytes = sample.totalBytes;
		systemCommitLastRefreshTime = now;
	}

	const bool belowRecommendedCommit = systemCommitSampleValid && systemCommitTotalBytes < kRecommendedSystemCommitBytes;
	if (belowRecommendedCommit)
		ImGui::PushStyleColor(ImGuiCol_Text, settings.Theme.StatusPalette.Error);
	if (systemCommitSampleValid) {
		ImGui::BulletText(
			"Commit: %.1f / %.1f GB",
			BytesToGiB(systemCommitCurrentBytes),
			BytesToGiB(systemCommitTotalBytes));
	} else {
		ImGui::BulletText("Commit: unavailable");
	}
	if (belowRecommendedCommit)
		ImGui::PopStyleColor();
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextWrapped(
			"Current Windows system commit and total commit capacity. A minimum total commit capacity "
			"of 128 GB is recommended for all systems. This text is shown in red when the total is "
			"below that minimum.");
	}

	ImGui::SameLine();
	ImGui::BulletText(std::format("GPU: {}", globals::state->adapterDescription.c_str()).c_str());
}

/**
 * @brief Main overlay rendering coordinator
 *
 * Delegates all overlay rendering to OverlayRenderer while providing necessary
 * callbacks for input processing, settings rendering, and key mapping. This method
 * serves as the bridge between Menu's state and the extracted overlay rendering logic.
 *
 * Handles VR setup, input event processing, shader compilation status, feature overlays,
 * A/B testing, and ImGui frame management through the specialized renderer component.
 */
void Menu::DrawOverlay()
{
	// Only process reloads when ImGui is NOT in an active frame
	ImGuiContext* ctx = ImGui::GetCurrentContext();
	bool canReload = ctx && !ctx->WithinFrameScope && ctx->WithinEndChildID == 0;

	// Process deferred font reload BEFORE any ImGui operations
	// This is the safest place to do font atlas modifications
	if (pendingFontReload && canReload) {
		// Call ReloadFont first - only clear flag if it succeeds
		if (ThemeManager::ReloadFont(*this, cachedFontSize)) {
			// Reload completed successfully
			pendingFontReload = false;
		} else {
			// Reload failed - keep flag true to retry next frame
			logger::warn("Menu::DrawOverlay() - Font reload failed, will retry next frame");
		}
	}

	// Process deferred icon reload BEFORE rendering
	if (pendingIconReload && canReload) {
		if (Util::IconLoader::InitializeMenuIcons(this)) {
			pendingIconReload = false;
		} else {
			logger::warn("Menu::DrawOverlay() - Icon reload failed, will retry next frame");
		}
	}

	OverlayRenderer::RenderOverlay(
		*this,
		[this]() { ProcessInputEventQueue(); },
		[this]() { DrawSettings(); },
		[](std::vector<InputCombo> keys) -> const char* {
			static std::string result_cache;
			result_cache = Util::Input::KeyIdToString(keys);
			return result_cache.c_str();
		},
		cachedFontSize,
		ThemeManager::ResolveFontSize(*this));
}

bool Menu::IsMenuSessionOpen() const
{
	const auto* editorWindow = EditorWindow::GetSingleton();
	return IsEnabled || (editorWindow && editorWindow->open);
}

bool Menu::HasClosedMenuOverlay() const
{
	return PerformanceTuningRenderer::HasActiveMeasurements();
}

void Menu::OpenMenu()
{
	if (IsEnabled || HasClosedMenuOverlay())
		return;

	IsEnabled = true;
	if (globals::features::vr.IsOpenVRCompatible()) {
		auto& vr = globals::features::vr;
		vr.ResetMenuInputRuntimeState();
		if (VRMenuPositioningPolicy::ShouldReanchorOnOpen(vr.settings.UnlockMenuPositionAndSize))
			vr.RequestFixedWorldMenuReanchor();
	}
}

void Menu::CloseMenu()
{
	auto* editorWindow = EditorWindow::GetSingleton();
	const bool editorWasOpen = editorWindow && editorWindow->open;
	if (!IsEnabled && !editorWasOpen)
		return;

	if (editorWindow) {
		editorWindow->open = false;
		editorWindow->UpdateOpenState();
	}
	IsEnabled = false;
	systemCommitLastRefreshTime = -1.0;

	PerformanceTuningRenderer::NotifyMenuClosed();

	if (globals::features::vr.IsOpenVRCompatible())
		globals::features::vr.ResetMenuInputRuntimeState();
}

/**
 * @brief Processes queued input events for both VR and non-VR devices
 *
 * This method handles the complex logic of routing input events to appropriate handlers:
 * - VR controller events are forwarded to the VR system for specialized processing
 * - Non-VR events (keyboard, mouse) are processed directly for ImGui integration
 * - Includes key state normalization and stuck key detection/correction
 *
 * The method maintains thread safety through mutex protection of the input event queue.
 *
 * @note This method contains Menu-specific logic and state management that makes it
 *       inappropriate for extraction to a utility class.
 */
static std::vector<InputCombo> DeriveCSEditorKey(const std::vector<InputCombo>& menuKey)
{
	bool hasShift = false;
	uint32_t baseKey = 0;

	for (const auto& combo : menuKey) {
		uint32_t vk = combo.GetKey();
		if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) {
			hasShift = true;
		} else if (vk != VK_CONTROL && vk != VK_LCONTROL && vk != VK_RCONTROL &&
				   vk != VK_MENU && vk != VK_LMENU && vk != VK_RMENU) {
			baseKey = vk;
		}
	}

	if (hasShift || baseKey == 0)
		return {};

	return { InputCombo::Keyboard(VK_SHIFT), InputCombo::Keyboard(baseKey) };
}

void Menu::ProcessInputEventQueue()
{
	std::unique_lock<std::shared_mutex> mutex(_inputEventMutex);
	ImGuiIO& io = ImGui::GetIO();
	// Split the queue into VR and non-VR events
	std::vector<KeyEvent> vrEvents;
	std::vector<KeyEvent> nonVREvents;
	for (auto& event : _keyEventQueue) {
		bool isVRController = ((event.device == RE::INPUT_DEVICE::kVivePrimary || event.device == RE::INPUT_DEVICE::kViveSecondary ||
								event.device == RE::INPUT_DEVICE::kOculusPrimary || event.device == RE::INPUT_DEVICE::kOculusSecondary ||
								event.device == RE::INPUT_DEVICE::kWMRPrimary || event.device == RE::INPUT_DEVICE::kWMRSecondary));

		if (globals::features::vr.IsOpenVRCompatible() && isVRController) {
			vrEvents.push_back(event);
		} else {
			nonVREvents.push_back(event);
		}
	}
	// Process VR events in VR
	if (!vrEvents.empty()) {
		globals::features::vr.ProcessVREvents(vrEvents);
		globals::features::vr.UpdateOverlayMenuStateFromInput();
	}

	// Process non-VR events in Menu
	for (auto& event : nonVREvents) {
		if (event.eventType == RE::INPUT_EVENT_TYPE::kChar) {
			io.AddInputCharacter(event.keyCode);
			continue;
		}
		if (event.device == RE::INPUT_DEVICE::kMouse) {
			logger::trace("Detect mouse scan code {} value {} pressed: {}", event.keyCode, event.value, event.IsPressed());
			auto* ew = EditorWindow::GetSingleton();
			bool flying = ew && ew->IsPreviewFlying();
			if (event.keyCode > 7) {  // middle scroll
				if (ew && ew->previewMode == EditorWindow::PreviewMode::FreeCamera) {
					ew->AdjustFlySpeed(event.keyCode == 8 ? 1.0f : -1.0f);
				}
			} else if (!flying) {
				if (event.keyCode > 5)
					event.keyCode = 5;
				io.AddMouseButtonEvent(event.keyCode, event.IsPressed());
			}
		}

		if (event.device == RE::INPUT_DEVICE::kKeyboard) {
			uint32_t key = Util::Input::DIKToVK(event.keyCode);
			logger::trace("Detected key code {} ({})", event.keyCode, key);
			if (key == event.keyCode)
				key = MapVirtualKeyEx(event.keyCode, MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0));

			const bool wasCapturingHotkey = IsCapturingHotkeyInput();
			const bool allowSetupCloseKey = wasCapturingHotkey && HomePageRenderer::ShouldShowFirstTimeSetup() &&
			                                (key == VK_RETURN || key == VK_ESCAPE);

			if (event.IsDown() && !wasCapturingHotkey && HomePageRenderer::TryCompleteFirstTimeSetupFromInput(key)) {
				continue;
			}

			auto shaderCache = globals::shaderCache;
			const bool screenshotHotkeyActive =
				globals::features::screenshotFeature.IsRuntimeEnabled();
			auto dispatchHotkeyActions = [this, key, shaderCache, screenshotHotkeyActive](bool combosOnly) {
				struct KeyAction
				{
					std::vector<InputCombo>& settingKey;
					std::function<void()> action;
					bool active = true;
				};

				KeyAction keyActions[] = {
					{ settings.ToggleKey, [this]() {
						 if (!HomePageRenderer::ShouldShowFirstTimeSetup()) {
							 if (IsMenuSessionOpen()) {
								 CloseMenu();
							 } else {
								 OpenMenu();
								 ImGui::GetIO().ClearInputKeys();  // Prevent toggle key from remaining "held" in ImGui after open.
							 }
						 }
					 } },
					{ settings.SkipCompilationKey, [this, shaderCache]() { if (!ShouldSwallowInput() && shaderCache->IsCompiling()) shaderCache->backgroundCompilation = true; } },
					{ settings.EffectToggleKey, [shaderCache]() { shaderCache->SetEnabled(!shaderCache->IsEnableRequested()); } },
					{ settings.ShaderBlockPrevKey, [this, shaderCache]() { if (settings.EnableShaderBlocking) shaderCache->IterateShaderBlock(); } },
					{ settings.ShaderBlockNextKey, [this, shaderCache]() { if (settings.EnableShaderBlocking) shaderCache->IterateShaderBlock(false); } },
					{ settings.OverlayToggleKey, []() { Menu::GetSingleton()->overlayVisible = !Menu::GetSingleton()->overlayVisible; } },
					{ settings.CSEditorToggleKey, []() {
						 if (Menu::GetSingleton()->HasClosedMenuOverlay())
							 return;
						 auto* ew = EditorWindow::GetSingleton();
						 if (!ew)
							 return;
						 if (ew->GetPreviewMode() == EditorWindow::PreviewMode::FreeCamera) {
							 // Flying -> lock camera position for editing
							 ew->ToggleFreeCameraLock();
						 } else if (ew->IsInPreviewMode()) {
							 // Locked or PlayMode -> fully exit preview
							 ew->ExitPreviewMode();
						 } else {
							 CSEditor::ToggleEditorWindow();
						 }
					 } },
					{ settings.ScreenshotKey, []() { globals::features::screenshotFeature.RequestUiCapture(); }, screenshotHotkeyActive },
				};

				// RenderDoc's capture key is a single, unmodified key; only consider it on key-up.
				if (!combosOnly && globals::features::renderDoc.HandleCaptureHotkey(key))
					return true;

				for (const auto& ka : keyActions) {
					const bool isCombo = ka.settingKey.size() > 1;
					if (ka.active &&
						isCombo == combosOnly &&
						InputCombo::MatchesKeyboardCombo(ka.settingKey, key)) {
						ka.action();
						return true;
					}
				}

				return false;
			};

			if (!event.IsPressed()) {
				// Skip key release if it was used to close the first-time setup dialog
				if (HomePageRenderer::ShouldSkipKeyRelease(key)) {
					io.AddKeyEvent(Util::Input::VirtualKeyToImGuiKey(key), event.IsPressed());
					continue;
				}

				struct HotkeyAction
				{
					std::vector<InputCombo>* settingKey;
					bool* settingFlag;
					std::function<void(std::vector<InputCombo>)> action;
				};
				HotkeyAction hotkeyActions[] = {
					{ &settings.ToggleKey, &settingToggleKey, [this](std::vector<InputCombo> keys) {
						 settings.ToggleKey = keys;
						 settingToggleKey = false;
						 if (!settings.FirstTimeSetupCompleted)
							 settings.CSEditorToggleKey = DeriveCSEditorKey(keys);
					 } },
					{ &settings.SkipCompilationKey, &settingSkipCompilationKey, [this](std::vector<InputCombo> keys) { settings.SkipCompilationKey = keys; settingSkipCompilationKey = false; } },
					{ &settings.EffectToggleKey, &settingsEffectsToggle, [this](std::vector<InputCombo> keys) { settings.EffectToggleKey = keys; settingsEffectsToggle = false; } },
					{ &settings.OverlayToggleKey, &settingOverlayToggleKey, [this](std::vector<InputCombo> keys) { settings.OverlayToggleKey = keys; settingOverlayToggleKey = false; } },
					{ &settings.ShaderBlockPrevKey, &settingShaderBlockPrevKey, [this](std::vector<InputCombo> keys) { settings.ShaderBlockPrevKey = keys; settingShaderBlockPrevKey = false; } },
					{ &settings.ShaderBlockNextKey, &settingShaderBlockNextKey, [this](std::vector<InputCombo> keys) { settings.ShaderBlockNextKey = keys; settingShaderBlockNextKey = false; } },
					{ &settings.CSEditorToggleKey, &settingCSEditorToggleKey, [this](std::vector<InputCombo> keys) { settings.CSEditorToggleKey = keys; settingCSEditorToggleKey = false; } },
					{ &settings.ScreenshotKey, &settingScreenshotKey, [this](std::vector<InputCombo> keys) { settings.ScreenshotKey = keys; settingScreenshotKey = false; } },
				};
				bool handled = false;
				bool closedFirstTimeSetup = false;
				for (auto& h : hotkeyActions) {
					if (*(h.settingFlag)) {
						// During first-time setup, don't capture Enter or Escape as hotkeys
						// These keys cancel capture and close the dialog instead.
						if (HomePageRenderer::ShouldShowFirstTimeSetup() && (key == VK_RETURN || key == VK_ESCAPE)) {
							*(h.settingFlag) = false;  // Cancel hotkey capture mode
							if (!IsCapturingHotkeyInput()) {
								closedFirstTimeSetup = HomePageRenderer::TryCompleteFirstTimeSetupFromInput(key, false);
							}
							handled = true;
							break;
						}

						// Ignore modifier-only key releases during recording
						bool isModifier = (key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL ||
										   key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT ||
										   key == VK_MENU || key == VK_LMENU || key == VK_RMENU);

						if (isModifier) {
							handled = true;
							break;
						}

						// Capture modifiers + key
						std::vector<InputCombo> combo;

						// Add active modifiers to combo
						if ((GetAsyncKeyState(VK_CONTROL) & Constants::KEY_PRESSED_MASK) &&
							key != VK_CONTROL && key != VK_LCONTROL && key != VK_RCONTROL)
							combo.push_back(InputCombo::Keyboard(VK_CONTROL));
						if ((GetAsyncKeyState(VK_SHIFT) & Constants::KEY_PRESSED_MASK) &&
							key != VK_SHIFT && key != VK_LSHIFT && key != VK_RSHIFT)
							combo.push_back(InputCombo::Keyboard(VK_SHIFT));
						if ((GetAsyncKeyState(VK_MENU) & Constants::KEY_PRESSED_MASK) &&
							key != VK_MENU && key != VK_LMENU && key != VK_RMENU)
							combo.push_back(InputCombo::Keyboard(VK_MENU));

						combo.push_back(InputCombo::Keyboard(key));

						h.action(combo);
						RequestSettingsDirtyCheck();
						handled = true;
						break;
					}
				}
				if (closedFirstTimeSetup) {
					continue;
				}
				if (!handled) {
					// Single-key hotkeys fire on key-up; combos already fired on key-down.
					// If this key's key-down already fired a combo, suppress the single-key
					// binding so releasing the modifier first does not trigger it as well.
					if (_comboFiredKeys.erase(key) == 0)
						dispatchHotkeyActions(false);
				}

				// Handle ESC key for menu and editor window
				auto* editorWindow = EditorWindow::GetSingleton();
				if (key == VK_ESCAPE) {
					if (editorWindow && editorWindow->IsInPreviewMode()) {
						editorWindow->ExitPreviewMode();
					} else if (editorWindow && editorWindow->open && editorWindow->ShouldHandleEscapeKey()) {
						editorWindow->open = false;
					} else if (IsEnabled && (!editorWindow || !editorWindow->open)) {
						CloseMenu();
					}
				}
			} else if (event.IsDown() && !wasCapturingHotkey) {
				// Fire combo hotkeys on the key-down transition so they respond on
				// press rather than release. IsDown() (not IsPressed()) ensures we
				// trigger only once instead of every frame the key is held.
				if (dispatchHotkeyActions(true))
					_comboFiredKeys.insert(key);
			}

			// Don't forward hotkey events to ImGui when input is captured (prevents e.g. End key scrolling the feature list)
			// SkipCompilationKey (ESC) is excluded - ESC must reach ImGui for menu/dialog close.
			const std::vector<InputCombo>* hotkeys[] = {
				&settings.ToggleKey, &settings.EffectToggleKey,
				&settings.OverlayToggleKey, &settings.ShaderBlockPrevKey, &settings.ShaderBlockNextKey,
				&settings.CSEditorToggleKey
			};
			const bool isHotkey = ShouldSwallowInput() &&
			                      ((screenshotHotkeyActive &&
									   InputCombo::MatchesKeyboardCombo(settings.ScreenshotKey, key)) ||
									  std::any_of(std::begin(hotkeys), std::end(hotkeys),
										  [key](const auto* combo) { return InputCombo::MatchesKeyboardCombo(*combo, key); }));

			// Always forward key-up events. Suppress key-down during active hotkeys,
			// and during hotkey capture except setup close keys (Enter/Escape).
			const bool isKeyDown = event.IsPressed();
			const bool suppressForwarding = isKeyDown && (isHotkey || (wasCapturingHotkey && !allowSetupCloseKey));
			if (!suppressForwarding) {
				// DirectInput loses key-up events after alt-tab; validate against OS state.
				bool pressed = isKeyDown && (GetAsyncKeyState(key) & Constants::KEY_PRESSED_MASK);
				io.AddKeyEvent(Util::Input::VirtualKeyToImGuiKey(key), pressed);

				if (key == VK_LCONTROL || key == VK_RCONTROL)
					io.AddKeyEvent(ImGuiMod_Ctrl, pressed);
				else if (key == VK_LSHIFT || key == VK_RSHIFT)
					io.AddKeyEvent(ImGuiMod_Shift, pressed);
				else if (key == VK_LMENU || key == VK_RMENU)
					io.AddKeyEvent(ImGuiMod_Alt, pressed);
			}
		}
	}

	const auto directInputWheelRaw = _directInputWheelDelta.exchange(0, std::memory_order_relaxed);
	const float wheelY = static_cast<float>(directInputWheelRaw) / static_cast<float>(WHEEL_DELTA);
	if (wheelY != 0.0f)
		io.AddMouseWheelEvent(0.0f, wheelY);

	_keyEventQueue.clear();
}

void Menu::RecordDirectInputWheelDelta(std::int32_t a_delta)
{
	if (a_delta != 0)
		_directInputWheelDelta.fetch_add(a_delta, std::memory_order_relaxed);
}

bool Menu::IsCapturingHotkeyInput() const
{
	return settingToggleKey || settingSkipCompilationKey || settingsEffectsToggle ||
	       settingOverlayToggleKey || settingShaderBlockPrevKey || settingShaderBlockNextKey || settingCSEditorToggleKey || settingScreenshotKey;
}

void Menu::addToEventQueue(KeyEvent e)
{
	std::unique_lock<std::shared_mutex> mutex(_inputEventMutex);
	_keyEventQueue.emplace_back(e);
}

void Menu::OnFocusChanged()
{
	// Solves the alt+tab stuck issue, but disables tab after tabbing back in.
	if (const auto& inputMgr = RE::BSInputDeviceManager::GetSingleton()) {
		if (const auto& device = inputMgr->GetKeyboard()) {
			device->ClearInputState();
		}
	}
	// Allows tab to work again after alt+tabbing back in.
	ImGui::GetIO().ClearInputKeys();
}

void Menu::ProcessInputEvents(RE::InputEvent* const* a_events)
{
	for (auto it = *a_events; it; it = it->next) {
		// Accept button, char, and thumbstick events
		if (it->GetEventType() != RE::INPUT_EVENT_TYPE::kButton &&
			it->GetEventType() != RE::INPUT_EVENT_TYPE::kChar &&

			it->GetEventType() != RE::INPUT_EVENT_TYPE::kThumbstick

			)  // we do not care about non button/char/thumbstick events
			continue;

		if (it->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
			addToEventQueue(KeyEvent(static_cast<RE::ButtonEvent*>(it)));
		} else if (it->GetEventType() == RE::INPUT_EVENT_TYPE::kChar) {
			addToEventQueue(KeyEvent(static_cast<CharEvent*>(it)));

		} else if (it->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
			addToEventQueue(KeyEvent(static_cast<RE::ThumbstickEvent*>(it)));
		}
	}
}

bool Menu::ShouldSwallowInput()
{
	return IsMenuSessionOpen() || HomePageRenderer::ShouldShowFirstTimeSetup();
}

bool Menu::ShouldBlockAllGameInput()
{
	return HomePageRenderer::ShouldShowFirstTimeSetup();
}

bool Menu::IsPreviewFlying()
{
	auto editorWindow = EditorWindow::GetSingleton();
	return editorWindow && editorWindow->IsPreviewFlying();
}

void Menu::SelectFeatureMenu(const std::string& featureName)
{
	pendingFeatureSelection = featureName;
	logger::info("Queued navigation to {} feature menu", featureName);
}
