#pragma once

namespace FeatureSettingsSaveTest
{
	using json = nlohmann::json;
	struct Feature
	{
		std::string name;
		bool loaded = true;
		int notified = 0;
		json settings;
		static inline std::vector<Feature*> features;
		static const auto& GetFeatureList() { return features; }
		const std::string& GetName() const { return name; }
		const std::string& GetShortName() const { return name; }
		const std::string& GetDisplayName() const { return name; }
		bool HasFeatureSettings() const { return true; }
		void Save(json& root) { root[name] = settings; }
		void SaveSettings(json& value) { value = settings; }
		void OnSettingsSaved() { ++notified; }
	};
	namespace globals
	{
		struct Menu
		{
			int resets = 0;
			std::string accepted;
			std::string message;
			bool PrepareSettingsMutation() { return true; }
			void ReportSettingsSaveResult(bool, std::string value) { message = std::move(value); }
			void AcceptCurrentFeatureSettingsAsClean(const std::string& name) { accepted = name; }
			void CaptureFeatureMenuSettings(const std::string&, json&) const {}
			void ResetSettingsDirtyState() { ++resets; }
		} storage;
		auto* menu = &storage;
	}
	struct PerformanceTuningRenderer
	{
		static bool HasActiveMeasurements() { return false; }
	};
	struct WeatherManager
	{
		static WeatherManager* GetSingleton()
		{
			static WeatherManager manager;
			return &manager;
		}
		int GetActiveVariablesForFeature(const std::string&) { return 0; }
	};
	namespace WeatherVariables
	{
		struct GlobalWeatherRegistry
		{
			static GlobalWeatherRegistry* GetSingleton()
			{
				static GlobalWeatherRegistry registry;
				return &registry;
			}
			void SerializeFeatureUserSettings(const std::string&, int, const std::function<void()>& save) { save(); }
		};
	}
	namespace PresetCompatibility
	{
		struct Evaluation
		{
			std::string message;
			bool ShouldApply() const { return true; }
		};
		Evaluation GetPublished() { return {}; }
	}
	namespace Util::FileHelpers
	{
		enum class JsonFileReadResult
		{
			Success,
			NotFound,
			Error
		};
		json file;
		JsonFileReadResult result = JsonFileReadResult::Success;
		JsonFileReadResult ReadJsonFile(const std::filesystem::path&, json& value, std::string& error)
		{
			value = file;
			error = "Cannot read settings";
			return result;
		}
	}
	namespace SettingsSerialization
	{
		int writes = 0;
		bool fail = false;
		bool WriteFileAtomic(const std::filesystem::path&, const json& value, std::string& error)
		{
			++writes;
			if (fail) {
				error = "Cannot write settings";
				return false;
			}
			Util::FileHelpers::file = value;
			return true;
		}
	}
	Feature* overrideFeature = nullptr;
	bool overrideFailure = false;
	std::string globalSection;
	namespace SettingsMigrations
	{
		using ::SettingsMigrations::MigrateEditorSettingsRootLayer;
		constexpr std::string_view kAdaptiveBalanceFeatureName = "AdaptiveBrightness";
	}
	struct SettingsOverrideManager
	{
		static SettingsOverrideManager* GetSingleton()
		{
			static SettingsOverrideManager manager;
			return &manager;
		}
		bool IsEnabled() const { return true; }
		bool HasFeatureOverrides(const std::string&) const { return true; }
		bool HasUserOverride(const std::string&) const { return true; }
		json GetMergedOverrideSettings(const std::string&, const json&) { return json::object(); }
		bool SaveUserOverride(const std::string& name, const json&, const json&, const std::string& section = {})
		{
			if (name == "Global")
				globalSection = section;
			else
				for (auto* feature : Feature::features)
					if (feature->name == name)
						overrideFeature = feature;
			return !overrideFailure;
		}
	};
	std::string JoinSettingLayerNames(const std::vector<std::string>& names) { return names.empty() ? "" : names[0]; }
	namespace logger
	{
		template <class... T>
		void warn(T&&...)
		{}
		template <class... T>
		void info(T&&...)
		{}
	}
	struct State
	{
		enum class ConfigMode
		{
			USER,
			DEFAULT
		};
		bool blocked = false;
		bool Save(ConfigMode mode = ConfigMode::USER, Feature* feature = nullptr);
		bool IsPersistentMutationBlocked() const { return blocked; }
		std::filesystem::path GetConfigPath(ConfigMode) { return "SettingsUser.json"; }
		void SaveToJson(json& values, bool)
		{
			for (auto* feature : Feature::GetFeatureList()) feature->Save(values);
		}
	};
#include "feature_settings_save_under_test.h"

	void Run(auto require)
	{
		Feature selected{ "Selected", true, 0, { { "value", 2 } } };
		Feature other{ "Other", true, 0, { { "value", 99 } } };
		Feature::features = { &selected, &other };
		Util::FileHelpers::file = { { "Selected", { { "value", 1 } } }, { "Other", { { "value", 3 } } }, { "Unknown", "retain" } };
		State state;
		require(state.Save(State::ConfigMode::USER, &selected), "scoped persistence succeeds");
		require(Util::FileHelpers::file["Selected"]["value"] == 2 && Util::FileHelpers::file["Other"]["value"] == 3 && Util::FileHelpers::file["Unknown"] == "retain", "feature save preserves unrelated persisted and unknown sections");
		require(other.settings["value"] == 99 && other.notified == 0 && selected.notified == 1, "only selected feature receives post-save handling");
		require(globals::menu->accepted == "Selected" && globals::menu->resets == 0 && overrideFeature == &selected && globalSection == "Selected", "feature save scopes clean state and both override layers");
		globals::menu->accepted.clear();
		overrideFailure = true;
		require(!state.Save(State::ConfigMode::USER, &selected) && globals::menu->accepted.empty(), "failed override persistence keeps the feature dirty");
		overrideFailure = false;
		const auto before = Util::FileHelpers::file;
		SettingsSerialization::fail = true;
		selected.settings["value"] = 7;
		require(!state.Save(State::ConfigMode::USER, &selected) && Util::FileHelpers::file == before && globals::menu->accepted.empty(), "failed writes preserve saved values and dirty state");
		SettingsSerialization::fail = false;
		Util::FileHelpers::result = Util::FileHelpers::JsonFileReadResult::Error;
		const int writes = SettingsSerialization::writes;
		require(!state.Save(State::ConfigMode::USER, &selected) && SettingsSerialization::writes == writes, "unreadable saved settings are protected");
		Util::FileHelpers::result = Util::FileHelpers::JsonFileReadResult::Success;
		state.blocked = true;
		require(!state.Save(State::ConfigMode::USER, &selected) && SettingsSerialization::writes == writes, "save/load activity blocks scoped persistence");
		Feature::features.clear();
	}
}
