#include "Menu/HomePageRenderer.h"
#include "Menu/Layout.h"
#include "Menu/OverlayPolicy.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Real ImGui executes the production theme and welcome layout. Only game
// settings and unrelated colour loading are replaced by test dependencies.
struct Menu
{
	static inline Menu* singleton = nullptr;
	static Menu* GetSingleton() { return singleton; }
	struct Settings
	{
		bool FirstTimeSetupCompleted = false;
		bool ShowCompilationHUDInVR = false;
	} settings;
	const Settings& GetSettings() const { return settings; }
	struct Theme
	{
		ImGuiStyle Style;
		float GlobalScale = 0.0f;
		float TooltipHoverDelay = 0.4f;
		std::array<ImVec4, ImGuiCol_COUNT> FullPalette{};
		struct
		{
			ImVec4 Background{ 0, 0, 0, 1 };
			ImVec4 Text{ 1, 1, 1, 1 };
		} Palette;
		struct
		{
			float Background = 1, Thumb = 1, ThumbHovered = 1, ThumbActive = 1;
		} ScrollbarOpacity;
	} theme;
	const Theme& GetTheme() const { return theme; }
	bool LoadThemePreset(const char*) { return false; }
};
struct ThemeManager
{
	struct Constants
	{
		static constexpr float DEFAULT_GLOBAL_SCALE = 0;
		static constexpr float DEFAULT_FONT_SIZE = 27;
		static constexpr float MIN_SCROLLBAR_SIZE = 28;
		static constexpr float DEFAULT_SCREEN_HEIGHT = 1080;
		static constexpr float DEFAULT_FONT_RATIO = 0.025f;
	};
	static void SetupImGuiStyle(const Menu& menu);
};
namespace logger
{
	void warn(const char*) {}
	void info(const char*) {}
	void error(const char*) {}
}
static void ApplySemanticPalette(ImVec4*, const Menu::Theme&) {}
namespace Util
{
#include "menu_ui_scale_under_test.h"
}
namespace REL
{
	struct Module
	{
		static inline bool vr = false;
		static bool IsVR() { return vr; }
	};
}
#include "menu_theme_style_under_test.h"
bool HomePageRenderer::isFirstTimeSetupShown = false;
#include "menu_welcome_admission_under_test.h"
#include "menu_welcome_layout_under_test.h"
ImGui::TextUnformatted("Welcome to Community Shaders Expanded");
ImGui::End();
ImGui::PopStyleVar(2);
}

struct OverlayRenderer
{
	static bool MoveWindowBelowShaderCompilationStatus(ImVec2&, const ImVec2&, const ImVec2&);
	static bool ExcludeTopStatusWindowsFromLayout(ImVec2&, const ImVec2&, float&);
	static float GetTopStatusWindowBottom(float, const char*);
	static float ShaderBlockingTop(float);
};
struct VR
{
	struct Settings
	{
		enum class OverlayAttachMode
		{
			None,
			HMDOnly,
			ControllerOnly
		};
	};
	bool compatible = true;
	Settings::OverlayAttachMode attachMode = Settings::OverlayAttachMode::HMDOnly;
	bool IsOpenVRCompatible() const { return compatible; }
	Settings::OverlayAttachMode GetEffectiveMenuAttachMode() const { return attachMode; }
};
namespace globals
{
	Menu* menu = nullptr;
	namespace features
	{
		VR vr;
	}
}
#include "menu_status_visibility_under_test.h"

