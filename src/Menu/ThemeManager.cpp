#include "ThemeManager.h"
#include "../Menu.h"
#include "ThemePresets.h"

#include "BackgroundBlur.h"
#include "Fonts.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <thread>
#include <unordered_map>
#include <vector>

#include <imgui_impl_dx11.h>
#include <imgui_internal.h>

#include "RE/Skyrim.h"
#include "State.h"

#include "../Globals.h"
#include "../Util.h"
#include "../Utils/FileSystem.h"
#include "../Utils/UI.h"
using namespace SKSE;

namespace
{
	// Theme System Constants
	// ======================

	// Text Contrast and Opacity
	// -------------------------
	// Disabled text alpha: Makes inactive UI elements visually distinct but still readable
	// Value calibrated for accessibility - too low = invisible, too high = looks enabled
	constexpr float DISABLED_TEXT_ALPHA = 0.58f;  // 58% opacity for disabled elements

	/**
	 * @brief Gets file modification time
	 */
	std::time_t GetFileModTime(const std::filesystem::path& filePath)
	{
		try {
			auto fileTime = std::filesystem::last_write_time(filePath);
			auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
				fileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
			return std::chrono::system_clock::to_time_t(systemTime);
		} catch (...) {
			return 0;
		}
	}

	using Util::Color::Blend;
	using Util::Color::Lift;
	using Util::Color::WithAlpha;

