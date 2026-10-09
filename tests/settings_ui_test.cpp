#include "Features/Upscaling/NeuralRendering/ModelResolutionPolicy.h"
#include "Menu/SettingsPage.h"
#include "Utils/FeatureProfiling.h"
#include "Utils/NumericEntry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <functional>
#include <future>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_map>
#define NOMINMAX
#include <windows.h>

// Input bindings are opaque to menu-layout migration; exercise the real settings serializer.
struct InputCombo
{
	int key = 0;
	static InputCombo Keyboard(int code) { return { code }; }
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(InputCombo, key)
#include "menu_settings_under_test.h"

// Only game services are substituted. Layout, input, tabs and popups use real ImGui.
struct Feature
{
	std::string name;
	bool supportsMeasurement = true;
	std::string GetShortName() const { return name; }
	bool SupportsPerformanceCostMeasurement() const { return supportsMeasurement; }
	std::string GetDisplayName() const { return name; }
	static std::vector<Feature*> GetFeatureList() { return {}; }
};
namespace globals
{
	int profilerStorage = 0;
	auto* profiler = &profilerStorage;
	struct FakeMenu
	{
		struct Theme
		{
			struct Palette
			{
				ImVec4 InfoColor{ 1, .7f, .2f, 1 };
				ImVec4 Error{ 1, .1f, .1f, 1 };
				ImVec4 Warning{ 1, .7f, .2f, 1 };
			} StatusPalette;
		} theme;
		struct Settings
		{
			bool SkipConstraintWarning = false;
		} settings;
		Settings& GetSettings() { return settings; }
		const Theme& GetTheme() const { return theme; }
		bool IsSettingsSaveMessageError() const { return false; }
		std::string GetSettingsSaveMessage() const { return {}; }
		bool HasUnsavedSettings() const { return false; }
	} menuStorage;
	auto* menu = &menuStorage;
	struct State
	{
		void Save() {}
		void Load() {}
	} stateStorage;
	auto* state = &stateStorage;
	namespace features
	{
		namespace llf
		{
			struct ParticleLights
			{
				void GetConfigs() {}
			} particleLights;
		}
		struct Pointer
		{
			bool headset = false;
			bool IsMenuPointerInHeadset() const { return headset; }
		} vr;
	}
}
namespace SKSE::stl
{
	template <class F>
	struct scope_exit
	{
		F function;
		bool active = true;
		explicit scope_exit(F f) : function(std::move(f)) {}
		~scope_exit()
		{
			if (active)
				function();
		}
		void release() { active = false; }
	};
}
struct ProfilingRenderer
{
	static inline bool eligible = true;
	static bool CanProfileFeature(std::string_view name) { return eligible && (name == "FeaturePage" || Util::FeatureProfiling::Find(name)); }
	static inline int draws = 0;
	static inline int globalDraws = 0;
	static inline std::string feature;
	static inline std::function<void()> inspect;
	static void RenderStatistics()
	{
		++globalDraws;
		if (inspect)
			inspect();
	}
	static void RenderFeatureTimers(const std::string& prefix)
	{
		if (!globals::profiler)
			throw std::runtime_error("missing profiler");
		feature = prefix;
		++draws;
	}
};
struct PerformanceTuningRenderer
{
	static bool HasActiveMeasurements() { return false; }
	static inline int draws = 0;
	static inline int globalDraws = 0;
	static inline int inactiveNotifications = 0;
	static void NotifyOverviewInactive() { ++inactiveNotifications; }
	static inline std::string feature;
	static inline std::function<void()> inspect;
	static void RenderMeasurementSuite(Feature* selected = nullptr)
	{
		if (!selected) {
			++globalDraws;
			return;
		}
		feature = selected->GetShortName();
		++draws;
		if (inspect)
			inspect();
	}
};
namespace Util
{
	bool HoverTooltipWrapper() { return false; }
	std::map<std::string, ImRect> controls;
	std::map<std::string, ImGuiHoveredFlags> tooltipFlags;
	void AddTooltip(const char* help, ImGuiHoveredFlags flags = 0)
	{
		controls[help] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		tooltipFlags[help] = flags;
	}
	struct DisableGuard
	{
		explicit DisableGuard(bool disabled) { ImGui::BeginDisabled(disabled); }
		~DisableGuard() { ImGui::EndDisabled(); }
	};
	struct Popup
	{
		bool shown;
		explicit operator bool() const { return shown; }
		~Popup()
		{
			if (shown) {
				controls["Popup last control"] = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
				ImGui::EndPopup();
			}
		}
	};
	Popup CenteredPopupModal(const char* name) { return { ImGui::BeginPopupModal(name, nullptr, ImGuiWindowFlags_AlwaysAutoResize) }; }
	enum class ActionGlyph
	{
		SaveSettings,
		LoadSettings,
		RestoreDefaults
	};
	void DrawActionGlyph(ImDrawList*, ActionGlyph, ImVec2, ImVec2, ImU32) {}
	struct StyledButtonWrapper
	{
		StyledButtonWrapper(ImVec4 button, ImVec4 hover, ImVec4 active)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, button);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
		}
		~StyledButtonWrapper() { ImGui::PopStyleColor(3); }
	};
}
#include "settings_page_under_test.h"
#include "settings_widget_under_test.h"
namespace Util
{
#include "settings_uint_checkbox_under_test.h"
}
namespace MenuUI
{
#include "settings_external_actions_under_test.h"
	struct StabilizerPage
	{
		static inline bool dirty = false;
		static StabilizerPage& Get()
		{
			static StabilizerPage page;
			return page;
		}
		bool HasUnsavedChanges() const { return dirty; }
	};
}
#include "settings_footer_under_test.h"
#include "settings_stabilizer_navigation_under_test.h"
struct Upscaling
{
	struct Settings
	{
		uint32_t neuralRenderingModelResolutionPercent = 100;
		uint32_t neuralRenderingCentralAreaPercent = 100, neuralRenderingCentralFeatherPixels = 64;
	};
};
#include "settings_model_resolution_under_test.h"

struct VolumetricLighting : Feature
{
	std::mutex settingsMutex;
	int sanitizations = 0;
	void SanitizeSettings() { ++sanitizations; }
	void DrawSettings();
};
#include "settings_lighting_pages_under_test.h"

using uint = unsigned int;
struct float3
{
	float x, y, z;
};
using float4 = ImVec4;
namespace REL::Module
{
	bool IsVR() { return globals::features::vr.headset; }
}
bool pbrAvailable = true;
bool IsTruePBRActive() { return pbrAvailable; }
void DrawTruePBRDependentTooltip(bool, const char*) {}
#pragma warning(push)
#pragma warning(disable: 4324)
struct FoliageLighting : Feature
{
#include "settings_foliage_fields_under_test.h"
	Settings settings;
	static constexpr float kAmbientAmountMin = 0, kAmbientAmountMax = 1;
	bool enabled = true;
	bool IsEnabled() const { return enabled; }
	void SanitizeSettings(Settings&) {}
	void DrawFoliageScatteringSetting();
	void DrawFoliageAmbientBoostSetting(bool);
	void DrawFoliageAmbientFlipSetting();
	void DrawGrassScatteringSetting();
	void DrawSettings();
};
#pragma warning(pop)
#include "settings_foliage_under_test.h"
constexpr float kHumanSkinControlMin = 0, kHumanSkinControlMax = 2;
#include "settings_skin_controls_under_test.h"
struct SubsurfaceScattering : Feature
{
#include "settings_skin_fields_under_test.h"
	bool updateKernels = true;
	void DrawSettings();
};
#include "Utils/StringUtils.h"
#include "settings_skin_under_test.h"
#include "settings_ui_inventory_under_test.h"
namespace
{
	INT_PTR shellResult = 33;
	int shellCalls = 0;
	std::wstring shellTarget;
	HINSTANCE FakeShellExecuteW(HWND, LPCWSTR, LPCWSTR target, LPCWSTR, LPCWSTR, int)
	{
		++shellCalls;
		shellTarget = target;
		return reinterpret_cast<HINSTANCE>(shellResult);
	}
}
#define ShellExecuteW FakeShellExecuteW
namespace Util
{
#include "settings_shell_under_test.h"
}
#undef ShellExecuteW
#include "settings_capture_feedback_under_test.h"
namespace fmt
{
	using std::format;
}
namespace ThemeManager::Constants
{
	constexpr float POPUP_BUTTON_WIDTH = 100;
}
namespace FeatureConstraints
{
	struct SettingId
	{
		std::string featureShortName, settingPath;
	};
	struct Source
	{
		std::string featureName, featureShortName, reason;
	};
	struct ConstraintResult
	{
		std::vector<Source> sources;
		bool forcedValue;
	};
	std::string FormatConstraintValue(bool value) { return value ? "true" : "false"; }
}
bool g_reactiveWarningShow = false, g_dontShowAgainCheckbox = false;
std::vector<std::pair<FeatureConstraints::SettingId, FeatureConstraints::ConstraintResult>> g_reactiveWarningConstraints;
struct FeatureListRenderer
{
	struct DrawMenuVisitor
	{
		std::string pendingFeatureSelection;
		void RenderReactiveConstraintWarningDialog();
	};
};
bool TrackedSelectable(const char* label)
{
	const bool clicked = ImGui::Selectable(label);
	Util::controls[label] = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
	return clicked;
}
#include "settings_constraint_warning_under_test.h"

