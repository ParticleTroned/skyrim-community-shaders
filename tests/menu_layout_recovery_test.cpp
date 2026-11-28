#include "CSEditor/Layout.h"
#include "Features/VR/MenuPositioningPolicy.h"
#include "Menu/Layout.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

namespace SKSE::stl
{
	template <class Callback>
	struct scope_exit
	{
		Callback callback;
		~scope_exit() { callback(); }
	};
}

namespace REL
{
	struct Module
	{
		static inline bool vr = false;
		static bool IsVR() { return vr; }
	};
}
namespace logger
{
	void info(const char*) {}
}
namespace globals::features
{
	struct VR
	{
		int resets = 0;
		void ResetMenuLayout() { ++resets; }
		bool IsMenuPointerInHeadset() const { return false; }
	} vr;
}
struct FeatureListRenderer
{
	static inline int resets = 0;
	static void ResetLayout() { ++resets; }
};
struct ThemeManager
{
	struct Constants
	{
		static constexpr float DEFAULT_GLOBAL_SCALE = 0;
		static constexpr float HEADER_BASE_ICON_MULTIPLIER = 1.5f;
		static constexpr float DOCKED_ICON_SIZE_MULTIPLIER = 1.0f;
	};
};
constexpr float kHeaderActionIconScale = 1.0f;
#include "menu_header_scale_under_test.h"

struct SettingsWindowLayout
{
	ImVec2 center, size;
	bool constrainedByTopStatusWindow = false;
};
#include "menu_canvas_bounds_under_test.h"

struct OverlayRenderer
{
	static bool MoveWindowBelowShaderCompilationStatus(ImVec2&, const ImVec2&, const ImVec2&) { return false; }
};

constexpr uint32_t VK_CONTROL = 17, VK_SHIFT = 16, VK_END = 35;
struct InputCombo
{
	uint32_t key;
	static inline bool ctrl = false, shift = false, alt = false;
	static InputCombo Keyboard(uint32_t key) { return { key }; }
	static bool MatchesKeyboardCombo(const std::vector<InputCombo>& combo, uint32_t key)
	{
		return combo.size() == 3 && combo[0].key == VK_CONTROL && combo[1].key == VK_SHIFT &&
		       combo[2].key == key && ctrl && shift && !alt;
	}
};
struct KeyEvent
{
	uint32_t key;
	bool pressed, repeat = false;
	bool IsDown() const { return pressed && !repeat; }
	bool IsPressed() const { return pressed; }
};
struct Menu
{
	enum class FontRole
	{
		Subheading
	};
	struct ThemeSettings
	{
		float FontSize = 27;
		ImGuiStyle Style;
		struct Role
		{
			float SizeScale = 1;
		};
		std::array<Role, 5> FontRoles;
	};
	struct Settings
	{
		ThemeSettings Theme;
		float GlobalScale = 0;
		bool UseResolutionFont = false;
		int untouchedGraphicsSetting = 42;
	} settings;
	std::atomic<bool> pendingLayoutReset = false;
	bool resetLayout = false, pendingFontReload = false, layoutResetKeyHeld = false;
	bool resetMenuDocking = false;
	bool settingToggleKey = false, settingSkipCompilationKey = false, settingsEffectsToggle = false;
	bool settingNeuralRenderingToggleKey = false, settingOverlayToggleKey = false;
	bool settingShaderBlockPrevKey = false, settingShaderBlockNextKey = false;
	bool settingCSEditorToggleKey = false, settingScreenshotKey = false;
	bool baseline = false, dirty = false, open = false, cancelledMeasurement = false;
	bool IsEnabled = true;
	std::string featureSearch, pendingFeatureSelection;
	int requests = 0, forwardedKeys = 0;
	std::unordered_set<uint32_t> _comboFiredKeys;
	void EnsureSettingsDirtyBaseline() { baseline = true; }
	void RequestSettingsDirtyCheck() { dirty = true; }
	void OpenMenu(bool cancel)
	{
		open = true;
		cancelledMeasurement = cancel;
	}
	void RequestLayoutReset()
	{
		++requests;
		pendingLayoutReset = true;
	}
	void ApplyPendingLayoutReset();
	void OnFocusChanged()
	{
#include "menu_recovery_focus_under_test.h"
	}
	ImRect DrawGeometry(ImVec2 windowPos, ImVec2 windowSizeForOverlap, SettingsWindowLayout defaultWindowLayout, bool willBeDocked = false)
	{
		const std::string title = "Layout recovery###CommunityShaders";
		const bool vrMenuLayoutUnlocked = false;
		const bool useSteamVRWindowControls = false, useOpenCompositeStableHeader = false;
		const auto layoutCond = resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
		const auto defaultWindowPos = defaultWindowLayout.center;
		const auto defaultWindowSize = defaultWindowLayout.size;
		const ImVec2 centeredPivot(.5f, .5f);
#include "menu_status_layout_under_test.h"
#include "menu_window_geometry_under_test.h"
		if (contentsVisible)
			ImGui::TextUnformatted("Recovered settings");
		const auto* window = ImGui::GetCurrentWindow();
		const ImRect result(window->Pos, { window->Pos.x + window->Size.x, window->Pos.y + window->Size.y });
		ImGui::End();
		return result;
	}
	void ProcessKeys(const std::vector<KeyEvent>& events)
	{
		for (const auto& event : events) {
			const auto key = event.key;
#include "menu_recovery_shortcut_under_test.h"
			if (!event.IsPressed() && _comboFiredKeys.erase(key))
				continue;
			++forwardedKeys;
		}
	}
};
#include "menu_recovery_under_test.h"