#include "menu_status_overlap_under_test.h"
#include "menu_status_reservation_under_test.h"
float OverlayRenderer::ShaderBlockingTop(float pos)
{
#include "menu_shader_status_stack_under_test.h"
	return yPos;
}
static float WaterStatusTop(float pos)
{
#include "menu_water_status_stack_under_test.h"
	return yPos;
}
struct OverlayFeature
{
	const char* name;
	bool HideFromDesktopWhenSubmittedToVR() const { return true; }
	const char* GetOverlayWindowName() const { return name; }
};
static bool filterDesktopOverlays = false;
static bool ShouldFilterVROverlaysFromDesktop() { return filterDesktopOverlays; }
#include "menu_desktop_status_under_test.h"
struct SettingsWindowLayout
{
	bool constrainedByTopStatusWindow = false;
};
static ImVec2 ApplyStatusLayout(ImVec2 windowPos, ImVec2 windowSizeForOverlap,
	bool willBeDocked = false,
	SettingsWindowLayout defaultWindowLayout = {}, bool resetLayout = false)
{
	const std::string title = "CommunityShaders";
	const ImVec2 defaultWindowPos(200, 250);
	const ImVec2 defaultWindowSize(300, 300);
	const ImVec2 centeredPivot(0.5f, 0.5f);
#include "menu_status_layout_under_test.h"
	return windowPos;
}

static bool CheckTraceMenuPosition()
{
	bool passed = true;
	ImGui::CreateContext();
	auto& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = ImVec2(1920, 1080);
	io.Fonts->AddFontDefault();
	io.Fonts->Build();
	for (const char* name : { "ShaderCompilationInfo", "ShaderBlockingInfo", "UWCacheCreationInfo" }) {
		ImVec2 position(200, 250);
		for (int frame = 0; frame < 28; ++frame) {
			ImGui::NewFrame();
			const bool statusVisible = frame < 20;
			const float height = frame < 10 ? 200.0f : (frame < 16 ? 280.0f : 80.0f);
			if (statusVisible) {
				ImGui::SetNextWindowPos(ImVec2(0, 0));
				ImGui::SetNextWindowSize(ImVec2(500, height));
				ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoSavedSettings);
				ImGui::TextUnformatted("Trace status");
				ImGui::End();
			}
			position = ApplyStatusLayout(position, ImVec2(300, 300));
			const float expectedY = statusVisible && height > 100 ? height + ImGui::GetStyle().ItemSpacing.y + 150 : 250;
			if (frame > 2 && std::abs(position.y - expectedY) > 0.01f) {
				std::printf("FAIL: %s moves settings to y=%.1f instead of %.1f on frame %d\n", name, position.y, expectedY, frame);
				passed = false;
			}
			ImGui::SetNextWindowPos(position, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(300, 300));
			ImGui::Begin("CommunityShaders");
			ImGui::TextUnformatted("Full settings menu");
			ImGui::End();
			ImGui::Render();
		}
	}
	// A user drag away from the status panel must replace the saved placement.
	ImGui::NewFrame();
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(500, 200));
	ImGui::Begin("ShaderCompilationInfo");
	ImGui::End();
	const auto displaced = ApplyStatusLayout(ImVec2(200, 250), ImVec2(300, 300));
	auto* settingsWindow = ImGui::FindWindowByName("CommunityShaders");
	ImGui::StartMouseMovingWindow(settingsWindow);
	const ImVec2 draggedPosition(1000, 500);
	ImGui::SetWindowPos(settingsWindow, ImVec2(850, 350));
	const auto dragged = ApplyStatusLayout(draggedPosition, ImVec2(300, 300));
	if (displaced.y <= 250 || dragged.x != draggedPosition.x || dragged.y != draggedPosition.y) {
		std::puts("FAIL: status displacement prevents moving the flat menu away from the panel");
		passed = false;
	}
	ImGui::GetCurrentContext()->MovingWindow = nullptr;
	const auto afterDrag = ApplyStatusLayout(draggedPosition, ImVec2(300, 300));
	passed = passed && afterDrag.x == draggedPosition.x && afterDrag.y == draggedPosition.y;
	ImGui::Render();
	ImGui::NewFrame();
	const ImVec2 original(700, 500);
	const ImVec2 size(300, 300);
	const auto docked = ApplyStatusLayout(original, size, true);
	passed = passed && docked.x == original.x && docked.y == original.y;
	REL::Module::vr = true;
	Menu vrMenu;
	// VR rejects this flat-only dialog before accessing any saved menu settings.
	passed = passed && !HomePageRenderer::ShouldShowFirstTimeSetup();
	ImGui::GetStyle().FontScaleDpi = 1.5f;
	ImGui::GetStyle()._NextFrameFontSizeBase = 33.0f;
	const float vrFontBase = ImGui::GetStyle().FontSizeBase;
	ThemeManager::SetupImGuiStyle(vrMenu);
	const auto& vrStyle = ImGui::GetStyle();
	passed = passed && vrStyle.FontSizeBase == vrFontBase &&
	         vrStyle.FontScaleDpi == 1.5f && vrStyle._NextFrameFontSizeBase == 33.0f;
	const auto lockedVR = ApplyStatusLayout(original, size, false, { true });
	const auto restoredVR = ApplyStatusLayout(lockedVR, size);
	if (lockedVR.x != 200 || lockedVR.y != 250 || restoredVR.x != original.x || restoredVR.y != original.y) {
		std::puts("FAIL: VR status layout must restore the saved placement after avoiding status panels");
		passed = false;
	}
	ApplyStatusLayout(original, size, false, { true });
	const ImVec2 resetPosition(900, 600);
	const auto resetVR = ApplyStatusLayout(resetPosition, size, false, {}, true);
	if (resetVR.x != resetPosition.x || resetVR.y != resetPosition.y) {
		std::puts("FAIL: resetting layout restores a stale pre-status position");
		passed = false;
	}
	REL::Module::vr = false;
	Menu::singleton = &vrMenu;
	passed = passed && HomePageRenderer::ShouldShowFirstTimeSetup();
	vrMenu.settings.FirstTimeSetupCompleted = true;
	passed = passed && !HomePageRenderer::ShouldShowFirstTimeSetup();
	Menu::singleton = nullptr;
	ImGui::Render();
	ImGui::DestroyContext();
	return passed;
}

