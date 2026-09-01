#include "AdaptiveBrightness.h"

#include "Globals.h"
#include "InverseSquareLighting.h"
#include "LocationContext.h"
#include "SettingsMigrations.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/FileSystem.h"
#include "Utils/Form.h"
#include "Utils/Game.h"
#include "Utils/PointLightFlags.h"
#include "Utils/UI.h"

#include "RE/B/BGSLocation.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/S/Sky.h"
#include "RE/T/TES.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESWeather.h"
#include "RE/T/TESWorldSpace.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <numeric>
#include <system_error>
#include <unordered_set>
#include <utility>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	AdaptiveBrightness::WaterWindSettings,
	overrideEnabled,
	enabled,
	calmWaveMultiplier,
	strongWindWaveMultiplier)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	AdaptiveBrightness::ProfileSettings,
	brightness,
	advanced,
	bloomAdvanced,
	waterAdvanced,
	skyBrightnessMult,
	directionalLightMult,
	pointLightMult,
	linearPointLightMult,
	spotlightMult,
	linearSpotlightMult,
	omnidirectionalBulbMult,
	linearOmnidirectionalBulbMult,
	ambientMult,
	emitColorMult,
	glowmapMult,
	effectLightingMult,
	skyGammaOffset,
	fogGammaOffset,
	fogAlphaGammaOffset,
	waterGammaOffset,
	vlGammaOffset,
	bloom,
	water,
	waterWind)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	AdaptiveBrightness::LocationOverride,
	key,
	name,
	type,
	cocCode,
	profile,
	layered)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	AdaptiveBrightness::Settings,
	enabled,
	dayStartHour,
	nightStartHour,
	transitionHours,
	globalProfile,
	profiles,
	locationOverrides)

AdaptiveBrightness::ProfileSettings AdaptiveBrightness::ProfileSettings::AdjustmentDefaults()
{
	return {};
}

AdaptiveBrightness::ProfileSettings AdaptiveBrightness::ProfileSettings::GlobalDefaults()
{
	auto profile = AdjustmentDefaults();
	profile.advanced = true;
	profile.bloomAdvanced = true;
	profile.waterAdvanced = true;
	profile.waterWind.overrideEnabled = true;
	profile.waterWind.calmWaveMultiplier = 0.65f;
	profile.waterWind.strongWindWaveMultiplier = 1.35f;
	return profile;
}

namespace
{
	constexpr uint32_t kMaxVanillaPointLights = 7;
	constexpr uint32_t kFirstPointLightSceneIndex = 1;
	constexpr float kBrightnessMin = 0.25f;
	constexpr float kBrightnessMax = 2.0f;
	constexpr float kGammaOffsetMin = -1.0f;
	constexpr float kGammaOffsetMax = 1.0f;
	constexpr float kGlobalSkyBrightnessMax = 2.0f;
	constexpr float kGlobalLightingMultiplierMax = 5.0f;
	constexpr float kWaterWindMultiplierMin = 0.0f;
	constexpr float kWaterWindMultiplierMax = 2.0f;
	constexpr float kWaterWindSmoothingSeconds = 2.0f;
	constexpr float kMaxWaterWindSmoothingFrameTime = 0.25f;
	constexpr std::size_t kMaxOverrideHierarchyDepth = 64;

	using Profile = AdaptiveBrightness::Profile;

	bool UsesClassifiedPointLightMultipliers(const SharedLightingSettings& a_settings)
	{
		return a_settings.linearPointLightMult != a_settings.pointLightMult ||
		       a_settings.spotlightMult != 1.0f ||
		       a_settings.linearSpotlightMult != 1.0f ||
		       a_settings.omnidirectionalBulbMult != 1.0f ||
		       a_settings.linearOmnidirectionalBulbMult != 1.0f;
	}

	constexpr std::array<Profile, AdaptiveBrightness::kProfileCount> kProfileOrder{
		Profile::ExteriorDay,
		Profile::ExteriorNight,
		Profile::Interior,
		Profile::Dungeon,
		Profile::Dwelling
	};

	constexpr std::array<const char*, AdaptiveBrightness::kProfileCount> kProfileNames{
		"Exterior Day",
		"Exterior Night",
		"Interior",
		"Dungeon",
		"Dwelling"
	};

	constexpr const char* kOverrideTypeLocation = "Location";
	constexpr const char* kOverrideTypeRegion = "Region";
	constexpr const char* kOverrideTypeCity = "City";
	constexpr const char* kOverrideTypeCell = "Cell";
	constexpr const char* kOverrideTypeWorldspace = "Worldspace";
	constexpr const char* kPresetVersion = "5.0.0";
	constexpr std::string_view kLocationOverridesFieldName = "locationOverrides";
	constexpr std::string_view kProfilesFieldName = "profiles";
	constexpr std::string_view kLegacyGlobalWaterAppearanceFieldName = SettingsMigrations::kLegacyWaterAppearanceSettingsKey;
	constexpr std::string_view kGlobalPresetFilenameSuffix = "_AdaptiveBrightness_Global";
	constexpr std::string_view kLocationPresetFilenameSuffix = "_AdaptiveBrightness_LocationOverrides";
	constexpr std::string_view kWorldspacePresetFilenameSuffix = "_AdaptiveBrightness_Worldspace";
	constexpr std::string_view kRegionPresetFilenameSuffix = "_AdaptiveBrightness_Location";
	constexpr std::string_view kCityPresetFilenameSuffix = "_AdaptiveBrightness_City";
	constexpr std::string_view kFullPresetFilenameSuffix = "_AdaptiveBrightness_Full";

	enum class PresetKind
	{
		Global,
		Location,
		Worldspace,
		Region,
		City,
		Full
	};

	using ContextProfileScope = AdaptiveBrightness::ContextProfileScope;
	constexpr std::array kContextProfileOrder{
		ContextProfileScope::Worldspace,
		ContextProfileScope::Region,
		ContextProfileScope::City
	};

	constexpr std::size_t ContextScopeIndex(ContextProfileScope a_scope)
	{
		return static_cast<std::size_t>(a_scope);
	}
	static_assert(kContextProfileOrder.size() == ContextScopeIndex(ContextProfileScope::Count));

	constexpr const char* GetContextScopeTabLabel(ContextProfileScope a_scope)
	{
		switch (a_scope) {
		case ContextProfileScope::Worldspace:
			return "Worldspace";
		case ContextProfileScope::Region:
			return "Locations";
		case ContextProfileScope::City:
			return "Cities";
		default:
			return "Context";
		}
	}

	constexpr const char* GetContextScopeName(ContextProfileScope a_scope)
	{
		switch (a_scope) {
		case ContextProfileScope::Worldspace:
			return "Worldspace";
		case ContextProfileScope::Region:
			return "Location";
		case ContextProfileScope::City:
			return "City";
		default:
			return "Context";
		}
	}

	constexpr PresetKind GetContextScopePresetKind(ContextProfileScope a_scope)
	{
		switch (a_scope) {
		case ContextProfileScope::Worldspace:
			return PresetKind::Worldspace;
		case ContextProfileScope::Region:
			return PresetKind::Region;
		case ContextProfileScope::City:
			return PresetKind::City;
		default:
			return PresetKind::Location;
		}
	}

	struct CurrentLocationForms
	{
		const RE::TESWorldSpace* worldspace = nullptr;
		const RE::BGSLocation* location = nullptr;
		const RE::TESObjectCELL* cell = nullptr;
	};

	const AdaptiveBrightness::LocationOverrideTarget* GetContextTarget(
		const AdaptiveBrightness::CurrentLocationOverrideTargets& a_targets,
		ContextProfileScope a_scope)
	{
		switch (a_scope) {
		case ContextProfileScope::Worldspace:
			return a_targets.worldspace ? &*a_targets.worldspace : nullptr;
		case ContextProfileScope::Region:
			return a_targets.region ? &*a_targets.region : nullptr;
		case ContextProfileScope::City:
			return a_targets.city ? &*a_targets.city : nullptr;
		default:
			return nullptr;
		}
	}

	constexpr const char* GetContextScopeDescription(ContextProfileScope a_scope)
	{
		switch (a_scope) {
		case ContextProfileScope::Worldspace:
			return "Applies throughout the current game worldspace and its child worldspaces.";
		case ContextProfileScope::Region:
			return "Applies throughout the current hold or regional location, such as The Reach or The Rift.";
		case ContextProfileScope::City:
			return "Applies throughout the current city, town, or settlement, such as Markarth or Riften.";
		default:
			return "Applies to the current contextual scope.";
		}
	}

	struct LocationOverrideImportStats
	{
		std::size_t imported = 0;
		std::size_t replaced = 0;
		std::size_t skipped = 0;
	};

	std::size_t ProfileIndex(Profile a_profile)
	{
		return std::clamp(
			static_cast<std::size_t>(a_profile),
			static_cast<std::size_t>(0),
			AdaptiveBrightness::kProfileCount - 1);
	}

	float SafeFinite(float a_value, float a_fallback)
	{
		return std::isfinite(a_value) ? a_value : a_fallback;
	}

	float ApplyRelativeValue(float a_base, float a_layer, float a_identity)
	{
		if (std::abs(a_identity) <= 0.0001f)
			return a_base + a_layer;

		return a_base * (a_layer / a_identity);
	}

	float ClampMultiplier(float a_value)
	{
		return std::clamp(SafeFinite(a_value, 1.0f), 0.0f, 10.0f);
	}

	float ClampGamma(float a_value)
	{
		return std::clamp(SafeFinite(a_value, 1.0f), 0.1f, 3.0f);
	}

	float ClampGammaOffset(float a_value)
	{
		return std::clamp(SafeFinite(a_value, 0.0f), kGammaOffsetMin, kGammaOffsetMax);
	}

	float ClampBrightness(float a_value)
	{
		return std::clamp(SafeFinite(a_value, 1.0f), kBrightnessMin, kBrightnessMax);
	}

	void SanitizeSharedLightingSettings(SharedLightingSettings& a_settings)
	{
		const SharedLightingSettings defaults{};
		const auto clamp = [](float a_value, float a_max, float a_default) {
			return std::clamp(SafeFinite(a_value, a_default), 0.0f, a_max);
		};

		a_settings.skyBrightness = clamp(a_settings.skyBrightness, kGlobalSkyBrightnessMax, defaults.skyBrightness);
		a_settings.directionalLightMult = clamp(a_settings.directionalLightMult, kGlobalLightingMultiplierMax, defaults.directionalLightMult);
		a_settings.pointLightMult = clamp(a_settings.pointLightMult, kGlobalLightingMultiplierMax, defaults.pointLightMult);
		a_settings.linearPointLightMult = clamp(a_settings.linearPointLightMult, kGlobalLightingMultiplierMax, defaults.linearPointLightMult);
		a_settings.spotlightMult = clamp(a_settings.spotlightMult, kGlobalLightingMultiplierMax, defaults.spotlightMult);
		a_settings.linearSpotlightMult = clamp(a_settings.linearSpotlightMult, kGlobalLightingMultiplierMax, defaults.linearSpotlightMult);
		a_settings.omnidirectionalBulbMult = clamp(a_settings.omnidirectionalBulbMult, kGlobalLightingMultiplierMax, defaults.omnidirectionalBulbMult);
		a_settings.linearOmnidirectionalBulbMult = clamp(a_settings.linearOmnidirectionalBulbMult, kGlobalLightingMultiplierMax, defaults.linearOmnidirectionalBulbMult);
	}

	float WrapHour(float a_hour)
	{
		if (!std::isfinite(a_hour))
			return 12.0f;

		auto wrapped = std::fmod(a_hour, 24.0f);
		if (wrapped < 0.0f)
			wrapped += 24.0f;

		return wrapped;
	}

	float HoursSince(float a_startHour, float a_currentHour)
	{
		auto delta = WrapHour(a_currentHour) - WrapHour(a_startHour);
		if (delta < 0.0f)
			delta += 24.0f;

		return delta;
	}

