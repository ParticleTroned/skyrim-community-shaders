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
struct ImGuiWindow;
namespace Util::Widgets
{
	class ControlLayout;
}

namespace MenuUI
{
	inline constexpr float SettingsSurfaceOpacityScale = .75f;
	inline constexpr float SettingsTileOpacityScale = .75f;
	inline constexpr float SettingsBarVerticalPadding = .5f;
	/** Shared height for navigation tabs and action bars at the current font size. */
	float SettingsBarHeight();
	/** Shared theme colour and opacity for navigation tabs and settings action bars. */
	ImVec4 SettingsBarColor(ImGuiCol source, bool inactive = false);

	/** Distinguish explanations from settings without relying on visible labels. */
	enum class SectionContent
	{
		Settings,
		Explanation,
		Diagnostics
	};
	inline constexpr char NoAdditionalSettingsMessage[] = "No additional settings are available for this feature.";

	/** A navigable settings section. Navigation never applies or resets settings. */
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
		bool showTab = true;                   // Hidden tabs retain navigation through cards or tool icons.
		const char* disabledReason = nullptr;  // Blocks entry and supplies help when unavailable.
		const char* nextSectionId = nullptr;   // Explicit workflow successor; adjacency alone never implies a link.
		SectionContent content = SectionContent::Settings;
	};

	/** A choice card; independent selections can supply their own selected state. */
	struct Choice
	{
		const char* id;
		const char* title;
		const char* description;
		const char* tooltip;
		bool enabled = true;
		bool selected = false;
		ImVec4 groupColor{};
	};

	/** Returns the clicked choice index, or -1. Narrow panels stack the cards. */
	int ChoiceCards(const char* a_id, int a_selected, std::span<const Choice> a_choices);
	/** Visible primary choices; changes only the selected index. */
	bool ChoiceSetting(const char* a_label, int* a_selected, std::span<const Choice> a_choices);
	bool ChoiceSetting(const char* a_label, int* a_selected, const char* const a_items[], int a_count);
	bool ChoiceSetting(const char* a_label, int* a_selected, const char* a_items);
	/** Select a UI-only subpage, allowing search to reveal controls in it. */
	bool NavigationChoice(const char* a_label, int* a_selected, const char* const a_items[], int a_count);
	/** Group heading with a visible divider spanning the available content width. */
	void SectionHeading(const char* a_label, const char* a_help = nullptr);
	/** Visible, accessible reason for unavailable controls, with an optional recovery link. */
	bool AvailabilityNotice(const char* a_text, const char* a_link = nullptr);
	/** Body text at the shared detail-control size. */
	void DetailText(const char* a_text);
	/** A wrapped explanation using the shared detail-panel style. */
	bool DetailNote(const char* a_text, const char* a_link = nullptr);

	enum class NoticeKind
	{
		Information,
		Pending,
		Unavailable,
		Error
	};
	/** Show page feedback in the reserved header, or locally when inside a dialog. */
	void PageNotice(std::string_view a_text, NoticeKind a_kind = NoticeKind::Information, std::string_view a_details = {});

	/** Collect this frame's page notices without changing tab or content positions. */
	class PageNoticeScope
	{
	public:
		PageNoticeScope();
		~PageNoticeScope();
		PageNoticeScope(const PageNoticeScope&) = delete;
		PageNoticeScope& operator=(const PageNoticeScope&) = delete;
		/** Reserve two text rows after the title and before the tabbed body. */
		void Reserve(float a_leftInset = 0);

	private:
		friend void PageNotice(std::string_view, NoticeKind, std::string_view);
		struct Notice
		{
			std::string text;
			NoticeKind kind;
			std::string details;
		};
		bool AcceptsCurrentWindow() const;
		void Draw();
		PageNoticeScope* previous;
		ImGuiWindow* owner;
		ImFont* font = nullptr;
		float fontBaseSize = 0;
		float lineHeight = 0;
		ImVec2 position{}, size{};
		std::vector<Notice> notices;
	};

	/** Persistent action feedback; page messages use the header, dialog errors stay local. */
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

	/** Associates feature tools and shared controls with the content below its tabs. */
	class FeatureScope
	{
	public:
		explicit FeatureScope(Feature* a_feature, std::function<bool()> a_contentControls = {});
		~FeatureScope();
		FeatureScope(const FeatureScope&) = delete;
		FeatureScope& operator=(const FeatureScope&) = delete;
		/** Draw shared controls once; return whether the remaining content is disabled. */
		bool DrawContentControls();

	private:
		Feature* previous;
		FeatureScope* previousScope;
		std::function<bool()> contentControls;
	};

	/** Shared pinned tabs with controls directly in each scrolling section. */
	class SettingsPage
	{
	public:
		/** Empty sections draw one continuous page. A child-page section owns its scrolling and spacing. */
		SettingsPage(const char* a_id, std::initializer_list<Section> a_sections, const char* a_overviewTitle = "", const char* a_guidance = "", std::function<bool(std::string_view)> a_canSelect = {}, std::string_view a_childPageSection = {});
		~SettingsPage();
		SettingsPage(const SettingsPage&) = delete;
		SettingsPage& operator=(const SettingsPage&) = delete;
		bool Is(std::string_view a_section) const;
		static bool Select(const char* a_page, const char* a_section);
		/** Open a sidebar destination, retaining its detail selection when Overview is hidden. */
		static bool Open(const char* a_page);
		static std::string Selected(const char* a_page);
		/** Horizontal insets of the active page content, including the scrollbar. */
		static ImVec2 OverviewCardInsets(const char* a_page, float a_panelWidth);
		/** Left edge of the last overview column within its owning feature panel. */
		static float OverviewLastColumnInset(const char* a_page, float a_panelWidth);
		/** Align surrounding text with the line through the overview step markers. */
		static float OverviewTextInset(const char* a_page, float a_panelWidth);
#ifdef DEVBENCH_BRIDGE_ENABLED
		/** Navigate to an available section, including tools opened from tab-bar icons. */
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
		bool contentDisabled = false;
		float contentLeftPadding = 0;
		std::vector<Section> sections;
		void DrawOverview(const char* a_page);
		void DrawCard(const Section& step, ImVec2 minimum, ImVec2 size, int a_ordinal = 0);
	};
}
