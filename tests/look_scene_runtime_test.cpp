#include "Presets/LocationSkyLighting.h"
#include "Presets/LookSceneRuntime.h"
#include "Presets/LookScenes.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_set>
using Presets::Json;
namespace RE
{
	struct Form
	{};
	struct PlayerCharacter
	{
		static PlayerCharacter* GetSingleton()
		{
			static PlayerCharacter player;
			return &player;
		}
		Form* GetParentCell()
		{
			static Form cell;
			return &cell;
		}
		Form* GetWorldspace() { return nullptr; }
		Form* GetCurrentLocation() { return nullptr; }
	};
}
namespace globals
{
	struct State
	{
		bool isLoadingMenuOpen = false, isMainMenuOpen = false, isMapMenuOpen = false;
		float timer = 100;
		uint32_t frameCount = 0;
	} stateStorage;
	inline State* state = &stateStorage;
	namespace game
	{
		struct Climate
		{
			struct Timing
			{
				struct Period
				{
					int begin = 30, end = 60;
				} sunrise, sunset;
			} timing;
		};
		struct Sky
		{
			Climate* currentClimate = nullptr;
			float currentGameHour = 12;
		} skyStorage;
		inline Sky* sky = &skyStorage;
		struct Calendar
		{
			struct Hour
			{
				float value = 12;
			} hour;
			Hour* gameHour = &hour;
		} calendarStorage;
		inline Calendar* calendar = &calendarStorage;
	}
}
namespace Util
{
	inline bool interior = false;
	bool IsInterior() { return interior; }
	std::string GetFormFileKey(const RE::Form*) { return "0x1~Test.esm"; }
	template <class Visitor>
	void VisitLocationLayers(RE::Form*, RE::Form*, RE::Form* cell, Visitor visit)
	{
		visit(cell);
	}
}
struct WeatherManager
{
	bool active = true;
	struct Current
	{
		RE::Form* currentWeather = nullptr;
		RE::Form* lastWeather = nullptr;
		float lerpFactor = 1;
	};
	static WeatherManager* GetSingleton()
	{
		static WeatherManager manager;
		return &manager;
	}
	Current GetCurrentWeathers() { return {}; }
	bool HasActiveVariablesForFeature(const std::string&) const { return active; }
};
struct SceneSettingsManager
{
	static std::vector<std::string> GetInteriorRelevantFeatureNames();
	bool paused = false;
	static SceneSettingsManager* GetSingleton()
	{
		static SceneSettingsManager manager;
		return &manager;
	}
	bool IsFeaturePaused(const std::string&) const { return paused; }
	Json ExportLookLayer() const { return Util::interior && !paused ? Json{ { "Skylighting", { { "MinDiffuseVisibility", 2.f } } } } : Json::object(); }
};
struct Feature
{
	static std::vector<std::string> GetLoadedFeatureNames() { return { "Skin", "SubsurfaceScattering", "GrassLighting", "TerrainHelper" }; }
	Json settings = { { "MinDiffuseVisibility", 1.f } };
	std::string name;
	unsigned loads = 0, saves = 0;
	std::set<unsigned> failLoads;
	bool normalizeOnSave = false;
	bool loaded = true, throwNext = false;
	static Feature* FindFeatureByShortName(const std::string& name)
	{
		static std::map<std::string, Feature> registry;
		auto& feature = registry[name];
		feature.name = name;
		return &feature;
	}
	void SaveSettings(Json& value)
	{
		++saves;
		if (normalizeOnSave)
			settings["value"] = std::max(0, settings.at("value").get<int>());
		value = settings;
	}
	void SaveBaseSettings(Json& value)
	{
		value = settings;
		if (Util::interior && name == "Skylighting")
			value["MinDiffuseVisibility"] = 1.f;
		Presets::LookSceneRuntime::OverlayBase(name, value);
	}
	void LoadSettings(Json& value)
	{
		++loads;
		settings = value;
		if (throwNext || failLoads.contains(loads)) {
			throwNext = false;
			throw std::runtime_error("Simulated partial feature failure");
		}
	}
};
namespace logger
{
	template <class... Args>
	void error(Args&&...)
	{}
}
#include "look_scene_runtime_under_test.h"
void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}

#include "look_scene_apply_test.h"

