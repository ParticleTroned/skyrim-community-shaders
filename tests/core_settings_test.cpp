#include "SettingsMigrations.h"
#include "Utils/NormalizedCoordinates.h"
#include "Utils/SettingsOverlay.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;
#include "subrect_settings_types.h"
#include "subrect_settings_under_test.h"

namespace
{
	void Check(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	json Defaults()
	{
		return { { "General", { { "Enable Shaders", true }, { "Enable Async", true } } },
			{ "Advanced", { { "Compiler Threads", std::int32_t{ 4 } }, { "Log Level", std::uint32_t{ 2 } },
							  { "Refraction Scale", 1.0f }, { "Shader Defines", "DEFAULT" } } },
			{ "Replace Original Shaders", { { "Lighting", true }, { "Water", true } } },
			{ "Disable at Boot", { { "Upscaling", false }, { "WetnessEffects", true } } },
			{ "Upscaling", { { "fsr4RuntimeEnable", false } } } };
	}

	void CheckCoreOverlay()
	{
		auto defaults = Defaults();
		const json user{ { "General", { { "Enable Shaders", false } } },
			{ "Advanced", { { "Compiler Threads", 7.0 }, { "Shader Defines", "USER" } } },
			{ "Replace Original Shaders", { { "Water", false } } },
			{ "Disable at Boot", { { "Upscaling", true }, { "NewFeature", false } } },
			{ "Upscaling", { { "qualityMode", 3 } } } };
		const auto originalUser = user;
		for (const auto& [key, value] : user.items()) {
			const bool handled = Util::Settings::OverlayCoreSettings(defaults, key, value);
			Check(handled == (key != "Upscaling"), "Feature settings entered the core overlay");
		}
		Check(user == originalUser, "Source settings were modified");
		Check(!defaults["General"]["Enable Shaders"].get<bool>() && defaults["General"]["Enable Async"].get<bool>(), "Partial general settings discarded defaults");
		Check(defaults["Advanced"]["Compiler Threads"] == 7 && defaults["Advanced"]["Refraction Scale"] == 1.0f && defaults["Advanced"]["Shader Defines"] == "USER", "Valid advanced settings were lost");
		Check(defaults["Replace Original Shaders"]["Lighting"].get<bool>() && !defaults["Replace Original Shaders"]["Water"].get<bool>(), "Shader replacement flags were reset");
		Check(defaults["Disable at Boot"]["Upscaling"].get<bool>() && defaults["Disable at Boot"]["WetnessEffects"].get<bool>() && !defaults["Disable at Boot"]["NewFeature"].get<bool>(), "Partial boot flags discarded saved choices");
		Check(defaults["Upscaling"] == Defaults()["Upscaling"], "Core overlay synthesized a feature migration field");

		for (const auto* section : { "General", "Advanced", "Replace Original Shaders", "Disable at Boot" }) {
			for (const auto& bad : { json(nullptr), json(true), json(9), json("invalid"), json::array() }) {
				auto unchanged = Defaults();
				Check(Util::Settings::OverlayCoreSettings(unchanged, section, bad), "Malformed core patch was not handled");
				Check(unchanged == Defaults(), "Malformed core patch replaced valid defaults");
			}
		}
		defaults = Defaults();
		Util::Settings::OverlayCoreSettings(defaults, "General", { { "Enable Async", "yes" }, { "Enable Shaders", false } });
		Util::Settings::OverlayCoreSettings(defaults, "Advanced", { { "Shader Defines", true }, { "Compiler Threads", 2 }, { "FutureSetting", "retained" } });
		Util::Settings::OverlayCoreSettings(defaults, "Disable at Boot", { { "Upscaling", nullptr }, { "WetnessEffects", "false" }, { "NewFeature", 0 } });
		Util::Settings::OverlayCoreSettings(defaults, "Replace Original Shaders", { { "Lighting", 1 }, { "Water", false } });
		Check(defaults["General"]["Enable Async"].get<bool>() && !defaults["General"]["Enable Shaders"].get<bool>(), "Malformed sibling discarded a valid general choice");
		Check(defaults["Advanced"]["Shader Defines"] == "DEFAULT" && defaults["Advanced"]["Compiler Threads"] == 2 && defaults["Advanced"]["FutureSetting"] == "retained", "Known-value validation discarded valid siblings or owner data");
		Check(defaults["Disable at Boot"] == Defaults()["Disable at Boot"], "Malformed dynamic boot flags entered live state");
		Check(defaults["Replace Original Shaders"]["Lighting"].get<bool>() && !defaults["Replace Original Shaders"]["Water"].get<bool>(), "Malformed shader flag discarded a valid sibling");
	}

	void CheckReverseZStartupMigration()
	{
		for (const bool disabled : { false, true }) {
			for (const bool enabled : { false, true }) {
				json settings{ { "Disable at Boot", { { "ReverseZ", disabled }, { "CloudShadows", true } } },
					{ "Reverse Z", { { "EnableReverseZBuffer", enabled } } } };
				SettingsMigrations::MigrateReverseZStartup(settings);
				Check(settings["Reverse Z"]["EnableReverseZBuffer"] == (enabled && !disabled), "Reverse Z migration changed the effective boot choice");
				Check(!settings["Disable at Boot"].contains("ReverseZ") && settings["Disable at Boot"]["CloudShadows"] == true,
					"Reverse Z migration altered another startup choice or retained its duplicate gate");
				const auto once = settings;
				SettingsMigrations::MigrateReverseZStartup(settings);
				Check(settings == once, "Reverse Z startup migration is not idempotent");
			}
		}
		for (const auto& invalid : { json(nullptr), json(true), json("invalid"), json::array() }) {
			json settings{ { "Disable at Boot", { { "ReverseZ", true } } }, { "Reverse Z", invalid } };
			SettingsMigrations::MigrateReverseZStartup(settings);
			Check(settings["Reverse Z"]["EnableReverseZBuffer"] == false, "Legacy disabled Reverse Z became active with malformed settings");
		}
	}

	void CheckNumericBounds()
	{
		for (const auto& bad : { json(0.5), json(std::uint64_t{ 2147483648 }), json(std::numeric_limits<std::uint64_t>::max()), json("2"), json(true), json::object() }) {
			auto defaults = Defaults();
			Util::Settings::OverlayCoreSettings(defaults, "Advanced", { { "Log Level", bad }, { "Compiler Threads", 8 } });
			Check(defaults["Advanced"]["Log Level"] == 2 && defaults["Advanced"]["Compiler Threads"] == 8, "Invalid enum setting escaped validation or discarded its sibling");
		}
		for (const auto& bad : { json(-2147483649.0), json(2147483648.0), json(1.5), json(std::numeric_limits<std::uint64_t>::max()) }) {
			auto defaults = Defaults();
			Util::Settings::OverlayCoreSettings(defaults, "Advanced", { { "Compiler Threads", bad } });
			Check(defaults["Advanced"]["Compiler Threads"] == 4, "Invalid signed setting escaped validation");
		}
		for (const auto& bad : { json(1e100), json(-1e100), json(std::numeric_limits<double>::quiet_NaN()), json(std::numeric_limits<double>::infinity()), json(nullptr) }) {
			auto defaults = Defaults();
			Util::Settings::OverlayCoreSettings(defaults, "Advanced", { { "Refraction Scale", bad } });
			Check(defaults["Advanced"]["Refraction Scale"] == 1.0f, "Invalid float setting escaped validation");
		}
		for (const auto value : { std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max() }) {
			auto defaults = Defaults();
			Util::Settings::OverlayCoreSettings(defaults, "Advanced", { { "Compiler Threads", value } });
			Check(defaults["Advanced"]["Compiler Threads"].get<std::int32_t>() == value, "A valid signed boundary was rejected");
		}
		auto defaults = Defaults();
		Util::Settings::OverlayCoreSettings(defaults, "Advanced", { { "Log Level", std::numeric_limits<std::int32_t>::max() }, { "Refraction Scale", 2 } });
		Check(defaults["Advanced"]["Log Level"].get<std::int32_t>() == std::numeric_limits<std::int32_t>::max() && defaults["Advanced"]["Refraction Scale"] == 2.0f, "Safe numeric representations were rejected");
		auto parsed = json::parse(Defaults().dump());
		Check(parsed["Advanced"]["Compiler Threads"].is_number_unsigned(), "Round-trip did not exercise persisted integer signedness");
		Util::Settings::OverlayCoreSettings(parsed, "Advanced", { { "Compiler Threads", std::uint64_t{ 2147483648 } }, { "Log Level", std::uint64_t{ 2147483648 } }, { "Refraction Scale", 0.25 } });
		Check(parsed["Advanced"]["Compiler Threads"] == 4 && parsed["Advanced"]["Log Level"] == 2 && parsed["Advanced"]["Refraction Scale"] == 0.25f, "Persisted JSON weakened numeric validation");
		parsed["Advanced"].erase("Compiler Threads");
		Util::Settings::OverlayCoreSettings(parsed, "Advanced", { { "Compiler Threads", std::uint64_t{ 2147483648 } } });
		Check(!parsed["Advanced"].contains("Compiler Threads"), "An absent known field accepted an overflowing value");
		Util::Settings::OverlayCoreSettings(parsed, "Advanced", { { "Compiler Threads", 6 }, { "Refraction Scale", 0.5 } });
		Check(parsed["Advanced"]["Compiler Threads"] == 6 && parsed["Advanced"]["Refraction Scale"] == 0.5f, "A valid missing known field was not restored");
	}

	void CheckCropPresets()
	{
		using Util::Subrect::Controller;
		for (const auto& bad : json::array({ 42, 0.5, true, "invalid", nullptr, json::array() })) {
			Controller controller;
			controller.LoadSettings({ { "CropPresets", json::array({ bad }) } });
			json saved;
			controller.SaveSettings(saved);
			Check(saved["CropPresets"].size() == 1 && saved["CropPresets"][0]["name"] == "Full Frame", "Invalid-only presets did not retain the normal fallback");
		}
		const json first{ { "name", "First" }, { "uv", { 0.1f, 0.2f, 0.5f, 0.6f } } };
		const json second{ { "name", "Second" }, { "uv", { 0.2f, 0.1f, 0.6f, 0.5f } } };
		Controller controller;
		controller.LoadSettings({ { "CropPresets", json::array({ 42, first, nullptr, second, false }) }, { "SelectedPresetIndex", 3 } });
		json saved;
		controller.SaveSettings(saved);
		Check(saved["CropPresets"] == json::array({ first, second }), "Malformed presets discarded valid neighbors");
		Check(std::abs(controller.GetUV().x - 0.2f) < 1e-6f && std::abs(controller.GetUV().w - 0.6f) < 1e-6f, "Valid crop selection failed after filtering");
		Controller reloaded;
		reloaded.LoadSettings(saved);
		json roundTrip;
		reloaded.SaveSettings(roundTrip);
		Check(roundTrip == saved, "Filtered crop settings did not round-trip");
		Controller selectedFirst;
		selectedFirst.LoadSettings({ { "CropPresets", json::array({ 42, first, nullptr, second }) }, { "SelectedPresetIndex", 1 } });
		json firstSaved;
		selectedFirst.SaveSettings(firstSaved);
		Check(firstSaved["SelectedPresetIndex"] == 0 && firstSaved["CropX"] == first["uv"][0], "Filtering changed the first selected preset's identity");
		for (const auto& selection : { json(2), json(std::numeric_limits<std::uint64_t>::max()), json("1"), json(1.5) }) {
			Controller custom;
			custom.LoadSettings({ { "CropX", 0.3f }, { "CropW", 0.4f },
				{ "CropPresets", json::array({ 42, first, nullptr, second }) }, { "SelectedPresetIndex", selection } });
			json customSaved;
			custom.SaveSettings(customSaved);
			Check(customSaved["SelectedPresetIndex"] == -1 && std::abs(custom.GetUV().x - 0.3f) < 1e-6f,
				"An invalid preset selection replaced the custom crop");
		}
		Controller seeded;
		seeded.SeedDefaultPresets({ { "Left Eye", { 0.0f, 0.0f, 0.5f, 1.0f } }, { "Right Eye", { 0.5f, 0.0f, 0.5f, 1.0f } } });
		seeded.LoadSettings({ { "CropPresets", json::array({ nullptr, false }) } });
		json seededSaved;
		seeded.SaveSettings(seededSaved);
		Check(seededSaved["CropPresets"].size() == 2 && seededSaved["CropPresets"][0]["name"] == "Left Eye", "Malformed presets erased seeded stereo defaults");
	}
}

int main()
{
	try {
		CheckCoreOverlay();
		CheckReverseZStartupMigration();
		CheckNumericBounds();
		CheckCropPresets();
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	std::cout << "Core settings overlay and crop recovery checks passed\n";
	return 0;
}
