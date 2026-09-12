#include "Feature.h"

#include <algorithm>

#include "FeatureIssues.h"
#include "FeatureVersions.h"
#include "Features/AdaptiveBrightness.h"
#include "Features/CSEditor.h"
#include "Features/CSUtility.h"
#include "Features/CloudShadows.h"
#include "Features/DynamicCubemaps.h"
#include "Features/ExtendedMaterials.h"
#include "Features/ExtendedTranslucency.h"
#include "Features/FoliageLighting.h"
#include "Features/GrassCollision.h"
#include "Features/GrassLighting.h"
#include "Features/HairSpecular.h"
#include "Features/HorizonFix.h"
#include "Features/IBL.h"
#include "Features/InteriorSun.h"
#include "Features/InverseSquareLighting.h"
#include "Features/LODBlending.h"
#include "Features/LightLimitFix.h"
#include "Features/LinearLighting.h"
#include "Features/PerformanceOverlay.h"
#include "Features/RenderDoc.h"
#include "Features/ScreenSpaceGI.h"
#include "Features/ScreenSpaceShadows.h"
#include "Features/ScreenshotFeature.h"
#include "Features/SkySync.h"
#include "Features/Skylighting.h"
#include "Features/SubsurfaceScattering.h"
#include "Features/TerrainBlending.h"
#include "Features/TerrainHelper.h"
#include "Features/TerrainShadows.h"
#include "Features/TerrainVariation.h"
#include "Features/UnifiedWater.h"
#include "Features/Upscaling.h"
#include "Features/VR.h"
#include "Features/VolumetricLighting.h"
#include "Features/VolumetricShadows.h"
#include "Features/WaterEffects.h"
#include "Features/WeatherPicker.h"
#include "Features/WetnessEffects.h"
#include "Features/Wetterness.h"
#include "Menu.h"
#include "SettingsOverrideManager.h"
#include "Utils/Format.h"
#include "WeatherManager.h"
#include "WeatherVariableRegistry.h"

#include "State.h"
#include "TruePBR.h"

