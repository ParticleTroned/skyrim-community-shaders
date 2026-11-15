#include "SettingsSearch.h"

#include "ControlCatalog.h"
#include "Feature.h"
#include "FeatureIssues.h"
#include "Globals.h"
#include "Menu.h"
#include "Menu/ExternalSettingsPage.h"
#include "Menu/FeatureListRenderer.h"
#include "Menu/PerformanceTuningRenderer.h"
#include "Menu/SettingsPage.h"
#include "SettingsSearchPolicy.h"
#include "State.h"
#include "Utils/StringUtils.h"
#include "Utils/UI.h"
#include <algorithm>
#include <chrono>
#include <imgui_internal.h>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <nlohmann/json.hpp>
#endif

namespace MenuUI::SettingsSearch
{
	namespace
	{
		using Policy::MaxQueryBytes;
		using Policy::MaxResults;
		constexpr auto revealDuration = std::chrono::seconds(3);
		std::recursive_mutex searchMutex;
		struct Indexed
		{
			const ControlCatalog::Entry* control = nullptr;
			std::string id, feature, title, breadcrumb, foldedTitle, foldedLocation;
			size_t owner;
		};
		struct Owner
		{
			Feature* feature;
			std::string name, title;
			bool pageAvailable = false, controlsAvailable = false;
		};
		struct Match
		{
			size_t index;
			int score;
		};
		std::vector<Indexed> index;
		std::vector<Owner> owners;
		bool registryDirty = true;
		std::vector<Match> matches;
		std::string lastQuery;
		bool queryValid = false;
		bool truncated = false;
		thread_local std::string_view drawingPage;
		struct Reveal
		{
			std::string id, page, section, label, rawLabel, selector, choice, group;
			std::string state;
			std::chrono::steady_clock::time_point deadline;
			int drawnFrames = 0;
			bool active = false;
			bool found = false;
			bool scrollRequested = false;
			ImGuiID item = 0, window = 0;
		};
		Reveal reveal;
		std::string navigationError;
		void ExpireReveal()
		{
			if (reveal.active && std::chrono::steady_clock::now() >= reveal.deadline) {
				reveal.active = false;
				if (!reveal.found)
					reveal.state = "not_visible";
			}
		}
		bool Available(const Indexed& entry)
		{
			const auto& owner = owners[entry.owner];
			return entry.control ? owner.controlsAvailable : owner.pageAvailable;
		}

		void RefreshIndex()
		{
			if (registryDirty) {
				registryDirty = false;
				owners.clear();
				index.clear();
				for (auto* feature : FeatureListRenderer::GetMenuFeatures())
					if (feature)
						owners.push_back({ feature, feature->GetShortName(), feature->GetDisplayName() });
				for (const auto* name : { "Home", "General", "Advanced", "Profiling", "PerformanceTuning" })
					owners.push_back({ nullptr, name, name });
				index.reserve(ControlCatalog::Entries().size() + owners.size());
				for (size_t i = 0; i < owners.size(); ++i) {
					const auto& owner = owners[i];
					index.push_back({ nullptr, "page:" + owner.name, owner.name, owner.title, "Page", {}, {}, i });
				}
				for (const auto& control : ControlCatalog::Entries()) {
					const auto owner = std::ranges::find(owners, control.feature, &Owner::name);
					if (owner == owners.end())
						continue;
					std::string breadcrumb = owner->title;
					if (!control.section.empty())
						breadcrumb += " > " + std::string(control.section);
					if (!control.choice.empty())
						breadcrumb += " > " + std::string(control.choice);
					if (!control.group.empty())
						breadcrumb += " > " + std::string(control.group);
					index.push_back({ &control, std::string(control.id), std::string(control.feature), std::string(control.label), std::move(breadcrumb), {}, {}, static_cast<size_t>(owner - owners.begin()) });
				}
				for (auto& entry : index) {
					entry.foldedTitle = Util::ToLowerAscii(entry.title);
					entry.foldedLocation = Util::ToLowerAscii(entry.breadcrumb + " " + entry.feature);
				}
				queryValid = false;
			}
			for (auto& owner : owners) {
				const auto* feature = owner.feature;
				const bool page = !feature || (feature->IsInMenu() && !feature->IsHiddenFromUserView() &&
												  (feature->loaded || dynamic_cast<const ExternalSettingsPage*>(feature) || !FeatureIssues::IsObsoleteFeature(owner.name) || (globals::state && globals::state->IsDeveloperMode())));
				const bool controls = page && (!feature || dynamic_cast<const ExternalSettingsPage*>(feature) ||
												  (feature->loaded && globals::state && !globals::state->IsFeatureDisabled(owner.name)));
				if (owner.pageAvailable != page || owner.controlsAvailable != controls) {
					owner.pageAvailable = page;
					owner.controlsAvailable = controls;
					queryValid = false;
				}
			}
		}

