#include "Menu/PerformanceProfiles.h"

#include "performance_profile_application_test.h"
#include "ssgi_presets_under_test.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using nlohmann::json;

namespace
{
	void Require(bool a_value, const char* a_message)
	{
		if (!a_value)
			throw std::runtime_error(a_message);
	}
}

void TestSsgiPresets()
{
	using namespace SsgiPresetTest;
	for (bool vr : { false, true }) {
		REL::Module::vr = vr;
		ScreenSpaceGI feature;
		feature.settings.Enabled = false;
		Require(feature.GetPerformancePresetNames() == std::vector<std::string>{ "AO only", "AO + GI", "Reference" },
			"profile capture exposes all native SSGI presets on each runtime");
		for (const auto& name : feature.GetPerformancePresetNames()) {
			feature.recompileFlag = false;
			Require(feature.ApplyPerformancePreset(name) && feature.GetPerformancePresetName() == name &&
						!feature.settings.Enabled && feature.recompileFlag,
				"native profile presets preserve disabled state and match runtime-specific defaults");
			feature.settings.Enabled = true;
			Require(feature.ApplyPerformancePreset(name) && feature.settings.Enabled,
				"native profile presets preserve enabled state");
			feature.settings.Enabled = false;
		}
		feature.recompileFlag = false;
		const auto before = feature.GetPerformancePresetName();
		Require(!feature.ApplyPerformancePreset("Unknown") && feature.GetPerformancePresetName() == before && !feature.recompileFlag,
			"an unknown preset preserves settings and shader state");

		for (int preset = 0; preset < 3; ++preset) {
			ScreenSpaceGI::Settings settings;
			settings.Enabled = false;
			settings.EnableAdaptiveSampling = false;
			settings.ExperimentalOCUEffectFoveation = true;
			settings.EnableExperimentalSpecularGI = true;
			settings.EnableTemporalDenoiser = true;
			settings.VRCullDistance = 17;
			settings.AOPower = .5f;
			const auto apply = preset == 0 ? ApplyAOOnlyPreset : preset == 1 ? ApplyAOGIPreset :
			                                                                   ApplyReferencePreset;
			const auto matches = preset == 0 ? IsAOOnlyPreset : preset == 1 ? IsAOGIPreset :
			                                                                  IsReferencePreset;
			apply(settings);
			Require(!settings.Enabled && matches(settings, vr), "SSGI preset must match after application and preserve Enabled");
			settings.CenterFullResMaskScale = .7f;
			Require(matches(settings, vr), "Derived shared FOV cache must not invalidate a preset");
			settings.ExperimentalOCUEffectFoveation = true;
			Require(!matches(settings, vr), "Experimental OCU sampling must invalidate the preset");
			apply(settings);
			settings.EnableAdaptiveSampling = false;
			Require(!matches(settings, vr), "Changed adaptive sampling must invalidate the preset");
			apply(settings);
			settings.EnableTemporalDenoiser = true;
			Require(!matches(settings, vr), "Changed denoising must invalidate the preset");
		}
	}
}