	void ApplySemanticPalette(ImVec4* colors, const Menu::ThemeSettings& themeSettings)
	{
		const auto& palette = themeSettings.Palette;
		const auto& status = themeSettings.StatusPalette;

		const ImVec4 background = palette.Background;
		const float backgroundAlpha = background.w;
		const float frameAlpha = palette.FrameBorder.w;
		const float childAlpha = std::clamp(backgroundAlpha - 0.08f, 0.18f, 0.34f);
		const float controlAlpha = std::clamp(frameAlpha, backgroundAlpha + 0.12f, 0.72f);
		const float elevatedAlpha = std::clamp(frameAlpha + 0.06f, backgroundAlpha + 0.18f, 0.78f);
		const float popupAlpha = std::clamp(frameAlpha + 0.16f, backgroundAlpha + 0.28f, 0.84f);
		const float titleAlpha = std::clamp(backgroundAlpha + 0.06f, backgroundAlpha, 0.52f);
		const float menuBarAlpha = std::clamp(backgroundAlpha + 0.12f, backgroundAlpha + 0.04f, 0.58f);
		const float tabAlpha = std::clamp(frameAlpha + 0.02f, backgroundAlpha + 0.16f, 0.72f);
		const float dimmedTabAlpha = std::clamp(frameAlpha - 0.06f, backgroundAlpha + 0.10f, 0.62f);
		const auto layeredSurface = [&](float blendAmount, float liftAmount, float alpha) {
			return Lift(Blend(background, palette.FrameBorder, blendAmount, alpha), liftAmount, alpha);
		};
		const ImVec4 controlSurface = layeredSurface(0.55f, -0.010f, controlAlpha);
		const ImVec4 elevatedSurface = layeredSurface(0.60f, 0.006f, elevatedAlpha);
		const ImVec4 popupSurface = layeredSurface(0.58f, -0.004f, popupAlpha);
		const ImVec4 titleSurface = Lift(background, -0.004f, titleAlpha);
		const ImVec4 menuBarSurface = layeredSurface(0.52f, -0.006f, menuBarAlpha);
		const ImVec4 tabSurface = layeredSurface(0.56f, -0.006f, tabAlpha);
		const ImVec4 dimmedTabSurface = layeredSurface(0.50f, -0.010f, dimmedTabAlpha);
		const ImVec4 primary = status.InfoColor;
		const ImVec4 secondary = status.Warning;
		const ImVec4 disabled = status.Disable;

		colors[ImGuiCol_WindowBg] = palette.Background;
		colors[ImGuiCol_ChildBg] = Lift(background, 0.006f, childAlpha);
		colors[ImGuiCol_PopupBg] = popupSurface;
		colors[ImGuiCol_Text] = palette.Text;
		colors[ImGuiCol_TextDisabled] = WithAlpha(disabled, DISABLED_TEXT_ALPHA);
		colors[ImGuiCol_Border] = palette.WindowBorder;
		colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

		colors[ImGuiCol_FrameBg] = controlSurface;
		colors[ImGuiCol_FrameBgHovered] = Blend(controlSurface, primary, 0.18f, std::clamp(controlAlpha + 0.08f, 0.0f, 0.80f));
		colors[ImGuiCol_FrameBgActive] = Blend(controlSurface, primary, 0.30f, std::clamp(controlAlpha + 0.16f, 0.0f, 0.88f));
		colors[ImGuiCol_CheckMark] = primary;
		colors[ImGuiCol_SliderGrab] = WithAlpha(primary, 0.82f);
		colors[ImGuiCol_SliderGrabActive] = Blend(primary, palette.Text, 0.22f, 1.0f);

		colors[ImGuiCol_Button] = Blend(elevatedSurface, primary, 0.13f, std::clamp(elevatedAlpha - 0.04f, 0.54f, 0.74f));
		colors[ImGuiCol_ButtonHovered] = Blend(elevatedSurface, primary, 0.24f, std::clamp(elevatedAlpha + 0.04f, 0.62f, 0.80f));
		colors[ImGuiCol_ButtonActive] = Blend(elevatedSurface, primary, 0.34f, std::clamp(elevatedAlpha + 0.12f, 0.70f, 0.88f));

		colors[ImGuiCol_Header] = Blend(controlSurface, primary, 0.12f, std::clamp(controlAlpha, 0.56f, 0.72f));
		colors[ImGuiCol_HeaderHovered] = Blend(controlSurface, primary, 0.24f, std::clamp(controlAlpha + 0.10f, 0.64f, 0.82f));
		colors[ImGuiCol_HeaderActive] = Blend(controlSurface, primary, 0.34f, std::clamp(controlAlpha + 0.18f, 0.72f, 0.90f));

		colors[ImGuiCol_Separator] = palette.Separator;
		colors[ImGuiCol_SeparatorHovered] = WithAlpha(primary, 0.74f);
		colors[ImGuiCol_SeparatorActive] = WithAlpha(primary, 1.0f);
		colors[ImGuiCol_ResizeGrip] = palette.ResizeGrip;
		colors[ImGuiCol_ResizeGripHovered] = Blend(palette.ResizeGrip, primary, 0.20f, 0.85f);
		colors[ImGuiCol_ResizeGripActive] = Blend(palette.ResizeGrip, primary, 0.32f, 0.95f);

		colors[ImGuiCol_TitleBg] = titleSurface;
		colors[ImGuiCol_TitleBgActive] = Blend(titleSurface, primary, 0.08f, std::clamp(titleAlpha + 0.08f, 0.0f, 0.60f));
		colors[ImGuiCol_TitleBgCollapsed] = Lift(background, -0.004f, std::clamp(backgroundAlpha - 0.06f, 0.24f, 0.40f));
		colors[ImGuiCol_MenuBarBg] = menuBarSurface;

		colors[ImGuiCol_ScrollbarBg] = Lift(background, 0.010f, colors[ImGuiCol_ScrollbarBg].w);
		colors[ImGuiCol_ScrollbarGrab] = Blend(elevatedSurface, primary, 0.18f, colors[ImGuiCol_ScrollbarGrab].w);
		colors[ImGuiCol_ScrollbarGrabHovered] = Blend(elevatedSurface, primary, 0.30f, colors[ImGuiCol_ScrollbarGrabHovered].w);
		colors[ImGuiCol_ScrollbarGrabActive] = Blend(elevatedSurface, primary, 0.42f, colors[ImGuiCol_ScrollbarGrabActive].w);

		colors[ImGuiCol_Tab] = Blend(tabSurface, primary, 0.08f, tabAlpha);
		colors[ImGuiCol_TabHovered] = Blend(tabSurface, primary, 0.30f, std::clamp(tabAlpha + 0.08f, 0.0f, 0.82f));
		colors[ImGuiCol_TabSelected] = Blend(elevatedSurface, primary, 0.18f, std::clamp(elevatedAlpha + 0.12f, 0.0f, 0.88f));
		colors[ImGuiCol_TabSelectedOverline] = WithAlpha(primary, 1.0f);
		colors[ImGuiCol_TabDimmed] = dimmedTabSurface;
		colors[ImGuiCol_TabDimmedSelected] = Blend(dimmedTabSurface, primary, 0.12f, std::clamp(dimmedTabAlpha + 0.14f, 0.0f, 0.80f));
		colors[ImGuiCol_TabDimmedSelectedOverline] = WithAlpha(primary, 0.45f);

		colors[ImGuiCol_TableHeaderBg] = Blend(elevatedSurface, primary, 0.10f, std::clamp(elevatedAlpha, 0.60f, 0.78f));
		colors[ImGuiCol_TableBorderStrong] = WithAlpha(palette.Separator, 1.0f);
		colors[ImGuiCol_TableBorderLight] = WithAlpha(palette.Separator, 0.70f);
		colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		colors[ImGuiCol_TableRowBgAlt] = WithAlpha(primary, 0.055f);

		colors[ImGuiCol_InputTextCursor] = primary;
		colors[ImGuiCol_TextLink] = primary;
		colors[ImGuiCol_TextSelectedBg] = WithAlpha(primary, 0.35f);
		colors[ImGuiCol_TreeLines] = WithAlpha(palette.Separator, 0.70f);
		colors[ImGuiCol_DockingPreview] = WithAlpha(primary, 0.42f);
		colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		colors[ImGuiCol_DragDropTarget] = secondary;
		colors[ImGuiCol_DragDropTargetBg] = WithAlpha(secondary, 0.18f);
		colors[ImGuiCol_UnsavedMarker] = secondary;
		colors[ImGuiCol_NavCursor] = primary;
		colors[ImGuiCol_NavWindowingHighlight] = WithAlpha(palette.Text, 0.70f);
		colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.40f);
		colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
	}
}

