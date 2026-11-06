#include "SeasonalWeather.h"

#include "Globals.h"
#include "Utils/Form.h"
#include "Utils/Game.h"
#include "Utils/StringUtils.h"
#include "Utils/UI.h"

#include <array>
#include <optional>

namespace
{
	constexpr std::array<const char*, 12> Months{ "Morning Star", "Sun's Dawn", "First Seed", "Rain's Hand", "Second Seed", "Midyear", "Sun's Height", "Last Seed", "Hearthfire", "Frostfall", "Sun's Dusk", "Evening Star" };

	const char* ReasonLabel(std::string_view reason)
	{
		if (reason.empty())
			return "Ready for the next natural weather change.";
		if (reason == "disabled")
			return "Monthly selection is disabled. Projected seasonal chances are a preview; enabled snow-only areas remain independent.";
		if (reason == "no_snow_candidates")
			return "No snowfall candidates need filtering. Native selection is retained.";
		if (reason == "no_non_snow_candidate")
			return "This area has no eligible non-snow weather. Native selection is retained; add a suitable non-snow candidate in your weather mod.";
		if (reason == "seasonal_weathers_framework")
			return "Seasonal Weathers Framework is loaded. This region uses weather globals, so CSX leaves its weather chances and candidates unchanged.";
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
		Util::AddTooltip("Recommended when another mod controls seasons. Seasonal Weathers Framework always takes priority when detected, regardless of this setting.");
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

	void DrawAreaPreview(const SeasonalWeatherConfig::SnowExclusion& area)
	{
		for (const auto& point : area.Vertices)
			if (!Util::IsFinite(point[0]) || !Util::IsFinite(point[1]) || std::abs(point[0]) > SeasonalWeatherConfig::MaxAreaCoordinate || std::abs(point[1]) > SeasonalWeatherConfig::MaxAreaCoordinate)
				return;
		const auto bounds = SeasonalWeatherConfig::PackArea(area);
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 size(std::min(ImGui::GetContentRegionAvail().x, 400.0f), 200.0f);
		const float scale = std::min((size.x - 20) / std::max(1.0f, bounds.maxX - bounds.minX),
			(size.y - 20) / std::max(1.0f, bounds.maxY - bounds.minY));
		auto project = [&](const auto& p) { return ImVec2(origin.x + 10 + (p[0] - bounds.minX) * scale, origin.y + size.y - 10 - (p[1] - bounds.minY) * scale); };
		auto* draw = ImGui::GetWindowDrawList();
		for (size_t i = 0; i < area.Vertices.size(); ++i) {
			const auto point = project(area.Vertices[i]);
			draw->AddLine(point, project(area.Vertices[(i + 1) % area.Vertices.size()]), ImGui::GetColorU32(ImGuiCol_PlotLines), 2);
			draw->AddText(point, ImGui::GetColorU32(ImGuiCol_Text), std::to_string(i + 1).c_str());
		}
		ImGui::Dummy(size);
	}

	struct SnowAreaEditor
	{
		int selected = 0;
		std::optional<SeasonalWeatherConfig::SnowExclusion> draft;
		int editingIndex = -1;
		json originalAreas;
		std::string error;
		bool pending = false;
	};

	bool DrawSnowAreas(SeasonalWeatherConfig::Settings& edited, SnowAreaEditor& editor)
	{
		if (!ImGui::CollapsingHeader("Snow-only areas"))
			return false;
		ImGui::TextWrapped("Choose your own exterior areas, independent of hold or region borders. Each polygon has 3-16 corners in world XY game units; up to 8 areas can overlap. These switches work independently of monthly selection.");
		ImGui::TextWrapped("Ground snow masks CSX Snow Cover on terrain, objects and foliage. Rain, wetness and seasonal plant colors remain active. Baked snow textures and other mods' snow are unchanged. Snowfall removes snow-flagged candidates at the next natural change, preserving other relative weights. Scripted weather and protected weather globals retain control.");
		auto& [selected, draft, editingIndex, originalAreas, error, pending] = editor;
		static Util::FormEditorIDIndex editorIDs;
		const auto* player = RE::PlayerCharacter::GetSingleton();
		auto* world = player && !Util::IsInterior() ? player->GetWorldspace() : nullptr;
		const auto worldID = world ? Util::ToLowerAscii(Util::GetFormEditorID(world, &editorIDs)) : "";
		const auto position = player ? player->GetPosition() : RE::NiPoint3{};
		selected = std::clamp(selected, 0, std::max(0, static_cast<int>(edited.SnowExclusions.size()) - 1));
		{
			Util::DisableGuard busy(draft.has_value());
			if (ImGui::BeginCombo("Area", edited.SnowExclusions.empty() ? "No areas configured" : edited.SnowExclusions[selected].Name.c_str())) {
				const SKSE::stl::scope_exit endCombo([] { ImGui::EndCombo(); });
				for (size_t i = 0; i < edited.SnowExclusions.size(); ++i) {
					ImGui::PushID(static_cast<int>(i));
					const SKSE::stl::scope_exit popID([] { ImGui::PopID(); });
					if (ImGui::Selectable(edited.SnowExclusions[i].Name.c_str(), selected == static_cast<int>(i)))
						selected = static_cast<int>(i);
				}
			}
		}
		{
			Util::DisableGuard busy(draft.has_value());
			{
				Util::DisableGuard full(edited.SnowExclusions.size() >= SnowExclusions::MaxAreas);
				if (ImGui::Button("New area")) {
					draft.emplace();
					if (!worldID.empty())
						draft->Worldspace = worldID;
					for (const auto& offset : std::array<std::array<float, 2>, 4>{ { { -4096, -4096 }, { 4096, -4096 }, { 4096, 4096 }, { -4096, 4096 } } })
						draft->Vertices.push_back({ position.x + offset[0], position.y + offset[1] });
					editingIndex = -1;
					originalAreas = SeasonalWeatherConfig::Serialize(edited).at("SnowExclusions");
					error.clear();
				}
			}
			ImGui::SameLine();
			Util::DisableGuard empty(edited.SnowExclusions.empty());
			if (ImGui::Button("Edit selected area")) {
				draft = edited.SnowExclusions[selected];
				editingIndex = selected;
				originalAreas = SeasonalWeatherConfig::Serialize(edited).at("SnowExclusions");
				error.clear();
			}
			ImGui::SameLine();
			if (ImGui::Button("Remove selected area")) {
				edited.SnowExclusions.erase(edited.SnowExclusions.begin() + selected);
				return true;
			}
		}
		if (!draft)
			return false;
		char name[256]{}, worldName[256]{};
		std::copy_n(draft->Name.begin(), std::min(draft->Name.size(), sizeof(name) - 1), name);
		std::copy_n(draft->Worldspace.begin(), std::min(draft->Worldspace.size(), sizeof(worldName) - 1), worldName);
		if (ImGui::InputText("Area name", name, sizeof(name)))
			draft->Name = name;
		if (ImGui::InputText("Worldspace editor ID", worldName, sizeof(worldName)))
			draft->Worldspace = worldName;
		{
			Util::DisableGuard noWorld(worldID.empty());
			if (ImGui::Button("Use current world"))
				draft->Worldspace = worldID;
		}
		ImGui::Checkbox("Enable this area", &draft->Enabled);
		ImGui::Checkbox("Exclude ground snow", &draft->SnowCover);
		ImGui::Checkbox("Exclude natural snowfall", &draft->Snowfall);
		ImGui::TextWrapped("Place corners around the perimeter in order. Use player XY to trace a boundary in game, or enter coordinates. Apply area validates the complete shape; Save settings persists it.");
		DrawAreaPreview(*draft);
		for (size_t i = 0; i < draft->Vertices.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			const SKSE::stl::scope_exit popID([] { ImGui::PopID(); });
			ImGui::InputFloat2(std::format("Corner {}", i + 1).c_str(), draft->Vertices[i].data(), "%.0f");
			{
				Util::DisableGuard wrongWorld(worldID.empty() || Util::ToLowerAscii(draft->Worldspace) != worldID);
				if (ImGui::Button("Use player XY"))
					draft->Vertices[i] = { position.x, position.y };
			}
			ImGui::SameLine();
			{
				Util::DisableGuard full(draft->Vertices.size() >= SnowExclusions::MaxVertices);
				if (ImGui::Button("Insert after")) {
					const auto a = draft->Vertices[i], b = draft->Vertices[(i + 1) % draft->Vertices.size()];
					draft->Vertices.insert(draft->Vertices.begin() + i + 1, { (a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f });
					break;
				}
			}
			ImGui::SameLine();
			Util::DisableGuard minimum(draft->Vertices.size() <= 3);
			if (ImGui::Button("Remove corner")) {
				draft->Vertices.erase(draft->Vertices.begin() + i);
				break;
			}
		}
		if (ImGui::Button("Apply area")) {
			try {
				if (originalAreas != SeasonalWeatherConfig::Serialize(edited).at("SnowExclusions"))
					throw std::invalid_argument("Areas changed elsewhere. Cancel and reopen the area before applying.");
				auto candidate = edited;
				if (editingIndex < 0)
					candidate.SnowExclusions.push_back(*draft);
				else
					candidate.SnowExclusions[editingIndex] = *draft;
				edited = std::move(candidate);
				pending = true;
				error.clear();
				return true;
			} catch (const std::exception& failure) {
				error = failure.what();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel area edit")) {
			draft.reset();
			error.clear();
		}
		if (!error.empty())
			ImGui::TextWrapped("%s", error.c_str());
		return false;
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
	if (Util::Widgets::Checkbox("Monthly weather selection", &value)) {
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
	if (frameworkDetected.load())
		ImGui::TextWrapped("Seasonal Weathers Framework detected: CSX only adjusts regions without weather globals. Snow Cover, melting and wetness remain independent.");
	SeasonalWeatherConfig::Settings original;
	{
		std::lock_guard lock(mutex);
		original = settings;
	}
	auto edited = original;
	static SnowAreaEditor areaEditor;
	areaEditor.pending = false;
	bool changed = DrawSeasonControls(edited, selectedWeather);
	changed |= DrawSnowAreas(edited, areaEditor);
	auto* region = DrawRegionPicker();
	changed |= DrawRegionSnow(edited, region, selectedWeather);
	static std::string settingsError;
	if (changed) {
		try {
			std::lock_guard lock(mutex);
			ApplyEditorEdits(original, edited);
			if (areaEditor.pending) {
				areaEditor.selected = areaEditor.editingIndex < 0 ? static_cast<int>(edited.SnowExclusions.size()) - 1 : areaEditor.editingIndex;
				areaEditor.draft.reset();
			}
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
	if (status.at("snowExcludedAtPlayer").get<bool>())
		ImGui::TextWrapped("A snow-only area matches the player's position. Snowfall filtering remains subject to the weather ownership and candidate checks above.");
	if (!status.at("eligibilityReason").get_ref<const std::string&>().empty() && status.at("eligibilityReason") != status.at("bypassReason"))
		ImGui::TextWrapped("%s", ReasonLabel(status.at("eligibilityReason").get<std::string>()));
	if (!status.at("installationError").get_ref<const std::string&>().empty())
		ImGui::TextWrapped("%s", status.at("installationError").get_ref<const std::string&>().c_str());
	DrawCandidateTable(status);
}
