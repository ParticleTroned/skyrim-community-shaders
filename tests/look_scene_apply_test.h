#pragma once

#include <cmath>
#include <limits>

namespace LookSceneApplyTests
{
	using namespace Presets::LookSceneRuntime;
	void Reset()
	{
		Use(nullptr);
		SceneSettingsManager::GetSingleton()->paused = false;
		WeatherManager::GetSingleton()->active = true;
		Util::interior = false;
		suspended = 0;
		for (const auto name : { "A", "B", "C" }) {
			auto* f = Feature::FindFeatureByShortName(name);
			f->normalizeOnSave = false;
			f->loaded = true;
			f->throwNext = false;
			f->failLoads.clear();
			f->loads = f->saves = 0;
		}
	}
	Json Setup(const Json& settings)
	{
		Reset();
		authoredBase = settings;
		for (const auto& [name, value] : settings.items()) {
			features.insert(name);
			Feature::FindFeatureByShortName(name)->settings = value;
		}
		return settings;
	}
	void ApplyValues(const Json& base, const Json& values, bool weather = true)
	{
		auto prepared = PrepareFields(values);
		Apply(base, prepared, weather);
	}

	void CheckNoOwnership()
	{
		const auto base = Setup({ { "A", { { "value", 1.0 }, { "unrelated", 2.0 } } } });
		auto* a = Feature::FindFeatureByShortName("A");
		ApplyValues(base, Json::object());
		Require(a->settings == base["A"] && saved.empty(), "unowned context changed settings");
		Require(a->saves == 1, "unowned context must retain serializer side effects");
		a->saves = 0;
		SceneSettingsManager::GetSingleton()->paused = true;
		auto prepared = PrepareFields({ { "A", { { "value", 3.0 } } } });
		Apply(base, prepared, true);
		Require(!prepared.at("A").flattened && fieldAddresses.empty(), "paused unowned fields were prepared");
		Require(a->saves == 1, "paused context must retain serializer side effects");
	}

	void CheckWeatherAndEdits()
	{
		const auto base = Setup({ { "A", { { "value", 1.0 }, { "unrelated", 2.0 } } } });
		auto* a = Feature::FindFeatureByShortName("A");
		const Json patch = { { "A", { { "value", 3.0 } } } };
		ApplyValues(base, patch);
		const auto loads = a->loads;
		ApplyValues(base, patch);
		Require(a->loads == loads && Controls("A", "value"), "stable application changed load/ownership");
		a->settings["value"] = 12.0;
		a->settings["unrelated"] = 14.0;
		ApplyValues(base, patch);
		Require(a->settings["value"] == 3.0 && a->settings["unrelated"] == 14.0, "weather/user edit composition changed");
		Json exported = a->settings;
		OverlayBase("A", exported);
		Require(exported["value"] == 1.0 && exported["unrelated"] == 14.0, "authored export lost unrelated edit");
		WeatherManager::GetSingleton()->active = false;
		a->settings["value"] = 20.0;
		ApplyValues(base, patch);
		Require(a->settings["value"] == 20.0, "inactive weather gate changed");
		WeatherManager::GetSingleton()->active = true;
		SceneSettingsManager::GetSingleton()->paused = true;
		ApplyValues(base, patch);
		Require(a->settings["value"] == 1.0 && !Controls("A"), "pause did not release ownership");
		SceneSettingsManager::GetSingleton()->paused = false;
		ApplyValues(base, patch);
		Require(a->settings["value"] == 3.0 && Controls("A"), "resume did not reacquire ownership");
	}

	void CheckArraysNullMissing()
	{
		const auto base = Setup({ { "A", { { "arr", Json::array({ 1, 2, 3 }) }, { "a/b", { { "~v", 4 } } }, { "nullish", nullptr }, { "untouched", true } } } });
		auto* a = Feature::FindFeatureByShortName("A");
		const Json values = { { "A", { { "arr", Json::array({ 5, nullptr, 7 }) }, { "a/b", { { "~v", 6 } } }, { "missing", 8 }, { "nullish", nullptr } } } };
		ApplyValues(base, values);
		Require(a->settings["arr"] == Json::array({ 5, 2, 7 }) && a->settings["a/b"]["~v"] == 6, "array/null/escaped pointer semantics changed");
		Require(!a->settings.contains("missing") && !saved["A"].contains("/missing") && !saved["A"].contains("/nullish"), "missing/null field gained ownership");
		a->settings.erase("arr");
		ApplyValues(base, values);
		Require(a->settings["arr"] == Json::array({ 5, nullptr, 7 }), "missing controlled array construction changed");
		a->settings["arr"] = nullptr;
		ApplyValues(base, values);
		Require(a->settings["arr"] == Json::array({ 5, nullptr, 7 }), "null parent array construction changed");
		a->settings["arr"] = 42;
		const auto before = a->settings;
		bool failed = false;
		try {
			ApplyValues(base, values);
		} catch (const std::exception&) {
			failed = true;
		}
		Require(failed && a->settings == before, "blocked scalar path did not preserve exception behavior");
		a->settings["arr"] = Json::array({ 5, 2, 7 });
	}