// Static UI helper methods
void ThemeManager::SetupImGuiStyle(const Menu& menu)
{
	auto& style = ImGui::GetStyle();
	auto& colors = style.Colors;

	// Theme based on https://github.com/powerof3/DialogueHistory
	auto& themeSettings = menu.GetTheme();

	// Safety check: If theme appears corrupted/empty, force reload Default.json
	// This prevents fallback to ImGui's hardcoded defaults
	bool isThemeCorrupted = (themeSettings.FullPalette.size() < ImGuiCol_COUNT / 2) ||
	                        (themeSettings.Palette.Background.w == 0.0f && themeSettings.Palette.Text.w == 0.0f);

	if (isThemeCorrupted) {
		logger::warn("Theme appears corrupted, attempting emergency reload of Default.json");
		// Emergency recovery: const_cast is acceptable here to prevent total UI failure
		if (const_cast<Menu*>(&menu)->LoadThemePreset("Default")) {
			logger::info("Successfully recovered with Default.json theme");
		} else {
			logger::error("Failed to reload Default.json - ImGui may revert to hardcoded defaults");
		}
	}

	// rescale here
	auto styleCopy = themeSettings.Style;

	float globalScale = themeSettings.GlobalScale;

	// Use default global scale (0.0) for built-in themes when GlobalScale equals the default
	if (std::abs(globalScale - Constants::DEFAULT_GLOBAL_SCALE) < 0.001f) {
		globalScale = Constants::DEFAULT_GLOBAL_SCALE;  // Ensure built-in themes stay at 0.0
	}

	// Scale style sizes by GlobalScale and font-size ratio (theme values target 1080p baseline)
	float fontScale = 1.0f;
	auto& io = ImGui::GetIO();
	if (io.FontDefault) {
		constexpr float kBaselineFontSize = Constants::DEFAULT_SCREEN_HEIGHT * Constants::DEFAULT_FONT_RATIO;
		fontScale = io.FontDefault->LegacySize / kBaselineFontSize;
	}
	const float scaleFactor = fontScale * exp2(globalScale);
	styleCopy.ScaleAllSizes(scaleFactor);

	// ScaleAllSizes skips border and separator sizes — scale them manually, flooring non-zero values at 1px
	auto scaleSize = [scaleFactor](float value) -> float {
		if (value <= 0.0f)
			return 0.0f;
		return ImMax(1.0f, ImTrunc(value * scaleFactor));
	};
	styleCopy.WindowBorderSize = scaleSize(themeSettings.Style.WindowBorderSize);
	styleCopy.ChildBorderSize = scaleSize(themeSettings.Style.ChildBorderSize);
	styleCopy.PopupBorderSize = scaleSize(themeSettings.Style.PopupBorderSize);
	styleCopy.FrameBorderSize = scaleSize(themeSettings.Style.FrameBorderSize);
	styleCopy.TabBorderSize = scaleSize(themeSettings.Style.TabBorderSize);
	styleCopy.TabBarBorderSize = scaleSize(themeSettings.Style.TabBarBorderSize);
	styleCopy.SeparatorTextBorderSize = scaleSize(themeSettings.Style.SeparatorTextBorderSize);
	styleCopy.DockingSeparatorSize = scaleSize(themeSettings.Style.DockingSeparatorSize);
	styleCopy.MouseCursorScale = ImMax(1.0f, themeSettings.Style.MouseCursorScale);

	style = styleCopy;
	style.HoverDelayNormal = themeSettings.TooltipHoverDelay;
	style.FontScaleMain = exp2(globalScale);

	// Always use the unified FullPalette system instead of switching between simple/full
	// This ensures consistent behavior regardless of UI presentation mode
	for (size_t i = 0; i < std::min(themeSettings.FullPalette.size(), static_cast<size_t>(ImGuiCol_COUNT)); ++i) {
		colors[i] = themeSettings.FullPalette[i];
	}

	ApplySemanticPalette(colors, themeSettings);

	// Apply scrollbar opacity settings
	colors[ImGuiCol_ScrollbarBg].w = themeSettings.ScrollbarOpacity.Background;
	colors[ImGuiCol_ScrollbarGrab].w = themeSettings.ScrollbarOpacity.Thumb;
	colors[ImGuiCol_ScrollbarGrabHovered].w = themeSettings.ScrollbarOpacity.ThumbHovered;
	colors[ImGuiCol_ScrollbarGrabActive].w = themeSettings.ScrollbarOpacity.ThumbActive;
}

