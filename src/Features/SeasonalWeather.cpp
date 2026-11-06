#include "SeasonalWeather.h"

#include "Api/RuntimeThreadAffinity.h"
#include "CSEditor/EditorWindow.h"
#include "Globals.h"
#include "RE/T/TESRegionDataWeather.h"
#include "RE/W/WeatherType.h"
#include "SnowCover/Environment.h"
#include "State.h"
#include "Utils/Form.h"
#include "Utils/Game.h"
#include "Utils/StringUtils.h"
#include "Utils/VirtualFunctionHook.h"

#include <cmath>
#include <memory>
#include <optional>
#include <utility>

namespace
{
	using namespace SeasonalWeatherPolicy;
	using WeatherList = RE::BSSimpleList<RE::WeatherType*>;

	Category Classify(const RE::TESWeather* weather)
	{
		using Flag = RE::TESWeather::WeatherDataFlag;
		if (weather->data.flags.any(Flag::kSnow))
			return Category::Snow;
		if (weather->data.flags.any(Flag::kRainy))
			return Category::Rain;
		if (weather->data.flags.any(Flag::kCloudy))
			return Category::Cloudy;
		if (weather->data.flags.any(Flag::kPleasant))
			return Category::Clear;
		return Category::Unchanged;
	}

	RE::TESRegionDataWeather* WeatherData(RE::TESRegion* region)
	{
		RE::TESRegionDataWeather* result = nullptr;
		if (region && region->dataList) {
			for (auto* data : region->dataList->regionDataList) {
				if (data && data->GetType() == RE::TESRegionData::Type::kWeather) {
					// Ambiguous lists retain the engine's own priority and selection rules.
					if (result)
						return nullptr;
					result = static_cast<RE::TESRegionDataWeather*>(data);
				}
			}
		}
		return result;
	}

	bool ExcludesSnowfall(const SeasonalWeatherConfig::Settings& settings, RE::TESRegion* region, std::string_view world)
	{
		if (settings.SnowExclusions.empty() || Util::IsInterior())
			return false;
		const auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || player->GetWorldspace() != region->worldSpace)
			return false;
		const auto position = player->GetPosition();
		if (!Util::IsFinite(position.x) || !Util::IsFinite(position.y))
			return false;
		return std::ranges::any_of(settings.SnowExclusions, [&](const auto& area) {
			return area.Snowfall && SeasonalWeatherConfig::MatchesWorld(area, world) &&
			       SnowExclusions::Contains(SeasonalWeatherConfig::PackArea(area), position.x, position.y);
		});
	}

	RE::FormID ResolveFormKey(const std::string& key)
	{
		const auto id = Util::SpidToFormId(key);
		const auto* form = RE::TESForm::LookupByID(id);
		// Oversized light-plugin local IDs must not spill into another plugin's forms.
		return form && Util::ToLowerAscii(Util::GetFormFileKey(form)) == key ? id : 0;
	}

	// The borrowed engine list must outlive the replacement and survive native exits.
	struct ScopedWeatherList
	{
		WeatherList& target;
		WeatherList original;
		ScopedWeatherList(WeatherList& targetList, WeatherList&& replacement) :
			target(targetList), original(std::move(targetList))
		{
			target = std::move(replacement);
		}
		~ScopedWeatherList() { target = std::move(original); }
		ScopedWeatherList(const ScopedWeatherList&) = delete;
		ScopedWeatherList& operator=(const ScopedWeatherList&) = delete;
	};
}

struct SeasonalWeather::Plan
{
	RE::TESRegionDataWeather* data = nullptr;
	std::string reason;
	std::optional<float> month;
	std::vector<RE::WeatherType> entries;
	std::vector<double> baseWeights;
	std::vector<uint32_t> chances;
	bool snowExcluded = false;
};

std::pair<std::string, std::vector<std::string>> SeasonalWeather::GetFeatureSummary()
{
	return { "Adjusts regional weather chances throughout the year.",
		{ "Uses weather mods' regional candidates and normal transitions",
			"Monthly weights for clear, cloudy, rain and snow",
			"Optional snow assignments for regions without snowfall",
			"Selectable snow-only polygons for snowfall and ground snow",
			"Yields to Seasonal Weathers Framework in global-controlled regions",
			"Leaves scripted weather and excluded worlds under native control" } };
}

