#include "Features/Upscaling/NeuralRendering/ModelResolutionPolicy.h"
#include "Menu/SettingsPage.h"
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
		bool essentials = false;
		bool IsEssentialsUiMode() const { return essentials; }
		struct Theme
		{
			struct Palette
			{
				ImVec4 InfoColor{ 1, .7f, .2f, 1 };
			} StatusPalette;
		} theme;
		const Theme& GetTheme() const { return theme; }
	} menuStorage;
	auto* menu = &menuStorage;
	namespace features
	{
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
	static inline bool hasTimers = false;
	static inline int draws = 0;
	static bool HasFeatureTimers(const std::string&) { return hasTimers; }
	template <class F>
	static void RenderFeatureTimers(const std::string&, F callback)
	{
		if (!globals::profiler)
			throw std::runtime_error("missing profiler");
		++draws;
		callback();
	}
};
struct PerformanceTuningRenderer
{
	static inline int draws = 0;
	static inline std::function<void()> inspect;
	static void RenderFeatureMeasurement(Feature*, bool = false)
	{
		++draws;
		if (inspect)
			inspect();
	}
};
namespace Util
{
	bool HoverTooltipWrapper() { return false; }
	std::map<std::string, ImRect> controls;
	void AddTooltip(const char* help) { controls[help] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()); }
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
			if (shown)
				ImGui::EndPopup();
		}
	};
	Popup CenteredPopupModal(const char* name) { return { ImGui::BeginPopupModal(name, nullptr, ImGuiWindowFlags_AlwaysAutoResize) }; }
}
#include "settings_page_under_test.h"
#include "settings_widget_declarations.h"
#include "settings_widget_under_test.h"
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
	void DrawEssentialSettings();
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
		require(PerformanceTuningRenderer::draws > 0 && ProfilingRenderer::draws == 0, "measurement works without profiler");
		globals::profiler = &globals::profilerStorage;
		feature.supportsMeasurement = false;
		ProfilingRenderer::hasTimers = true;
		for (int i = 0; i < 3; ++i) frame(drawPerformance);
		require(ProfilingRenderer::draws > 0, "timers remain accessible without cost measurement support");
		globals::menu->essentials = true;
		const int timerDraws = ProfilingRenderer::draws;
		frame(drawPerformance);
		require(ProfilingRenderer::draws == timerDraws && !MenuUI::SettingsPage::Navigate("FeaturePage", "performance"), "Essentials retains previous timer visibility");
		feature.supportsMeasurement = true;
		frame(drawPerformance);
		require(MenuUI::SettingsPage::Navigate("FeaturePage", "performance"), "Essentials retains measurement controls");
		frame(drawPerformance);
		frame(drawPerformance);
		require(ProfilingRenderer::draws == timerDraws, "Essentials measurements do not show profiler timers");
		globals::menu->essentials = false;

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
		for (bool essentials : { false, true }) {
			globals::menu->essentials = essentials;
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
				if (essentials)
					lighting.DrawEssentialSettings();
				else
					lighting.DrawSettings();
			};
			frame(drawLighting);
			require(MenuUI::SettingsPage::Navigate("VolumetricLighting", "performance"), "lighting performance tab remains reachable");
			for (int i = 0; i < 3; ++i) frame(drawLighting);
			require(inspected > 0 && lighting.sanitizations > 0, "lighting keeps validation and performance controls");
		}
		PerformanceTuningRenderer::inspect = {};
		globals::menu->essentials = false;

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
		ImGui::DestroyContext();
		std::cout << "Settings navigation and numeric interaction checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