	float SmoothStep(float a_edge0, float a_edge1, float a_x)
	{
		if (a_edge1 <= a_edge0)
			return a_x >= a_edge1 ? 1.0f : 0.0f;

		const float t = std::clamp((a_x - a_edge0) / (a_edge1 - a_edge0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	bool IsValidFormKey(const std::string& a_key)
	{
		return !a_key.empty() && a_key != "Invalid";
	}

	bool EndsWithCaseInsensitive(std::string_view a_value, std::string_view a_suffix)
	{
		if (a_value.size() < a_suffix.size())
			return false;

		const auto offset = a_value.size() - a_suffix.size();
		for (std::size_t i = 0; i < a_suffix.size(); ++i) {
			const auto lhs = static_cast<unsigned char>(a_value[offset + i]);
			const auto rhs = static_cast<unsigned char>(a_suffix[i]);
			if (std::tolower(lhs) != std::tolower(rhs))
				return false;
		}

		return true;
	}

	std::string TrimCopy(std::string_view a_value)
	{
		const auto first = std::find_if(a_value.begin(), a_value.end(), [](char a_ch) { return !std::isspace(static_cast<unsigned char>(a_ch)); });
		if (first == a_value.end())
			return {};

		const auto last = std::find_if(a_value.rbegin(), a_value.rend(), [](char a_ch) { return !std::isspace(static_cast<unsigned char>(a_ch)); }).base();
		return std::string(first, last);
	}

	std::string_view GetDefaultPresetName(PresetKind a_kind)
	{
		switch (a_kind) {
		case PresetKind::Global:
			return AdaptiveBrightness::kDefaultGlobalPresetName;
		case PresetKind::Full:
			return AdaptiveBrightness::kDefaultFullPresetName;
		case PresetKind::Location:
		case PresetKind::Worldspace:
		case PresetKind::Region:
		case PresetKind::City:
		default:
			return AdaptiveBrightness::kDefaultLocationOverridePresetName;
		}
	}

	std::string GetPresetModName(std::string_view a_name, PresetKind a_kind = PresetKind::Location)
	{
		auto baseName = TrimCopy(a_name);
		if (EndsWithCaseInsensitive(baseName, ".json"))
			baseName.resize(baseName.size() - std::string_view(".json").size());

		for (const auto suffix : {
				 kGlobalPresetFilenameSuffix,
				 kLocationPresetFilenameSuffix,
				 kWorldspacePresetFilenameSuffix,
				 kRegionPresetFilenameSuffix,
				 kCityPresetFilenameSuffix,
				 kFullPresetFilenameSuffix }) {
			if (EndsWithCaseInsensitive(baseName, suffix)) {
				baseName.resize(baseName.size() - suffix.size());
				break;
			}
		}

		std::string sanitized;
		sanitized.reserve(baseName.size());
		bool lastWasSeparator = false;
		for (const auto ch : baseName) {
			const auto uch = static_cast<unsigned char>(ch);
			if (std::isalnum(uch) || ch == '-' || ch == '_') {
				sanitized.push_back(ch);
				lastWasSeparator = ch == '_';
			} else if (std::isspace(uch) || ch == '.') {
				if (!sanitized.empty() && !lastWasSeparator) {
					sanitized.push_back('_');
					lastWasSeparator = true;
				}
			}

			if (sanitized.size() >= 96)
				break;
		}

		while (!sanitized.empty() && sanitized.back() == '_')
			sanitized.pop_back();

		return sanitized.empty() ? std::string(GetDefaultPresetName(a_kind)) : sanitized;
	}

	std::string_view GetPresetFilenameSuffix(PresetKind a_kind)
	{
		switch (a_kind) {
		case PresetKind::Global:
			return kGlobalPresetFilenameSuffix;
		case PresetKind::Full:
			return kFullPresetFilenameSuffix;
		case PresetKind::Worldspace:
			return kWorldspacePresetFilenameSuffix;
		case PresetKind::Region:
			return kRegionPresetFilenameSuffix;
		case PresetKind::City:
			return kCityPresetFilenameSuffix;
		case PresetKind::Location:
		default:
			return kLocationPresetFilenameSuffix;
		}
	}

	std::string GetPresetFilename(std::string_view a_name, PresetKind a_kind)
	{
		return std::format("{}{}.json", GetPresetModName(a_name, a_kind), GetPresetFilenameSuffix(a_kind).data());
	}

	std::filesystem::path GetPresetDirectory()
	{
		// Keep exported presets outside the live Overrides folder so sharing a preset
		// does not implicitly turn it into an active feature override on next load.
		return Util::PathHelpers::GetCommunityShaderPath() / AdaptiveBrightness::kFeatureShortName.data() / "Presets";
	}

	std::filesystem::path GetPresetPath(std::string_view a_name, PresetKind a_kind)
	{
		return GetPresetDirectory() / GetPresetFilename(a_name, a_kind);
	}

	std::filesystem::path GetLocationOverrideLiveOverridePath(std::string_view a_name)
	{
		return Util::PathHelpers::GetOverridesPath() / GetPresetFilename(a_name, PresetKind::Location);
	}

	std::optional<std::filesystem::path> ResolvePresetImportPath(std::string_view a_name, PresetKind a_kind)
	{
		std::array paths{
			GetPresetPath(a_name, a_kind),
			a_kind == PresetKind::Location ? GetLocationOverrideLiveOverridePath(a_name) : std::filesystem::path{}
		};

		for (const auto& path : paths) {
			if (path.empty())
				continue;

			std::error_code ec;
			if (std::filesystem::exists(path, ec) && !ec)
				return path;
		}

		return std::nullopt;
	}

	std::string GetPresetNotFoundMessage(std::string_view a_name, PresetKind a_kind)
	{
		if (a_kind == PresetKind::Location) {
			return std::format(
				"Import failed: no preset named {} was found in {} or {}.",
				GetPresetFilename(a_name, a_kind),
				GetPresetDirectory().string(),
				Util::PathHelpers::GetOverridesPath().string());
		}

		return std::format(
			"Import failed: no preset named {} was found in {}.",
			GetPresetFilename(a_name, a_kind),
			GetPresetDirectory().string());
	}

	bool WriteJsonFileAtomic(const std::filesystem::path& a_path, const json& a_json)
	{
		auto tempPath = a_path;
		tempPath += ".tmp";
		auto backupPath = a_path;
		backupPath += ".bak";

		std::ofstream file(tempPath, std::ios::out | std::ios::trunc);
		if (!file.is_open())
			return false;

		file << a_json.dump(1);
		file.flush();
		file.close();
		if (file.fail()) {
			std::error_code removeEc;
			std::filesystem::remove(tempPath, removeEc);
			return false;
		}

		std::error_code ec;
		if (std::filesystem::exists(backupPath, ec) && !ec)
			std::filesystem::remove(backupPath, ec);
		if (ec) {
			std::filesystem::remove(tempPath, ec);
			return false;
		}

		const bool hadExistingFile = std::filesystem::exists(a_path, ec) && !ec;
		if (ec) {
			std::filesystem::remove(tempPath, ec);
			return false;
		}

		if (hadExistingFile) {
			std::filesystem::rename(a_path, backupPath, ec);
			if (ec) {
				std::filesystem::remove(tempPath, ec);
				return false;
			}
		}

		std::filesystem::rename(tempPath, a_path, ec);
		if (ec) {
			std::error_code restoreEc;
			if (hadExistingFile)
				std::filesystem::rename(backupPath, a_path, restoreEc);
			std::filesystem::remove(tempPath, ec);
			return false;
		}

		if (hadExistingFile)
			std::filesystem::remove(backupPath, ec);

		return true;
	}

	bool ReadJsonPresetFile(const std::filesystem::path& a_path, json& o_json, std::string& o_status)
	{
		try {
			std::error_code ec;
			const auto fileSize = std::filesystem::file_size(a_path, ec);
			if (ec) {
				o_status = std::format("Import failed: could not read file size for {}.", a_path.string());
				return false;
			}

			constexpr std::uintmax_t maxImportFileSize = 1024 * 1024;
			if (fileSize == 0 || fileSize > maxImportFileSize) {
				o_status = "Import failed: preset file must be between 1 byte and 1 MB.";
				return false;
			}

			std::ifstream file(a_path);
			if (!file.is_open()) {
				o_status = std::format("Import failed: could not open {}.", a_path.string());
				return false;
			}

			file >> o_json;
			return true;
		} catch (const json::exception& e) {
			o_status = std::format("Import failed: invalid JSON ({})", e.what());
			return false;
		} catch (const std::exception& e) {
			o_status = std::format("Import failed: {}", e.what());
			return false;
		}
	}

	void SanitizeWaterWindSettings(AdaptiveBrightness::WaterWindSettings& a_settings);

	void ClampProfileSettings(AdaptiveBrightness::ProfileSettings& a_profile)
	{
		a_profile.brightness = ClampBrightness(a_profile.brightness);
		a_profile.skyBrightnessMult = ClampMultiplier(a_profile.skyBrightnessMult);
		a_profile.directionalLightMult = ClampMultiplier(a_profile.directionalLightMult);
		a_profile.pointLightMult = ClampMultiplier(a_profile.pointLightMult);
		a_profile.linearPointLightMult = ClampMultiplier(a_profile.linearPointLightMult);
		a_profile.spotlightMult = ClampMultiplier(a_profile.spotlightMult);
		a_profile.linearSpotlightMult = ClampMultiplier(a_profile.linearSpotlightMult);
		a_profile.omnidirectionalBulbMult = ClampMultiplier(a_profile.omnidirectionalBulbMult);
		a_profile.linearOmnidirectionalBulbMult = ClampMultiplier(a_profile.linearOmnidirectionalBulbMult);
		a_profile.ambientMult = ClampMultiplier(a_profile.ambientMult);
		a_profile.emitColorMult = ClampMultiplier(a_profile.emitColorMult);
		a_profile.glowmapMult = ClampMultiplier(a_profile.glowmapMult);
		a_profile.effectLightingMult = ClampMultiplier(a_profile.effectLightingMult);
		a_profile.skyGammaOffset = ClampGammaOffset(a_profile.skyGammaOffset);
		a_profile.fogGammaOffset = ClampGammaOffset(a_profile.fogGammaOffset);
		a_profile.fogAlphaGammaOffset = ClampGammaOffset(a_profile.fogAlphaGammaOffset);
		a_profile.waterGammaOffset = ClampGammaOffset(a_profile.waterGammaOffset);
		a_profile.vlGammaOffset = ClampGammaOffset(a_profile.vlGammaOffset);
		Bloom::SanitizeProfile(a_profile.bloom);
		WaterAppearance::SanitizeProfile(a_profile.water);
		SanitizeWaterWindSettings(a_profile.waterWind);
	}

	void NormalizeBaseProfiles(std::array<AdaptiveBrightness::ProfileSettings, AdaptiveBrightness::kProfileCount>& a_profiles)
	{
		for (auto& profile : a_profiles)
			ClampProfileSettings(profile);
	}

	void NormalizeExteriorTimeSettings(AdaptiveBrightness::Settings& a_settings)
	{
		a_settings.dayStartHour = WrapHour(a_settings.dayStartHour);
		a_settings.nightStartHour = WrapHour(a_settings.nightStartHour);
		a_settings.transitionHours = std::clamp(SafeFinite(a_settings.transitionHours, 1.0f), 0.0f, 4.0f);
	}

	void SanitizeWaterWindSettings(AdaptiveBrightness::WaterWindSettings& a_settings)
	{
		const AdaptiveBrightness::WaterWindSettings defaults{};
		a_settings.calmWaveMultiplier = std::clamp(
			SafeFinite(a_settings.calmWaveMultiplier, defaults.calmWaveMultiplier),
			kWaterWindMultiplierMin,
			kWaterWindMultiplierMax);
		a_settings.strongWindWaveMultiplier = std::clamp(
			SafeFinite(a_settings.strongWindWaveMultiplier, defaults.strongWindWaveMultiplier),
			kWaterWindMultiplierMin,
			kWaterWindMultiplierMax);
	}

	float GetWaterWindWaveMultiplier(
		const AdaptiveBrightness::WaterWindSettings& a_settings,
		float a_windSpeed)
	{
		const float windSpeed = std::clamp(SafeFinite(a_windSpeed, 0.0f), 0.0f, 1.0f);
		return std::lerp(a_settings.calmWaveMultiplier, a_settings.strongWindWaveMultiplier, windSpeed);
	}

	template <class T, class ApplyLayer>
	T ApplyLocationLayers(
		const T& a_globalBase,
		T a_baseProfile,
		const std::vector<const AdaptiveBrightness::LocationOverride*>& a_layers,
		ApplyLayer&& a_applyLayer)
	{
		for (const auto* layer : a_layers) {
			if (!layer)
				continue;
			a_baseProfile = a_applyLayer(layer->layered ? a_baseProfile : a_globalBase, layer->profile);
		}
		return a_baseProfile;
	}

	template <class T>
	struct ComposedProfileBranches
	{
		T from;
		std::optional<T> to;
		float factor = 0.0f;
	};

	template <class T, class ApplyLayer>
	ComposedProfileBranches<T> ComposeProfileBranches(
		const T& a_globalBase,
		const AdaptiveBrightness::ActiveProfileBlend& a_activeProfiles,
		const std::vector<const AdaptiveBrightness::LocationOverride*>& a_locationLayers,
		ApplyLayer&& a_applyLayer)
	{
		if (!a_activeProfiles.from)
			return { .from = a_globalBase };

		const auto composeBranch = [&](const AdaptiveBrightness::ProfileSettings& a_profile) {
			return ApplyLocationLayers(
				a_globalBase,
				a_applyLayer(a_globalBase, a_profile),
				a_locationLayers,
				a_applyLayer);
		};
		auto from = composeBranch(*a_activeProfiles.from);
		if (!a_activeProfiles.to || a_activeProfiles.from == a_activeProfiles.to)
			return { .from = std::move(from) };

		return {
			.from = std::move(from),
			.to = composeBranch(*a_activeProfiles.to),
			.factor = std::clamp(SafeFinite(a_activeProfiles.factor, 0.0f), 0.0f, 1.0f)
		};
	}

	void NormalizeBaseSettings(AdaptiveBrightness::Settings& a_settings)
	{
		ClampProfileSettings(a_settings.globalProfile);
		a_settings.globalProfile.waterWind.overrideEnabled = true;
		NormalizeExteriorTimeSettings(a_settings);
		NormalizeBaseProfiles(a_settings.profiles);
	}

	float GetOptionalFloat(const json& a_json, const char* a_key, float a_fallback)
	{
		if (!a_json.is_object())
			return a_fallback;

		const auto it = a_json.find(a_key);
		if (it == a_json.end())
			return a_fallback;

		try {
			return SafeFinite(it->get<float>(), a_fallback);
		} catch (const json::exception&) {
			return a_fallback;
		}
	}

	bool GetOptionalBool(const json& a_json, const char* a_key, bool a_fallback)
	{
		if (!a_json.is_object())
			return a_fallback;

		const auto it = a_json.find(a_key);
		if (it == a_json.end())
			return a_fallback;
		if (it->is_boolean())
			return it->get<bool>();
		if (it->is_number_unsigned())
			return it->get<uint64_t>() != 0;
		if (it->is_number_integer())
			return it->get<int64_t>() != 0;
		if (it->is_number_float()) {
			const auto value = it->get<double>();
			return std::isfinite(value) ? value != 0.0 : a_fallback;
		}
		return a_fallback;
	}

	void NormalizeJsonObjectWithDefaults(json& a_value, const json& a_defaults)
	{
		if (!a_value.is_object() || !a_defaults.is_object())
			return;

		for (const auto& [key, defaultValue] : a_defaults.items()) {
			auto valueIt = a_value.find(key);
			if (valueIt == a_value.end())
				continue;

			if (defaultValue.is_object()) {
				if (!valueIt->is_object())
					*valueIt = defaultValue;
				else
					NormalizeJsonObjectWithDefaults(*valueIt, defaultValue);
			} else if (!SettingsMigrations::MatchesJsonSchema(*valueIt, defaultValue)) {
				*valueIt = defaultValue;
			}
		}
	}

	std::optional<Bloom::Profile> TryGetBloomProfile(const json& a_value)
	{
		if (!a_value.is_object())
			return std::nullopt;

		try {
			auto profile = a_value.get<Bloom::Profile>();
			Bloom::SanitizeProfile(profile);
			return profile;
		} catch (const json::exception&) {
			return std::nullopt;
		}
	}

	void SetProfileBloom(json& a_profile, const Bloom::Profile& a_bloom, bool a_advanced)
	{
		auto bloom = a_bloom;
		Bloom::SanitizeProfile(bloom);
		a_profile["bloom"] = bloom;
		a_profile["bloomAdvanced"] = a_advanced;
	}

	std::optional<WaterAppearance::Profile> TryGetWaterAppearanceProfile(const json& a_value)
	{
		if (!a_value.is_object())
			return std::nullopt;

		try {
			auto profile = a_value.get<WaterAppearance::Profile>();
			WaterAppearance::SanitizeProfile(profile);
			return profile;
		} catch (const json::exception&) {
			return std::nullopt;
		}
	}

	void SetProfileWaterAppearance(json& a_profile, const WaterAppearance::Profile& a_water)
	{
		auto water = a_water;
		WaterAppearance::SanitizeProfile(water);
		a_profile["water"] = water;
	}

	void MigrateLegacyProfileLighting(json& a_profile)
	{
		if (!a_profile.is_object())
			return;

		if (const auto linearPointIt = a_profile.find("linearPointLightMult");
			linearPointIt != a_profile.end() && linearPointIt->is_number())
			return;

		if (const auto pointIt = a_profile.find("pointLightMult"); pointIt != a_profile.end() && pointIt->is_number())
			a_profile["linearPointLightMult"] = *pointIt;
	}

	void MigrateLegacyProfileWaterAppearance(
		json& a_profile,
		const WaterAppearance::Profile& a_globalProfile,
		bool a_hasLegacyGlobal,
		bool a_forceGlobal = false)
	{
		if (!a_profile.is_object())
			return;

		const auto waterIt = a_profile.find("water");
		const bool hasExplicitWater = waterIt != a_profile.end() && waterIt->is_object() &&
		                              GetOptionalBool(*waterIt, SettingsMigrations::kLegacyWaterProfileExplicitKey.data(), false);
		if ((!a_forceGlobal || hasExplicitWater) && waterIt != a_profile.end() && waterIt->is_object()) {
			// A profile-native value wins field-by-field. Missing fields inherit the
			// migrated global value so a formerly global Unified Water configuration
			// remains global across every Adaptive Balance profile and override.
			json mergedWater = a_hasLegacyGlobal ? json(a_globalProfile) : json(WaterAppearance::Profile{});
			for (const auto fieldName : SettingsMigrations::kLegacyUnifiedWaterAppearanceKeys) {
				if (const auto fieldIt = waterIt->find(fieldName.data()); fieldIt != waterIt->end() && fieldIt->is_number())
					mergedWater[std::string(fieldName)] = *fieldIt;
			}

			if (const auto nativeWater = TryGetWaterAppearanceProfile(mergedWater))
				SetProfileWaterAppearance(a_profile, *nativeWater);
			else
				SetProfileWaterAppearance(a_profile, WaterAppearance::Profile{});
			return;
		}

		if (a_forceGlobal || a_hasLegacyGlobal) {
			SetProfileWaterAppearance(a_profile, a_globalProfile);
		} else if (waterIt != a_profile.end()) {
			// Do not let a malformed profile-native Water field reject the whole
			// profile or location override.
			SetProfileWaterAppearance(a_profile, WaterAppearance::Profile{});
		}
	}

	Bloom::Profile GetLegacyGlobalBloomProfile(const json& a_settings, bool& o_enabled)
	{
		o_enabled = false;
		auto profile = Bloom::Profile{};
		const auto bloomIt = a_settings.find("bloomEnhancement");
		if (bloomIt == a_settings.end() || !bloomIt->is_object())
			return profile;

		const auto& legacyBloom = *bloomIt;
		o_enabled = GetOptionalBool(legacyBloom, "Enabled", false);
		uint preset = 0;
		if (const auto presetIt = legacyBloom.find("SelectedPreset"); presetIt != legacyBloom.end()) {
			if (presetIt->is_number_unsigned())
				preset = static_cast<uint>(std::min<uint64_t>(presetIt->get<uint64_t>(), 2u));
			else if (presetIt->is_number_integer())
				preset = static_cast<uint>(std::clamp<int64_t>(presetIt->get<int64_t>(), 0, 2));
		}
		const char* profileName = preset == 1 ? "Fantasy" : preset == 2 ? "Dreamy" :
		                                                                  "Default";
		if (const auto profileIt = legacyBloom.find(profileName); profileIt != legacyBloom.end() && profileIt->is_object()) {
			try {
				profile = profileIt->get<Bloom::Profile>();
			} catch (const json::exception&) {
				profile = Bloom::GetPresetProfile(preset);
			}
		} else {
			profile = Bloom::GetPresetProfile(preset);
		}
		Bloom::SanitizeProfile(profile);
		return profile;
	}

	void MigrateLegacyProfileBloom(
		json& a_profile,
		const Bloom::Profile& a_globalProfile,
		bool a_globalEnabled,
		bool a_hasLegacyGlobal,
		bool a_forceGlobal = false)
	{
		if (!a_profile.is_object())
			return;

		const auto bloomIt = a_profile.find("bloom");
		const auto nativeBloom = bloomIt != a_profile.end() ? TryGetBloomProfile(*bloomIt) : std::nullopt;
		const auto advancedIt = a_profile.find("bloomAdvanced");
		const bool hasNativeAdvanced = advancedIt != a_profile.end() && advancedIt->is_boolean();
		const bool hasLegacyProfileFields =
			a_profile.contains("bloomOverride") || a_profile.contains("bloomEnabled");

		if (!a_forceGlobal && nativeBloom && (hasNativeAdvanced || !hasLegacyProfileFields)) {
			// A valid profile-native value wins over stale legacy fields that may
			// remain after layered settings or override merges. A partial native
			// profile is also valid: bloomAdvanced defaults to false.
			SetProfileBloom(a_profile, *nativeBloom, hasNativeAdvanced && advancedIt->get<bool>());
			a_profile.erase("bloomOverride");
			a_profile.erase("bloomEnabled");
			return;
		}

		if (!a_forceGlobal && !hasLegacyProfileFields &&
			(a_profile.contains("bloom") || a_profile.contains("bloomAdvanced"))) {
			// Do not let a malformed native Bloom field fail the entire feature load.
			SetProfileBloom(a_profile, Bloom::Profile{}, false);
			return;
		}

		if (!a_profile.contains("bloomOverride")) {
			if (a_forceGlobal || a_hasLegacyGlobal) {
				auto profile = a_globalProfile;
				if (!a_globalEnabled)
					profile.EnhancementIntensity = 0.0f;
				SetProfileBloom(a_profile, profile, false);
			}
			return;
		}

		const bool useProfileBloom = GetOptionalBool(a_profile, "bloomOverride", false);
		const bool profileBloomEnabled = GetOptionalBool(a_profile, "bloomEnabled", false);
		auto profile = a_globalProfile;
		if (useProfileBloom) {
			if (nativeBloom)
				profile = *nativeBloom;
			if (!profileBloomEnabled)
				profile.EnhancementIntensity = 0.0f;
		} else if (!a_globalEnabled) {
			profile.EnhancementIntensity = 0.0f;
		}

		SetProfileBloom(a_profile, profile, false);
		a_profile.erase("bloomOverride");
		a_profile.erase("bloomEnabled");
	}

	void NormalizeProfileArray(json& a_settings)
	{
		auto profilesIt = a_settings.find(kProfilesFieldName.data());
		if (profilesIt == a_settings.end())
			return;

		const json defaultProfiles = AdaptiveBrightness::Settings{}.profiles;
		if (!profilesIt->is_array()) {
			*profilesIt = defaultProfiles;
			return;
		}

		json normalizedProfiles = defaultProfiles;
		for (std::size_t index = 0; index < std::min(profilesIt->size(), normalizedProfiles.size()); ++index) {
			if (!(*profilesIt)[index].is_object())
				continue;
			normalizedProfiles[index] = (*profilesIt)[index];
			NormalizeJsonObjectWithDefaults(normalizedProfiles[index], defaultProfiles[index]);
		}
		*profilesIt = std::move(normalizedProfiles);
	}

	void NormalizeLocationOverrideArray(json& a_settings)
	{
		auto overridesIt = a_settings.find(kLocationOverridesFieldName.data());
		if (overridesIt == a_settings.end())
			return;
		if (!overridesIt->is_array()) {
			a_settings.erase(overridesIt);
			return;
		}

		const json defaultOverride = AdaptiveBrightness::LocationOverride{};
		json normalizedOverrides = json::array();
		for (const auto& locationOverride : *overridesIt) {
			if (!locationOverride.is_object())
				continue;
			auto normalizedOverride = locationOverride;
			NormalizeJsonObjectWithDefaults(normalizedOverride, defaultOverride);
			normalizedOverrides.push_back(std::move(normalizedOverride));
		}
		*overridesIt = std::move(normalizedOverrides);
	}

	void NormalizeAdaptiveBalanceSettings(json& a_settings)
	{
		if (!a_settings.is_object())
			return;

		const json defaults = AdaptiveBrightness::Settings{};
		for (const auto& [key, defaultValue] : defaults.items()) {
			if (key == kProfilesFieldName || key == kLocationOverridesFieldName)
				continue;
			auto valueIt = a_settings.find(key);
			if (valueIt == a_settings.end())
				continue;
			if (defaultValue.is_object()) {
				if (!valueIt->is_object())
					*valueIt = defaultValue;
				else
					NormalizeJsonObjectWithDefaults(*valueIt, defaultValue);
			} else if (!SettingsMigrations::MatchesJsonSchema(*valueIt, defaultValue)) {
				*valueIt = defaultValue;
			}
		}

		NormalizeProfileArray(a_settings);
		NormalizeLocationOverrideArray(a_settings);
	}

	json MigrateLegacyProfileSettings(const json& a_settings)
	{
		if (!a_settings.is_object())
			return a_settings;

		auto migrated = a_settings;
		const bool hasLegacyLightingToggle = migrated.contains("globalLightingEnabled") || migrated.contains("rendererControlsEnabled");
		const bool legacyLightingEnabled = GetOptionalBool(
			migrated,
			"globalLightingEnabled",
			GetOptionalBool(migrated, "rendererControlsEnabled", true));
		const auto legacyWaterWindIt = migrated.find("waterWind");
		const bool hasLegacyWaterWind = legacyWaterWindIt != migrated.end() && legacyWaterWindIt->is_object();
		const auto globalEnabledIt = migrated.find("globalProfileEnabled");
		const bool removedGlobalLayerWasDisabled =
			globalEnabledIt != migrated.end() && globalEnabledIt->is_boolean() && !globalEnabledIt->get<bool>();

		const auto existingGlobalProfileIt = migrated.find("globalProfile");
		json globalProfile = existingGlobalProfileIt != migrated.end() && existingGlobalProfileIt->is_object() ?
		                         *existingGlobalProfileIt :
		                         json::object();
		const auto setFallback = [&](const char* a_name, const json& a_value) {
			const auto valueIt = globalProfile.find(a_name);
			if (valueIt == globalProfile.end() || !SettingsMigrations::MatchesJsonSchema(*valueIt, a_value))
				globalProfile[a_name] = a_value;
		};

		if (hasLegacyLightingToggle)
			setFallback("advanced", legacyLightingEnabled);
		if (const auto lightingIt = migrated.find("lighting"); lightingIt != migrated.end() && lightingIt->is_object()) {
			constexpr std::array lightingFields{
				std::pair{ "skyBrightness", "skyBrightnessMult" },
				std::pair{ "directionalLightMult", "directionalLightMult" },
				std::pair{ "pointLightMult", "pointLightMult" },
				std::pair{ "linearPointLightMult", "linearPointLightMult" },
				std::pair{ "spotlightMult", "spotlightMult" },
				std::pair{ "linearSpotlightMult", "linearSpotlightMult" },
				std::pair{ "omnidirectionalBulbMult", "omnidirectionalBulbMult" },
				std::pair{ "linearOmnidirectionalBulbMult", "linearOmnidirectionalBulbMult" }
			};
			for (const auto& [legacyName, profileName] : lightingFields) {
				if (const auto valueIt = lightingIt->find(legacyName); valueIt != lightingIt->end() && valueIt->is_number())
					setFallback(profileName, *valueIt);
			}
		}

		if (hasLegacyWaterWind) {
			auto& globalWaterWind = globalProfile["waterWind"];
			if (!globalWaterWind.is_object())
				globalWaterWind = json::object();
			globalWaterWind["overrideEnabled"] = true;
			const json waterWindDefaults = AdaptiveBrightness::WaterWindSettings{};
			for (const auto* fieldName : { "enabled", "calmWaveMultiplier", "strongWindWaveMultiplier" }) {
				if (const auto valueIt = legacyWaterWindIt->find(fieldName); valueIt != legacyWaterWindIt->end()) {
					const auto defaultIt = waterWindDefaults.find(fieldName);
					const auto currentIt = globalWaterWind.find(fieldName);
					if (defaultIt != waterWindDefaults.end() &&
						SettingsMigrations::MatchesJsonSchema(*valueIt, *defaultIt) &&
						(currentIt == globalWaterWind.end() || !SettingsMigrations::MatchesJsonSchema(*currentIt, *defaultIt)))
						globalWaterWind[fieldName] = *valueIt;
				}
			}
		}
		if (removedGlobalLayerWasDisabled)
			globalProfile = AdaptiveBrightness::ProfileSettings::GlobalDefaults();
		migrated["globalProfile"] = std::move(globalProfile);

		const auto legacyBloomIt = migrated.find("bloomEnhancement");
		const bool hasLegacyGlobalBloom = legacyBloomIt != migrated.end() && legacyBloomIt->is_object();
		bool globalBloomEnabled = false;
		const auto globalBloomProfile = GetLegacyGlobalBloomProfile(migrated, globalBloomEnabled);
		const auto legacyWaterIt = migrated.find(kLegacyGlobalWaterAppearanceFieldName.data());
		const bool hasLegacyGlobalWater = legacyWaterIt != migrated.end() &&
		                                  SettingsMigrations::HasLegacyUnifiedWaterAppearanceValues(*legacyWaterIt);
		const bool forceLegacyGlobalWater = hasLegacyGlobalWater && GetOptionalBool(*legacyWaterIt, SettingsMigrations::kLegacyWaterAppearanceForceGlobalKey.data(), false);
		const auto globalWaterProfile = hasLegacyGlobalWater ?
		                                    TryGetWaterAppearanceProfile(*legacyWaterIt).value_or(WaterAppearance::Profile{}) :
		                                    WaterAppearance::Profile{};
		const auto existingProfilesIt = migrated.find(kProfilesFieldName.data());
		const bool createdProfilesFromLegacyGlobal =
			(hasLegacyGlobalBloom || hasLegacyGlobalWater) &&
			(existingProfilesIt == migrated.end() || !existingProfilesIt->is_array());
		if (createdProfilesFromLegacyGlobal)
			migrated[std::string(kProfilesFieldName)] = AdaptiveBrightness::Settings{}.profiles;
		if (auto profilesIt = migrated.find(kProfilesFieldName.data()); profilesIt != migrated.end() && profilesIt->is_array()) {
			for (auto& profile : *profilesIt) {
				MigrateLegacyProfileLighting(profile);
				MigrateLegacyProfileBloom(
					profile,
					globalBloomProfile,
					globalBloomEnabled,
					hasLegacyGlobalBloom,
					createdProfilesFromLegacyGlobal && hasLegacyGlobalBloom);
				MigrateLegacyProfileWaterAppearance(
					profile,
					globalWaterProfile,
					hasLegacyGlobalWater,
					forceLegacyGlobalWater || (createdProfilesFromLegacyGlobal && hasLegacyGlobalWater));
			}
		}
		if (auto overridesIt = migrated.find(kLocationOverridesFieldName.data()); overridesIt != migrated.end() && overridesIt->is_array()) {
			for (auto& locationOverride : *overridesIt) {
				if (!locationOverride.is_object())
					continue;
				if (auto profileIt = locationOverride.find("profile"); profileIt != locationOverride.end()) {
					MigrateLegacyProfileLighting(*profileIt);
					MigrateLegacyProfileBloom(*profileIt, globalBloomProfile, globalBloomEnabled, hasLegacyGlobalBloom);
					MigrateLegacyProfileWaterAppearance(*profileIt, globalWaterProfile, hasLegacyGlobalWater, forceLegacyGlobalWater);
				}
			}
		}

		migrated.erase("rendererControlsEnabled");
		migrated.erase("globalLightingEnabled");
		migrated.erase("globalProfileEnabled");
		migrated.erase("lighting");
		migrated.erase("waterWind");
		migrated.erase("bloomEnhancement");
		migrated.erase(kLegacyGlobalWaterAppearanceFieldName.data());
		NormalizeAdaptiveBalanceSettings(migrated);
		return migrated;
	}

	json MakePresetMetadata(std::string_view a_name, PresetKind a_kind, std::string_view a_type, std::string_view a_description)
	{
		return {
			{ "feature", AdaptiveBrightness::kFeatureShortName.data() },
			{ "modName", GetPresetModName(a_name, a_kind) },
			{ "type", std::string(a_type) },
			{ "version", kPresetVersion },
			{ "description", std::string(a_description) }
		};
	}

	json MakeBasePresetJson(const AdaptiveBrightness::Settings& a_settings, std::string_view a_name, PresetKind a_kind, std::string_view a_type, std::string_view a_description)
	{
		return {
			{ "globalProfile", a_settings.globalProfile },
			{ "dayStartHour", a_settings.dayStartHour },
			{ "nightStartHour", a_settings.nightStartHour },
			{ "transitionHours", a_settings.transitionHours },
			{ "profiles", a_settings.profiles },
			{ "_metadata", MakePresetMetadata(a_name, a_kind, a_type, a_description) }
		};
	}

	bool NormalizeImportedLocationOverride(AdaptiveBrightness::LocationOverride& a_locationOverride)
	{
		if (a_locationOverride.name.empty())
			a_locationOverride.name = a_locationOverride.key;

		bool changedLookup = false;
		if (a_locationOverride.type != kOverrideTypeLocation &&
			a_locationOverride.type != kOverrideTypeCell &&
			a_locationOverride.type != kOverrideTypeWorldspace &&
			a_locationOverride.type != kOverrideTypeRegion &&
			a_locationOverride.type != kOverrideTypeCity) {
			a_locationOverride.type = kOverrideTypeLocation;
			changedLookup = true;
		}

		ClampProfileSettings(a_locationOverride.profile);
		return changedLookup;
	}

	LocationOverrideImportStats ParseLocationOverridesJson(const json& a_json, std::vector<AdaptiveBrightness::LocationOverride>& o_locationOverrides)
	{
		LocationOverrideImportStats stats;
		for (const auto& entry : a_json) {
			try {
				auto migratedEntry = entry;
				if (auto profileIt = migratedEntry.find("profile"); profileIt != migratedEntry.end()) {
					MigrateLegacyProfileLighting(*profileIt);
					MigrateLegacyProfileBloom(*profileIt, Bloom::Profile{}, false, false);
					MigrateLegacyProfileWaterAppearance(*profileIt, WaterAppearance::Profile{}, false);
				}
				auto locationOverride = migratedEntry.get<AdaptiveBrightness::LocationOverride>();
				if (!IsValidFormKey(locationOverride.key)) {
					++stats.skipped;
					continue;
				}

				NormalizeImportedLocationOverride(locationOverride);
				o_locationOverrides.push_back(std::move(locationOverride));
				++stats.imported;
			} catch (const json::exception&) {
				++stats.skipped;
			}
		}

		return stats;
	}

	bool NormalizeLocationOverrideList(std::vector<AdaptiveBrightness::LocationOverride>& a_locationOverrides)
	{
		bool changedLookup = false;

		for (auto it = a_locationOverrides.begin(); it != a_locationOverrides.end();) {
			if (!IsValidFormKey(it->key)) {
				it = a_locationOverrides.erase(it);
				changedLookup = true;
				continue;
			}

			changedLookup = NormalizeImportedLocationOverride(*it) || changedLookup;
			++it;
		}

		if (a_locationOverrides.size() > 1) {
			std::unordered_set<std::string> seenKeys;
			std::vector<AdaptiveBrightness::LocationOverride> dedupedOverrides;
			dedupedOverrides.reserve(a_locationOverrides.size());

			for (auto it = a_locationOverrides.rbegin(); it != a_locationOverrides.rend(); ++it) {
				if (seenKeys.insert(it->key).second)
					dedupedOverrides.push_back(*it);
				else
					changedLookup = true;
			}

			if (changedLookup) {
				std::reverse(dedupedOverrides.begin(), dedupedOverrides.end());
				a_locationOverrides = std::move(dedupedOverrides);
			}
		}

		return changedLookup;
	}

	const json* FindBasePresetJson(const json& a_json)
	{
		if (!a_json.is_object())
			return nullptr;

		if (auto it = a_json.find(kProfilesFieldName.data()); it != a_json.end() && it->is_array())
			return &a_json;

		if (auto it = a_json.find("settings"); it != a_json.end() && it->is_object()) {
			if (auto profilesIt = it->find(kProfilesFieldName.data()); profilesIt != it->end() && profilesIt->is_array())
				return &*it;
		}

		for (const auto* featureName : { AdaptiveBrightness::kFeatureName.data(), AdaptiveBrightness::kFeatureShortName.data(), SettingsMigrations::kLegacyAdaptiveBrightnessSettingsName.data(), "AdaptiveBalance" }) {
			if (auto it = a_json.find(featureName); it != a_json.end() && it->is_object()) {
				if (auto profilesIt = it->find(kProfilesFieldName.data()); profilesIt != it->end() && profilesIt->is_array())
					return &*it;
			}
		}

		return nullptr;
	}

	bool ApplyBasePresetJson(const json& a_json, AdaptiveBrightness::Settings& a_settings, std::string& o_status)
	{
		const auto* presetJson = FindBasePresetJson(a_json);
		if (!presetJson) {
			o_status = "Import failed: no profile data was found.";
			return false;
		}

		try {
			const auto migratedPreset = MigrateLegacyProfileSettings(*presetJson);
			auto importedSettings = a_settings;
			auto importedProfiles = migratedPreset.at(kProfilesFieldName.data()).get<std::array<AdaptiveBrightness::ProfileSettings, AdaptiveBrightness::kProfileCount>>();

			if (const auto it = migratedPreset.find("globalProfile"); it != migratedPreset.end() && it->is_object())
				importedSettings.globalProfile = it->get<AdaptiveBrightness::ProfileSettings>();

			importedSettings.dayStartHour = GetOptionalFloat(migratedPreset, "dayStartHour", importedSettings.dayStartHour);
			importedSettings.nightStartHour = GetOptionalFloat(migratedPreset, "nightStartHour", importedSettings.nightStartHour);
			importedSettings.transitionHours = GetOptionalFloat(migratedPreset, "transitionHours", importedSettings.transitionHours);
			importedSettings.profiles = std::move(importedProfiles);
			NormalizeBaseSettings(importedSettings);
			a_settings = std::move(importedSettings);
			return true;
		} catch (const json::exception& e) {
			o_status = std::format("Import failed: invalid profile data ({})", e.what());
			return false;
		}
	}

	const json* FindLocationOverridesJson(const json& a_json)
	{
		if (a_json.is_array())
			return &a_json;

		if (!a_json.is_object())
			return nullptr;

		if (auto it = a_json.find(kLocationOverridesFieldName.data()); it != a_json.end())
			return &*it;

		for (const auto* featureName : { AdaptiveBrightness::kFeatureName.data(), AdaptiveBrightness::kFeatureShortName.data(), SettingsMigrations::kLegacyAdaptiveBrightnessSettingsName.data(), "AdaptiveBalance" }) {
			if (auto it = a_json.find(featureName); it != a_json.end() && it->is_object()) {
				if (auto overridesIt = it->find(kLocationOverridesFieldName.data()); overridesIt != it->end())
					return &*overridesIt;
			}
		}

		return nullptr;
	}

	std::string GetFormDisplayName(const RE::TESForm* a_form)
	{
		if (!a_form)
			return "";

		const auto* name = a_form->GetName();
		if (name && name[0] != '\0')
			return name;

		auto editorID = Util::GetFormEditorID(a_form);
		if (!editorID.empty())
			return editorID;

		return Util::GetFormFileKey(a_form);
	}

	std::string GetCellCocCode(const RE::TESObjectCELL* a_cell)
	{
		if (!a_cell)
			return "";

		return Util::GetFormEditorID(a_cell);
	}

	const char* GetCocLabel(const std::string& a_cocCode)
	{
		return a_cocCode.empty() ? "-" : a_cocCode.c_str();
	}

	void DrawTableWrappedText(const char* a_text)
	{
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
		ImGui::TextUnformatted(a_text);
		ImGui::PopTextWrapPos();
	}

	void DrawHintText(const char* a_text)
	{
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
		ImGui::TextDisabled("%s", a_text);
		ImGui::PopTextWrapPos();
	}

	void DrawPresetNameInput(
		const char* a_label,
		const char* a_id,
		std::string& a_name,
		const std::filesystem::path& a_exportPath,
		const std::filesystem::path* a_alternateImportPath = nullptr,
		float a_width = 0.0f)
	{
		if (a_label && a_label[0] != '\0') {
			ImGui::TextUnformatted(a_label);
			ImGui::SameLine();
		}
		const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
		const float requestedWidth = a_width > 0.0f ?
		                                 a_width :
		                                 std::min(280.0f * Util::GetUIScale(), std::max(120.0f, availableWidth * 0.45f));
		const float inputWidth = std::clamp(requestedWidth, 1.0f, availableWidth);
		ImGui::SetNextItemWidth(inputWidth);
		ImGui::InputTextWithHint(a_id, "Preset name", &a_name);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextWrapped("Export path: %s", a_exportPath.string().c_str());
			if (a_alternateImportPath)
				ImGui::TextWrapped("Import also accepts: %s", a_alternateImportPath->string().c_str());
		}
	}

	int CompareString(const std::string& a_lhs, const std::string& a_rhs)
	{
		if (a_lhs == a_rhs)
			return 0;

		return a_lhs < a_rhs ? -1 : 1;
	}

	const RE::TESObjectCELL* GetCurrentPlayerCell(const RE::PlayerCharacter* a_player)
	{
		return a_player ? a_player->GetParentCell() : nullptr;
	}

	const RE::TESWorldSpace* GetLocationWorldspace(const RE::BGSLocation* a_location)
	{
		std::size_t depth = 0;
		for (auto* current = a_location; current && depth < kMaxOverrideHierarchyDepth; current = current->parentLoc, ++depth) {
			const auto marker = current->worldLocMarker.get();
			if (marker) {
				if (const auto* worldspace = marker->GetWorldspace())
					return worldspace;
			}
		}

		return nullptr;
	}

	const RE::BGSLocation* GetNearestLocationWithKeyword(
		const RE::BGSLocation* a_location,
		std::initializer_list<std::string_view> a_keywords)
	{
		std::size_t depth = 0;
		for (auto* current = a_location; current && depth < kMaxOverrideHierarchyDepth; current = current->parentLoc, ++depth) {
			for (const auto keyword : a_keywords) {
				if (current->HasKeywordString(keyword))
					return current;
			}
		}

		return nullptr;
	}

	const RE::BGSLocation* FindLocationInHierarchy(const RE::BGSLocation* a_location, const std::string& a_key)
	{
		std::size_t depth = 0;
		for (auto* current = a_location; current && depth < kMaxOverrideHierarchyDepth; current = current->parentLoc, ++depth) {
			if (Util::GetFormFileKey(current) == a_key)
				return current;
		}

		return nullptr;
	}

	CurrentLocationForms GetCurrentLocationForms()
	{
		const auto* player = RE::PlayerCharacter::GetSingleton();
		const auto* cell = GetCurrentPlayerCell(player);
		if (!cell)
			return {};

		auto* location = player->GetCurrentLocation();
		if (!location)
			location = cell->GetLocation();

		// Interior cells do not expose the exterior runtime-data member. Resolve
		// their containing worldspace through location metadata instead.
		const auto* worldspace = cell->IsExteriorCell() ? cell->GetRuntimeData().worldSpace : nullptr;
		if (!worldspace)
			worldspace = GetLocationWorldspace(location);
		if (!worldspace) {
			const auto* tes = RE::TES::GetSingleton();
			if (tes && tes->interiorCell == cell)
				worldspace = tes->GetRuntimeData2().worldSpace;
		}

		return {
			.worldspace = worldspace,
			.location = location,
			.cell = cell,
		};
	}

	bool LocationHasAnyKeyword(const RE::BGSLocation* a_location, std::initializer_list<std::string_view> a_keywords)
	{
		return GetNearestLocationWithKeyword(a_location, a_keywords) != nullptr;
	}
}

void AdaptiveBrightness::DrawSettingsHeaderControls()
{
	if (ImGui::Checkbox("Enable Adaptive Profiles", &settings.enabled))
		ResetWaterWindSmoothing();
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Blend the active lighting, atmosphere, Bloom, and water appearance profile by location and exterior time.");
		ImGui::Text("Every base and location layer uses the same Lighting, Bloom, Water, and wind controls.");
	}

	if (settings.enabled) {
		const auto contextLabel = GetContextLabel();
		ImGui::TextWrapped("%s", contextLabel.c_str());
	}
}

void AdaptiveBrightness::DrawSettings()
{
	const auto contextSectionToSelect = SyncContextSection();
	const ImGuiTabItemFlags profileSectionFlags =
		contextSectionToSelect == ContextSection::Profiles ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
	const ImGuiTabItemFlags locationSectionFlags =
		contextSectionToSelect == ContextSection::Locations ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;

	if (ImGui::BeginTabBar("##AdaptiveBalanceSections", ImGuiTabBarFlags_None)) {
		if (ImGui::BeginTabItem("Global")) {
			ImGui::TextWrapped("Set the shared Lighting, Bloom, and Water adjustments applied before the active profile and location layers.");
			DrawGlobalPresetControls();
			DrawGlobalSettings(true);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Profiles", nullptr, profileSectionFlags)) {
			ImGui::TextWrapped("Tune the lighting, atmosphere, Bloom, and water appearance used for each time and location type. Context profiles are ordered from broad Worldspace and Location scopes to specific Cities; exact locations and cells remain under Locations.");
			if (!settings.enabled)
				ImGui::TextDisabled("Adaptive profile switching is off. Saved profile values can still be reviewed.");

			ImGui::BeginDisabled(!settings.enabled);
			DrawExteriorTimeSettings();
			ImGui::EndDisabled();

			const auto profileTabToSelect = SyncSelectedProfileTabToContext();
			const auto contextScopeToSelect = profileTabToSelect ?
			                                      GetCurrentContextOverrideScope(GetActiveLocationOverride()) :
			                                      std::nullopt;
			if (ImGui::BeginTabBar("##AdaptiveBrightnessProfiles", ImGuiTabBarFlags_None)) {
				for (auto profile : kProfileOrder) {
					const ImGuiTabItemFlags tabFlags =
						!contextScopeToSelect && profileTabToSelect && *profileTabToSelect == profile ?
							ImGuiTabItemFlags_SetSelected :
							ImGuiTabItemFlags_None;
					if (ImGui::BeginTabItem(GetProfileName(profile), nullptr, tabFlags)) {
						DrawProfile(profile, settings.enabled);
						ImGui::EndTabItem();
					}

					if (profile == Profile::Interior) {
						for (auto scope : kContextProfileOrder) {
							DrawCurrentContextProfileTab(
								scope,
								true,
								settings.enabled,
								contextScopeToSelect && *contextScopeToSelect == scope);
						}
					}
				}
				ImGui::EndTabBar();
			}
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Locations", nullptr, locationSectionFlags)) {
			ImGui::TextWrapped("Create precise profile overrides for worldspaces, regional locations, cities, specific locations, or exact cells.");
			if (!settings.enabled)
				ImGui::TextDisabled("Adaptive profile switching is off. Saved overrides can still be reviewed.");
			DrawLocationOverrides(false, true, settings.enabled);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Presets")) {
			ImGui::TextWrapped("Import or export location override collections and complete balance configurations. The Worldspace, Locations, and Cities profile tabs also provide JSON presets for their current scope.");
			DrawLocationOverridePresetControls();
			DrawFullPresetControls();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
}

void AdaptiveBrightness::DrawProfileControlTabs(
	ProfileSettings& a_profile,
	const char* a_tabBarID,
	bool a_showAdvancedControls,
	bool a_globalLayer,
	bool a_allowEdits)
{
	if (ImGui::BeginTabBar(a_tabBarID, ImGuiTabBarFlags_None)) {
		const auto drawTab = [&](const char* a_label, const auto& a_drawControls) {
			if (!ImGui::BeginTabItem(a_label))
				return;

			ImGui::BeginDisabled(!a_allowEdits);
			a_drawControls();
			ImGui::EndDisabled();
			ImGui::EndTabItem();
		};
		drawTab("Lighting", [&] { DrawLightingSettings(a_profile, a_showAdvancedControls, a_globalLayer); });
		drawTab("Bloom", [&] { DrawBloomSettings(a_profile, a_showAdvancedControls, a_globalLayer); });
		drawTab("Water", [&] { DrawWaterSettings(a_profile, a_showAdvancedControls, a_globalLayer); });

		ImGui::EndTabBar();
	}
}

void AdaptiveBrightness::DrawGlobalSettings(bool a_showAdvancedControls)
{
	ClampProfileSettings(settings.globalProfile);
	DrawProfileControlTabs(
		settings.globalProfile,
		"##AdaptiveBalanceGlobalSettings",
		a_showAdvancedControls,
		true,
		true);
	ClampProfileSettings(settings.globalProfile);
}

void AdaptiveBrightness::DrawEssentialSettings()
{
	ImGui::TextWrapped("Set the shared Lighting, Bloom, and Water adjustments.");
	DrawGlobalPresetControls();
	DrawGlobalSettings(false);
}

void AdaptiveBrightness::DrawPerformanceSettings(bool a_advanced)
{
	DrawGlobalSettings(a_advanced);
}

json AdaptiveBrightness::CapturePerformanceSettingsState() const
{
	return {
		{ "globalProfile", settings.globalProfile }
	};
}

void AdaptiveBrightness::SetPerformanceCostMeasurementEnabled(bool a_enabled)
{
	performanceCostMeasurementEnabled = a_enabled;
}

void AdaptiveBrightness::LoadSettings(json& o_json)
{
	settings = MigrateLegacyProfileSettings(o_json);
	NormalizeBaseSettings(settings);
	globalPresetStatus.clear();
	if (globalPresetName.empty())
		globalPresetName = kDefaultGlobalPresetName;
	locationOverridePresetStatus.clear();
	if (locationOverridePresetName.empty())
		locationOverridePresetName = kDefaultLocationOverridePresetName;
	for (std::size_t i = 0; i < contextPresetNames.size(); ++i) {
		contextPresetStatuses[i].clear();
		if (contextPresetNames[i].empty())
			contextPresetNames[i] = kDefaultLocationOverridePresetName;
	}
	fullPresetStatus.clear();
	if (fullPresetName.empty())
		fullPresetName = kDefaultFullPresetName;
	ClearLocationOverrideSelection();
	InvalidateProfileTabSync();
	NormalizeLocationOverrides();
	MarkLocationOverrideLookupDirty();
	ResetWaterWindSmoothing();
}

void AdaptiveBrightness::SaveSettings(json& o_json)
{
	NormalizeBaseSettings(settings);
	NormalizeLocationOverrides();
	o_json = settings;
}

void AdaptiveBrightness::RestoreDefaultSettings()
{
	settings = {};
	NormalizeBaseSettings(settings);
	globalPresetName = kDefaultGlobalPresetName;
	globalPresetStatus.clear();
	locationOverridePresetName = kDefaultLocationOverridePresetName;
	locationOverridePresetStatus.clear();
	contextPresetNames.fill(kDefaultLocationOverridePresetName);
	contextPresetStatuses.fill(std::string{});
	fullPresetName = kDefaultFullPresetName;
	fullPresetStatus.clear();
	ClearLocationOverrideSelection();
	InvalidateProfileTabSync();
	MarkLocationOverrideLookupDirty();
	ResetWaterWindSmoothing();
}

void AdaptiveBrightness::SetupResources()
{
	vanillaPointLightCB = new ConstantBuffer(
		ConstantBufferDesc<VanillaPointLightData>(),
		"AdaptiveBalance::VanillaPointLightData");
}

const char* AdaptiveBrightness::GetProfileName(Profile a_profile)
{
	return kProfileNames[ProfileIndex(a_profile)];
}

std::optional<AdaptiveBrightness::Profile> AdaptiveBrightness::SyncSelectedProfileTabToContext()
{
	if (!GetCurrentPlayerCell(RE::PlayerCharacter::GetSingleton())) {
		profileTabSyncState = {};
		return std::nullopt;
	}

	const auto currentProfile = GetCurrentProfileForUI();
	std::string currentProfileTabSyncKey = std::to_string(static_cast<uint32_t>(currentProfile));
	const auto* activeOverride = GetActiveLocationOverride();
	if (activeOverride) {
		currentProfileTabSyncKey += ':';
		currentProfileTabSyncKey += activeOverride->key;
		currentProfileTabSyncKey += ':';
		currentProfileTabSyncKey += activeOverride->type;
	}
	const auto* worldspaceOverride = GetActiveWorldspaceOverride();
	if (worldspaceOverride && (!activeOverride || worldspaceOverride->key != activeOverride->key)) {
		currentProfileTabSyncKey += ":worldspace:";
		currentProfileTabSyncKey += worldspaceOverride->key;
	}
	const auto forms = GetCurrentLocationForms();
	if (forms.worldspace) {
		const auto currentWorldspaceKey = Util::GetFormFileKey(forms.worldspace);
		if (IsValidFormKey(currentWorldspaceKey)) {
			currentProfileTabSyncKey += ":current-worldspace:";
			currentProfileTabSyncKey += currentWorldspaceKey;
		}
	}

	auto& syncState = profileTabSyncState;
	const int currentFrame = ImGui::GetFrameCount();
	const bool profileTabsWereVisible =
		syncState.lastDrawFrame >= 0 &&
		(currentFrame == syncState.lastDrawFrame || currentFrame == syncState.lastDrawFrame + 1);
	syncState.lastDrawFrame = currentFrame;

	if (profileTabsWereVisible && syncState.initialized && syncState.key == currentProfileTabSyncKey)
		return std::nullopt;

	syncState.key = std::move(currentProfileTabSyncKey);
	syncState.initialized = true;
	return currentProfile;
}

std::optional<AdaptiveBrightness::ContextSection> AdaptiveBrightness::SyncContextSection()
{
	const auto* activeOverride = GetActiveLocationOverride();
	const auto contextScope = GetCurrentContextOverrideScope(activeOverride);
	std::string currentKey = "base";
	if (activeOverride) {
		currentKey = std::format(
			"override:{}:{}:{}",
			activeOverride->key,
			activeOverride->type,
			contextScope ? ContextScopeIndex(*contextScope) : ContextScopeIndex(ContextProfileScope::Count));
	}
	const int currentFrame = ImGui::GetFrameCount();
	const bool sectionsWereVisible =
		contextSectionLastDrawFrame >= 0 &&
		(currentFrame == contextSectionLastDrawFrame || currentFrame == contextSectionLastDrawFrame + 1);
	contextSectionLastDrawFrame = currentFrame;

	if (sectionsWereVisible && contextSectionSyncInitialized && contextSectionSyncKey == currentKey)
		return std::nullopt;

	contextSectionSyncKey = currentKey;
	contextSectionSyncInitialized = true;
	if (activeOverride) {
		selectedLocationOverrideKey = activeOverride->key;
		return contextScope ? ContextSection::Profiles : ContextSection::Locations;
	}

	return std::nullopt;
}

void AdaptiveBrightness::DrawExteriorTimeSettings()
{
	ImGui::SeparatorText("Exterior Schedule");
	ImGui::SliderFloat("Day Blend Start", &settings.dayStartHour, 0.0f, 24.0f, "%.1f h");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Hour when the Exterior Day profile starts blending in.");
	}

	ImGui::SliderFloat("Night Blend Start", &settings.nightStartHour, 0.0f, 24.0f, "%.1f h");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Hour when the Exterior Night profile starts blending in.");
	}

	ImGui::SliderFloat("Blend Duration", &settings.transitionHours, 0.0f, 4.0f, "%.1f h");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Hours used to blend between Exterior Day and Exterior Night.");
	}

