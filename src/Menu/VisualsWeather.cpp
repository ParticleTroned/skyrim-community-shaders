#include "VisualsWeather.h"

#include "CSEditor/EditorWindow.h"
#include "Features/AdaptiveBrightness.h"
#include "Features/WeatherPicker.h"
#include "Globals.h"
#include "SettingsActions.h"
#include "SettingsPage.h"
#include "Utils/Form.h"
#include "Utils/UI.h"
#include "Utils/WeatherFilter.h"
#include "WeatherFeatureOverrides.h"
#include "WeatherManager.h"

#include <algorithm>
#include <format>
#include <unordered_map>
#include <vector>

namespace MenuUI
{
	using json = nlohmann::json;

	json ApplyVisualsWeather(const std::string& sourceKey, uint32_t filter, bool allMatches, const json* settings)
	{
		json result = { { "ok", false }, { "matchedWeathers", 0 }, { "savedWeathers", 0 }, { "appliedWeathers", 0 }, { "weatherRefreshed", false }, { "error", "" } };
		auto& feature = globals::features::adaptiveBrightness;
		if (const auto* reason = GetFeatureMutationBlockReason(&feature)) {
			result["error"] = reason;
			return result;
		}
		if (filter == 0 || (filter & ~Util::WeatherFilter::All) != 0) {
			result["error"] = "Select a valid weather category.";
			return result;
		}
		auto* editor = EditorWindow::GetSingleton();
		auto* manager = WeatherManager::GetSingleton();
		try {
			const auto catalog = WeatherPicker::GetWeatherCatalog();
			auto source = std::ranges::find_if(catalog, [&](auto* weather) { return Util::GetFormFileKey(weather) == sourceKey; });
			if (source == catalog.end() || !Util::WeatherFilter::Matches((*source)->data.flags.underlying(), filter)) {
				result["error"] = "Select a weather in the displayed category.";
				return result;
			}
			std::unordered_map<RE::TESWeather*, WeatherWidget*> widgets;
			widgets.reserve(editor->weatherWidgets.size());
			for (auto& entry : editor->weatherWidgets) {
				if (auto* widget = dynamic_cast<WeatherWidget*>(entry.get()); widget && widget->weather)
					widgets.emplace(widget->weather, widget);
			}
			const auto featureName = feature.GetShortName();
			json value;
			std::string error;
			if (settings) {
				value = *settings;
			} else {
				json sourceSettings;
				if (const auto widget = widgets.find(*source); widget != widgets.end()) {
					sourceSettings = widget->second->settings.featureSettings;
				} else {
					json attachment;
					if (!WeatherFeatureOverrides::Read(*source, attachment, error)) {
						result["error"] = error;
						return result;
					}
					sourceSettings = attachment.value("featureSettings", json::object());
				}
				if (!sourceSettings.is_object() || !sourceSettings.contains(featureName)) {
					result["error"] = "Edit the selected weather's Visuals adjustments first.";
					return result;
				}
				value = sourceSettings.at(featureName);
			}
			if (!WeatherFeatureOverrides::Prepare(&feature, value, error)) {
				result["error"] = error;
				return result;
			}
			std::vector<RE::TESWeather*> targets;
			targets.reserve(allMatches ? catalog.size() : 1);
			for (auto* weather : catalog) {
				if (allMatches ? Util::WeatherFilter::Matches(weather->data.flags.underlying(), filter) : weather == *source)
					targets.push_back(weather);
			}
			result["matchedWeathers"] = targets.size();
			for (auto* weather : targets) {
				result["failedWeather"] = Util::GetFormFileKey(weather);
				std::map<std::string, json> effective, persisted;
				manager->GetAllSettingsForWeather(weather, effective);
				effective[featureName] = value;
				bool changed = false;
				if (!WeatherFeatureOverrides::Save(weather, featureName, &value, persisted, changed, error)) {
					result["error"] = error;
					break;
				}
				result["savedWeathers"] = result["savedWeathers"].get<size_t>() + 1;
				manager->StageSettingsForWeather(weather, effective);
				manager->CommitSettingsForWeather(weather, persisted);
				if (const auto widget = widgets.find(weather); widget != widgets.end())
					editor->RecordWeatherFeatureSave(*widget->second, featureName, value);
				result["appliedWeathers"] = result["appliedWeathers"].get<size_t>() + 1;
				result.erase("failedWeather");
			}
			result["ok"] = result["appliedWeathers"] == result["matchedWeathers"];
		} catch (const std::exception& error) {
			result["error"] = error.what();
		}
		if (result["savedWeathers"].get<size_t>() != 0) {
			try {
				manager->RefreshFeatureOverrides();
				result["weatherRefreshed"] = true;
			} catch (const std::exception& error) {
				result["ok"] = false;
				result["error"] = result["error"].get<std::string>() + std::format(" Weather refresh failed: {}", error.what());
			}
		}
		return result;
	}