namespace MenuFonts
{
	struct FontRoleGuard
	{
		explicit FontRoleGuard(Menu::FontRole) {}
	};
}
#include "menu_sidebar_font_under_test.h"

bool Check(bool result, const char* message)
{
	if (!result)
		std::printf("FAIL: %s\n", message);
	return result;
}

bool CheckEditorLayout()
{
	bool passed = true;
	using namespace CSXEditorLayout;
	for (const ImVec2 canvas : { ImVec2(640, 480), ImVec2(1536, 1380), ImVec2(1920, 1080), ImVec2(3440, 1440), ImVec2(1080, 1920) })
		for (const float scale : { .5f, 1.0f, 2.0f }) {
			const ImVec2 origin{ 120.25f, 60.75f };
			const auto layout = Defaults(origin, canvas, 10 * scale, 960 * scale, canvas.x / canvas.y, { 16 * scale, 40 * scale }, true, true);
			passed &= Check(layout.has_value(), "editor defaults accept supported canvases and scales");
			if (!layout)
				continue;
			for (const auto panel : { layout->browser, layout->viewport, layout->palette })
				passed &= Check(panel.size.x > 0 && panel.size.y > 0 && panel.position.x >= origin.x && panel.position.y >= origin.y &&
									panel.position.x + panel.size.x <= origin.x + canvas.x && panel.position.y + panel.size.y <= origin.y + canvas.y,
					"all default editor panels fit the usable canvas");
			passed &= Check(layout->browser.position.y == layout->viewport.position.y && layout->viewport.position.x == layout->palette.position.x &&
								layout->viewport.size.x == layout->palette.size.x && layout->browser.position.y + layout->browser.size.y == layout->palette.position.y + layout->palette.size.y,
				"browser, viewport and palette share aligned top, right-column and bottom edges");
			passed &= Check(layout->browser.position.x + layout->browser.size.x < layout->viewport.position.x &&
								layout->viewport.position.y + layout->viewport.size.y < layout->palette.position.y,
				"editor columns and stacked panels retain their gaps");
			for (const bool showViewport : { false, true }) {
				const auto single = Defaults(origin, canvas, 10 * scale, 960 * scale, canvas.x / canvas.y, { 16 * scale, 40 * scale }, showViewport, !showViewport);
				const auto& right = showViewport ? single->viewport : single->palette;
				passed &= Check(right.position.y == single->browser.position.y && right.size.y == single->browser.size.y,
					"a single right-hand editor panel fills the browser height");
			}
		}
	passed &= Check(!Defaults({}, {}, 10, 960, 1, {}, true, true) &&
						!Defaults({}, { 1920, 1080 }, 10, 960, std::numeric_limits<float>::quiet_NaN(), {}, true, true),
		"invalid editor canvases and aspect ratios do not produce window geometry");
	passed &= Check(ImHashStr(BrowserTitle) == ImHashStr("CS Editor Browser"), "the CSX Editor title retains its saved browser identity");
	const auto layout = Defaults({}, { 1920, 1080 }, 10, 960, 16.0f / 9.0f, { 16, 40 }, true, true);
	const auto draw = [&](const Panel& panel, const char* name, ImGuiCond condition) {
		ApplyDefaults(panel, condition);
		ImGui::Begin(name);
		const auto result = ImRect(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y));
		ImGui::End();
		return result;
	};
	ImGui::GetIO().DisplaySize = { 1920, 1080 };
	ImGui::NewFrame();
	const auto browser = draw(layout->browser, BrowserTitle, ImGuiCond_FirstUseEver);
	const auto viewport = draw(layout->viewport, "Editor test viewport", ImGuiCond_FirstUseEver);
	const auto palette = draw(layout->palette, "Editor test palette", ImGuiCond_FirstUseEver);
	passed &= Check(browser.Min.y == viewport.Min.y && viewport.Min.x == palette.Min.x && viewport.Max.x == palette.Max.x && browser.Max.y == palette.Max.y,
		"native ImGui windows retain the aligned first-use geometry");
	ImGui::Render();
	ImGui::LoadIniSettingsFromMemory("[Window][CS Editor Browser]\nPos=40,80\nSize=500,650\nCollapsed=0\n");
	ImGui::NewFrame();
	const auto restored = draw(layout->browser, BrowserTitle, ImGuiCond_FirstUseEver);
	passed &= Check(restored.Min.x == 40 && restored.Min.y == 80 && restored.GetWidth() == 500 && restored.GetHeight() == 650,
		"renamed editor browser preserves an existing saved arrangement");
	ImGui::Render();
	ImGui::NewFrame();
	const auto reset = draw(layout->browser, BrowserTitle, ImGuiCond_Always);
	passed &= Check(reset.Min.x == browser.Min.x && reset.Min.y == browser.Min.y && reset.GetWidth() == browser.GetWidth() && reset.GetHeight() == browser.GetHeight(),
		"explicit editor layout reset restores the aligned defaults");
	ImGui::Render();
	return passed;
}