	NormalizeExteriorTimeSettings(settings);
}

void AdaptiveBrightness::DrawProfile(Profile a_profile, bool a_allowEdits)
{
	auto& profile = settings.profiles[ProfileIndex(a_profile)];

	ImGui::PushID(static_cast<int>(ProfileIndex(a_profile)));
	DrawProfileSettings(profile, "Profile Values", true, a_allowEdits);
	ImGui::PopID();
}

void AdaptiveBrightness::DrawLocationOverrideProfileEditor(
	LocationOverride& a_locationOverride,
	const char* a_sectionTitle,
	bool a_showAdvancedControls,
	bool a_allowEdits,
	const char* a_saveLabel,
	bool a_closeWhenFinished)
{
	ImGui::PushID(a_locationOverride.key.c_str());
	ImGui::TextDisabled(
		a_locationOverride.layered ?
			"Adjustment layer: applied after broader location layers." :
			"Legacy profile: replaces broader contextual layers to preserve its saved appearance.");
	if (a_allowEdits) {
		if (auto* editProfile = GetLocationOverrideEditProfile(a_locationOverride)) {
			DrawProfileSettings(*editProfile, a_sectionTitle, a_showAdvancedControls, true);

			const bool saveEdit = ImGui::Button(a_saveLabel);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Apply these profile values to the current settings. Use the main Save button to persist them.");
			ImGui::SameLine();
			const bool cancelEdit = ImGui::Button("Cancel");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Discard unsaved edits to this profile.");

			if (saveEdit)
				a_locationOverride.profile = *editProfile;
			if (saveEdit || cancelEdit) {
				if (a_closeWhenFinished)
					ClearLocationOverrideSelection();
				else
					ResetLocationOverrideEdit();
			}
		}
	} else {
		auto viewProfile = a_locationOverride.profile;
		DrawProfileSettings(viewProfile, a_sectionTitle, a_showAdvancedControls, false);
		if (a_closeWhenFinished && ImGui::Button("Close"))
			ClearLocationOverrideSelection();
	}
	ImGui::PopID();
}