void require(bool condition, const char* reason)
{
	if (!condition)
		throw std::runtime_error(reason);
}
void frame(const std::function<void()>& draw)
{
	ImGui::NewFrame();
	ImGui::SetNextWindowPos({ 0, 0 });
	ImGui::SetNextWindowSize({ 900, 700 });
	ImGui::Begin("Test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
	draw();
	ImGui::End();
	ImGui::Render();
}
void click(const ImVec2& point, const std::function<void()>& draw)
{
	auto& io = ImGui::GetIO();
	io.AddMousePosEvent(point.x, point.y);
	frame(draw);
	io.AddMouseButtonEvent(0, true);
	frame(draw);
	io.AddMouseButtonEvent(0, false);
	frame(draw);
}
void key(ImGuiKey keyCode, const std::function<void()>& draw)
{
	auto& io = ImGui::GetIO();
	io.AddKeyEvent(keyCode, true);
	frame(draw);
	io.AddKeyEvent(keyCode, false);
	frame(draw);
}
int main()
{
	try {
		using MenuUI::ParseNumber;
		require(ParseNumber<float>("0.25", 0, 1) == .25f, "fraction parsing");
		require(ParseNumber<int>("-2", -5, 5) == -2, "signed parsing");
		for (const char* invalid : { "", " ", "0.2x", "nan", "inf", "1e99", "1.1", "-0.1" })
			require(!ParseNumber<float>(invalid, 0, 1), "invalid float rejected");
		for (const char* invalid : { "1.0", "2147483648", "1x" })
			require(!ParseNumber<int>(invalid, 0, 100), "invalid integer rejected");
		require(!ParseNumber<unsigned>("-1", 0, 100), "unsigned sign rejected");
		require(!ParseNumber<float>("0", std::numeric_limits<float>::quiet_NaN(), 1), "invalid range rejected");
		require(!ParseNumber<float>("0", 1, 0), "reversed range rejected");

		const nlohmann::json savedMenu = {
			{ "AutoHideFeatureList", true }, { "SelectedThemePreset", "NordicFrost" },
			{ "FirstTimeSetupCompleted", true }, { "RequireShiftToDock", false }
		};
		const auto expectedMenu = nlohmann::json(savedMenu.get<Menu::Settings>());
		for (const auto* key : { "UI Mode", "PerformanceUiMode" }) {
			for (const auto& obsoleteValue : nlohmann::json::array({ 0, 1, -1, "invalid", nullptr })) {
				auto legacyMenu = savedMenu;
				legacyMenu[key] = obsoleteValue;
				const auto loaded = legacyMenu.get<Menu::Settings>();
				require(nlohmann::json(loaded) == expectedMenu, "legacy mode keys must not hide controls or change other menu preferences");
				require(!nlohmann::json(loaded).contains(key), "saving must retire obsolete mode keys");
			}
		}

		{
			std::string error;
			require(!Util::OpenInShell({}, error) && !error.empty() && shellCalls == 0, "empty shell targets fail without launching");
			const std::filesystem::path target(L"capture-\u65e5\u672c");
			for (const INT_PTR failure : { 0, 2, 5, 31, 32 }) {
				shellResult = failure;
				require(!Util::OpenInShell(target, error) && error.find(Util::PathToUtf8(target)) != error.npos, "all shell failure codes remain visible with Unicode paths");
			}
			shellResult = 33;
			require(Util::OpenInShell(target, error) && error.empty() && shellTarget == target.native(), "successful shell launch preserves the target and clears stale errors");
			const auto accepted = CaptureRequestFeedback({ { "ok", true }, { "result", { { "state", "queued" } } } }, "Requested");
			require(!accepted.error && accepted.message == "Requested", "accepted capture actions show success");
			for (const auto& response : nlohmann::json::array({ { { "ok", false }, { "error", { { "message", "Capture is busy" } } } },
					 { { "ok", true }, { "result", { { "state", "failed_partial" }, { "error", "Disk is full" } } } },
					 { { "ok", false }, { "error", { { "code", "transport_error" } } } } })) {
				const auto feedback = CaptureRequestFeedback(response, "Requested");
				require(feedback.error && !feedback.message.empty() && feedback.message != "Requested", "capture rejection, partial failure and transport failures cannot appear as success");
			}
		}

		ImGui::CreateContext();
		auto& io = ImGui::GetIO();
		io.DisplaySize = { 1000, 800 };
		io.DeltaTime = 1.0f / 60;
		io.IniFilename = nullptr;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		unsigned char* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		io.Fonts->SetTexID(ImTextureID(1));

		using Viewport = MenuUI::DevBenchViewport;
		bool allowScroll = true;
		auto drawScrollable = [&](const char* tab = "content") {
			Viewport outer("ScrollTest", "legacy", allowScroll);
			ImGui::BeginChild("ScrollContent", { 0, 250 });
			{
				Viewport inner("ScrollTest", tab, allowScroll);
				ImGui::Dummy({ 1, 2000 });
			}
			ImGui::EndChild();
		};
		auto drawScroll = [&] { drawScrollable(); };
		frame(drawScroll);
		frame(drawScroll);
		auto viewport = Viewport::Describe(true);
		require(viewport["fresh"] && viewport["tab"] == "content" && viewport["scrollMaxY"].get<float>() > 1000, "innermost viewport owns scroll observation");
		require(!Viewport::Describe(false)["fresh"].get<bool>(), "closed menu cannot report a fresh viewport");
		for (double ratio : { -1.0, 1.001, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
			require(!Viewport::Scroll("ScrollTest", "content", ratio), "invalid scroll ratio rejected");
		require(!Viewport::Scroll("Missing", "content", 1) && !Viewport::Scroll("ScrollTest", "legacy", 1), "foreign or outer viewport scroll rejected");
		require(Viewport::Scroll("ScrollTest", "content", 1), "bottom scroll accepted");
		require(!Viewport::Scroll("ScrollTest", "content", 0), "pending request cannot be overwritten");
		frame(drawScroll);
		frame(drawScroll);
		viewport = Viewport::Describe(true);
		require(viewport["appliedGeneration"] == viewport["requestedGeneration"] && !viewport["pending"].get<bool>() &&
					std::abs(viewport["scrollY"].get<float>() - viewport["scrollMaxY"].get<float>()) < 1,
			"accepted scroll reaches the observed bottom");
		const auto applied = viewport["appliedGeneration"];
		require(Viewport::Scroll("ScrollTest", "content", 0), "navigation cancellation fixture accepted");
		frame([&] { drawScrollable("other"); });
		frame(drawScroll);
		require(Viewport::Describe(true)["appliedGeneration"] == applied && !Viewport::Describe(true)["pending"].get<bool>(), "navigation cancels an unapplied scroll");
		require(Viewport::Scroll("ScrollTest", "content", 0), "measurement guard fixture accepted");
		allowScroll = false;
		frame(drawScroll);
		require(Viewport::Describe(true)["appliedGeneration"] == applied, "measurement begun before rendering blocks pending scroll");
		allowScroll = true;
		require(Viewport::Scroll("ScrollTest", "content", 0), "expired frame fixture accepted");
		for (int i = 0; i < 3; ++i)
			frame([] {});
		frame(drawScroll);
		require(Viewport::Describe(true)["appliedGeneration"] == applied, "request cannot survive an absent UI viewport");
		frame([&] { Viewport legacy("LegacyTest", "legacy", true); ImGui::Dummy({ 1, 2000 }); });
		require(Viewport::Describe(true)["page"] == "LegacyTest" && Viewport::Describe(true)["tab"] == "legacy", "untabbed content remains observable");

		bool showExtra = true;
		auto drawPage = [&] {
			MenuUI::SettingsPage page("TestPage", { { "mode", "Mode", "Choose the rendering mode." },
													  { "look", "Look", "Refine appearance." },
													  { "extra", "Extra", "Optional controls.", {}, showExtra } });
			if (page.Is("mode"))
				ImGui::TextUnformatted("Mode controls");
		};
		frame(drawPage);
		frame(drawPage);
		require(MenuUI::SettingsPage::Selected("TestPage") == "overview", "overview is default");
		require(!MenuUI::SettingsPage::Navigate("Missing", "mode"), "unknown page rejected");
		require(!MenuUI::SettingsPage::Navigate("TestPage", "missing"), "unknown tab rejected");
		const auto card = Util::controls.at("Choose the rendering mode.");
		click(card.GetCenter(), drawPage);
		frame(drawPage);
		frame(drawPage);
		require(MenuUI::SettingsPage::Selected("TestPage") == "mode", "card opens matching tab");
		click(Util::controls.at("Return to this feature's setup overview. Your settings are kept.").GetCenter(), drawPage);
		frame(drawPage);
		frame(drawPage);
		require(MenuUI::SettingsPage::Selected("TestPage") == "overview", "detail back button returns to its own overview");

		int selectedChoice = 0;
		bool parentDisabled = false, narrowChoices = false;
		auto drawChoices = [&] {
			ImGui::BeginChild("Choices", { narrowChoices ? 230.0f : 850.0f, 600 });
			{
				Util::DisableGuard disabled(parentDisabled);
				const MenuUI::Choice choices[]{
					{ "first", "Full resolution", "A finished scene", "First choice help" },
					{ "blocked", "Foveated", "A longer subtitle which must wrap inside a narrow card", "Blocked choice help", false },
					{ "last", "Render scale", "A smaller image", "Last choice help" }
				};
				const int result = MenuUI::ChoiceCards("Mode", selectedChoice, choices);
				if (result >= 0)
					selectedChoice = result;
				MenuUI::DetailNote("A wrapped explanation stays within the available panel width, including narrow windows.");
			}
			ImGui::EndChild();
		};
		frame(drawChoices);
		frame(drawChoices);
		const auto firstChoice = Util::controls.at("First choice help");
		const auto blockedChoice = Util::controls.at("Blocked choice help");
		const auto lastChoice = Util::controls.at("Last choice help");
		require(firstChoice.Min.y == lastChoice.Min.y && firstChoice.GetSize().x == blockedChoice.GetSize().x && firstChoice.GetSize().y == blockedChoice.GetSize().y, "choice cards share width and wrap-aware height");
		click(blockedChoice.GetCenter(), drawChoices);
		require(selectedChoice == 0, "disabled choices cannot apply");
		require((Util::tooltipFlags.at("Blocked choice help") & ImGuiHoveredFlags_AllowWhenDisabled) != 0, "disabled choices still explain their requirements");
		click(lastChoice.GetCenter(), drawChoices);
		require(selectedChoice == 2, "choice card applies the corresponding option");
		parentDisabled = true;
		click(firstChoice.GetCenter(), drawChoices);
		require(selectedChoice == 2, "choice cards honor an outer hardware or runtime guard");
		parentDisabled = false;
		narrowChoices = true;
		frame(drawChoices);
		frame(drawChoices);
		const auto narrowFirst = Util::controls.at("First choice help");
		const auto narrowLast = Util::controls.at("Last choice help");
		require(narrowFirst.Min.x == narrowLast.Min.x && narrowFirst.Max.y < narrowLast.Min.y && narrowFirst.GetSize().y == narrowLast.GetSize().y, "narrow choice cards stack without overlapping and keep equal size");
		click(narrowFirst.GetCenter(), drawChoices);
		require(selectedChoice == 0, "stacked cards retain selection behavior");
		narrowChoices = false;
		selectedChoice = 2;
		frame(drawChoices);
		key(ImGuiKey_Tab, drawChoices);
		key(ImGuiKey_Space, drawChoices);
		require(selectedChoice == 0, "Tab enters keyboard navigation and Space selects a card");
		key(ImGuiKey_Tab, drawChoices);
		key(ImGuiKey_Enter, drawChoices);
		require(selectedChoice == 2, "keyboard navigation skips the disabled choice and Enter selects a card");
		frame(drawPage);

		require(MenuUI::SettingsPage::Navigate("TestPage", "extra"), "visible tab accepted");
		frame(drawPage);
		frame(drawPage);
		showExtra = false;
		frame(drawPage);
		frame(drawPage);
		require(MenuUI::SettingsPage::Selected("TestPage") == "overview", "hidden tab falls back");
		require(!MenuUI::SettingsPage::Navigate("TestPage", "extra"), "hidden tab rejected");
		MenuUI::SettingsPage::Select("TestPage", "overview");
		frame(drawPage);
		Feature feature{ "FeaturePage" };
		frame([&] { MenuUI::FeatureScope scope(&feature); MenuUI::SettingsPage page("Ignored", {{"look", "Look", "Appearance"}}); });
		require(MenuUI::SettingsPage::Navigate("FeaturePage", "performance"), "performance tab retained");
		require(MenuUI::SettingsPage::Selected("TestPage") == "overview", "pages keep independent navigation");

		auto drawPerformance = [&] {
			MenuUI::FeatureScope scope(&feature);
			MenuUI::SettingsPage page("Ignored", { { "look", "Look", "Appearance" } });
		};
		globals::profiler = nullptr;
		for (int i = 0; i < 3; ++i) frame(drawPerformance);
		require(PerformanceTuningRenderer::draws > 0 && ProfilingRenderer::draws == 0, "measurement panel does not open profiling");
		require(PerformanceTuningRenderer::feature == "FeaturePage", "measurement receives the selected feature");
		globals::profiler = &globals::profilerStorage;
		MenuUI::SettingsPage::Select("FeaturePage", "overview");
		for (int i = 0; i < 3; ++i) frame(drawPerformance);
		const auto measurementCard = Util::controls.at("Measures in-game frame times and FPS with the current feature settings.");
		const auto setupCard = Util::controls.at("Appearance");
		require(measurementCard.Min.x == setupCard.Min.x && measurementCard.GetSize().x == setupCard.GetSize().x && measurementCard.GetSize().y == setupCard.GetSize().y, "measurement card matches setup card size and column alignment");
		require(!MenuUI::SettingsPage::Navigate("FeaturePage", "profiling"), "measurement suite owns profiling instead of a second tab");
		const int measurementsBefore = PerformanceTuningRenderer::draws;
		int profilingBefore = ProfilingRenderer::draws;
		click(measurementCard.GetCenter(), drawPerformance);
		for (int i = 0; i < 3; ++i) frame(drawPerformance);
		require(MenuUI::SettingsPage::Selected("FeaturePage") == "performance" && PerformanceTuningRenderer::draws > measurementsBefore, "measurement card opens the shared suite");
		require(ProfilingRenderer::draws == profilingBefore, "measurement suite does not invoke a separate profiling mode selector");
		feature.supportsMeasurement = true;
		MenuUI::SettingsPage::Select("TestPage", "performance");
		for (int i = 0; i < 3; ++i) frame(drawPage);
		require(PerformanceTuningRenderer::globalDraws > 0, "non-feature pages open the global measurement view");
		const int inactiveBeforeProfiling = PerformanceTuningRenderer::inactiveNotifications;
		ProfilingRenderer::inspect = [&] {
			require(PerformanceTuningRenderer::inactiveNotifications > inactiveBeforeProfiling, "leaving tuning releases temporary capture before independent profiling controls draw");
		};
		MenuUI::SettingsPage::Select("TestPage", "profiling");
		for (int i = 0; i < 3; ++i) frame(drawPage);
		require(ProfilingRenderer::globalDraws > 0, "non-feature pages open the global profiling view");
		ProfilingRenderer::inspect = {};

		feature.supportsMeasurement = false;
		MenuUI::SettingsPage::Select("FeaturePage", "performance");
		Util::controls.clear();
		const int measurementsBeforeRemoval = PerformanceTuningRenderer::draws;
		const int inactiveBeforeRemoval = PerformanceTuningRenderer::inactiveNotifications;
		frame(drawPerformance);
		require(PerformanceTuningRenderer::inactiveNotifications > inactiveBeforeRemoval, "removing tuning releases temporary capture when selection falls back");
		for (int i = 0; i < 3; ++i) frame(drawPerformance);
		require(MenuUI::SettingsPage::Selected("FeaturePage") == "overview", "removing tuning returns a previously selected measurement tab to overview");
		require(!MenuUI::SettingsPage::Navigate("FeaturePage", "performance") && !Util::controls.contains("Measures in-game frame times and FPS with the current feature settings."), "removed tuning has neither a reachable tab nor an overview card");
		require(Util::controls.contains("Appearance") && Util::controls.contains("Choose CPU, GPU or Off to inspect timings."), "ordinary settings and profiling cards remain without tuning");
		require(MenuUI::SettingsPage::Navigate("FeaturePage", "look"), "ordinary settings remain reachable without tuning");
		frame(drawPerformance);
		profilingBefore = ProfilingRenderer::draws;
		require(MenuUI::SettingsPage::Navigate("FeaturePage", "profiling"), "profiling remains reachable without tuning");
		for (int i = 0; i < 3; ++i) frame(drawPerformance);
		require(ProfilingRenderer::draws > profilingBefore && PerformanceTuningRenderer::draws == measurementsBeforeRemoval, "preserved profiling does not invoke removed tuning");

		feature.supportsMeasurement = false;
		ProfilingRenderer::eligible = false;
		MenuUI::SettingsPage::Select("FeaturePage", "overview");
		Util::controls.clear();
		auto drawNoTuning = [&] { MenuUI::FeatureScope scope(&feature); MenuUI::SettingsPage page("Unused", {}); };
		for (int i = 0; i < 3; ++i) frame(drawNoTuning);
		require(!MenuUI::SettingsPage::Navigate("FeaturePage", "performance") && !MenuUI::SettingsPage::Navigate("FeaturePage", "profiling"), "unsupported tools cannot be opened");
		require(Util::controls.empty(), "a feature without tuning or eligible tools has an empty body");
		feature.supportsMeasurement = true;
		for (int i = 0; i < 3; ++i) frame(drawNoTuning);
		require(MenuUI::SettingsPage::Navigate("FeaturePage", "performance") && !MenuUI::SettingsPage::Navigate("FeaturePage", "profiling"), "measurement eligibility is independent of profiling");
		ProfilingRenderer::eligible = true;
		auto drawGlobalPerformance = [&] { MenuUI::SettingsPage page("PerformanceTuning", { { "compare", "Compare total feature set", "Compare features" } }); };
		frame(drawGlobalPerformance);
		require(!MenuUI::SettingsPage::Navigate("PerformanceTuning", "performance"), "global measurement view cannot recursively open itself");
		require(!MenuUI::SettingsPage::Navigate("PerformanceTuning", "profiling") && MenuUI::SettingsPage::Navigate("PerformanceTuning", "compare"), "global tuning exposes comparison without profiling");

		for (const auto name : { "NeuralRendering", "ImageBasedLighting", "CSUtility", "CloudShadows", "InteriorSun", "Wetterness", "TruePBR", "ExtendedMaterials", "TerrainVariation", "ExtendedTranslucency", "FoliageLighting", "GrassLighting", "HairSpecular", "WaterEffects", "VR", "Screenshot" }) {
			Feature profiledFeature{ name, false };
			auto drawProfiledPage = [&] {
				MenuUI::FeatureScope scope(&profiledFeature);
				MenuUI::SettingsPage page("Unused", { { "settings", "Settings", "Ordinary settings" } });
			};
			MenuUI::SettingsPage::Select(name, "overview");
			Util::controls.clear();
			for (int i = 0; i < 3; ++i) frame(drawProfiledPage);
			const bool screenshot = std::string_view(name) == "Screenshot";
			require(Util::controls.contains("Choose CPU, GPU or Off to inspect timings.") != screenshot, "profiling card must follow feature coverage without waiting for samples");
			require(MenuUI::SettingsPage::Navigate(name, "profiling") != screenshot, "profiling tab eligibility differs from its overview card");
			require(MenuUI::SettingsPage::Navigate(name, "settings"), "profiling changes must preserve ordinary settings");
		}

		bool draft = true;
		auto drawGuarded = [&] {
			MenuUI::SettingsPage page("CompanionDraft", { { "profiles", "Profiles", "Edit profiles" }, { "commands", "Commands", "Edit commands" } }, "Your setup", "Choose an editor", {}, [&](std::string_view target) { return !draft || target != "commands"; });
		};
		frame(drawGuarded);
		require(MenuUI::SettingsPage::Select("CompanionDraft", "profiles"), "draft editor remains reachable");
		frame(drawGuarded);
		frame(drawGuarded);
		require(!MenuUI::SettingsPage::Navigate("CompanionDraft", "commands"), "DevBench cannot bypass unsaved draft protection");
		click(Util::controls.at("Edit commands").GetCenter(), drawGuarded);
		frame(drawGuarded);
		frame(drawGuarded);
		require(MenuUI::SettingsPage::Selected("CompanionDraft") == "profiles", "tab click cannot hide another editor's draft");
		require(MenuUI::SettingsPage::Select("CompanionDraft", "overview"), "overview remains reachable with draft");
		frame(drawGuarded);
		frame(drawGuarded);
		click(Util::controls.at("Edit commands").GetCenter(), drawGuarded);
		frame(drawGuarded);
		require(MenuUI::SettingsPage::Selected("CompanionDraft") == "overview", "card cannot bypass unsaved draft protection");
		draft = false;
		require(MenuUI::SettingsPage::Navigate("CompanionDraft", "commands"), "saving or discarding releases navigation");
		frame(drawGuarded);
		frame(drawGuarded);
		require(MenuUI::SettingsPage::Selected("CompanionDraft") == "commands", "accepted navigation opens the requested editor");

		ImGuiID firstTabId = 0, secondTabId = 0;
		float firstScroll = 0, secondScroll = 0;
		bool scrollFirst = true;
		auto drawIsolation = [&] {
			MenuUI::SettingsPage page("Isolation", { { "first", "First", "First controls" }, { "second", "Second", "Second controls" } });
			if (page.Is("first")) {
				firstTabId = ImGui::GetID("Shared label");
				firstScroll = ImGui::GetScrollY();
				ImGui::Dummy({ 10, 1800 });
				if (scrollFirst)
					ImGui::SetScrollY(350);
			}
			if (page.Is("second")) {
				secondTabId = ImGui::GetID("Shared label");
				secondScroll = ImGui::GetScrollY();
				ImGui::Dummy({ 10, 1800 });
			}
		};
		frame(drawIsolation);
		MenuUI::SettingsPage::Navigate("Isolation", "first");
		for (int i = 0; i < 4; ++i) frame(drawIsolation);
		require(firstScroll == 350, "first tab can scroll");
		scrollFirst = false;
		MenuUI::SettingsPage::Navigate("Isolation", "second");
		for (int i = 0; i < 3; ++i) frame(drawIsolation);
		require(firstTabId != secondTabId && secondScroll == 0, "tabs isolate widget IDs and scroll position");
		MenuUI::SettingsPage::Navigate("Isolation", "first");
		for (int i = 0; i < 3; ++i) frame(drawIsolation);
		require(firstScroll == 350, "returning to tab preserves its own scroll position");

		VolumetricLighting lighting;
		lighting.name = "VolumetricLighting";
		{
			int inspected = 0;
			PerformanceTuningRenderer::inspect = [&] {
				++inspected;
				// A callback may capture settings on another thread; it must not wait for this UI call.
				const bool unlocked = std::async(std::launch::async, [&] {
					if (!lighting.settingsMutex.try_lock())
						return false;
					lighting.settingsMutex.unlock();
					return true;
				}).get();
				require(unlocked, "performance callbacks must run outside the feature settings lock");
			};
			auto drawLighting = [&] {
				MenuUI::FeatureScope scope(&lighting);
				lighting.DrawSettings();
			};
			frame(drawLighting);
			require(MenuUI::SettingsPage::Navigate("VolumetricLighting", "performance"), "lighting performance tab remains reachable");
			for (int i = 0; i < 3; ++i) frame(drawLighting);
			require(inspected > 0 && lighting.sanitizations > 0, "lighting keeps validation and performance controls");
		}
		PerformanceTuningRenderer::inspect = {};

		{
			float first = .5f, second = .75f;
			ImRect firstNumber, secondNumber, headerToggle, detailToggle;
			int commits = 0;
			bool checked = true;
			auto form = [&] {
				Util::Widgets::Checkbox("Enabled", &checked);
				headerToggle = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
				const Util::Widgets::ControlLayout layout;
				Util::Widgets::Checkbox("Detail checkbox", &checked);
				detailToggle = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
				Util::Widgets::SliderFloat("Short", &first, 0, 1, "%.2f");
				commits += ImGui::IsItemDeactivatedAfterEdit() ? 1 : 0;
				firstNumber = Util::controls.at("Double-click to enter a value. In the headset, this opens the number pad.");
				Util::Widgets::SliderFloat("A longer label", &second, 0, 1, "%.2f");
				secondNumber = Util::controls.at("Double-click to enter a value. In the headset, this opens the number pad.");
			};
			globals::features::vr.headset = true;
			frame(form);
			frame(form);
			require(std::abs(headerToggle.GetHeight() - detailToggle.GetHeight()) < 1, "header and detail square toggles have identical sizes");
			require(std::abs(firstNumber.Min.x - secondNumber.Min.x) < 1 && std::abs(firstNumber.Max.x - secondNumber.Max.x) < 1, "detail values align independently of label lengths");
			require(firstNumber.GetHeight() >= ImGui::GetFontSize() * 2.4f && firstNumber.GetWidth() >= ImGui::GetFontSize() * 5, "numeric entry has a large HMD target");
			const ImVec2 paddedCorner{ firstNumber.Min.x + 2, firstNumber.Min.y + 2 };
			click(paddedCorner, form);
			click(paddedCorner, form);
			frame(form);
			require(!GImGui->OpenPopupStack.empty(), "the padded numeric target opens the headset keypad");
			const auto digit = Util::controls.at("Add this digit.");
			const auto apply = Util::controls.at("Use this value.");
			const auto cancel = Util::controls.at("Keep the previous value.");
			require(digit.GetHeight() > firstNumber.GetHeight() && apply.GetHeight() >= firstNumber.GetHeight() && cancel.GetHeight() == apply.GetHeight(), "digits and keypad actions retain large HMD targets");
			click(Util::controls.at("Clear this entry. Your setting is unchanged.").GetCenter(), form);
			click(Util::controls.at("Add this digit.").GetCenter(), form);
			click(Util::controls.at("Use this value.").GetCenter(), form);
			require(first == 0 && second == .75f && commits == 1, "form keypad commits only its setting and publishes release once");
			globals::features::vr.headset = false;
			int preset = 0;
			const char* options[]{ "Natural", "Strong" };
			auto combo = [&] {
				const Util::Widgets::ControlLayout layout;
				Util::Widgets::Combo("Preset", &preset, options, 2);
			};
			frame(combo);
			frame(combo);
			const auto comboBounds = Util::controls.at("Choose Preset.");
			click({ comboBounds.Max.x - 10, comboBounds.GetCenter().y }, combo);
			frame(combo);
			frame(combo);
			const auto option = Util::controls.at("Select Strong for Preset.");
			require(option.GetHeight() >= firstNumber.GetHeight(), "dropdown choices have full-height HMD targets");
			click(option.GetCenter(), combo);
			require(preset == 1, "large dropdown selection changes the original setting");
		}
		{
			std::array<bool, 10> enabled{};
			const std::array<MenuUI::ToggleChoice, 5> first{ { { "Humans", &enabled[0], "a0" }, { "Other humanoids", &enabled[1], "a1" }, { "Creatures", &enabled[2], "a2" }, { "Animals", &enabled[3], "a3" }, { "Other", &enabled[4], "a4" } } };
			const std::array<MenuUI::ToggleChoice, 5> second{ { { "Faces", &enabled[5], "b0" }, { "Skin", &enabled[6], "b1" }, { "Hair", &enabled[7], "b2" }, { "Armour", &enabled[8], "b3" }, { "Weapons", &enabled[9], "b4" } } };
			auto grids = [&] {
				const Util::Widgets::ControlLayout layout;
				MenuUI::ToggleGrid("Types", first, 220);
				MenuUI::ToggleGrid("Materials", second, 220);
			};
			frame(grids);
			frame(grids);
			for (int i = 0; i < 5; ++i)
				require(std::abs(Util::controls.at(std::format("a{}", i)).Min.x - Util::controls.at(std::format("b{}", i)).Min.x) < 1, "checkbox groups share aligned responsive columns");
			require(Util::controls.at("a4").Min.y > Util::controls.at("a0").Min.y, "wide checkbox targets wrap onto another row");
			click(Util::controls.at("b1").GetCenter(), grids);
			require(enabled[6] && std::count(enabled.begin(), enabled.end(), true) == 1, "aligned checkbox grid edits only the clicked setting");
		}

		float value = .5f;
		bool disabled = false;
		ImGuiSliderFlags sliderFlags = 0;
		int completedEdits = 0;
		int sliderClicks = 0;
		auto drawSlider = [&] {
			Util::DisableGuard guard(disabled);
			ImGui::SetNextItemWidth(300);
			Util::Widgets::SliderFloat("Strength", &value, 0, 1, "%.2f", sliderFlags);
			completedEdits += ImGui::IsItemDeactivatedAfterEdit() ? 1 : 0;
			sliderClicks += ImGui::IsItemClicked() ? 1 : 0;
		};
		globals::features::vr.headset = true;
		frame(drawSlider);
		frame(drawSlider);
		auto bounds = Util::controls.at("Adjust Strength. Double-click the value to enter a number.");
		ImVec2 number(bounds.Min.x + 290, bounds.Min.y + ImGui::GetFrameHeight() * .5f);
		click(number, drawSlider);
		click(number, drawSlider);
		frame(drawSlider);
		require(!GImGui->OpenPopupStack.empty(), "headset double-click opens keypad");
		require(value == .5f, "opening keypad preserves value");
		click(Util::controls.at("Clear this entry. Your setting is unchanged.").GetCenter(), drawSlider);
		click(Util::controls.at("Add this digit.").GetCenter(), drawSlider);
		// The last digit help belongs to 0, producing a valid zero.
		require(value == .5f, "draft editing preserves value");
		frame(drawSlider);
		frame(drawSlider);
		click(Util::controls.at("Keep the previous value.").GetCenter(), drawSlider);
		frame(drawSlider);

		require(value == .5f && GImGui->OpenPopupStack.empty(), "cancel discards draft");
		for (int i = 0; i < 25; ++i) frame(drawSlider);
		click(number, drawSlider);
		click(number, drawSlider);
		frame(drawSlider);
		click(Util::controls.at("Clear this entry. Your setting is unchanged.").GetCenter(), drawSlider);
		click(Util::controls.at("Add this digit.").GetCenter(), drawSlider);
		disabled = true;
		frame(drawSlider);
		click(Util::controls.at("Use this value.").GetCenter(), drawSlider);
		require(value == .5f, "disabled control cannot commit an open draft");
		disabled = false;
		frame(drawSlider);
		click(Util::controls.at("Use this value.").GetCenter(), drawSlider);
		require(value == 0 && GImGui->OpenPopupStack.empty(), "apply commits a valid draft once");
		require(completedEdits == 1, "keypad Apply publishes exactly one completed slider edit");
		globals::features::vr.headset = false;
		for (int i = 0; i < 25; ++i) frame(drawSlider);
		click(number, drawSlider);
		click(number, drawSlider);
		frame(drawSlider);
		require(GImGui->OpenPopupStack.empty(), "desktop double-click uses keyboard entry");
		key(ImGuiKey_Escape, drawSlider);
		require(value == 0, "desktop cancel preserves value");
		for (int i = 0; i < 25; ++i) frame(drawSlider);
		click(number, drawSlider);
		click(number, drawSlider);
		frame(drawSlider);
		io.AddKeyEvent(ImGuiMod_Ctrl, true);
		key(ImGuiKey_A, drawSlider);
		io.AddKeyEvent(ImGuiMod_Ctrl, false);
		io.AddInputCharactersUTF8("0.75");
		frame(drawSlider);
		key(ImGuiKey_Enter, drawSlider);
		require(value == .75f, "desktop keyboard entry applies on Enter");
		require(completedEdits == 2, "desktop Enter publishes exactly one completed slider edit");
		ImRect focusButton;
		auto drawTabbedSlider = [&] {
			ImGui::Button("Before slider");
			focusButton = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
			drawSlider();
		};
		frame(drawTabbedSlider);
		frame(drawTabbedSlider);
		click(focusButton.GetCenter(), drawTabbedSlider);
		// The first Tab enables keyboard navigation after mouse interaction.
		key(ImGuiKey_Tab, drawTabbedSlider);
		key(ImGuiKey_Tab, drawTabbedSlider);
		frame(drawTabbedSlider);
		frame(drawTabbedSlider);
		require(GImGui->TempInputId != 0, "Tab retains native slider number editing");
		io.AddInputCharactersUTF8("1.5");
		frame(drawTabbedSlider);
		key(ImGuiKey_Enter, drawTabbedSlider);
		require(value == 1.5f, "native keyboard editing retains caller clamping policy");
		require(completedEdits == 3, "native keyboard editing publishes completion");
		value = .5f;
		frame(drawSlider);
		const ImVec2 track(bounds.Min.x + 60, number.y);
		click(track, drawSlider);
		require(value != .5f && completedEdits == 4, "mouse drag retains native value and completion events");
		value = 0;
		globals::features::vr.headset = true;
		sliderFlags = static_cast<ImGuiSliderFlags>(ImGuiSliderFlags_ReadOnly) | ImGuiSliderFlags_NoInput;
		for (int i = 0; i < 25; ++i) frame(drawSlider);
		click(number, drawSlider);
		click(number, drawSlider);
		frame(drawSlider);
		require(GImGui->OpenPopupStack.empty() && value == 0, "weather-locked numeric controls stay read-only");
		const int previousClicks = sliderClicks;
		click(track, drawSlider);
		require(sliderClicks == previousClicks + 1 && value == 0, "read-only weather controls still report clicks to their editor handler");
		sliderFlags = 0;
		value = std::numeric_limits<float>::quiet_NaN();
		frame(drawSlider);
		require(std::isnan(value), "unavailable settings are not silently overwritten");
		value = .5f;
		frame([&] {
			int choice = 1;
			Util::Widgets::SliderInt("Profile", &choice, 0, 3, "Balanced");
			double logarithmic = 2;
			const double minimum = .01, maximum = 10;
			Util::Widgets::SliderScalar("Log", ImGuiDataType_Double, &logarithmic, &minimum, &maximum, "%.2f", ImGuiSliderFlags_Logarithmic);
		});
		frame([&] { float vector[3]{1,2,3}; Util::Widgets::SliderFloat3("Position", vector, 0, 5); });

		struct NeuralSliderCase
		{
			const char* label;
			uint32_t Upscaling::Settings::* member;
			void (*draw)(Upscaling::Settings&);
			uint32_t minimum, maximum;
		};
		for (const auto& test : {
				 NeuralSliderCase{ "NR Model Resolution", &Upscaling::Settings::neuralRenderingModelResolutionPercent, DrawNeuralModelResolutionSettings, 30, 100 },
				 NeuralSliderCase{ "NR central area", &Upscaling::Settings::neuralRenderingCentralAreaPercent, DrawNeuralCentralAreaSettings, 25, 100 },
				 NeuralSliderCase{ "NR central feather", &Upscaling::Settings::neuralRenderingCentralFeatherPixels, DrawNeuralCentralAreaSettings, 0, 256 } }) {
			Upscaling::Settings neuralSettings;
			const auto initialValue = neuralSettings.*test.member;
			auto drawNeuralSlider = [&] {
				ImGui::PushItemWidth(300);
				const SKSE::stl::scope_exit restoreWidth([] { ImGui::PopItemWidth(); });
				test.draw(neuralSettings);
			};
			globals::features::vr.headset = false;
			frame(drawNeuralSlider);
			frame(drawNeuralSlider);
			auto sliderBounds = Util::controls.at(std::format("Adjust {}. Double-click the value to enter a number.", test.label));
			const ImVec2 sliderNumber(sliderBounds.Min.x + 290, sliderBounds.Min.y + ImGui::GetFrameHeight() * .5f);
			io.AddMousePosEvent(sliderBounds.Min.x + 70, sliderNumber.y);
			frame(drawNeuralSlider);
			io.AddMouseButtonEvent(0, true);
			frame(drawNeuralSlider);
			io.AddMousePosEvent(sliderBounds.Min.x + 140, sliderNumber.y);
			frame(drawNeuralSlider);
			require(neuralSettings.*test.member == initialValue, "NR slider must not resize while dragging");
			io.AddMouseButtonEvent(0, false);
			frame(drawNeuralSlider);
			const auto draggedValue = neuralSettings.*test.member;
			require(draggedValue > test.minimum && draggedValue < test.maximum, "NR slider applies after releasing the slider");

			globals::features::vr.headset = true;
			for (int i = 0; i < 25; ++i) frame(drawNeuralSlider);
			click(sliderNumber, drawNeuralSlider);
			click(sliderNumber, drawNeuralSlider);
			frame(drawNeuralSlider);
			require(!GImGui->OpenPopupStack.empty(), std::format("{} supports the headset keypad", test.label).c_str());
			click(Util::controls.at("Clear this entry. Your setting is unchanged.").GetCenter(), drawNeuralSlider);
			auto zeroKey = Util::controls.at("Add this digit.");
			// The keypad's 7 is three rows above and one column left of 0.
			const ImVec2 sevenKey(zeroKey.GetCenter().x - zeroKey.GetWidth() - ImGui::GetStyle().ItemSpacing.x,
				zeroKey.GetCenter().y - 3 * (zeroKey.GetHeight() + ImGui::GetStyle().ItemSpacing.y));
			click(sevenKey, drawNeuralSlider);
			click(Util::controls.at("Add this digit.").GetCenter(), drawNeuralSlider);
			require(neuralSettings.*test.member == draggedValue, "NR slider keypad stages a draft without resizing");
			frame(drawNeuralSlider);
			frame(drawNeuralSlider);
			click(Util::controls.at("Use this value.").GetCenter(), drawNeuralSlider);
			require(neuralSettings.*test.member == 70 && GImGui->OpenPopupStack.empty(), "NR slider keypad Apply commits the deferred integer setting");
			for (int i = 0; i < 25; ++i) frame(drawNeuralSlider);
			click(sliderNumber, drawNeuralSlider);
			click(sliderNumber, drawNeuralSlider);
			frame(drawNeuralSlider);
			click(Util::controls.at("Clear this entry. Your setting is unchanged.").GetCenter(), drawNeuralSlider);
			frame(drawNeuralSlider);
			frame(drawNeuralSlider);
			click(Util::controls.at("Keep the previous value.").GetCenter(), drawNeuralSlider);
			require(neuralSettings.*test.member == 70 && GImGui->OpenPopupStack.empty(), "NR slider keypad Cancel preserves the setting");

			globals::features::vr.headset = false;
			for (int i = 0; i < 25; ++i) frame(drawNeuralSlider);
			click(sliderNumber, drawNeuralSlider);
			click(sliderNumber, drawNeuralSlider);
			frame(drawNeuralSlider);
			io.AddKeyEvent(ImGuiMod_Ctrl, true);
			key(ImGuiKey_A, drawNeuralSlider);
			io.AddKeyEvent(ImGuiMod_Ctrl, false);
			io.AddInputCharactersUTF8("85");
			frame(drawNeuralSlider);
			require(neuralSettings.*test.member == 70, "NR slider desktop entry stages a draft");
			key(ImGuiKey_Enter, drawNeuralSlider);
			require(neuralSettings.*test.member == 85, "NR slider desktop Enter commits the deferred integer setting");
		}

		globals::features::vr.headset = true;
		value = .5f;
		disabled = false;
		for (int i = 0; i < 25; ++i) frame(drawSlider);
		click(number, drawSlider);
		click(number, drawSlider);
		frame(drawSlider);
		require(!GImGui->OpenPopupStack.empty(), "keypad opens before the setting becomes unavailable");
		disabled = true;
		frame(drawSlider);
		click(Util::controls.at("Keep the previous value.").GetCenter(), drawSlider);
		require(GImGui->OpenPopupStack.empty() && value == .5f, "an unavailable setting must still allow keypad cancellation");
		disabled = false;
		bool checked = true;
		bool checkboxDisabled = false;
		ImVec2 checkboxCentre;
		auto drawCheckbox = [&] {
			const auto colorsBefore = ImGui::GetStyle();
			const auto stackBefore = GImGui->ColorStack.Size;
			ImGui::BeginDisabled(checkboxDisabled);
			Util::Widgets::Checkbox("Enabled", &checked);
			checkboxCentre = ImGui::GetItemRectMin();
			checkboxCentre.x += ImGui::GetFrameHeight() * .5f;
			checkboxCentre.y += ImGui::GetFrameHeight() * .5f;
			ImGui::EndDisabled();
			require(GImGui->ColorStack.Size == stackBefore, "checkbox styling restores its color stack");
			require(std::memcmp(colorsBefore.Colors, ImGui::GetStyle().Colors, sizeof(colorsBefore.Colors)) == 0,
				"checkbox styling must not leak into the next control");
		};
		frame(drawCheckbox);
		click(checkboxCentre, drawCheckbox);
		require(!checked, "filled checkbox can be switched off");
		click(checkboxCentre, drawCheckbox);
		require(checked, "empty checkbox can be switched on");
		checkboxDisabled = true;
		click(checkboxCentre, drawCheckbox);
		require(checked, "disabled checkbox retains its value");

		{
			bool blocked = false;
			int selection = 0;
			auto choices = [&] {
				Util::DisableGuard guard(blocked);
				MenuUI::ChoiceSetting("Quality", &selection, "Performance\0Quality\0");
			};
			frame(choices);
			frame(choices);
			click(Util::controls.at("Quality").GetCenter(), choices);
			require(selection == 1, "visible primary choices retain zero-separated option ordering");
			blocked = true;
			click(Util::controls.at("Performance").GetCenter(), choices);
			require(selection == 1, "primary choice groups respect the feature's disabled state");

			unsigned int lightFlags = 0b1001;
			ImVec2 flagCentre;
			auto drawFlags = [&] {
				const auto itemFlags = GImGui->CurrentItemFlags;
				Util::Widgets::CheckboxFlags("Light flags", &lightFlags, 0b0011);
				flagCentre = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()).GetCenter();
				require(itemFlags == GImGui->CurrentItemFlags, "flag toggles restore mixed-value state");
			};
			frame(drawFlags);
			click(flagCentre, drawFlags);
			require(lightFlags == 0b1011, "mixed flag toggle enables its complete mask and preserves unrelated bits");
			click(flagCentre, drawFlags);
			require(lightFlags == 0b1000, "flag toggle disables only its mask");

			bool openImmediately = false;
			bool comboDisabled = false;
			ImRect comboBounds;
			auto customCombo = [&] {
				const auto styles = GImGui->StyleVarStack.Size;
				const auto colors = GImGui->ColorStack.Size;
				const auto fonts = GImGui->FontStack.Size;
				const auto ids = ImGui::GetCurrentWindow()->IDStack.Size;
				const auto windows = GImGui->CurrentWindowStack.Size;
				{
					const Util::Widgets::ControlLayout layout;
					Util::DisableGuard guard(comboDisabled);
					if (auto combo = Util::Widgets::ComboBox("Custom preset", "Natural", 0, openImmediately)) {
						ImGui::Selectable("Natural");
						ImGui::Selectable("Strong");
					}
					comboBounds = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
				}
				require(styles == GImGui->StyleVarStack.Size && colors == GImGui->ColorStack.Size && fonts == GImGui->FontStack.Size,
					"custom dropdown restores frame, font and colour state whether open or closed");
				require(ids == ImGui::GetCurrentWindow()->IDStack.Size && windows == GImGui->CurrentWindowStack.Size,
					"custom dropdown restores its owner window and ID scope");
			};
			frame(customCombo);
			frame(customCombo);
			click(comboBounds.GetCenter(), customCombo);
			frame(customCombo);
			require(!GImGui->OpenPopupStack.empty(), "custom dropdown opens with a padded hit target");
			key(ImGuiKey_Escape, customCombo);
			require(GImGui->OpenPopupStack.empty(), "custom dropdown closes on Escape");
			openImmediately = true;
			frame(customCombo);
			require(!GImGui->OpenPopupStack.empty(), "editor can open the shared dropdown on entering its mode");
			openImmediately = false;
			key(ImGuiKey_Escape, customCombo);
			comboDisabled = true;
			openImmediately = true;
			frame(customCombo);
			require(GImGui->OpenPopupStack.empty(), "disabled dropdown cannot be opened programmatically");
		}

		{
			std::string first = "First";
			char second[64] = "Second";
			double entryNumber = .25;
			std::string multiline = "Commands";
			char description[64] = "Description";
			float color3[]{ .1f, .2f, .3f }, color4[]{ .1f, .2f, .3f, 1.f };
			bool entriesDisabled = false;
			float panel = 850;
			ImRect firstBounds, secondBounds;
			const auto entries = [&] {
				ImGui::BeginChild("EntryControls", { panel, 600 });
				const auto endChild = SKSE::stl::scope_exit([] { ImGui::EndChild(); });
				const Util::Widgets::ControlLayout layout;
				Util::DisableGuard guard(entriesDisabled);
				const int styles = GImGui->StyleVarStack.Size, fonts = GImGui->FontStack.Size, ids = ImGui::GetCurrentWindow()->IDStack.Size;
				Util::Widgets::InputTextWithHint("Name", "Type here", &first);
				firstBounds = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
				Util::Widgets::InputText("Longer name", second, sizeof(second));
				secondBounds = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
				Util::Widgets::InputDouble("Number", &entryNumber, .1);
				Util::Widgets::ColorEdit3("Tint", color3);
				Util::Widgets::ColorEdit4("Tint with alpha", color4);
				Util::Widgets::InputTextMultiline("Commands", &multiline, { 0, 60 });
				Util::Widgets::InputTextMultiline("Description", description, sizeof(description), { 0, 60 });
				const auto bounds = ImGui::GetCurrentWindow()->InnerRect;
				for (const auto& entry : { firstBounds, secondBounds })
					if (!(entry.Min.x >= bounds.Min.x && entry.Max.x <= bounds.Max.x + .1f && entry.GetHeight() >= ImGui::GetFontSize() * 2))
						throw std::runtime_error(std::format("Entry bounds {}..{} height {} panel {}..{} font {}", entry.Min.x, entry.Max.x, entry.GetHeight(), bounds.Min.x, bounds.Max.x, ImGui::GetFontSize()));
				require(std::abs(firstBounds.Max.x - secondBounds.Max.x) < 1, "entry widths align independently of label length");
				require(styles == GImGui->StyleVarStack.Size && fonts == GImGui->FontStack.Size && ids == ImGui::GetCurrentWindow()->IDStack.Size, "text, numeric and color entries restore every shared style and ID scope");
			};
			for (const float entryWidth : { 850.f, 420.f }) {
				panel = entryWidth;
				frame(entries);
				frame(entries);
			}
			click({ firstBounds.Max.x - 20, firstBounds.GetCenter().y }, entries);
			ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
			key(ImGuiKey_A, entries);
			ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
			ImGui::GetIO().AddInputCharactersUTF8("Edited");
			frame(entries);
			key(ImGuiKey_Enter, entries);
			require(first == "Edited" && std::string(second) == "Second", "text entry changes only its own setting");
			entriesDisabled = true;
			click({ secondBounds.Max.x - 20, secondBounds.GetCenter().y }, entries);
			ImGui::GetIO().AddInputCharactersUTF8("Ignored");
			frame(entries);
			require(std::string(second) == "Second", "disabled text fields cannot change settings");
			entriesDisabled = false;
			globals::features::vr.headset = true;
			frame(entries);
			click(Util::controls.at("Enter this number using the headset number pad.").GetCenter(), entries);
			frame(entries);
			require(!GImGui->OpenPopupStack.empty(), "numeric input fields open the shared HMD keypad");
			click(Util::controls.at("Keep the previous value.").GetCenter(), entries);
			require(entryNumber == .25 && GImGui->OpenPopupStack.empty(), "canceling numeric input preserves its value");
			globals::features::vr.headset = false;
		}

		{
			FeatureListRenderer::DrawMenuVisitor warning;
			const auto drawWarning = [&] {
				const auto windows = GImGui->CurrentWindowStack.Size, tables = GImGui->TablesTempDataStacked;
				warning.RenderReactiveConstraintWarningDialog();
				require(windows == GImGui->CurrentWindowStack.Size && tables == GImGui->TablesTempDataStacked, "warning links restore table and popup state before navigating");
			};
			for (const char* target : { "Impacted", "Source" }) {
				g_reactiveWarningShow = true;
				g_reactiveWarningConstraints = { { { "Impacted", "Setting" }, { { { "Source", "Source", "Dependency" } }, false } } };
				for (int settle = 0; settle < 4; ++settle)
					frame(drawWarning);
				const auto link = Util::controls.at(std::format("{}##{}0", target, std::string_view(target) == "Impacted" ? "imp" : "src"));
				click(link.GetCenter(), drawWarning);
				if (!(warning.pendingFeatureSelection == target && !g_reactiveWarningShow && GImGui->OpenPopupStack.empty()))
					throw std::runtime_error(std::format("Warning target {} selected {} show {} popup {} at {},{}", target, warning.pendingFeatureSelection, g_reactiveWarningShow, GImGui->OpenPopupStack.Size, link.GetCenter().x, link.GetCenter().y));
			}
		}

		// Invalid IDs must not change which file owns the footer or release a draft lock.
		MenuUI::StabilizerPage::dirty = false;
		require(CanSelectEditor("profiles"), "profile editor accepts clean navigation");
		MenuUI::StabilizerPage::dirty = true;
		require(!CanSelectEditor("commands") && editorGroup == "profiles", "profile draft blocks other main-file editors");
		require(CanSelectEditor("overview") && editorGroup == "profiles", "overview retains draft owner");
		require(!CanSelectEditor("missing") && editorGroup == "profiles", "unknown direct selection cannot steal draft owner");
		MenuUI::StabilizerPage::dirty = false;
		require(CanSelectEditor("targets"), "clean main-file editor becomes owner");
		MenuUI::StabilizerPage::dirty = true;
		for (const char* section : { "lod", "levels", "commands", "targets" })
			require(CanSelectEditor(section), "main INI sections share one draft");
		require(!CanSelectEditor("locations") && !CanSelectEditor("profiles"), "main draft cannot switch documents");
		require(!CanSelectEditor("missing") && editorGroup == "targets", "invalid main section is rejected");
		MenuUI::StabilizerPage::dirty = false;
		require(CanSelectEditor("locations"), "clean location editor becomes owner");
		MenuUI::StabilizerPage::dirty = true;
		require(!CanSelectEditor("targets") && CanSelectEditor("locations"), "location draft remains visible");
		MenuUI::StabilizerPage::dirty = false;

		int saves = 0, discards = 0, applies = 0;
		MenuUI::SettingsFooter footer{
			true, false, "Unsaved INI changes", "VRFpsStabilizer.ini",
			{ MenuUI::SettingsAction{ "Save & Apply", "Save INI help", true, [&] { ++saves; }, nullptr, "Save" },
				{ "Discard edits", "Discard INI help", true, [&] { ++discards; }, "Discard this draft?", "Discard" },
				{ "Apply saved INI", "Apply INI help", false, [&] { ++applies; }, nullptr, "Apply" } }
		};
		float footerWidth = 850;
		auto drawFooter = [&] {
			ImGui::BeginChild("Footer", { footerWidth, 500 });
			const auto layout = GetSettingsFooterLayout("External", true, &footer);
			const float height = SettingsFooterHeight("External", true, &footer);
			const float top = ImGui::GetCursorScreenPos().y;
			const auto styles = GImGui->StyleVarStack.Size;
			const auto colors = GImGui->ColorStack.Size;
			const auto fonts = GImGui->FontStack.Size;
			const auto ids = ImGui::GetCurrentWindow()->IDStack.Size;
			DrawSettingsFooter("External", {}, &footer);
			for (const char* help : { "Save INI help", "Discard INI help", "Apply INI help" }) {
				const auto bounds = Util::controls.at(help);
				const auto region = ImGui::GetCurrentWindow()->InnerRect;
				require(bounds.Min.x >= region.Min.x && bounds.Max.x <= region.Max.x + .1f, "footer action stays within its panel");
				require(bounds.Max.y < top + height, "footer reserves every action row");
			}
			if (footerWidth < 250)
				require(layout.actionRows > 1, "small footer wraps actions instead of clipping them");
			require(styles == GImGui->StyleVarStack.Size && colors == GImGui->ColorStack.Size && fonts == GImGui->FontStack.Size && ids == ImGui::GetCurrentWindow()->IDStack.Size,
				"footer restores ImGui styles, fonts and IDs");
			ImGui::EndChild();
		};
		for (float panelWidth : { 850.0f, 450.0f, 180.0f }) {
			footerWidth = panelWidth;
			frame(drawFooter);
			frame(drawFooter);
		}
		footerWidth = 850;
		frame(drawFooter);
		click(Util::controls.at("Save INI help").GetCenter(), drawFooter);
		require(saves == 1, "external save invokes only its own persistence callback");
		click(Util::controls.at("Apply INI help").GetCenter(), drawFooter);
		require(applies == 0, "disabled external action cannot mutate settings");
		click(Util::controls.at("Discard INI help").GetCenter(), drawFooter);
		frame(drawFooter);
		require(discards == 0 && !GImGui->OpenPopupStack.empty(), "destructive draft read requires confirmation");
		footer.actions[1].enabled = false;
		click(Util::controls.at("Popup last control").GetCenter(), drawFooter);
		require(discards == 0 && GImGui->OpenPopupStack.empty(), "pending reload cannot trap a disabled confirmation modal");

		{
			const auto duplicateTitle = [] {
				MenuUI::SettingsPage page("DuplicateTitle", { { "targets", "Performance", "Frame targets" } });
			};
			frame(duplicateTitle);
			frame(duplicateTitle);
			for (const char* id : { "targets", "performance", "targets" }) {
				require(MenuUI::SettingsPage::Navigate("DuplicateTitle", id), "same-title tabs have valid independent destinations");
				frame(duplicateTitle);
				frame(duplicateTitle);
				require(MenuUI::SettingsPage::Selected("DuplicateTitle") == id, "same-title tabs preserve destination identity");
			}
		}

		for (const float font : { 13.0f, 21.0f }) {
			for (const float panel : { 850.0f, 420.0f }) {
				for (const auto& route : uiReviewRoutes) {
					bool conditional = true;
					const auto draw = [&] {
						ImGui::PushFont(ImGui::GetFont(), font);
						ImGui::BeginChild("RegisteredPage", { panel, 600 });
						DrawUiReviewPage(route.page, conditional);
						ImGui::EndChild();
						ImGui::PopFont();
					};
					MenuUI::SettingsPage::Select(route.page, "overview");
					frame(draw);
					frame(draw);
					require(MenuUI::SettingsPage::Navigate(route.page, route.section), "every registered detail route is reachable");
					frame(draw);
					frame(draw);
					if (MenuUI::SettingsPage::Selected(route.page) != route.section)
						throw std::runtime_error(std::format("Route {}/{} selected {} at font {} and width {}", route.page, route.section, MenuUI::SettingsPage::Selected(route.page), font, panel));
					const auto back = Util::controls.at("Return to this feature's setup overview. Your settings are kept.");
					require(back.Min.x >= 0 && back.Max.x <= panel + 10, "back action stays inside wide and narrow panels");
					click(back.GetCenter(), draw);
					frame(draw);
					frame(draw);
					require(MenuUI::SettingsPage::Selected(route.page) == "overview", "every detail back button returns to the same production page");
					if (route.conditional) {
						conditional = false;
						frame(draw);
						require(!MenuUI::SettingsPage::Navigate(route.page, route.section), "unavailable runtime sections cannot be selected");
					}
				}
			}
		}
		std::cout << "Checked " << std::size(uiReviewRoutes) << " production section routes at two widths and two font sizes\n";

		{
			FoliageLighting foliage;
			for (const bool enabled : { false, true }) {
				foliage.enabled = enabled;
				for (const bool pbr : { false, true }) {
					pbrAvailable = pbr;
					for (const char* section : { "overview", "trees", "grass" }) {
						MenuUI::SettingsPage::Select("FoliageLighting", section);
						const auto draw = [&] {
							Util::DisableGuard outerDisabled(true);
							const auto disabledDepth = GImGui->DisabledStackSize;
							const auto styleDepth = GImGui->StyleVarStack.Size;
							foliage.DrawSettings();
							require(GImGui->DisabledStackSize == disabledDepth && GImGui->StyleVarStack.Size == styleDepth,
								"foliage pages must preserve enclosing disabled and style scopes");
						};
						frame(draw);
						frame(draw);
					}
				}
			}
			SubsurfaceScattering skin;
			skin.settings.SSMode = 0;
			MenuUI::SettingsPage::Select("SubsurfaceScattering", "profiles");
			const auto draw = [&] { skin.DrawSettings(); };
			frame(draw);
			frame(draw);
			std::string log;
			frame([&] {
				ImGui::LogToBuffer();
				skin.DrawSettings();
				log = GImGui->LogBuffer.c_str();
				ImGui::LogFinish();
			});
			for (const char* label : { "Strength", "Falloff" }) {
				const auto first = log.find(label);
				require(first != log.npos && log.find(label, first + 1) != log.npos,
					"both skin profiles keep colour controls visible while kernel updates are pending");
			}
			unsigned int toggle = 256;
			const auto drawToggle = [&] { Util::UIntCheckbox("Uint control", toggle); };
			frame(drawToggle);
			frame(drawToggle);
			require(toggle == 1, "integer-backed toggles normalize the full value");
			click(Util::controls.at("Turn Uint control on or off.").GetCenter(), drawToggle);
			require(toggle == 0, "integer-backed toggles can clear values outside the low byte");
		}

		ImGui::DestroyContext();
		std::cout << "Settings navigation and numeric interaction checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
