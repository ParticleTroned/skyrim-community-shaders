#include "Menu/SettingsNavigation.h"
#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;
constexpr int ImGuiPopupFlags_AnyPopupId = 1, ImGuiPopupFlags_AnyPopupLevel = 2;
namespace ImGui
{
	bool popupOpen = false;
	void* GetCurrentContext()
	{
		static int context;
		return &context;
	}
	bool IsPopupOpen(void*, int) { return popupOpen; }
}
struct Feature
{
	bool loaded = true;
	static std::vector<Feature*> GetFeatureList();
	virtual ~Feature() = default;
	std::string GetShortName() const { return "VR"; }
	bool IsInMenu() const { return true; }
	bool IsHiddenFromUserView() const { return false; }
};
std::vector<Feature*> Feature::GetFeatureList()
{
	static Feature feature;
	return { &feature };
}

namespace MenuUI
{
	struct ExternalSettingsPage : Feature
	{};
	struct SettingsPage
	{
		static inline bool hostRegistered = false, acceptSelection = true;
		static inline int selects = 0, navigations = 0;
		static inline std::string selectedPage, selectedSection;
		static bool Select(const char* a_page, const char* a_section)
		{
			++selects;
			if (!acceptSelection)
				return false;
			const auto location = SettingsNavigation::Resolve(a_page, a_section);
			selectedPage = location.page;
			selectedSection = location.section;
			return true;
		}
		static bool Navigate(const char* a_page, const char* a_section)
		{
			++navigations;
			return hostRegistered && std::string_view(a_section) != "missing" && Select(a_page, a_section);
		}
		static json Describe() { return json::array(); }
	};
	struct DevBenchViewport
	{
		static inline std::string page, section;
		static inline bool scrollAccepted = true;
		static bool Scroll(const std::string& a_page, const std::string& a_section, double)
		{
			page = a_page;
			section = a_section;
			return scrollAccepted;
		}
		static json Describe(bool) { return json::object(); }
	};
}
struct FeatureListRenderer
{
	static std::vector<Feature*> GetMenuFeatures()
	{
		static Feature vr;
		return { &vr };
	}
	static std::string GetBuiltInPageMenuEntryId(const std::string& a_page)
	{
		return a_page == "Settings" || a_page == "DevTools" || a_page == "Home" || MenuUI::SettingsNavigation::IsPerformancePage(a_page) ? "builtin:" + a_page : "";
	}
	static bool IsSidebarVisible() { return true; }
};
struct PerformanceTuningRenderer
{
	static inline bool busy = false;
	static bool HasActiveMeasurements() { return busy; }
};
namespace globals
{
	namespace game
	{
		bool isVR = false;
	}
	struct Menu
	{
		bool IsEnabled = true;
		int opens = 0;
		std::string selected;
		void SelectFeatureMenu(const std::string& a_page) { selected = a_page; }
		void OpenMenu()
		{
			++opens;
			IsEnabled = true;
		}
		bool HasUnsavedSettings() const { return false; }
	} menuStorage;
	Menu* menu = &menuStorage;
	struct State
	{
		bool IsFeatureDisabled(const std::string&) const { return false; }
	} stateStorage;
	State* state = &stateStorage;
}
json VisualsProfileUIStatus() { return { { "mode", 1 } }; }

namespace MenuUI
{
	enum class GlobalSettingsAction
	{
		Save,
		Load,
		RestoreDefaults
	};
	struct Feedback
	{
		std::string message = "feature action";
	};
	bool featureActionAccepted = true;
	int featureActions = 0;
	GlobalSettingsAction lastFeatureAction = GlobalSettingsAction::Save;
	bool InvokeFeatureSettingsAction(GlobalSettingsAction action, Feature*)
	{
		++featureActions;
		lastFeatureAction = action;
		return featureActionAccepted;
	}
	const Feedback& GetFeatureSettingsFeedback(Feature*)
	{
		static Feedback feedback;
		return feedback;
	}
}
json RunOnMainThread(auto callback) { return callback(); }
json ExecuteFeatureAction(const json& a_args)
{
	const std::string action = "feature_settings";
#include "feature_settings_action_under_test.h"
	throw std::runtime_error("feature action was not handled");
}