static bool CheckStatusPresentationLayout()
{
	bool passed = true;
	using AttachMode = VR::Settings::OverlayAttachMode;
	for (bool isVR : { false, true }) {
		for (bool compatible : { false, true }) {
			for (auto attachMode : { AttachMode::None, AttachMode::HMDOnly, AttachMode::ControllerOnly }) {
				for (const char* name : { "ShaderCompilationInfo", "UWCacheCreationInfo", "ShaderBlockingInfo" }) {
					ImGui::CreateContext();
					auto& io = ImGui::GetIO();
					io.IniFilename = nullptr;
					io.DisplaySize = ImVec2(1920, 1620);
					io.Fonts->AddFontDefault();
					io.Fonts->Build();
					Menu menu;
					globals::menu = &menu;
					globals::features::vr = { compatible, attachMode };
					REL::Module::vr = isVR;
					const ImVec2 original(700, 800);
					ImVec2 position = original;
					for (int frame = 0; frame < 32; ++frame) {
						ImGui::NewFrame();
						const bool statusPresent = frame >= 4 && frame < 26;
						menu.settings.ShowCompilationHUDInVR = frame >= 12 && frame < 20;
						const float height = frame % 4 < 2 ? 80.0f : 300.0f;
						if (statusPresent) {
							ImGui::SetNextWindowPos(ImVec2(12, 8));
							ImGui::SetNextWindowSize(ImVec2(600, height));
							ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoSavedSettings);
							ImGui::TextUnformatted("Changing desktop status");
							ImGui::End();
						}
						const bool hiddenFromHMD = isVR && compatible && attachMode != AttachMode::None &&
						                           std::strcmp(name, "ShaderCompilationInfo") == 0 && !menu.settings.ShowCompilationHUDInVR;
						const bool expectedReservation = statusPresent && !hiddenFromHMD;
						ImVec2 minimum(2, 2);
						float left = 2;
						const bool reserved = OverlayRenderer::ExcludeTopStatusWindowsFromLayout(minimum, ImVec2(1918, 1618), left);
						const float expectedMinY = expectedReservation ? 8 + height + ImGui::GetStyle().ItemSpacing.y : 2;
						const float nextStatusTop = OverlayRenderer::GetTopStatusWindowBottom(2, nullptr);
						const float selfExcludedTop = OverlayRenderer::GetTopStatusWindowBottom(2, name);
						if (nextStatusTop != expectedMinY || selfExcludedTop != 2) {
							std::puts("FAIL: status stacking reserves invisible panels or stacks a panel below itself");
							passed = false;
						}
						if (reserved != expectedReservation || minimum.y != expectedMinY || left != (expectedReservation ? 12 : 2)) {
							std::printf("FAIL: %s reserves incorrect canvas space (VR %d compatible %d mode %d frame %d)\n",
								name, isVR, compatible, static_cast<int>(attachMode), frame);
							passed = false;
						}
						if (isVR) {
							position = ApplyStatusLayout(position, ImVec2(300, 300), false, { reserved }, frame == 0);
							const ImVec2 expectedPosition = expectedReservation ? ImVec2(200, 250) : original;
							if (position.x != expectedPosition.x || position.y != expectedPosition.y) {
								std::puts("FAIL: hidden status moves the HMD menu or visible status fails to restore placement");
								passed = false;
							}
						}
						ImGui::Render();
					}
					ImGui::DestroyContext();
				}
			}
		}
	}
	globals::menu = nullptr;
	REL::Module::vr = false;
	return passed;
}