int main()
{
	try {
		using namespace PerformanceProfiles;
		const auto root = std::filesystem::path(__FILE__).parent_path().parent_path();
		for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "MGO-Presets")) {
			if (entry.path().filename() != "SettingsUser.json")
				continue;
			std::ifstream input(entry.path());
			const auto preset = json::parse(input);
			Require(preset.at("Wetterness").at("RestrictPuddleSlopes") == true &&
						preset.at("Wetterness").at("PreserveWetnessHighlights") == true,
				"Every bundled preset enables puddle slopes and wetness preservation");
		}
		for (const auto name : { "Performance", "Balanced", "Quality" }) {
			const auto choices = BuiltIn(name);
			Require(ValidateChoices(choices), "built-in profile contains only allowed choices");
			std::ifstream input(root / "MGO-Presets" / (std::string("CSX Unified- ") + name + " - Press END on PC to Customize") /
								"SKSE/Plugins/CommunityShaders/SettingsUser.json");
			Require(input.good(), "unified preset is available");
			const auto unified = json::parse(input);
			for (const auto& [key, value] : choices["upscaling"].items())
				Require(unified["Upscaling"][key] == value, "upscaling choices match the unified tier");
			for (const auto& entry : { std::array{ "GrassCollision", "Grass Collision", "EnableGrassCollision" },
					 std::array{ "ScreenSpaceGI", "Screen Space GI", "Enabled" },
					 std::array{ "ScreenSpaceShadows", "Screen Space Shadows", "Enable" },
					 std::array{ "Skylighting", "Skylighting", "EnableSkylighting" },
					 std::array{ "TerrainBlending", "Terrain Blending", "Enabled" },
					 std::array{ "Wetterness", "Wetterness", "EnableWetterness" } }) {
				const auto& setting = unified[entry[1]][entry[2]];
				const bool enabled = setting.is_boolean() ? setting.get<bool>() : setting.get<unsigned>() != 0;
				Require(choices["features"][entry[0]] == enabled, "feature switches match the unified tier");
			}
		}
		UserProfile user;
		user.choices = { { "features", { { "ScreenSpaceGI", false } } }, { "presets", { { "Skylighting", "Hoshipa" } } } };
		const auto resolved = user.Resolve();
		Require(resolved["features"]["ScreenSpaceGI"] == false, "false selections survive base composition");
		Require(resolved["presets"]["Skylighting"] == "Hoshipa", "named feature preset replaces the base selection");
		Require(resolved["presets"]["Wetterness"] == "Balanced", "unmodified selections inherit the base");
		UserProfile loaded;
		Require(loaded.Load(user.Save()) && loaded.Save() == user.Save(), "saved user profile round-trips");
		auto legacy = user.Save();
		legacy["version"] = 1;
		legacy.erase("settings");
		Require(loaded.Load(legacy) && loaded.settings.empty() && loaded.choices == user.choices,
			"version one profiles remain readable without a settings snapshot");
		const auto previous = loaded.Save();
		auto missingSnapshot = previous;
		missingSnapshot.erase("settings");
		missingSnapshot["unknown"] = json::object();
		Require(!loaded.Load(missingSnapshot) && loaded.Save() == previous, "version two requires its settings field, not an arbitrary fourth field");
		Require(!ValidateSettings(json{ { std::string("bad\0key", 7), true } }), "embedded nulls in setting names are rejected");
		for (const auto& invalid : {
				 json{ { "appearance", { { "brightness", 2 } } } },
				 json{ { "features", { { "Wetterness", { { "brightness", 2 } } } } } },
				 json{ { "presets", { { "Skylighting", { { "ProbeFieldSize", 512 } } } } } },
				 json{ { "upscaling", { { "sharpnessDLSS", 1 } } } },
				 json{ { "upscaling", { { "qualityMode", -1 } } } },
				 json{ { "upscaling", { { "qualityMode", 7 } } } },
				 json{ { "upscaling", { { "qualityMode", 2.0 } } } },
				 json{ { "features", { { "Skylighting", 1 } } } } }) {
			Require(!ValidateChoices(invalid), "visual, unchecked or incorrectly typed settings are rejected");
			auto document = previous;
			document["choices"] = invalid;
			Require(!loaded.Load(document) && loaded.Save() == previous, "invalid input preserves the last valid profile");
		}
		auto recursive = previous;
		recursive["base"] = "User defined";
		Require(!loaded.Load(recursive), "a user profile cannot inherit from itself");
		Require(!ValidateChoices(json{ { "upscaling", { { "qualityMode", UINT64_MAX } } } }), "oversized integers are rejected");
		Require(!ValidateChoices(json{ { "features", { { "Upscaling", false } } } }), "upscaling cannot bypass its selection boundary as a feature toggle");
		Require(!ValidateChoices(json{ { "presets", { { "Skylighting", "" } } } }), "empty preset names are rejected");
		TestProfileApplication();
		TestSsgiPresets();
		std::cout << "Performance profile policy and unified-tier checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
