#include <algorithm>
#include <filesystem>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

namespace RE
{
	struct TESWeather
	{
		enum class WeatherDataFlag
		{
			kPleasant = 1,
			kCloudy = 2,
			kRainy = 4,
			kSnow = 8,
			kPermAurora = 16,
			kAuroraFollowsSun = 32
		};
		struct Flags
		{
			uint32_t value;
			uint32_t underlying() const { return value; }
		};
		struct Data
		{
			Flags flags;
		} data;
		std::string key;
	};
}
#include "Utils/WeatherFilter.h"

struct Feature
{
	std::string GetShortName() const { return "AdaptiveBrightness"; }
	void NormalizeWeatherSettings(json&) {}
};
namespace globals::features
{
	Feature adaptiveBrightness;
}

namespace WeatherVariables
{
	struct IWeatherVariable
	{
		std::string GetName() const { return "brightness"; }
		bool IsValidJsonValue(const json& value) const { return value.is_number() && value >= 0.01f && value <= 4.0f; }
	};
	struct Registry
	{
		std::vector<std::shared_ptr<IWeatherVariable>> variables{ std::make_shared<IWeatherVariable>() };
		const auto& GetVariables() const { return variables; }
	};
	struct GlobalWeatherRegistry
	{
		Registry registry;
		static GlobalWeatherRegistry* GetSingleton()
		{
			static GlobalWeatherRegistry value;
			return &value;
		}
		Registry* GetFeatureRegistry(const std::string&) { return &registry; }
	};
}

std::string failingStage;
bool failingRefresh = false;

struct WeatherManager
{
	using Settings = std::map<std::string, json>;
	std::map<std::string, Settings> live, saved;
	int refreshes = 0;
	static WeatherManager* GetSingleton()
	{
		static WeatherManager value;
		return &value;
	}
	static std::string GetWeatherKey(RE::TESWeather* weather) { return weather->key; }
	void GetAllSettingsForWeather(RE::TESWeather* weather, Settings& out) { out = live[weather->key]; }
	void StageSettingsForWeather(RE::TESWeather* weather, const Settings& value)
	{
		if (weather->key == failingStage)
			throw std::runtime_error("injected apply failure");
		live[weather->key] = value;
	}
	void CommitSettingsForWeather(RE::TESWeather* weather, const Settings& value) { saved[weather->key] = value; }
	void RefreshFeatureOverrides()
	{
		++refreshes;
		if (failingRefresh)
			throw std::runtime_error("injected refresh failure");
	}
};

std::map<std::string, std::string> files;
std::string failingFile;
int writes = 0;
namespace Util
{
	std::string GetFormFileKey(RE::TESWeather* weather) { return weather->key; }
	namespace PathHelpers
	{
		std::filesystem::path GetCommunityShaderPath() { return "test-data"; }
	}
	namespace FileHelpers
	{
		enum class JsonFileReadResult
		{
			NotFound,
			Success,
			Failed
		};
		JsonFileReadResult ReadJsonFile(const std::filesystem::path& path, json& value, std::string& error)
		{
			const auto it = files.find(path.filename().string());
			if (it == files.end())
				return JsonFileReadResult::NotFound;
			value = json::parse(it->second, nullptr, false);
			if (value.is_discarded()) {
				error = "invalid JSON";
				return JsonFileReadResult::Failed;
			}
			return JsonFileReadResult::Success;
		}
		void EnsureDirectoryExists(const std::filesystem::path&) {}
		bool WriteTextFileAtomic(const std::filesystem::path& path, const std::string& value, std::string& error, bool allowDirectFallback = true)
		{
			if (path.filename() == failingFile) {
				if (allowDirectFallback)
					files[path.filename().string()] = "truncated by unsafe direct fallback";
				error = "injected save failure";
				return false;
			}
			files[path.filename().string()] = value;
			++writes;
			return true;
		}
	}
}

struct Widget
{
	virtual ~Widget() = default;
};
struct WeatherWidget : Widget
{
	RE::TESWeather* weather;
	struct Settings
	{
		WeatherManager::Settings featureSettings;
		int native = 17;
	} settings, originalSettings;
	std::string GetSaveKey() const { return weather->key; }
};
struct EditorWindow
{
	std::vector<std::unique_ptr<Widget>> weatherWidgets;
	static EditorWindow* GetSingleton()
	{
		static EditorWindow value;
		return &value;
	}
	void EnsureResources() { throw std::runtime_error("Bulk apply must not populate the editor"); }
	void InvalidateJsonAttachmentCache(WeatherWidget*) {}
	void RecordWeatherFeatureSave(WeatherWidget&, const std::string&, const json&);
};
struct WeatherPicker
{
	static inline std::vector<RE::TESWeather*> catalog;
	static std::span<RE::TESWeather* const> GetWeatherCatalog() { return catalog; }
};
bool blocked = false;
namespace MenuUI
{
	const char* GetFeatureMutationBlockReason(Feature*) { return blocked ? "blocked" : nullptr; }
}

