#include "Features/WaterAppearanceMigration.h"
#include "SettingsMigrations.h"

#include <string>

namespace
{
	using json = nlohmann::json;

	const json kAppearanceValues{
		{ "WaterBrightness", 1.25 },
		{ "GlobalReflectionAmount", 0.60 },
		{ "RefractionAmount", 0.70 },
		{ "SunSpecularMultiplier", 1.50 },
		{ "WaveAmplitude", 0.80 },
		{ "FresnelMin", 0.20 },
		{ "FresnelMax", 0.90 },
		{ "Muddiness", 1.10 }
	};

	bool ContainsAppearanceKey(const json& a_value)
	{
		if (!a_value.is_object())
			return false;
		for (const auto key : SettingsMigrations::kLegacyUnifiedWaterAppearanceKeys) {
			if (a_value.contains(key.data()))
				return true;
		}
		return false;
	}

	bool MoveValues(json& a_source, json& a_destination, bool a_forceGlobal)
	{
		return WaterAppearanceMigration::MoveValues(
			a_source,
			a_destination,
			SettingsMigrations::kLegacyWaterAppearanceForceGlobalKey,
			a_forceGlobal,
			SettingsMigrations::kLegacyUnifiedWaterAppearanceKeys);
	}

	bool CoversRecognizedKeyDetection()
	{
		const json malformedSource{ { "Muddiness", "malformed" } };
		const json unrelatedSource{ { "WaterTintStrength", 0.35 } };
		return WaterAppearanceMigration::ContainsAnyKey(
				   malformedSource,
				   SettingsMigrations::kLegacyUnifiedWaterAppearanceKeys) &&
		       !WaterAppearanceMigration::ContainsAnyKey(
				   unrelatedSource,
				   SettingsMigrations::kLegacyUnifiedWaterAppearanceKeys) &&
		       !WaterAppearanceMigration::ContainsAnyKey(
				   json::array(),
				   SettingsMigrations::kLegacyUnifiedWaterAppearanceKeys);
	}

	bool CoversCompleteOneWayMigration()
	{
		auto source = kAppearanceValues;
		source["Enabled"] = true;
		source["WaterTintStrength"] = 0.35;
		json destination = json::object();

		if (!MoveValues(source, destination, true) ||
			ContainsAppearanceKey(source) ||
			!source.value("Enabled", false) ||
			source.value("WaterTintStrength", 0.0) != 0.35 ||
			destination.value(SettingsMigrations::kLegacyWaterAppearanceForceGlobalKey.data(), false) != true)
			return false;

		for (const auto key : SettingsMigrations::kLegacyUnifiedWaterAppearanceKeys) {
			const auto expectedIt = kAppearanceValues.find(key.data());
			const auto actualIt = destination.find(key.data());
			if (expectedIt == kAppearanceValues.end() || actualIt == destination.end() || *actualIt != *expectedIt)
				return false;
		}

		const auto migratedSource = source;
		const auto migratedDestination = destination;
		return !MoveValues(source, destination, true) &&
		       source == migratedSource &&
		       destination == migratedDestination;
	}

	bool CoversPrecedenceAndMalformedCleanup()
	{
		json source{
			{ "WaterBrightness", 1.25 },
			{ "FresnelMin", 0.20 },
			{ "Muddiness", "malformed" }
		};
		json destination{
			{ "WaterBrightness", 0.75 },
			{ "FresnelMin", "malformed" },
			{ std::string(SettingsMigrations::kLegacyWaterAppearanceForceGlobalKey), true }
		};

		if (!MoveValues(source, destination, false))
			return false;

		return !ContainsAppearanceKey(source) &&
		       destination["WaterBrightness"] == 0.75 &&
		       destination["FresnelMin"] == 0.20 &&
		       !destination.contains("Muddiness") &&
		       destination[SettingsMigrations::kLegacyWaterAppearanceForceGlobalKey.data()] == false;
	}

	bool CoversInvalidContainers()
	{
		json source = nullptr;
		json destination = json::object();
		if (MoveValues(source, destination, true))
			return false;

		source = json::object();
		destination = json::array();
		return !MoveValues(source, destination, true);
	}
}

int main()
{
	return CoversRecognizedKeyDetection() &&
	               CoversCompleteOneWayMigration() &&
	               CoversPrecedenceAndMalformedCleanup() &&
	               CoversInvalidContainers() ?
	           0 :
	           1;
}