	void CheckOverlaps()
	{
		const auto base = Setup({ { "A", { { "node", { { "x", 1 }, { "y", 2 } } }, { "node-", 3 } } } });
		auto* a = Feature::FindFeatureByShortName("A");
		ApplyValues(base, { { "A", { { "node", 7 }, { "node-", 8 } } } });
		ApplyValues(base, { { "A", { { "node", { { "x", 9 } } }, { "node-", 8 } } } });
		Require(a->settings["node"] == Json({ { "x", 9 }, { "y", 2 } }), "parent restoration before child overlay changed");
		ApplyValues(base, { { "A", { { "node", 7 }, { "node-", 8 } } } });
		Require(a->settings["node"] == 7, "child restoration before parent replacement changed");
		ApplyValues(base, { { "A", { { "node", { { "x", 9 } } } } } });
		a->settings["node"] = 7;
		bool failed = false;
		try {
			ApplyValues(base, { { "A", { { "node", 7 } } } });
		} catch (const std::exception&) {
			failed = true;
		}
		Require(failed && a->settings["node"] == 7, "overlap fast path suppressed a structural exception");
		a->settings["node"] = { { "x", 9 }, { "y", 2 } };
	}

	void CheckOwnershipTypes()
	{
		auto base = Setup({ { "A", { { "value", 1 } } } });
		auto* a = Feature::FindFeatureByShortName("A");
		const Json patch = { { "A", { { "value", 3 } } } };
		ApplyValues(base, patch);
		const auto loads = a->loads;
		authoredBase["A"]["value"] = 1.0;
		base["A"]["value"] = 1.0;
		ApplyValues(base, patch);
		Require(a->loads == loads && saved["A"]["/value"].is_number_float(), "numeric-equal ownership type was not replaced");
	}

	void CheckRollback()
	{
		const auto base = Setup({ { "A", { { "x", 1.0 }, { "extra", 2.0 } } }, { "B", { { "x", 10.0 } } } });
		auto* a = Feature::FindFeatureByShortName("A");
		auto* b = Feature::FindFeatureByShortName("B");
		ApplyValues(base, { { "A", { { "x", 3.0 } } }, { "B", { { "x", 30.0 } } } });
		a->settings["x"] = 7.0;
		b->settings["x"] = 70.0;
		const auto beforeA = a->settings, beforeB = b->settings, beforeOwnership = saved;
		b->throwNext = true;
		bool failed = false;
		try {
			ApplyValues(base, { { "A", { { "extra", 9.0 } } }, { "B", { { "x", 40.0 } } } });
		} catch (const std::exception&) {
			failed = true;
		}
		Require(failed && a->settings == beforeA && b->settings == beforeB && saved == beforeOwnership, "partial load rollback changed live settings or ownership");
		a->throwNext = true;
		failed = false;
		try {
			ApplyValues(base, { { "A", { { "x", 3.0 } } }, { "B", { { "x", 30.0 } } } });
		} catch (const std::exception&) {
			failed = true;
		}
		Require(failed && a->settings == beforeA && b->settings == beforeB && saved == beforeOwnership, "first load failure altered ownership before snapshot");
		b->throwNext = true;
		a->failLoads.insert(a->loads + 2);
		std::string error;
		try {
			ApplyValues(base, { { "A", { { "extra", 9.0 } } }, { "B", { { "x", 40.0 } } } });
		} catch (const std::exception& e) {
			error = e.what();
		}
		Require(error.find("rollback failed") != std::string::npos && saved == beforeOwnership && b->settings == beforeB, "rollback exception handling changed");
	}