void SeasonalWeather::DataLoaded()
{
	std::lock_guard lock(mutex);
	configuredForms.clear();
	editorIDIndex = {};
	const auto data = RE::TESDataHandler::GetSingleton();
	frameworkDetected.store(data && (data->LookupLoadedModByName("Seasonal Weathers Framework.esp") ||
										data->LookupLoadedLightModByName("Seasonal Weathers Framework.esp")));
	if (frameworkDetected.load()) {
		logger::info("{}: Seasonal Weathers Framework detected; retaining global-controlled regions", GetDisplayName());
	}
	try {
		const auto result = Util::InstallDetour(REL::RelocationID(16203, 16449).address(),
			reinterpret_cast<void**>(&originalSelect), reinterpret_cast<void*>(&SelectWeather));
		available.store(result.error == NO_ERROR);
		if (result.error != NO_ERROR)
			installationError = std::format("Region selector hook failed ({})", result.error);
	} catch (const std::exception& error) {
		installationError = error.what();
	}
	if (!available.load())
		logger::error("{}: {}", GetDisplayName(), installationError);
}

RE::FormID SeasonalWeather::ResolveConfiguredForm(const std::string& key) const
{
	const auto found = configuredForms.find(key);
	if (found != configuredForms.end())
		return found->second;
	// Settings references are bounded and stable until the next settings update.
	return configuredForms.emplace(key, ResolveFormKey(key)).first->second;
}

SeasonalWeather::Plan SeasonalWeather::BuildPlan(RE::TESRegion* region, bool preview) const
{
	Plan plan;
	auto decline = [&](const char* reason) { plan.reason = reason; return std::move(plan); };
	if (!region)
		return decline("no_region");
	if (globals::state->IsMainOrLoadingMenuOpen())
		return decline("loading_or_main_menu");
	auto* sky = globals::game::sky;
	if (!sky || sky->overrideWeather || EditorWindow::GetSingleton()->IsWeatherLocked())
		return decline("weather_override");
	if (!region->worldSpace)
		return decline("no_worldspace");
	const auto worldKey = Util::ToLowerAscii(Util::GetFormEditorID(region->worldSpace, &editorIDIndex));
	plan.snowExcluded = ExcludesSnowfall(settings, region, worldKey);
	if (!plan.snowExcluded) {
		if (!settings.Enabled && !preview)
			return decline("disabled");
		if (std::ranges::any_of(settings.ExcludedRegions, [&](const auto& key) { return ResolveConfiguredForm(key) == region->GetFormID(); }))
			return decline("excluded_region");
		if (std::ranges::find(settings.ExcludedWorldspaces, worldKey) != settings.ExcludedWorldspaces.end())
			return decline("excluded_worldspace");
		const auto* calendar = globals::game::calendar;
		if (!calendar || !calendar->gameMonth || !calendar->gameDay || !calendar->gameHour)
			return decline("calendar_unavailable");
		const float month = calendar->gameMonth->value;
		const float day = calendar->GetDay();
		const float hour = calendar->GetHour();
		// Validate before the shared helper clamps months or masks nonfinite dates.
		if (!Util::IsFinite(month) || !Util::IsFinite(day) || !Util::IsFinite(hour) ||
			month < 0 || month >= MonthCount || std::floor(month) != month ||
			hour < 0 || hour >= 24 || day < 1 || day > RE::Calendar::DAYS_IN_MONTH[static_cast<size_t>(month)])
			return decline("invalid_calendar");
		const auto fractionalMonth = SnowEnvironment::GetFractionalGameMonth(*calendar);
		if (!Util::IsFinite(fractionalMonth) || fractionalMonth < 0 || fractionalMonth >= MonthCount)
			return decline("invalid_calendar");
		plan.month = fractionalMonth;
	}
	plan.data = WeatherData(region);
	if (!plan.data)
		return decline("no_unambiguous_region_list");
	for (const auto* entry : plan.data->weatherTypes) {
		if (!entry || !entry->weather)
			return decline("invalid_weather_entry");
		if (plan.entries.size() == MaxCandidates)
			return decline("candidate_limit");
		if (entry->global && (frameworkDetected.load() || settings.SkipGlobalControlledRegions))
			return decline(frameworkDetected.load() ? "seasonal_weathers_framework" : "weather_global_control");
		const double weight = entry->global ? static_cast<double>(entry->global->value) : static_cast<double>(entry->chance);
		if (!Util::IsFinite(weight) || weight < 0 || weight > 1000000)
			return decline("invalid_native_weight");
		plan.entries.push_back(*entry);
		plan.baseWeights.push_back(weight);
	}
	if (plan.entries.empty())
		return decline("empty_region_list");
	for (const auto& assignment : settings.SnowAssignments) {
		if (plan.snowExcluded)
			break;
		if (ResolveConfiguredForm(assignment.Region) != region->GetFormID())
			continue;
		auto* weather = RE::TESForm::LookupByID<RE::TESWeather>(ResolveConfiguredForm(assignment.Weather));
		if (!weather || Classify(weather) != Category::Snow)
			return decline("snow_assignment_unavailable");
		// Existing zero-weight entries remain deliberate exclusions.
		if (std::ranges::any_of(plan.entries, [&](const auto& entry) { return entry.weather == weather; }))
			continue;
		if (plan.entries.size() == MaxCandidates)
			return decline("candidate_limit");
		plan.entries.push_back({ weather, 0, 0, nullptr });
		plan.baseWeights.push_back(assignment.Weight);
	}
	std::vector<double> weights;
	weights.reserve(plan.entries.size());
	std::unordered_map<RE::FormID, Category> categories;
	if (!plan.snowExcluded)
		for (const auto& override : settings.WeatherCategories)
			categories.emplace(ResolveConfiguredForm(override.Weather), override.Category);
	bool removesSnow = false;
	for (size_t i = 0; i < plan.entries.size(); ++i) {
		const auto* weather = plan.entries[i].weather;
		auto category = Classify(weather);
		if (const auto found = categories.find(weather->GetFormID()); found != categories.end())
			category = found->second;
		removesSnow |= category == Category::Snow && plan.baseWeights[i] > 0;
		const auto multiplier = plan.snowExcluded ? (category == Category::Snow ? 0.0f : 1.0f) : Multiplier(settings.MonthlyWeights, *plan.month, category);
		weights.push_back(plan.baseWeights[i] * multiplier);
	}
	if (plan.snowExcluded && !removesSnow)
		return decline("no_snow_candidates");
	plan.chances = Percentages(weights);
	if (plan.chances.empty())
		return decline(plan.snowExcluded ? "no_non_snow_candidate" : "all_weights_zero");
	for (size_t i = 0; i < plan.entries.size(); ++i) {
		plan.entries[i].chance = plan.chances[i];
		plan.entries[i].global = nullptr;
	}
	return plan;
}