		void Search(std::string_view query)
		{
			RefreshIndex();
			query = query.substr(0, MaxQueryBytes);
			if (queryValid && query == lastQuery)
				return;
			lastQuery = query;
			navigationError.clear();
			queryValid = true;
			matches.clear();
			truncated = false;
			const auto folded = Util::ToLowerAscii(query);
			const auto words = Policy::Words(folded);
			if (words.empty())
				return;
			for (size_t i = 0; i < index.size(); ++i) {
				const auto& entry = index[i];
				if (!Available(entry))
					continue;
				const int score = Policy::Score(entry.foldedTitle, entry.foldedLocation, words);
				if (score >= 0)
					matches.push_back({ i, score });
			}
			const auto count = std::min(matches.size(), MaxResults);
			truncated = matches.size() > count;
			std::partial_sort(matches.begin(), matches.begin() + count, matches.end(), [](const Match& a, const Match& b) {
				if (a.score != b.score)
					return a.score > b.score;
				return index[a.index].id < index[b.index].id;
			});
			matches.resize(count);
		}
	}

	void Invalidate()
	{
		std::scoped_lock lock(searchMutex);
		registryDirty = true;
	}

	void CancelReveal()
	{
		std::scoped_lock lock(searchMutex);
		if ((reveal.active && !reveal.found) || reveal.state == "page_queued")
			reveal.state = "cancelled";
		reveal.active = false;
	}

	bool Navigate(std::string_view id, std::string& error)
	{
		std::scoped_lock lock(searchMutex);
		RefreshIndex();
		const auto found = std::ranges::find(index, id, &Indexed::id);
		if (found == index.end() || !Available(*found)) {
			error = "That search result is no longer available.";
			return false;
		}
		if (!globals::menu || !globals::menu->IsEnabled) {
			error = "Open the CSX menu before navigating.";
			return false;
		}
		if (PerformanceTuningRenderer::HasActiveMeasurements()) {
			error = "Finish or cancel the performance comparison before navigating.";
			return false;
		}
		if (!ImGui::GetCurrentContext() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
			error = "Finish or cancel the open dialog before navigating.";
			return false;
		}
		const auto* control = found->control;
		const auto page = control ? std::string(control->page) : found->feature;
		const auto section = control && !control->section.empty() ? std::string(control->section) : page == "Home" ? "welcome" :
		                                                                                                             "overview";
		if (!SettingsPage::Select(page.c_str(), section.c_str())) {
			error = "That section is unavailable, or has an unsaved navigation draft.";
			return false;
		}
		reveal = {};
		reveal.id = found->id;
		reveal.page = page;
		reveal.state = "page_queued";
		if (control) {
			reveal.section = section;
			reveal.label = found->title;
			reveal.rawLabel = control->rawLabel;
			reveal.selector = control->selector;
			reveal.choice = control->choice;
			reveal.group = control->group;
			reveal.state = "queued";
			reveal.deadline = std::chrono::steady_clock::now() + revealDuration;
			reveal.active = true;
		}
		const auto builtIn = FeatureListRenderer::GetBuiltInPageMenuEntryId(found->feature);
		globals::menu->SelectFeatureMenu(builtIn.empty() ? found->feature : builtIn);
		error.clear();
		return true;
	}