void Feature::Load(json& o_json)
{
	// AIO packages can contain feature INIs for every runtime. Keep an
	// unsupported feature unloaded unless developer mode explicitly exposes it.
	if (REL::Module::IsVR() && !SupportsVR() && !globals::state->IsDeveloperMode()) {
		loaded = false;
		logger::info("{} does not support VR, feature disabled", GetShortName());
		return;
	}

	// Convert string to wstring
	auto ini_filename = std::format("{}.ini", GetShortName());
	std::wstring ini_filename_w;
	std::ranges::copy(ini_filename, std::back_inserter(ini_filename_w));
	auto ini_path = L"Data\\Shaders\\Features\\" + ini_filename_w;

	CSimpleIniA ini;
	ini.SetUnicode();
	SI_Error rc = ini.LoadFile(ini_path.c_str());

	if (rc < 0) {
		if (!FeatureIssues::IsObsoleteFeature(GetShortName()))
			logger::info("{} failed to load, feature disabled", ini_filename);
		loaded = false;
		return;
	}

	bool hasError = false;
	std::string errorVersion;
	FeatureIssues::FeatureIssueInfo::IssueType errorType = FeatureIssues::FeatureIssueInfo::IssueType::UNKNOWN;

	if (FeatureIssues::IsObsoleteFeature(GetShortName())) {
		hasError = true;
		errorVersion = "N/A";
		errorType = FeatureIssues::FeatureIssueInfo::IssueType::OBSOLETE;
		failedLoadedMessage = std::format("{} is an obsolete feature that has been removed", GetShortName());
	} else if (auto value = ini.GetValue("Info", "Version")) {
		try {
			REL::Version featureVersion(std::regex_replace(value, std::regex("-"), "."));

			// Check if feature exists in minimal versions
			REL::Version minimalFeatureVersion;
			if (!Feature::IsFeatureKnown(GetShortName(), &minimalFeatureVersion)) {
				hasError = true;
				errorVersion = value;
				errorType = FeatureIssues::FeatureIssueInfo::IssueType::UNKNOWN;
				failedLoadedMessage = std::format("{} {} is an unknown feature not supported by this CSX version. This may be a feature from a development branch.", GetShortName(), value);
			} else {
				// Version compatibility check
				bool oldFeature = featureVersion.compare(minimalFeatureVersion) == std::strong_ordering::less;
				bool majorVersionMismatch = featureVersion.major() < minimalFeatureVersion.major();

				if (!oldFeature && !majorVersionMismatch) {
					loaded = true;
					logger::info("{} {} successfully loaded", ini_filename, value);
				} else {
					hasError = true;
					errorVersion = value;
					errorType = FeatureIssues::FeatureIssueInfo::IssueType::VERSION_MISMATCH;

					std::string minimalVersionString = Util::GetFormattedVersion(minimalFeatureVersion);

					if (IsCore()) {
						failedLoadedMessage = std::format("This feature is already included as part of the core CSX installation. Uninstall this feature with your mod manager.");
					} else if (majorVersionMismatch) {
						failedLoadedMessage = std::format("{} {} is too old, major version incompatibility detected. Required: {}", GetShortName(), value, minimalVersionString);
					} else {
						failedLoadedMessage = std::format("{} {} is an old feature version, required: {}", GetShortName(), value, minimalVersionString);
					}
				}
			}

			version = value;
		} catch (const std::exception& e) {
			hasError = true;
			errorVersion = value;
			errorType = FeatureIssues::FeatureIssueInfo::IssueType::VERSION_MISMATCH;
			failedLoadedMessage = std::format("{} {} has invalid version format: {}", GetShortName(), value, e.what());
		}
	} else {
		hasError = true;
		errorVersion = "unknown";
		errorType = FeatureIssues::FeatureIssueInfo::IssueType::VERSION_MISMATCH;

		// Get the minimum required version to include in the error message
		std::string requiredVersion = Feature::GetFeatureRequiredVersion(GetShortName());

		failedLoadedMessage = std::format("This feature is not installed! Version required: {}", ini_filename, requiredVersion);
	}

	if (hasError) {
		loaded = false;
		if (IsHiddenFromUserView()) {
			logger::info("Hidden feature '{}' failed to load: {}", GetShortName(), failedLoadedMessage);
			return;
		}

		logger::warn("{}", failedLoadedMessage);

		// Guard against empty shortName to prevent bogus filesystem access
		std::string shortName = GetShortName();
		if (!shortName.empty()) {
			FeatureIssues::FeatureFileInfo fileInfo = FeatureIssues::GetFeatureFileInfo(shortName);

			// For version mismatch, also pass the minimum required version
			std::string minimumVersion;
			if (errorType == FeatureIssues::FeatureIssueInfo::IssueType::VERSION_MISMATCH) {
				minimumVersion = Feature::GetFeatureRequiredVersion(shortName);
			}

			FeatureIssues::AddFeatureIssue(shortName, errorVersion, failedLoadedMessage, errorType, fileInfo, minimumVersion);

		} else {
			logger::error("Feature has empty short name, cannot add to feature issues list");
		}
	} else {
		// No errors, load settings now
		if (HasFeatureSettings()) {
			if (o_json[GetName()].is_structured()) {
				logger::info("Loading {} settings", GetName());
				try {
					LoadSettings(o_json[GetName()]);
				} catch (...) {
					logger::warn("Invalid settings for {}, using default.", GetName());
					RestoreDefaultSettings();
				}
			} else {
				logger::info("Loading default settings for {}", GetName());
				RestoreDefaultSettings();
			}
		}
	}
}

void Feature::Save(json& o_json)
{
	if (!HasFeatureSettings()) {
		return;
	}

	SaveSettings(o_json[GetName()]);
}

bool Feature::ValidateCache(CSimpleIniA& a_ini)
{
	auto name = GetName();
	auto ini_name = GetShortName();

	logger::info("Validating {}", name);

	auto enabledInCache = a_ini.GetBoolValue(ini_name.c_str(), "Enabled", false);
	if (enabledInCache && !loaded) {
		logger::info("Feature was uninstalled");
		return false;
	}
	if (!enabledInCache && loaded) {
		logger::info("Feature was installed");
		return false;
	}

	if (loaded) {
		const auto shaderAbi = GetShaderCacheAbiVersion();
		const auto shaderAbiInCache = a_ini.GetValue(ini_name.c_str(), "ShaderCacheABI");
		const std::string_view cachedShaderAbi = shaderAbiInCache ? shaderAbiInCache : "";
		if (cachedShaderAbi != shaderAbi) {
			logger::info(
				"Shader cache contract changed for {}. Installed {} but {} in Disk Cache",
				name,
				shaderAbi.empty() ? "<none>" : shaderAbi,
				cachedShaderAbi.empty() ? "<none>" : cachedShaderAbi);
			return false;
		}
	}

	logger::info("Cached feature is valid");
	return true;
}