static bool CheckCombinedStatusStacking()
{
	bool passed = true;
	for (bool blocking : { false, true }) {
		ImGui::CreateContext();
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = ImVec2(1920, 1620);
		io.Fonts->AddFontDefault();
		io.Fonts->Build();
		Menu menu;
		globals::menu = &menu;
		globals::features::vr = { true, VR::Settings::OverlayAttachMode::HMDOnly };
		REL::Module::vr = true;
		for (int frame = 0; frame < 24; ++frame) {
			ImGui::NewFrame();
			menu.settings.ShowCompilationHUDInVR = frame >= 12;
			const float shaderHeight = frame % 2 ? 80.0f : 300.0f;
			const float spacing = ImGui::GetStyle().ItemSpacing.y;
			ImGui::SetNextWindowPos(ImVec2(8, 8));
			ImGui::SetNextWindowSize(ImVec2(600, shaderHeight));
			ImGui::Begin("ShaderCompilationInfo");
			ImGui::End();
			float expectedWaterTop = menu.settings.ShowCompilationHUDInVR ? 8 + shaderHeight + spacing : 8;
			if (blocking) {
				const float top = OverlayRenderer::ShaderBlockingTop(8);
				passed &= top == expectedWaterTop;
				ImGui::SetNextWindowPos(ImVec2(8, top));
				ImGui::SetNextWindowSize(ImVec2(600, 80));
				ImGui::Begin("ShaderBlockingInfo");
				ImGui::End();
				expectedWaterTop += 80 + spacing;
			}
			const float waterTop = WaterStatusTop(8);
			passed &= waterTop == expectedWaterTop;
			ImGui::SetNextWindowPos(ImVec2(8, waterTop));
			ImGui::SetNextWindowSize(ImVec2(600, 80));
			ImGui::Begin("UWCacheCreationInfo");
			ImGui::End();
			ImVec2 minimum(2, 2);
			float left = 2;
			const bool reserved = OverlayRenderer::ExcludeTopStatusWindowsFromLayout(minimum, ImVec2(1918, 1618), left);
			passed &= reserved && minimum.y == expectedWaterTop + 80 + spacing;
			ImGui::Render();
		}
		ImGui::DestroyContext();
	}
	globals::menu = nullptr;
	REL::Module::vr = false;
	if (!passed)
		std::puts("FAIL: hidden shader status indirectly displaces water or shader-blocking panels");
	return passed;
}

