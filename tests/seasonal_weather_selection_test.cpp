#include "Features/SeasonalWeatherConfig.h"
#include "Utils/StringUtils.h"

#include <atomic>
#include <cassert>
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using json = nlohmann::json;
namespace testAllocation
{
	int outstanding = 0;
	bool failNext = false;
}
#define TES_HEAP_REDEFINE_NEW()                             \
	static void* operator new(std::size_t size)             \
	{                                                       \
		if (std::exchange(testAllocation::failNext, false)) \
			throw std::bad_alloc();                         \
		auto* memory = ::operator new(size);                \
		++testAllocation::outstanding;                      \
		return memory;                                      \
	}                                                       \
	static void operator delete(void* memory)               \
	{                                                       \
		--testAllocation::outstanding;                      \
		::operator delete(memory);                          \
	}
namespace RE::stl
{
	template <class T>
	using observer = T;
}
#include "seasonal_weather_native_list_under_test.h"
#undef TES_HEAP_REDEFINE_NEW

namespace RE
{
	using FormID = unsigned;
	struct TESForm
	{
		int id;
		template <class T = TESForm>
		static T* LookupByID(unsigned id);
	};
	struct TESWeather : TESForm
	{};
	struct WeatherType
	{
		TESWeather* weather;
		unsigned chance;
		void* global;
	};
	struct TESRegion : TESForm
	{};
}
namespace Util
{
	unsigned SpidToFormId(const std::string& key);
	std::string GetFormFileKey(const RE::TESForm* form);
}
using Category = SeasonalWeatherPolicy::Category;
Category Classify(const RE::TESWeather* weather) { return weather->id == 2 ? Category::Snow : Category::Clear; }
using WeatherList = RE::BSSimpleList<RE::WeatherType*>;
struct WeatherData
{
	WeatherList weatherTypes;
};

struct SeasonalWeather
{
	struct Plan
	{
		WeatherData* data;
		std::string reason;
		std::vector<RE::WeatherType> entries;
	};
	std::atomic_bool enabled{ true };
	std::atomic_bool faulted{ false };
	std::atomic_bool available{ true };
	std::atomic_uint64_t adjustedSelections{ 0 };
	std::mutex mutex;
	SeasonalWeatherConfig::Settings settings;
	std::unordered_map<std::string, unsigned> configuredForms;
	void Apply(const json& patch);
	std::function<Plan()> prepare;
	Plan BuildPlan(RE::TESRegion*) { return prepare(); }
	static inline RE::TESWeather* (*originalSelect)(RE::TESRegion*) = nullptr;
	static RE::TESWeather* SelectWeather(RE::TESRegion*);
};
namespace globals::features
{
	SeasonalWeather seasonalWeather;
}
namespace CSX::Api
{
	thread_local bool mainThread = true;
	bool IsRuntimeMainThread() { return mainThread; }
}
namespace logger
{
	template <class... Args>
	void error(const char*, Args&&...)
	{}
}

RE::FormID ResolveFormKey(const std::string& key);

#include "seasonal_weather_apply_under_test.h"
#include "seasonal_weather_list_under_test.h"
#include "seasonal_weather_selection_under_test.h"

