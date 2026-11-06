#include "SeasonalWeather.h"

#include "Globals.h"
#include "Utils/Form.h"
#include "Utils/StringUtils.h"
#include "Utils/UI.h"

#include <array>

namespace
{
	constexpr std::array<const char*, 12> Months{ "Morning Star", "Sun's Dawn", "First Seed", "Rain's Hand", "Second Seed", "Midyear", "Sun's Height", "Last Seed", "Hearthfire", "Frostfall", "Sun's Dusk", "Evening Star" };

	const char* ReasonLabel(std::string_view reason)
	{
		if (reason.empty())
			return "Ready for the next natural weather change.";
		if (reason == "disabled")
			return "Preview only. Enable Seasonal Weather to change natural weather chances.";
		if (reason == "weather_global_control")
			return "This region has weather globals. Another weather controller may own its chances.";
		if (reason == "excluded_worldspace")
			return "This world is excluded from seasonal changes.";
		if (reason == "excluded_region")
			return "This region is excluded from seasonal changes.";
		if (reason == "weather_override")
			return "Scripted weather or an editor lock currently controls the sky.";
		if (reason == "all_weights_zero")
			return "All candidate weights are zero. The game's usual selection is retained.";
		if (reason == "snow_assignment_unavailable")
			return "An assigned snow weather is missing or is no longer classified as snow.";
		if (reason == "selector_unavailable")
			return "Seasonal selection is unavailable in this session.";
		if (reason == "loading_or_main_menu")
			return "Load an exterior scene to inspect regional weather.";
		if (reason == "invalid_calendar")
			return "The game calendar is invalid. The game's usual selection is retained.";
		return "This region cannot currently use seasonal selection; its usual weather is retained.";
	}
	bool DrawSeasonControls(SeasonalWeatherConfig::Settings& edited, RE::TESWeather* selectedWeather)
	{
		bool changed = ImGui::Checkbox("Leave regions with weather globals unchanged", &edited.SkipGlobalControlledRegions);
		Util::AddTooltip("Recommended when another mod controls seasons. Turn off only after choosing which mod should own seasonal weather chances.");
		if (selectedWeather) {
			const auto weatherKey = Util::ToLowerAscii(Util::GetFormFileKey(selectedWeather));
			auto found = std::ranges::find(edited.WeatherCategories, weatherKey, &SeasonalWeatherConfig::WeatherCategory::Weather);
			int category = found == edited.WeatherCategories.end() ? 0 : static_cast<int>(found->Category) + 1;
			if (ImGui::Combo("Selection category for this weather", &category, "Automatic\0Clear\0Cloudy\0Rain\0Snow\0Original weight\0")) {
				std::erase_if(edited.WeatherCategories, [&](const auto& entry) { return entry.Weather == weatherKey; });
				if (category > 0)
					edited.WeatherCategories.push_back({ weatherKey, static_cast<SeasonalWeatherPolicy::Category>(category - 1) });
				changed = true;
			}
			Util::AddTooltip("Changes which seasonal weight is used for selection. It does not change this weather's precipitation or appearance.");
		}
		if (ImGui::CollapsingHeader("Monthly weather weights")) {
			ImGui::TextWrapped("Each row is Clear / Cloudy / Rain / Snow. Higher weights make that category more likely relative to the others. A zero weight excludes that category while this controller is active.");
			for (size_t i = 0; i < Months.size(); ++i)
				changed |= ImGui::SliderFloat4(Months[i], edited.MonthlyWeights[i].data(), 0.0f, SeasonalWeatherPolicy::MaxMultiplier, "%.2fx");
			if (ImGui::Button("Restore monthly weights")) {
				edited.MonthlyWeights = SeasonalWeatherPolicy::DefaultWeights;
				changed = true;
			}
		}
		return changed;
	}