template <class Task>
json RunOnMainThread(Task task)
{
	return task();
}

#include "visuals_weather_under_test.h"

void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

int main()
{
	using Util::WeatherFilter::Matches;
	Require(Matches(4, 4) && Matches(6, 4) && !Matches(8, 4), "Rain matching lost multi-flag semantics");
	Require(Matches(0, 64) && Matches(128, 64) && !Matches(132, 64), "None must include unknown-only flags");
	Require(Matches(128, 127) && !Matches(4, 0) && Matches(8, 12), "All, empty and combined filters");
	for (const auto& category : Util::WeatherFilter::Classified)
		Require(Matches(static_cast<uint32_t>(category.flag), static_cast<uint32_t>(category.flag)), "Category did not match itself");

	RE::TESWeather rain{ { { 4 } }, "rain" }, mixed{ { { 6 } }, "mixed" }, snow{ { { 8 } }, "snow" };
	WeatherPicker::catalog = { &rain, &mixed, &snow };
	const json settings = { { "__enabled", true }, { "brightness", 1.5f } };
	auto reset = [&] {
		files.clear();
		failingFile.clear();
		failingStage.clear();
		failingRefresh = false;
		writes = 0;
		blocked = false;
		*WeatherManager::GetSingleton() = {};
		auto& widgets = EditorWindow::GetSingleton()->weatherWidgets;
		widgets.clear();
		for (auto* weather : WeatherPicker::catalog) {
			auto widget = std::make_unique<WeatherWidget>();
			widget->weather = weather;
			widget->settings.featureSettings = { { "AdaptiveBrightness", settings }, { "Other", { { "draft", 3 } } } };
			widget->originalSettings.featureSettings = { { "Other", { { "saved", 4 } } } };
			widgets.push_back(std::move(widget));
			files[weather->key + ".json"] = json{ { "clouds", { 1, 2 } }, { "featureSettings", { { "Other", { { "disk", 5 } } } } } }.dump();
			WeatherManager::GetSingleton()->live[weather->key] = { { "Other", { { "live", 6 } } } };
		}
	};
	reset();
	auto apply = [&](bool all, const json* value = nullptr) { return MenuUI::ApplyVisualsWeather("rain", 4, all, value); };
	auto result = apply(true);
	Require(result["ok"] && result["savedWeathers"] == 2 && writes == 2, "Category copy did not save exactly the rain matches");
	Require(!json::parse(files["snow.json"])["featureSettings"].contains("AdaptiveBrightness"), "Category copy touched snow");
	Require(json::parse(files["mixed.json"])["clouds"] == json({ 1, 2 }), "Native fields were replaced");
	Require(json::parse(files["mixed.json"])["featureSettings"]["Other"]["disk"] == 5, "Other persisted features were replaced");
	auto* target = static_cast<WeatherWidget*>(EditorWindow::GetSingleton()->weatherWidgets[1].get());
	Require(target->settings.featureSettings["Other"]["draft"] == 3 && target->originalSettings.featureSettings["Other"]["saved"] == 4 && target->settings.native == 17, "Unrelated editor changes were replaced");
	Require(WeatherManager::GetSingleton()->live["mixed"]["Other"]["live"] == 6 && WeatherManager::GetSingleton()->refreshes == 1, "Bulk apply lost live settings or repeatedly refreshed");
	Require(apply(true)["ok"] && writes == 2, "Unchanged copies rewrote files");

	reset();
	result = apply(false);
	Require(result["ok"] && result["savedWeathers"] == 1 && writes == 1, "Selected-only apply changed another entry");
	reset();
	blocked = true;
	Require(!apply(true)["ok"] && writes == 0, "Persistence guard bypassed");
	reset();
	Require(!MenuUI::ApplyVisualsWeather("rain", 8, true, nullptr)["ok"] && writes == 0, "Stale hidden selection accepted");
	Require(!MenuUI::ApplyVisualsWeather("rain", 128, true, nullptr)["ok"] && writes == 0, "Unknown filter accepted");
	Require(!MenuUI::ApplyVisualsWeather("missing", 4, true, nullptr)["ok"] && writes == 0, "Unknown source accepted");
	for (const json invalid : { json{ { "__enabled", "yes" } }, json{ { "__enabled", true }, { "brightness", 9 } }, json{ { "__enabled", true }, { "unknown", 1 } } }) {
		Require(!apply(true, &invalid)["ok"] && writes == 0, "Invalid source modified targets");
	}
	reset();
	const json disabled = { { "__enabled", false } };
	Require(apply(true, &disabled)["ok"] && json::parse(files["mixed.json"])["featureSettings"]["AdaptiveBrightness"] == disabled, "Disabled sparse settings not preserved");
	reset();
	files["rain.json"] = "{broken";
	Require(!apply(true)["ok"] && writes == 0 && files["rain.json"] == "{broken", "Malformed attachment was overwritten");
	reset();
	files["rain.json"] = json{ { "featureSettings", 12 } }.dump();
	Require(!apply(true)["ok"] && writes == 0, "Malformed feature root was overwritten");
	reset();
	files.erase("rain.json");
	Require(apply(false)["ok"] && writes == 1, "Missing attachment could not be created");
	reset();
	EditorWindow::GetSingleton()->weatherWidgets.erase(EditorWindow::GetSingleton()->weatherWidgets.begin() + 1);
	Require(apply(true)["ok"] && writes == 2, "A missing editor widget prevented a catalog-matched update");
	reset();
	auto* source = static_cast<WeatherWidget*>(EditorWindow::GetSingleton()->weatherWidgets.front().get());
	source->settings.featureSettings.erase("AdaptiveBrightness");
	Require(!apply(true)["ok"] && writes == 0, "Absent source adjustments were copied");
	source->settings.featureSettings["AdaptiveBrightness"] = { { "__enabled", true }, { "brightness", 2.0f } };
	Require(apply(false)["ok"] && WeatherManager::GetSingleton()->live["rain"]["AdaptiveBrightness"]["brightness"] == 2.0f, "Unapplied source draft was not applied");

	reset();
	const auto previous = files["mixed.json"];
	failingFile = "mixed.json";
	result = apply(true);
	Require(!result["ok"] && result["savedWeathers"] == 1 && result["matchedWeathers"] == 2 && result["failedWeather"] == "mixed", "Partial failure was reported as success");
	Require(files["mixed.json"] == previous && !WeatherManager::GetSingleton()->live["mixed"].contains("AdaptiveBrightness"), "Failed write changed target state");

	reset();
	failingStage = "mixed";
	result = apply(true);
	Require(!result["ok"] && result["savedWeathers"] == 2 && result["appliedWeathers"] == 1 && result["failedWeather"] == "mixed", "Post-save apply failure lost accurate counts");
	reset();
	failingRefresh = true;
	result = apply(true);
	Require(!result["ok"] && result["savedWeathers"] == 2 && result["appliedWeathers"] == 2 && !result["weatherRefreshed"].get<bool>(), "Refresh failure escaped or was reported as success");
	reset();
	EditorWindow::GetSingleton()->weatherWidgets.clear();
	Require(apply(true, &settings)["ok"] && writes == 2, "Automation required creating unrelated editor widgets");
	reset();
	EditorWindow::GetSingleton()->weatherWidgets.clear();
	files["rain.json"] = json{ { "featureSettings", { { "AdaptiveBrightness", settings } } } }.dump();
	Require(apply(true)["ok"] && writes == 1, "Unopened saved source did not apply to its category");

	reset();
	auto request = json::parse(R"({"action":"apply_visuals_weather","weatherKey":"rain","weatherFilter":4,"allMatches":false})");
	Require(Dispatch(request)["ok"] && writes == 1, "DevBench did not dispatch selected-only application");
	reset();
	for (const auto invalidFilter : { 0ULL, 128ULL, 4294967300ULL }) {
		request["weatherFilter"] = invalidFilter;
		Require(!Dispatch(request)["ok"] && writes == 0, "DevBench truncated or accepted an invalid filter");
	}
	request["weatherFilter"] = 4u;
	request["weatherSettings"] = nullptr;
	Require(!Dispatch(request)["ok"] && writes == 0, "Null DevBench settings silently selected an editor draft");
	request["weatherSettings"] = settings;
	request.erase("allMatches");
	Require(!Dispatch(request)["ok"] && writes == 0, "Missing DevBench target scope was accepted");
	request["allMatches"] = true;
	Require(Dispatch(request)["ok"] && writes == 2, "DevBench did not dispatch bulk application");

	const auto schema = WeatherSchema();
	const auto& outputProperties = schema["outputSchema"]["properties"];
	Require(!outputProperties.contains("saved") && !outputProperties.contains("applied"), "Weather count schemas constrained existing boolean action results");
	for (const auto* count : { "matchedWeathers", "savedWeathers", "appliedWeathers" })
		Require(outputProperties[count]["type"] == "integer", "Weather counts are missing from the registered schema");
	Require(outputProperties["weatherRefreshed"]["type"] == "boolean", "Weather refresh status is missing from the registered schema");

	reset();
	std::map<std::string, json> persisted;
	bool changed = false;
	std::string error;
	Require(WeatherFeatureOverrides::Save(&rain, "Other", nullptr, persisted, changed, error) && changed && persisted.empty(), "Shared API removal regressed");
	std::cout << "Weather categories, selected/bulk scope, validation, preservation and save failures passed\n";
}