namespace
{
	RE::TESWeather nativeWeather{ 1 }, snowWeather{ 2 };
	RE::TESRegion loadedRegion{ 3 };
	RE::WeatherType nativeEntry{ &nativeWeather, 100, &nativeWeather };
	RE::WeatherType zeroEntry{ &snowWeather, 0, nullptr };
	WeatherData data;
	RE::WeatherType** originalTail = nullptr;
	std::atomic_int calls{ 0 };
	thread_local bool expectReplacement = false;
	bool throwNative = false;
	bool recurse = false;
	std::function<void()> insideNative;
	void Require(bool condition)
	{
		if (!condition)
			throw std::logic_error("Seasonal selection ownership failed");
	}
	RE::TESWeather* Native(RE::TESRegion* region)
	{
		++calls;
		auto* entry = data.weatherTypes.front();
		Require(expectReplacement ? entry != &nativeEntry && entry->weather == &snowWeather && entry->global == nullptr : entry == &nativeEntry);
		if (expectReplacement)
			for (const auto* candidate : data.weatherTypes)
				Require(candidate->chance > 0);
		if (expectReplacement && insideNative)
			insideNative();
		if (recurse) {
			recurse = false;
			return SeasonalWeather::SelectWeather(region);
		}
		if (throwNative)
			throw std::runtime_error("native failure");
		return entry->weather;
	}
	void Restored()
	{
		Require(data.weatherTypes.front() == &nativeEntry);
		Require(nativeEntry.weather == &nativeWeather && nativeEntry.chance == 100 && nativeEntry.global == &nativeWeather);
		const auto tail = std::next(data.weatherTypes.begin());
		Require(*tail == &zeroEntry && std::addressof(*tail) == originalTail);
		Require(std::next(tail) == data.weatherTypes.end());
		Require(testAllocation::outstanding == 1);
	}
	SeasonalWeather::Plan MakePlan()
	{
		return { &data, "", { { &nativeWeather, 0, nullptr }, { &snowWeather, 50, nullptr }, { &nativeWeather, 50, nullptr } } };
	}
}

template <class T>
T* RE::TESForm::LookupByID(unsigned id)
{
	if constexpr (std::is_same_v<T, RE::TESRegion>)
		return id == 3 ? &loadedRegion : nullptr;
	else if constexpr (std::is_same_v<T, RE::TESForm>)
		return id == 3 ? static_cast<RE::TESForm*>(&loadedRegion) : LookupByID<RE::TESWeather>(id);
	else
		return id == 1 ? &nativeWeather : (id == 2 ? &snowWeather : nullptr);
}
unsigned Util::SpidToFormId(const std::string& key)
{
	if (key == "0x1~skyrim.esm")
		return 1;
	if (key == "0x2~skyrim.esm")
		return 2;
	if (key == "0x3~skyrim.esm")
		return 3;
	if (key == "0x1002~light.esl")
		return 2;
	return 0;
}

std::string Util::GetFormFileKey(const RE::TESForm* form)
{
	return "0x" + std::to_string(form->id) + "~Skyrim.esm";
}