void AdaptiveBrightness::DrawCurrentContextProfileTab(
	ContextProfileScope a_scope,
	bool a_showAdvancedControls,
	bool a_allowEdits,
	bool a_select)
{
	const auto tabId = std::format(
		"{}###AdaptiveBalanceContextProfile{}",
		GetContextScopeTabLabel(a_scope),
		ContextScopeIndex(a_scope));
	const ImGuiTabItemFlags tabFlags = a_select ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
	const bool drawTab = ImGui::BeginTabItem(tabId.c_str(), nullptr, tabFlags);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", GetContextScopeDescription(a_scope));
		ImGui::Text("Profiles continue into descendant locations and interiors.");
		ImGui::Text("Exact-cell and more specific location profiles keep priority.");
	}
	if (!drawTab)
		return;

	const auto targets = GetCurrentLocationOverrideTargets();
	const auto* target = GetContextTarget(targets, a_scope);
	if (!target) {
		ImGui::TextDisabled("The current cell has no associated %s scope.", GetContextScopeName(a_scope));
		ImGui::TextWrapped("This tab remains available so the profile layout does not change while cells are loading or when this scope is not present in the current location hierarchy.");
		ImGui::EndTabItem();
		return;
	}

	ImGui::TextWrapped("Current %s: %s", GetContextScopeName(a_scope), target->name.c_str());
	ImGui::TextDisabled("%s form %s", target->type.c_str(), target->key.c_str());
	ImGui::TextWrapped("%s", GetContextScopeDescription(a_scope));

	auto* contextProfile = FindLocationOverride(target->key);
	const auto* inheritedProfile = contextProfile ? nullptr : GetInheritedLocationOverride(*target);

	if (!contextProfile) {
		if (inheritedProfile) {
			ImGui::TextWrapped("%s currently inherits the saved broader profile %s. Create an adjustment layer here to refine Lighting, Bloom, and Water.", target->name.c_str(), inheritedProfile->name.c_str());
		} else {
			ImGui::TextWrapped("%s currently inherits the %s base profile. Create an adjustment layer here to refine Lighting, Bloom, and Water.", target->name.c_str(), GetProfileName(target->defaultProfile));
		}
		ImGui::BeginDisabled(!a_allowEdits);
		const auto createLabel = std::format("Create {} Profile", GetContextScopeName(a_scope));
		if (ImGui::Button(createLabel.c_str()))
			SaveCurrentLocationOverride(*target);
		ImGui::EndDisabled();
		DrawContextProfilePresetControls(a_scope, *target, nullptr, a_allowEdits);
		ImGui::EndTabItem();
		return;
	}

	ImGui::TextWrapped("%s profile for %s.", GetContextScopeName(a_scope), contextProfile->name.c_str());
	DrawLocationOverrideProfileEditor(
		*contextProfile,
		"Profile Values",
		a_showAdvancedControls,
		a_allowEdits,
		"Save Profile",
		false);
	DrawContextProfilePresetControls(a_scope, *target, contextProfile, a_allowEdits);
	ImGui::EndTabItem();
}

void AdaptiveBrightness::DrawContextProfilePresetControls(
	ContextProfileScope a_scope,
	const LocationOverrideTarget& a_target,
	const LocationOverride* a_locationOverride,
	bool a_allowEdits)
{
	ImGui::SeparatorText("Share Profile");
	DrawHintText("Export stores this scope's Lighting, Bloom, Water, wind, and layer mode as a portable JSON profile. Import applies them to the current scope without changing other saved profiles.");

	const auto scopeIndex = ContextScopeIndex(a_scope);
	auto& presetName = contextPresetNames[scopeIndex];
	auto& presetStatus = contextPresetStatuses[scopeIndex];
	const auto presetKind = GetContextScopePresetKind(a_scope);
	const auto presetPath = GetPresetPath(presetName, presetKind);

	ImGui::PushID(static_cast<int>(scopeIndex));
	const auto inputLabel = std::format("{} profile", GetContextScopeName(a_scope));
	DrawPresetNameInput(inputLabel.c_str(), "##ContextProfilePresetName", presetName, presetPath);

	ImGui::SameLine();
	ImGui::BeginDisabled(!a_locationOverride);
	if (ImGui::Button("Export Profile") && a_locationOverride)
		ExportContextProfile(a_scope, a_target, *a_locationOverride);
	ImGui::EndDisabled();
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Export the current %s profile to JSON.", GetContextScopeName(a_scope));

	ImGui::SameLine();
	ImGui::BeginDisabled(!a_allowEdits);
	if (ImGui::Button("Import Profile"))
		ImportContextProfile(a_scope, a_target);
	ImGui::EndDisabled();
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Apply a shared JSON profile to the current %s scope.", GetContextScopeName(a_scope));

	if (!presetStatus.empty())
		ImGui::TextWrapped("%s", presetStatus.c_str());
	ImGui::PopID();
}