json Execute(const std::string& action, const std::string& featureName, const std::string& tab, double ratio = 0)
{
#include "menu_ui_navigation_under_test.h"
}

int main()
try {
	const auto require = [](bool a_value, const char* a_message) {
		if (!a_value)
			throw std::runtime_error(a_message);
	};
	using MenuUI::SettingsPage;
	for (const auto& operation : { "save", "load", "defaults" }) {
		const auto result = ExecuteFeatureAction({ { "feature", "VR" }, { "settingsOperation", operation } });
		require(result.value("ok", false) && result.at("saved") == (std::string_view(operation) == "save"), "feature persistence dispatch retains its explicit scope");
	}
	require(MenuUI::lastFeatureAction == MenuUI::GlobalSettingsAction::RestoreDefaults, "defaults dispatches through the shared feature action");
	require(!ExecuteFeatureAction({ { "feature", "missing" }, { "settingsOperation", "save" } }).value("ok", false), "unknown features cannot trigger global persistence");
	require(!ExecuteFeatureAction({ { "feature", "VR" } }).value("ok", false) && MenuUI::featureActions == 3, "missing operations cannot trigger persistence");
	MenuUI::featureActionAccepted = false;
	require(!ExecuteFeatureAction({ { "feature", "VR" }, { "settingsOperation", "save" } }).value("ok", false), "feature ownership rejection reaches the API caller");

	PerformanceTuningRenderer::busy = ImGui::popupOpen = true;
	require(Execute("ui_status", "VR", "layout").value("ok", false), "status inspects inventory despite hidden VR sections, active measurements and dialogs");
	require(Execute("ui_status", "VR", "layout").at("visualsProfileUI") == VisualsProfileUIStatus(), "status includes the shared Visuals import/export state");
	require(SettingsPage::selects == 0 && globals::menu->opens == 0, "status never queues navigation or opens the menu");
	PerformanceTuningRenderer::busy = ImGui::popupOpen = false;
	require(!Execute("select_tab", "VR", "layout").value("ok", false), "SE/AE rejects VR navigation");
	globals::game::isVR = true;
	require(Execute("select_tab", "VR", "layout").value("ok", false) && globals::menu->selected == "builtin:Settings" &&
				SettingsPage::selectedPage == "Settings" && SettingsPage::selectedSection == "vr_menu",
		"legacy VR menu requests select the cold destination and its host");
	const int opensBefore = globals::menu->opens, selectsBefore = SettingsPage::selects;
	require(!Execute("select_tab", "General", "missing").value("ok", false) && SettingsPage::selects == selectsBefore &&
				globals::menu->opens == opensBefore,
		"unknown legacy tabs cannot bypass destination validation");
	SettingsPage::acceptSelection = false;
	require(!Execute("select_tab", "VR", "controls").value("ok", false) && globals::menu->opens == opensBefore, "rejected destination guards never queue a host change");
	SettingsPage::acceptSelection = true;
	require(Execute("open_feature", "General", "missing").value("ok", false) && SettingsPage::selectedSection == "overview", "ordinary open_feature requests retain overview semantics");
	require(Execute("select_tab", "Advanced", "startup").value("ok", false) && SettingsPage::selectedPage == "Settings" &&
				SettingsPage::selectedSection == "startup",
		"legacy startup requests reach Settings");
	require(Execute("scroll", "VR", "layout", 0.5).value("ok", false) && MenuUI::DevBenchViewport::page == "Settings" &&
				MenuUI::DevBenchViewport::section == "vr_menu",
		"scrolling uses the resolved viewport identity");
	PerformanceTuningRenderer::busy = true;
	require(!Execute("open_feature", "Settings", "overview").value("ok", false), "measurements retain exclusive navigation ownership");
	PerformanceTuningRenderer::busy = false;
	ImGui::popupOpen = true;
	require(!Execute("open_feature", "Settings", "overview").value("ok", false), "dialogs retain navigation ownership");
	ImGui::popupOpen = false;
	globals::menu = nullptr;
	require(!Execute("ui_status", "VR", "layout").value("ok", false), "missing menu services return a bounded failure");
	std::cout << "Menu status, runtime gates, legacy routing and navigation ownership checks passed\n";
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
