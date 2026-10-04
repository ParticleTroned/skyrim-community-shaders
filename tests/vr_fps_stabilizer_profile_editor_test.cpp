#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <ranges>
#include <source_location>
#include <stdexcept>
#include <string>
#include <utility>

namespace VRFpsStabilizer
{
	enum class ConfigFile
	{
		Main
	};
	struct IniDocument
	{
		std::string original;
	};
	struct ReloadStatus
	{
		uint64_t revision = 1;
	};
	bool Load(ConfigFile, IniDocument&, std::string&) { return true; }
	ReloadStatus Status() { return {}; }
}

struct Upscaling
{
	static constexpr uint32_t kQualityModeMaxIndex = 6;
	enum class UpscaleMethod
	{
		kNONE,
		kTAA,
		kFSR,
		kDLSS
	};
	struct VRFpsStabilizerProfile
	{
		bool hasUpscaleMethod = false;
		UpscaleMethod upscaleMethod = UpscaleMethod::kNONE;
		bool hasLegacyMethodSelection = false;
		bool hasQualityMode = false;
		uint32_t qualityMode = 0;
		bool hasDLSSPreset = false;
		uint32_t dlssPreset = 1;
		bool hasRenderScaleMode = false;
		bool renderScaleMode = false;
		bool hasScreenSpaceShadows = false;
		bool screenSpaceShadowsEnabled = false;
		bool hasScreenSpaceGI = false;
		bool screenSpaceGIEnabled = false;
		bool hasVolumetricLightingExterior = false;
		bool volumetricLightingExteriorEnabled = false;
		bool hasContactShadows = false;
		bool contactShadowsEnabled = false;
		uint32_t invalidSettingCount = 0;
		bool HasAnySetting() const
		{
			return hasUpscaleMethod || hasLegacyMethodSelection || hasQualityMode ||
			       hasDLSSPreset || hasRenderScaleMode || hasScreenSpaceShadows ||
			       hasScreenSpaceGI || hasVolumetricLightingExterior || hasContactShadows;
		}
		bool operator==(const VRFpsStabilizerProfile&) const = default;
	};
	struct VRFpsStabilizerConfig
	{
		bool upscalingSwitchingEnabled = true;
		float fadeDuration = 0.0f;
		VRFpsStabilizerProfile interior;
		VRFpsStabilizerProfile exterior;
		bool HasAnyProfile() const { return interior.HasAnySetting() || exterior.HasAnySetting(); }
		bool operator==(const VRFpsStabilizerConfig&) const = default;
	};
	VRFpsStabilizerConfig source;
	bool LoadVRFpsStabilizerConfig(VRFpsStabilizerConfig& out, std::string&)
	{
		out = source;
		return true;
	}
};
namespace globals::features
{
	Upscaling upscaling;
}
bool vrFpsStabilizerProfilesDirty = false;
uint32_t ClampQualityModeUInt(uint32_t value) { return std::min(value, Upscaling::kQualityModeMaxIndex); }

#include "stabilizer_profile_editor_under_test.h"

namespace
{
	int checks = 0;
	void Require(bool value, const std::source_location where = std::source_location::current())
	{
		++checks;
		if (!value)
			throw std::runtime_error("Failed at line " + std::to_string(where.line()));
	}
}

int main()
{
	try {
		constexpr std::array<uint32_t, 7> expectedPresetValues{ 0, 5, 6, 1, 2, 3, 4 };
		for (uint32_t quality = 0; quality < expectedPresetValues.size(); ++quality) {
			const auto preset = VRFpsStabilizerQualityModeToUpscalePreset(quality);
			Require(preset == expectedPresetValues[quality]);
			uint32_t parsedQuality = 99;
			Require(TryVRFpsStabilizerUpscalePresetToQualityMode(preset, parsedQuality));
			Require(parsedQuality == quality);
		}
		uint32_t parsedQuality = 99;
		Require(!TryVRFpsStabilizerUpscalePresetToQualityMode(7, parsedQuality));

		constexpr uint32_t selections = 4 * (Upscaling::kQualityModeMaxIndex + 1);
		for (uint32_t interiorSelection = 0; interiorSelection < selections; ++interiorSelection) {
			for (uint32_t exteriorSelection = 0; exteriorSelection < selections; ++exteriorSelection) {
				for (uint32_t renderScaleMask = 0; renderScaleMask < 4; ++renderScaleMask) {
					for (uint32_t features = 0; features < 16; ++features) {
						auto& source = globals::features::upscaling.source;
						source = {};
						auto& interior = source.interior;
						interior.upscaleMethod = static_cast<Upscaling::UpscaleMethod>(
							interiorSelection / (Upscaling::kQualityModeMaxIndex + 1));
						interior.qualityMode = interiorSelection % (Upscaling::kQualityModeMaxIndex + 1);
						interior.dlssPreset = interiorSelection % 5;
						interior.renderScaleMode = (renderScaleMask & 1) != 0;
						interior.screenSpaceShadowsEnabled = (features & 1) != 0;
						interior.screenSpaceGIEnabled = (features & 2) != 0;
						interior.volumetricLightingExteriorEnabled = (features & 4) != 0;
						interior.contactShadowsEnabled = (features & 8) != 0;
						source.exterior = interior;
						source.exterior.upscaleMethod = static_cast<Upscaling::UpscaleMethod>(
							exteriorSelection / (Upscaling::kQualityModeMaxIndex + 1));
						source.exterior.qualityMode = exteriorSelection % (Upscaling::kQualityModeMaxIndex + 1);
						source.exterior.dlssPreset = exteriorSelection % 5;
						source.exterior.renderScaleMode = (renderScaleMask & 2) != 0;
						source.exterior.screenSpaceShadowsEnabled = !interior.screenSpaceShadowsEnabled;
						source.exterior.screenSpaceGIEnabled = !interior.screenSpaceGIEnabled;
						source.exterior.volumetricLightingExteriorEnabled = !interior.volumetricLightingExteriorEnabled;
						source.exterior.contactShadowsEnabled = !interior.contactShadowsEnabled;
						const auto expected = source;
						VRFpsStabilizerUIState state;
						LoadVRFpsStabilizerUIState(state);
						Require(!state.loadFailed && !state.profilesDefinedInIni && !state.config.upscalingSwitchingEnabled);
						Require(state.config.interior == expected.interior);
						Require(state.config.exterior == expected.exterior);
						Require(state.baselineConfig == state.config && !state.dirty && !vrFpsStabilizerProfilesDirty);
					}
				}
			}
		}
		auto& source = globals::features::upscaling.source;
		source.interior.hasUpscaleMethod = true;
		VRFpsStabilizerUIState configured;
		LoadVRFpsStabilizerUIState(configured);
		Require(configured.profilesDefinedInIni && configured.config == source);
		source.interior.hasUpscaleMethod = false;
		source.exterior.invalidSettingCount = 1;
		VRFpsStabilizerUIState invalid;
		LoadVRFpsStabilizerUIState(invalid);
		Require(invalid.profilesDefinedInIni && invalid.config == source);
		std::cout << checks << " Stabilizer profile initialization and preset checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
