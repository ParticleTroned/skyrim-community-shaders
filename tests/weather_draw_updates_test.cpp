#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <source_location>
#include <string>
#include <vector>

struct float3
{
	float x{}, y{}, z{};
};
struct float4
{
	float x{}, y{}, z{}, w{};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(float3, x, y, z)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(float4, x, y, z, w)

namespace logger
{
	template <class... Args>
	void debug(Args&&...)
	{}
	template <class... Args>
	void info(Args&&...)
	{}
	template <class... Args>
	void warn(Args&&...)
	{}
}
namespace RE
{
	struct TESWeather
	{
		std::string key;
	};
	struct Sky
	{
		TESWeather* currentWeather{};
		TESWeather* lastWeather{};
		float currentWeatherPct{ 1.0f };
		static inline Sky* singleton{};
		static inline unsigned lookups{};
		static Sky* GetSingleton()
		{
			++lookups;
			return singleton;
		}
	};
	struct TESWorldSpace
	{
		enum class Flag
		{
			kNoSky = 1,
			kFixedDimensions = 2
		};
		struct Flags
		{
			unsigned bits{};
			bool any(Flag a, Flag b) const { return (bits & (unsigned(a) | unsigned(b))) != 0; }
		} flags;
	};
	struct TES
	{
		void* interiorCell{};
		struct Runtime
		{
			TESWorldSpace* worldSpace{};
		} runtime;
		Runtime& GetRuntimeData2() { return runtime; }
		static inline TES* singleton{};
		static inline unsigned lookups{};
		static TES* GetSingleton()
		{
			++lookups;
			return singleton;
		}
	};
}
struct State
{
	uint32_t frameCount{};
	bool isMapMenuOpen{};
	bool HasDirectionalShadows() const;
};
namespace globals
{
	State storage;
	State* state = &storage;
	namespace game
	{
		RE::Sky* sky{};
		RE::TES* tes{};
	}
	namespace features
	{
		struct InteriorSun
		{
			bool active{};
			bool IsActiveInteriorSun() const { return active; }
		} interiorSun;
	}
}
struct Feature
{
	bool loaded{ true };
	std::string GetShortName() const { return "Test"; }
	void NormalizeWeatherSettings(nlohmann::json&) {}
	static Feature* FindFeatureByShortName(const std::string&)
	{
		static Feature feature;
		return &feature;
	}
	static std::vector<Feature*> GetFeatureList() { return { FindFeatureByShortName("Test") }; }
};
namespace Util
{
	std::string GetFormFileKey(RE::TESWeather* weather) { return weather->key; }
}

#include "WeatherManager.h"

namespace
{
	unsigned evaluations{}, queries{};
	bool failEvaluation{}, diskReadFails{ true };
	struct EvaluationFailure
	{};
	std::map<std::string, std::map<std::string, json>> diskSettings;
}

#include "weather_manager_under_test.h"

namespace
{
	RE::TESWeather weatherA{ "A" }, weatherB{ "B" }, weatherC{ "C" };
	RE::Sky sky;
	float featureValue{ 10.0f };
	auto* manager = WeatherManager::GetSingleton();
	auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();
	void Check(bool condition, std::source_location at = std::source_location::current())
	{
		if (!condition) {
			std::cerr << "weather/scene assertion at line " << at.line() << '\n';
			std::abort();
		}
	}
	std::map<std::string, json> Settings(float amount)
	{
		return { { "Test", { { "__enabled", true }, { "Value", amount } } } };
	}
	void Reset()
	{
		failEvaluation = false;
		diskReadFails = true;
		diskSettings.clear();
		manager->ClearCache();
		globals::storage = {};
		sky = { &weatherA, nullptr, 1.0f };
		globals::game::sky = RE::Sky::singleton = &sky;
		featureValue = 10.0f;
		registry->SetFeaturePaused("Test", false);
		registry->GetOrCreateFeatureRegistry("Test")->RegisterVariable(std::make_shared<WeatherVariables::FloatVariable>(
			"Value", "Value", "", &featureValue, 10.0f, 0.0f, 100.0f));
		manager->StageSettingsForWeather(&weatherA, Settings(20.0f));
		manager->StageSettingsForWeather(&weatherB, Settings(80.0f));
		manager->StageSettingsForWeather(&weatherC, Settings(40.0f));
		evaluations = queries = RE::Sky::lookups = 0;
	}
	void CadenceAndSkyLifetime()
	{
		Reset();
		for (unsigned i = 0; i < 10000; ++i) manager->UpdateFeatures();
		Check(evaluations == 1 && queries == 1 && RE::Sky::lookups == 0 && featureValue == 20.0f);
		for (auto frame : { 1U, UINT32_MAX, 0U }) {
			globals::state->frameCount = frame;
			const auto before = evaluations;
			for (unsigned i = 0; i < 100; ++i) manager->UpdateFeatures();
			Check(evaluations == before + 1);
		}
		RE::Sky replacement = sky;
		globals::game::sky = &replacement;
		manager->UpdateFeatures();
		Check(evaluations == 5 && featureValue == 20.0f);
		globals::game::sky = nullptr;
		RE::Sky::singleton = nullptr;
		manager->UpdateFeatures();
		Check(evaluations == 6 && featureValue == 10.0f);
		RE::Sky::singleton = &sky;
		manager->UpdateFeatures();
		Check(evaluations == 7 && featureValue == 20.0f && RE::Sky::lookups != 0);
		globals::game::sky = &sky;
		for (auto factor : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1.0f, 2.0f }) {
			sky.currentWeatherPct = factor;
			const auto before = evaluations;
			for (unsigned i = 0; i < 100; ++i) manager->UpdateFeatures();
			Check(evaluations == before + 1 && std::isfinite(featureValue));
		}
	}
	void LiveWeatherChanges()
	{
		Reset();
		manager->UpdateFeatures();
		sky.currentWeather = &weatherB;
		manager->UpdateFeatures();
		Check(featureValue == 80.0f && evaluations == 2);
		sky.lastWeather = &weatherA;
		sky.currentWeatherPct = 0.5f;
		manager->UpdateFeatures();
		Check(featureValue == 50.0f);
		for (auto factor : { 0.5004f, 0.5008f }) {
			sky.currentWeatherPct = factor;
			manager->UpdateFeatures();
			Check(featureValue == 50.0f);
		}
		sky.currentWeatherPct = 0.5012f;
		manager->UpdateFeatures();
		Check(std::abs(featureValue - 50.072f) < 0.001f);
		sky.lastWeather = nullptr;
		manager->UpdateFeatures();
		Check(manager->GetCurrentWeathers().lastWeather == &weatherA);
		const auto before = evaluations;
		// An external read can remember a new source before native code clears lastWeather again.
		sky.lastWeather = &weatherC;
		Check(manager->GetCurrentWeathers().lastWeather == &weatherC);
		sky.lastWeather = nullptr;
		manager->UpdateFeatures();
		Check(evaluations == before + 1 && std::abs(featureValue - 60.048f) < 0.001f);
		sky.currentWeatherPct = 0.9995f;
		manager->UpdateFeatures();
		sky.currentWeatherPct = 1.0f;
		manager->UpdateFeatures();
		Check(featureValue == 80.0f && manager->GetCurrentWeathers().lastWeather == nullptr);
	}
	void EditsMenusAndReset()
	{
		Reset();
		manager->UpdateFeatures();
		featureValue = 33.0f;
		registry->CaptureFeatureUserSettings("Test");
		manager->NotifyUserSettingsChanged();
		manager->UpdateFeatures();
		Check(featureValue == 20.0f && evaluations == 2);
		manager->StageSettingsForWeather(&weatherA, Settings(37.0f));
		manager->RefreshFeatureOverrides();
		Check(featureValue == 37.0f && evaluations == 3);
		manager->SetFeaturePaused("Test", true);
		Check(featureValue == 33.0f);
		manager->SetFeaturePaused("Test", false);
		Check(featureValue == 37.0f);
		manager->UpdateFeatures();
		const auto before = evaluations;
		manager->ClearCache();
		Check(featureValue == 33.0f);
		manager->UpdateFeatures();
		Check(evaluations == before + 1 && featureValue == 37.0f);
		diskReadFails = false;
		diskSettings[weatherA.key] = Settings(55.0f);
		manager->LoadPerWeatherSettingsFromDisk();
		manager->UpdateFeatures();
		Check(featureValue == 55.0f);
		globals::state->isMapMenuOpen = true;
		sky.currentWeather = &weatherB;
		manager->UpdateFeatures();
		Check(featureValue == 55.0f);
		globals::state->isMapMenuOpen = false;
		manager->UpdateFeatures();
		Check(featureValue == 33.0f);
		globals::state->isMapMenuOpen = true;
		sky.currentWeather = &weatherA;
		manager->RefreshFeatureOverrides();
		Check(featureValue == 55.0f);
	}
	void ForcedRefreshReversal()
	{
		Reset();
		manager->UpdateFeatures();
		sky.currentWeather = &weatherB;
		manager->RefreshFeatureOverrides();
		Check(featureValue == 80.0f);
		sky.currentWeather = &weatherA;
		manager->UpdateFeatures();
		Check(featureValue == 20.0f && evaluations == 3);
	}
	void FailedEvaluationRetries()
	{
		Reset();
		manager->UpdateFeatures();
		++globals::state->frameCount;
		failEvaluation = true;
		bool failed{};
		try {
			manager->UpdateFeatures();
		} catch (const EvaluationFailure&) {
			failed = true;
		}
		Check(failed && evaluations == 2);
		failEvaluation = false;
		manager->UpdateFeatures();
		Check(evaluations == 3 && featureValue == 20.0f);
		manager->UpdateFeatures();
		Check(evaluations == 3);
	}
	void LiveSceneQueries()
	{
		RE::TES tes;
		RE::TESWorldSpace world;
		globals::game::tes = &tes;
		RE::TES::singleton = nullptr;
		tes.runtime.worldSpace = &world;
		for (unsigned i = 0; i < 10000; ++i) Check(globals::state->HasDirectionalShadows());
		Check(RE::TES::lookups == 0);
		for (unsigned flags : { 1U, 2U, 3U, 0U }) {
			world.flags.bits = flags;
			Check(Util::IsInterior() == (flags != 0));
		}
		tes.interiorCell = &tes;
		Check(!globals::state->HasDirectionalShadows());
		globals::features::interiorSun.active = true;
		Check(globals::state->HasDirectionalShadows());
		globals::features::interiorSun.active = false;
		tes.interiorCell = nullptr;
		tes.runtime.worldSpace = nullptr;
		Check(Util::IsInterior());
		globals::game::tes = nullptr;
		Check(Util::IsInterior() && RE::TES::lookups == 1);
		RE::TES::singleton = &tes;
		tes.runtime.worldSpace = &world;
		Check(!Util::IsInterior() && RE::TES::lookups == 2);
		globals::game::tes = RE::TES::singleton = nullptr;
	}
}

int main()
{
	CadenceAndSkyLifetime();
	LiveWeatherChanges();
	EditsMenusAndReset();
	ForcedRefreshReversal();
	FailedEvaluationRetries();
	LiveSceneQueries();
	manager->ClearCache();
	std::cout << "Weather draw reuse, same-frame mutations, transitions, reset/retry and live scene queries passed\n";
}
