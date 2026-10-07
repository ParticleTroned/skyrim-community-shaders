#include "SettingsPage.h"

#include "Feature.h"
#include "Globals.h"
#include "Menu.h"
#include "Menu/PerformanceTuningRenderer.h"
#include "Menu/ProfilingRenderer.h"
#include "Utils/UI.h"
#include <algorithm>
#include <format>
#include <mutex>
#include <unordered_map>

namespace MenuUI
{
	namespace
	{
		struct Navigation
		{
			std::string selected = "overview";
			bool pending = false;
			std::vector<Section> sections;
		};
		std::unordered_map<std::string, Navigation> navigation;
		Feature* activeFeature = nullptr;
		std::recursive_mutex navigationMutex;
	}

	FeatureScope::FeatureScope(Feature* a_feature) : previous(activeFeature) { activeFeature = a_feature; }
	FeatureScope::~FeatureScope() { activeFeature = previous; }

	void SettingsPage::Select(const char* a_page, const char* a_section)
	{
		std::scoped_lock lock(navigationMutex);
		auto& state = navigation[a_page];
		state.selected = a_section;
		state.pending = true;
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
		Select(a_page, a_section);
		return true;
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

	SettingsPage::SettingsPage(const char* a_id, std::initializer_list<Section> a_sections) :
		sections(a_sections)
	{
		std::scoped_lock lock(navigationMutex);
		const std::string pageId = activeFeature ? activeFeature->GetShortName() : a_id;
		a_id = pageId.c_str();
		const bool showTimers = activeFeature && globals::profiler && !globals::menu->IsEssentialsUiMode() &&
		                        ProfilingRenderer::HasFeatureTimers(activeFeature->GetShortName());
		if (activeFeature && (showTimers || activeFeature->SupportsPerformanceCostMeasurement()))
			sections.push_back({ "performance", "Performance", "Measure this feature while keeping your settings.", {}, true, false });
		ImGui::PushID(a_id);
		auto& state = navigation[a_id];
		state.sections = sections;
		if (state.selected != "overview" && std::ranges::none_of(sections, [&](const Section& step) {
				return step.visible && state.selected == step.id;
			})) {
			state.selected = "overview";
			state.pending = true;
		}
		if (ImGui::BeginTabBar("##SetupTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
			const auto requested = state.selected;
			auto tab = [&](const char* id, const char* title, const char* help) {
				const auto flags = state.pending && requested == id ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
				if (ImGui::BeginTabItem(title, nullptr, flags)) {
					state.selected = id;
					ImGui::EndTabItem();
				}
				Util::AddTooltip(help);
			};
			tab("overview", "Overview", "Start here. Choose a card to open its settings.");
			for (const auto& step : sections)
				if (step.visible)
					tab(step.id, step.title, step.description);
			ImGui::EndTabBar();
		}
		state.pending = false;
		selected = state.selected;
		contentVisible = ImGui::BeginChild(std::format("##SettingsContent/{}", selected).c_str(), { 0, 0 }, ImGuiChildFlags_None);
		if (contentVisible && selected == "performance" && activeFeature) {
			if (showTimers) {
				ProfilingRenderer::RenderFeatureTimers(activeFeature->GetShortName(), [] {
					PerformanceTuningRenderer::RenderFeatureMeasurement(activeFeature, true);
				});
			} else {
				PerformanceTuningRenderer::RenderFeatureMeasurement(activeFeature);
			}
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
		ImGui::EndChild();
		ImGui::PopID();
	}

	bool SettingsPage::Is(std::string_view a_section) const
	{
		return contentVisible && selected == a_section;
	}

	void SettingsPage::DrawOverview()
	{
		ImGui::TextDisabled("Start with the first step, then refine your settings.");
		ImGui::Spacing();
		const float line = ImGui::GetTextLineHeight();
		const auto& style = ImGui::GetStyle();
		const float gap = style.ItemSpacing.x;
		const float available = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		const int columns = available >= line * 24.0f ? 2 : 1;
		const float width = (available - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
		const auto count = std::ranges::count_if(sections, [](const Section& step) { return step.visible && step.overview; });
		const auto rows = std::max(1, (static_cast<int>(count) + columns - 1) / columns);
		const float rowGap = line * .75f;
		const float height = std::clamp((ImGui::GetContentRegionAvail().y - (rows - 1) * rowGap) / rows, line * 4.1f, line * 5.6f);
		const auto accent = globals::menu->GetTheme().StatusPalette.InfoColor;
		int ordinal = 0;
		for (const auto& step : sections) {
			if (!step.visible || !step.overview)
				continue;
			if (ordinal % columns)
				ImGui::SameLine(0, gap);
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			if (ImGui::InvisibleButton(step.id, { width, height }))
				ImGui::GetStateStorage()->SetBool(ImGui::GetID(step.id), true);
			const bool hovered = ImGui::IsItemHovered();
			auto* draw = ImGui::GetWindowDrawList();
			const auto fill = ImGui::GetStyleColorVec4(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
			draw->AddRectFilled(origin, { origin.x + width, origin.y + height }, ImGui::GetColorU32(fill), style.WindowRounding);
			draw->AddRect(origin, { origin.x + width, origin.y + height }, ImGui::GetColorU32(hovered ? accent : style.Colors[ImGuiCol_Border]), style.WindowRounding);
			const float inset = style.WindowPadding.x + style.FramePadding.x;
			const std::string title = std::format("{:02}  {}", ordinal + 1, step.title);
			draw->PushClipRect(origin, { origin.x + width - inset, origin.y + height - inset }, true);
			draw->AddText({ origin.x + inset, origin.y + inset }, ImGui::GetColorU32(accent), title.c_str());
			const auto& detail = step.summary.empty() ? std::string(step.description) : step.summary;
			draw->AddText(nullptr, 0, { origin.x + inset, origin.y + inset + line * 1.5f }, ImGui::GetColorU32(ImGuiCol_Text), detail.c_str(), nullptr, width - inset * 2);
			draw->PopClipRect();
			Util::AddTooltip(step.description);
			++ordinal;
			if (ordinal % columns == 0) {
				const auto cursor = ImGui::GetCursorScreenPos();
				const float x = cursor.x + available * 0.5f;
				draw->AddLine({ x, cursor.y }, { x, cursor.y + line * 0.65f }, ImGui::GetColorU32(ImGuiCol_Separator));
				ImGui::Dummy({ 0, line * 0.75f });
			}
		}
	}
}