	RE::TESRegion* DrawRegionPicker()
	{
		static Util::FormEditorIDIndex editorIDs;
		static std::string selectedRegionKey;
		RE::TESRegion* region = globals::game::sky ? globals::game::sky->region : nullptr;
		if (!selectedRegionKey.empty())
			region = RE::TESForm::LookupByID<RE::TESRegion>(Util::SpidToFormId(selectedRegionKey));
		ImGui::SeparatorText("Regional weather");
		const auto regionName = region ? Util::GetFormDisplayName(region->GetFormID(), &editorIDs) : "Current sky region";
		if (ImGui::BeginCombo("Region", regionName.c_str())) {
			const SKSE::stl::scope_exit endCombo([] { ImGui::EndCombo(); });
			if (ImGui::Selectable("Follow current sky", selectedRegionKey.empty())) {
				selectedRegionKey.clear();
				region = globals::game::sky ? globals::game::sky->region : nullptr;
			}
			if (auto* data = RE::TESDataHandler::GetSingleton())
				for (auto* candidate : data->GetFormArray<RE::TESRegion>()) {
					if (!candidate)
						continue;
					ImGui::PushID(static_cast<int>(candidate->GetFormID()));
					const SKSE::stl::scope_exit popId([] { ImGui::PopID(); });
					const auto key = Util::GetFormFileKey(candidate);
					if (ImGui::Selectable(Util::GetFormDisplayName(candidate->GetFormID(), &editorIDs).c_str(), selectedRegionKey == key)) {
						selectedRegionKey = key;
						region = candidate;
					}
				}
		}
		return region;
	}

	bool DrawRegionSnow(SeasonalWeatherConfig::Settings& edited, RE::TESRegion* region, RE::TESWeather* selectedWeather)
	{
		static Util::FormEditorIDIndex editorIDs;
		bool changed = false;
		static RE::TESWeather* assignedWeather = nullptr;
		static RE::TESWeather* previousSelection = nullptr;
		if (selectedWeather != previousSelection && selectedWeather && selectedWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kSnow))
			assignedWeather = selectedWeather;
		previousSelection = selectedWeather;
		if (region) {
			const auto regionKey = Util::ToLowerAscii(Util::GetFormFileKey(region));
			bool excluded = std::ranges::find(edited.ExcludedRegions, regionKey) != edited.ExcludedRegions.end();
			if (ImGui::Checkbox("Exclude this region", &excluded)) {
				std::erase(edited.ExcludedRegions, regionKey);
				if (excluded)
					edited.ExcludedRegions.push_back(regionKey);
				changed = true;
			}
			if (region->worldSpace) {
				const auto worldKey = Util::ToLowerAscii(Util::GetFormEditorID(region->worldSpace, &editorIDs));
				bool worldExcluded = std::ranges::find(edited.ExcludedWorldspaces, worldKey) != edited.ExcludedWorldspaces.end();
				Util::DisableGuard noKey(worldKey.empty());
				if (ImGui::Checkbox("Exclude this world", &worldExcluded)) {
					std::erase(edited.ExcludedWorldspaces, worldKey);
					if (worldExcluded)
						edited.ExcludedWorldspaces.push_back(worldKey);
					changed = true;
				}
			}
			const auto snowName = assignedWeather ? Util::GetFormDisplayName(assignedWeather->GetFormID(), &editorIDs) : "Choose snow weather";
			if (ImGui::BeginCombo("Additional snow candidate", snowName.c_str())) {
				const SKSE::stl::scope_exit endCombo([] { ImGui::EndCombo(); });
				if (auto* data = RE::TESDataHandler::GetSingleton())
					for (auto* weather : data->GetFormArray<RE::TESWeather>()) {
						if (!weather || weather->data.flags.none(RE::TESWeather::WeatherDataFlag::kSnow))
							continue;
						ImGui::PushID(static_cast<int>(weather->GetFormID()));
						const SKSE::stl::scope_exit popId([] { ImGui::PopID(); });
						if (ImGui::Selectable(Util::GetFormDisplayName(weather->GetFormID(), &editorIDs).c_str(), assignedWeather == weather))
							assignedWeather = weather;
					}
			}
			{
				Util::DisableGuard noWeather(!assignedWeather || edited.SnowAssignments.size() >= SeasonalWeatherPolicy::MaxAssignments);
				if (ImGui::Button("Assign snow to this region")) {
					const auto key = Util::ToLowerAscii(Util::GetFormFileKey(assignedWeather));
					if (!std::ranges::any_of(edited.SnowAssignments, [&](const auto& entry) { return entry.Region == regionKey && entry.Weather == key; })) {
						edited.SnowAssignments.push_back({ regionKey, key, 20.0f });
						changed = true;
					}
				}
			}
			Util::AddTooltip("Adds this snow to the region's candidate list. Choose a snow weather that fits your weather mod. Existing entries keep their original base weights.");
			for (size_t i = 0; i < edited.SnowAssignments.size();) {
				auto& entry = edited.SnowAssignments[i];
				if (entry.Region != regionKey) {
					++i;
					continue;
				}
				ImGui::PushID(static_cast<int>(i));
				const SKSE::stl::scope_exit popId([] { ImGui::PopID(); });
				ImGui::TextUnformatted(entry.Weather.c_str());
				changed |= ImGui::SliderFloat("Base weight", &entry.Weight, SeasonalWeatherPolicy::MinAssignmentWeight, SeasonalWeatherPolicy::MaxAssignmentWeight, "%.2f");
				if (ImGui::Button("Remove assignment")) {
					edited.SnowAssignments.erase(edited.SnowAssignments.begin() + i);
					changed = true;
				} else
					++i;
			}
		}
		return changed;
	}

	void DrawCandidateTable(const json& status)
	{
		if (ImGui::BeginTable("SeasonalWeatherCandidates", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
			const SKSE::stl::scope_exit endTable([] { ImGui::EndTable(); });
			ImGui::TableSetupColumn("Weather");
			ImGui::TableSetupColumn("Base weight");
			ImGui::TableSetupColumn("Projected chance");
			ImGui::TableHeadersRow();
			for (const auto& candidate : status.at("candidates")) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				const auto label = candidate.at("name").get<std::string>();
				ImGui::TextUnformatted(label.empty() ? candidate.at("weather").get_ref<const std::string&>().c_str() : label.c_str());
				ImGui::TableNextColumn();
				ImGui::Text("%.2f", candidate.at("baseWeight").get<double>());
				ImGui::TableNextColumn();
				if (candidate.at("chancePercent").is_null())
					ImGui::TextUnformatted("Native selection");
				else
					ImGui::Text("%u%%", candidate.at("chancePercent").get<uint32_t>());
			}
		}
	}

}

