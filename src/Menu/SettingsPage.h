#pragma once

#include "DevBenchViewport.h"
#include <functional>
#include <imgui.h>
#include <initializer_list>
#include <memory>
#include <optional>
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
	inline constexpr float SettingsSurfaceOpacityScale = .75f;

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
		bool active = true;                    // Inactive appearance alone does not block navigation.
		bool showTab = true;                   // Card-only sections retain navigation and overview cards.
		const char* disabledReason = nullptr;  // Blocks entry and supplies help when unavailable.
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

	/** Persistent feedback for a page action; errors remain beside its controls. */
	struct ActionFeedback
	{
		std::string message;
		bool error = false;
		void Draw() const;
	};

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

	/** Shared pinned tabs with controls directly in each scrolling section. */
	class SettingsPage
	{
	public:
		/** The optional navigation guard is retained between frames and must outlive the page ID. */
		SettingsPage(const char* a_id, std::initializer_list<Section> a_sections, const char* a_overviewTitle = "Your setup", const char* a_guidance = "Choose a step, then refine the result.", std::function<bool(std::string_view)> a_canSelect = {});
		~SettingsPage();
		SettingsPage(const SettingsPage&) = delete;
		SettingsPage& operator=(const SettingsPage&) = delete;
		bool Is(std::string_view a_section) const;
		static bool Select(const char* a_page, const char* a_section);
		static std::string Selected(const char* a_page);
		/** Horizontal insets of the active page content, including the scrollbar. */
		static ImVec2 OverviewCardInsets(const char* a_page, float a_panelWidth);
		/** Left edge of the last overview column within its owning feature panel. */
		static float OverviewLastColumnInset(const char* a_page, float a_panelWidth);
		/** Align surrounding text with the line through the overview step markers. */
		static float OverviewTextInset(const char* a_page, float a_panelWidth);
#ifdef DEVBENCH_BRIDGE_ENABLED
		/** Navigate to an available section, including tools opened from overview cards. */
		static bool Navigate(const char* a_page, const char* a_section);
		static nlohmann::json Describe();
#endif

	private:
#ifdef DEVBENCH_BRIDGE_ENABLED
		std::optional<DevBenchViewport> devBenchViewport;
#endif
		std::unique_ptr<Util::Widgets::ControlLayout> controlLayout;
		std::string selected;
		std::string overviewTitle;
		std::string overviewGuidance;
		bool contentVisible = false;
		float contentLeftPadding = 0;
		std::vector<Section> sections;
		void DrawOverview();
		void DrawCard(const Section& step, ImVec2 minimum, ImVec2 size, int a_ordinal = 0);
	};
}
