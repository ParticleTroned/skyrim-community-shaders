#include "PresetCompatibility.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <string>

namespace
{
	nlohmann::json CompatiblePreset()
	{
		return {
			{ "Preset Compatibility",
				{
					{ "contractVersion", 1 },
					{ "presetId", "csx-unified-balanced" },
					{ "presetVersion", "d2026.08.30.2" },
					{ "target",
						{
							{ "runtime", "VR" },
							{ "minimumVersion", "3.19" },
							{ "maximumVersionExclusive", "3.20" },
						} },
					{ "settingsContract",
						{
							{ "revision", 3 },
							{ "sourceTreeSha256", std::string(64, 'A') },
						} },
				} },
		};
	}
}

int main()
{
	using PresetCompatibility::Disposition;

	const auto unmarked = PresetCompatibility::Evaluate(nlohmann::json::object(), "CSX 3.19-VR");
	assert(unmarked.disposition == Disposition::kUnmarked);
	assert(unmarked.ShouldApply());

	const auto compatible = PresetCompatibility::Evaluate(CompatiblePreset(), "CSX 3.19-VR");
	assert(compatible.disposition == Disposition::kCompatible);
	assert(compatible.ShouldApply());
	assert(compatible.presetId == "csx-unified-balanced");

	const auto older = PresetCompatibility::Evaluate(CompatiblePreset(), "CSX 3.18-VR");
	assert(older.disposition == Disposition::kRejected);
	assert(!older.ShouldApply());

	const auto newer = PresetCompatibility::Evaluate(CompatiblePreset(), "CSX 3.20-VR");
	assert(newer.disposition == Disposition::kRejected);

	const auto wrongRuntime = PresetCompatibility::Evaluate(CompatiblePreset(), "CSX 3.19-SE");
	assert(wrongRuntime.disposition == Disposition::kRejected);

	auto futureContract = CompatiblePreset();
	futureContract["Preset Compatibility"]["contractVersion"] = 2;
	const auto unsupported = PresetCompatibility::Evaluate(futureContract, "CSX 3.19-VR");
	assert(unsupported.disposition == Disposition::kRejected);

	auto malformed = CompatiblePreset();
	malformed["Preset Compatibility"]["target"].erase("maximumVersionExclusive");
	const auto invalid = PresetCompatibility::Evaluate(malformed, "CSX 3.19-VR");
	assert(invalid.disposition == Disposition::kRejected);

	auto futureSettings = CompatiblePreset();
	futureSettings["Preset Compatibility"]["settingsContract"]["revision"] = 4;
	const auto unsupportedSettings = PresetCompatibility::Evaluate(futureSettings, "CSX 3.19-VR");
	assert(unsupportedSettings.disposition == Disposition::kRejected);

	auto invalidSourceHash = CompatiblePreset();
	invalidSourceHash["Preset Compatibility"]["settingsContract"]["sourceTreeSha256"] = std::string(64, 'Z');
	const auto malformedSourceHash = PresetCompatibility::Evaluate(invalidSourceHash, "CSX 3.19-VR");
	assert(malformedSourceHash.disposition == Disposition::kRejected);

	PresetCompatibility::Publish(compatible);
	const auto published = PresetCompatibility::GetPublished();
	assert(published.disposition == Disposition::kCompatible);
	assert(PresetCompatibility::ToJson(published).at("shouldApply").get<bool>());
}