	bool Draw(std::string_view query)
	{
		if (query.empty())
			return false;
		std::scoped_lock lock(searchMutex);
		Search(query);
		if (!navigationError.empty())
			ImGui::TextWrapped("%s", navigationError.c_str());
		if (matches.empty())
			ImGui::TextDisabled("No matching settings or features.");
		for (const auto& match : matches) {
			const auto& entry = index[match.index];
			ImGui::PushID(entry.id.c_str());
			const SKSE::stl::scope_exit restore([] { ImGui::PopID(); });
			if (ImGui::Selectable(entry.title.c_str()))
				Navigate(entry.id, navigationError);
			ImGui::TextColored(Util::Color::SecondaryText(), "%s", entry.breadcrumb.c_str());
		}
		if (truncated)
			ImGui::TextWrapped("Showing the first %zu matches. Add a feature name to narrow your search.", MaxResults);
		return true;
	}

	PageScope::PageScope(std::string_view page) : previous(drawingPage)
	{
		std::scoped_lock lock(searchMutex);
		drawingPage = page;
		ExpireReveal();
		if (reveal.state == "page_queued" && page == reveal.page)
			reveal.state = "page_open";
		if (reveal.active && page == reveal.page) {
			++reveal.drawnFrames;
			ImGui::TextWrapped("Search: %s", reveal.label.c_str());
			if (!reveal.found && reveal.drawnFrames > 2)
				ImGui::TextWrapped("This control may require another option or an expanded group on this page.");
		}
	}

	PageScope::~PageScope() { drawingPage = previous; }

	bool SelectNavigationChoice(const char* label, int& selected, const char* const items[], int count)
	{
		std::scoped_lock lock(searchMutex);
		if (!reveal.active || drawingPage != reveal.page || reveal.selector != label ||
			SettingsPage::Selected(reveal.page.c_str()) != reveal.section || std::chrono::steady_clock::now() >= reveal.deadline)
			return false;
		for (int i = 0; i < count; ++i)
			if (items[i] && reveal.choice == items[i]) {
				reveal.selector.clear();
				const bool changed = selected != i;
				selected = i;
				return changed;
			}
		return false;
	}

	void PrepareGroup(const char* label)
	{
		std::scoped_lock lock(searchMutex);
		if (reveal.active && drawingPage == reveal.page && reveal.group == label &&
			reveal.selector.empty() && !reveal.found && std::chrono::steady_clock::now() < reveal.deadline &&
			SettingsPage::Selected(reveal.page.c_str()) == reveal.section)
			ImGui::SetNextItemOpen(true);
	}

	void NotifyControl(const char* label)
	{
		if (drawingPage.empty())
			return;
		std::scoped_lock lock(searchMutex);
		if (!reveal.active || drawingPage != reveal.page || !label || reveal.rawLabel != label ||
			!reveal.selector.empty() || std::chrono::steady_clock::now() >= reveal.deadline || ImGui::GetCurrentWindow()->SkipItems ||
			SettingsPage::Selected(reveal.page.c_str()) != reveal.section)
			return;
		const auto window = ImGui::GetCurrentWindow()->ID;
		const auto item = ImGui::GetItemID();
		if (reveal.scrollRequested && (reveal.item != item || reveal.window != window))
			return;
		if (!reveal.scrollRequested) {
			reveal.scrollRequested = true;
			reveal.item = item;
			reveal.window = window;
			ImGui::SetScrollHereY(.5f);
		}
		if (!ImGui::IsItemVisible())
			return;
		reveal.found = true;
		reveal.state = "revealed";
		auto* draw = ImGui::GetWindowDrawList();
		draw->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_CheckMark), ImGui::GetStyle().FrameRounding, 0, 2.0f);
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	nlohmann::json Query(std::string_view query)
	{
		std::scoped_lock lock(searchMutex);
		if (query.size() > MaxQueryBytes || query.find('\0') != std::string_view::npos)
			return { { "ok", false }, { "error", "query must be at most 255 UTF-8 bytes without NUL" } };
		Search(query);
		auto results = nlohmann::json::array();
		for (const auto& match : matches) {
			const auto& entry = index[match.index];
			results.push_back({ { "id", entry.id }, { "title", entry.title }, { "location", entry.breadcrumb }, { "feature", entry.feature } });
		}
		return { { "ok", true }, { "results", std::move(results) }, { "limit", MaxResults }, { "truncated", truncated } };
	}

	nlohmann::json Status()
	{
		std::scoped_lock lock(searchMutex);
		ExpireReveal();
		return { { "id", reveal.id }, { "page", reveal.page }, { "state", reveal.state }, { "revealed", reveal.found } };
	}
#endif
}
