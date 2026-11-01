#include "Utils/NormalizedCoordinates.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
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
	void Check(bool condition, const char* message)
	{
		if (!condition) throw std::runtime_error(message);
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
		seeded.SeedDefaultPresets({ { "Left Half", { 0.0f, 0.0f, 0.5f, 1.0f } }, { "Right Half", { 0.5f, 0.0f, 0.5f, 1.0f } } });
		seeded.LoadSettings({ { "CropPresets", json::array({ nullptr, false }) } });
		json seededSaved;
		seeded.SaveSettings(seededSaved);
		Check(seededSaved["CropPresets"].size() == 2 && seededSaved["CropPresets"][0]["name"] == "Left Half", "Malformed presets erased seeded rectangular defaults");
	}
	void CheckCropBounds()
	{
		using namespace Util::Subrect;
		const auto empty = ResolvePixelRegion({}, 0, 200);
		Check(!empty.w && !empty.h, "Empty image must have no pixels");
		const auto whole = ResolvePixelRegion({}, 1921, 1081);
		Check(!whole.x && !whole.y && whole.w == 1921 && whole.h == 1081, "Full image lost edge pixels");
		const auto left = ResolvePixelRegion({ 0, 0, .5f, 1 }, 1921, 1081);
		const auto right = ResolvePixelRegion({ .5f, 0, .5f, 1 }, 1921, 1081);
		Check(left.x + left.w == right.x && right.x + right.w == 1921, "Odd-size neighboring crops overlap or leave a gap");
		const auto tiny = ResolvePixelRegion({ .99999f, .99999f, .00001f, .00001f }, 100, 200);
		Check(tiny.x == 99 && tiny.y == 199 && tiny.w == 1 && tiny.h == 1, "Tiny edge crop escaped the image");
		const float bad = std::numeric_limits<float>::quiet_NaN();
		const auto finite = ResolvePixelRegion({ bad, bad, bad, bad }, 100, 200);
		Check(finite.x == 0 && finite.y == 0 && finite.w == 100 && finite.h == 200, "Non-finite crop did not recover safely");
		Controller controller;
		controller.LoadSettings({ { "CropX", "invalid" }, { "CropY", .2f }, { "CropW", 1e100 }, { "CropH", .3f },
			{ "SelectedPresetIndex", -1 }, { "CropPresets", json::array({ { { "name", false }, { "uv", { "bad", .1f, .5f, .4f } } } }) } });
		Check(controller.GetUV().x == 0 && std::abs(controller.GetUV().y - .2f) < 1e-6f &&
			controller.GetUV().w == 1 && std::abs(controller.GetUV().h - .3f) < 1e-6f, "Malformed components discarded valid crop siblings");
		json saved; controller.SaveSettings(saved);
		Check(saved["CropPresets"][0]["name"] == "Unknown" && saved["CropPresets"][0]["uv"][2] == .5f,
			"Malformed preset fields discarded valid siblings");
	}
	void CheckSeededMigration()
	{
		using namespace Util::Subrect;
		const std::vector<Preset> defaults{ { "Center", { .25f, .25f, .5f, .5f } }, { "Full Frame", {} } };
		Controller fresh; fresh.SeedDefaultPresets(defaults);
		Check(fresh.GetUV().x == .25f && fresh.GetUV().w == .5f, "Fresh seeded selection and crop disagree");
		Controller custom;
		custom.LoadSettings({ { "CropX", .1f }, { "CropW", .4f }, { "SelectedPresetIndex", -1 } });
		custom.SeedDefaultPresets(defaults);
		json saved; custom.SaveSettings(saved);
		Check(saved["CropPresets"].size() == 2 && saved["SelectedPresetIndex"] == -1 && std::abs(custom.GetUV().x - .1f) < 1e-6f,
			"Seeding a legacy placeholder replaced a saved custom crop");
		Controller full; full.LoadSettings(json::object()); full.SeedDefaultPresets(defaults);
		full.SaveSettings(saved);
		Check(saved["SelectedPresetIndex"] == 1 && full.GetUV().w == 1, "Legacy full-frame choice did not retain full-frame behavior");
		Controller emptyCustom; emptyCustom.SeedDefaultPresets(defaults);
		emptyCustom.LoadSettings({ { "CropX", .3f }, { "CropW", .2f }, { "SelectedPresetIndex", -1 }, { "CropPresets", json::array() } });
		Check(std::abs(emptyCustom.GetUV().x - .3f) < 1e-6f && std::abs(emptyCustom.GetUV().w - .2f) < 1e-6f,
			"Recovery of empty presets overwrote a valid custom rectangle");
	}
}
int main()
{
	try { CheckCropPresets(); CheckCropBounds(); CheckSeededMigration(); }
	catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
	std::cout << "Mono crop validation, pixel boundaries, recovery and seeded migration passed\n";
}