void ThemeManager::ForceApplyDefaultTheme()
{
	// This function applies Default.json colors directly to ImGui, bypassing any hardcoded defaults
	// It's used when the theme system fails or ImGui resets to defaults unexpectedly

	auto* themeManager = GetSingleton();
	json defaultThemeSettings;

	if (!themeManager->LoadTheme("Default", defaultThemeSettings)) {
		logger::warn("ForceApplyDefaultTheme: Could not load Default.json theme");
		return;
	}

	auto& style = ImGui::GetStyle();
	auto& colors = style.Colors;

	// Load palette using named-map or legacy-array deserialization
	std::array<ImVec4, ImGuiCol_COUNT> palette;
	Menu::PaletteFromJson(defaultThemeSettings, palette);
	for (int i = 0; i < ImGuiCol_COUNT; i++)
		colors[i] = palette[i];
	logger::info("ForceApplyDefaultTheme: Applied Default.json colors directly to ImGui");
}

bool ThemeManager::ReloadFont(const Menu& menu, float& cachedFontSize)
{
	// Thread-safe reentrancy guard using atomic flag
	static std::atomic<bool> isReloading{ false };
	bool expected = false;
	if (!isReloading.compare_exchange_strong(expected, true)) {
		return false;
	}

	// RAII scope guard to ensure isReloading is always reset on exit (exceptions, returns, etc.)
	struct ReloadGuard
	{
		std::atomic<bool>& flag;
		explicit ReloadGuard(std::atomic<bool>& f) :
			flag(f) {}
		~ReloadGuard() { flag = false; }
	} guard(isReloading);

	auto& themeSettings = menu.GetTheme();

	ImGuiIO& io = ImGui::GetIO();

	// Additional safety checks: ensure ImGui is in a valid state
	ImGuiContext* ctx = ImGui::GetCurrentContext();
	if (!ctx) {
		logger::error("ReloadFont: No valid ImGui context");
		return false;
	}

	// Ensure we're not in the middle of a frame
	if (ctx->WithinFrameScope) {
		logger::error("ReloadFont: Cannot reload font within frame scope");
		return false;
	}

	// Additional check: make sure font atlas exists
	if (!io.Fonts) {
		logger::error("ReloadFont: No font atlas available");
		return false;
	}

	// Verify D3D11 device is valid
	auto device = globals::d3d::device;
	auto context = globals::d3d::context;
	if (!device || !context) {
		logger::error("ReloadFont: D3D11 device or context is null");
		return false;
	}

	// Clear existing fonts from the atlas
	io.Fonts->Clear();
	io.Fonts->TexGlyphPadding = 1;

	ImFontConfig font_config;

	font_config.OversampleH = Constants::FCONF_OVERSAMPLE_H;
	font_config.OversampleV = Constants::FCONF_OVERSAMPLE_V;
	font_config.PixelSnapH = Constants::FCONF_PIXELSNAP_H;
	font_config.RasterizerMultiply = Constants::FCONF_RASTERIZER_MULTIPLY;

	float fontSize = ResolveFontSize(menu);
	auto fontsRoot = Util::PathHelpers::GetFontsPath();
	menu.loadedFontRoles.fill(nullptr);

	std::unordered_map<std::string, ImFont*> atlasCache;
	std::vector<size_t> rolesNeedingFallback;

	for (size_t i = 0; i < static_cast<size_t>(Menu::FontRole::Count); ++i) {
		Menu::FontRole role = static_cast<Menu::FontRole>(i);
		auto& mutableRoleSettings = const_cast<Menu&>(menu).GetFontRoleSettings(role);
		Menu::ThemeSettings::FontRoleSettings effective = themeSettings.FontRoles[i];

		if (effective.SizeScale <= 0.f) {
			effective.SizeScale = Menu::GetFontRoleDefaultScale(role);
		}

		if (effective.File.empty()) {
			effective = Menu::GetDefaultFontRole(role);
		}

		float scaledSize = std::clamp(fontSize * effective.SizeScale, Constants::MIN_FONT_SIZE, Constants::MAX_FONT_SIZE);
		float roundedSize = std::round(scaledSize);
		menu.cachedFontPixelSizesByRole[i] = roundedSize;

		ImFont* loadedFont = nullptr;
		if (!effective.File.empty()) {
			auto fontPath = fontsRoot / effective.File;

			// Security: Validate font path stays within fonts directory
			if (!Util::IsPathWithinDirectory(fontsRoot, fontPath)) {
				logger::error("Security: Font path traversal attempt for role '{}': {}",
					Menu::GetFontRoleKey(role), effective.File);
				effective = Menu::GetDefaultFontRole(role);
				fontPath = fontsRoot / effective.File;
			}

			if (std::filesystem::exists(fontPath)) {
				std::string cacheKey = std::format("{}|{}", effective.File, static_cast<int>(roundedSize));
				auto cached = atlasCache.find(cacheKey);
				if (cached != atlasCache.end()) {
					loadedFont = cached->second;
				} else {
					ImFontConfig cfg = font_config;
					auto* font = io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), roundedSize, &cfg);
					if (font) {
						atlasCache.emplace(cacheKey, font);
						loadedFont = font;
					}
				}
			}
		}

		if (!loadedFont) {
			rolesNeedingFallback.push_back(i);
		} else {
			menu.loadedFontRoles[i] = loadedFont;
			mutableRoleSettings = effective;
			const_cast<Menu&>(menu).cachedFontFilesByRole[i] = effective.File;
		}
	}

	const size_t bodyIndex = static_cast<size_t>(Menu::FontRole::Body);
	if (!menu.loadedFontRoles[bodyIndex]) {
		const auto& defaults = Menu::GetDefaultFontRole(Menu::FontRole::Body);
		float bodySize = std::clamp(fontSize * defaults.SizeScale, Constants::MIN_FONT_SIZE, Constants::MAX_FONT_SIZE);
		float roundedBodySize = std::round(bodySize);
		menu.cachedFontPixelSizesByRole[bodyIndex] = roundedBodySize;

		ImFont* bodyFont = nullptr;
		auto defaultPath = fontsRoot / defaults.File;
		if (std::filesystem::exists(defaultPath)) {
			std::string cacheKey = std::format("{}|{}", defaults.File, static_cast<int>(roundedBodySize));
			ImFontConfig cfg = font_config;
			bodyFont = io.Fonts->AddFontFromFileTTF(defaultPath.string().c_str(), roundedBodySize, &cfg);
			if (bodyFont) {
				atlasCache.emplace(cacheKey, bodyFont);
			}
		}
		if (!bodyFont) {
			bodyFont = io.Fonts->AddFontDefault();
		}

		menu.loadedFontRoles[bodyIndex] = bodyFont;
		const_cast<Menu&>(menu).GetFontRoleSettings(Menu::FontRole::Body) = defaults;
		const_cast<Menu&>(menu).cachedFontFilesByRole[bodyIndex] = defaults.File;
		menu.cachedFontName = defaults.File;
		const_cast<Menu&>(menu).GetSettings().Theme.FontName = defaults.File;
	}

	ImFont* bodyFont = menu.loadedFontRoles[bodyIndex];
	for (size_t idx : rolesNeedingFallback) {
		if (idx == bodyIndex) {
			continue;
		}
		Menu::FontRole role = static_cast<Menu::FontRole>(idx);
		const auto& defaults = Menu::GetDefaultFontRole(role);
		float fallbackSize = std::clamp(fontSize * defaults.SizeScale, Constants::MIN_FONT_SIZE, Constants::MAX_FONT_SIZE);
		menu.cachedFontPixelSizesByRole[idx] = std::round(fallbackSize);
		menu.loadedFontRoles[idx] = bodyFont;
		const_cast<Menu&>(menu).GetFontRoleSettings(role) = defaults;
		const_cast<Menu&>(menu).cachedFontFilesByRole[idx] = defaults.File;
	}

	if (!bodyFont) {
		bodyFont = io.Fonts->AddFontDefault();
		menu.loadedFontRoles[bodyIndex] = bodyFont;
	}

	io.FontDefault = bodyFont ? bodyFont : io.Fonts->AddFontDefault();
	menu.cachedFontName = const_cast<Menu&>(menu).GetFontRoleSettings(Menu::FontRole::Body).File;
	cachedFontSize = fontSize;
	const_cast<Menu&>(menu).GetSettings().Theme.FontName = menu.cachedFontName;
	const_cast<Menu&>(menu).cachedFontSignature = const_cast<Menu&>(menu).BuildFontSignature(fontSize);

	// Build the font atlas - this bakes all fonts into the texture
	if (!io.Fonts->Build()) {
		logger::error("ReloadFont: Failed to build font atlas");

		// Emergency fallback: try to restore with default font before giving up
		io.Fonts->Clear();
		ImFont* fallbackFont = io.Fonts->AddFontDefault();
		if (fallbackFont && io.Fonts->Build()) {
			menu.loadedFontRoles.fill(fallbackFont);
			io.FontDefault = fallbackFont;
		} else {
			logger::error("ReloadFont: Emergency fallback failed");
			return false;
		}
	}

	// Recreate device objects - this is where crashes can occur
	// Must be done between frames with no active rendering state

	// Flush and wait for GPU idle before invalidating resources
	context->Flush();

	winrt::com_ptr<ID3D11Query> eventQuery;
	D3D11_QUERY_DESC queryDesc = { D3D11_QUERY_EVENT, 0 };
	if (SUCCEEDED(device->CreateQuery(&queryDesc, eventQuery.put()))) {
		context->End(eventQuery.get());
		BOOL queryData = FALSE;
		for (int i = 0; i < 1000 && context->GetData(eventQuery.get(), &queryData, sizeof(BOOL), 0) != S_OK; i++) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	ImGui_ImplDX11_InvalidateDeviceObjects();

	if (!ImGui_ImplDX11_CreateDeviceObjects()) {
		logger::error("ReloadFont: Failed to create device objects");

		// Emergency fallback: restore with default font and retry device objects
		io.Fonts->Clear();
		ImFont* fallbackFont = io.Fonts->AddFontDefault();

		bool recoverySucceeded = false;
		if (fallbackFont && io.Fonts->Build()) {
			ImGui_ImplDX11_InvalidateDeviceObjects();
			if (ImGui_ImplDX11_CreateDeviceObjects()) {
				menu.loadedFontRoles.fill(fallbackFont);
				io.FontDefault = fallbackFont;
				menu.cachedFontName = "ImGui Default";
				recoverySucceeded = true;
			}
		}

		if (!recoverySucceeded) {
			logger::error("ReloadFont: Critical failure - unable to recover device objects");
		}

		return false;
	}

	// Verify font texture was created successfully
	if (!io.Fonts->TexIsBuilt) {
		logger::error("ReloadFont: Font texture not created");
		return false;
	}

	float globalScale = themeSettings.GlobalScale;

	// Use default global scale (0.0) for built-in themes when GlobalScale equals the default
	if (std::abs(globalScale - Constants::DEFAULT_GLOBAL_SCALE) < 0.001f) {
		globalScale = Constants::DEFAULT_GLOBAL_SCALE;  // Ensure built-in themes stay at 0.0
	}

	ImGui::GetStyle().FontScaleMain = exp2(globalScale);
	ImGui::GetStyle().FontSizeBase = 0.0f;  // Force UpdateFontsNewFrame to re-detect from font->LegacySize

	cachedFontSize = fontSize;
	// Also update cached font name in the menu instance
	menu.cachedFontName = themeSettings.FontName;

	return true;
}