RE::TESWeather* SeasonalWeather::SelectWeather(RE::TESRegion* region)
{
	auto& feature = globals::features::seasonalWeather;
	static thread_local bool selecting = false;
	if (selecting)
		return originalSelect(region);
	std::unique_lock lock(feature.mutex);
	struct SelectionGuard
	{
		bool& active;
		SelectionGuard(bool& value) : active(value) { active = true; }
		~SelectionGuard() { active = false; }
	} selectionGuard(selecting);
	if (feature.faulted.load() || !CSX::Api::IsRuntimeMainThread() ||
		(!feature.enabled.load(std::memory_order_relaxed) && std::ranges::none_of(feature.settings.SnowExclusions, [](const auto& area) { return area.Enabled && area.Snowfall; })))
		return originalSelect(region);
	std::optional<Plan> plan;
	WeatherList replacement;
	try {
		plan.emplace(feature.BuildPlan(region));
		if (plan->reason.empty())
			for (auto it = plan->entries.rbegin(); it != plan->entries.rend(); ++it)
				if (it->chance > 0)
					replacement.push_front(std::addressof(*it));
	} catch (const std::exception& error) {
		feature.faulted.store(true);
		logger::error("{}: retaining native selection after preparation failed: {}", feature.GetDisplayName(), error.what());
		return originalSelect(region);
	}
	if (!plan->reason.empty()) {
		return originalSelect(region);
	}
	ScopedWeatherList listGuard(plan->data->weatherTypes, std::move(replacement));
#ifdef DEVBENCH_BRIDGE_ENABLED
	feature.adjustedSelections.fetch_add(1, std::memory_order_relaxed);
#endif
	return originalSelect(region);
}

void SeasonalWeather::Apply(const json& patch)
{
	json candidate = SeasonalWeatherConfig::Serialize(settings);
	if (!patch.is_object())
		throw std::invalid_argument("seasonalWeather must be an object");
	candidate.update(patch);
	auto validated = SeasonalWeatherConfig::Parse(candidate);
	if (validated.Enabled && !settings.Enabled && (!available.load() || faulted.load()))
		throw std::invalid_argument(std::format("{} selector is unavailable; inspect installationError and faulted", GetDisplayName()));
	for (const auto& assignment : validated.SnowAssignments) {
		if (std::ranges::any_of(settings.SnowAssignments, [&](const auto& old) {
				return old.Region == assignment.Region && old.Weather == assignment.Weather && old.Weight == assignment.Weight;
			}))
			continue;
		if (!RE::TESForm::LookupByID<RE::TESRegion>(ResolveFormKey(assignment.Region)))
			throw std::invalid_argument("Snow assignment region is not loaded: " + assignment.Region);
		const auto* weather = RE::TESForm::LookupByID<RE::TESWeather>(ResolveFormKey(assignment.Weather));
		if (!weather || Classify(weather) != Category::Snow)
			throw std::invalid_argument("Snow assignment requires a loaded, snow-flagged weather: " + assignment.Weather);
	}
	for (const auto& override : validated.WeatherCategories) {
		if (std::ranges::any_of(settings.WeatherCategories, [&](const auto& old) {
				return old.Weather == override.Weather && old.Category == override.Category;
			}))
			continue;
		if (!RE::TESForm::LookupByID<RE::TESWeather>(ResolveFormKey(override.Weather)))
			throw std::invalid_argument("Weather category is not loaded: " + override.Weather);
	}
	settings = std::move(validated);
	configuredForms.clear();
	enabled.store(settings.Enabled, std::memory_order_relaxed);
}