void CheckPreparationGates()
{
	using namespace Presets::LookSceneRuntime;
	auto* feature = Feature::FindFeatureByShortName("Skylighting");
	Presets::LookPack pack;
	pack.scenes["TimeOfDay/Day"] = { { "Skylighting", { { "MinDiffuseVisibility", 3.f } } } };
	SceneSettingsManager::GetSingleton()->paused = true;
	Use(&pack);
	++globals::state->frameCount;
	Update();
	Require(!previousFields.at("Skylighting").flattened && fieldAddresses.empty(), "Paused features must not flatten or parse their fields");
	SceneSettingsManager::GetSingleton()->paused = false;
	WeatherManager::GetSingleton()->active = false;
	lastTick = std::chrono::steady_clock::now();
	++globals::state->frameCount;
	Update();
	Require(!previousFields.at("Skylighting").flattened && fieldAddresses.empty(), "Inactive weather reapplication must not prepare fields");
	WeatherManager::GetSingleton()->active = true;
	feature->loaded = false;
	lastTick = std::chrono::steady_clock::now();
	++globals::state->frameCount;
	Update();
	Require(!previousFields.at("Skylighting").flattened && fieldAddresses.empty(), "Unloaded features must not prepare fields");
	feature->loaded = true;
	lastTick = std::chrono::steady_clock::now();
	++globals::state->frameCount;
	Update();
	Require(previousFields.at("Skylighting").flattened && feature->settings["MinDiffuseVisibility"] == 3.f,
		"Eligible cached features must prepare and apply their retained values");
	Use(nullptr);
}