// Theme management methods
size_t ThemeManager::DiscoverThemes()
{
	if (discovered) {
		return themes.size();
	}

	themes.clear();

	// Collect all theme directories to search
	std::vector<std::filesystem::path> searchPaths;

	// Primary themes directory (always check this first)
	auto themesDir = GetThemesDirectory();
	logger::info("Checking base themes directory: {}", themesDir.string());
	if (std::filesystem::exists(themesDir)) {
		searchPaths.push_back(themesDir);
		logger::info("Base themes directory exists, added to search paths");
	} else {
		logger::warn("Base themes directory does not exist: {}", themesDir.string());
	}

	// Check for MO2 Overwrite directory
	auto dataPath = Util::PathHelpers::GetDataPath();
	auto parentPath = dataPath.parent_path();  // Go up from Data to game root or MO2 instance

	logger::info("Data path: {}", dataPath.string());
	logger::info("Parent path: {}", parentPath.string());

	// MO2 Overwrite path: <MO2 instance>/overwrite/SKSE/Plugins/CommunityShaders/Themes
	auto mo2OverwritePath = parentPath / "overwrite" / "SKSE" / "Plugins" / "CommunityShaders" / "Themes";
	logger::info("Checking MO2 Overwrite path: {}", mo2OverwritePath.string());
	if (std::filesystem::exists(mo2OverwritePath)) {
		searchPaths.push_back(mo2OverwritePath);
		logger::info("Found MO2 Overwrite themes directory");
	} else {
		logger::info("MO2 Overwrite themes directory does not exist");
	}

	if (searchPaths.empty()) {
		logger::info("No theme directories found");
		discovered = true;
		return 0;
	}

	logger::info("Discovering themes in {} directories", searchPaths.size());

	// Search all paths for theme files
	for (const auto& searchPath : searchPaths) {
		logger::info("Searching for themes in: {}", searchPath.string());

		try {
			for (const auto& entry : std::filesystem::directory_iterator(searchPath)) {
				if (!entry.is_regular_file() || entry.path().extension() != ".json") {
					continue;
				}

				// Check file size
				auto fileSize = entry.file_size();
				if (fileSize > MAX_FILE_SIZE) {
					logger::warn("Theme file too large, skipping: {} ({}MB)",
						entry.path().filename().string(), fileSize / (1024 * 1024));
					continue;
				}

				if (themes.size() >= MAX_THEMES) {
					logger::warn("Maximum number of themes ({}) reached, skipping remaining files", MAX_THEMES);
					break;
				}

				auto themeInfo = LoadThemeFile(entry.path());
				if (themeInfo && themeInfo->isValid) {
					themes.push_back(std::move(*themeInfo));
					logger::info("Discovered theme: {} ({})", themes.back().name, themes.back().displayName);
				}
			}
		} catch (const std::filesystem::filesystem_error& e) {
			logger::warn("Error discovering themes in {}: {}", searchPath.string(), e.what());
		}
	}

	// Sort themes alphabetically by display name
	std::sort(themes.begin(), themes.end(), [](const ThemeInfo& a, const ThemeInfo& b) {
		return a.displayName < b.displayName;
	});

	discovered = true;
	logger::info("Theme discovery complete. Found {} themes", themes.size());
	return themes.size();
}