int main()
{
	data.weatherTypes.push_front(&zeroEntry);
	data.weatherTypes.push_front(&nativeEntry);
	originalTail = std::addressof(*std::next(data.weatherTypes.begin()));
	auto& feature = globals::features::seasonalWeather;
	SeasonalWeather::originalSelect = &Native;
	int plans = 0;
	feature.prepare = [&] {
		++plans;
		return MakePlan();
	};
	feature.enabled = false;
	Require(SeasonalWeather::SelectWeather(nullptr) == &nativeWeather && calls == 1 && plans == 0);
	feature.enabled = true;
	CSX::Api::mainThread = false;
	Require(SeasonalWeather::SelectWeather(nullptr) == &nativeWeather && calls == 2 && plans == 0);
	CSX::Api::mainThread = true;
	expectReplacement = true;
	Require(SeasonalWeather::SelectWeather(nullptr) == &snowWeather && calls == 3 && plans == 1);
	Restored();
#ifndef DEVBENCH_BRIDGE_ENABLED
	Require(feature.adjustedSelections == 0);
#else
	Require(feature.adjustedSelections == 1);
#endif
	recurse = true;
	Require(SeasonalWeather::SelectWeather(nullptr) == &snowWeather && calls == 5 && plans == 2);
	Restored();
	throwNative = true;
	try {
		SeasonalWeather::SelectWeather(nullptr);
		Require(false);
	} catch (const std::runtime_error&) {}
	Require(calls == 6 && !feature.faulted);
	Restored();

	expectReplacement = false;
	feature.prepare = [&] { return SeasonalWeather::Plan{ &data, "weather_override", {} }; };
	try {
		SeasonalWeather::SelectWeather(nullptr);
		Require(false);
	} catch (const std::runtime_error&) {}
	Require(calls == 7 && !feature.faulted);
	Restored();
	throwNative = false;
	Require(SeasonalWeather::SelectWeather(nullptr) == &nativeWeather && calls == 8);
	feature.prepare = []() -> SeasonalWeather::Plan { throw std::runtime_error("preparation failure"); };
	Require(SeasonalWeather::SelectWeather(nullptr) == &nativeWeather && calls == 9 && feature.faulted);
	Require(SeasonalWeather::SelectWeather(nullptr) == &nativeWeather && calls == 10);
	Restored();

	feature.faulted = false;
	feature.prepare = MakePlan;
	testAllocation::failNext = true;
	Require(SeasonalWeather::SelectWeather(nullptr) == &nativeWeather && feature.faulted);
	Restored();
	feature.faulted = false;
	std::promise<void> enteredNative, releaseNative;
	auto entered = enteredNative.get_future();
	auto release = releaseNative.get_future();
	insideNative = [&] { enteredNative.set_value(); release.wait(); };
	auto owner = std::async(std::launch::async, [] {
		CSX::Api::mainThread = true;
		expectReplacement = true;
		return SeasonalWeather::SelectWeather(nullptr);
	});
	entered.wait();
	auto other = std::async(std::launch::async, [] {
		CSX::Api::mainThread = false;
		return SeasonalWeather::SelectWeather(nullptr);
	});
	const bool serialized = other.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout;
	releaseNative.set_value();
	Require(owner.get() == &snowWeather && other.get() == &nativeWeather && serialized);
	Restored();

	feature.settings = SeasonalWeatherConfig::Parse({ { "Enabled", true },
		{ "SnowAssignments", { { { "Region", "0x3~Missing.esp" }, { "Weather", "0x2~Missing.esp" }, { "Weight", 20 } } } },
		{ "WeatherCategories", { { { "Weather", "0x2~Missing.esp" }, { "Category", 3 } } } } });
	feature.faulted = true;
	feature.Apply({ { "Enabled", false } });
	Require(!feature.enabled && !feature.settings.Enabled);
	feature.Apply({ { "SkipGlobalControlledRegions", false } });
	Require(!feature.settings.SkipGlobalControlledRegions);
	const auto retained = SeasonalWeatherConfig::Serialize(feature.settings);
	try {
		feature.Apply({ { "Enabled", true } });
		Require(false);
	} catch (const std::invalid_argument&) {}
	Require(SeasonalWeatherConfig::Serialize(feature.settings) == retained);
	feature.Apply({ { "SnowAssignments", json::array() }, { "WeatherCategories", json::array() } });
	Require(feature.settings.SnowAssignments.empty() && feature.settings.WeatherCategories.empty());
	feature.faulted = false;
	const auto valid = json{ { "Enabled", true }, { "SnowAssignments", { { { "Region", "0x3~Skyrim.esm" }, { "Weather", "0x2~Skyrim.esm" }, { "Weight", 20 } } } } };
	feature.Apply(valid);
	Require(feature.enabled && feature.settings.SnowAssignments.size() == 1);
	auto bad = valid;
	bad["SnowAssignments"][0]["Weather"] = "0x1~Skyrim.esm";
	try {
		feature.Apply(bad);
		Require(false);
	} catch (const std::invalid_argument&) {}
	Require(feature.settings.SnowAssignments.front().Weather == "0x2~skyrim.esm");
	bad["SnowAssignments"][0]["Weather"] = "0x1002~Light.esl";
	try {
		feature.Apply(bad);
		Require(false);
	} catch (const std::invalid_argument&) {}
	Require(feature.settings.SnowAssignments.front().Weather == "0x2~skyrim.esm");
	data.weatherTypes.clear();
	Require(testAllocation::outstanding == 0);
}