static bool CheckPassiveDesktopStatus()
{
	bool passed = true;
	ImGui::CreateContext();
	auto& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = ImVec2(1920, 1620);
	io.Fonts->AddFontDefault();
	io.Fonts->Build();
	Menu menu;
	globals::menu = &menu;
	io.MouseDrawCursor = true;
	OverlayFeature privateOverlay{ "PrivateOverlay" };
	const std::vector<OverlayFeature*> overlays{ &privateOverlay };
	for (int frame = 0; frame < 24; ++frame) {
		REL::Module::vr = frame < 12 || frame >= 16;
		globals::features::vr.compatible = frame < 20;
		globals::features::vr.attachMode = frame >= 8 && frame < 12 ?
		                                       VR::Settings::OverlayAttachMode::None :
		                                       VR::Settings::OverlayAttachMode::HMDOnly;
		menu.settings.ShowCompilationHUDInVR = frame >= 4 && frame < 8;
		io.AddMousePosEvent(30, 30);
		ImGui::NewFrame();
		const bool statusPresent = frame % 8 >= 2 && frame % 8 < 6;
		if (statusPresent) {
			ImGui::SetNextWindowPos(ImVec2(8, 8));
			ImGui::SetNextWindowSize(ImVec2(600, 150));
			ImGui::Begin("ShaderCompilationInfo", nullptr, kShaderStatusWindowFlags);
			ImGui::TextUnformatted("Passive shader status");
			ImGui::End();
		}
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImVec2(700, 700));
		ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoTitleBar);
		ImGui::TextUnformatted("Settings under desktop-only status");
		ImGui::End();
		if (statusPresent) {
			auto* status = ImGui::FindWindowByName("ShaderCompilationInfo");
			if (frame % 4 < 2)
				ImGui::BringWindowToDisplayFront(status);
			else
				ImGui::BringWindowToDisplayBack(status);
		}
		if (frame > 1) {
			const auto* settings = ImGui::FindWindowByName("Settings");
			passed &= ImGui::GetCurrentContext()->HoveredWindow == settings && ImGui::GetCurrentContext()->NavWindow == settings;
		}
		ImGui::SetNextWindowPos(ImVec2(900, 900));
		ImGui::Begin("PrivateOverlay", nullptr, ImGuiWindowFlags_NoFocusOnAppearing);
		ImGui::TextUnformatted("Private overlay");
		ImGui::End();
		ImGui::BeginTooltip();
		ImGui::TextUnformatted("Settings tooltip");
		ImGui::EndTooltip();
		ImGui::Render();
		auto* original = ImGui::GetDrawData();
		const auto* foreground = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
		std::vector<ImDrawList*> sourceLists(original->CmdLists.begin(), original->CmdLists.end());
		for (bool filter : { false, true }) {
			filterDesktopOverlays = filter;
			ImDrawData filtered;
			auto* desktop = BuildDesktopDrawData(original, overlays, filtered);
			std::vector<ImDrawList*> expected;
			ImDrawList* shader = nullptr;
			const bool desktopOnly = frame < 4 || (frame >= 16 && frame < 20);
			for (auto* list : sourceLists) {
				if (filter && list->_OwnerName && std::strcmp(list->_OwnerName, "PrivateOverlay") == 0)
					continue;
				if (desktopOnly && list->_OwnerName && std::strcmp(list->_OwnerName, "ShaderCompilationInfo") == 0)
					shader = list;
				else
					expected.push_back(list);
			}
			std::vector<ImDrawList*> retained;
			bool sawShader = false;
			int vertices = 0, indices = 0;
			for (int index = 0; index < desktop->CmdLists.Size; ++index) {
				auto* list = desktop->CmdLists[index];
				vertices += list->VtxBuffer.Size;
				indices += list->IdxBuffer.Size;
				if (list == shader) {
					sawShader = true;
					continue;
				}
				retained.push_back(list);
				if (shader) {
					const auto* window = list->_OwnerName ? ImGui::FindWindowByName(list->_OwnerName) : nullptr;
					const bool aboveStatus = list == foreground || (window && (window->Flags & ImGuiWindowFlags_Tooltip));
					passed &= aboveStatus == sawShader;
				}
			}
			passed &= retained == expected && sawShader == (shader != nullptr);
			passed &= desktop->TotalVtxCount == vertices && desktop->TotalIdxCount == indices;
			passed &= std::equal(sourceLists.begin(), sourceLists.end(), original->CmdLists.begin());
		}
	}
	ImGui::DestroyContext();
	globals::menu = nullptr;
	REL::Module::vr = false;
	if (!passed)
		std::puts("FAIL: passive desktop status steals HMD input, becomes unreadable, or mutates shared drawing");
	return passed;
}