std::vector<std::string> ThemeManager::GetThemeNames() const
{
	std::vector<std::string> names;
	names.reserve(themes.size());

	for (const auto& theme : themes) {
		names.push_back(theme.name);
	}

	return names;
}

bool ThemeManager::LoadTheme(const std::string& themeName, json& themeSettings)
{
	if (!discovered) {
		DiscoverThemes();
	}

	if (themeName.empty()) {
		// Empty theme name means use current/custom theme
		return true;
	}

	std::string safeFileName = Util::FileHelpers::SanitizeFileName(themeName);
	auto it = std::find_if(themes.begin(), themes.end(),
		[&safeFileName](const ThemeInfo& theme) { return theme.name == safeFileName; });

	if (it == themes.end()) {
		logger::warn("Theme not found: {}", themeName);
		return false;
	}

	if (!it->isValid) {
		logger::warn("Theme is invalid: {}", themeName);
		return false;
	}

	try {
		if (it->themeData.contains("Theme") && it->themeData["Theme"].is_object()) {
			themeSettings = it->themeData["Theme"];
			logger::info("Loaded theme: {} ({})", it->name, it->displayName);
			return true;
		} else {
			logger::warn("Theme file missing 'Theme' object: {}", themeName);
			return false;
		}
	} catch (const std::exception& e) {
		logger::warn("Error loading theme {}: {}", themeName, e.what());
		return false;
	}
}

