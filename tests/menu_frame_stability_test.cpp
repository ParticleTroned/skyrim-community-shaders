#include "Menu/HomePageRenderer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

// Real ImGui executes the production theme and welcome layout. Only game
// settings and unrelated colour loading are replaced by test dependencies.
struct Menu
{
	static inline Menu* singleton = nullptr;
	static Menu* GetSingleton() { return singleton; }
	struct Settings
	{
		bool FirstTimeSetupCompleted = false;
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
};
#include "menu_status_overlap_under_test.h"
struct SettingsWindowLayout
{
	bool constrainedByTopStatusWindow = false;
};
static ImVec2 ApplyStatusLayout(ImVec2 windowPos, ImVec2 windowSizeForOverlap,
	bool willBeDocked = false, bool vrMenuLayoutUnlocked = false,
	SettingsWindowLayout defaultWindowLayout = {})
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
	ThemeManager::SetupImGuiStyle(vrMenu);
	const auto& vrStyle = ImGui::GetStyle();
	passed = passed && vrStyle.FontSizeBase == vrMenu.theme.Style.FontSizeBase &&
	         vrStyle.FontScaleDpi == vrMenu.theme.Style.FontScaleDpi &&
	         vrStyle._NextFrameFontSizeBase == vrMenu.theme.Style._NextFrameFontSizeBase;
	const auto lockedVR = ApplyStatusLayout(original, size, false, false, { true });
	const auto restoredVR = ApplyStatusLayout(lockedVR, size);
	const auto unlockedVR = ApplyStatusLayout(original, size, false, true, { true });
	passed = passed && lockedVR.x == 200 && lockedVR.y == 250 && restoredVR.x == 200 && restoredVR.y == 250 &&
	         unlockedVR.x == original.x && unlockedVR.y == original.y;
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

int main()
{
	bool passed = CheckTraceMenuPosition();
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