void Feature::WriteDiskCacheInfo(CSimpleIniA& a_ini)
{
	auto ini_name = GetShortName();
	a_ini.SetBoolValue(ini_name.c_str(), "Enabled", loaded);
	a_ini.SetValue(ini_name.c_str(), "Version", version.c_str());
	const auto shaderAbi = GetShaderCacheAbiVersion();
	if (!shaderAbi.empty())
		a_ini.SetValue(ini_name.c_str(), "ShaderCacheABI", std::string(shaderAbi).c_str());
	else
		a_ini.Delete(ini_name.c_str(), "ShaderCacheABI");
}

namespace
{
	const std::vector<Feature*>& GetAllFeatures()
	{
		static std::vector<Feature*> features = {
			&globals::features::truePBR,
			&globals::features::foliageLighting,
			&globals::features::adaptiveBrightness,
			&globals::features::grassLighting,
			&globals::features::grassCollision,
			&globals::features::screenSpaceShadows,
			&globals::features::extendedMaterials,
			&globals::features::wetnessEffects,
			&globals::features::wetterness,
			&globals::features::lightLimitFix,
			&globals::features::dynamicCubemaps,
			&globals::features::cloudShadows,
			&globals::features::waterEffects,
			&globals::features::performanceOverlay,
			&globals::features::subsurfaceScattering,
			&globals::features::terrainShadows,
			&globals::features::screenSpaceGI,
			&globals::features::skylighting,
			&globals::features::skySync,
			&globals::features::terrainBlending,
			&globals::features::terrainHelper,
			&globals::features::volumetricLighting,
			&globals::features::volumetricShadows,
			&globals::features::lodBlending,
			&globals::features::inverseSquareLighting,
			&globals::features::hairSpecular,
			&globals::features::interiorSun,
			&globals::features::terrainVariation,
			&globals::features::ibl,
			&globals::features::extendedTranslucency,
			&globals::features::upscaling,
			&globals::features::renderDoc,
			&globals::features::csEditor,
			&globals::features::weatherPicker,
			&globals::features::screenshotFeature,
			&globals::features::linearLighting,
			&globals::features::unifiedWater,
			&globals::features::horizonFix,
			&globals::features::csUtility
		};
		return features;
	}
}

const std::vector<Feature*>& Feature::GetFeatureList()
{
	if (REL::Module::IsVR()) {
		// Helper function to build VR feature list
		static auto BuildVRList = []() -> std::vector<Feature*> {
			auto v = GetAllFeatures();
			v.push_back(&globals::features::vr);

			// In developer mode, keep all features for testing
			// In production mode, filter to VR-compatible only
			if (!globals::state->IsDeveloperMode()) {
				std::erase_if(v, [](Feature* a) { return !a->SupportsVR(); });
			}
			return v;
		};

		// Cache the VR feature list but invalidate when developer mode changes
		static std::vector<Feature*> featuresVR;
		static bool cachedDevMode = false;

		bool currentDevMode = globals::state->IsDeveloperMode();
		if (featuresVR.empty() || currentDevMode != cachedDevMode) {
			featuresVR = BuildVRList();
			cachedDevMode = currentDevMode;
		}

		return featuresVR;
	} else {
		return GetAllFeatures();
	}
}

Feature* Feature::FindFeatureByShortName(const std::string& shortName)
{
	for (auto* feature : GetFeatureList()) {
		if (feature->loaded && feature->GetShortName() == shortName)
			return feature;
	}
	return nullptr;
}

Feature* Feature::FindRegisteredFeatureByShortName(const std::string& shortName)
{
	for (auto* feature : GetAllFeatures()) {
		if (feature->GetShortName() == shortName)
			return feature;
	}
	if (shortName == "VR")
		return &globals::features::vr;
	return nullptr;
}

std::vector<std::string> Feature::GetLoadedFeatureNames()
{
	std::vector<std::string> names;
	for (auto* feature : GetFeatureList()) {
		if (feature->loaded && feature->IsInMenu())
			names.push_back(feature->GetShortName());
	}
	std::sort(names.begin(), names.end());
	return names;
}

bool Feature::ToggleAtBootSetting()
{
	auto state = globals::state;
	const std::string featureName = GetShortName();
	auto disabled = state->IsFeatureDisabled(featureName);
	state->SetFeatureDisabled(featureName, !disabled);

	return state->IsFeatureDisabled(featureName);  // Return the new state
}

