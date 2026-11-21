#include "Menu/PerformanceProfiles.h"
#include "Presets/LookPack.h"
#include "Presets/LookProfileState.h"
#include "Presets/LookScenes.h"
#include "SettingsSerialization.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>

// The pack tests exercise document contents and publication; atomic writes have separate tests.
bool SettingsSerialization::WriteFileAtomic(const std::filesystem::path& path, const nlohmann::json& value, std::string& error)
{
	std::ofstream output(path, std::ios::binary);
	output << value.dump(2);
	output.close();
	if (output.fail()) {
		error = "Test file write failed";
		return false;
	}
	return true;
}
using namespace Presets;
void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}
void Save(const std::filesystem::path& path, const Json& value)
{
	std::filesystem::create_directories(path.parent_path());
	std::string error;
	Require(SettingsSerialization::WriteFileAtomic(path, value, error), "fixture write");
}
int main()
try {
	const auto root = std::filesystem::current_path() / ("csx-look-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	struct Cleanup
	{
		std::filesystem::path path;
		~Cleanup()
		{
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{ root };
	std::filesystem::create_directories(root);
	const Json schemas = { { "Advanced", { { "Refraction Scale", 1.f } } }, { "Skylighting", { { "EnableSkylighting", true }, { "ProbeGridQuality", 2 }, { "MinDiffuseVisibility", .3f }, { "MinSpecularVisibility", .4f } } } };
	Save(root / "Example" / "preset.json", { { "name", "Example" }, { "type", "CS" }, { "description", "Shared profile" } });
	Save(root / "Example" / "Baseline" / "Skylighting.json", { { "EnableSkylighting", false }, { "ProbeGridQuality", 0 }, { "MinDiffuseVisibility", .7f } });
	Save(root / "Example" / "TimeOfDay" / "Night" / "Example_Skylighting.json", { { "feature", "Skylighting" }, { "MinDiffuseVisibility", .1f }, { "_metadata", { { "enabled", true }, { "label", "Night" } } } });
	Save(root / "Example" / "Locations" / "0x1~Test.esm" / "Example_Skylighting.json", { { "feature", "Skylighting" }, { "MinDiffuseVisibility", .8f }, { "_metadata", { { "timeOfDayEnabled", false } } } });
	Save(root / "Example" / "InteriorOnly" / "Disabled_Skylighting.json", { { "feature", "Skylighting" }, { "MinSpecularVisibility", .9f }, { "_metadata", { { "enabled", false } } } });
	std::ofstream(root / "Example" / "notes.txt") << "asset bytes";
	auto pack = ReadLookPack(root, "Example", schemas);
	Require(pack.IsCompatible(), "native pack reads");
	Require(!pack.baseline["Skylighting"].contains("ProbeGridQuality") && !pack.baseline["Skylighting"].contains("EnableSkylighting"), "appearance excludes quality and master switches");
	Require(pack.interior.empty(), "disabled scene document is not applied");
	Require(!pack.periodicContexts.at("Locations/0x1~Test.esm"), "native periodic-mode metadata");
	pack.id = "Shared";
	std::string error;
	if (!WriteLookPack(root, pack, schemas, error))
		throw std::runtime_error(error);
	auto shared = ReadLookPack(root, "Shared", schemas);
	Require(shared.IsCompatible() && shared.baseline == pack.baseline && shared.scenes == pack.scenes, "native pack round trip");
	Require(shared.documents.contains("InteriorOnly/Disabled_Skylighting.json"), "disabled context survives export");
	Require(shared.documents.at("TimeOfDay/Night/Skylighting.json")["_metadata"]["label"] == "Night", "scene metadata survives export");
	Require(std::filesystem::file_size(root / "Shared" / "notes.txt") == 11, "assets survive export");
	Require(!WriteLookPack(root, pack, schemas, error), "existing export is never replaced");
	Require(SceneFormKey("0X000ABC~TeSt.EsM") == "0xabc~test.esm", "scene keys ignore case and hex padding");
	auto collision = pack;
	collision.id = "DisabledCollision";
	collision.documents["Baseline/Skylighting.json"] = { { "MinDiffuseVisibility", .2f }, { "_metadata", { { "enabled", false } } } };
	Require(WriteLookPack(root, collision, schemas, error), "disabled source may share an emitted feature filename");
	auto collisionRead = ReadLookPack(root, collision.id, schemas);
	Require(collisionRead.IsCompatible() && collisionRead.baseline == collision.baseline, "disabled source stays inactive after export");
	Save(root / "Deletion" / "Baseline" / "Skylighting.json", { { "MinDiffuseVisibility", .2f }, { "_metadata", { { "entryDeletions", { { "MinDiffuseVisibility", true } } } } } });
	Require(!ReadLookPack(root, "Deletion", schemas).IsCompatible(), "unsupported deletion rules cannot silently change the appearance");
	Save(root / "Misplaced" / "Baseline" / "Skylighting.json", { { "MinDiffuseVisibility", .2f } });
	Save(root / "Misplaced" / "TimeOfDay" / "Skylighting.json", { { "MinDiffuseVisibility", .3f } });
	Require(!ReadLookPack(root, "Misplaced", schemas).IsCompatible(), "unconsumed scene files are rejected");
	Require(!IsPackId("../bad") && !IsPackId("CON") && !IsPackId("bad.") && IsPackId("My Visuals"), "portable names");
	std::ofstream(root / "duplicate.json") << R"({"value":1,"value":2})";
	bool rejected = false;
	try {
		ReadProfileDocument(root / "duplicate.json");
	} catch (...) {
		rejected = true;
	}
	Require(rejected, "duplicate document keys rejected");
	Save(root / "E11" / "preset.json", { { "type", "E11" } });
	Require(!ReadLookPack(root, "E11", schemas).IsCompatible(), "E11 never supported");
	Save(root / "PP" / "Baseline" / "PostProcessing.json", { { "Exposure", 1.f } });
	Require(!ReadLookPack(root, "PP", schemas).IsCompatible(), "missing PP capability rejected before application");
	Save(root / "Bad" / "Baseline" / "Skylighting.json", { { "MinDiffuseVisibility", "oops" } });
	Require(!ReadLookPack(root, "Bad", schemas).IsCompatible(), "invalid numeric setting rejected");
	auto current = schemas["Skylighting"];
	auto selected = ReplaceAppearance("Skylighting", current, pack.baseline["Skylighting"]);
	Require(selected["EnableSkylighting"] == true && selected["ProbeGridQuality"] == 2 && selected["MinDiffuseVisibility"] == .7f, "selection preserves performance");
	const Json mixed = { { "profiles", Json::array({ Json{ { "brightness", 1.f }, { "ParallaxQuality", 3 }, { "advanced", true } } }) } };
	const Json visual = { { "profiles", Json::array({ Json{ { "brightness", 2.f } } }) } };
	const auto layered = ReplaceAppearance("AdaptiveBrightness", mixed, visual);
	Require(layered["profiles"][0]["ParallaxQuality"] == 3 && layered["profiles"][0]["advanced"] == true && layered["profiles"][0]["brightness"] == 2.f, "mixed appearance arrays preserve quality and local UI fields");
	Json edits = Difference(Json{ { "x", .75f } }, Json{ { "x", .5f } });
	Require(edits["x"] == .75f, "personal edits are relative to authored values");
	Json personalState = Json::object();
	StoreProfileEdits(personalState, "A", { { "Skylighting", { { "MinDiffuseVisibility", .8f } } } }, { { "Skylighting", { { "MinDiffuseVisibility", .4f } } } }, Json::array());
	StoreProfileEdits(personalState, "B", { { "Skylighting", { { "MinDiffuseVisibility", .2f } } } }, { { "Skylighting", { { "MinDiffuseVisibility", .6f } } } }, Json::array());
	Require(personalState["csxProfiles"]["A"]["edits"]["Skylighting"]["MinDiffuseVisibility"] == .8f, "switching profiles retains independent A edits");
	Require(personalState["csxProfiles"]["B"]["edits"]["Skylighting"]["MinDiffuseVisibility"] == .2f, "switching profiles retains independent B edits");
	StoreProfileEdits(personalState, "A", { { "Skylighting", { { "MinDiffuseVisibility", .4f } } } }, { { "Skylighting", { { "MinDiffuseVisibility", .4f } } } }, Json::array());
	Require(personalState["csxProfiles"]["A"]["edits"].empty() && !personalState["csxProfiles"]["B"]["edits"].empty(), "restoring A does not reset B");
	const auto interiorEntry = [](const char* feature, const char* key, const Json& value, bool paused = false) {
		return Json{ { "feature", feature }, { "setting", key }, { "value", value }, { "paused", paused } };
	};
	const auto currentInterior = Json::array({ interiorEntry("ScreenSpaceGI", "NumSlices", 8), interiorEntry("ScreenSpaceGI", "AOPower", 1.f) });
	const auto sharedInterior = Json::array({ interiorEntry("ScreenSpaceGI", "NumSlices", 2), interiorEntry("ScreenSpaceGI", "AOPower", 2.f) });
	const auto switchedInterior = ReplaceInteriorAppearance(currentInterior, sharedInterior);
	Require(switchedInterior == Json::array({ currentInterior[0], sharedInterior[1] }), "Visuals selection preserves interior performance choices");
	Require(ReplaceInteriorAppearance(currentInterior, Json::array()) == Json::array({ currentInterior[0] }), "restoring profile appearance retains interior performance");
	StoreProfileEdits(personalState, "Interior", Json::object(), Json::object(), currentInterior);
	Require(personalState["csxProfiles"]["Interior"]["interior"] == Json::array({ currentInterior[1] }), "per-profile interior edits contain appearance only");
	const auto mixedInterior = Json::array({ interiorEntry("AdaptiveBrightness", "profiles", mixed["profiles"], true) });
	const auto visualInterior = Json::array({ interiorEntry("AdaptiveBrightness", "profiles", visual["profiles"]) });
	const auto mixedResult = ReplaceInteriorAppearance(mixedInterior, visualInterior);
	Require(mixedResult[0]["value"] == layered["profiles"] && mixedResult[0]["paused"] == true, "mixed interior fields retain local values, quality and shared pause state");
	const Json source = { { "Presets", { { "activePackId", "A" }, { "csxProfiles", { { "A", { { "edits", { { "Skylighting", { { "x", 2 } } }, { "Other", { { "x", 9 } } } } }, { "interior", Json::array() } } } } }, { "csxInteriorEdits", Json::array() } } } };
	auto target = source;
	target["Presets"]["csxProfiles"]["A"]["edits"]["Other"]["x"] = 1;
	target["Presets"]["csxProfiles"]["A"]["edits"]["Skylighting"]["x"] = 0;
	CopyFeatureProfileState(target, source, "Skylighting");
	Require(target["Presets"]["csxProfiles"]["A"]["edits"]["Other"]["x"] == 1, "feature save preserves other unsaved edits");
	Require(target["Presets"]["csxProfiles"]["A"]["edits"]["Skylighting"]["x"] == 2, "feature save accepts its profile edits");
	for (float hour = -24; hour < 48; hour += .125f) {
		const auto weights = ScenePeriodWeights({ 5, 7.5f, 10, 16, 18.5f, 21 }, hour, 1, 1);
		Require(std::abs(std::accumulate(weights.begin(), weights.end(), 0.f) - 1.f) < 1e-5f, "period weights sum to one across midnight");
		for (auto weight : weights) Require(std::isfinite(weight) && weight >= 0 && weight <= 1, "bounded period weights");
	}
	Json base = { { "F", { { "value", 1.f }, { "enabled", false }, { "color", { 1.f, 2.f, 3.f } } } } };
	auto blend = BlendSceneLayers(base, { { "F", { { "value", 3.f } } } }, Json::object(), .5f);
	Require(blend["F"]["value"] == 2.f, "missing endpoint restores baseline");
	blend = BlendSceneLayers(base, { { "F", { { "color", { 3.f, 4.f, 5.f } } } } }, Json::object(), .5f);
	Require(blend["F"]["color"].is_array() && blend["F"]["color"][2] == 4.f, "color arrays retain structure");
	LookSceneContext context;
	context.weather = "rain";
	context.previousWeather = "clear";
	context.weatherWeight = .5f;
	std::map<std::string, Json> layers{ { "TimeOfDay/Day", { { "F", { { "value", 2.f } } } } }, { "Weather/rain", { { "F", { { "value", 4.f } } } } }, { "Locations/world", { { "F", { { "value", 6.f } } } } }, { "Locations/cell", { { "F", { { "value", 8.f } } } } } };
	Require(ResolveLookScenes(layers, context, base)["F"]["value"] == 3.f, "weather blends above period baseline");
	layers["Weather/rain/Day"] = { { "F", { { "value", 10.f } } } };
	Require(ResolveLookScenes(layers, context, base)["F"]["value"] == 3.f, "flat weather mode excludes period entries by default");
	Require(ResolveLookScenes(layers, context, base, { { "Weather/rain", true } })["F"]["value"] == 6.f, "periodic mode excludes the flat weather layer");
	context.locations = { "world", "cell" };
	Require(ResolveLookScenes(layers, context, base)["F"]["value"] == 8.f, "specific place wins");
	context.interior = true;
	context.locations.clear();
	Require(ResolveLookScenes(layers, context, base).empty(), "interior excludes exterior weather and periods");
	using namespace PerformanceProfiles;
	UserProfile performance, imported;
	performance.base = "Quality";
	performance.choices = { { "features", { { "Wetterness", false } } } };
	auto document = MakeExport(performance, "test");
	Require(ReadExport(document, imported) && imported.Save() == performance.Save(), "MyPerformance round trip");
	const auto previous = imported.Save();
	document["format"] = "CSX.MyPreset";
	Require(!ReadExport(document, imported) && imported.Save() == previous, "no obsolete MyPreset alias");
	document = MakeExport(performance, "test");
	document["profile"]["choices"] = { { "appearance", { { "brightness", 2 } } } };
	Require(!ReadExport(document, imported) && imported.Save() == previous, "performance exchange rejects visual settings atomically");
	std::cout << "Visuals pack, ownership, scene and exchange tests passed\n";
	return 0;
} catch (const std::exception& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