bool ThemeManager::SaveTheme(const std::string& themeName, const json& themeSettings,
	const std::string& displayName, const std::string& description)
{
	if (themeName.empty()) {
		logger::warn("Cannot save theme with empty name");
		return false;
	}
	if (IsPresetTheme(themeName)) {
		logger::warn("Cannot overwrite preset theme: {}", themeName);
		return false;
	}

	// Create the full theme JSON structure
	json fullTheme = {
		{ "DisplayName", displayName.empty() ? themeName : displayName },
		{ "Description", description.empty() ? "Custom user theme" : description },
		{ "Version", "1.0.0" },
		{ "Author", "User" },
		{ "Theme", themeSettings }
	};

	std::string safeFileName = Util::FileHelpers::SanitizeFileName(themeName);
	auto themesDir = GetThemesDirectory();
	auto filePath = themesDir / (safeFileName + ".json");

	logger::info("SaveTheme: Saving theme '{}' to file: {}", themeName, filePath.string());
	logger::debug("SaveTheme: Theme has {} top-level keys", fullTheme.size());

	try {
		// Ensure themes directory exists
		std::filesystem::create_directories(themesDir);
		logger::debug("SaveTheme: Themes directory ensured: {}", themesDir.string());

		// Write the theme file
		std::ofstream file(filePath);
		if (!file.is_open()) {
			logger::warn("Failed to create theme file: {}", filePath.string());
			return false;
		}

		file << fullTheme.dump(4);  // Pretty print with 4-space indentation
		file.close();

		logger::info("Saved theme: {} to {}", themeName, filePath.string());

		// Refresh themes to include the new one
		RefreshThemes();

		return true;
	} catch (const std::exception& e) {
		logger::warn("Error saving theme {}: {}", themeName, e.what());
		return false;
	}
}

const ThemeManager::ThemeInfo* ThemeManager::GetThemeInfo(const std::string& themeName) const
{
	auto it = std::find_if(themes.begin(), themes.end(),
		[&themeName](const ThemeInfo& theme) { return theme.name == themeName; });

	return (it != themes.end()) ? &(*it) : nullptr;
}

void ThemeManager::RefreshThemes()
{
	discovered = false;
	DiscoverThemes();
}

std::filesystem::path ThemeManager::GetThemesDirectory() const
{
	return Util::PathHelpers::GetThemesPath();
}

bool ThemeManager::IsPresetTheme(const std::string& themeName) const
{
	for (const char* preset : ThemePresets::names) {
		if (themeName == preset)
			return true;
	}
	return false;
}