bool Feature::ReapplyOverrideSettings()
{
	auto overrideManager = SettingsOverrideManager::GetSingleton();
	std::string featureName = GetShortName();

	if (!overrideManager || !overrideManager->HasFeatureOverrides(featureName)) {
		return false;
	}

	// Get base settings and apply overrides fresh
	json originalSettings;
	auto* weatherRegistry =
		WeatherVariables::GlobalWeatherRegistry::GetSingleton();
	auto* weatherManager = WeatherManager::GetSingleton();
	const bool hasWeatherSupport =
		weatherRegistry->HasWeatherSupport(featureName);
	const auto synchronizeWeatherBaseline = [&]() {
		if (!hasWeatherSupport)
			return;
		weatherRegistry->CaptureFeatureUserSettings(featureName);
		weatherManager->NotifyUserSettingsChanged();
		weatherManager->RefreshFeatureOverrides();
	};
	const auto activeVariables =
		weatherManager->GetActiveVariablesForFeature(featureName);
	weatherRegistry->SerializeFeatureUserSettings(featureName, activeVariables, [&]() {
		SaveSettings(originalSettings);
	});

	// Apply overrides to the settings (without user customizations)
	json featureJson = originalSettings;
	size_t appliedCount = overrideManager->ReapplyFeatureOverrides(featureName, featureJson);

	if (appliedCount == 0)
		return false;

	const auto restoreOriginalSettings = [&]() noexcept {
		try {
			LoadSettings(originalSettings);
			synchronizeWeatherBaseline();
		} catch (const std::exception& e) {
			logger::error(
				"Failed to restore settings after applying overrides for {}: {}",
				featureName, e.what());
		} catch (...) {
			logger::error(
				"Failed to restore settings after applying overrides for {}",
				featureName);
		}
	};

	try {
		LoadSettings(featureJson);
	} catch (const std::exception& e) {
		logger::warn(
			"Failed to validate reapplied override settings for {}: {}",
			featureName, e.what());
		restoreOriginalSettings();
		return false;
	} catch (...) {
		logger::warn(
			"Failed to validate reapplied override settings for {}",
			featureName);
		restoreOriginalSettings();
		return false;
	}

	// Do not discard the user's persisted customizations unless an override was
	// successfully prepared and the file can actually be removed.
	if (!overrideManager->DeleteUserOverride(featureName)) {
		restoreOriginalSettings();
		return false;
	}

	synchronizeWeatherBaseline();
	return true;
}

void Feature::DrawUnloadedUI()
{
	// Prioritize detailed failure message if available
	if (!failedLoadedMessage.empty()) {
		// Use error color for all failure messages
		auto& themeSettings = Menu::GetSingleton()->GetTheme();
		ImGui::TextColored(themeSettings.StatusPalette.Error, failedLoadedMessage.c_str());
		return;
	}

	// Fallback: Always show missing file message when no specific failure message exists
	auto& themeSettings = Menu::GetSingleton()->GetTheme();
	auto ini_filename = std::format("{}.ini", GetShortName());
	// Get the minimum required version to include in the error message
	std::string requiredVersion = Feature::GetFeatureRequiredVersion(GetShortName());

	auto missingFileMessage = std::format(
		"This feature is not installed or may not yet be available! Version required: {} version {}",
		ini_filename,
		requiredVersion);
	ImGui::TextColored(themeSettings.StatusPalette.Error, missingFileMessage.c_str());

	// Also show feature summary if available
	auto [description, keyFeatures] = GetFeatureSummary();
	if (!description.empty()) {
		ImGui::Spacing();
		ImGui::TextWrapped("%s", description.c_str());
	}

	if (!keyFeatures.empty()) {
		if (description.empty()) {
			ImGui::Spacing();
		}
		ImGui::TextWrapped("Key features:");
		for (const auto& feature : keyFeatures) {
			ImGui::BulletText("%s", feature.c_str());
		}
	}
}

std::string Feature::GetFeatureRequiredVersion(const std::string& shortName)
{
	if (shortName.empty()) {
		return "unknown";
	}
	auto iter = FeatureVersions::FEATURE_MINIMAL_VERSIONS.find(shortName);
	if (iter != FeatureVersions::FEATURE_MINIMAL_VERSIONS.end()) {
		return Util::GetFormattedVersion(iter->second);
	}

	return "unknown";
}

bool Feature::IsFeatureKnown(const std::string& shortName, REL::Version* outVersion)
{
	if (shortName.empty()) {
		return false;
	}

	auto iter = FeatureVersions::FEATURE_MINIMAL_VERSIONS.find(shortName);
	if (iter != FeatureVersions::FEATURE_MINIMAL_VERSIONS.end()) {
		if (outVersion) {
			*outVersion = iter->second;
		}
		return true;
	}

	return false;
}
