#include "SettingsPage.h"

#include "Feature.h"
#include "Globals.h"
#include "Menu.h"
#include "Menu/PerformanceTuningRenderer.h"
#include "Menu/ProfilingRenderer.h"
#include "Utils/UI.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <imgui_internal.h>
#include <mutex>
#include <unordered_map>

namespace MenuUI
{
	namespace
	{
		struct Navigation
		{
			std::string selected = "overview";
			std::function<bool(std::string_view)> canSelect;
			bool pending = false;
			std::vector<Section> sections;
		};
		std::unordered_map<std::string, Navigation> navigation;
		Feature* activeFeature = nullptr;
		std::recursive_mutex navigationMutex;

		constexpr float stageGuideInset = 1.0f;
		constexpr float stageDescriptionInset = 2.2f;
		constexpr float stageMarkerRadius = .85f;
		constexpr float stageNumberScale = 1.25f;
		constexpr float stageRailWidth = 8.25f;
		constexpr float overviewTitleScale = 20.0f / 12.0f;
		constexpr float cardTitleScale = 19.0f / 12.0f;
		constexpr float cardSummaryScale = 14.0f / 12.0f;
		constexpr float stageTextScale = 1.25f;
		constexpr float detailTitleScale = 2.0f;

		bool DrawSettingsCard(const Section& step, ImVec2 minimum, ImVec2 size, int ordinal, bool selected);

		struct CardTextLayout
		{
			float inset;
			float titleOffset;
			float titleWidth;
			float summaryY;
			float descriptionY;
			float height;
		};

		CardTextLayout MeasureCardText(const Section& step, float width, int ordinal)
		{
			const float line = ImGui::GetTextLineHeight();
			const float inset = line * .75f;
			const float textWidth = std::max(1.0f, width - inset * 2);
			auto* font = ImGui::GetFont();
			const float titleOffset = ordinal > 0 ? font->CalcTextSizeA(line * stageNumberScale, FLT_MAX, 0, std::format("{:02}", ordinal).c_str()).x + line * .6f : 0;
			const float titleWidth = std::max(1.0f, textWidth - titleOffset);
			const auto* title = step.cardTitle ? step.cardTitle : step.title;
			const float titleHeight = font->CalcTextSizeA(line * cardTitleScale, FLT_MAX, titleWidth, title).y;
			const float summaryY = inset + titleHeight + line * .3f;
			const float summaryHeight = step.summary.empty() ? 0 : font->CalcTextSizeA(line * cardSummaryScale, FLT_MAX, textWidth, step.summary.c_str()).y + line * .3f;
			const float descriptionY = summaryY + summaryHeight;
			const float descriptionHeight = *step.description ? font->CalcTextSizeA(line, FLT_MAX, textWidth, step.description).y : 0;
			return { inset, titleOffset, titleWidth, summaryY, descriptionY, descriptionY + descriptionHeight + inset };
		}

		struct OverviewGrid
		{
			int columns;
			float rail;
			float gap;
			float width;
		};

		OverviewGrid GetOverviewGrid(float available, bool hasCards)
		{
			const float line = ImGui::GetTextLineHeight();
			const int columns = available >= line * 27 ? 2 : 1;
			const float rail = hasCards && available >= line * 36 ? line * stageRailWidth : 0;
			const float gap = columns == 2 ? line * 2 : 0;
			return { columns, rail, gap, (available - rail - gap) / columns };
		}
	}

	FeatureScope::FeatureScope(Feature* a_feature) : previous(activeFeature) { activeFeature = a_feature; }
	FeatureScope::~FeatureScope() { activeFeature = previous; }

	float SettingsPage::OverviewLastColumnInset(const char* a_page, float a_panelWidth)
	{
		std::scoped_lock lock(navigationMutex);
		const auto found = navigation.find(a_page);
		if (found == navigation.end())
			return a_panelWidth;
		const bool hasCards = std::ranges::any_of(found->second.sections, [](const Section& step) { return step.visible && step.overview; });
		const float padding = ImGui::GetStyle().WindowPadding.x;
		const auto grid = GetOverviewGrid(std::max(1.0f, a_panelWidth - padding), hasCards);
		return padding + grid.rail + (grid.columns - 1) * (grid.width + grid.gap);
	}

	float SettingsPage::OverviewTextInset(const char* a_page, float a_panelWidth)
	{
		std::scoped_lock lock(navigationMutex);
		const auto found = navigation.find(a_page);
		// Match the initial header to the setup grid before its first content frame.
		const bool hasCards = found == navigation.end() || found->second.sections.empty() || std::ranges::any_of(found->second.sections, [](const Section& step) { return step.visible && step.overview; });
		const float padding = ImGui::GetStyle().WindowPadding.x;
		const auto grid = GetOverviewGrid(std::max(1.0f, a_panelWidth - padding), hasCards);
		return padding + (grid.rail > 0 ? ImGui::GetTextLineHeight() * stageGuideInset : 0);
	}

