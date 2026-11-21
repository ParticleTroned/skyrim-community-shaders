#include "Presets/LookSceneRuntime.h"
#include "Presets/LookScenes.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
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
	bool HasActiveVariablesForFeature(const std::string&) const { return true; }
};
struct SceneSettingsManager
{
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
	Json settings = { { "MinDiffuseVisibility", 1.f } };
	bool loaded = true, throwNext = false;
	static Feature* FindFeatureByShortName(const std::string&)
	{
		static Feature feature;
		return &feature;
	}
	void SaveSettings(Json& value) { value = settings; }
	void SaveBaseSettings(Json& value)
	{
		value = settings;
		if (Util::interior)
			value["MinDiffuseVisibility"] = 1.f;
		Presets::LookSceneRuntime::OverlayBase("Skylighting", value);
	}
	void LoadSettings(Json& value)
	{
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
int main()
try {
	using namespace Presets::LookSceneRuntime;
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
	std::cout << "Visuals scene transaction and restoration tests passed\n";
	return 0;
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