bool CheckWindowGeometry()
{
	bool passed = true;
	Menu menu;
	for (bool vr : { false, true }) {
		REL::Module::vr = vr;
		for (ImVec2 canvas : { ImVec2(640, 480), ImVec2(1920, 1080), ImVec2(1920, 1620) }) {
			ImGui::GetIO().DisplaySize = canvas;
			ImGui::GetStyle().WindowMinSize = { 8000, 8000 };
			ImGui::NewFrame();
			const auto window = menu.DrawGeometry({ -5000, 9000 }, { 64, 64 },
				{ { canvas.x * .5f, canvas.y * .5f }, { canvas.x - 16, canvas.y - 16 } });
			passed &= Check(window.Min.x >= 8 && window.Min.y >= 8 && window.Max.x <= canvas.x - 8 && window.Max.y <= canvas.y - 8,
				"production window bounds override oversized saved theme minima on desktop and VR");
			passed &= Check(ImGui::GetStyle().WindowMinSize.x == 8000,
				"window constraints restore style before rendering child panels");
			ImGui::Render();
		}
	}
	ImGui::GetIO().DisplaySize = { 1920, 1080 };
	ImGui::GetStyle().WindowMinSize = { 32, 32 };
	ImGui::NewFrame();
	const float font = ImGui::GetFontSize();
	const auto repaired = menu.DrawGeometry({ -5000, 9000 }, { 64, 64 }, { { 960, 540 }, { 1800, 900 } });
	passed &= Check(repaired.GetWidth() >= font * MenuUI::Layout::MinimumWidthInFonts &&
						repaired.GetHeight() >= font * MenuUI::Layout::MinimumHeightInFonts,
		"production window sizing repairs the saved tiny window using text dimensions");
	ImGui::Render();
	ImGui::NewFrame();
	const auto constrained = menu.DrawGeometry({ 960, 900 }, { 1000, 200 }, { { 960, 900 }, { 1000, 200 }, true });
	passed &= Check(constrained.Min.y == 800 && constrained.Max.y == 1072,
		"height extension retains the space above the menu reserved for compilation status");
	ImGui::Render();
	ImGui::NewFrame();
	const auto statusBounded = menu.DrawGeometry({ 960, 540 }, { 1800, 1050 }, { { 960, 900 }, { 1000, 200 }, true });
	passed &= Check(statusBounded.Min.y >= 800 && statusBounded.Max.y <= 1072,
		"canvas repair cannot move a tall saved menu back over compilation or RenderDoc status");
	ImGui::Render();
	REL::Module::vr = false;
	return passed;
}