void SeasonalWeather::ApplyEditorEdits(const SeasonalWeatherConfig::Settings& original, const SeasonalWeatherConfig::Settings& edited)
{
	const auto before = SeasonalWeatherConfig::Serialize(original);
	const auto after = SeasonalWeatherConfig::Serialize(edited);
	const auto current = SeasonalWeatherConfig::Serialize(settings);
	json patch = json::object();
	for (const auto& [key, value] : after.items()) {
		if (value == before.at(key))
			continue;
		// Preserve unrelated concurrent changes and reject conflicting edits atomically.
		if (current.at(key) != before.at(key) && current.at(key) != value)
			throw std::invalid_argument("Settings changed while editing " + key + "; retry the edit");
		patch[key] = value;
	}
	if (!patch.empty())
		Apply(patch);
}

void SeasonalWeather::LoadSettings(json& input)
{
	const auto validated = SeasonalWeatherConfig::Parse(input);
	std::lock_guard lock(mutex);
	settings = validated;
	configuredForms.clear();
	enabled.store(settings.Enabled, std::memory_order_relaxed);
}

void SeasonalWeather::SaveSettings(json& output)
{
	std::lock_guard lock(mutex);
	output = SeasonalWeatherConfig::Serialize(settings);
}

void SeasonalWeather::RestoreDefaultSettings()
{
	std::lock_guard lock(mutex);
	settings = {};
	configuredForms.clear();
	enabled.store(false, std::memory_order_relaxed);
}

json SeasonalWeather::Inspect(RE::TESRegion* region, bool includeSettings) const
{
	const auto plan = BuildPlan(region, true);
	json candidates = json::array();
	for (size_t i = 0; i < plan.entries.size(); ++i) {
		const auto* weather = plan.entries[i].weather;
		candidates.push_back({ { "weather", Util::GetFormFileKey(weather) }, { "name", Util::GetFormEditorID(weather, &editorIDIndex) },
			{ "baseWeight", plan.baseWeights[i] }, { "chancePercent", plan.chances.empty() ? json(nullptr) : json(plan.chances[i]) } });
	}
	const auto reason = !settings.Enabled && !plan.snowExcluded ? "disabled" : (!available.load() || faulted.load() ? "selector_unavailable" : plan.reason);
	json result{ { "available", available.load() }, { "faulted", faulted.load() }, { "installationError", installationError },
		{ "frameworkDetected", frameworkDetected.load() },
		{ "snowExcludedAtPlayer", plan.snowExcluded },
		{ "region", region ? Util::GetFormFileKey(region) : "" },
		{ "month", plan.month ? json(*plan.month) : json(nullptr) }, { "bypassReason", reason }, { "candidates", std::move(candidates) },
		{ "eligibilityReason", plan.reason },
		{ "persistence", "Menu Save settings" } };
#ifdef DEVBENCH_BRIDGE_ENABLED
	result["adjustedSelections"] = adjustedSelections.load();
#endif
	if (includeSettings)
		result["settings"] = SeasonalWeatherConfig::Serialize(settings);
	return result;
}

json SeasonalWeather::Control(const json& request)
{
	std::lock_guard lock(mutex);
	RE::TESRegion* region = globals::game::sky ? globals::game::sky->region : nullptr;
	if (request.contains("seasonalRegion")) {
		if (!request.at("seasonalRegion").is_string())
			throw std::invalid_argument("seasonalRegion must be a portable region identifier");
		const auto key = SeasonalWeatherConfig::FormKey(request.at("seasonalRegion").get_ref<const std::string&>());
		region = RE::TESForm::LookupByID<RE::TESRegion>(ResolveFormKey(key));
		if (!region)
			throw std::invalid_argument("seasonalRegion did not resolve to a region");
	}
	if (request.contains("seasonalWeather"))
		Apply(request.at("seasonalWeather"));
	return Inspect(region);
}

SnowExclusions::MaskData SeasonalWeather::GetSnowCoverExclusions(std::string_view world) const
{
	std::lock_guard lock(mutex);
	SnowExclusions::MaskData result{};
	for (const auto& area : settings.SnowExclusions)
		if (area.SnowCover && SeasonalWeatherConfig::MatchesWorld(area, world))
			result.areas[result.count++] = SeasonalWeatherConfig::PackArea(area);
	return result;
}
