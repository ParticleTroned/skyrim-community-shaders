#include "Features/LightLimitFix/ShadowCullingPolicy.h"
#include "Features/SeasonalWeatherConfig.h"
#include "PresetCompatibility.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
	void VerifySeasonalSettings(const nlohmann::json& preset)
	{
		const auto defaults = SeasonalWeatherConfig::Serialize({});
		const auto& settings = preset.at("Seasonal Weather");
		assert(settings.size() == defaults.size());
		for (const auto& [key, value] : defaults.items())
			assert(settings.contains(key));
		assert(SeasonalWeatherConfig::Serialize(SeasonalWeatherConfig::Parse(settings)) == defaults);
		assert(preset.at("Disable at Boot").at("SeasonalWeather") == false);
	}

	nlohmann::json CompatiblePreset()
	{
		return {
			{ "Preset Compatibility",
				{
					{ "contractVersion", 1u },
					{ "presetId", "csx-unified-balanced" },
					{ "presetVersion", "d2026.08.30.2" },
					{ "target",
						{
							{ "runtime", "VR" },
							{ "minimumVersion", "3.21" },
							{ "maximumVersionExclusive", "3.22" },
						} },
					{ "settingsContract",
						{
							{ "revision", PresetCompatibility::kSettingsContractRevision },
							{ "sourceTreeSha256", std::string(64, 'A') },
						} },
				} },
		};
	}
}