bool CheckWindowHeight()
{
	bool passed = true;
	Menu menu;
	for (bool vr : { false, true }) {
		REL::Module::vr = vr;
		for (ImVec2 canvas : { ImVec2(1920, 1080), ImVec2(1920, 1620) }) {
			ImGui::GetIO().DisplaySize = canvas;
			menu.resetLayout = true;
			for (const float top : { 100.0f, 400.0f, 320.0f, 160.0f }) {
				if (!vr && top != 100)
					continue;
				ImGui::NewFrame();
				const float font = ImGui::GetFontSize();
				const ImVec2 center{ 800, top + 300 };
				const auto bounds = menu.DrawGeometry(center, { 1200, 600 }, { center, { 1200, 600 }, top != 100 });
				passed &= Check(bounds.Min.x == 200 && bounds.Min.y == top && bounds.GetWidth() == 1200,
					"desktop and headset height extension preserves the top edge and width");
				passed &= Check(bounds.Max.y == canvas.y - MenuUI::Layout::CanvasPadding,
					"desktop and headset retain the extra height as compilation status changes");
				passed &= Check(ImGui::GetFontSize() == font, "height extension does not scale typography");
				ImGui::Render();
			}
		}
	}
	REL::Module::vr = false;
	ImGui::GetIO().DisplaySize = { 1920, 1080 };
	return passed;
}

bool CheckStatusHeightRestoration()
{
	bool passed = true;
	REL::Module::vr = true;
	ImGui::GetIO().DisplaySize = { 1920, 1620 };
	Menu menu;
	const SettingsWindowLayout defaults{ { 960, 810 }, { 1800, 1200 } };
	const SettingsWindowLayout status{ { 960, 960 }, { 1500, 900 }, true };
	for (const bool customSize : { false, true }) {
		menu.resetLayout = true;
		ImGui::NewFrame();
		auto bounds = menu.DrawGeometry(defaults.center, defaults.size, defaults);
		ImGui::Render();
		if (customSize) {
			auto* window = ImGui::FindWindowByName("###CommunityShaders");
			ImGui::SetWindowPos(window, { 210, 250 });
			ImGui::SetWindowSize(window, { 1500, 900 });
			bounds = { { 210, 250 }, { 1710, 1150 } };
		}
		const auto saved = bounds;
		for (int frame = 0; frame < 5; ++frame) {
			ImGui::NewFrame();
			const bool statusVisible = frame < 3;
			bounds = menu.DrawGeometry(bounds.GetCenter(), bounds.GetSize(), statusVisible ? status : defaults);
			if (statusVisible) {
				passed &= Check(bounds.Min.y == status.center.y - status.size.y * .5f && bounds.Max.y == 1612,
					"compilation status cannot reset the extended height on later frames");
			} else {
				passed &= Check(bounds.Min.x == saved.Min.x && bounds.Min.y == saved.Min.y &&
									bounds.Max.x == saved.Max.x && bounds.Max.y == saved.Max.y,
					"status dismissal restores the exact saved placement, including manual resize");
			}
			ImGui::Render();
		}
	}
	for (const float invalidCoordinate : { -5000.0f, std::numeric_limits<float>::quiet_NaN() }) {
		menu.resetLayout = true;
		ImGui::NewFrame();
		auto bounds = menu.DrawGeometry({ invalidCoordinate, invalidCoordinate }, { 1200, 600 }, status);
		passed &= Check(bounds.Min.y == status.center.y - status.size.y * .5f && bounds.Max.y == 1612,
			"opening with compilation status keeps the expanded menu inside the HMD canvas");
		ImGui::Render();
		ImGui::NewFrame();
		bounds = menu.DrawGeometry(bounds.GetCenter(), bounds.GetSize(), defaults);
		passed &= Check(std::isfinite(bounds.Min.x) && std::isfinite(bounds.Min.y) &&
							bounds.Min.x >= 8 && bounds.Min.y >= 8 && bounds.Max.x <= 1912 && bounds.Max.y == 1612,
			"status dismissal repairs corrupt saved geometry before extending its height");
		ImGui::Render();
	}
	REL::Module::vr = false;
	ImGui::GetIO().DisplaySize = { 1920, 1080 };
	return passed;
}