void AdaptiveBrightness::DrawProfileSettings(ProfileSettings& a_profile, const char* a_sectionTitle, bool a_showAdvancedControls, bool a_allowEdits)
{
	ClampProfileSettings(a_profile);

	ImGui::SeparatorText(a_sectionTitle);
	ImGui::Indent();
	DrawProfileControlTabs(a_profile, "##ProfileControlSections", a_showAdvancedControls, false, a_allowEdits);
	ImGui::Unindent();
	ClampProfileSettings(a_profile);
}

void AdaptiveBrightness::DrawLightingSettings(
	ProfileSettings& a_profile,
	bool a_showAdvancedControls,
	bool a_globalLayer)
{
	ImGui::SliderFloat("Scene Brightness", &a_profile.brightness, kBrightnessMin, kBrightnessMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Master lighting adjustment for this %s layer.", a_globalLayer ? "global" : "profile");

	if (!a_showAdvancedControls)
		return;

	ImGui::Checkbox("Show Detailed Lighting Controls", &a_profile.advanced);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Shows and enables the detailed lighting and atmosphere adjustments for this layer.");
	if (!a_profile.advanced)
		return;

	ImGui::Indent();
	ImGui::SeparatorText("Direct Lighting");
	ImGui::SliderFloat("Sky Brightness", &a_profile.skyBrightnessMult, 0.0f, kGlobalSkyBrightnessMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Directional Light", &a_profile.directionalLightMult, 0.0f, kGlobalLightingMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Point Lights", &a_profile.pointLightMult, 0.0f, kGlobalLightingMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);

	ImGui::SeparatorText("Point-light Type Balance");
	ImGui::TextWrapped("Subtype values multiply the Point Lights adjustment after the active layers are composed.");
	ImGui::SliderFloat("Spotlights", &a_profile.spotlightMult, 0.0f, kGlobalLightingMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Omnidirectional Bulbs", &a_profile.omnidirectionalBulbMult, 0.0f, kGlobalLightingMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::TextWrapped("Linear values target lights authored with linear falloff; they do not require Linear Lighting.");
	ImGui::SliderFloat("Linear Point Lights", &a_profile.linearPointLightMult, 0.0f, kGlobalLightingMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Linear Spotlights", &a_profile.linearSpotlightMult, 0.0f, kGlobalLightingMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Linear Omnidirectional Bulbs", &a_profile.linearOmnidirectionalBulbMult, 0.0f, kGlobalLightingMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);

	ImGui::SeparatorText("Indirect and Material Lighting");
	ImGui::SliderFloat("Ambient", &a_profile.ambientMult, 0.0f, kGlobalLightingMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Emissive", &a_profile.emitColorMult, 0.0f, kGlobalLightingMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Glowmaps", &a_profile.glowmapMult, 0.0f, kGlobalLightingMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Effects", &a_profile.effectLightingMult, 0.0f, kGlobalLightingMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);

	ImGui::SeparatorText("Atmosphere Gamma Offsets");
	ImGui::SliderFloat("Sky", &a_profile.skyGammaOffset, kGammaOffsetMin, kGammaOffsetMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Fog", &a_profile.fogGammaOffset, kGammaOffsetMin, kGammaOffsetMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Fog Transparency", &a_profile.fogAlphaGammaOffset, kGammaOffsetMin, kGammaOffsetMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat("Volumetric Lighting", &a_profile.vlGammaOffset, kGammaOffsetMin, kGammaOffsetMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::Unindent();
}

void AdaptiveBrightness::DrawBloomSettings(
	ProfileSettings& a_profile,
	bool a_showAdvancedControls,
	bool a_globalLayer)
{
	Bloom::DrawProfileControls(a_profile.bloom);
	if (a_globalLayer)
		ImGui::TextDisabled("Global Bloom strength adds to every active profile; detailed shaping can also adjust inherited Bloom.");
	if (!a_showAdvancedControls)
		return;

	ImGui::Checkbox("Show Detailed Bloom Controls", &a_profile.bloomAdvanced);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Shows detailed shaping that can adjust Bloom inherited from earlier layers without adding strength.");
	if (!a_profile.bloomAdvanced)
		return;

	ImGui::Indent();
	Bloom::DrawAdvancedProfileSettings(a_profile.bloom);
	ImGui::Unindent();
}

void AdaptiveBrightness::DrawWaterSettings(
	ProfileSettings& a_profile,
	bool a_showAdvancedControls,
	bool a_globalLayer)
{
	WaterAppearance::DrawProfileControls(a_profile.water);
	if (!a_showAdvancedControls)
		return;

	ImGui::Checkbox("Show Detailed Water Controls", &a_profile.waterAdvanced);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Shows detailed water color, surface, reflection, refraction, and clarity adjustments for this layer.");
	if (a_profile.waterAdvanced) {
		ImGui::Indent();
		ImGui::SliderFloat("Water Color Gamma", &a_profile.waterGammaOffset, kGammaOffsetMin, kGammaOffsetMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Offsets water color gamma for this layer. This is separate from Water Brightness and surface appearance.");
		WaterAppearance::DrawAdvancedProfileSettings(a_profile.water);
		ImGui::Unindent();
	}

	ImGui::SeparatorText("Wind Response");
	DrawWaterWindSettings(a_profile, a_globalLayer);
}

void AdaptiveBrightness::DrawWaterWindSettings(ProfileSettings& a_profile, bool a_globalLayer)
{
	auto& waterWind = a_profile.waterWind;
	if (a_globalLayer) {
		waterWind.overrideEnabled = true;
		if (ImGui::Checkbox("Enable Wind-Driven Waves", &waterWind.enabled))
			ResetWaterWindSmoothing();
	} else {
		if (ImGui::Checkbox("Override Wind Enable", &waterWind.overrideEnabled))
			ResetWaterWindSmoothing();
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Leave off to inherit whether wind-driven waves are enabled by earlier global, base, or location layers.");
		ImGui::BeginDisabled(!waterWind.overrideEnabled);
		if (ImGui::Checkbox("Enable Wind-Driven Waves", &waterWind.enabled))
			ResetWaterWindSmoothing();
		ImGui::EndDisabled();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Reads the active Sky wind speed, including weather-record changes made by mods.");
		ImGui::Text("Interior cells are calm, and response changes are smoothed across weather transitions.");
	}

	SanitizeWaterWindSettings(waterWind);
	const bool layerDisabled = a_globalLayer ? !waterWind.enabled : waterWind.overrideEnabled && !waterWind.enabled;
	ImGui::BeginDisabled(layerDisabled);
	ImGui::SliderFloat("Calm Wave Scale", &waterWind.calmWaveMultiplier, kWaterWindMultiplierMin, kWaterWindMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Multiplies Wave Amplitude at zero wind after the earlier layers are composed.");
	ImGui::SliderFloat("Strong Wind Wave Scale", &waterWind.strongWindWaveMultiplier, kWaterWindMultiplierMin, kWaterWindMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Multiplies Wave Amplitude at maximum wind after the earlier layers are composed.");

	const float windSpeed = GetSmoothedWaterWindSpeed();
	const float waveMultiplier = GetWaterWindWaveMultiplier(waterWind, windSpeed);
	ImGui::TextDisabled("Layer wind: %.0f%%  Wave adjustment: %.2fx", windSpeed * 100.0f, waveMultiplier);
	ImGui::EndDisabled();
}

void AdaptiveBrightness::DrawGlobalPresetControls()
{
	ImGui::SeparatorText("Global Presets");
	DrawHintText("Global presets store the shared Lighting, Bloom, Water, and wind adjustment layer, the five profiles, and exterior timing.");
	DrawHintText("Import overwrites those profile tabs in the current settings. Saved location overrides are not changed.");
	ImGui::PushID("GlobalPresetControls");

	const auto presetPath = GetPresetPath(globalPresetName, PresetKind::Global);
	const auto& style = ImGui::GetStyle();
	const float scale = Util::GetUIScale();
	const float exportButtonWidth = ImGui::CalcTextSize("Export Global").x + style.FramePadding.x * 2.0f;
	const float importButtonWidth = ImGui::CalcTextSize("Import Global").x + style.FramePadding.x * 2.0f;
	const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
	const float inlineButtonWidth = exportButtonWidth + importButtonWidth + style.ItemSpacing.x * 2.0f;
	const bool keepButtonsOnOneLine =
		availableWidth >= exportButtonWidth + importButtonWidth + style.ItemSpacing.x;
	const float minimumInputWidth = 140.0f * scale;
	const bool keepControlsOnOneLine = availableWidth >= minimumInputWidth + inlineButtonWidth;
	const float inputWidth = keepControlsOnOneLine ?
	                             availableWidth - inlineButtonWidth :
	                             std::min(280.0f * scale, availableWidth);
	DrawPresetNameInput(nullptr, "##GlobalPresetName", globalPresetName, presetPath, nullptr, inputWidth);

	if (keepControlsOnOneLine)
		ImGui::SameLine();
	if (ImGui::Button("Export Global")) {
		ExportGlobalPreset();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Export the global adjustment layer, exterior timing, and the five Lighting, Bloom, and Water profiles. Location overrides are not included.");
	}

	if (keepControlsOnOneLine || keepButtonsOnOneLine)
		ImGui::SameLine();
	if (ImGui::Button("Import Global")) {
		ImportGlobalPreset();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Replace the global adjustment layer, exterior timing, and the five Lighting, Bloom, and Water profiles. Saved location overrides stay unchanged.");
	}

	if (!globalPresetStatus.empty())
		ImGui::TextWrapped("%s", globalPresetStatus.c_str());

	ImGui::PopID();
}

void AdaptiveBrightness::DrawLocationOverrides(bool a_includePresetControls, bool a_showAdvancedControls, bool a_allowEdits)
{
	ImGui::SeparatorText("Location Override Profiles");
	DrawHintText("Layers are applied after the base profile from broad to specific: parent and current worldspaces, location hierarchy, then exact cell. Legacy saved profiles replace broader contextual layers at their position to preserve their original appearance.");
	if (a_includePresetControls) {
		DrawHintText("Import adds overrides from a preset to the override list below. Later edits change this list, not the preset file.");
	}

	const auto targets = GetCurrentLocationOverrideTargets();
	const auto* activeOverride = GetActiveLocationOverride();
	const auto& activeLayers = GetActiveLocationLayers();
	std::optional<Profile> currentProfile;
	if (GetCurrentPlayerCell(RE::PlayerCharacter::GetSingleton()))
		currentProfile = GetCurrentProfileForUI();

	if (activeOverride && currentProfile) {
		ImGui::TextWrapped("Using %zu saved location layer%s here; most specific is \"%s\". Base profile: %s.", activeLayers.size(), activeLayers.size() == 1 ? "" : "s", activeOverride->name.c_str(), GetProfileName(*currentProfile));
	} else if (currentProfile) {
		ImGui::TextWrapped("Using base profile %s here. No saved override matches this place.", GetProfileName(*currentProfile));
	} else {
		ImGui::TextDisabled("Current location context is unavailable while no cell is loaded.");
	}

	const auto drawTargetAction = [&](const std::optional<LocationOverrideTarget>& a_target, const char* a_scope, const char* a_description) {
		if (!a_target)
			return;

		const bool hasSavedTarget = FindLocationOverride(a_target->key) != nullptr;
		const auto buttonLabel = std::format("{} {} Override", hasSavedTarget ? "Open" : "Create", a_scope);
		ImGui::PushID(a_target->key.c_str());
		ImGui::BeginDisabled(!a_allowEdits && !hasSavedTarget);
		if (ImGui::Button(buttonLabel.c_str())) {
			if (a_allowEdits) {
				SaveCurrentLocationOverride(*a_target);
			} else {
				ClearLocationOverrideSelection();
				selectedLocationOverrideKey = a_target->key;
			}
		}
		ImGui::EndDisabled();
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", a_description);
		ImGui::SameLine();
		ImGui::TextDisabled("%s", a_target->name.c_str());
		ImGui::TextDisabled("Form %s, %s, COC %s", a_target->type.c_str(), a_target->key.c_str(), GetCocLabel(a_target->cocCode));
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("The COC code is captured from the current cell as a navigation shortcut.");
		ImGui::PopID();
	};

	drawTargetAction(
		targets.worldspace,
		"Worldspace",
		"Applies throughout this game worldspace and its child worldspaces, before more specific location layers.");
	drawTargetAction(
		targets.region,
		"Location",
		"Applies throughout this hold or regional location and its descendants, before more specific layers.");
	drawTargetAction(
		targets.city,
		"City",
		"Applies throughout this city, town, or settlement and its descendants, before specific-location and exact-cell layers.");
	drawTargetAction(
		targets.location,
		"Specific Location",
		"Applies to this specific location and its descendants, before an exact-cell layer.");
	drawTargetAction(
		targets.cell,
		"Exact Cell",
		"Applies only to this exact cell and is composed last.");

	if (!targets.worldspace && !targets.region && !targets.city && !targets.cell && !targets.location) {
		ImGui::TextDisabled("No current worldspace, location, or cell form is available.");
	}
	ImGui::TextDisabled("%zu saved override%s.", settings.locationOverrides.size(), settings.locationOverrides.size() == 1 ? "" : "s");

	if (a_includePresetControls)
		DrawLocationOverridePresetControls();
	ImGui::SeparatorText("Saved Overrides");
	DrawHintText(a_allowEdits ?
					 "These saved overrides are matched by worldspace, regional location, city, specific location, or cell. Click a row to edit it." :
					 "These saved overrides are matched by worldspace, regional location, city, specific location, or cell. Click a row to review it.");

	if (settings.locationOverrides.empty()) {
		ClearLocationOverrideSelection();
		ImGui::TextDisabled("No location overrides saved.");
		return;
	}

	enum LocationOverrideColumn : ImGuiID
	{
		ColLocation,
		ColForm,
		ColKey,
		ColCoc,
		ColActions
	};

	std::vector<std::size_t> sortedIndices(settings.locationOverrides.size());
	std::iota(sortedIndices.begin(), sortedIndices.end(), 0);

	const auto compareOverrides = [&](std::size_t a_lhsIndex, std::size_t a_rhsIndex, const ImGuiTableColumnSortSpecs& a_spec) {
		const auto& lhs = settings.locationOverrides[a_lhsIndex];
		const auto& rhs = settings.locationOverrides[a_rhsIndex];

		int cmp = 0;
		switch (a_spec.ColumnUserID) {
		case ColLocation:
			cmp = CompareString(lhs.name, rhs.name);
			break;
		case ColForm:
			cmp = CompareString(lhs.type, rhs.type);
			break;
		case ColKey:
			cmp = CompareString(lhs.key, rhs.key);
			break;
		case ColCoc:
			cmp = CompareString(lhs.cocCode, rhs.cocCode);
			break;
		default:
			cmp = CompareString(lhs.key, rhs.key);
			break;
		}

		if (cmp == 0)
			cmp = CompareString(lhs.key, rhs.key);

		const bool ascending = a_spec.SortDirection != ImGuiSortDirection_Descending;
		return ascending ? cmp < 0 : cmp > 0;
	};

	if (ImGui::BeginTable("##AdaptiveBrightnessLocationOverrides", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingStretchProp)) {
		const float actionColumnWidth = std::max(
			96.0f * Util::GetUIScale(),
			ImGui::CalcTextSize("Edit").x + ImGui::CalcTextSize("Copy").x + ImGui::GetStyle().FramePadding.x * 4.0f + ImGui::GetStyle().ItemSpacing.x + 8.0f);

		ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch, 2.0f, ColLocation);
		ImGui::TableSetupColumn("Form", ImGuiTableColumnFlags_WidthFixed, 64.0f * Util::GetUIScale(), ColForm);
		ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 1.6f, ColKey);
		ImGui::TableSetupColumn("coc", ImGuiTableColumnFlags_WidthStretch, 2.0f, ColCoc);
		ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, actionColumnWidth, ColActions);
		ImGui::TableHeadersRow();

		if (auto* sortSpecs = ImGui::TableGetSortSpecs(); sortSpecs && sortSpecs->SpecsCount > 0) {
			std::sort(sortedIndices.begin(), sortedIndices.end(), [&](std::size_t a_lhsIndex, std::size_t a_rhsIndex) {
				for (int i = 0; i < sortSpecs->SpecsCount; ++i) {
					const auto& spec = sortSpecs->Specs[i];
					if (compareOverrides(a_lhsIndex, a_rhsIndex, spec))
						return true;
					if (compareOverrides(a_rhsIndex, a_lhsIndex, spec))
						return false;
				}

				return a_lhsIndex < a_rhsIndex;
			});
			sortSpecs->SpecsDirty = false;
		}

		std::size_t deleteIndex = kInvalidLocationOverrideIndex;

		for (const auto overrideIndex : sortedIndices) {
			auto& locationOverride = settings.locationOverrides[overrideIndex];
			const bool selected = selectedLocationOverrideKey == locationOverride.key;
			const auto selectOverride = [&]() {
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
					selectedLocationOverrideKey = locationOverride.key;
			};

			ImGui::PushID(static_cast<int>(overrideIndex));
			ImGui::TableNextRow();
			if (selected) {
				const auto rowColor = ImGui::GetColorU32(ImGuiCol_Header);
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowColor);
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, rowColor);
			}

			ImGui::TableSetColumnIndex(0);
			DrawTableWrappedText(locationOverride.name.c_str());
			selectOverride();

			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(locationOverride.type.c_str());
			selectOverride();

			ImGui::TableSetColumnIndex(2);
			DrawTableWrappedText(locationOverride.key.c_str());
			selectOverride();
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", locationOverride.key.c_str());
			}

			ImGui::TableSetColumnIndex(3);
			DrawTableWrappedText(GetCocLabel(locationOverride.cocCode));
			selectOverride();
			if (auto _tt = Util::HoverTooltipWrapper()) {
				if (locationOverride.cocCode.empty()) {
					ImGui::Text("No cell EditorID was saved for this override.");
				} else {
					ImGui::Text("Console: coc %s", locationOverride.cocCode.c_str());
				}
			}

			ImGui::TableSetColumnIndex(4);
			if (ImGui::SmallButton(a_allowEdits ? "Edit" : "View")) {
				selectedLocationOverrideKey = locationOverride.key;
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(locationOverride.cocCode.empty());
			if (ImGui::SmallButton("Copy")) {
				const auto command = std::format("coc {}", locationOverride.cocCode);
				ImGui::SetClipboardText(command.c_str());
			}
			ImGui::EndDisabled();
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Copy the COC command saved with this override.");
			}
			ImGui::BeginDisabled(!a_allowEdits);
			if (ImGui::SmallButton("Delete")) {
				deleteIndex = overrideIndex;
			}
			ImGui::EndDisabled();

			ImGui::PopID();
		}

		ImGui::EndTable();

		if (deleteIndex != kInvalidLocationOverrideIndex && deleteIndex < settings.locationOverrides.size()) {
			const bool deletingSelectedOverride = selectedLocationOverrideKey == settings.locationOverrides[deleteIndex].key;
			if (deletingSelectedOverride)
				ClearLocationOverrideSelection();

			settings.locationOverrides.erase(settings.locationOverrides.begin() + static_cast<std::ptrdiff_t>(deleteIndex));
			ResetWaterWindSmoothing();
			MarkLocationOverrideLookupDirty();
		}
	}

	if (auto* selectedOverride = FindLocationOverride(selectedLocationOverrideKey)) {
		ImGui::SeparatorText(a_allowEdits ? "Edit Location Override Profile" : "View Location Override Profile");
		ImGui::TextWrapped("%s (%s, %s)", selectedOverride->name.c_str(), selectedOverride->type.c_str(), selectedOverride->key.c_str());
		DrawLocationOverrideProfileEditor(
			*selectedOverride,
			"Override Profile Values",
			a_showAdvancedControls,
			a_allowEdits,
			"Save Edit",
			true);
	} else if (!selectedLocationOverrideKey.empty()) {
		ClearLocationOverrideSelection();
	}
}

void AdaptiveBrightness::DrawLocationOverridePresetControls()
{
	ImGui::SeparatorText("Override Presets");
	DrawHintText("Override presets store all saved worldspace, regional location, city, specific location, and cell overrides. They do not include the five base profile tabs.");
	ImGui::PushID("LocationOverridePresetControls");

	const auto presetPath = GetPresetPath(locationOverridePresetName, PresetKind::Location);
	const auto overridePath = GetLocationOverrideLiveOverridePath(locationOverridePresetName);
	DrawPresetNameInput("Override preset", "##LocationOverridePresetName", locationOverridePresetName, presetPath, &overridePath);

	ImGui::SameLine();
	ImGui::BeginDisabled(settings.locationOverrides.empty());
	if (ImGui::Button("Export Overrides")) {
		ExportLocationOverrides();
	}
	ImGui::EndDisabled();
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Export the saved override list.");
	}

	ImGui::SameLine();
	if (ImGui::Button("Import Overrides")) {
		ImportLocationOverrides();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Add overrides from this preset to the override list below.");
	}

	if (!locationOverridePresetStatus.empty())
		ImGui::TextWrapped("%s", locationOverridePresetStatus.c_str());

	ImGui::PopID();
}

void AdaptiveBrightness::DrawFullPresetControls()
{
	ImGui::SeparatorText("Full Presets");
	DrawHintText("Full presets store the global adjustment layer, exterior timing, the five profile tabs, and all saved worldspace, location, and cell overrides.");
	DrawHintText("Import replaces the profile tabs and the saved override list in the current settings.");
	ImGui::PushID("FullPresetControls");

	const auto presetPath = GetPresetPath(fullPresetName, PresetKind::Full);
	DrawPresetNameInput("Full preset", "##FullPresetName", fullPresetName, presetPath);

	ImGui::SameLine();
	if (ImGui::Button("Export Full")) {
		ExportFullPreset();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Export the global adjustment layer, exterior timing, the five profiles, and all saved overrides.");
	}

	ImGui::SameLine();
	if (ImGui::Button("Import Full")) {
		ImportFullPreset();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Replace the global adjustment layer, exterior timing, the five profiles, and the saved override list.");
	}

	if (!fullPresetStatus.empty())
		ImGui::TextWrapped("%s", fullPresetStatus.c_str());

	ImGui::PopID();
}

bool AdaptiveBrightness::ExportGlobalPreset()
{
	NormalizeBaseSettings(settings);

	const auto path = GetPresetPath(globalPresetName, PresetKind::Global);
	try {
		std::filesystem::create_directories(path.parent_path());

		const auto exportJson = MakeBasePresetJson(
			settings,
			globalPresetName,
			PresetKind::Global,
			"global",
			"Adaptive Balance global preset");

		if (!WriteJsonFileAtomic(path, exportJson)) {
			globalPresetStatus = std::format("Export failed: could not write {}.", path.string());
			return false;
		}

		globalPresetStatus = std::format("Exported global preset to {}.", path.string());
		return true;
	} catch (const std::exception& e) {
		globalPresetStatus = std::format("Export failed: {}", e.what());
		return false;
	}
}

bool AdaptiveBrightness::ImportGlobalPreset()
{
	const auto resolvedPath = ResolvePresetImportPath(globalPresetName, PresetKind::Global);
	if (!resolvedPath) {
		globalPresetStatus = GetPresetNotFoundMessage(globalPresetName, PresetKind::Global);
		return false;
	}

	json importedJson;
	if (!ReadJsonPresetFile(*resolvedPath, importedJson, globalPresetStatus))
		return false;

	if (!ApplyBasePresetJson(importedJson, settings, globalPresetStatus))
		return false;

	ResetWaterWindSmoothing();
	globalPresetStatus = std::format("Imported global preset from {}.", resolvedPath->filename().string());
	return true;
}

bool AdaptiveBrightness::ExportLocationOverrides()
{
	NormalizeLocationOverrides();

	if (settings.locationOverrides.empty()) {
		locationOverridePresetStatus = "Nothing to export: no location overrides are saved.";
		return false;
	}

	const auto path = GetPresetPath(locationOverridePresetName, PresetKind::Location);
	try {
		std::filesystem::create_directories(path.parent_path());

		json exportJson;
		exportJson["locationOverrides"] = settings.locationOverrides;
		exportJson["_metadata"] = MakePresetMetadata(
			locationOverridePresetName,
			PresetKind::Location,
			"locations",
			"Adaptive Balance location override preset");

		if (!WriteJsonFileAtomic(path, exportJson)) {
			locationOverridePresetStatus = std::format("Export failed: could not write {}.", path.string());
			return false;
		}

		locationOverridePresetStatus = std::format("Exported {} location override(s) to {}.", settings.locationOverrides.size(), path.string());
		return true;
	} catch (const std::exception& e) {
		locationOverridePresetStatus = std::format("Export failed: {}", e.what());
		return false;
	}
}

bool AdaptiveBrightness::ImportLocationOverrides()
{
	const auto resolvedPath = ResolvePresetImportPath(locationOverridePresetName, PresetKind::Location);
	if (!resolvedPath) {
		locationOverridePresetStatus = GetPresetNotFoundMessage(locationOverridePresetName, PresetKind::Location);
		return false;
	}

	const auto& path = *resolvedPath;

	json importedJson;
	if (!ReadJsonPresetFile(path, importedJson, locationOverridePresetStatus))
		return false;

	const auto* overridesJson = FindLocationOverridesJson(importedJson);
	if (!overridesJson || !overridesJson->is_array()) {
		locationOverridePresetStatus = "Import failed: no locationOverrides array was found.";
		return false;
	}

	auto mergedOverrides = settings.locationOverrides;
	NormalizeLocationOverrideList(mergedOverrides);

	std::unordered_map<std::string, std::size_t> existingIndices;
	existingIndices.reserve(mergedOverrides.size() + overridesJson->size());
	for (std::size_t i = 0; i < mergedOverrides.size(); ++i) {
		const auto& locationOverride = mergedOverrides[i];
		if (IsValidFormKey(locationOverride.key))
			existingIndices[locationOverride.key] = i;
	}

	std::vector<LocationOverride> importedOverrides;
	importedOverrides.reserve(overridesJson->size());
	auto stats = ParseLocationOverridesJson(*overridesJson, importedOverrides);

	if (stats.imported == 0) {
		locationOverridePresetStatus = std::format("Import failed: no valid location overrides found ({} skipped).", stats.skipped);
		return false;
	}

	std::string lastImportedKey;
	for (auto& locationOverride : importedOverrides) {
		const auto key = locationOverride.key;
		if (auto it = existingIndices.find(key); it != existingIndices.end() && it->second < mergedOverrides.size()) {
			mergedOverrides[it->second] = std::move(locationOverride);
			++stats.replaced;
		} else {
			existingIndices[key] = mergedOverrides.size();
			mergedOverrides.push_back(std::move(locationOverride));
		}

		lastImportedKey = key;
	}

	NormalizeLocationOverrideList(mergedOverrides);
	settings.locationOverrides = std::move(mergedOverrides);
	selectedLocationOverrideKey = std::move(lastImportedKey);
	ResetLocationOverrideEdit();
	ResetWaterWindSmoothing();
	MarkLocationOverrideLookupDirty();
	locationOverridePresetStatus = std::format(
		"Imported {} location override(s) from {} ({} replaced, {} skipped).",
		stats.imported,
		path.filename().string(),
		stats.replaced,
		stats.skipped);
	return true;
}

bool AdaptiveBrightness::ExportContextProfile(
	ContextProfileScope a_scope,
	const LocationOverrideTarget& a_target,
	const LocationOverride& a_locationOverride)
{
	const auto scopeIndex = ContextScopeIndex(a_scope);
	auto& status = contextPresetStatuses[scopeIndex];
	const auto presetKind = GetContextScopePresetKind(a_scope);
	const auto path = GetPresetPath(contextPresetNames[scopeIndex], presetKind);

	auto profile = a_locationOverride.profile;
	ClampProfileSettings(profile);
	try {
		std::filesystem::create_directories(path.parent_path());

		json exportJson{
			{ "profile", profile },
			{ "layered", a_locationOverride.layered },
			{ "scope", { { "key", a_target.key },
						   { "name", a_target.name },
						   { "type", a_target.type } } },
			{ "_metadata", MakePresetMetadata(
							   contextPresetNames[scopeIndex],
							   presetKind,
							   a_target.type,
							   std::format("Adaptive Balance {} profile", GetContextScopeName(a_scope))) }
		};

		if (!WriteJsonFileAtomic(path, exportJson)) {
			status = std::format("Export failed: could not write {}.", path.string());
			return false;
		}

		status = std::format(
			"Exported the {} profile for {} to {}.",
			GetContextScopeName(a_scope),
			a_target.name,
			path.string());
		return true;
	} catch (const std::exception& e) {
		status = std::format("Export failed: {}", e.what());
		return false;
	}
}

bool AdaptiveBrightness::ImportContextProfile(
	ContextProfileScope a_scope,
	const LocationOverrideTarget& a_target)
{
	const auto scopeIndex = ContextScopeIndex(a_scope);
	auto& status = contextPresetStatuses[scopeIndex];
	const auto presetKind = GetContextScopePresetKind(a_scope);
	const auto resolvedPath = ResolvePresetImportPath(contextPresetNames[scopeIndex], presetKind);
	if (!resolvedPath) {
		status = GetPresetNotFoundMessage(contextPresetNames[scopeIndex], presetKind);
		return false;
	}

	json importedJson;
	if (!ReadJsonPresetFile(*resolvedPath, importedJson, status))
		return false;

	const json* profileJson = nullptr;
	if (const auto profileIt = importedJson.find("profile");
		profileIt != importedJson.end() && profileIt->is_object()) {
		profileJson = &*profileIt;
	}

	if (!profileJson) {
		status = "Import failed: no profile object was found.";
		return false;
	}
	const bool layered = GetOptionalBool(importedJson, "layered", false);

	auto migratedProfileJson = *profileJson;
	MigrateLegacyProfileLighting(migratedProfileJson);
	MigrateLegacyProfileBloom(migratedProfileJson, Bloom::Profile{}, false, false);
	MigrateLegacyProfileWaterAppearance(migratedProfileJson, WaterAppearance::Profile{}, false);
	NormalizeJsonObjectWithDefaults(migratedProfileJson, json(ProfileSettings{}));

	ProfileSettings importedProfile;
	try {
		importedProfile = migratedProfileJson.get<ProfileSettings>();
	} catch (const json::exception& e) {
		status = std::format("Import failed: invalid profile data: {}", e.what());
		return false;
	}
	ClampProfileSettings(importedProfile);

	auto* targetOverride = FindLocationOverride(a_target.key);
	if (!targetOverride) {
		settings.locationOverrides.push_back(LocationOverride{
			.key = a_target.key,
			.name = a_target.name,
			.type = a_target.type,
			.cocCode = a_target.cocCode,
			.profile = importedProfile,
			.layered = layered,
		});
	} else {
		targetOverride->name = a_target.name;
		targetOverride->type = a_target.type;
		targetOverride->cocCode = a_target.cocCode;
		targetOverride->profile = importedProfile;
		targetOverride->layered = layered;
	}

	selectedLocationOverrideKey = a_target.key;
	ResetLocationOverrideEdit();
	ResetWaterWindSmoothing();
	MarkLocationOverrideLookupDirty();
	status = std::format(
		"Imported {} for the current {} {}.",
		resolvedPath->filename().string(),
		GetContextScopeName(a_scope),
		a_target.name);
	return true;
}

bool AdaptiveBrightness::ExportFullPreset()
{
	NormalizeBaseSettings(settings);
	NormalizeLocationOverrides();

	const auto path = GetPresetPath(fullPresetName, PresetKind::Full);
	try {
		std::filesystem::create_directories(path.parent_path());

		auto exportJson = MakeBasePresetJson(
			settings,
			fullPresetName,
			PresetKind::Full,
			"full",
			"Adaptive Balance full preset");
		exportJson["locationOverrides"] = settings.locationOverrides;

		if (!WriteJsonFileAtomic(path, exportJson)) {
			fullPresetStatus = std::format("Export failed: could not write {}.", path.string());
			return false;
		}

		fullPresetStatus = std::format("Exported full preset with {} location override(s) to {}.", settings.locationOverrides.size(), path.string());
		return true;
	} catch (const std::exception& e) {
		fullPresetStatus = std::format("Export failed: {}", e.what());
		return false;
	}
}

bool AdaptiveBrightness::ImportFullPreset()
{
	const auto resolvedPath = ResolvePresetImportPath(fullPresetName, PresetKind::Full);
	if (!resolvedPath) {
		fullPresetStatus = GetPresetNotFoundMessage(fullPresetName, PresetKind::Full);
		return false;
	}

	json importedJson;
	if (!ReadJsonPresetFile(*resolvedPath, importedJson, fullPresetStatus))
		return false;

	auto importedSettings = settings;
	if (!ApplyBasePresetJson(importedJson, importedSettings, fullPresetStatus))
		return false;

	std::vector<LocationOverride> importedOverrides;
	LocationOverrideImportStats stats;
	bool hadNonEmptyOverridesArray = false;
	if (const auto* overridesJson = FindLocationOverridesJson(importedJson); overridesJson) {
		if (!overridesJson->is_array()) {
			fullPresetStatus = "Import failed: locationOverrides must be an array when present.";
			return false;
		}

		importedOverrides.reserve(overridesJson->size());
		stats = ParseLocationOverridesJson(*overridesJson, importedOverrides);
		hadNonEmptyOverridesArray = !overridesJson->empty();
	}

	if (hadNonEmptyOverridesArray && stats.imported == 0) {
		fullPresetStatus = std::format("Import failed: no valid location overrides found ({} skipped).", stats.skipped);
		return false;
	}

	importedSettings.locationOverrides = std::move(importedOverrides);
	settings = std::move(importedSettings);
	ResetWaterWindSmoothing();
	ClearLocationOverrideSelection();
	NormalizeLocationOverrides();
	MarkLocationOverrideLookupDirty();
	fullPresetStatus = std::format(
		"Imported full preset from {} ({} location override(s), {} skipped).",
		resolvedPath->filename().string(),
		settings.locationOverrides.size(),
		stats.skipped);
	return true;
}

bool AdaptiveBrightness::IsRuntimeAvailable() const
{
	if (!loaded)
		return false;

	auto state = globals::state;
	if (state && state->IsMainOrLoadingMenuOpen())
		return false;

	return GetCurrentPlayerCell(RE::PlayerCharacter::GetSingleton()) != nullptr;
}

bool AdaptiveBrightness::IsAdjustmentRuntimeActive() const
{
	return performanceCostMeasurementEnabled && IsRuntimeAvailable();
}

bool AdaptiveBrightness::IsRuntimeEnabled() const
{
	return settings.enabled && IsAdjustmentRuntimeActive();
}

AdaptiveBrightness::Profile AdaptiveBrightness::GetInteriorProfile() const
{
	const auto forms = GetCurrentLocationForms();
	const auto* location = forms.location;

	if (LocationHasAnyKeyword(location, { "LocTypeDungeon", "LocTypeMine" }))
		return Profile::Dungeon;

	if (LocationHasAnyKeyword(location, { "LocTypeDwelling" }))
		return Profile::Dwelling;

	return Profile::Interior;
}

AdaptiveBrightness::Profile AdaptiveBrightness::GetCurrentProfileForUI() const
{
	const auto location = LocationContext::Get();
	if (location.inInterior)
		return GetInteriorProfile();

	return GetExteriorNightFactor() >= 0.5f ? Profile::ExteriorNight : Profile::ExteriorDay;
}

const AdaptiveBrightness::LocationOverride* AdaptiveBrightness::GetActiveLocationOverride() const
{
	const auto& layers = GetActiveLocationLayers();
	return layers.empty() ? nullptr : layers.back();
}

const std::vector<const AdaptiveBrightness::LocationOverride*>& AdaptiveBrightness::GetActiveLocationLayers() const
{
	EnsureLocationOverrideLookup();
	const auto forms = GetCurrentLocationForms();
	const uint32_t worldspaceFormID = forms.worldspace ? forms.worldspace->GetFormID() : 0;
	const uint32_t locationFormID = forms.location ? forms.location->GetFormID() : 0;
	const uint32_t cellFormID = forms.cell ? forms.cell->GetFormID() : 0;
	if (locationLayerCache.valid &&
		locationLayerCache.lookupVersion == locationOverrideLookupVersion &&
		locationLayerCache.worldspaceFormID == worldspaceFormID &&
		locationLayerCache.locationFormID == locationFormID &&
		locationLayerCache.cellFormID == cellFormID) {
		return locationLayerCache.layers;
	}

	auto& layers = locationLayerCache.layers;
	layers.clear();
	std::unordered_set<std::string> seenKeys;
	const auto appendLayer = [&](const RE::TESForm* a_form) {
		if (!a_form)
			return;

		const auto* locationOverride = FindLocationOverride(Util::GetFormFileKey(a_form));
		if (!locationOverride)
			return;
		if (seenKeys.insert(locationOverride->key).second)
			layers.push_back(locationOverride);
	};

	std::vector<const RE::TESWorldSpace*> worldspaces;
	for (auto* worldspace = forms.worldspace;
		worldspace && worldspaces.size() < kMaxOverrideHierarchyDepth;
		worldspace = worldspace->parentWorld) {
		worldspaces.push_back(worldspace);
	}
	for (auto it = worldspaces.rbegin(); it != worldspaces.rend(); ++it)
		appendLayer(*it);

	std::vector<const RE::BGSLocation*> locations;
	for (auto* location = forms.location;
		location && locations.size() < kMaxOverrideHierarchyDepth;
		location = location->parentLoc) {
		locations.push_back(location);
	}
	for (auto it = locations.rbegin(); it != locations.rend(); ++it)
		appendLayer(*it);

	appendLayer(forms.cell);
	locationLayerCache.worldspaceFormID = worldspaceFormID;
	locationLayerCache.locationFormID = locationFormID;
	locationLayerCache.cellFormID = cellFormID;
	locationLayerCache.lookupVersion = locationOverrideLookupVersion;
	locationLayerCache.valid = true;
	return layers;
}

const AdaptiveBrightness::LocationOverride* AdaptiveBrightness::GetActiveWorldspaceOverride() const
{
	const auto forms = GetCurrentLocationForms();
	if (!forms.cell)
		return nullptr;

	const auto overrideIndex = ResolveWorldspaceHierarchyOverrideIndex(forms.worldspace);
	if (overrideIndex == kInvalidLocationOverrideIndex || overrideIndex >= settings.locationOverrides.size())
		return nullptr;

	return &settings.locationOverrides[overrideIndex];
}

std::optional<AdaptiveBrightness::ContextProfileScope> AdaptiveBrightness::GetCurrentContextOverrideScope(
	const LocationOverride* a_locationOverride) const
{
	if (!a_locationOverride)
		return std::nullopt;
	const auto* worldspaceOverride = GetActiveWorldspaceOverride();
	if (worldspaceOverride && worldspaceOverride->key == a_locationOverride->key)
		return ContextProfileScope::Worldspace;

	const auto targets = GetCurrentLocationOverrideTargets();
	for (auto scope = kContextProfileOrder.rbegin(); scope != kContextProfileOrder.rend(); ++scope) {
		const auto* target = GetContextTarget(targets, *scope);
		if (target && target->key == a_locationOverride->key)
			return *scope;
	}

	return std::nullopt;
}

AdaptiveBrightness::CurrentLocationOverrideTargets AdaptiveBrightness::GetCurrentLocationOverrideTargets() const
{
	const auto forms = GetCurrentLocationForms();
	if (!forms.cell)
		return {};

	const auto currentProfile = GetCurrentProfileForUI();
	const auto cocCode = GetCellCocCode(forms.cell);

	const auto makeTarget = [&](const RE::TESForm* a_form, std::string_view a_type) -> std::optional<LocationOverrideTarget> {
		if (!a_form)
			return std::nullopt;

		auto key = Util::GetFormFileKey(a_form);
		if (!IsValidFormKey(key))
			return std::nullopt;

		return LocationOverrideTarget{
			.key = std::move(key),
			.name = GetFormDisplayName(a_form),
			.type = std::string(a_type),
			.cocCode = cocCode,
			.defaultProfile = currentProfile,
		};
	};

	auto worldspaceTarget = makeTarget(forms.worldspace, kOverrideTypeWorldspace);
	auto regionTarget = makeTarget(
		GetNearestLocationWithKeyword(forms.location, { "LocTypeHold" }),
		kOverrideTypeRegion);
	auto cityTarget = makeTarget(
		GetNearestLocationWithKeyword(
			forms.location,
			{ "LocTypeCity", "LocTypeTown", "LocTypeSettlement" }),
		kOverrideTypeCity);
	if (regionTarget && cityTarget && regionTarget->key == cityTarget->key)
		regionTarget.reset();
	auto locationTarget = makeTarget(forms.location, kOverrideTypeLocation);
	if (locationTarget &&
		((regionTarget && regionTarget->key == locationTarget->key) ||
			(cityTarget && cityTarget->key == locationTarget->key))) {
		locationTarget.reset();
	}

	return {
		.worldspace = worldspaceTarget,
		.region = regionTarget,
		.city = cityTarget,
		.location = locationTarget,
		.cell = makeTarget(forms.cell, kOverrideTypeCell),
	};
}

const AdaptiveBrightness::LocationOverride* AdaptiveBrightness::GetInheritedLocationOverride(
	const LocationOverrideTarget& a_target) const
{
	const auto forms = GetCurrentLocationForms();
	auto inheritedIndex = kInvalidLocationOverrideIndex;

	if (a_target.type == kOverrideTypeCell) {
		inheritedIndex = ResolveLocationHierarchyOverrideIndex(forms.location);
		if (inheritedIndex == kInvalidLocationOverrideIndex)
			inheritedIndex = ResolveWorldspaceHierarchyOverrideIndex(forms.worldspace);
	} else if (a_target.type == kOverrideTypeLocation ||
			   a_target.type == kOverrideTypeRegion ||
			   a_target.type == kOverrideTypeCity) {
		// Start above the target so a child profile cannot seed a broader scope.
		const auto* targetLocation = FindLocationInHierarchy(forms.location, a_target.key);
		inheritedIndex = ResolveLocationHierarchyOverrideIndex(
			targetLocation ? targetLocation->parentLoc : nullptr);
		if (inheritedIndex == kInvalidLocationOverrideIndex)
			inheritedIndex = ResolveWorldspaceHierarchyOverrideIndex(forms.worldspace);
	} else if (a_target.type == kOverrideTypeWorldspace) {
		inheritedIndex = ResolveWorldspaceHierarchyOverrideIndex(
			forms.worldspace ? forms.worldspace->parentWorld : nullptr);
	}

	if (inheritedIndex == kInvalidLocationOverrideIndex || inheritedIndex >= settings.locationOverrides.size())
		return nullptr;

	return &settings.locationOverrides[inheritedIndex];
}

void AdaptiveBrightness::SaveCurrentLocationOverride(const LocationOverrideTarget& a_target)
{
	if (auto* existingOverride = FindLocationOverride(a_target.key)) {
		existingOverride->name = a_target.name;
		existingOverride->type = a_target.type;
		existingOverride->cocCode = a_target.cocCode;
		ClearLocationOverrideSelection();
		selectedLocationOverrideKey = existingOverride->key;
		MarkLocationOverrideLookupDirty();
		return;
	}

	LocationOverride locationOverride;
	locationOverride.key = a_target.key;
	locationOverride.name = a_target.name;
	locationOverride.type = a_target.type;
	locationOverride.cocCode = a_target.cocCode;

	locationOverride.profile = ProfileSettings::AdjustmentDefaults();
	locationOverride.layered = true;

	selectedLocationOverrideKey = locationOverride.key;
	settings.locationOverrides.push_back(std::move(locationOverride));
	ResetLocationOverrideEdit();
	MarkLocationOverrideLookupDirty();
}

void AdaptiveBrightness::ClearLocationOverrideSelection()
{
	selectedLocationOverrideKey.clear();
	ResetLocationOverrideEdit();
}

void AdaptiveBrightness::InvalidateProfileTabSync()
{
	profileTabSyncState = {};
	contextSectionSyncKey.clear();
	contextSectionSyncInitialized = false;
	contextSectionLastDrawFrame = -1;
}

void AdaptiveBrightness::ResetLocationOverrideEdit()
{
	locationOverrideEditKey.clear();
	locationOverrideEditProfile.reset();
}

AdaptiveBrightness::ProfileSettings* AdaptiveBrightness::GetLocationOverrideEditProfile(LocationOverride& a_locationOverride)
{
	if (locationOverrideEditKey != a_locationOverride.key || !locationOverrideEditProfile) {
		locationOverrideEditKey = a_locationOverride.key;
		locationOverrideEditProfile = a_locationOverride.profile;
	}

	return locationOverrideEditProfile ? &*locationOverrideEditProfile : nullptr;
}

AdaptiveBrightness::LocationOverride* AdaptiveBrightness::FindLocationOverride(const std::string& a_key)
{
	const auto overrideIndex = FindLocationOverrideIndexByKey(a_key);
	if (overrideIndex == kInvalidLocationOverrideIndex)
		return nullptr;

	return &settings.locationOverrides[overrideIndex];
}

const AdaptiveBrightness::LocationOverride* AdaptiveBrightness::FindLocationOverride(const std::string& a_key) const
{
	const auto overrideIndex = FindLocationOverrideIndexByKey(a_key);
	if (overrideIndex == kInvalidLocationOverrideIndex)
		return nullptr;

	return &settings.locationOverrides[overrideIndex];
}

std::size_t AdaptiveBrightness::FindLocationOverrideIndexByKey(const std::string& a_key) const
{
	if (!IsValidFormKey(a_key))
		return kInvalidLocationOverrideIndex;

	EnsureLocationOverrideLookup();

	const auto it = locationOverrideLookup.find(a_key);
	if (it == locationOverrideLookup.end() || it->second >= settings.locationOverrides.size())
		return kInvalidLocationOverrideIndex;

	return it->second;
}

std::size_t AdaptiveBrightness::FindLocationOverrideIndexByForm(const RE::TESForm* a_form) const
{
	if (!a_form)
		return kInvalidLocationOverrideIndex;

	const auto key = Util::GetFormFileKey(a_form);
	return FindLocationOverrideIndexByKey(key);
}

std::size_t AdaptiveBrightness::ResolveWorldspaceHierarchyOverrideIndex(const RE::TESWorldSpace* a_worldspace) const
{
	// Parent chains are normally shallow and acyclic. The cap prevents malformed
	// plugin data from trapping the render thread in an unbounded traversal.
	std::size_t depth = 0;
	for (auto* current = a_worldspace; current && depth < kMaxOverrideHierarchyDepth; current = current->parentWorld, ++depth) {
		const auto resolvedIndex = FindLocationOverrideIndexByForm(current);
		if (resolvedIndex != kInvalidLocationOverrideIndex && resolvedIndex < settings.locationOverrides.size())
			return resolvedIndex;
	}

	return kInvalidLocationOverrideIndex;
}

std::size_t AdaptiveBrightness::ResolveLocationHierarchyOverrideIndex(const RE::BGSLocation* a_location) const
{
	std::size_t depth = 0;
	for (auto* current = a_location; current && depth < kMaxOverrideHierarchyDepth; current = current->parentLoc, ++depth) {
		const auto resolvedIndex = FindLocationOverrideIndexByForm(current);
		if (resolvedIndex != kInvalidLocationOverrideIndex)
			return resolvedIndex;
	}

	return kInvalidLocationOverrideIndex;
}

void AdaptiveBrightness::NormalizeLocationOverrides()
{
	if (NormalizeLocationOverrideList(settings.locationOverrides))
		MarkLocationOverrideLookupDirty();
}

void AdaptiveBrightness::MarkLocationOverrideLookupDirty()
{
	locationOverrideLookupDirty = true;
	locationLayerCache = {};
}

void AdaptiveBrightness::EnsureLocationOverrideLookup() const
{
	if (!locationOverrideLookupDirty)
		return;

	locationOverrideLookup.clear();
	for (std::size_t i = 0; i < settings.locationOverrides.size(); ++i) {
		const auto& locationOverride = settings.locationOverrides[i];
		if (IsValidFormKey(locationOverride.key))
			locationOverrideLookup[locationOverride.key] = i;
	}

	locationLayerCache = {};
	locationOverrideLookupDirty = false;
	++locationOverrideLookupVersion;
}

float AdaptiveBrightness::GetExteriorNightFactor() const
{
	const auto* sky = globals::game::sky;
	const float hour = sky ? WrapHour(sky->currentGameHour) : 12.0f;
	const float dayStart = WrapHour(settings.dayStartHour);
	const float nightStart = WrapHour(settings.nightStartHour);
	float dayLength = HoursSince(dayStart, nightStart);

	if (dayLength < 0.25f || dayLength > 23.75f)
		dayLength = 12.0f;

	const float nightLength = 24.0f - dayLength;
	const float hoursIntoDay = HoursSince(dayStart, hour);
	float transition = std::clamp(SafeFinite(settings.transitionHours, 1.0f), 0.0f, 4.0f);
	transition = std::min(transition, std::min(dayLength, nightLength));

	if (transition <= 0.0f)
		return hoursIntoDay < dayLength ? 0.0f : 1.0f;

	if (hoursIntoDay < dayLength) {
		const float dayFactor = SmoothStep(0.0f, transition, hoursIntoDay);
		return 1.0f - std::clamp(dayFactor, 0.0f, 1.0f);
	}

	const float nightFactor = SmoothStep(dayLength, dayLength + transition, hoursIntoDay);
	return std::clamp(nightFactor, 0.0f, 1.0f);
}

LinearLighting::Settings AdaptiveBrightness::GetNeutralLinearLightingSettings() const
{
	auto neutral = LinearLighting::Settings{};

	neutral.lightGamma = 1.0f;
	neutral.colorGamma = 1.0f;
	neutral.emitColorGamma = 1.0f;
	neutral.glowmapGamma = 1.0f;
	neutral.ambientGamma = 1.0f;
	neutral.fogGamma = 1.0f;
	neutral.fogAlphaGamma = 1.0f;
	neutral.effectGamma = 1.0f;
	neutral.effectAlphaGamma = 1.0f;
	neutral.skyGamma = 1.0f;
	neutral.waterGamma = 1.0f;
	neutral.vlGamma = 1.0f;
	neutral.glowmapMult = 1.0f;
	neutral.effectLightingMult = 1.0f;

	return neutral;
}

LinearLighting::Settings AdaptiveBrightness::ApplyProfile(const LinearLighting::Settings& a_base, const ProfileSettings& a_profile) const
{
	auto out = a_base;
	const float brightness = ClampBrightness(a_profile.brightness);
	const float brightnessDelta = brightness - 1.0f;
	const float masterGammaOffset = std::clamp((1.0f - brightness) * 0.35f, -0.35f, 0.35f);

	const auto masterScale = [&](float a_weight) {
		return std::max(0.0f, 1.0f + brightnessDelta * a_weight);
	};
	const auto advancedMult = [&](float a_multiplier) {
		return a_profile.advanced ? ClampMultiplier(a_multiplier) : 1.0f;
	};
	const auto advancedOffset = [&](float a_offset) {
		return a_profile.advanced ? ClampGammaOffset(a_offset) : 0.0f;
	};

	out.ambientMult = ClampMultiplier(out.ambientMult * masterScale(0.95f) * advancedMult(a_profile.ambientMult));
	out.emitColorMult = ClampMultiplier(out.emitColorMult * masterScale(0.35f) * advancedMult(a_profile.emitColorMult));
	out.glowmapMult = ClampMultiplier(out.glowmapMult * masterScale(0.35f) * advancedMult(a_profile.glowmapMult));
	out.effectLightingMult = ClampMultiplier(out.effectLightingMult * masterScale(0.55f) * advancedMult(a_profile.effectLightingMult));

	out.skyGamma = ClampGamma(out.skyGamma + masterGammaOffset * 0.90f + advancedOffset(a_profile.skyGammaOffset));
	out.fogGamma = ClampGamma(out.fogGamma + masterGammaOffset * 0.75f + advancedOffset(a_profile.fogGammaOffset));
	out.fogAlphaGamma = ClampGamma(out.fogAlphaGamma + masterGammaOffset * 0.50f + advancedOffset(a_profile.fogAlphaGammaOffset));
	out.waterGamma = ClampGamma(out.waterGamma + masterGammaOffset * 0.75f + ClampGammaOffset(a_profile.waterGammaOffset));
	out.vlGamma = ClampGamma(out.vlGamma + masterGammaOffset * 0.85f + advancedOffset(a_profile.vlGammaOffset));

	return out;
}

namespace
{
	bool HasAdaptiveBrightnessColorAdjustments(
		const LinearLighting::Settings& a_base,
		const LinearLighting::Settings& a_effective)
	{
		// Keep this list aligned with the fields changed by the Linear Lighting
		// ApplyProfile overload. Exact comparisons preserve the original shader
		// path for a fully neutral profile instead of evaluating identity curves.
		return a_effective.ambientMult != a_base.ambientMult ||
		       a_effective.emitColorMult != a_base.emitColorMult ||
		       a_effective.glowmapMult != a_base.glowmapMult ||
		       a_effective.effectLightingMult != a_base.effectLightingMult ||
		       a_effective.skyGamma != a_base.skyGamma ||
		       a_effective.fogGamma != a_base.fogGamma ||
		       a_effective.fogAlphaGamma != a_base.fogAlphaGamma ||
		       a_effective.waterGamma != a_base.waterGamma ||
		       a_effective.vlGamma != a_base.vlGamma;
	}
}

SharedLightingSettings AdaptiveBrightness::ApplyProfile(const SharedLightingSettings& a_base, const ProfileSettings& a_profile) const
{
	auto out = a_base;
	const float brightness = ClampBrightness(a_profile.brightness);
	const float brightnessDelta = brightness - 1.0f;

	const auto masterScale = [&](float a_weight) {
		return std::max(0.0f, 1.0f + brightnessDelta * a_weight);
	};
	const auto advancedMult = [&](float a_multiplier) {
		return a_profile.advanced ? ClampMultiplier(a_multiplier) : 1.0f;
	};

	out.skyBrightness = ClampMultiplier(out.skyBrightness * advancedMult(a_profile.skyBrightnessMult));
	out.directionalLightMult = ClampMultiplier(out.directionalLightMult * masterScale(0.70f) * advancedMult(a_profile.directionalLightMult));

	const float pointLightBrightness = masterScale(0.75f);
	out.pointLightMult = ClampMultiplier(
		out.pointLightMult * pointLightBrightness * advancedMult(a_profile.pointLightMult));
	out.linearPointLightMult = ClampMultiplier(
		out.linearPointLightMult * pointLightBrightness * advancedMult(a_profile.linearPointLightMult));
	out.spotlightMult = ClampMultiplier(out.spotlightMult * advancedMult(a_profile.spotlightMult));
	out.linearSpotlightMult = ClampMultiplier(out.linearSpotlightMult * advancedMult(a_profile.linearSpotlightMult));
	out.omnidirectionalBulbMult = ClampMultiplier(
		out.omnidirectionalBulbMult * advancedMult(a_profile.omnidirectionalBulbMult));
	out.linearOmnidirectionalBulbMult = ClampMultiplier(
		out.linearOmnidirectionalBulbMult * advancedMult(a_profile.linearOmnidirectionalBulbMult));

	return out;
}

Bloom::Profile AdaptiveBrightness::ApplyProfile(
	const Bloom::Profile& a_base,
	const ProfileSettings& a_profile) const
{
	auto base = a_base;
	auto layer = a_profile.bloom;
	Bloom::SanitizeProfile(base);
	Bloom::SanitizeProfile(layer);
	const Bloom::Profile identity{};

	Bloom::Profile out{
		base.EnhancementIntensity + layer.EnhancementIntensity,
		ApplyRelativeValue(base.HaloRadius, layer.HaloRadius, identity.HaloRadius),
		ApplyRelativeValue(base.HaloSpread, layer.HaloSpread, identity.HaloSpread),
		ApplyRelativeValue(base.BloomSaturation, layer.BloomSaturation, identity.BloomSaturation),
		{ ApplyRelativeValue(base.BloomTint.x, layer.BloomTint.x, identity.BloomTint.x),
			ApplyRelativeValue(base.BloomTint.y, layer.BloomTint.y, identity.BloomTint.y),
			ApplyRelativeValue(base.BloomTint.z, layer.BloomTint.z, identity.BloomTint.z) },
		ApplyRelativeValue(base.CompressionThreshold, layer.CompressionThreshold, identity.CompressionThreshold),
		ApplyRelativeValue(base.CompressionCeiling, layer.CompressionCeiling, identity.CompressionCeiling)
	};
	Bloom::SanitizeProfile(out);
	return out;
}

WaterAppearance::Profile AdaptiveBrightness::ApplyProfile(
	const WaterAppearance::Profile& a_base,
	const ProfileSettings& a_profile) const
{
	auto base = a_base;
	auto layer = a_profile.water;
	WaterAppearance::SanitizeProfile(base);
	WaterAppearance::SanitizeProfile(layer);
	const WaterAppearance::Profile identity{};

	WaterAppearance::Profile out{
		ApplyRelativeValue(base.WaterBrightness, layer.WaterBrightness, identity.WaterBrightness),
		ApplyRelativeValue(base.GlobalReflectionAmount, layer.GlobalReflectionAmount, identity.GlobalReflectionAmount),
		ApplyRelativeValue(base.RefractionAmount, layer.RefractionAmount, identity.RefractionAmount),
		ApplyRelativeValue(base.SunSpecularMultiplier, layer.SunSpecularMultiplier, identity.SunSpecularMultiplier),
		ApplyRelativeValue(base.WaveAmplitude, layer.WaveAmplitude, identity.WaveAmplitude),
		ApplyRelativeValue(base.FresnelMin, layer.FresnelMin, identity.FresnelMin),
		ApplyRelativeValue(base.FresnelMax, layer.FresnelMax, identity.FresnelMax),
		ApplyRelativeValue(base.Muddiness, layer.Muddiness, identity.Muddiness)
	};
	WaterAppearance::SanitizeProfile(out);
	return out;
}

AdaptiveBrightness::WaterWindSettings AdaptiveBrightness::ApplyProfile(
	const WaterWindSettings& a_base,
	const ProfileSettings& a_profile) const
{
	auto out = a_base;
	auto layer = a_profile.waterWind;
	SanitizeWaterWindSettings(out);
	SanitizeWaterWindSettings(layer);
	if (layer.overrideEnabled)
		out.enabled = layer.enabled;
	out.overrideEnabled = true;
	out.calmWaveMultiplier *= layer.calmWaveMultiplier;
	out.strongWindWaveMultiplier *= layer.strongWindWaveMultiplier;
	SanitizeWaterWindSettings(out);
	return out;
}

LinearLighting::Settings AdaptiveBrightness::LerpSettings(const LinearLighting::Settings& a_a, const LinearLighting::Settings& a_b, float a_t) const
{
	auto out = a_a;
	const float t = std::clamp(SafeFinite(a_t, 0.0f), 0.0f, 1.0f);
	const auto lerp = [&](float a_start, float a_end) {
		return std::lerp(a_start, a_end, t);
	};

	out.lightGamma = lerp(a_a.lightGamma, a_b.lightGamma);
	out.colorGamma = lerp(a_a.colorGamma, a_b.colorGamma);
	out.emitColorGamma = lerp(a_a.emitColorGamma, a_b.emitColorGamma);
	out.glowmapGamma = lerp(a_a.glowmapGamma, a_b.glowmapGamma);
	out.ambientGamma = lerp(a_a.ambientGamma, a_b.ambientGamma);
	out.fogGamma = lerp(a_a.fogGamma, a_b.fogGamma);
	out.fogAlphaGamma = lerp(a_a.fogAlphaGamma, a_b.fogAlphaGamma);
	out.effectGamma = lerp(a_a.effectGamma, a_b.effectGamma);
	out.effectAlphaGamma = lerp(a_a.effectAlphaGamma, a_b.effectAlphaGamma);
	out.skyGamma = lerp(a_a.skyGamma, a_b.skyGamma);
	out.waterGamma = lerp(a_a.waterGamma, a_b.waterGamma);
	out.vlGamma = lerp(a_a.vlGamma, a_b.vlGamma);
	out.vanillaDiffuseColorMult = lerp(a_a.vanillaDiffuseColorMult, a_b.vanillaDiffuseColorMult);
	out.ambientMult = lerp(a_a.ambientMult, a_b.ambientMult);
	out.emitColorMult = lerp(a_a.emitColorMult, a_b.emitColorMult);
	out.glowmapMult = lerp(a_a.glowmapMult, a_b.glowmapMult);
	out.effectLightingMult = lerp(a_a.effectLightingMult, a_b.effectLightingMult);
	out.membraneEffectMult = lerp(a_a.membraneEffectMult, a_b.membraneEffectMult);
	out.bloodEffectMult = lerp(a_a.bloodEffectMult, a_b.bloodEffectMult);
	out.projectedEffectMult = lerp(a_a.projectedEffectMult, a_b.projectedEffectMult);
	out.deferredEffectMult = lerp(a_a.deferredEffectMult, a_b.deferredEffectMult);
	out.otherEffectMult = lerp(a_a.otherEffectMult, a_b.otherEffectMult);

	return out;
}

SharedLightingSettings AdaptiveBrightness::LerpSettings(const SharedLightingSettings& a_a, const SharedLightingSettings& a_b, float a_t) const
{
	auto out = a_a;
	const float t = std::clamp(SafeFinite(a_t, 0.0f), 0.0f, 1.0f);
	const auto lerp = [&](float a_start, float a_end) {
		return std::lerp(a_start, a_end, t);
	};

	out.skyBrightness = lerp(a_a.skyBrightness, a_b.skyBrightness);
	out.directionalLightMult = lerp(a_a.directionalLightMult, a_b.directionalLightMult);
	out.pointLightMult = lerp(a_a.pointLightMult, a_b.pointLightMult);
	out.linearPointLightMult = lerp(a_a.linearPointLightMult, a_b.linearPointLightMult);
	out.spotlightMult = lerp(a_a.spotlightMult, a_b.spotlightMult);
	out.linearSpotlightMult = lerp(a_a.linearSpotlightMult, a_b.linearSpotlightMult);
	out.omnidirectionalBulbMult = lerp(a_a.omnidirectionalBulbMult, a_b.omnidirectionalBulbMult);
	out.linearOmnidirectionalBulbMult = lerp(a_a.linearOmnidirectionalBulbMult, a_b.linearOmnidirectionalBulbMult);

	return out;
}

AdaptiveBrightness::EffectiveLinearLightingSettings AdaptiveBrightness::GetEffectiveLinearLightingSettings(
	const LinearLighting::Settings& a_linearLightingSettings,
	bool a_linearLightingEnabled) const
{
	const auto baseSettings = a_linearLightingEnabled ? a_linearLightingSettings : GetNeutralLinearLightingSettings();
	const bool runtimeAvailable = IsAdjustmentRuntimeActive();
	auto layeredBase = baseSettings;
	if (runtimeAvailable)
		layeredBase = ApplyProfile(layeredBase, settings.globalProfile);
	auto effectiveSettings = layeredBase;

	if (runtimeAvailable && settings.enabled) {
		const auto activeProfiles = GetActiveProfileBlend();
		const auto& locationLayers = GetActiveLocationLayers();
		const auto branches = ComposeProfileBranches(
			layeredBase,
			activeProfiles,
			locationLayers,
			[this](const auto& a_base, const auto& a_layer) { return ApplyProfile(a_base, a_layer); });
		effectiveSettings = branches.to ?
		                        LerpSettings(branches.from, *branches.to, branches.factor) :
		                        branches.from;
	}

	return {
		.settings = effectiveSettings,
		.hasColorAdjustments = HasAdaptiveBrightnessColorAdjustments(baseSettings, effectiveSettings)
	};
}

SharedLightingSettings AdaptiveBrightness::GetEffectiveSharedLightingSettings() const
{
	SharedLightingSettings neutralSettings{};
	SanitizeSharedLightingSettings(neutralSettings);
	const bool runtimeAvailable = IsAdjustmentRuntimeActive();
	auto layeredBase = neutralSettings;
	if (runtimeAvailable)
		layeredBase = ApplyProfile(layeredBase, settings.globalProfile);
	auto effectiveSettings = layeredBase;

	if (runtimeAvailable && settings.enabled) {
		const auto activeProfiles = GetActiveProfileBlend();
		const auto& locationLayers = GetActiveLocationLayers();
		const auto branches = ComposeProfileBranches(
			layeredBase,
			activeProfiles,
			locationLayers,
			[this](const auto& a_base, const auto& a_layer) { return ApplyProfile(a_base, a_layer); });
		effectiveSettings = branches.to ?
		                        LerpSettings(branches.from, *branches.to, branches.factor) :
		                        branches.from;
	}

	return effectiveSettings;
}

Bloom::Settings AdaptiveBrightness::GetEffectiveBloomSettings() const
{
	if (!IsAdjustmentRuntimeActive())
		return Bloom::GetCommonBufferData(Bloom::Profile{}, 0.0f);

	const auto& globalBloom = settings.globalProfile.bloom;
	if (!settings.enabled)
		return Bloom::GetCommonBufferData(globalBloom, 1.0f);

	const auto activeProfiles = GetActiveProfileBlend();
	const auto& locationLayers = GetActiveLocationLayers();
	const auto branches = ComposeProfileBranches(
		globalBloom,
		activeProfiles,
		locationLayers,
		[this](const auto& a_base, const auto& a_layer) { return ApplyProfile(a_base, a_layer); });
	const auto effectiveProfile = branches.to ?
	                                  Bloom::LerpProfiles(branches.from, *branches.to, branches.factor) :
	                                  branches.from;
	return Bloom::GetCommonBufferData(effectiveProfile, 1.0f);
}

WaterAppearance::Settings AdaptiveBrightness::GetEffectiveWaterAppearanceSettings() const
{
	if (!IsAdjustmentRuntimeActive())
		return WaterAppearance::GetCommonBufferData(WaterAppearance::Profile{});

	const auto& globalProfile = settings.globalProfile;
	const auto applyWind = [&](WaterAppearance::Profile water, const WaterWindSettings& waterWind) {
		if (waterWind.enabled) {
			water.WaveAmplitude *= GetWaterWindWaveMultiplier(waterWind, GetSmoothedWaterWindSpeed());
			WaterAppearance::SanitizeProfile(water);
		}
		return water;
	};
	if (!settings.enabled)
		return WaterAppearance::GetCommonBufferData(applyWind(globalProfile.water, globalProfile.waterWind));

	struct WaterLayerState
	{
		WaterAppearance::Profile appearance;
		WaterWindSettings wind;
	};
	const WaterLayerState globalWater{
		.appearance = globalProfile.water,
		.wind = globalProfile.waterWind,
	};
	const auto activeProfiles = GetActiveProfileBlend();
	const auto& locationLayers = GetActiveLocationLayers();
	const auto branches = ComposeProfileBranches(
		globalWater,
		activeProfiles,
		locationLayers,
		[this](const WaterLayerState& a_base, const ProfileSettings& a_layer) {
			return WaterLayerState{
				.appearance = ApplyProfile(a_base.appearance, a_layer),
				.wind = ApplyProfile(a_base.wind, a_layer),
			};
		});
	auto effectiveProfile = applyWind(branches.from.appearance, branches.from.wind);
	if (branches.to) {
		const auto toProfile = applyWind(branches.to->appearance, branches.to->wind);
		effectiveProfile = WaterAppearance::LerpProfiles(effectiveProfile, toProfile, branches.factor);
	}

	return WaterAppearance::GetCommonBufferData(effectiveProfile);
}

float AdaptiveBrightness::GetSmoothedWaterWindSpeed() const
{
	if (LocationContext::Get().inInterior) {
		ResetWaterWindSmoothing();
		return 0.0f;
	}

	float targetWindSpeed = 0.0f;
	auto* sky = globals::game::sky ? globals::game::sky : RE::Sky::GetSingleton();
	if (sky) {
		if (std::isfinite(sky->windSpeed)) {
			targetWindSpeed = std::clamp(sky->windSpeed, 0.0f, 1.0f);
		} else if (sky->currentWeather) {
			targetWindSpeed = Util::Units::WindRawToNormalized(sky->currentWeather->data.windSpeed);
		}
	}

	auto* state = globals::state;
	if (!state) {
		smoothedWaterWindSpeed = targetWindSpeed;
		waterWindSmoothingInitialized = true;
		return targetWindSpeed;
	}

	const uint32_t currentFrame = state->frameCountAtomic.load(std::memory_order_relaxed);
	if (!waterWindSmoothingInitialized) {
		smoothedWaterWindSpeed = targetWindSpeed;
		smoothedWaterWindFrame = currentFrame;
		waterWindSmoothingInitialized = true;
	} else if (currentFrame != smoothedWaterWindFrame) {
		// Feature data can be assembled more than once per render frame.
		const float frameTime = std::clamp(
			SafeFinite(RE::GetSecondsSinceLastFrame(), 0.0f),
			0.0f,
			kMaxWaterWindSmoothingFrameTime);
		const float blend = 1.0f - std::exp(-frameTime / kWaterWindSmoothingSeconds);
		smoothedWaterWindSpeed = std::lerp(smoothedWaterWindSpeed, targetWindSpeed, blend);
		smoothedWaterWindFrame = currentFrame;
	}

	return std::clamp(SafeFinite(smoothedWaterWindSpeed, targetWindSpeed), 0.0f, 1.0f);
}

void AdaptiveBrightness::ResetWaterWindSmoothing() const
{
	smoothedWaterWindSpeed = 0.0f;
	smoothedWaterWindFrame = 0;
	waterWindSmoothingInitialized = false;
}

AdaptiveBrightness::PerFrameData AdaptiveBrightness::GetCommonBufferData() const
{
	const auto effectiveSettings = GetEffectiveSharedLightingSettings();

	PerFrameData data{};
	data.skyBrightness = effectiveSettings.skyBrightness;
	data.directionalLightMult = effectiveSettings.directionalLightMult;
	data.pointLightMult = effectiveSettings.pointLightMult;
	data.linearPointLightMult = effectiveSettings.linearPointLightMult;
	data.spotlightMult = effectiveSettings.spotlightMult;
	data.linearSpotlightMult = effectiveSettings.linearSpotlightMult;
	data.omnidirectionalBulbMult = effectiveSettings.omnidirectionalBulbMult;
	data.linearOmnidirectionalBulbMult = effectiveSettings.linearOmnidirectionalBulbMult;
	return data;
}

bool AdaptiveBrightness::NeedsVanillaPointLightData() const
{
	if (!loaded)
		return false;

	if (globals::features::linearLighting.IsRuntimeEnabled())
		return true;

	return UsesClassifiedPointLightMultipliers(GetEffectiveSharedLightingSettings());
}

void AdaptiveBrightness::UpdateVanillaPointLightData(
	RE::BSRenderPass* a_pass,
	uint32_t a_lightCount,
	uint32_t a_bufferRegister)
{
	if (!vanillaPointLightCB || !globals::d3d::context || !a_pass || !a_pass->sceneLights)
		return;

	VanillaPointLightData data{};
	const uint32_t lightCount = std::min(a_lightCount, kMaxVanillaPointLights);
	for (uint32_t lightIndex = 0; lightIndex < lightCount; ++lightIndex) {
		const uint32_t sceneLightIndex = lightIndex + kFirstPointLightSceneIndex;
		if (sceneLightIndex >= a_pass->numLights)
			break;

		auto* bsLight = a_pass->sceneLights[sceneLightIndex];
		if (!bsLight)
			continue;

		auto* niLight = bsLight->light.get();
		auto pointLightFlags = PointLightFlags::GetVanillaPointLightFlags(bsLight, niLight);
		if (!globals::features::inverseSquareLighting.IsEnabled())
			pointLightFlags &= ~PointLightFlags::ToMask(PointLightFlags::Flags::Linear);
		data.pointLightFlags[lightIndex] = pointLightFlags;
	}

	vanillaPointLightCB->Update(data);

	ID3D11Buffer* buffer = vanillaPointLightCB->CB();
	globals::d3d::context->PSSetConstantBuffers(a_bufferRegister, 1, &buffer);
}

AdaptiveBrightness::ActiveProfileBlend AdaptiveBrightness::GetActiveProfileBlend() const
{
	const auto location = LocationContext::Get();

	if (location.inInterior) {
		const auto profile = GetInteriorProfile();
		const auto* profileSettings = &settings.profiles[ProfileIndex(profile)];
		return { .from = profileSettings, .to = profileSettings, .factor = 0.0f };
	}

	return {
		.from = &settings.profiles[ProfileIndex(Profile::ExteriorDay)],
		.to = &settings.profiles[ProfileIndex(Profile::ExteriorNight)],
		.factor = GetExteriorNightFactor()
	};
}

std::string AdaptiveBrightness::GetContextLabel() const
{
	constexpr auto displayName = kFeatureDisplayName;

	if (!settings.enabled)
		return std::format("{} is disabled.", displayName);

	if (!IsRuntimeEnabled())
		return std::format("{} is inactive in the current menu or while the feature is unloaded.", displayName);

	if (const auto* locationOverride = GetActiveLocationOverride()) {
		return std::format("Current override: {} ({})", locationOverride->name, locationOverride->type);
	}

	const auto location = LocationContext::Get();
	if (location.inInterior) {
		return std::format("Current profile: {}", GetProfileName(GetCurrentProfileForUI()));
	}

	const float nightFactor = GetExteriorNightFactor();
	const auto dominantProfile = GetCurrentProfileForUI();
	return std::format("Current profile: {} ({:.0f}% night blend)", GetProfileName(dominantProfile), nightFactor * 100.0f);
}

struct AdaptiveBrightness::Hooks
{
	struct BSWaterShader_SetupGeometry
	{
		static void thunk(RE::BSShader* a_shader, RE::BSRenderPass* a_pass, uint32_t a_renderFlags)
		{
			func(a_shader, a_pass, a_renderFlags);

			auto& adaptiveBalance = globals::features::adaptiveBrightness;
			if (!adaptiveBalance.NeedsVanillaPointLightData())
				return;

			const uint32_t lightCount =
				a_pass && a_pass->numLights > 0 ?
					a_pass->numLights - kFirstPointLightSceneIndex :
					0;
			adaptiveBalance.UpdateVanillaPointLightData(
				a_pass,
				lightCount,
				kWaterPointLightCBRegister);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void Install()
	{
		stl::write_vfunc<0x6, BSWaterShader_SetupGeometry>(RE::VTABLE_BSWaterShader[0]);
		logger::info("[AdaptiveBalance] Installed shared-light water hook");
	}
};

void AdaptiveBrightness::PostPostLoad()
{
	Hooks::Install();
}