void CheckPreparedFields()
{
	using namespace Presets::LookSceneRuntime;
	Util::interior = false;
	auto* feature = Feature::FindFeatureByShortName("Skylighting");
	const Json authored = { { "MinDiffuseVisibility", 1.f }, { "a/b", { { "~value", 2.f } } },
		{ "vector", Json::array({ 3.f, 4.f }) }, { "untouched", true } };
	feature->settings = authored;
	Presets::LookPack pack;
	pack.scenes["TimeOfDay/Day"] = { { "Skylighting", { { "MinDiffuseVisibility", 3.f },
														  { "a/b", { { "~value", 6.f } } }, { "vector", Json::array({ 7.f, 8.f }) } } } };
	pack.scenes["TimeOfDay/Night"] = { { "Skylighting", { { "MinDiffuseVisibility", 5.f } } } };
	Use(&pack);
	++globals::state->frameCount;
	Update();
	Require(feature->settings["a/b"]["~value"] == 6.f && feature->settings["vector"][1] == 8.f,
		"Prepared fields must preserve JSON escaping and array indices");
	const auto* address = &FieldAddress("/a~1b/~0value");
	const auto addressCount = fieldAddresses.size();
	const auto loads = feature->loads;
	lastTick = std::chrono::steady_clock::now();
	++globals::state->frameCount;
	Update();
	Require(feature->loads == loads && &FieldAddress("/a~1b/~0value") == address && fieldAddresses.size() == addressCount,
		"Unchanged cached application must reuse field paths without reloading settings");
	feature->settings["a/b"]["~value"] = 20.f;
	feature->settings["vector"][0] = 30.f;
	feature->settings["untouched"] = false;
	lastTick = std::chrono::steady_clock::now();
	++globals::state->frameCount;
	Update();
	Require(feature->settings["a/b"]["~value"] == 6.f && feature->settings["vector"][0] == 7.f && !feature->settings["untouched"].get<bool>(),
		"Cached reapplication must restore controlled fields and preserve unrelated edits");
	Json exported = feature->settings;
	OverlayBase("Skylighting", exported);
	Require(exported["a/b"] == authored["a/b"] && exported["vector"] == authored["vector"],
		"Base exports must use authored values for prepared field paths");
	SceneSettingsManager::GetSingleton()->paused = true;
	lastTick = std::chrono::steady_clock::now();
	++globals::state->frameCount;
	Update();
	Require(feature->settings["a/b"] == authored["a/b"] && !Controls("Skylighting"), "Pausing must release cached ownership");
	SceneSettingsManager::GetSingleton()->paused = false;
	lastTick = std::chrono::steady_clock::now();
	++globals::state->frameCount;
	Update();
	Require(feature->settings["a/b"]["~value"] == 6.f && Controls("Skylighting", "vector"), "Resuming must reapply cached fields");
	globals::game::calendarStorage.hour.value = 22;
	lastTick = {};
	++globals::state->frameCount;
	Update();
	Require(feature->settings["MinDiffuseVisibility"] == 5.f && feature->settings["vector"] == authored["vector"] && !Controls("Skylighting", "vector"),
		"Context changes must replace prepared fields and release absent paths");
	Use(nullptr);
	Require(fieldAddresses.empty() && feature->settings["a/b"] == authored["a/b"], "Profile replacement must restore values before clearing parsed paths");
	globals::game::calendarStorage.hour.value = 12;
	feature->settings = authored;
	auto* water = Feature::FindFeatureByShortName("Water");
	water->settings = { { "Value", 10.f } };
	pack.scenes.clear();
	pack.scenes["TimeOfDay/Day"] = { { "Skylighting", { { "MinDiffuseVisibility", 3.f } } }, { "Water", { { "Value", 20.f } } } };
	Use(&pack);
	++globals::state->frameCount;
	Update();
	Require(feature->settings["MinDiffuseVisibility"] == 3.f && water->settings["Value"] == 20.f, "Multi-feature prepared application failed");
	WeatherManager::GetSingleton()->active = false;
	feature->settings["MinDiffuseVisibility"] = 7.f;
	lastTick = std::chrono::steady_clock::now();
	++globals::state->frameCount;
	Update();
	Require(feature->settings["MinDiffuseVisibility"] == 7.f, "Features without active weather variables must retain the cached-apply gate");
	WeatherManager::GetSingleton()->active = true;
	water->settings["Value"] = 50.f;
	water->throwNext = true;
	lastTick = std::chrono::steady_clock::now();
	++globals::state->frameCount;
	Update();
	Require(!Error().empty() && feature->settings == authored && water->settings["Value"] == 10.f && !Controls("Skylighting") && !Controls("Water"),
		"Cached partial failure must roll back all features and restore authored ownership");
	Use(nullptr);
}
void CheckLocationSkyLighting()
{
	using namespace Presets;
	using namespace Presets::LookSceneRuntime;
	std::string error;
	Require(LocationSkyLighting::Validate(Json::object(), error), "Empty sky overrides must inherit");
	Require(LocationSkyLighting::Validate({ { "MinShadowElevation", 45.f }, { "HorizonFadeHours", 0.f } }, error), "Inclusive sky bounds rejected");
	Json packAppearance{ { "locationOverrides", Json::array({ { { "key", "0x1~Test.esm" },
													{ "skyLighting", { { "MinShadowElevation", 4.f } } } } }) } };
	auto inherited = packAppearance;
	inherited["locationOverrides"][0]["skyLighting"] = Json::object();
	const auto savedEdits = Json::parse(PersonalSettings("AdaptiveBrightness", inherited, packAppearance).dump());
	Overlay(packAppearance, SelectDomain("AdaptiveBrightness", savedEdits, SettingsDomain::Appearance));
	Require(packAppearance == inherited, "Saving and reloading an inherit reset must not resurrect the pack's sky override");
	for (const auto& invalid : std::vector<Json>{ Json::array(), { { "MinShadowElevation", -1 } },
			 { { "MinShadowElevation", 46 } }, { { "HorizonFadeHours", 1.6 } }, { { "HorizonFadeHours", "1" } },
			 { { "SunPath", 1 } }, { { "MinShadowElevation", std::numeric_limits<double>::infinity() } } }) {
		Require(!LocationSkyLighting::Validate(invalid, error), "Invalid sky override accepted");
		const Json schema{ { "MinShadowElevation", 0.f }, { "HorizonFadeHours", 0.f } };
		for (const auto mode : { ValidationMode::ClosedPatch, ValidationMode::SettingsSnapshot })
			Require(!ValidatePatch(invalid, schema, error, "/AdaptiveBrightness/locationOverrides/0/skyLighting", mode),
				"Preset validation must reject invalid sky values before applying a profile");
	}

	const Json base{ { "SkySync", { { "MinShadowElevation", 1.f }, { "HorizonFadeHours", .7f } } } };
	LookSceneContext context;
	context.locations = { "parent", "child" };
	context.periods = { 0, 0, 1, 0, 0, 0 };
	std::map<std::string, Json> packLayers{
		{ "Locations/child/Day", { { "SkySync", { { "HorizonFadeHours", 1.f } } } } }
	};
	std::map<std::string, Json> personal{
		{ "parent", { { "SkySync", { { "MinShadowElevation", 3.f }, { "HorizonFadeHours", .2f } } } } },
		{ "child", { { "SkySync", { { "MinShadowElevation", 4.f } } } } }
	};
	auto resolved = ResolveLookLocations(packLayers, context, base, {}, personal);
	Require(resolved["SkySync"]["MinShadowElevation"] == 4.f && resolved["SkySync"]["HorizonFadeHours"] == 1.f,
		"Personal settings must respect narrower pack scopes and preserve periodic settings");
	personal.erase("child");
	resolved = ResolveLookLocations(packLayers, context, base, {}, personal);
	Require(resolved["SkySync"]["MinShadowElevation"] == 3.f, "Reset must inherit the broader location");

	Util::interior = false;
	WeatherManager::GetSingleton()->active = false;
	auto* sky = Feature::FindFeatureByShortName("SkySync");
	sky->settings = base.at("SkySync");
	Use(nullptr);
	SetLocationOverrides({ { "0x1~test.esm", { { "SkySync", { { "MinShadowElevation", 4.f } } } } } });
	++globals::state->frameCount;
	Update();
	Require(sky->settings["MinShadowElevation"] == 4.f && sky->settings["HorizonFadeHours"] == .7f,
		"Saved locations must work without a selected pack and preserve omitted settings");
	Json exported;
	sky->SaveBaseSettings(exported);
	Require(exported == base.at("SkySync"), "Transient local lighting must not leak into the exported base");
	const auto loads = sky->loads;
	++globals::state->frameCount;
	Update();
	Require(sky->loads == loads, "Unchanged location must not reload Sky Sync each frame");
	Suspend();
	Require(sky->settings == base.at("SkySync"), "Settings transaction must restore inherited sky values");
	Resume();
	++globals::state->frameCount;
	Update();
	Require(sky->settings["MinShadowElevation"] == 4.f, "Location must resume after the settings transaction");
	SetLocationOverrides({ { "0x2~test.esm", { { "SkySync", { { "MinShadowElevation", 4.f } } } } } });
	++globals::state->frameCount;
	Update();
	Require(sky->settings == base.at("SkySync"), "A nonmatching location must restore the inherited values");
	SetLocationOverrides({});
	++globals::state->frameCount;
	Update();
	Require(sky->settings == base.at("SkySync") && !Controls("SkySync"), "Disabling or deleting overrides must release ownership");
	Use(nullptr);
	WeatherManager::GetSingleton()->active = true;
}

