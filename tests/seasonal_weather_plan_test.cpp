#include "Features/SeasonalWeatherConfig.h"
#include "Utils/StringUtils.h"

#include <atomic>
#include <cstdio>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace RE
{
	using FormID = uint32_t;
	struct TESForm
	{
		FormID id = 0;
		std::string editorID;
		FormID GetFormID() const { return id; }
		template <class T>
		static T* LookupByID(FormID id);
	};
	struct TESGlobal
	{
		float value = 0;
	};
	struct TESWeather : TESForm
	{
		enum WeatherDataFlag
		{
			kPleasant = 1,
			kCloudy = 2,
			kRainy = 4,
			kSnow = 8
		};
		struct Flags
		{
			uint32_t value = kPleasant;
			bool any(WeatherDataFlag flag) const { return (value & flag) != 0; }
		};
		struct Data
		{
			Flags flags;
		} data;
	};
	struct WeatherType
	{
		TESWeather* weather = nullptr;
		uint32_t chance = 0;
		uint32_t unk0C = 0;
		TESGlobal* global = nullptr;
	};
	struct TESRegionData
	{
		enum class Type
		{
			kWeather,
			kOther
		};
		Type type = Type::kWeather;
		Type GetType() const { return type; }
	};
	struct TESRegionDataWeather : TESRegionData
	{
		std::vector<WeatherType*> weatherTypes;
	};
	struct TESRegionDataList
	{
		std::vector<TESRegionData*> regionDataList;
	};
	struct TESRegion : TESForm
	{
		TESRegionDataList* dataList = nullptr;
		TESForm* worldSpace = nullptr;
	};
	struct Calendar
	{
		static constexpr uint8_t DAYS_IN_MONTH[]{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
		TESGlobal* gameMonth = nullptr;
		TESGlobal* gameDay = nullptr;
		TESGlobal* gameHour = nullptr;
		uint32_t GetMonth() const { return gameMonth ? static_cast<uint32_t>(gameMonth->value) : 7; }
		float GetDay() const { return gameDay ? gameDay->value : 17; }
		float GetHour() const { return gameHour ? gameHour->value : 12; }
	};
	struct Sky
	{
		TESWeather* overrideWeather = nullptr;
	};
	struct PlayerCharacter
	{
		TESForm* world = nullptr;
		struct Position
		{
			float x = 0, y = 0;
		} position;
		static PlayerCharacter* GetSingleton()
		{
			static PlayerCharacter player;
			return &player;
		}
		TESForm* GetWorldspace() const { return world; }
		Position GetPosition() const { return position; }
	};
}
namespace Util
{
	bool interior = false;
	bool IsInterior() { return interior; }
	struct FormEditorIDIndex
	{};
	std::string GetFormEditorID(const RE::TESForm* form, FormEditorIDIndex*) { return form->editorID; }
}
namespace SnowEnvironment
{
#include "seasonal_weather_calendar_under_test.h"
}
struct State
{
	bool loading = false;
	bool IsMainOrLoadingMenuOpen() const { return loading; }
};
struct EditorWindow
{
	bool locked = false;
	static EditorWindow* GetSingleton()
	{
		static EditorWindow editor;
		return &editor;
	}
	bool IsWeatherLocked() const { return locked; }
};
namespace globals
{
	State stateStorage;
	State* state = &stateStorage;
	namespace game
	{
		RE::Sky* sky = nullptr;
		RE::Calendar* calendar = nullptr;
	}
}
struct SeasonalWeather
{
	struct Plan;
	SeasonalWeatherConfig::Settings settings;
	std::atomic_bool frameworkDetected{ false };
	mutable std::unordered_map<std::string, RE::FormID> configuredForms;
	mutable Util::FormEditorIDIndex editorIDIndex;
	RE::FormID ResolveConfiguredForm(const std::string& key) const;
	Plan BuildPlan(RE::TESRegion* region, bool preview = false) const;
};
std::unordered_map<std::string, RE::FormID> keys;
std::unordered_map<RE::FormID, RE::TESWeather*> weathers;
RE::FormID ResolveFormKey(const std::string& key) { return keys.contains(key) ? keys.at(key) : 0; }
template <class T>
T* RE::TESForm::LookupByID(FormID id)
{
	return weathers.contains(id) ? weathers.at(id) : nullptr;
}

using namespace SeasonalWeatherPolicy;
#include "seasonal_weather_plan_helpers_under_test.h"
#include "seasonal_weather_plan_type_under_test.h"
#include "seasonal_weather_plan_under_test.h"

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
}

