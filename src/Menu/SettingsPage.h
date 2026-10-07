#pragma once

#include <imgui.h>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <nlohmann/json_fwd.hpp>
#endif

struct Feature;

namespace MenuUI
{
	/** An ordered setup step. Navigation never applies or resets settings. */
	struct Section
	{
		const char* id;
		const char* title;
		const char* description;
		std::string summary{};
		bool visible = true;
		bool overview = true;
	};

	/** Associates feature performance controls with its settings page. */
	class FeatureScope
	{
	public:
		explicit FeatureScope(Feature* a_feature);
		~FeatureScope();
		FeatureScope(const FeatureScope&) = delete;
		FeatureScope& operator=(const FeatureScope&) = delete;

	private:
		Feature* previous;
	};

	/** Shared overview, tabs and independently scrolling detail area. */
	class SettingsPage
	{
	public:
		SettingsPage(const char* a_id, std::initializer_list<Section> a_sections);
		~SettingsPage();
		SettingsPage(const SettingsPage&) = delete;
		SettingsPage& operator=(const SettingsPage&) = delete;
		bool Is(std::string_view a_section) const;
		static void Select(const char* a_page, const char* a_section);
		static std::string Selected(const char* a_page);
#ifdef DEVBENCH_BRIDGE_ENABLED
		/** Navigate only to an available tab of an observed page. */
		static bool Navigate(const char* a_page, const char* a_section);
		static nlohmann::json Describe();
#endif

	private:
		std::string selected;
		bool contentVisible = false;
		std::vector<Section> sections;
		void DrawOverview();
	};
}