int main()
try {
	using namespace Presets::LookSceneRuntime;
	Require(SceneSettingsManager::GetInteriorRelevantFeatureNames() == std::vector<std::string>{ "Skin", "SubsurfaceScattering" },
		"Interior settings offer Advanced Skin beside diffusion while excluding exterior-only features");
	auto* feature = Feature::FindFeatureByShortName("Skylighting");
	Presets::LookPack pack;
	pack.scenes["TimeOfDay/Day"] = { { "Skylighting", { { "MinDiffuseVisibility", 3.f } } } };
	Use(&pack);
	++globals::state->frameCount;
	Update();
	Require(feature->settings["MinDiffuseVisibility"] == 3.f, "native period applies");
	Json exported;
	feature->SaveBaseSettings(exported);
	Require(exported["MinDiffuseVisibility"] == 1.f, "exports exclude transient period value");
	Suspend();
	Suspend();
	Require(feature->settings["MinDiffuseVisibility"] == 1.f, "transaction restores authored settings");
	Resume();
	++globals::state->frameCount;
	Update();
	Require(feature->settings["MinDiffuseVisibility"] == 1.f, "nested transaction remains suspended");
	Resume();
	++globals::state->frameCount;
	Update();
	Require(feature->settings["MinDiffuseVisibility"] == 3.f, "scene ownership resumes");
	Suspend();
	SceneSettingsManager::GetSingleton()->paused = true;
	Resume();
	++globals::state->frameCount;
	Update();
	Require(feature->settings["MinDiffuseVisibility"] == 1.f && !Controls("Skylighting"), "pause releases scene controls");
	Suspend();
	SceneSettingsManager::GetSingleton()->paused = false;
	Resume();
	++globals::state->frameCount;
	Update();
	Require(feature->settings["MinDiffuseVisibility"] == 3.f, "scene controls resume after pause");
	globals::state->isMapMenuOpen = true;
	feature->settings["MinDiffuseVisibility"] = 7.f;
	++globals::state->frameCount;
	Update();
	Require(feature->settings["MinDiffuseVisibility"] == 7.f, "map freezes context evaluation");
	globals::state->isMapMenuOpen = false;
	++globals::state->frameCount;
	Update();
	Require(feature->settings["MinDiffuseVisibility"] == 3.f, "cached scene reasserts after weather writes");
	Use(nullptr);
	Require(feature->settings["MinDiffuseVisibility"] == 1.f, "clearing profile restores base");
	feature->throwNext = true;
	Use(&pack);
	++globals::state->frameCount;
	Update();
	Require(!Error().empty() && feature->settings["MinDiffuseVisibility"] == 1.f, "partial apply failure rolls back and reports");
	Use(nullptr);
	Require(Error().empty(), "new profile clears failed scene state");
	Util::interior = true;
	feature->settings["MinDiffuseVisibility"] = 2.f;
	pack.scenes.clear();
	pack.scenes["Locations/0x1~Test.esm"] = { { "Skylighting", { { "MinDiffuseVisibility", 4.f } } } };
	Use(&pack);
	++globals::state->frameCount;
	Update();
	feature->SaveBaseSettings(exported);
	Require(feature->settings["MinDiffuseVisibility"] == 4.f && exported["MinDiffuseVisibility"] == 1.f, "indoor place rules keep exterior authoring separate");
	Use(nullptr);
	Require(feature->settings["MinDiffuseVisibility"] == 2.f, "clearing place restores interior layer");
	CheckPreparedFields();
	CheckPreparationGates();
	LookSceneApplyTests::Run();
	CheckLocationSkyLighting();
	std::cout << "Visuals scene transaction and restoration tests passed\n";
	return 0;
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