int main()
{
	bool passed = CheckTraceMenuPosition();
	passed &= CheckStatusPresentationLayout();
	passed &= CheckCombinedStatusStacking();
	passed &= CheckPassiveDesktopStatus();
	for (float fontSize : { 21.0f, 27.0f, 42.0f, 54.0f }) {
		for (float globalScale : { 0.0f, 0.5f }) {
			for (bool traceOverlay : { false, true }) {
				ImGui::CreateContext();
				auto& io = ImGui::GetIO();
				io.IniFilename = nullptr;
				io.DisplaySize = ImVec2(3840, 2160);
				ImFontConfig config;
				config.SizePixels = fontSize;
				io.FontDefault = io.Fonts->AddFontDefault(&config);
				io.Fonts->Build();
				Menu menu;
				menu.theme.GlobalScale = globalScale;
				ImGui::GetStyle().FontScaleMain = std::exp2(globalScale);
				float previousWelcomeFont = 0;
				ImVec2 previousWelcomeSize{};
				for (int frame = 0; frame < 12; ++frame) {
					ImGui::NewFrame();
					const auto applyTheme = [&] {
						const auto before = ImGui::GetStyle();
						ThemeManager::SetupImGuiStyle(menu);
						const auto& after = ImGui::GetStyle();
						if (after.FontSizeBase != before.FontSizeBase || after.FontScaleDpi != before.FontScaleDpi ||
							after._NextFrameFontSizeBase != before._NextFrameFontSizeBase) {
							std::printf("FAIL: theme replaced live font state (base %.3f -> %.3f)\n", before.FontSizeBase, after.FontSizeBase);
							passed = false;
						}
					};
					applyTheme();
					const auto originalStyle = ImGui::GetStyle();
					ImGui::GetStyle().FontScaleDpi = 1.5f;
					ImGui::GetStyle()._NextFrameFontSizeBase = fontSize + 3.0f;
					applyTheme();
					ImGui::GetStyle() = originalStyle;
					if (traceOverlay) {
						ImGui::Begin("ShaderCompilationInfo");
						ImGui::TextUnformatted("Compiling Shaders");
						ImGui::End();
					}
					ImGui::Begin("Settings");
					ImGui::PushFont(io.FontDefault, fontSize * 1.2f);
					applyTheme();
					ImGui::TextUnformatted("Settings heading");
					ImGui::PopFont();
					ImGui::TextUnformatted("Settings body");
					ImGui::End();
					HomePageRenderer::RenderFirstTimeSetupDialog();
					auto* window = ImGui::FindWindowByName("##FirstTimeSetup");
					const float welcomeFont = window->FontWindowScale * fontSize;
					if (frame > 2 && (std::abs(welcomeFont - previousWelcomeFont) > 0.01f ||
										 window->Size.x != previousWelcomeSize.x || window->Size.y != previousWelcomeSize.y)) {
						std::printf("FAIL: welcome layout alternates at font %.1f scale %.1f trace %d frame %d (%.3f -> %.3f)\n",
							fontSize, globalScale, traceOverlay, frame, previousWelcomeFont, welcomeFont);
						passed = false;
					}
					previousWelcomeFont = welcomeFont;
					previousWelcomeSize = window->Size;
					ImGui::Render();
				}
				ImGui::DestroyContext();
			}
		}
	}
	if (passed)
		std::puts("Stable welcome layout and live font state across 16 font/scale/overlay configurations.");
	return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