void SeasonalWeather::DrawSettingsEnabledControl()
{
	bool value = enabled.load();
	Util::DisableGuard disabled(!value && (!available.load() || faulted.load()));
	if (Util::Widgets::Checkbox("Enabled", &value)) {
		std::lock_guard lock(mutex);
		settings.Enabled = value;
		enabled.store(value, std::memory_order_relaxed);
	}
	Util::AddTooltip("Adjusts future natural weather changes. Save settings to retain this preference.");
}

void SeasonalWeather::DrawSettings() { DrawEditorSettings(nullptr); }

void SeasonalWeather::DrawEditorSettings(RE::TESWeather* selectedWeather)
{
	if (selectedWeather)
		DrawSettingsEnabledControl();
	ImGui::TextWrapped("Adjust weather through the year using the same game calendar as Snow Cover. Changes apply at the next natural weather change. Use Save settings in the CSX menu to keep them.");
	SeasonalWeatherConfig::Settings edited;
	{
		std::lock_guard lock(mutex);
		edited = settings;
	}
	bool changed = DrawSeasonControls(edited, selectedWeather);
	auto* region = DrawRegionPicker();
	changed |= DrawRegionSnow(edited, region, selectedWeather);
	static std::string settingsError;
	if (changed) {
		try {
			std::lock_guard lock(mutex);
			Apply(SeasonalWeatherConfig::Serialize(edited));
			settingsError.clear();
		} catch (const std::exception& error) {
			settingsError = error.what();
		}
	}
	if (!settingsError.empty())
		ImGui::TextWrapped("Settings were retained: %s", settingsError.c_str());
	json status;
	{
		std::lock_guard lock(mutex);
		status = Inspect(region, false);
	}
	ImGui::TextWrapped("%s", ReasonLabel(status.at("bypassReason").get<std::string>()));
	if (!status.at("eligibilityReason").get_ref<const std::string&>().empty() && status.at("eligibilityReason") != status.at("bypassReason"))
		ImGui::TextWrapped("%s", ReasonLabel(status.at("eligibilityReason").get<std::string>()));
	if (!status.at("installationError").get_ref<const std::string&>().empty())
		ImGui::TextWrapped("%s", status.at("installationError").get_ref<const std::string&>().c_str());
	DrawCandidateTable(status);
}