void ThemeManager::CreateDefaultThemeFiles()
{
	auto themesDir = GetThemesDirectory();

	try {
		std::filesystem::create_directories(themesDir);
		logger::info("Ensured themes directory exists: {}", themesDir.string());
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Failed to create themes directory: {}", e.what());
		return;
	}

	// Check if any theme files exist - if so, use those instead of creating defaults
	bool hasThemes = false;
	try {
		for (const auto& entry : std::filesystem::directory_iterator(themesDir)) {
			if (entry.is_regular_file() && entry.path().extension() == ".json") {
				hasThemes = true;
				break;
			}
		}
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Failed to check for existing themes: {}", e.what());
	}

	if (hasThemes) {
		logger::info("Theme files already exist, skipping default creation");
		return;
	}

	// Only create a minimal default theme if no themes exist at all (rare fallback)
	auto defaultThemeFile = themesDir / "Default.json";
	try {
		std::ofstream file(defaultThemeFile);
		if (!file.is_open()) {
			logger::warn("Failed to create default theme file: {}", defaultThemeFile.string());
			return;
		}

		file << R"({
	"DisplayName": "Default Theme",
	"Description": "Default community shaders theme",
	"Version": "1.0",
	"Author": "Community Shaders",
	"Theme": {
		"UseSimplePalette": true,
		"Palette": {
			"Background": [0.05, 0.05, 0.05, 1.0],
			"Text": [1.0, 1.0, 1.0, 1.0],
			"Border": [0.4, 0.4, 0.4, 1.0]
		},
		"FontSize": 27.0,
		"GlobalScale": 0.0,
		"TooltipHoverDelay": 0.5
	}
})";

		file.close();
		logger::info("Created default theme file: {}", defaultThemeFile.string());
	} catch (const std::exception& e) {
		logger::warn("Failed to create default theme file: {}", e.what());
	}
}

std::unique_ptr<ThemeManager::ThemeInfo> ThemeManager::LoadThemeFile(const std::filesystem::path& filePath)
{
	auto themeInfo = std::make_unique<ThemeInfo>();
	themeInfo->name = filePath.stem().string();
	themeInfo->filePath = filePath.string();
	themeInfo->lastModified = GetFileModTime(filePath);

	try {
		std::ifstream file(filePath);
		if (!file.is_open()) {
			logger::warn("Failed to open theme file: {}", filePath.string());
			return themeInfo;
		}

		json data;
		file >> data;

		if (!ValidateThemeData(data)) {
			logger::warn("Invalid theme data in file: {}", filePath.string());
			return themeInfo;
		}

		themeInfo->themeData = data;

		// Extract metadata
		if (data.contains("DisplayName") && data["DisplayName"].is_string()) {
			themeInfo->displayName = data["DisplayName"].get<std::string>();
		} else {
			themeInfo->displayName = themeInfo->name;
		}

		if (data.contains("Description") && data["Description"].is_string()) {
			themeInfo->description = data["Description"].get<std::string>();
		}

		if (data.contains("Version") && data["Version"].is_string()) {
			themeInfo->version = data["Version"].get<std::string>();
		}

		if (data.contains("Author") && data["Author"].is_string()) {
			themeInfo->author = data["Author"].get<std::string>();
		}

		themeInfo->isValid = true;

	} catch (const std::exception& e) {
		logger::warn("Error parsing theme file {}: {}", filePath.string(), e.what());
	}

	return themeInfo;
}

bool ThemeManager::ValidateThemeData(const json& themeData) const
{
	return themeData.contains("Theme") && themeData["Theme"].is_object();
}

float ThemeManager::ResolveFontSize(const Menu& menu)
{
	const auto& settings = menu.GetSettings();

	// When resolution-based font is disabled, use the theme's fixed size directly
	if (!settings.UseResolutionFont) {
		float configured = settings.Theme.FontSize;
		if (std::round(configured) > 0)
			return std::clamp(configured, Constants::MIN_FONT_SIZE, Constants::MAX_FONT_SIZE);
	}

	// Compute dynamic size from screen resolution
	float dynamicSize;
	if (globals::game::graphicsState && globals::game::graphicsState->screenHeight > 0) {
		// Use current screen height
		dynamicSize = (float)globals::game::graphicsState->screenHeight * Constants::DEFAULT_FONT_RATIO;
	} else {
		// Fallback: use default font size
		logger::warn("ThemeManager::ResolveFontSize() - Falling back to Constants::DEFAULT_FONT_SIZE due to missing screen height.");
		dynamicSize = Constants::DEFAULT_FONT_SIZE;
	}
	return std::clamp(dynamicSize, Constants::MIN_FONT_SIZE, Constants::MAX_FONT_SIZE);
}