int main()
try {
	RE::TESGlobal month{ 0 }, day{ 1 }, hour{ 0 };
	RE::Calendar calendar{ &month, &day, &hour };
	RE::Sky sky;
	globals::game::calendar = &calendar;
	globals::game::sky = &sky;
	RE::TESForm world{ 10, "Tamriel" };
	RE::TESWeather clear, cloudy, rain, snow;
	std::array weather{ &clear, &cloudy, &rain, &snow };
	std::array<RE::WeatherType, 4> entries;
	RE::TESRegionDataWeather data;
	for (size_t i = 0; i < weather.size(); ++i) {
		weather[i]->id = static_cast<uint32_t>(i + 1);
		weather[i]->data.flags.value = 1u << i;
		weathers.emplace(weather[i]->id, weather[i]);
		keys.emplace(std::to_string(i + 1), weather[i]->id);
		entries[i] = { weather[i], i == 0 ? 40u : 20u, 0, nullptr };
		data.weatherTypes.push_back(&entries[i]);
	}
	RE::TESRegionDataList list{ { &data } };
	RE::TESRegion region;
	region.id = 20;
	region.worldSpace = &world;
	region.dataList = &list;
	keys.emplace("region", region.id);
	SeasonalWeather feature;
	feature.settings.Enabled = true;
	auto plan = [&] { return feature.BuildPlan(&region); };
	auto decline = [&](const char* reason) { Require(plan().reason == reason, reason); };
	const auto winter = plan();
	Require(winter.reason.empty() && winter.chances == std::vector<uint32_t>({ 21, 15, 4, 60 }), "Native winter candidates");
	Require(entries[0].chance == 40 && entries[3].chance == 20, "Planning must not mutate native entries");
	month.value = 6;
	Require(plan().chances[3] < winter.chances[3], "Summer must reduce snow probability");
	month.value = 0;
	RE::PlayerCharacter::GetSingleton()->world = &world;
	SeasonalWeatherConfig::SnowExclusion area{ "River", "tamriel", true, true, true, { { -10, -10 }, { 10, -10 }, { 10, 10 }, { -10, 10 } } };
	feature.settings.SnowExclusions.push_back(area);
	Require(plan().snowExcluded && plan().chances == std::vector<uint32_t>({ 50, 25, 25, 0 }), "Snow-only areas preserve relative non-snow weather weights");
	feature.settings.Enabled = false;
	globals::game::calendar = nullptr;
	Require(plan().chances == std::vector<uint32_t>({ 50, 25, 25, 0 }), "Snow areas work without monthly selection or a calendar");
	Require(!plan().month, "Calendar-independent snow filtering must not report January");
	entries[3].chance = 0;
	decline("no_snow_candidates");
	Require(plan().chances.empty(), "No-op areas must preserve native chances without renormalizing");
	entries[3].chance = 20;
	RE::TESGlobal controlled{ 70 };
	entries[3].global = &controlled;
	feature.frameworkDetected = true;
	feature.settings.SkipGlobalControlledRegions = false;
	decline("seasonal_weathers_framework");
	feature.frameworkDetected = false;
	feature.settings.SkipGlobalControlledRegions = true;
	decline("weather_global_control");
	entries[3].global = nullptr;
	feature.settings.WeatherCategories = { { "4", Category::Clear }, { "1", Category::Snow } };
	Require(plan().chances == std::vector<uint32_t>({ 50, 25, 25, 0 }), "Snow areas use precipitation flags, not monthly category overrides");
	feature.settings.WeatherCategories.clear();
	for (size_t i = 0; i < 3; ++i)
		entries[i].chance = 0;
	decline("no_non_snow_candidate");
	for (size_t i = 0; i < 3; ++i)
		entries[i].chance = i == 0 ? 40 : 20;
	RE::PlayerCharacter::GetSingleton()->position.x = 11;
	decline("disabled");
	RE::PlayerCharacter::GetSingleton()->position.x = 0;
	Util::interior = true;
	decline("disabled");
	Util::interior = false;
	RE::PlayerCharacter::GetSingleton()->world = nullptr;
	decline("disabled");
	feature.settings.SnowExclusions.clear();
	globals::game::calendar = &calendar;
	Require(feature.BuildPlan(&region, true).chances == winter.chances, "Disabled monthly selection retains read-only chance previews");
	feature.settings.Enabled = true;

	for (auto* field : { &month, &day, &hour }) {
		const auto original = field->value;
		for (const float invalid : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1.0f }) {
			field->value = invalid;
			decline("invalid_calendar");
		}
		field->value = original;
	}
	month.value = 12;
	decline("invalid_calendar");
	month.value = 0.5f;
	decline("invalid_calendar");
	month.value = 1;
	day.value = 29;
	decline("invalid_calendar");
	month.value = 0;
	day.value = 1;
	hour.value = 24;
	decline("invalid_calendar");
	hour.value = 0;
	month.value = 11;
	day.value = 31;
	hour.value = 23;
	Require(plan().reason.empty(), "Last day of the year remains eligible");
	month.value = 0;
	day.value = 1;
	hour.value = 0;
	calendar.gameDay = nullptr;
	decline("calendar_unavailable");
	calendar.gameDay = &day;
	globals::game::calendar = nullptr;
	decline("calendar_unavailable");
	globals::game::calendar = &calendar;

	RE::TESGlobal global{ 70 };
	entries[3].global = &global;
	decline("weather_global_control");
	feature.settings.SkipGlobalControlledRegions = false;
	const auto globalPlan = plan();
	Require(globalPlan.reason.empty() && globalPlan.baseWeights[3] == 70 && globalPlan.entries[3].global == nullptr, "Opt-in global weights");
	Require(entries[3].global == &global && global.value == 70, "Globals must remain borrowed");
	feature.frameworkDetected = true;
	decline("seasonal_weathers_framework");
	Require(entries[3].global == &global && entries[3].chance == 20 && global.value == 70, "Framework state must remain untouched even with global skipping disabled");
	entries[3].global = nullptr;
	Require(plan().chances == winter.chances, "Unpatched regions remain eligible with the framework loaded");
	entries[3].global = &global;
	feature.frameworkDetected = false;
	global.value = std::numeric_limits<float>::quiet_NaN();
	decline("invalid_native_weight");
	entries[3].global = nullptr;

	feature.settings.ExcludedRegions = { "region" };
	decline("excluded_region");
	feature.settings.ExcludedRegions.clear();
	feature.settings.ExcludedWorldspaces = { "tamriel" };
	decline("excluded_worldspace");
	feature.settings.ExcludedWorldspaces.clear();
	sky.overrideWeather = &clear;
	decline("weather_override");
	sky.overrideWeather = nullptr;
	EditorWindow::GetSingleton()->locked = true;
	decline("weather_override");
	EditorWindow::GetSingleton()->locked = false;
	globals::state->loading = true;
	decline("loading_or_main_menu");
	globals::state->loading = false;
	list.regionDataList.push_back(&data);
	decline("no_unambiguous_region_list");
	list.regionDataList.pop_back();

	feature.settings.SnowAssignments = { { "region", "4", 20 } };
	entries[3].chance = 0;
	Require(plan().chances[3] == 0, "Existing zero snow entries remain excluded");
	data.weatherTypes.pop_back();
	feature.frameworkDetected = true;
	entries[0].global = &global;
	decline("seasonal_weathers_framework");
	entries[0].global = nullptr;
	Require(plan().entries.size() == 4 && plan().entries.back().weather == &snow, "Explicit snow assignment adds a missing candidate");
	feature.settings.SnowAssignments.front().Weather = "missing";
	decline("snow_assignment_unavailable");
	feature.settings.SnowAssignments.clear();
	feature.settings.WeatherCategories = { { "1", Category::Snow } };
	Require(plan().chances.front() > winter.chances.front(), "Explicit classification changes the selected multiplier");
	feature.settings.WeatherCategories.clear();
	for (auto& row : feature.settings.MonthlyWeights)
		row.fill(0);
	decline("all_weights_zero");
	feature.settings.MonthlyWeights = DefaultWeights;
	data.weatherTypes.assign(MaxCandidates, &entries[0]);
	Require(plan().reason.empty(), "Candidate capacity remains eligible");
	data.weatherTypes.push_back(&entries[0]);
	decline("candidate_limit");
	data.weatherTypes.assign(1, nullptr);
	decline("invalid_weather_entry");
} catch (const std::exception& error) {
	std::fprintf(stderr, "%s\n", error.what());
	return 1;
}