	void DrawVisualsWeather()
	{
		static uint32_t filter = Util::WeatherFilter::All;
		static RE::TESWeather* selected = nullptr;
		static bool allSelected = false;
		static Util::WeatherFilter::CachedList weatherList;
		static ActionFeedback feedback;
		bool selectionChanged = false;
		SectionHeading("Weather adjustments", "Select one weather or all matching weathers, edit their Visuals adjustments, then Apply & Save. Saved adjustments are used whenever those weathers are active.");
		{
			float categoryWidth = 0;
			for (const auto& category : Util::WeatherFilter::Classified)
				categoryWidth = std::max(categoryWidth, ImGui::CalcTextSize(category.canonicalName.data()).x);
			DetailGrid categories("VisualsWeatherCategories", 4, categoryWidth + ImGui::GetFontSize() * 2);
			auto choice = [&](const char* label, uint32_t mask) {
				categories.Next();
				if (ImGui::Selectable(label, filter == mask)) {
					filter = mask;
					allSelected = false;
				}
			};
			choice("All", Util::WeatherFilter::All);
			for (const auto& category : Util::WeatherFilter::Classified)
				choice(category.canonicalName.data(), static_cast<uint32_t>(category.flag));
			choice("None", Util::WeatherFilter::Unclassified);
		}
		{
			DetailGrid selectionActions("VisualsWeatherSelection", 3);
			selectionActions.Next();
			auto* sky = globals::game::sky;
			{
				const auto disabled = Util::DisableGuard(!sky || !sky->currentWeather);
				if (ImGui::Button("Select Current Weather")) {
					filter = Util::WeatherFilter::All;
					selected = sky->currentWeather;
					allSelected = false;
					selectionChanged = true;
				}
			}
			const bool matchesChanged = weatherList.Update(WeatherPicker::GetWeatherCatalog(), filter);
			const auto matches = weatherList.Get();
			if (matchesChanged)
				allSelected = false;
			if ((matchesChanged || selectionChanged) && std::ranges::find(matches, selected) == matches.end())
				selected = nullptr;
			selectionActions.Next();
			{
				const auto disabled = Util::DisableGuard(matches.empty());
				if (ImGui::Button("Select all matching")) {
					allSelected = true;
					if (!selected)
						selected = matches.front();
				}
				Util::AddTooltip("Select every weather in the displayed category. Edit one template, then Apply & Save its Visuals adjustments to the whole group.");
			}
			selectionActions.Next();
			if (ImGui::Button("Clear selection")) {
				selected = nullptr;
				allSelected = false;
			}
		}
		const auto matches = weatherList.Get();
		ImGui::Text("%zu matching weathers; %zu selected", matches.size(), allSelected ? matches.size() : selected ? size_t{ 1 } :
																													 size_t{ 0 });
		if (selected)
			DetailText(std::format("{}: {}", allSelected ? "Group template" : "Selected", Util::FormatWeather(selected)).c_str());
		const auto* reason = GetFeatureMutationBlockReason(&globals::features::adaptiveBrightness);
		if (reason)
			AvailabilityNotice(reason);

		{
			DetailGrid actions("VisualsWeatherActions", 2);
			actions.Next();
			const auto disabled = Util::DisableGuard(!selected || reason);
			if (ImGui::Button(allSelected ? "Edit Group Settings" : "Edit Selected Weather"))
				EditorWindow::GetSingleton()->OpenWeatherFeatureSetting(selected, "AdaptiveBrightness", "brightness");
			Util::AddTooltip("In Features > Visuals, switch Using Global Settings to Using Weather-Specific Settings, then adjust the controls. Return here to apply and save.");
			actions.Next();
			if (ImGui::Button(allSelected ? "Apply & Save All Matches" : "Apply & Save Selected")) {
				const auto result = ApplyVisualsWeather(Util::GetFormFileKey(selected), filter, allSelected);
				feedback.error = !result.at("ok").get<bool>();
				feedback.message = std::format("Saved Visuals adjustments for {} of {} weathers.", result.at("savedWeathers").get<size_t>(), result.at("matchedWeathers").get<size_t>());
				if (feedback.error)
					feedback.message += std::format(" Applied {}. {} {}", result.at("appliedWeathers").get<size_t>(), result.value("failedWeather", std::string{}), result.at("error").get<std::string>());
			}
		}
		DetailText("In the editor, open Features > Visuals and enable Using Weather-Specific Settings. Adjust the controls, then return here to Apply & Save. Leave Pause Weather Overrides unchecked to use the saved adjustments.");
		feedback.Draw();
		if (ImGui::BeginListBox("##VisualsWeatherList", { -1, ImGui::GetTextLineHeightWithSpacing() * 8 })) {
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(matches.size()));
			while (clipper.Step()) {
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
					auto* weather = matches[row];
					ImGui::PushID(weather);
					if (ImGui::Selectable(Util::FormatWeather(weather).c_str(), allSelected || selected == weather)) {
						selected = weather;
						allSelected = false;
					}
					ImGui::PopID();
				}
			}
			ImGui::EndListBox();
		}
	}
}