int main(int argc, char** argv)
{
	using PresetCompatibility::Disposition;
	assert(argc == 2);
	std::ifstream policyInput(std::filesystem::path(argv[1]) / "docs/development/unified-preset-policy.json");
	assert(policyInput.is_open());
	const auto policy = nlohmann::json::parse(policyInput);
	std::ifstream baseInput(std::filesystem::path(argv[1]) / policy.at("baseTemplate").at("path").get<std::string>());
	assert(baseInput.is_open());
	VerifySeasonalSettings(nlohmann::json::parse(baseInput));
	std::size_t profileCount = 0;
	for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(argv[1]) / "MGO-Presets")) {
		const auto path = entry.path() / "SKSE/Plugins/CommunityShaders/SettingsUser.json";
		if (!entry.is_directory() || !std::filesystem::is_regular_file(path))
			continue;
		std::ifstream input(path);
		assert(input.is_open());
		const auto preset = nlohmann::json::parse(input);
		VerifySeasonalSettings(preset);
		const auto& marker = preset.at("Preset Compatibility");
		assert(marker.at("contractVersion") == policy.at("presetCompatibility").at("contractVersion"));
		assert(marker.at("presetVersion") == policy.at("packageVersion"));
		assert(marker.at("target") == policy.at("presetCompatibility").at("target"));
		assert(marker.at("settingsContract").at("revision") == PresetCompatibility::kSettingsContractRevision);
		assert(marker.at("settingsContract").at("sourceTreeSha256") == policy.at("runtimeSettingsContract").at("sourceTreeSha256"));
		std::ifstream metadataInput(entry.path() / "meta.ini");
		assert(metadataInput.is_open());
		std::size_t versionCount = 0;
		for (std::string line; std::getline(metadataInput, line);) {
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (line.starts_with("version=")) {
				assert(line.substr(8) == marker.at("presetVersion").get<std::string>());
				++versionCount;
			}
		}
		assert(!metadataInput.bad());
		assert(versionCount == 1);
		for (const auto& stalePath : policy.at("validation").at("stalePaths")) {
			nlohmann::json::json_pointer pointer;
			for (const auto& segment : stalePath)
				pointer /= segment.get<std::string>();
			assert(!preset.contains(pointer));
		}
		assert(PresetCompatibility::Evaluate(preset, "CSX 3.21.0-VR").disposition == Disposition::kCompatible);
		for (const auto label : { "CSX 3.20.0-VR", "CSX 3.22.0-VR", "CSX 3.21.0-SE", "CSX 3.21.0-AE" })
			assert(!PresetCompatibility::Evaluate(preset, label).ShouldApply());
		assert(preset.at("Version") == "CSX 3.21.0-VR");
		assert(preset.at("Reverse Z").at("EnableReverseZBuffer") == false);
		assert(preset.at("Fur Shells").at("Enabled") == false);
		assert(preset.at("Snow Cover").at("Enable") == 0);
		for (const auto shortName : { "FurShells", "SnowCover" })
			assert(preset.at("Disable at Boot").at(shortName) == false);
		assert(preset.at("Upscaling").at("antiLag2Enabled") == false);
		assert(preset.at("Light Limit Fix").at("SkipRedundantLightAttach") == true);
		++profileCount;
	}
	assert(profileCount == 10);
	for (const auto tier : { "Performance", "Balanced", "Quality" }) {
		const auto path = std::filesystem::path(argv[1]) / "MGO-Presets" /
		                  (std::string("CSX Unified- ") + tier + " - Press END on PC to Customize") /
		                  "SKSE/Plugins/CommunityShaders/SettingsUser.json";
		std::ifstream input(path);
		assert(input.is_open());
		const auto preset = nlohmann::json::parse(input);
		assert(preset.at("VR").at("WandAimPitchTrimDegrees") == -26.0);
		const auto& nr = preset.at("Neural Rendering").at("rendering");
		assert(nr.at("neuralRenderingEnabled") == false);
		assert(nr.at("neuralRenderingMode") == 2);
		assert(nr.at("neuralRenderingInsertionPoint") == 0);
		assert(nr.at("neuralRenderingNetwork") == 0);
		assert(!nr.contains("neuralRenderingReconstructionFilter"));
		assert(nr.at("neuralRenderingGuidedReconstruction") == true);
		assert(nr.at("neuralRenderingConfidenceRejectionPercent") == 0);
		assert(nr.at("neuralRenderingPeripheralCompressionPercent") == 100);
		assert(nr.at("neuralRenderingModelResolutionPercent") == 90);
		assert(nr.at("neuralRenderingPreset") == 4);
		assert(nr.at("neuralRenderingIntensity") == 1.75);
		assert(nr.at("neuralRenderingLocalTone") == 1.25);
		assert(nr.at("neuralRenderingLocalStructure") == 1.5);
		assert(nr.at("neuralRenderingSkinStructure") == 1.3);
		assert(nr.at("neuralRenderingPauseInCombat") == true);
		assert(preset.at("Terrain Blending").at("LandscapeSeams") == 0);
		assert(preset.at("Performance Overlay").at("ShowRenderTargets") == false);
		const auto& shadowCulling = preset.at("Light Limit Fix").at("ShadowCulling");
		for (const auto field : { "smallActorParts", "actorPartDistanceMeters", "actorPartReturnDistanceMeters", "actorPartRadiusCentimeters" })
			assert(shadowCulling.contains(field));
		assert(shadowCulling.get<LightLimitFixDetail::ShadowCulling::Settings>() == LightLimitFixDetail::ShadowCulling::Settings{});
		assert(preset.at("Sky Sync").at("DisabledWorldspaces") == nlohmann::json::array(
																	  { "Blackreach", "AlftandWorld", "Sovngarde", "DLC01SoulCairn", "DLC01Boneyard", "DLC2ApocryphaWorld" }));
	}

	const auto unmarked = PresetCompatibility::Evaluate(nlohmann::json::object(), "CSX 3.21.0-VR");
	assert(unmarked.disposition == Disposition::kUnmarked);
	assert(unmarked.ShouldApply());

	const auto compatible = PresetCompatibility::Evaluate(CompatiblePreset(), "CSX 3.21.0-VR");
	assert(compatible.disposition == Disposition::kCompatible);
	assert(compatible.ShouldApply());
	assert(compatible.presetId == "csx-unified-balanced");

	const auto older = PresetCompatibility::Evaluate(CompatiblePreset(), "CSX 3.18-VR");
	assert(older.disposition == Disposition::kRejected);
	assert(!older.ShouldApply());

	for (const auto presetId : { "csx-unified-performance", "csx-unified-balanced", "csx-unified-quality" }) {
		for (const auto revision : { 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u }) {
			auto legacy = CompatiblePreset();
			auto& marker = legacy["Preset Compatibility"];
			marker["presetId"] = presetId;
			marker["settingsContract"]["revision"] = revision;
			marker["target"]["minimumVersion"] = "3.20";
			marker["target"]["maximumVersionExclusive"] = "3.21";
			assert(PresetCompatibility::Evaluate(legacy, "CSX 3.20.0-VR").ShouldApply());
			assert(PresetCompatibility::Evaluate(legacy, "CSX 3.21.0-VR").ShouldApply());
			assert(PresetCompatibility::Evaluate(legacy, "CSX 3.21.1-VR").ShouldApply());
			assert(!PresetCompatibility::Evaluate(legacy, "CSX 3.22.0-VR").ShouldApply());
			assert(!PresetCompatibility::Evaluate(legacy, "CSX 3.21.0-SE").ShouldApply());
			auto olderRange = legacy;
			olderRange["Preset Compatibility"]["target"]["minimumVersion"] = "3.19";
			olderRange["Preset Compatibility"]["target"]["maximumVersionExclusive"] = "3.20";
			assert(PresetCompatibility::Evaluate(olderRange, "CSX 3.20.0-VR").ShouldApply() == (revision == 8));
			assert(PresetCompatibility::Evaluate(olderRange, "CSX 3.21.0-VR").ShouldApply() == (revision == 8));
			assert(!PresetCompatibility::Evaluate(olderRange, "CSX 3.22.0-VR").ShouldApply());
			marker["presetId"] = "another-preset";
			assert(!PresetCompatibility::Evaluate(legacy, "CSX 3.21.0-VR").ShouldApply());
		}
	}
	for (const auto presetId : { "csx-unified-performance", "csx-unified-balanced", "csx-unified-quality" }) {
		for (auto revision = 15u; revision < PresetCompatibility::kSettingsContractRevision; ++revision) {
			auto legacy = CompatiblePreset();
			auto& marker = legacy["Preset Compatibility"];
			marker["presetId"] = presetId;
			marker["settingsContract"]["revision"] = revision;
			marker["target"]["minimumVersion"] = "3.21";
			marker["target"]["maximumVersionExclusive"] = "3.22";
			assert(PresetCompatibility::Evaluate(legacy, "CSX 3.21.0-VR").ShouldApply());
			assert(PresetCompatibility::Evaluate(legacy, "CSX 3.21.1-VR").ShouldApply());
			for (const auto label : { "CSX 3.20.0-VR", "CSX 3.22.0-VR", "CSX 3.21.0-SE", "CSX 3.21.0-AE" }) {
				assert(!PresetCompatibility::Evaluate(legacy, label).ShouldApply());
			}
			auto widerRange = legacy;
			widerRange["Preset Compatibility"]["target"]["maximumVersionExclusive"] = "3.23";
			assert(!PresetCompatibility::Evaluate(widerRange, "CSX 3.21.0-VR").ShouldApply());
			marker["presetId"] = "another-preset";
			assert(!PresetCompatibility::Evaluate(legacy, "CSX 3.21.0-VR").ShouldApply());
		}
	}
	for (const auto revision : { 5u, 7u, 14u, PresetCompatibility::kSettingsContractRevision + 1 }) {
		auto incompatibleContract = CompatiblePreset();
		incompatibleContract["Preset Compatibility"]["settingsContract"]["revision"] = revision;
		assert(!PresetCompatibility::Evaluate(incompatibleContract, "CSX 3.21.0-VR").ShouldApply());
	}
	const auto newer = PresetCompatibility::Evaluate(CompatiblePreset(), "CSX 3.22.0-VR");
	assert(newer.disposition == Disposition::kRejected);
	auto thirdParty = CompatiblePreset();
	thirdParty["Preset Compatibility"]["presetId"] = "another-preset";
	thirdParty["Preset Compatibility"]["settingsContract"]["revision"] = 8u;
	assert(!PresetCompatibility::Evaluate(thirdParty, "CSX 3.21.0-VR").ShouldApply());
	for (const auto label : { "CSX 3.21.x-VR", "CSX 3.21.0.1-VR", "CSX 3.21.-VR", "CSX 3.21.4294967296-VR" }) {
		assert(!PresetCompatibility::Evaluate(CompatiblePreset(), label).ShouldApply());
	}
	auto invalidRange = CompatiblePreset();
	invalidRange["Preset Compatibility"]["target"]["minimumVersion"] = "3.19.0";
	assert(!PresetCompatibility::Evaluate(invalidRange, "CSX 3.21.0-VR").ShouldApply());

	const auto wrongRuntime = PresetCompatibility::Evaluate(CompatiblePreset(), "CSX 3.21.0-SE");
	assert(wrongRuntime.disposition == Disposition::kRejected);

	auto futureContract = CompatiblePreset();
	futureContract["Preset Compatibility"]["contractVersion"] = 2u;
	const auto unsupported = PresetCompatibility::Evaluate(futureContract, "CSX 3.21.0-VR");
	assert(unsupported.disposition == Disposition::kRejected);

	auto malformed = CompatiblePreset();
	malformed["Preset Compatibility"]["target"].erase("maximumVersionExclusive");
	const auto invalid = PresetCompatibility::Evaluate(malformed, "CSX 3.21.0-VR");
	assert(invalid.disposition == Disposition::kRejected);

	auto previousSettings = CompatiblePreset();
	previousSettings["Preset Compatibility"]["presetId"] = "another-preset";
	previousSettings["Preset Compatibility"]["settingsContract"]["revision"] = PresetCompatibility::kSettingsContractRevision - 1;
	assert(!PresetCompatibility::Evaluate(previousSettings, "CSX 3.21.0-VR").ShouldApply());

	auto futureSettings = CompatiblePreset();
	futureSettings["Preset Compatibility"]["settingsContract"]["revision"] = PresetCompatibility::kSettingsContractRevision + 1;
	const auto unsupportedSettings = PresetCompatibility::Evaluate(futureSettings, "CSX 3.21.0-VR");
	assert(unsupportedSettings.disposition == Disposition::kRejected);
	for (const auto key : { "contractVersion", "revision" }) {
		auto oversized = CompatiblePreset();
		auto& marker = oversized["Preset Compatibility"];
		if (std::string_view(key) == "contractVersion")
			marker[key] = (std::uint64_t{ 1 } << 32) + PresetCompatibility::kContractVersion;
		else
			marker["settingsContract"][key] = (std::uint64_t{ 1 } << 32) + PresetCompatibility::kSettingsContractRevision;
		assert(!PresetCompatibility::Evaluate(oversized, "CSX 3.21.0-VR").ShouldApply());
	}

	auto invalidSourceHash = CompatiblePreset();
	invalidSourceHash["Preset Compatibility"]["settingsContract"]["sourceTreeSha256"] = std::string(64, 'Z');
	const auto malformedSourceHash = PresetCompatibility::Evaluate(invalidSourceHash, "CSX 3.21.0-VR");
	assert(malformedSourceHash.disposition == Disposition::kRejected);

	PresetCompatibility::Publish(compatible);
	const auto published = PresetCompatibility::GetPublished();
	assert(published.disposition == Disposition::kCompatible);
	assert(PresetCompatibility::ToJson(published).at("shouldApply").get<bool>());
}
