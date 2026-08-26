#include "AdaptiveBrightness.h"

#include "Globals.h"
#include "InverseSquareLighting.h"
#include "LocationContext.h"
#include "SettingsMigrations.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/FileSystem.h"
#include "Utils/Form.h"
#include "Utils/PointLightFlags.h"
#include "Utils/UI.h"

#include "RE/B/BGSLocation.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/S/Sky.h"
#include "RE/T/TESObjectCELL.h"
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
	AdaptiveBrightness::ProfileSettings,
	brightness,
	advanced,
	bloomAdvanced,
	waterAdvanced,
	skyBrightnessMult,
	directionalLightMult,
	pointLightMult,
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
	water)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	AdaptiveBrightness::LocationOverride,
	key,
	name,
	type,
	cocCode,
	profile)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	AdaptiveBrightness::Settings,
	enabled,
	globalLightingEnabled,
	dayStartHour,
	nightStartHour,
	transitionHours,
	lighting,
	profiles,
	locationOverrides)

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
	constexpr const char* kOverrideTypeCell = "Cell";
	constexpr const char* kOverrideTypeWorldspace = "Worldspace";
	constexpr const char* kPresetVersion = "4.1.0";
	constexpr std::string_view kLocationOverridesFieldName = "locationOverrides";
	constexpr std::string_view kProfilesFieldName = "profiles";
	constexpr std::string_view kLegacyGlobalWaterAppearanceFieldName = SettingsMigrations::kLegacyWaterAppearanceSettingsKey;
	constexpr std::string_view kGlobalPresetFilenameSuffix = "_AdaptiveBrightness_Global";
	constexpr std::string_view kLocationPresetFilenameSuffix = "_AdaptiveBrightness_LocationOverrides";
	constexpr std::string_view kFullPresetFilenameSuffix = "_AdaptiveBrightness_Full";

	bool IsWorldspaceOverride(const AdaptiveBrightness::LocationOverride* a_locationOverride)
	{
		return a_locationOverride && a_locationOverride->type == kOverrideTypeWorldspace;
	}

	enum class PresetKind
	{
		Global,
		Location,
		Full
	};

	struct CurrentLocationForms
	{
		const RE::TESWorldSpace* worldspace = nullptr;
		const RE::BGSLocation* location = nullptr;
		const RE::TESObjectCELL* cell = nullptr;
	};

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
		default:
			return AdaptiveBrightness::kDefaultLocationOverridePresetName;
		}
	}

	std::string GetPresetModName(std::string_view a_name, PresetKind a_kind = PresetKind::Location)
	{
		auto baseName = TrimCopy(a_name);
		if (EndsWithCaseInsensitive(baseName, ".json"))
			baseName.resize(baseName.size() - std::string_view(".json").size());

		for (const auto suffix : { kGlobalPresetFilenameSuffix, kLocationPresetFilenameSuffix, kFullPresetFilenameSuffix }) {
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

	void ClampProfileSettings(AdaptiveBrightness::ProfileSettings& a_profile)
	{
		a_profile.brightness = ClampBrightness(a_profile.brightness);
		a_profile.skyBrightnessMult = ClampMultiplier(a_profile.skyBrightnessMult);
		a_profile.directionalLightMult = ClampMultiplier(a_profile.directionalLightMult);
		a_profile.pointLightMult = ClampMultiplier(a_profile.pointLightMult);
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
		if (!migrated.contains("globalLightingEnabled")) {
			if (const auto rendererIt = migrated.find("rendererControlsEnabled"); rendererIt != migrated.end() && rendererIt->is_boolean())
				migrated["globalLightingEnabled"] = rendererIt->get<bool>();
		}

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
					MigrateLegacyProfileBloom(*profileIt, globalBloomProfile, globalBloomEnabled, hasLegacyGlobalBloom);
					MigrateLegacyProfileWaterAppearance(*profileIt, globalWaterProfile, hasLegacyGlobalWater, forceLegacyGlobalWater);
				}
			}
		}

		migrated.erase("rendererControlsEnabled");
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
			{ "globalLightingEnabled", a_settings.globalLightingEnabled },
			{ "lighting", a_settings.lighting },
			{ "dayStartHour", a_settings.dayStartHour },
			{ "nightStartHour", a_settings.nightStartHour },
			{ "transitionHours", a_settings.transitionHours },
			{ "profiles", a_settings.profiles },
			{ "_metadata", MakePresetMetadata(a_name, a_kind, a_type, a_description) }
		};
	}

	void NormalizeImportedLocationOverride(AdaptiveBrightness::LocationOverride& a_locationOverride)
	{
		if (a_locationOverride.name.empty())
			a_locationOverride.name = a_locationOverride.key;

		if (a_locationOverride.type != kOverrideTypeCell &&
			a_locationOverride.type != kOverrideTypeWorldspace)
			a_locationOverride.type = kOverrideTypeLocation;

		ClampProfileSettings(a_locationOverride.profile);
	}

	LocationOverrideImportStats ParseLocationOverridesJson(const json& a_json, std::vector<AdaptiveBrightness::LocationOverride>& o_locationOverrides)
	{
		LocationOverrideImportStats stats;
		for (const auto& entry : a_json) {
			try {
				auto migratedEntry = entry;
				if (auto profileIt = migratedEntry.find("profile"); profileIt != migratedEntry.end()) {
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

			NormalizeImportedLocationOverride(*it);
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
			NormalizeBaseProfiles(importedProfiles);

			if (const auto it = migratedPreset.find("globalLightingEnabled"); it != migratedPreset.end() && it->is_boolean())
				importedSettings.globalLightingEnabled = it->get<bool>();
			if (const auto it = migratedPreset.find("lighting"); it != migratedPreset.end() && it->is_object()) {
				importedSettings.lighting = it->get<SharedLightingSettings>();
				SanitizeSharedLightingSettings(importedSettings.lighting);
			}

			importedSettings.dayStartHour = GetOptionalFloat(migratedPreset, "dayStartHour", importedSettings.dayStartHour);
			importedSettings.nightStartHour = GetOptionalFloat(migratedPreset, "nightStartHour", importedSettings.nightStartHour);
			importedSettings.transitionHours = GetOptionalFloat(migratedPreset, "transitionHours", importedSettings.transitionHours);
			NormalizeExteriorTimeSettings(importedSettings);
			importedSettings.profiles = std::move(importedProfiles);
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

	void DrawPresetNameInput(const char* a_label, const char* a_id, std::string& a_name, const std::filesystem::path& a_exportPath, const std::filesystem::path* a_alternateImportPath = nullptr)
	{
		ImGui::TextUnformatted(a_label);
		ImGui::SameLine();
		const float inputWidth = std::min(280.0f * Util::GetUIScale(), std::max(120.0f, ImGui::GetContentRegionAvail().x * 0.45f));
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

	CurrentLocationForms GetCurrentLocationForms()
	{
		const auto* player = RE::PlayerCharacter::GetSingleton();
		const auto* cell = GetCurrentPlayerCell(player);
		if (!cell)
			return {};

		// Worldspace lookup may consult incomplete save-parent state during cell
		// transitions, so derive it from the validated parent cell instead.
		const auto* worldspace = cell->IsExteriorCell() ? cell->GetRuntimeData().worldSpace : nullptr;
		auto* location = player->GetCurrentLocation();
		if (!location)
			location = cell->GetLocation();

		return {
			.worldspace = worldspace,
			.location = location,
			.cell = cell,
		};
	}

	bool LocationHasAnyKeyword(const RE::BGSLocation* a_location, std::initializer_list<std::string_view> a_keywords)
	{
		for (auto* current = a_location; current; current = current->parentLoc) {
			for (auto keyword : a_keywords) {
				if (current->HasKeywordString(keyword))
					return true;
			}
		}

		return false;
	}
}

void AdaptiveBrightness::DrawSettingsHeaderControls()
{
	ImGui::Checkbox("Enable Adaptive Profiles", &settings.enabled);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Blend the active lighting, atmosphere, Bloom, and water appearance profile by location and exterior time.");
		ImGui::Text("Each profile defines its own scene brightness, Bloom, and water appearance.");
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
		if (ImGui::BeginTabItem("Profiles", nullptr, profileSectionFlags)) {
			ImGui::TextWrapped("Tune the lighting, atmosphere, Bloom, and water appearance used for each time and location type. The current worldspace profile appears alongside the base profiles; all saved scopes can also be managed under Locations.");
			if (!settings.enabled)
				ImGui::TextDisabled("Adaptive profile switching is off. Saved profile values can still be reviewed.");

			ImGui::BeginDisabled(!settings.enabled);
			DrawExteriorTimeSettings();
			ImGui::EndDisabled();

			const auto profileTabToSelect = SyncSelectedProfileTabToContext(ProfileTabSurface::Advanced);
			const bool selectActiveWorldspaceTab = profileTabToSelect && IsWorldspaceOverride(GetActiveLocationOverride());
			if (ImGui::BeginTabBar("##AdaptiveBrightnessProfiles", ImGuiTabBarFlags_None)) {
				for (auto profile : kProfileOrder) {
					const ImGuiTabItemFlags tabFlags =
						!selectActiveWorldspaceTab && profileTabToSelect && *profileTabToSelect == profile ?
							ImGuiTabItemFlags_SetSelected :
							ImGuiTabItemFlags_None;
					if (ImGui::BeginTabItem(GetProfileName(profile), nullptr, tabFlags)) {
						DrawProfile(profile, settings.enabled);
						ImGui::EndTabItem();
					}

					if (profile == Profile::Interior)
						DrawCurrentWorldspaceProfileTab(true, settings.enabled, selectActiveWorldspaceTab);
				}
				ImGui::EndTabBar();
			}
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Global")) {
			ImGui::TextWrapped("Set the shared lighting baseline applied before each profile.");
			DrawGlobalRendererSettings();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Locations", nullptr, locationSectionFlags)) {
			ImGui::TextWrapped("Create precise profile overrides for worldspaces, locations, or exact cells.");
			if (!settings.enabled)
				ImGui::TextDisabled("Adaptive profile switching is off. Saved overrides can still be reviewed.");
			DrawLocationOverrides(false, true, settings.enabled);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Presets")) {
			ImGui::TextWrapped("Import or export complete balance configurations and location override collections.");
			DrawGlobalPresetControls();
			DrawLocationOverridePresetControls();
			DrawFullPresetControls();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
}

void AdaptiveBrightness::DrawEssentialSettings()
{
	ImGui::SeparatorText("Quick Controls");
	ImGui::Checkbox("Enable Global Lighting Calibration", &settings.globalLightingEnabled);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Applies the shared lighting baseline before the active profile.");
		ImGui::Text("Scene brightness, Bloom, and water appearance profiles remain active when this is off.");
	}

	if (!settings.enabled)
		ImGui::TextDisabled("Adaptive profile switching is off. Saved profile values can still be reviewed.");

	ImGui::BeginDisabled(!settings.enabled);
	DrawExteriorTimeSettings();
	ImGui::EndDisabled();

	const auto profileTabToSelect = SyncSelectedProfileTabToContext(ProfileTabSurface::Essentials);
	const bool selectActiveWorldspaceTab = profileTabToSelect && IsWorldspaceOverride(GetActiveLocationOverride());
	if (ImGui::BeginTabBar("##AdaptiveBrightnessProfilesEssentials", ImGuiTabBarFlags_None)) {
		for (auto profile : kProfileOrder) {
			const ImGuiTabItemFlags tabFlags =
				!selectActiveWorldspaceTab && profileTabToSelect && *profileTabToSelect == profile ?
					ImGuiTabItemFlags_SetSelected :
					ImGuiTabItemFlags_None;
			if (ImGui::BeginTabItem(GetProfileName(profile), nullptr, tabFlags)) {
				auto& profileSettings = settings.profiles[ProfileIndex(profile)];
				ImGui::PushID(static_cast<int>(ProfileIndex(profile)));
				DrawProfileSettings(profileSettings, "Profile Values", false, settings.enabled);
				ImGui::PopID();
				ImGui::EndTabItem();
			}

			if (profile == Profile::Interior)
				DrawCurrentWorldspaceProfileTab(false, settings.enabled, selectActiveWorldspaceTab);
		}
		ImGui::EndTabBar();
	}

	DrawLocationSummary();
}

void AdaptiveBrightness::LoadSettings(json& o_json)
{
	settings = MigrateLegacyProfileSettings(o_json);
	SanitizeSharedLightingSettings(settings.lighting);
	NormalizeExteriorTimeSettings(settings);
	NormalizeBaseProfiles(settings.profiles);
	globalPresetStatus.clear();
	if (globalPresetName.empty())
		globalPresetName = kDefaultGlobalPresetName;
	locationOverridePresetStatus.clear();
	if (locationOverridePresetName.empty())
		locationOverridePresetName = kDefaultLocationOverridePresetName;
	fullPresetStatus.clear();
	if (fullPresetName.empty())
		fullPresetName = kDefaultFullPresetName;
	ClearLocationOverrideSelection();
	InvalidateProfileTabSync();
	NormalizeLocationOverrides();
	MarkLocationOverrideLookupDirty();
}

void AdaptiveBrightness::SaveSettings(json& o_json)
{
	SanitizeSharedLightingSettings(settings.lighting);
	NormalizeExteriorTimeSettings(settings);
	NormalizeBaseProfiles(settings.profiles);
	NormalizeLocationOverrides();
	o_json = settings;
}

void AdaptiveBrightness::RestoreDefaultSettings()
{
	settings = {};
	NormalizeExteriorTimeSettings(settings);
	globalPresetName = kDefaultGlobalPresetName;
	globalPresetStatus.clear();
	locationOverridePresetName = kDefaultLocationOverridePresetName;
	locationOverridePresetStatus.clear();
	fullPresetName = kDefaultFullPresetName;
	fullPresetStatus.clear();
	ClearLocationOverrideSelection();
	InvalidateProfileTabSync();
	MarkLocationOverrideLookupDirty();
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

std::optional<AdaptiveBrightness::Profile> AdaptiveBrightness::SyncSelectedProfileTabToContext(ProfileTabSurface a_surface)
{
	if (!GetCurrentPlayerCell(RE::PlayerCharacter::GetSingleton())) {
		profileTabSyncStates[static_cast<std::size_t>(a_surface)] = {};
		return std::nullopt;
	}

	const auto currentProfile = GetCurrentProfileForUI();
	std::string currentProfileTabSyncKey = std::to_string(static_cast<uint32_t>(currentProfile));
	const auto* activeOverride = GetActiveLocationOverride();
	if (activeOverride) {
		currentProfileTabSyncKey += ':';
		currentProfileTabSyncKey += activeOverride->key;
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

	auto& syncState = profileTabSyncStates[static_cast<std::size_t>(a_surface)];
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
	const std::string currentKey = activeOverride ? std::format("override:{}", activeOverride->key) : "base";
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
		return IsWorldspaceOverride(activeOverride) ? ContextSection::Profiles : ContextSection::Locations;
	}

	return ContextSection::Profiles;
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

void AdaptiveBrightness::DrawCurrentWorldspaceProfileTab(bool a_showAdvancedControls, bool a_allowEdits, bool a_select)
{
	const auto targets = GetCurrentLocationOverrideTargets();
	if (!targets.worldspace)
		return;

	auto* worldspaceProfile = FindLocationOverride(targets.worldspace->key);
	if (!IsWorldspaceOverride(worldspaceProfile))
		worldspaceProfile = nullptr;
	const auto* inheritedWorldspaceProfile = worldspaceProfile ? nullptr : GetActiveWorldspaceOverride();

	const auto tabLabel = std::format("{}###AdaptiveBalanceWorldspaceProfile_{}", targets.worldspace->name, targets.worldspace->key);
	const ImGuiTabItemFlags tabFlags = a_select ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
	const bool drawTab = ImGui::BeginTabItem(tabLabel.c_str(), nullptr, tabFlags);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		if (worldspaceProfile) {
			ImGui::Text("Worldspace profile: %s", worldspaceProfile->name.c_str());
			ImGui::Text("Initialized from its inherited base profile when created, then edited independently.");
			ImGui::Text("Included in Full and Overrides exports.");
		} else if (inheritedWorldspaceProfile) {
			ImGui::Text("Currently inherits the parent worldspace profile %s.", inheritedWorldspaceProfile->name.c_str());
			ImGui::Text("Create a profile to tune %s independently.", targets.worldspace->name.c_str());
		} else {
			ImGui::Text("Create a profile for %s from its currently inherited exterior profile.", targets.worldspace->name.c_str());
		}
	}
	if (!drawTab)
		return;

	if (!worldspaceProfile) {
		if (inheritedWorldspaceProfile) {
			ImGui::TextWrapped("%s currently inherits the saved parent worldspace profile %s. Create a profile here to fork those values and tune Lighting, Bloom, and Water independently.", targets.worldspace->name.c_str(), inheritedWorldspaceProfile->name.c_str());
		} else {
			ImGui::TextWrapped("%s currently inherits the %s base profile. Create a profile here to fork those values and tune Lighting, Bloom, and Water independently.", targets.worldspace->name.c_str(), GetProfileName(targets.worldspace->defaultProfile));
		}
		ImGui::BeginDisabled(!a_allowEdits);
		if (ImGui::Button("Create Worldspace Profile"))
			SaveCurrentLocationOverride(*targets.worldspace);
		ImGui::EndDisabled();
		ImGui::EndTabItem();
		return;
	}

	ImGui::TextWrapped("Worldspace profile for %s.", worldspaceProfile->name.c_str());
	DrawLocationOverrideProfileEditor(
		*worldspaceProfile,
		"Profile Values",
		a_showAdvancedControls,
		a_allowEdits,
		"Save Worldspace Profile",
		false);
	ImGui::EndTabItem();
}

void AdaptiveBrightness::DrawProfileSettings(ProfileSettings& a_profile, const char* a_sectionTitle, bool a_showAdvancedControls, bool a_allowEdits)
{
	ClampProfileSettings(a_profile);

	const auto drawSlider = [](const char* a_label, float& a_value, float a_min, float a_max, const char* a_tooltip) {
		ImGui::SliderFloat(a_label, &a_value, a_min, a_max, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", a_tooltip);
		}
	};

	ImGui::SeparatorText(a_sectionTitle);
	ImGui::Indent();
	ImGui::BeginDisabled(!a_allowEdits);

	if (ImGui::BeginTabBar("##ProfileControlSections", ImGuiTabBarFlags_None)) {
		if (ImGui::BeginTabItem("Lighting")) {
			drawSlider("Scene Brightness", a_profile.brightness, kBrightnessMin, kBrightnessMax, "Overall brightness for this profile. Use it when this location type is too dark or too bright.");

			if (a_showAdvancedControls) {
				ImGui::Checkbox("Show Detailed Lighting Controls", &a_profile.advanced);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Shows and enables the detailed lighting and atmosphere values for this profile only.");
				}
				if (a_profile.advanced) {
					ImGui::Indent();
					ImGui::SeparatorText("Direct Lighting");
					ImGui::SliderFloat("Sky Brightness", &a_profile.skyBrightnessMult, 0.0f, 2.0f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::Text("Contextual multiplier applied to the global Sky Brightness value. This is separate from Sky Gamma.");
					ImGui::SliderFloat("Directional Light", &a_profile.directionalLightMult, 0.0f, 3.0f, "%.2f");
					ImGui::SliderFloat("Point Lights", &a_profile.pointLightMult, 0.0f, 3.0f, "%.2f");

					ImGui::SeparatorText("Indirect and Material Lighting");
					ImGui::SliderFloat("Ambient", &a_profile.ambientMult, 0.0f, 3.0f, "%.2f");
					ImGui::SliderFloat("Emissive", &a_profile.emitColorMult, 0.0f, 3.0f, "%.2f");
					ImGui::SliderFloat("Glowmaps", &a_profile.glowmapMult, 0.0f, 3.0f, "%.2f");
					ImGui::SliderFloat("Effects", &a_profile.effectLightingMult, 0.0f, 3.0f, "%.2f");

					ImGui::SeparatorText("Atmosphere Gamma Offsets");
					ImGui::SliderFloat("Sky", &a_profile.skyGammaOffset, kGammaOffsetMin, kGammaOffsetMax, "%.2f");
					ImGui::SliderFloat("Fog", &a_profile.fogGammaOffset, kGammaOffsetMin, kGammaOffsetMax, "%.2f");
					ImGui::SliderFloat("Fog Transparency", &a_profile.fogAlphaGammaOffset, kGammaOffsetMin, kGammaOffsetMax, "%.2f");
					ImGui::SliderFloat("Volumetric Lighting", &a_profile.vlGammaOffset, kGammaOffsetMin, kGammaOffsetMax, "%.2f");
					ImGui::Unindent();
				}
			}
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Bloom")) {
			Bloom::DrawProfileControls(a_profile.bloom);
			if (a_showAdvancedControls) {
				ImGui::Checkbox("Show Detailed Bloom Controls", &a_profile.bloomAdvanced);
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("Shows detailed Bloom shaping for this profile. Editing a detailed value activates Bloom at strength 1 if it is currently off; the Bloom slider and presets remain available either way.");
				if (a_profile.bloomAdvanced) {
					ImGui::Indent();
					if (Bloom::DrawAdvancedProfileSettings(a_profile.bloom) && a_profile.bloom.EnhancementIntensity <= 0.0f)
						a_profile.bloom.EnhancementIntensity = 1.0f;
					ImGui::Unindent();
				}
			}
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Water")) {
			WaterAppearance::DrawProfileControls(a_profile.water);
			if (a_showAdvancedControls) {
				ImGui::Checkbox("Show Detailed Water Controls", &a_profile.waterAdvanced);
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("Shows detailed water color, surface, reflection, refraction, and clarity controls for this profile.");
				if (a_profile.waterAdvanced) {
					ImGui::Indent();
					drawSlider(
						"Water Color Gamma",
						a_profile.waterGammaOffset,
						kGammaOffsetMin,
						kGammaOffsetMax,
						"Offsets water color gamma for this profile. This is separate from Water Brightness and the surface appearance controls below.");
					WaterAppearance::DrawAdvancedProfileSettings(a_profile.water);
					ImGui::Unindent();
				}
			}
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
	ImGui::EndDisabled();

	ImGui::Unindent();
	ClampProfileSettings(a_profile);
}

void AdaptiveBrightness::DrawGlobalRendererSettings()
{
	ImGui::Checkbox("Enable Global Lighting Calibration", &settings.globalLightingEnabled);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Applies the shared lighting baseline before each profile's detailed lighting adjustments.");
		ImGui::Text("Profile brightness, Bloom, and water appearance remain active when this is off.");
	}
	if (!settings.globalLightingEnabled)
		ImGui::TextDisabled("Global lighting calibration is currently inactive.");

	if (ImGui::BeginTabBar("##AdaptiveBalanceGlobalSections", ImGuiTabBarFlags_None)) {
		if (ImGui::BeginTabItem("Lighting")) {
			ImGui::BeginDisabled(!settings.globalLightingEnabled);
			ImGui::TextWrapped("These baseline values apply to every location. The active profile is multiplied on top.");
			ImGui::SeparatorText("Shared Baseline");
			ImGui::SliderFloat("Sky Brightness", &settings.lighting.skyBrightness, 0.0f, kGlobalSkyBrightnessMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Directional Light", &settings.lighting.directionalLightMult, 0.0f, kGlobalLightingMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Point Lights", &settings.lighting.pointLightMult, 0.0f, kGlobalLightingMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);

			ImGui::SeparatorText("Point-light Type Balance");
			ImGui::TextWrapped("Subtype values are additional multipliers applied after the active profile's Point Lights value.");
			ImGui::SliderFloat("Spotlights", &settings.lighting.spotlightMult, 0.0f, kGlobalLightingMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Omnidirectional Bulbs", &settings.lighting.omnidirectionalBulbMult, 0.0f, kGlobalLightingMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);

			ImGui::TextWrapped("Linear values target lights authored with linear falloff; they do not require Linear Lighting.");
			ImGui::SliderFloat("Linear Point Lights", &settings.lighting.linearPointLightMult, 0.0f, kGlobalLightingMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Linear Spotlights", &settings.lighting.linearSpotlightMult, 0.0f, kGlobalLightingMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Linear Omnidirectional Bulbs", &settings.lighting.linearOmnidirectionalBulbMult, 0.0f, kGlobalLightingMultiplierMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::EndDisabled();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	SanitizeSharedLightingSettings(settings.lighting);
}

void AdaptiveBrightness::DrawGlobalPresetControls()
{
	ImGui::SeparatorText("Global Presets");
	DrawHintText("Global presets store global light calibration, the five profiles (including Bloom and water appearance), and exterior timing.");
	DrawHintText("Import overwrites those profile tabs in the current settings. Saved location overrides are not changed.");
	ImGui::PushID("GlobalPresetControls");

	const auto presetPath = GetPresetPath(globalPresetName, PresetKind::Global);
	DrawPresetNameInput("Global preset", "##GlobalPresetName", globalPresetName, presetPath);

	ImGui::SameLine();
	if (ImGui::Button("Export Global")) {
		ExportGlobalPreset();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Export global lighting calibration, exterior timing, and the five profiles with their Bloom and water appearance settings. Location overrides are not included.");
	}

	ImGui::SameLine();
	if (ImGui::Button("Import Global")) {
		ImportGlobalPreset();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Replace global lighting calibration, exterior timing, and the five profiles with their Bloom and water appearance settings. Saved location overrides stay unchanged.");
	}

	if (!globalPresetStatus.empty())
		ImGui::TextWrapped("%s", globalPresetStatus.c_str());

	ImGui::PopID();
}

void AdaptiveBrightness::DrawLocationSummary()
{
	ImGui::SeparatorText("Location Overrides");
	const auto targets = GetCurrentLocationOverrideTargets();
	const auto* activeOverride = GetActiveLocationOverride();
	std::optional<Profile> currentProfile;
	if (GetCurrentPlayerCell(RE::PlayerCharacter::GetSingleton()))
		currentProfile = GetCurrentProfileForUI();

	if (activeOverride && currentProfile) {
		ImGui::TextWrapped(
			"Current location uses saved override \"%s\" (%s).",
			activeOverride->name.c_str(),
			activeOverride->type.c_str());
	} else if (currentProfile) {
		ImGui::TextWrapped("Current location uses the %s base profile.", GetProfileName(*currentProfile));
	} else {
		ImGui::TextDisabled("Current location context is unavailable while no cell is loaded.");
	}

	if (targets.worldspace)
		ImGui::TextDisabled("Worldspace target: %s", targets.worldspace->name.c_str());
	if (targets.location)
		ImGui::TextDisabled("Location target: %s", targets.location->name.c_str());
	if (targets.cell)
		ImGui::TextDisabled("Exact cell target: %s", targets.cell->name.c_str());
	if (!targets.worldspace && !targets.location && !targets.cell)
		ImGui::TextDisabled("No current worldspace, location, or cell form is available.");
	ImGui::TextDisabled("%zu saved override%s.", settings.locationOverrides.size(), settings.locationOverrides.size() == 1 ? "" : "s");
	ImGui::TextDisabled("Switch this feature to Advanced to manage location and exact-cell overrides.");
}

void AdaptiveBrightness::DrawLocationOverrides(bool a_includePresetControls, bool a_showAdvancedControls, bool a_allowEdits)
{
	ImGui::SeparatorText("Location Override Profiles");
	DrawHintText("Match priority is exact cell, current or parent location, then current or parent exterior worldspace. If none match, the interior, dungeon, dwelling, or exterior day/night base profile is used.");
	if (a_includePresetControls) {
		DrawHintText("Import adds overrides from a preset to the override list below. Later edits change this list, not the preset file.");
	}

	const auto targets = GetCurrentLocationOverrideTargets();
	const auto* activeOverride = GetActiveLocationOverride();
	std::optional<Profile> currentProfile;
	if (GetCurrentPlayerCell(RE::PlayerCharacter::GetSingleton()))
		currentProfile = GetCurrentProfileForUI();

	if (activeOverride && currentProfile) {
		ImGui::TextWrapped("Using saved override \"%s\" here. Base profile: %s.", activeOverride->name.c_str(), GetProfileName(*currentProfile));
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
		"Applies throughout this exterior worldspace and its child worldspaces unless a more specific worldspace, location, or exact-cell override exists.");
	drawTargetAction(
		targets.location,
		"Location",
		"Applies to this location and its descendants unless an exact-cell override exists; it takes precedence over worldspace overrides.");
	drawTargetAction(
		targets.cell,
		"Exact Cell",
		"Applies only to this exact cell and takes precedence over location and worldspace overrides.");

	if (!targets.worldspace && !targets.cell && !targets.location) {
		ImGui::TextDisabled("No current worldspace, location, or cell form is available.");
	}
	ImGui::TextDisabled("%zu saved override%s.", settings.locationOverrides.size(), settings.locationOverrides.size() == 1 ? "" : "s");

	if (a_includePresetControls)
		DrawLocationOverridePresetControls();
	ImGui::SeparatorText("Saved Overrides");
	DrawHintText(a_allowEdits ?
					 "These saved overrides are matched by worldspace, location, or cell. Click a row to edit it." :
					 "These saved overrides are matched by worldspace, location, or cell. Click a row to review it.");

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
	DrawHintText("Override presets store saved worldspace, location, and cell overrides only. They do not include the five profile tabs.");
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
	DrawHintText("Full presets store global calibration, Bloom and water appearance defaults, exterior timing, the five profile tabs, and all saved worldspace, location, and cell overrides.");
	DrawHintText("Import replaces the profile tabs and the saved override list in the current settings.");
	ImGui::PushID("FullPresetControls");

	const auto presetPath = GetPresetPath(fullPresetName, PresetKind::Full);
	DrawPresetNameInput("Full preset", "##FullPresetName", fullPresetName, presetPath);

	ImGui::SameLine();
	if (ImGui::Button("Export Full")) {
		ExportFullPreset();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Export global calibration, Bloom and water appearance defaults, exterior timing, the five profiles, and all saved overrides.");
	}

	ImGui::SameLine();
	if (ImGui::Button("Import Full")) {
		ImportFullPreset();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Replace global calibration, Bloom and water appearance defaults, exterior timing, the five profiles, and the saved override list.");
	}

	if (!fullPresetStatus.empty())
		ImGui::TextWrapped("%s", fullPresetStatus.c_str());

	ImGui::PopID();
}

bool AdaptiveBrightness::ExportGlobalPreset()
{
	NormalizeExteriorTimeSettings(settings);
	NormalizeBaseProfiles(settings.profiles);

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
	MarkLocationOverrideLookupDirty();
	locationOverridePresetStatus = std::format(
		"Imported {} location override(s) from {} ({} replaced, {} skipped).",
		stats.imported,
		path.filename().string(),
		stats.replaced,
		stats.skipped);
	return true;
}

bool AdaptiveBrightness::ExportFullPreset()
{
	NormalizeExteriorTimeSettings(settings);
	NormalizeBaseProfiles(settings.profiles);
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

bool AdaptiveBrightness::IsRuntimeEnabled() const
{
	if (!loaded || !settings.enabled)
		return false;

	auto state = globals::state;
	if (state && state->IsMainOrLoadingMenuOpen())
		return false;

	return GetCurrentPlayerCell(RE::PlayerCharacter::GetSingleton()) != nullptr;
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
	const auto overrideIndex = ResolveLocationOverrideIndex();
	if (overrideIndex == kInvalidLocationOverrideIndex || overrideIndex >= settings.locationOverrides.size())
		return nullptr;

	return &settings.locationOverrides[overrideIndex];
}

const AdaptiveBrightness::LocationOverride* AdaptiveBrightness::GetActiveWorldspaceOverride() const
{
	if (LocationContext::Get().inInterior)
		return nullptr;

	const auto forms = GetCurrentLocationForms();
	const auto overrideIndex = ResolveWorldspaceHierarchyOverrideIndex(forms.worldspace);
	if (overrideIndex == kInvalidLocationOverrideIndex || overrideIndex >= settings.locationOverrides.size())
		return nullptr;

	const auto& locationOverride = settings.locationOverrides[overrideIndex];
	return IsWorldspaceOverride(&locationOverride) ? &locationOverride : nullptr;
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

	return {
		.worldspace = LocationContext::Get().inInterior ?
		                  std::nullopt :
		                  makeTarget(forms.worldspace, kOverrideTypeWorldspace),
		.location = makeTarget(forms.location, kOverrideTypeLocation),
		.cell = makeTarget(forms.cell, kOverrideTypeCell),
	};
}

void AdaptiveBrightness::SaveCurrentLocationOverride(const LocationOverrideTarget& a_target)
{
	if (auto* existingOverride = FindLocationOverride(a_target.key)) {
		existingOverride->name = a_target.name;
		existingOverride->type = a_target.type;
		existingOverride->cocCode = a_target.cocCode;
		ClearLocationOverrideSelection();
		selectedLocationOverrideKey = existingOverride->key;
		return;
	}

	LocationOverride locationOverride;
	locationOverride.key = a_target.key;
	locationOverride.name = a_target.name;
	locationOverride.type = a_target.type;
	locationOverride.cocCode = a_target.cocCode;

	const auto forms = GetCurrentLocationForms();
	const LocationOverride* inheritedOverride = nullptr;
	if (a_target.type == kOverrideTypeCell) {
		inheritedOverride = GetActiveLocationOverride();
	} else if (a_target.type == kOverrideTypeLocation) {
		auto inheritedIndex = ResolveLocationHierarchyOverrideIndex(forms.location);
		if (inheritedIndex == kInvalidLocationOverrideIndex && !LocationContext::Get().inInterior)
			inheritedIndex = ResolveWorldspaceHierarchyOverrideIndex(forms.worldspace);
		if (inheritedIndex != kInvalidLocationOverrideIndex && inheritedIndex < settings.locationOverrides.size())
			inheritedOverride = &settings.locationOverrides[inheritedIndex];
	} else if (a_target.type == kOverrideTypeWorldspace) {
		const auto inheritedIndex = ResolveWorldspaceHierarchyOverrideIndex(
			forms.worldspace ? forms.worldspace->parentWorld : nullptr);
		if (inheritedIndex != kInvalidLocationOverrideIndex && inheritedIndex < settings.locationOverrides.size())
			inheritedOverride = &settings.locationOverrides[inheritedIndex];
	}

	locationOverride.profile = inheritedOverride ?
	                               inheritedOverride->profile :
	                               settings.profiles[ProfileIndex(a_target.defaultProfile)];

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
	for (auto& syncState : profileTabSyncStates) {
		syncState.key.clear();
		syncState.initialized = false;
		syncState.lastDrawFrame = -1;
	}
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
		if (resolvedIndex != kInvalidLocationOverrideIndex &&
			resolvedIndex < settings.locationOverrides.size() &&
			IsWorldspaceOverride(&settings.locationOverrides[resolvedIndex]))
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

std::size_t AdaptiveBrightness::ResolveLocationOverrideIndex() const
{
	EnsureLocationOverrideLookup();

	if (settings.locationOverrides.empty())
		return kInvalidLocationOverrideIndex;

	const auto forms = GetCurrentLocationForms();
	const bool inInterior = LocationContext::Get().inInterior;
	const uint32_t worldspaceFormID = forms.worldspace ? forms.worldspace->GetFormID() : 0;
	const uint32_t locationFormID = forms.location ? forms.location->GetFormID() : 0;
	const uint32_t cellFormID = forms.cell ? forms.cell->GetFormID() : 0;

	if (locationOverrideCache.valid &&
		locationOverrideCache.lookupVersion == locationOverrideLookupVersion &&
		locationOverrideCache.worldspaceFormID == worldspaceFormID &&
		locationOverrideCache.locationFormID == locationFormID &&
		locationOverrideCache.cellFormID == cellFormID &&
		locationOverrideCache.inInterior == inInterior) {
		return locationOverrideCache.overrideIndex;
	}

	// Resolve from most to least specific. Semantic interior profiles and the
	// exterior schedule are selected only when none of these saved scopes match.
	auto resolvedIndex = FindLocationOverrideIndexByForm(forms.cell);
	if (resolvedIndex == kInvalidLocationOverrideIndex)
		resolvedIndex = ResolveLocationHierarchyOverrideIndex(forms.location);
	if (resolvedIndex == kInvalidLocationOverrideIndex && !inInterior)
		resolvedIndex = ResolveWorldspaceHierarchyOverrideIndex(forms.worldspace);

	locationOverrideCache = {
		.worldspaceFormID = worldspaceFormID,
		.locationFormID = locationFormID,
		.cellFormID = cellFormID,
		.lookupVersion = locationOverrideLookupVersion,
		.overrideIndex = resolvedIndex,
		.inInterior = inInterior,
		.valid = true,
	};

	return resolvedIndex;
}

void AdaptiveBrightness::NormalizeLocationOverrides()
{
	if (NormalizeLocationOverrideList(settings.locationOverrides))
		MarkLocationOverrideLookupDirty();
}

void AdaptiveBrightness::MarkLocationOverrideLookupDirty()
{
	locationOverrideLookupDirty = true;
	locationOverrideCache = {};
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

	locationOverrideCache = {};
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

	const float pointLightScale = masterScale(0.75f) * advancedMult(a_profile.pointLightMult);
	out.pointLightMult = ClampMultiplier(out.pointLightMult * pointLightScale);
	out.linearPointLightMult = ClampMultiplier(out.linearPointLightMult * pointLightScale);

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
	auto effectiveSettings = baseSettings;

	if (IsRuntimeEnabled()) {
		const auto activeProfiles = GetActiveProfileBlend();
		if (activeProfiles.from == activeProfiles.to) {
			effectiveSettings = ApplyProfile(baseSettings, *activeProfiles.from);
		} else {
			const auto fromSettings = ApplyProfile(baseSettings, *activeProfiles.from);
			const auto toSettings = ApplyProfile(baseSettings, *activeProfiles.to);
			effectiveSettings = LerpSettings(fromSettings, toSettings, activeProfiles.factor);
		}
	}

	return {
		.settings = effectiveSettings,
		.hasColorAdjustments = HasAdaptiveBrightnessColorAdjustments(baseSettings, effectiveSettings)
	};
}

SharedLightingSettings AdaptiveBrightness::GetEffectiveSharedLightingSettings() const
{
	auto baseSettings = loaded && settings.globalLightingEnabled ? settings.lighting : SharedLightingSettings{};
	SanitizeSharedLightingSettings(baseSettings);
	auto effectiveSettings = baseSettings;

	if (IsRuntimeEnabled()) {
		const auto activeProfiles = GetActiveProfileBlend();
		if (activeProfiles.from == activeProfiles.to) {
			effectiveSettings = ApplyProfile(baseSettings, *activeProfiles.from);
		} else {
			const auto fromSettings = ApplyProfile(baseSettings, *activeProfiles.from);
			const auto toSettings = ApplyProfile(baseSettings, *activeProfiles.to);
			effectiveSettings = LerpSettings(fromSettings, toSettings, activeProfiles.factor);
		}
	}

	return effectiveSettings;
}

Bloom::Settings AdaptiveBrightness::GetEffectiveBloomSettings() const
{
	if (!IsRuntimeEnabled())
		return Bloom::GetCommonBufferData(Bloom::Profile{}, 0.0f);

	const auto activeProfiles = GetActiveProfileBlend();
	auto fromProfile = activeProfiles.from->bloom;
	Bloom::SanitizeProfile(fromProfile);
	if (activeProfiles.from == activeProfiles.to)
		return Bloom::GetCommonBufferData(fromProfile, 1.0f);

	auto toProfile = activeProfiles.to->bloom;
	Bloom::SanitizeProfile(toProfile);
	const float t = std::clamp(SafeFinite(activeProfiles.factor, 0.0f), 0.0f, 1.0f);
	const auto effectiveProfile = Bloom::LerpProfiles(fromProfile, toProfile, t);
	return Bloom::GetCommonBufferData(effectiveProfile, 1.0f);
}

WaterAppearance::Settings AdaptiveBrightness::GetEffectiveWaterAppearanceSettings() const
{
	if (!IsRuntimeEnabled())
		return WaterAppearance::GetCommonBufferData(WaterAppearance::Profile{});

	const auto activeProfiles = GetActiveProfileBlend();
	auto fromProfile = activeProfiles.from->water;
	WaterAppearance::SanitizeProfile(fromProfile);
	if (activeProfiles.from == activeProfiles.to)
		return WaterAppearance::GetCommonBufferData(fromProfile);

	auto toProfile = activeProfiles.to->water;
	WaterAppearance::SanitizeProfile(toProfile);
	const float t = std::clamp(SafeFinite(activeProfiles.factor, 0.0f), 0.0f, 1.0f);
	const auto effectiveProfile = WaterAppearance::LerpProfiles(fromProfile, toProfile, t);
	return WaterAppearance::GetCommonBufferData(effectiveProfile);
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

	return settings.globalLightingEnabled &&
	       UsesClassifiedPointLightMultipliers(settings.lighting);
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
	if (const auto* locationOverride = GetActiveLocationOverride())
		return { .from = &locationOverride->profile, .to = &locationOverride->profile, .factor = 0.0f };

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