bool CheckDockingRecovery()
{
	Menu menu;
	bool passed = true;
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	for (int frame = 0; frame < 4; ++frame) {
		ImGui::NewFrame();
		const auto dockSpace = ImGui::DockSpaceOverViewport();
		if (frame == 0)
			ImGui::SetNextWindowDockID(dockSpace, ImGuiCond_Always);
		menu.resetLayout = frame != 0;
		menu.resetMenuDocking = frame == 3;
		menu.DrawGeometry({ 960, 540 }, { 1900, 1000 }, { { 960, 540 }, { 1800, 900 } }, frame != 0);
		const auto* window = ImGui::FindWindowByName("###CommunityShaders");
		if (frame == 2)
			passed &= Check(window->DockIsActive, "display-size layout reset preserves usable desktop docking");
		if (frame == 3)
			passed &= Check(!window->DockIsActive, "explicit emergency recovery detaches the docked menu");
		ImGui::Render();
	}
	return passed;
}

int main()
{
	bool passed = true;
	ImGui::CreateContext();
	auto& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = { 1920, 1080 };
	ImFontConfig config;
	config.SizePixels = 21;
	io.FontDefault = io.Fonts->AddFontDefault(&config);
	io.Fonts->Build();
	for (const float zoom : { .5f, 1.0f, 1.5f, 2.0f }) {
		ImGui::GetStyle().FontScaleMain = zoom;
		ImGui::NewFrame();
		ImGui::Begin("Typography");
		const float base = ImGui::GetFontSize();
		const float baseSize = ImGui::GetStyle().FontSizeBase;
		{
			SidebarFontScope sidebar;
			passed &= Check(std::abs(ImGui::GetFontSize() - base * sidebarTextScale) < 1.1f, "sidebar applies zoom once");
		}
		passed &= Check(ImGui::GetStyle().FontSizeBase == baseSize && ImGui::GetFontSize() == base, "sidebar restores font state");
		passed &= Check(std::abs(GetHeaderActionIconSize() / base - 2.25f) < .001f, "header icon remains proportional to text");
		ImGui::End();
		ImGui::Render();
	}

	for (const ImVec2 canvas : { ImVec2(640, 480), ImVec2(1920, 1080), ImVec2(1920, 1620), ImVec2(3840, 2160) }) {
		const float zoom = MenuUI::Layout::EffectiveScale(1, 108, canvas);
		const float font = 108 * zoom;
		const ImVec2 available(canvas.x - 16, canvas.y - 16);
		const auto minimum = MenuUI::Layout::MinimumSize(font, available);
		ImVec2 center(-5000, 9000), size(64, 64);
		passed &= Check(MenuUI::Layout::FitWindow(center, size, { 8, 8 }, available,
							{ canvas.x * .5f, canvas.y * .5f }, available, minimum),
			"repair undersized offscreen geometry");
		passed &= Check(size.x >= font * 36 - .01f && size.y >= font * 22 - .01f &&
							center.x - size.x * .5f >= 7.99f && center.y + size.y * .5f <= canvas.y - 7.99f,
			"usable window fits canvas");
		passed &= Check(!MenuUI::Layout::FitWindow(center, size, { 8, 8 }, available,
							center, available, minimum),
			"repaired geometry stays stable");
	}
	passed &= Check(MenuUI::Layout::SanitizeGlobalScale(std::numeric_limits<float>::quiet_NaN()) == 0 &&
						MenuUI::Layout::SanitizeGlobalScale(100) == 1,
		"malformed saved zoom is bounded");
	for (float dpi : { 1.0f, 1.5f, 2.0f }) {
		const float scale = MenuUI::Layout::EffectiveScale(1, 108, io.DisplaySize, dpi);
		passed &= Check(108 * scale * dpi * 22 <= io.DisplaySize.y - 15.99f,
			"zoom cap includes display DPI");
	}

	ImGui::GetStyle().FontScaleMain = 1;
	ImGuiID tableId = 0;
	for (int frame = 0; frame < 4; ++frame) {
		ImGui::NewFrame();
		ImGui::SetNextWindowSize({ 1200, 800 });
		ImGui::Begin("Saved sidebar");
		if (ImGui::BeginTable("Columns", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Navigation", 0, .24f);
			ImGui::TableSetupColumn("Settings", 0, .76f);
			auto* table = ImGui::GetCurrentTable();
			tableId = table->ID;
			if (frame == 0) {
				MenuUI::Layout::ConstrainSidebar(false);
				passed &= Check(std::abs(table->Columns[0].StretchWeight / (table->Columns[0].StretchWeight + table->Columns[1].StretchWeight) - .24f) < .001f,
					"fresh tables retain the default sidebar split");
			}
			table->Columns[0].StretchWeight = .95f;
			table->Columns[1].StretchWeight = .05f;
			MenuUI::Layout::ConstrainSidebar(frame == 3);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted("Navigation");
			ImGui::TableNextColumn();
			passed &= Check(ImGui::GetContentRegionAvail().x >= ImGui::GetFontSize() * 24, "saved sidebar cannot consume settings pane");
			if (frame == 3)
				passed &= Check(std::abs(table->Columns[0].StretchWeight - .24f) < .001f, "reset restores default split");
			ImGui::EndTable();
		}
		ImGui::End();
		ImGui::Render();
	}

	auto* savedTable = ImGui::TableFindByID(tableId);
	savedTable->Columns[0].StretchWeight = .95f;
	savedTable->Columns[1].StretchWeight = .05f;
	ImGui::TableSaveSettings(savedTable);
	const std::string savedIni = ImGui::SaveIniSettingsToMemory();
	ImGui::DestroyContext();
	ImGui::CreateContext();
	auto& restoredIO = ImGui::GetIO();
	restoredIO.IniFilename = nullptr;
	restoredIO.DisplaySize = { 1920, 1080 };
	restoredIO.FontDefault = restoredIO.Fonts->AddFontDefault(&config);
	restoredIO.Fonts->Build();
	ImGui::LoadIniSettingsFromMemory(savedIni.c_str());
	ImGui::NewFrame();
	ImGui::SetNextWindowSize({ 756, 600 });
	ImGui::Begin("Saved sidebar");
	if (ImGui::BeginTable("Columns", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn("Navigation", 0, .24f);
		ImGui::TableSetupColumn("Settings", 0, .76f);
		MenuUI::Layout::ConstrainSidebar(false);
		ImGui::TableNextColumn();
		ImGui::TextUnformatted("Navigation");
		ImGui::TableNextColumn();
		passed &= Check(ImGui::GetContentRegionAvail().x >= ImGui::GetFontSize() * 24,
			"corrupt ini split is repaired on the first frame at minimum window width");
		ImGui::EndTable();
	}
	ImGui::End();
	ImGui::Render();

	passed &= CheckWindowGeometry();
	passed &= CheckEditorLayout();
	passed &= CheckWindowHeight();
	passed &= CheckStatusHeightRestoration();
	passed &= CheckDockingRecovery();

	Menu menu;
	InputCombo::ctrl = InputCombo::shift = true;
	menu.ProcessKeys({ { VK_END, true }, { VK_END, true, true } });
	passed &= Check(menu.requests == 1 && menu.forwardedKeys == 0, "emergency chord fires once while closed");
	InputCombo::ctrl = InputCombo::shift = false;
	menu.ProcessKeys({ { VK_END, false } });
	passed &= Check(!menu.layoutResetKeyHeld && menu.forwardedKeys == 0, "release cannot toggle menu after modifiers release");
	menu.ProcessKeys({ { VK_END, true }, { VK_END, false } });
	passed &= Check(menu.requests == 1 && menu.forwardedKeys == 2, "ordinary menu key is preserved");
	InputCombo::ctrl = InputCombo::shift = true;
	menu.ProcessKeys({ { VK_END, true } });
	menu.OnFocusChanged();
	passed &= Check(menu._comboFiredKeys.contains(VK_END), "focus cleanup preserves recovery key-release ownership");
	InputCombo::ctrl = InputCombo::shift = false;
	menu.ProcessKeys({ { VK_END, false } });
	passed &= Check(menu.forwardedKeys == 2 && menu._comboFiredKeys.empty(),
		"releasing recovery keys after refocus cannot fire the ordinary menu toggle");
	menu.settings.GlobalScale = 1;
	menu.settings.Theme.FontSize = 108;
	menu.settings.Theme.FontRoles[0].SizeScale = 4;
	menu.featureSearch = "hidden";
	menu.settingToggleKey = true;
	ImGui::NewFrame();
	ImGui::Begin("Recovery modal owner");
	ImGui::OpenPopup("Recovery modal");
	if (ImGui::BeginPopupModal("Recovery modal")) {
		ImGui::TextUnformatted("An inaccessible modal must not block recovery");
		ImGui::StartMouseMovingWindow(ImGui::GetCurrentWindow());
		ImGui::EndPopup();
	}
	ImGui::End();
	passed &= Check(!GImGui->OpenPopupStack.empty() && GImGui->MovingWindow && GImGui->ActiveId,
		"recovery fixture has a modal and an active drag");
	menu.ApplyPendingLayoutReset();
	passed &= Check(menu.pendingLayoutReset && !menu.open, "recovery waits until outside an ImGui frame");
	ImGui::Render();
	for (const bool vr : { false, true }) {
		REL::Module::vr = vr;
		menu.pendingLayoutReset = true;
		menu.ApplyPendingLayoutReset();
		passed &= Check(GImGui->OpenPopupStack.empty() && !GImGui->MovingWindow && !GImGui->ActiveId,
			"recovery dismisses blocking modals and cancels active drags");
		passed &= Check(!menu.pendingLayoutReset && menu.open && menu.resetLayout && menu.pendingFontReload &&
							menu.settings.UseResolutionFont && menu.settings.GlobalScale == 0 && menu.settings.Theme.FontSize == 27 &&
							menu.settings.Theme.FontRoles[0].SizeScale == 1 && menu.featureSearch.empty() && !menu.settingToggleKey,
			"recovery restores navigation, typography and geometry");
		passed &= Check(menu.settings.untouchedGraphicsSetting == 42 && menu.baseline && menu.dirty && menu.cancelledMeasurement,
			"recovery preserves graphics settings and tracks unsaved changes");
	}
	passed &= Check(globals::features::vr.resets == 1 && FeatureListRenderer::resets == 2, "headset reset is VR-only");
	ImGui::DestroyContext();
	if (passed)
		std::puts("Menu recovery, sidebar bounds, canvas fitting and single-application zoom passed.");
	return passed ? 0 : 1;
}