	void CheckOwnershipOnlyThenFailure()
	{
		const auto base = Setup({ { "A", { { "x", 1.0 } } }, { "B", { { "x", 10.0 } } } });
		auto* a = Feature::FindFeatureByShortName("A");
		auto* b = Feature::FindFeatureByShortName("B");
		b->throwNext = true;
		bool failed = false;
		try {
			ApplyValues(base, { { "A", { { "x", 1.0 } } }, { "B", { { "x", 20.0 } } } });
		} catch (const std::exception&) {
			failed = true;
		}
		Require(failed && a->loads == 0 && a->settings == base["A"] && b->settings == base["B"] && saved.empty(), "ownership-only mutation was not rolled back");
	}

	void CheckSerializerNormalization()
	{
		const auto base = Setup({ { "A", { { "value", -1 } } } });
		auto* a = Feature::FindFeatureByShortName("A");
		a->normalizeOnSave = true;
		ApplyValues(base, Json::object());
		Require(a->settings["value"] == 0, "unowned settings must still be normalized");
		a->settings["value"] = -2;
		SceneSettingsManager::GetSingleton()->paused = true;
		ApplyValues(base, { { "A", { { "value", 3 } } } });
		Require(a->settings["value"] == 0, "paused settings must still be normalized");
		a->normalizeOnSave = false;
	}

	void CheckMissingAncestorOrder()
	{
		const auto base = Setup({ { "A", { { "node", { { "a", 1 }, { "0", 2 } } } } } });
		auto* a = Feature::FindFeatureByShortName("A");
		ApplyValues(base, { { "A", { { "node", { { "a", 3 } } } } } });
		a->settings["node"] = nullptr;
		ApplyValues(base, { { "A", { { "node", { { "0", 4 } } } } } });
		Require(a->settings["node"].is_object() && a->settings["node"] == Json({ { "a", 1 }, { "0", 4 } }),
			"restoring a missing ancestor must precede numeric-key overlay");
	}

	void CheckOwnershipRepresentation()
	{
		auto base = Setup({ { "A", { { "node", { { "value", 0.0 } } } } } });
		const Json patch = { { "A", { { "node", 2 } } } };
		ApplyValues(base, patch);
		authoredBase["A"]["node"]["value"] = -0.0;
		ApplyValues(base, patch);
		Json exported = Feature::FindFeatureByShortName("A")->settings;
		OverlayBase("A", exported);
		Require(std::signbit(exported["node"]["value"].get<double>()), "saved ownership lost the authored zero sign");
		authoredBase["A"]["node"]["value"] = 0;
		ApplyValues(base, patch);
		OverlayBase("A", exported);
		Require(exported["node"]["value"].is_number_integer(), "nested saved ownership lost its authored type");
	}

	void CheckUnorderedValues()
	{
		const auto base = Setup({ { "A", { { "value", 1 }, { "other", std::numeric_limits<double>::quiet_NaN() } } } });
		auto* a = Feature::FindFeatureByShortName("A");
		ApplyValues(base, Json::object());
		Require(a->loads == 1, "unordered unrelated values changed unowned reload behavior");
		ApplyValues(base, { { "A", { { "value", 1 } } } });
		Require(a->loads == 2, "unordered unrelated values changed owned reload behavior");
		a->settings["other"] = 0;
		ApplyValues(base, { { "A", { { "value", std::numeric_limits<double>::quiet_NaN() } } } });
		Require(a->loads == 3 && std::isnan(a->settings["value"].get<double>()), "unordered field comparison suppressed a load");
	}

	void CheckRestoreBeforePreparation()
	{
		const auto base = Setup({ { "A", { { "node", { { "child", 1 } } } } } });
		auto* a = Feature::FindFeatureByShortName("A");
		ApplyValues(base, { { "A", { { "node", { { "child", 2 } } } } } });
		a->settings["node"] = 3;
		auto prepared = PrepareFields({ { "A", { { "new", 4 } } } });
		bool failed = false;
		try {
			Apply(base, prepared);
		} catch (const std::exception&) {
			failed = true;
		}
		Require(failed && !prepared.at("A").flattened && !fieldAddresses.contains("/new"), "failed restoration prepared new scene fields");
		a->settings["node"] = { { "child", 2 } };
	}

	void Run()
	{
		CheckUnorderedValues();
		CheckRestoreBeforePreparation();
		CheckNoOwnership();
		CheckWeatherAndEdits();
		CheckArraysNullMissing();
		CheckOverlaps();
		CheckOwnershipTypes();
		CheckRollback();
		CheckOwnershipOnlyThenFailure();
		CheckSerializerNormalization();
		CheckMissingAncestorOrder();
		CheckOwnershipRepresentation();
		Reset();
	}
}
