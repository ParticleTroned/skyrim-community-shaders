#include <algorithm>
#include <functional>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifndef DEVBENCH_BRIDGE_ENABLED
#	define DEVBENCH_BRIDGE_ENABLED
#endif

struct ProfilingRenderer
{
	static inline const char* disabledReason = nullptr;
	static const char* GetProfilingDisabledReason() { return disabledReason; }
};

namespace MenuUI
{
	struct DevBenchViewport
	{
		static void Invalidate() {}
	};
	struct SettingsPage
	{
		static bool Select(const char*, const char*);
		static bool Navigate(const char*, const char*);
		static nlohmann::json Describe();
	};
}

#include "settings_navigation_under_test.h"

namespace
{
	void Check(bool condition, std::string_view message)
	{
		if (!condition)
			throw std::runtime_error(std::string(message));
	}

	void CheckNavigationOwnership()
	{
		using namespace MenuUI;
		std::string id = "translated-page-section";
		std::string title = "Translated title from the active language";
		std::string reason = "Translated reason from the active language";
		const auto originalTitle = title;
		const auto originalReason = reason;
		std::vector<Section> sections{ { id.c_str(), title.c_str(), "Description", "Summary" } };
		sections[0].disabledReason = reason.c_str();
		Publish("test", sections);
		Check(!SettingsPage::Navigate("test", id.c_str()), "disabled section became navigable");
		Check(SettingsPage::Describe()[0]["tabs"][1]["title"] == originalTitle, "initial title was not published");

		// I18n::SetLocale/Reload invalidates its returned pointers. Mutating the
		// old buffers first makes a borrowed snapshot fail deterministically,
		// even on allocators that do not immediately reuse freed strings.
		std::fill(title.begin(), title.end(), 'x');
		std::fill(reason.begin(), reason.end(), 'y');
		std::fill(id.begin(), id.end(), 'z');
		const auto retained = SettingsPage::Describe()[0]["tabs"][1];
		Check(retained["title"] == originalTitle, "navigation retained borrowed translated text");
		Check(retained["disabledReason"] == originalReason, "navigation retained a borrowed disabled reason");
		Check(retained["id"] == "translated-page-section", "navigation retained a borrowed section ID");
		sections.clear();
		for (auto* text : { &id, &title, &reason }) {
			text->clear();
			text->shrink_to_fit();
		}
		Check(SettingsPage::Describe()[0]["tabs"][1] == retained, "locale teardown invalidated cached metadata");
		Check(!SettingsPage::Navigate("test", "translated-page-section"), "locale teardown lost the section guard");

		sections = { { "enabled", "New language title", "Description", "New summary" },
			{ "hidden", "Hidden", "Description", {}, false } };
		Publish("test", sections);
		Check(SettingsPage::Navigate("test", "enabled"), "refreshed enabled section was blocked");
		Check(!SettingsPage::Navigate("test", "hidden"), "hidden section was selectable");
		Check(!SettingsPage::Navigate("test", "translated-page-section"), "stale section survived republishing");
		auto refreshed = SettingsPage::Describe()[0];
		Check(refreshed["tabs"].size() == 2 && refreshed["tabs"][1]["disabledReason"] == "" &&
				  refreshed["tabs"][1]["title"] == "New language title",
			"republishing retained stale translation or visibility state");
		navigation.at("test").canSelect = [](std::string_view) { return false; };
		Check(!SettingsPage::Navigate("test", "enabled"), "navigation bypassed its transaction guard");
		navigation.at("test").canSelect = {};
		ProfilingRenderer::disabledReason = "Capture active";
		Check(!SettingsPage::Select("test", "profiling"), "navigation bypassed live profiling guard");
		ProfilingRenderer::disabledReason = nullptr;
		sections.clear();
		Publish("test", sections);
		Check(SettingsPage::Describe()[0]["tabs"].size() == 1, "empty publication left old metadata");
	}
}

int main()
{
	try {
		CheckNavigationOwnership();
		std::cout << "Persistent navigation ownership and guard checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
