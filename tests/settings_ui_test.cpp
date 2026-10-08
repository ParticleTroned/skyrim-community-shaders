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
	static void RenderStatistics() { ++globalDraws; }
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
	static inline int draws = 0;
	static inline int globalDraws = 0;
	static inline std::string feature;
	static inline std::function<void()> inspect;
	static void Render() { ++globalDraws; }
	static void RenderFeatureMeasurement(Feature* selected, bool = false)
	{
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
		const auto profilingCard = Util::controls.at("Choose CPU, GPU or Off to inspect timings.");
		const auto setupCard = Util::controls.at("Appearance");
		require(measurementCard.Min.y == profilingCard.Min.y && measurementCard.Max.x < profilingCard.Min.x, "inspection cards share a bottom row");
		require(measurementCard.Min.x == setupCard.Min.x && measurementCard.GetSize().x == setupCard.GetSize().x && measurementCard.GetSize().y == setupCard.GetSize().y && profilingCard.GetSize().x == setupCard.GetSize().x && profilingCard.GetSize().y == setupCard.GetSize().y, "inspection cards match setup card size and column alignment");
		const int measurementsBefore = PerformanceTuningRenderer::draws;
		click(profilingCard.GetCenter(), drawPerformance);
		for (int i = 0; i < 3; ++i) frame(drawPerformance);
		require(MenuUI::SettingsPage::Selected("FeaturePage") == "profiling" && ProfilingRenderer::feature == "FeaturePage", "profiling card opens the selected feature even before timing data exists");
		require(PerformanceTuningRenderer::draws == measurementsBefore, "profiling never invokes measurement controls");
		int profilingBefore = ProfilingRenderer::draws;
		globals::profiler = nullptr;
		frame(drawPerformance);
		require(ProfilingRenderer::draws == profilingBefore, "unavailable profiler is handled without dereferencing it");
		globals::profiler = &globals::profilerStorage;
		MenuUI::SettingsPage::Select("FeaturePage", "overview");
		for (int i = 0; i < 3; ++i) frame(drawPerformance);
		profilingBefore = ProfilingRenderer::draws;
		click(Util::controls.at("Measures in-game frame times and FPS with the current feature settings.").GetCenter(), drawPerformance);
		for (int i = 0; i < 3; ++i) frame(drawPerformance);
		require(MenuUI::SettingsPage::Selected("FeaturePage") == "performance" && PerformanceTuningRenderer::draws > measurementsBefore, "measurement card opens the existing readiness-aware controls");
		require(ProfilingRenderer::draws == profilingBefore, "measurement does not select a profiling mode");
		feature.supportsMeasurement = true;
		MenuUI::SettingsPage::Select("TestPage", "performance");
		for (int i = 0; i < 3; ++i) frame(drawPage);
		require(PerformanceTuningRenderer::globalDraws > 0, "non-feature pages open the global measurement view");
		MenuUI::SettingsPage::Select("TestPage", "profiling");
		for (int i = 0; i < 3; ++i) frame(drawPage);
		require(ProfilingRenderer::globalDraws > 0, "non-feature pages open the global profiling view");

		feature.supportsMeasurement = false;
		MenuUI::SettingsPage::Select("FeaturePage", "performance");
		Util::controls.clear();
		const int measurementsBeforeRemoval = PerformanceTuningRenderer::draws;
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
		auto drawGlobalPerformance = [&] { MenuUI::SettingsPage page("PerformanceTuning", { { "features", "Features", "Choose features" } }); };
		frame(drawGlobalPerformance);
		require(!MenuUI::SettingsPage::Navigate("PerformanceTuning", "performance"), "global measurement view cannot recursively open itself");

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

		Upscaling::Settings modelSettings;
		auto drawModelResolution = [&] {
			ImGui::SetNextItemWidth(300);
			DrawNeuralModelResolutionSettings(modelSettings);
		};
		globals::features::vr.headset = false;
		frame(drawModelResolution);
		frame(drawModelResolution);
		auto modelBounds = Util::controls.at("Adjust NR Model Resolution. Double-click the value to enter a number.");
		const ImVec2 modelNumber(modelBounds.Min.x + 290, modelBounds.Min.y + ImGui::GetFrameHeight() * .5f);
		io.AddMousePosEvent(modelBounds.Min.x + 70, modelNumber.y);
		frame(drawModelResolution);
		io.AddMouseButtonEvent(0, true);
		frame(drawModelResolution);
		io.AddMousePosEvent(modelBounds.Min.x + 140, modelNumber.y);
		frame(drawModelResolution);
		require(modelSettings.neuralRenderingModelResolutionPercent == 100, "model resolution must not resize while dragging");
		io.AddMouseButtonEvent(0, false);
		frame(drawModelResolution);
		const auto draggedResolution = modelSettings.neuralRenderingModelResolutionPercent;
		require(draggedResolution > 33 && draggedResolution < 100, "model resolution applies after releasing the slider");

		globals::features::vr.headset = true;
		for (int i = 0; i < 25; ++i) frame(drawModelResolution);
		click(modelNumber, drawModelResolution);
		click(modelNumber, drawModelResolution);
		frame(drawModelResolution);
		require(!GImGui->OpenPopupStack.empty(), "model resolution supports the headset keypad");
		click(Util::controls.at("Clear this entry. Your setting is unchanged.").GetCenter(), drawModelResolution);
		auto zeroKey = Util::controls.at("Add this digit.");
		// The keypad's 7 is three rows above and one column left of 0.
		const ImVec2 sevenKey(zeroKey.GetCenter().x - zeroKey.GetWidth() - ImGui::GetStyle().ItemSpacing.x,
			zeroKey.GetCenter().y - 3 * (zeroKey.GetHeight() + ImGui::GetStyle().ItemSpacing.y));
		click(sevenKey, drawModelResolution);
		click(Util::controls.at("Add this digit.").GetCenter(), drawModelResolution);
		require(modelSettings.neuralRenderingModelResolutionPercent == draggedResolution, "model resolution keypad stages a draft without resizing");
		frame(drawModelResolution);
		frame(drawModelResolution);
		click(Util::controls.at("Use this value.").GetCenter(), drawModelResolution);
		require(modelSettings.neuralRenderingModelResolutionPercent == 70 && GImGui->OpenPopupStack.empty(), "model resolution keypad Apply commits the deferred integer setting");
		for (int i = 0; i < 25; ++i) frame(drawModelResolution);
		click(modelNumber, drawModelResolution);
		click(modelNumber, drawModelResolution);
		frame(drawModelResolution);
		click(Util::controls.at("Clear this entry. Your setting is unchanged.").GetCenter(), drawModelResolution);
		frame(drawModelResolution);
		frame(drawModelResolution);
		click(Util::controls.at("Keep the previous value.").GetCenter(), drawModelResolution);
		require(modelSettings.neuralRenderingModelResolutionPercent == 70 && GImGui->OpenPopupStack.empty(), "model resolution keypad Cancel preserves the setting");

		globals::features::vr.headset = false;
		for (int i = 0; i < 25; ++i) frame(drawModelResolution);
		click(modelNumber, drawModelResolution);
		click(modelNumber, drawModelResolution);
		frame(drawModelResolution);
		io.AddKeyEvent(ImGuiMod_Ctrl, true);
		key(ImGuiKey_A, drawModelResolution);
		io.AddKeyEvent(ImGuiMod_Ctrl, false);
		io.AddInputCharactersUTF8("85");
		frame(drawModelResolution);
		require(modelSettings.neuralRenderingModelResolutionPercent == 70, "model resolution desktop entry stages a draft");
		key(ImGuiKey_Enter, drawModelResolution);
		require(modelSettings.neuralRenderingModelResolutionPercent == 85, "model resolution desktop Enter commits the deferred integer setting");

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

		ImGui::DestroyContext();
		std::cout << "Settings navigation and numeric interaction checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
