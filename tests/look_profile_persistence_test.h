#pragma once

#include "Presets/LookProfiles.h"
#include "Presets/LookSceneRuntime.h"
#include "Utils/StringUtils.h"

// Only engine services are substituted; profile loading, saving and exchange use production code.
struct Feature
{
	Json settings = { { "EnableSkylighting", true }, { "ProbeGridQuality", 2 }, { "MinDiffuseVisibility", .3f }, { "MinSpecularVisibility", .4f } };
	bool loaded = true, throwNext = false;
	Json transient = Json::object();
	std::string GetShortName() const { return "Skylighting"; }
	std::string GetName() const { return "Skylighting"; }
	static std::vector<Feature*> GetFeatureList()
	{
		static Feature feature;
		return { &feature };
	}
	void SaveSettings(Json& value)
	{
		value = settings;
		Overlay(value, transient);
	}
	void SaveBaseSettings(Json& value) { value = settings; }
	void LoadSettings(Json& value)
	{
		settings = value;
		if (throwNext) {
			throwNext = false;
			throw std::runtime_error("Simulated feature apply failure");
		}
	}
};

struct SceneSettingsManager
{
	struct LookState
	{
		Json layer = Json::object(), edits = Json::array();
		std::string id;
		bool managed = false;
	} state;
	struct BaseSettingsScope
	{
		void Finish() {}
	};
	static SceneSettingsManager* GetSingleton()
	{
		static SceneSettingsManager manager;
		return &manager;
	}
	LookState CaptureLookState() const { return state; }
	void RestoreLookState(const LookState& previous) { state = previous; }
	void UseLookPersistence(bool enabled) { state.managed = enabled; }
	void SetLookLayer(const Json& layer, const std::string& id)
	{
		state.layer = layer;
		state.id = id;
	}
	Json ExportLookLayer() const { return state.layer; }
	Json SerializeLookEdits() const { return state.edits; }
	void LoadLookEdits(const Json& edits, const Json&) { state.edits = edits; }
};

namespace Presets::LookSceneRuntime
{
	std::map<std::string, Json> testScenes;
	void Use(const LookPack* pack) { testScenes = pack ? pack->scenes : std::map<std::string, Json>{}; }
	const std::string& Error()
	{
		static const std::string error;
		return error;
	}
}

namespace globals
{
	struct State
	{
		float refractionScale = 1, pbrMetalReflectionScale = 1, pbrMetalHighlightScale = 1;
	} stateStorage;
	inline State* state = &stateStorage;
	struct Menu
	{
		bool PrepareSettingsMutation() { return true; }
		void RequestSettingsDirtyCheck() {}
	} menuStorage;
	inline Menu* menu = &menuStorage;
	namespace features
	{
		struct AdaptiveBrightness
		{
			bool locationOverrideEditProfile = false;
			bool ReadLegacyVisuals(const std::filesystem::path&, Json&, std::string& error)
			{
				error = "Legacy fixture not provided";
				return false;
			}
		} adaptiveBrightness;
	}
}
namespace Util::PathHelpers
{
	std::filesystem::path testRoot;
	std::filesystem::path GetCommunityShaderPath() { return testRoot; }
}
namespace PresetCompatibility
{
	struct Result
	{
		bool ShouldApply() const { return true; }
	};
	Result GetPublished() { return {}; }
}
namespace MenuUI
{
	enum class GlobalSettingsAction
	{
		Load
	};
	const char* GetGlobalSettingsActionBlockReason(GlobalSettingsAction) { return nullptr; }
}
struct SettingsOverrideManager
{
	static SettingsOverrideManager* GetSingleton()
	{
		static SettingsOverrideManager manager;
		return &manager;
	}
	void ApplyGlobalOverrides(Json&) {}
	void ApplyOverrides(const std::string&, Json&) {}
};
struct WeatherManager
{
	static WeatherManager* GetSingleton()
	{
		static WeatherManager manager;
		return &manager;
	}
	void NotifyUserSettingsChanged() {}
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
		void CaptureFeatureUserSettings(const std::string&) {}
	};
}
namespace PerformanceTuningRenderer
{
	void NotifyFeatureSettingsChanged(Feature*) {}
}
namespace logger
{
	template <class... Args>
	void error(Args&&...)
	{}
}

#include "look_profiles_under_test.h"

