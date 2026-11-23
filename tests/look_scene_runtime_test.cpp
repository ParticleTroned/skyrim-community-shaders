#include "Presets/LookSceneRuntime.h"
#include "Presets/LookScenes.h"
#include <algorithm>
#include <chrono>
#include <iostream>
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
	unsigned loads = 0;
	bool loaded = true, throwNext = false;
	static Feature* FindFeatureByShortName(const std::string& name)
	{
		static std::map<std::string, Feature> registry;
		auto& feature = registry[name];
		feature.name = name;
		return &feature;
	}
	void SaveSettings(Json& value) { value = settings; }
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
		if (throwNext) {
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
	std::cout << "Visuals scene transaction and restoration tests passed\n";
	return 0;
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
