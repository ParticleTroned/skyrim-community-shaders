#pragma once

#include <imgui.h>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <nlohmann/json_fwd.hpp>
#endif

struct Feature;
namespace Util::Widgets
{
	class ControlLayout;
}

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
		const char* stage = nullptr;
		const char* cardTitle = nullptr;
		const char* guidance = nullptr;
		bool active = true;  // Inactive steps remain navigable for their explanation.
	};

	/** One mutually exclusive option; disabled choices keep their own help. */
	struct Choice
	{
		const char* id;
		const char* title;
		const char* description;
		const char* tooltip;
		bool enabled = true;
	};

	/** Returns the clicked choice index, or -1. Narrow panels stack the cards. */
	int ChoiceCards(const char* a_id, int a_selected, std::span<const Choice> a_choices);
	/** Visible primary choices; changes only the selected index. */
	bool ChoiceSetting(const char* a_label, int* a_selected, const char* const a_items[], int a_count);
	bool ChoiceSetting(const char* a_label, int* a_selected, const char* a_items);
	/** Visible group heading that preserves the page's left alignment. */
	void SectionHeading(const char* a_label);
	/** Body text at the shared detail-control size. */
	void DetailText(const char* a_text);
	/** A wrapped explanation using the shared detail-panel style. */
	bool DetailNote(const char* a_text, const char* a_link = nullptr);

	/** Equal columns for related controls, stacking when the panel is narrow. */
	class DetailGrid
	{
	public:
		explicit DetailGrid(const char* a_id, int a_maxColumns = 2, float a_minColumnWidth = 0);
		~DetailGrid();
		DetailGrid(const DetailGrid&) = delete;
		DetailGrid& operator=(const DetailGrid&) = delete;
		void Next();

	private:
		bool table;
	};

	struct ToggleChoice
	{
		const char* label;
		bool* value;
		const char* tooltip;
	};
	/** Minimum cell width for a complete toggle and its label. */
	float ToggleColumnWidth(std::span<const ToggleChoice> a_choices);
	/** Aligned toggle columns that wrap together in narrow panels. */
	void ToggleGrid(const char* a_id, std::span<const ToggleChoice> a_choices, float a_columnWidth);

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
		SettingsPage(const char* a_id, std::initializer_list<Section> a_sections, const char* a_overviewTitle = "Your setup", const char* a_guidance = "Choose a step, then refine the result.", std::string_view a_summary = {});
		~SettingsPage();
		SettingsPage(const SettingsPage&) = delete;
		SettingsPage& operator=(const SettingsPage&) = delete;
		bool Is(std::string_view a_section) const;
		static void Select(const char* a_page, const char* a_section);
		static std::string Selected(const char* a_page);
		/** Left edge of the last overview column within its owning feature panel. */
		static float OverviewLastColumnInset(const char* a_page, float a_panelWidth);
		/** Align surrounding text with the line through the overview step markers. */
		static float OverviewTextInset(const char* a_page, float a_panelWidth);
#ifdef DEVBENCH_BRIDGE_ENABLED
		/** Navigate only to an available tab of an observed page. */
		static bool Navigate(const char* a_page, const char* a_section);
		static nlohmann::json Describe();
#endif

	private:
		std::unique_ptr<Util::Widgets::ControlLayout> controlLayout;
		std::string selected;
		std::string overviewTitle;
		std::string overviewGuidance;
		bool contentVisible = false;
		float contentLeftPadding = 0;
		std::vector<Section> sections;
		void DrawOverview();
		void DrawDetailHeader(const char* a_page, std::string_view a_summary);
		void DrawCard(const Section& step, ImVec2 minimum, ImVec2 size, int a_ordinal = 0);
	};
}