void TestProfilePersistence(const std::filesystem::path& root)
{
	Util::PathHelpers::testRoot = root / "Runtime";
	const auto library = Presets::LookProfiles::RootPath();
	auto* feature = Feature::GetFeatureList().front();
	const auto factory = feature->settings;
	Presets::LookProfiles profiles;
	profiles.CaptureFactorySettings();
	profiles.Load(Json::object(), Json::object());
	std::string error;
	Require(profiles.Import(root / "Example", "Imported", error), "supported profile imports and selects");
	Require(profiles.ActiveId() == "Imported" && feature->settings["MinDiffuseVisibility"] == .7f, "import applies authored appearance");
	const auto authored = ReadLookPack(library, "Imported", { { "Skylighting", factory } });
	Require(authored.IsCompatible(), "imported profile is independently readable");

	feature->settings["MinDiffuseVisibility"] = .81f;
	feature->settings["ProbeGridQuality"] = 1;
	Json saved = { { "Skylighting", feature->settings }, { "Unrelated", { { "value", 42 } } } };
	Require(profiles.PrepareSave(saved, error), "full settings save records profile selection and edits");
	saved["Presets"]["csxProfiles"]["Imported"]["edits"]["FutureFeature"] = { { "value", 7 } };
	Save(root / "SettingsUser.json", saved);
	const auto disk = ReadProfileDocument(root / "SettingsUser.json");
	Require(disk["Presets"]["activePackId"] == "Imported" && disk["Skylighting"]["ProbeGridQuality"] == 1, "selection and performance survive serialization");

	feature->settings = factory;
	Presets::LookProfiles restored;
	restored.CaptureFactorySettings();
	restored.Load(Json::object(), disk);
	Require(restored.GetLoadError().empty() && restored.ActiveId() == "Imported", "saved selection reloads");
	Require(feature->settings["MinDiffuseVisibility"] == .81f && feature->settings["ProbeGridQuality"] == 1, "personal appearance and performance reload separately");
	Json resaved = { { "Skylighting", feature->settings } };
	Require(restored.PrepareSave(resaved, error), "reloaded profile can be saved again");
	Require(resaved["Presets"]["csxProfiles"]["Imported"]["edits"]["FutureFeature"]["value"] == 7, "unavailable personal feature edits survive save and reload");

	feature->settings["MinSpecularVisibility"] = .65f;
	Require(restored.PrepareSave(resaved, error, feature), "feature-only save retains the active profile");
	Require(resaved["Presets"]["csxProfiles"]["Imported"]["edits"]["Skylighting"]["MinSpecularVisibility"] == .65f, "feature save updates its personal profile edits");
	Require(resaved["Presets"]["csxProfiles"]["Imported"]["edits"]["FutureFeature"]["value"] == 7, "feature save preserves another feature's unavailable data");

	const auto before = feature->settings;
	Json beforeSelection;
	restored.SaveSelection(beforeSelection);
	const auto beforeScenes = Presets::LookSceneRuntime::testScenes;
	Require(!restored.Import(root / "PP", "RejectedPP", error) && error.starts_with("Requires Post Processing."), "PP import returns an unavailable requirement");
	Require(!std::filesystem::exists(library / "RejectedPP") && feature->settings == before, "rejected PP import publishes nothing and changes no feature values");
	Require(!restored.Import(root / "Bad", "RejectedInvalid", error) && error.starts_with("Invalid profile."), "malformed import returns an invalid-profile error");
	Json afterSelection;
	restored.SaveSelection(afterSelection);
	Require(afterSelection == beforeSelection && Presets::LookSceneRuntime::testScenes == beforeScenes, "failed imports preserve selection, personal edits and scene ownership");

	Save(library / "Failing" / "Baseline" / "Skylighting.json", { { "MinDiffuseVisibility", .1f } });
	feature->throwNext = true;
	Require(!restored.Select("Failing", error) && feature->settings == before && restored.ActiveId() == "Imported", "partial feature application rolls back values and selection");

	feature->transient = { { "MinDiffuseVisibility", .05f } };
	Require(restored.Export("Exported", error, root), "authored profile exports while a scene overrides the live appearance");
	const auto exported = ReadLookPack(root, "Exported", { { "Advanced", { { "Refraction Scale", 1.f }, { "PBR Metal Reflection Scale", 1.f }, { "PBR Metal Highlight Scale", 1.f } } }, { "Skylighting", factory } });
	Require(exported.IsCompatible() && exported.baseline["Skylighting"]["MinDiffuseVisibility"] == .81f, "export uses authored settings rather than temporary scene values");
	Require(exported.scenes == authored.scenes && exported.periodicContexts == authored.periodicContexts, "weather, time and location rules survive import and export");
	Require(exported.documents.at("Baseline/PostProcessing.json") == authored.documents.at("Baseline/PostProcessing.json"), "unsupported inactive settings survive full import and export");
	Require(ReadLookPack(library, "Imported", { { "Skylighting", factory } }).baseline == authored.baseline, "personal edits never rewrite the source pack");
	feature->transient = Json::object();

	Save(library / "PP" / "Baseline" / "PostProcessing.json", { { "Exposure", 1.f } });
	Save(library / "Invalid" / "Baseline" / "Skylighting.json", { { "MinDiffuseVisibility", "wrong" } });
	const auto catalog = restored.Inspect();
	Require(catalog["postProcessingAvailable"] == false, "inspection cannot advertise PP support");
	for (const auto& pack : catalog["packs"]) {
		if (pack["id"] == "PP")
			Require(pack["status"] == "unavailable" && pack["summary"] == "Requires Post Processing." && !pack["unavailable"].empty(), "UI and API receive the unavailable requirement");
		if (pack["id"] == "Invalid")
			Require(pack["status"] == "invalid" && pack["summary"] == "Invalid profile." && !pack["errors"].empty(), "UI and API receive malformed-profile feedback");
	}
	Require(restored.Import(root / "Exported" / "Exported.json", "Reimported", error) &&
				restored.ActiveId() == "Reimported" && feature->settings["MinDiffuseVisibility"] == .81f,
		"a freshly exported manifest can be imported from a different folder and applied");
	Require(restored.Import(library / "Reimported" / "Reimported.json", "Reimported", error),
		"reopening an installed manifest selects it without replacing its folder");
}