	bool SettingsPage::Select(const char* a_page, const char* a_section)
	{
		std::scoped_lock lock(navigationMutex);
		auto& state = navigation[a_page];
		if (state.canSelect && !state.canSelect(a_section))
			return false;
		state.selected = a_section;
		state.pending = true;
		return true;
	}

	std::string SettingsPage::Selected(const char* a_page)
	{
		std::scoped_lock lock(navigationMutex);
		return navigation[a_page].selected;
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	bool SettingsPage::Navigate(const char* a_page, const char* a_section)
	{
		std::scoped_lock lock(navigationMutex);
		const auto page = navigation.find(a_page);
		if (page == navigation.end() || (std::string_view(a_section) != "overview" &&
											std::ranges::none_of(page->second.sections, [&](const Section& step) { return step.visible && std::string_view(a_section) == step.id; })))
			return false;
		return Select(a_page, a_section);
	}

	nlohmann::json SettingsPage::Describe()
	{
		std::scoped_lock lock(navigationMutex);
		auto result = nlohmann::json::array();
		for (const auto& [id, state] : navigation) {
			auto tabs = nlohmann::json::array({ { { "id", "overview" }, { "title", "Overview" } } });
			for (const auto& step : state.sections)
				if (step.visible)
					tabs.push_back({ { "id", step.id }, { "title", step.title }, { "summary", step.summary } });
			result.push_back({ { "page", id }, { "selected", state.selected }, { "tabs", std::move(tabs) } });
		}
		return result;
	}
#endif

	SettingsPage::SettingsPage(const char* a_id, std::initializer_list<Section> a_sections, const char* a_overviewTitle, const char* a_guidance, std::string_view a_summary, std::function<bool(std::string_view)> a_canSelect) :
		overviewTitle(a_overviewTitle), overviewGuidance(a_guidance), sections(a_sections)
	{
		std::scoped_lock lock(navigationMutex);
		const std::string pageId = activeFeature ? activeFeature->GetShortName() : a_id;
		a_id = pageId.c_str();
		const bool measurementAvailable = activeFeature ? activeFeature->SupportsPerformanceCostMeasurement() : pageId != "PerformanceTuning";
		const bool profilingAvailable = globals::profiler && (activeFeature ? ProfilingRenderer::CanProfileFeature(pageId) : pageId != "Profiling");
		sections.push_back({ "performance", "Performance", "Measures in-game frame times and FPS with the current feature settings.", "Measure current settings", measurementAvailable, false, nullptr, "Performance tuning" });
		sections.push_back({ "profiling", "Profiling", "Choose CPU, GPU or Off to inspect timings.", "Live CPU and GPU timings", profilingAvailable, false });
		ImGui::PushID(a_id);
		auto& state = navigation[a_id];
		state.canSelect = std::move(a_canSelect);
		bool rejectedSelection = false;
		state.sections = sections;
		if (state.selected != "overview" && std::ranges::none_of(sections, [&](const Section& step) {
				return step.visible && state.selected == step.id;
			})) {
			state.selected = "overview";
			state.pending = true;
		}
		if (std::ranges::any_of(sections, [](const Section& step) { return step.visible; })) {
			const float font = ImGui::GetFontSize();
			const auto accent = globals::menu->GetTheme().StatusPalette.InfoColor;
			const float tabCount = 1.0f + static_cast<float>(std::ranges::count_if(sections, [](const Section& step) { return step.visible; }));
			const float tabWidth = ImGui::GetContentRegionAvail().x / tabCount;
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { font * .75f, font * .7f });
			ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 0);
			ImGui::PushStyleVar(ImGuiStyleVar_TabBarBorderSize, 0);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, { 1, ImGui::GetStyle().ItemInnerSpacing.y });
			ImGui::PushStyleColor(ImGuiCol_Tab, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
			ImGui::PushStyleColor(ImGuiCol_TabSelected, ImGui::GetStyleColorVec4(ImGuiCol_Header));
			ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, { 0, 0, 0, 0 });
			const SKSE::stl::scope_exit restoreStyle([] { ImGui::PopStyleColor(3); ImGui::PopStyleVar(4); });
			if (ImGui::BeginTabBar("##SetupTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
				const SKSE::stl::scope_exit endTabs([] { ImGui::EndTabBar(); });
				const auto requested = state.selected;
				const bool restoreSelection = state.pending || ImGui::IsWindowAppearing();
				auto tab = [&](const char* id, const char* title, const char* help) {
					const auto flags = restoreSelection && requested == id ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
					const float titleWidth = ImGui::CalcTextSize(title).x;
					const float width = std::max(tabWidth - 1, titleWidth + font * .7f);
					ImGui::SetNextItemWidth(width);
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { (width - titleWidth) * .5f, font * .7f });
					ImGui::PushStyleColor(ImGuiCol_Text, { 0, 0, 0, 0 });
					const bool active = ImGui::BeginTabItem(title, nullptr, flags);
					ImGui::PopStyleColor();
					ImGui::PopStyleVar();
					if (active) {
						if (state.selected != id && state.canSelect && !state.canSelect(id))
							rejectedSelection = true;
						else
							state.selected = id;
						ImGui::EndTabItem();
						const auto minimum = ImGui::GetItemRectMin();
						const auto maximum = ImGui::GetItemRectMax();
						ImGui::GetWindowDrawList()->AddLine({ minimum.x, maximum.y - 1 }, { maximum.x, maximum.y - 1 }, ImGui::GetColorU32(accent), 2);
					}
					const auto minimum = ImGui::GetItemRectMin();
					const auto maximum = ImGui::GetItemRectMax();
					const auto* bar = ImGui::GetCurrentTabBar();
					const ImVec2 clipMin{ std::max(minimum.x, bar->ScrollingRectMinX), minimum.y };
					const ImVec2 clipMax{ std::min(maximum.x, bar->ScrollingRectMaxX), maximum.y };
					if (clipMin.x < clipMax.x) {
						auto* draw = ImGui::GetWindowDrawList();
						draw->PushClipRect(clipMin, clipMax, true);
						const SKSE::stl::scope_exit unclip([draw] { draw->PopClipRect(); });
						draw->AddText({ minimum.x + (maximum.x - minimum.x - titleWidth) * .5f, minimum.y + font * .7f },
							ImGui::GetColorU32(active ? accent : ImGui::GetStyleColorVec4(ImGuiCol_Text)), title);
					}
					Util::AddTooltip(help);
				};
				tab("overview", "Overview", "Start here. Choose a card to open its settings.");
				for (const auto& step : sections)
					if (step.visible)
						tab(step.id, step.title, step.description);
			}
		}
		state.pending = rejectedSelection;
		selected = state.selected;
		contentLeftPadding = ImGui::GetStyle().WindowPadding.x;
		{
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, ImGui::GetStyle().WindowPadding.y });
			const SKSE::stl::scope_exit restorePadding([] { ImGui::PopStyleVar(); });
			contentVisible = ImGui::BeginChild(std::format("##SettingsContent/{}", selected).c_str(), { 0, 0 }, ImGuiChildFlags_AlwaysUseWindowPadding);
		}
		if (contentLeftPadding > 0)
			ImGui::Indent(contentLeftPadding);
		if (contentVisible && selected != "overview") {
			DrawDetailHeader(a_id, a_summary);
			controlLayout = std::make_unique<Util::Widgets::ControlLayout>();
		}
		if (contentVisible && selected == "performance") {
			if (activeFeature) {
				ImGui::TextWrapped("Measure this feature with your current settings, then compare it with the feature turned off.");
				PerformanceTuningRenderer::RenderFeatureMeasurement(activeFeature);
			} else {
				PerformanceTuningRenderer::Render();
			}
		}
		if (contentVisible && selected == "profiling") {
			if (!globals::profiler)
				ImGui::TextColored(Util::Color::SecondaryText(), "Profiling is not available yet.");
			else if (activeFeature)
				ProfilingRenderer::RenderFeatureTimers(activeFeature->GetShortName());
			else
				ProfilingRenderer::RenderStatistics();
		}
		if (contentVisible && selected == "overview") {
			DrawOverview();
			// A clicked card changes the next frame, keeping this frame's layout intact.
			for (const auto& step : sections)
				if (step.visible && ImGui::GetStateStorage()->GetBool(ImGui::GetID(step.id))) {
					ImGui::GetStateStorage()->SetBool(ImGui::GetID(step.id), false);
					Select(a_id, step.id);
				}
		}
	}

	SettingsPage::~SettingsPage()
	{
		controlLayout.reset();
		if (contentLeftPadding > 0)
			ImGui::Unindent(contentLeftPadding);
		ImGui::EndChild();
		ImGui::PopID();
	}

	bool SettingsPage::Is(std::string_view a_section) const
	{
		return contentVisible && selected == a_section;
	}

	void SettingsPage::DrawDetailHeader(const char* a_page, std::string_view a_summary)
	{
		const auto step = std::ranges::find_if(sections, [&](const Section& item) { return selected == item.id; });
		if (step == sections.end())
			return;
		const float line = ImGui::GetTextLineHeight();
		const bool hasCards = std::ranges::any_of(sections, [](const Section& item) { return item.visible && item.overview; });
		if (GetOverviewGrid(ImGui::GetContentRegionAvail().x, hasCards).rail > 0) {
			const float inset = line * stageGuideInset;
			ImGui::Indent(inset);
			contentLeftPadding += inset;
		}
		std::string summary(a_summary);
		if (summary.empty()) {
			for (const auto& item : sections) {
				if (!item.visible || !item.overview)
					continue;
				if (!summary.empty())
					summary += " / ";
				summary += item.title;
			}
		}
		if (!summary.empty()) {
			ImGui::Dummy({ 0, line * .35f });
			ImGui::PushTextWrapPos(0);
			ImGui::TextColored(Util::Color::SecondaryText(), "%s", summary.c_str());
			ImGui::PopTextWrapPos();
			ImGui::Dummy({ 0, line * .35f });
			ImGui::Separator();
		}
		ImGui::Dummy({ 0, line * .5f });
		const auto start = ImGui::GetCursorScreenPos();
		const float available = ImGui::GetContentRegionAvail().x;
		const int ordinal = step->overview ? 1 + static_cast<int>(std::count_if(sections.begin(), step, [](const Section& item) { return item.visible && item.overview; })) : 0;
		const char* label = step->cardTitle ? step->cardTitle : step->title;
		const auto title = ordinal > 0 ? std::format("{:02} · {}", ordinal, label) : std::string(label);
		const char* back = "Overview";
		const auto& style = ImGui::GetStyle();
		constexpr float backScale = 1.25f;
		const float buttonWidth = (ImGui::CalcTextSize(back).x + line * 1.15f + style.FramePadding.x * 2) * backScale;
		const float buttonHeight = ImGui::GetFrameHeight() * backScale;
		const float rightGap = line * .75f;
		const float titleWidth = ImGui::GetFont()->CalcTextSizeA(line * detailTitleScale, FLT_MAX, 0, title.c_str()).x;
		const bool sameLine = titleWidth + buttonWidth + rightGap + line < available;
		{
			ImGui::PushFont(ImGui::GetFont(), line * detailTitleScale);
			ImGui::PushTextWrapPos(0);
			const SKSE::stl::scope_exit restore([] { ImGui::PopTextWrapPos(); ImGui::PopFont(); });
			ImGui::TextUnformatted(title.c_str());
		}
		if (sameLine) {
			ImGui::SameLine();
			ImGui::SetCursorScreenPos({ start.x + available - buttonWidth - rightGap, start.y + (line * detailTitleScale - buttonHeight) * .5f });
		}
		if (ImGui::Button("##BackToOverview", { buttonWidth, buttonHeight }))
			Select(a_page, "overview");
		const auto buttonStart = ImGui::GetItemRectMin();
		auto* draw = ImGui::GetWindowDrawList();
		const auto colour = ImGui::GetColorU32(ImGuiCol_Text);
		const float arrowX = buttonStart.x + style.FramePadding.x * backScale;
		const float arrowY = (buttonStart.y + ImGui::GetItemRectMax().y) * .5f;
		draw->AddLine({ arrowX, arrowY }, { arrowX + line * .65f * backScale, arrowY }, colour, backScale);
		draw->AddLine({ arrowX, arrowY }, { arrowX + line * .25f * backScale, arrowY - line * .25f * backScale }, colour, backScale);
		draw->AddLine({ arrowX, arrowY }, { arrowX + line * .25f * backScale, arrowY + line * .25f * backScale }, colour, backScale);
		draw->AddText(nullptr, line * backScale, { arrowX + line * 1.15f * backScale, buttonStart.y + style.FramePadding.y * backScale }, colour, back);
		Util::AddTooltip("Return to this feature's setup overview. Your settings are kept.");
		{
			ImGui::PushFont(ImGui::GetFont(), line * cardSummaryScale);
			ImGui::PushStyleColor(ImGuiCol_Text, Util::Color::SecondaryText());
			const SKSE::stl::scope_exit restore([] { ImGui::PopStyleColor(); ImGui::PopFont(); });
			ImGui::TextWrapped("%s", step->guidance ? step->guidance : step->description);
		}
		ImGui::Dummy({ 0, line * .65f });
	}

	int ChoiceCards(const char* a_id, int a_selected, std::span<const Choice> a_choices)
	{
		if (a_choices.empty())
			return -1;
		ImGui::PushID(a_id);
		const SKSE::stl::scope_exit restore([] { ImGui::PopID(); });
		const float line = ImGui::GetTextLineHeight();
		const float available = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		const int count = static_cast<int>(a_choices.size());
		float minimumWidth = line * 8;
		for (const auto& choice : a_choices)
			minimumWidth = std::max(minimumWidth, ImGui::GetFont()->CalcTextSizeA(line * cardTitleScale, FLT_MAX, 0, choice.title).x + line * 1.5f);
		const int columns = std::clamp(static_cast<int>((available + line * .65f) / (minimumWidth + line * .65f)), 1, count);
		const float gap = line * .65f;
		const float width = (available - (columns - 1) * gap) / columns;
		float height = line * 3.2f;
		for (const auto& choice : a_choices)
			height = std::max(height, MeasureCardText({ choice.id, choice.title, choice.description }, width, 0).height);
		const auto origin = ImGui::GetCursorScreenPos();
		int clicked = -1;
		for (int index = 0; index < count; ++index) {
			const auto& choice = a_choices[index];
			const ImVec2 minimum{ origin.x + (index % columns) * (width + gap), origin.y + (index / columns) * (height + gap) };
			auto guard = Util::DisableGuard(!choice.enabled);
			if (DrawSettingsCard({ choice.id, choice.title, choice.description }, minimum, { width, height }, 0, index == a_selected))
				clicked = index;
			Util::AddTooltip(choice.tooltip, ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled);
		}
		const int rows = (count + columns - 1) / columns;
		ImGui::SetCursorScreenPos({ origin.x, origin.y + rows * height + (rows - 1) * gap });
		ImGui::Dummy({ available, line * .5f });
		return clicked;
	}

	bool ChoiceSetting(const char* a_label, int* a_selected, const char* const a_items[], int a_count)
	{
		if (a_count <= 0)
			return false;
		std::vector<Choice> choices;
		choices.reserve(a_count);
		for (int index = 0; index < a_count; ++index)
			choices.push_back({ a_items[index], a_items[index], "", a_items[index] });
		SectionHeading(a_label);
		const int choice = ChoiceCards(a_label, *a_selected, choices);
		if (choice < 0 || choice == *a_selected)
			return false;
		*a_selected = choice;
		return true;
	}

	bool ChoiceSetting(const char* a_label, int* a_selected, const char* a_items)
	{
		std::vector<const char*> choices;
		for (auto* item = a_items; item && *item; item += std::strlen(item) + 1)
			choices.push_back(item);
		return ChoiceSetting(a_label, a_selected, choices.data(), static_cast<int>(choices.size()));
	}

	void SectionHeading(const char* a_label)
	{
		ImGui::Spacing();
		ImGui::PushFont(ImGui::GetFont(), ImGui::GetFontSize() * cardSummaryScale);
		const SKSE::stl::scope_exit restore([] { ImGui::PopFont(); });
		ImGui::TextUnformatted(a_label, ImGui::FindRenderedTextEnd(a_label));
		ImGui::Separator();
		ImGui::Spacing();
	}

	void DetailText(const char* a_text)
	{
		ImGui::PushFont(ImGui::GetFont(), ImGui::GetFontSize() * cardSummaryScale);
		const SKSE::stl::scope_exit restore([] { ImGui::PopFont(); });
		ImGui::TextWrapped("%s", a_text);
	}

	bool DetailNote(const char* a_text, const char* a_link)
	{
		const float line = ImGui::GetTextLineHeight();
		const float inset = line;
		const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		const float textWidth = std::max(1.0f, width - inset * 2);
		const float textHeight = ImGui::GetFont()->CalcTextSizeA(line * cardSummaryScale, FLT_MAX, textWidth, a_text).y;
		const float height = textHeight + inset * 2 + (a_link ? line * 3 : 0);
		const auto start = ImGui::GetCursorScreenPos();
		const ImVec2 end{ start.x + width, start.y + height };
		auto* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(start, end, ImGui::GetColorU32(ImGuiCol_FrameBg), line * .2f);
		draw->AddLine(start, { start.x, end.y }, ImGui::GetColorU32(Util::Color::SecondaryText()), 2);
		draw->AddText(nullptr, line * cardSummaryScale, { start.x + inset, start.y + inset }, ImGui::GetColorU32(ImGuiCol_Text), a_text, nullptr, textWidth);
		bool clicked = false;
		if (a_link) {
			ImGui::SetCursorScreenPos({ start.x + inset, start.y + inset + textHeight + line * .3f });
			clicked = ImGui::Button(a_link, { std::min(textWidth, ImGui::CalcTextSize(a_link).x + line * 2), line * 2.2f });
			Util::AddTooltip("Open the selection controls. Your current settings are kept.");
		}
		ImGui::SetCursorScreenPos(start);
		ImGui::Dummy({ width, height });
		return clicked;
	}

	DetailGrid::DetailGrid(const char* a_id, int a_maxColumns, float a_minColumnWidth)
	{
		const float font = ImGui::GetFontSize();
		const float minimumWidth = a_minColumnWidth > 0 ? a_minColumnWidth : font * 24;
		const int columns = std::clamp(static_cast<int>(ImGui::GetContentRegionAvail().x / minimumWidth), 1, std::max(1, a_maxColumns));
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, { font * .6f, font * .4f });
		table = ImGui::BeginTable(a_id, columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX);
	}
	DetailGrid::~DetailGrid()
	{
		if (table)
			ImGui::EndTable();
		ImGui::PopStyleVar();
	}
	void DetailGrid::Next()
	{
		if (table)
			ImGui::TableNextColumn();
	}

	float ToggleColumnWidth(std::span<const ToggleChoice> a_choices)
	{
		const float font = ImGui::GetFontSize();
		float width = 0;
		for (const auto& choice : a_choices)
			width = std::max(width, ImGui::GetFont()->CalcTextSizeA(font * cardSummaryScale, FLT_MAX, 0, choice.label).x);
		return width + Util::Widgets::CheckboxSize() + ImGui::GetStyle().ItemInnerSpacing.x + font * 1.5f;
	}

	void ToggleGrid(const char* a_id, std::span<const ToggleChoice> a_choices, float a_columnWidth)
	{
		DetailGrid grid(a_id, static_cast<int>(a_choices.size()), a_columnWidth);
		for (const auto& choice : a_choices) {
			grid.Next();
			Util::Widgets::Checkbox(choice.label, choice.value);
			Util::AddTooltip(choice.tooltip, ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled);
		}
	}

	void SettingsPage::DrawOverview()
	{
		std::vector<const Section*> cards, tools;
		for (const auto& step : sections) {
			if (!step.visible)
				continue;
			if (step.overview)
				cards.push_back(&step);
			else if (std::string_view(step.id) == "performance" || std::string_view(step.id) == "profiling")
				tools.push_back(&step);
		}
		if (cards.empty() && tools.empty())
			return;
		const float line = ImGui::GetTextLineHeight();
		const float available = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		const auto grid = GetOverviewGrid(available, !cards.empty());
		const float textInset = grid.rail > 0 ? line * stageGuideInset : 0;
		if (textInset > 0)
			ImGui::Indent(textInset);
		const auto headingStart = ImGui::GetCursorScreenPos();
		const float headingWidth = ImGui::GetContentRegionAvail().x;
		const char* hint = "Click a box to open its settings.";
		const char* guidance = !overviewGuidance.empty() ? overviewGuidance.c_str() : !cards.empty() ? cards.front()->description :
		                                                                                               "";
		const float hintWidth = ImGui::CalcTextSize(hint).x;
		const float guidanceWidth = ImGui::CalcTextSize(guidance).x;
		ImGui::PushFont(ImGui::GetFont(), ImGui::GetFontSize() * overviewTitleScale);
		const float titleWidth = ImGui::CalcTextSize(overviewTitle.c_str()).x;
		ImGui::TextUnformatted(overviewTitle.c_str());
		const float headingBottom = std::floor(ImGui::GetItemRectMin().y) + Util::Widgets::VisibleTextEnd(overviewTitle.c_str()).y;
		ImGui::PopFont();
		const float supportingTextY = headingBottom - Util::Widgets::VisibleTextEnd(guidance).y;
		const bool inlineGuidance = titleWidth + guidanceWidth + hintWidth + line * 2 < headingWidth;
		if (*guidance && inlineGuidance) {
			ImGui::SameLine(0, line);
			ImGui::SetCursorScreenPos({ ImGui::GetCursorScreenPos().x, supportingTextY });
			ImGui::TextColored(Util::Color::SecondaryText(), "%s", guidance);
		}
		if (titleWidth + hintWidth + line < headingWidth) {
			ImGui::SameLine();
			ImGui::SetCursorScreenPos({ headingStart.x + headingWidth - hintWidth, headingBottom - Util::Widgets::VisibleTextEnd(hint).y });
		}
		ImGui::TextColored(Util::Color::SecondaryText(), "%s", hint);
		if (*guidance && !inlineGuidance) {
			ImGui::PushTextWrapPos(0);
			ImGui::TextColored(Util::Color::SecondaryText(), "%s", guidance);
			ImGui::PopTextWrapPos();
		}
		ImGui::Dummy({ 0, line * .4f });

		if (textInset > 0)
			ImGui::Unindent(textInset);
		const auto [columns, rail, arrow, width] = grid;
		const int rows = (static_cast<int>(cards.size()) + columns - 1) / columns;
		const int toolRows = (static_cast<int>(tools.size()) + columns - 1) / columns;
		const int totalRows = rows + toolRows;
		float minimumHeight = line * 5.5f;
		for (size_t index = 0; index < cards.size(); ++index)
			minimumHeight = std::max(minimumHeight, MeasureCardText(*cards[index], width, static_cast<int>(index) + 1).height);
		for (const auto* step : tools)
			minimumHeight = std::max(minimumHeight, MeasureCardText(*step, width, 0).height);
		const float availableHeight = ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ItemSpacing.y;
		const float rowGap = std::clamp((availableHeight - minimumHeight * totalRows) / std::max(1, totalRows - 1), line * .5f, line);
		const float height = std::clamp((availableHeight - (totalRows - 1) * rowGap) / totalRows, minimumHeight, std::max(minimumHeight, line * 8.5f));
		const auto accent = globals::menu->GetTheme().StatusPalette.InfoColor;
		const auto origin = ImGui::GetCursorScreenPos();
		auto* draw = ImGui::GetWindowDrawList();
		const ImU32 muted = ImGui::GetColorU32(Util::Color::SecondaryText());

		for (int row = 0; row < rows; ++row) {
			const float y = origin.y + row * (height + rowGap);
			if (rail > 0) {
				const ImVec2 centre{ origin.x + line * stageGuideInset, y + height * .5f };
				if (row + 1 < rows)
					draw->AddLine({ centre.x, centre.y + line * (stageMarkerRadius + .35f) }, { centre.x, centre.y + height + rowGap - line * (stageMarkerRadius + .35f) }, ImGui::GetColorU32(ImGuiCol_Border));
				draw->AddCircle(centre, line * stageMarkerRadius, ImGui::GetColorU32(ImGuiCol_Border));
				const auto number = std::to_string(row + 1);
				const float numberFontSize = line * stageNumberScale;
				const auto numberSize = ImGui::GetFont()->CalcTextSizeA(numberFontSize, FLT_MAX, 0, number.c_str());
				draw->AddText(nullptr, numberFontSize, { centre.x - numberSize.x * .5f, centre.y - numberSize.y * .5f }, ImGui::GetColorU32(accent), number.c_str());
				const auto& first = *cards[row * columns];
				std::string labels = first.cardTitle ? first.cardTitle : first.title;
				if (columns == 2 && row * columns + 1 < static_cast<int>(cards.size())) {
					const auto& next = *cards[row * columns + 1];
					labels += std::format(" & {}", next.cardTitle ? next.cardTitle : next.title);
				}
				const auto* stage = first.stage ? first.stage : labels.c_str();
				const float stageWidth = rail - line * (stageDescriptionInset + .4f);
				const float stageFontSize = ImGui::GetFontSize() * stageTextScale;
				const auto stageSize = ImGui::GetFont()->CalcTextSizeA(stageFontSize, FLT_MAX, stageWidth, stage);
				draw->AddText(nullptr, stageFontSize, { origin.x + line * stageDescriptionInset, centre.y - stageSize.y * .5f }, muted, stage, nullptr, stageWidth);
			}
			for (int column = 0; column < columns; ++column) {
				const int index = row * columns + column;
				if (index >= static_cast<int>(cards.size()))
					break;
				const auto& step = *cards[index];
				const ImVec2 minimum{ origin.x + rail + column * (width + arrow), y };
				DrawCard(step, minimum, { width, height }, index + 1);
			}
			if (columns == 2 && row * columns + 1 < static_cast<int>(cards.size())) {
				const float x = origin.x + rail + width + arrow * .25f;
				const float middle = y + height * .5f;
				const float length = arrow * .5f;
				draw->AddLine({ x, middle }, { x + length, middle }, muted);
				draw->AddLine({ x + length - line * .25f, middle - line * .25f }, { x + length, middle }, muted);
				draw->AddLine({ x + length - line * .25f, middle + line * .25f }, { x + length, middle }, muted);
			}
		}
		const float toolsY = origin.y + rows * (height + rowGap);
		for (int index = 0; index < static_cast<int>(tools.size()); ++index) {
			const ImVec2 minimum{ origin.x + rail + (columns == 2 ? index * (width + arrow) : 0), toolsY + (columns == 1 ? index * (height + rowGap) : 0) };
			DrawCard(*tools[index], minimum, { width, height });
		}
		ImGui::SetCursorScreenPos({ origin.x, origin.y + totalRows * height + (totalRows - 1) * rowGap });
		ImGui::Dummy({ available, 0 });
	}

	namespace
	{
		void DrawDottedCardBorder(ImDrawList* draw, ImVec2 minimum, ImVec2 maximum, float rounding, ImU32 colour, float line)
		{
			draw->PathRect(minimum, maximum, rounding);
			const std::vector<ImVec2> outline(draw->_Path.begin(), draw->_Path.end());
			draw->PathClear();
			const float spacing = std::max(3.0f, line * .16f);
			float offset = 0;
			for (size_t i = 0; i < outline.size(); ++i) {
				const auto start = outline[i];
				const auto end = outline[(i + 1) % outline.size()];
				const float length = std::hypot(end.x - start.x, end.y - start.y);
				for (; offset < length; offset += spacing)
					draw->AddCircleFilled(ImLerp(start, end, offset / length), std::max(.55f, line * .025f), colour, 6);
				offset -= length;
			}
		}

		void DrawCardSurface(ImDrawList* draw, ImVec2 minimum, ImVec2 maximum, float rounding, bool hovered, bool inactive)
		{
			const auto background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
			const bool light = background.x + background.y + background.z > 1.5f;
			const ImVec4 lift = light ? ImVec4(-.06f, -.05f, -.045f, 0) : ImVec4(.11f, .14f, .16f, 0);
			const float strength = inactive ? .55f : hovered ? 1.15f :
			                                                   1.0f;
			const ImVec4 top{ std::clamp(background.x + lift.x * strength, 0.0f, 1.0f),
				std::clamp(background.y + lift.y * strength, 0.0f, 1.0f),
				std::clamp(background.z + lift.z * strength, 0.0f, 1.0f), .94f };
			const auto bottom = ImLerp(top, ImVec4(background.x, background.y, background.z, top.w), .22f);
			const int firstVertex = draw->VtxBuffer.Size;
			draw->AddRectFilled(minimum, maximum, ImGui::GetColorU32(top), rounding);
			ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw, firstVertex, draw->VtxBuffer.Size,
				minimum, maximum, ImGui::GetColorU32(top), ImGui::GetColorU32(bottom));
		}

		bool DrawSettingsCard(const Section& step, ImVec2 minimum, ImVec2 size, int a_ordinal, bool selected)
		{
			const auto& style = ImGui::GetStyle();
			const float line = ImGui::GetTextLineHeight();
			const float width = size.x, height = size.y, y = minimum.y;
			const auto accent = globals::menu->GetTheme().StatusPalette.InfoColor;
			const auto muted = ImGui::GetColorU32(Util::Color::SecondaryText());
			auto* draw = ImGui::GetWindowDrawList();
			const ImVec2 maximum{ minimum.x + width, y + height };
			ImGui::SetCursorScreenPos(minimum);
			const bool clicked = ImGui::InvisibleButton(step.id, { width, height }, ImGuiButtonFlags_EnableNav);
			const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemFocused();
			const bool inactive = !step.active || (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled);
			const float rounding = line * .45f;
			const ImU32 border = ImGui::GetColorU32(hovered || selected ? accent : style.Colors[ImGuiCol_Border]);
			DrawCardSurface(draw, minimum, maximum, rounding, hovered, inactive);
			if (inactive)
				DrawDottedCardBorder(draw, minimum, maximum, rounding, border, line);
			else
				draw->AddRect(minimum, maximum, border, rounding);
			const auto text = MeasureCardText(step, width, a_ordinal);
			const float inset = text.inset;
			draw->PushClipRect({ minimum.x + inset, minimum.y + inset }, { maximum.x - inset, maximum.y - inset }, true);
			const SKSE::stl::scope_exit unclip([draw] { draw->PopClipRect(); });
			const float titleX = minimum.x + inset + text.titleOffset;
			if (a_ordinal > 0) {
				const auto number = std::format("{:02}", a_ordinal);
				const float numberFontSize = line * stageNumberScale;
				const float numberY = y + inset + (line * cardTitleScale - numberFontSize) * .5f;
				draw->AddText(nullptr, numberFontSize, { minimum.x + inset, numberY }, ImGui::GetColorU32(accent), number.c_str());
				draw->AddLine({ titleX - line * .3f, numberY }, { titleX - line * .3f, numberY + numberFontSize }, ImGui::GetColorU32(ImGuiCol_Border));
			}
			draw->AddText(nullptr, line * cardTitleScale, { titleX, y + inset }, ImGui::GetColorU32(selected ? accent : style.Colors[ImGuiCol_Text]), step.cardTitle ? step.cardTitle : step.title, nullptr, text.titleWidth);
			const float textWidth = std::max(1.0f, width - inset * 2);
			if (!step.summary.empty())
				draw->AddText(nullptr, line * cardSummaryScale, { minimum.x + inset, y + text.summaryY }, inactive ? muted : ImGui::GetColorU32(ImGuiCol_Text), step.summary.c_str(), nullptr, textWidth);
			draw->AddText(nullptr, line, { minimum.x + inset, y + text.descriptionY }, muted, step.description, nullptr, textWidth);
			return clicked;
		}
	}

	void SettingsPage::DrawCard(const Section& step, ImVec2 minimum, ImVec2 size, int a_ordinal)
	{
		if (DrawSettingsCard(step, minimum, size, a_ordinal, false))
			ImGui::GetStateStorage()->SetBool(ImGui::GetID(step.id), true);
		Util::AddTooltip(step.description);
	}

}
