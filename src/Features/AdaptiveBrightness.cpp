#include "AdaptiveBrightness.h"

#include "LocationContext.h"
#include "State.h"
#include "Utils/FileSystem.h"
#include "Utils/Form.h"
#include "Utils/UI.h"

#include "RE/B/BGSLocation.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/S/Sky.h"
#include "RE/T/TESObjectCELL.h"

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
	vlGammaOffset)

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
	dayStartHour,
	nightStartHour,
	transitionHours,
	profiles,
	locationOverrides)

namespace
{
	constexpr float kBrightnessMin = 0.25f;
	constexpr float kBrightnessMax = 2.0f;
	constexpr float kGammaOffsetMin = -1.0f;
	constexpr float kGammaOffsetMax = 1.0f;

	using Profile = AdaptiveBrightness::Profile;

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
	constexpr const char* kPresetVersion = "1.0.0";
	constexpr std::string_view kLocationOverridesFieldName = "locationOverrides";
	constexpr std::string_view kProfilesFieldName = "profiles";
	constexpr std::string_view kGlobalPresetFilenameSuffix = "_AdaptiveBrightness_Global";
	constexpr std::string_view kLocationPresetFilenameSuffix = "_AdaptiveBrightness_LocationOverrides";
	constexpr std::string_view kFullPresetFilenameSuffix = "_AdaptiveBrightness_Full";
	constexpr const char* kImportedChangesSaveHint =
		"Import changes the current settings immediately, but those changes are not saved automatically. Use the main Save Settings button to keep them after closing the game.";
	constexpr const char* kImportedChangesSaveStatusSuffix = " Use Save Settings to keep these changes after closing the game.";

	enum class PresetKind
	{
		Global,
		Location,
		Full
	};

	struct CurrentLocationForms
	{
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

		if (a_locationOverride.type != kOverrideTypeCell)
			a_locationOverride.type = kOverrideTypeLocation;

		ClampProfileSettings(a_locationOverride.profile);
	}

	LocationOverrideImportStats ParseLocationOverridesJson(const json& a_json, std::vector<AdaptiveBrightness::LocationOverride>& o_locationOverrides)
	{
		LocationOverrideImportStats stats;
		for (const auto& entry : a_json) {
			try {
				auto locationOverride = entry.get<AdaptiveBrightness::LocationOverride>();
				if (!IsValidFormKey(locationOverride.key)) {
					++stats.skipped;
					continue;
				}

				NormalizeImportedLocationOverride(locationOverride);
				o_locationOverrides.push_back(std::move(locationOverride));
				++stats.imported;
			} catch (const std::exception&) {
				++stats.skipped;
			}
		}

		return stats;
	}

	bool HasAdvancedControlsOpen(const AdaptiveBrightness::Settings& a_settings)
	{
		const auto profileHasAdvancedControls = [](const AdaptiveBrightness::ProfileSettings& a_profile) {
			return a_profile.advanced;
		};

		return std::any_of(a_settings.profiles.begin(), a_settings.profiles.end(), profileHasAdvancedControls) ||
		       std::any_of(a_settings.locationOverrides.begin(), a_settings.locationOverrides.end(), [&](const AdaptiveBrightness::LocationOverride& a_locationOverride) {
				   return profileHasAdvancedControls(a_locationOverride.profile);
			   });
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

		for (const auto* featureName : { AdaptiveBrightness::kFeatureName.data(), AdaptiveBrightness::kFeatureShortName.data() }) {
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
			auto importedProfiles = presetJson->at(kProfilesFieldName.data()).get<std::array<AdaptiveBrightness::ProfileSettings, AdaptiveBrightness::kProfileCount>>();
			NormalizeBaseProfiles(importedProfiles);

			a_settings.dayStartHour = GetOptionalFloat(*presetJson, "dayStartHour", a_settings.dayStartHour);
			a_settings.nightStartHour = GetOptionalFloat(*presetJson, "nightStartHour", a_settings.nightStartHour);
			a_settings.transitionHours = GetOptionalFloat(*presetJson, "transitionHours", a_settings.transitionHours);
			NormalizeExteriorTimeSettings(a_settings);
			a_settings.profiles = std::move(importedProfiles);
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

		for (const auto* featureName : { AdaptiveBrightness::kFeatureName.data(), AdaptiveBrightness::kFeatureShortName.data() }) {
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

	std::string AppendImportedChangesSaveReminder(std::string a_status)
	{
		a_status += kImportedChangesSaveStatusSuffix;
		return a_status;
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

	CurrentLocationForms GetCurrentLocationForms()
	{
		const auto* player = RE::PlayerCharacter::GetSingleton();
		const auto* cell = player ? player->parentCell : nullptr;
		auto* location = player ? player->GetCurrentLocation() : nullptr;
		if (!location)
			location = cell ? cell->GetLocation() : nullptr;

		return {
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
	const auto displayName = GetDisplayName();
	ImGui::Checkbox(("Enable " + displayName).c_str(), &settings.enabled);

	if (settings.enabled) {
		const auto contextLabel = GetContextLabel();
		ImGui::TextWrapped("%s", contextLabel.c_str());
	}
}

void AdaptiveBrightness::DrawSettings()
{
	ImGui::BeginDisabled(!settings.enabled);

	DrawGlobalPresetControls();

	const auto currentProfile = GetCurrentProfileForUI();
	std::string currentProfileTabSyncKey = std::to_string(static_cast<uint32_t>(currentProfile));
	if (const auto* activeOverride = GetActiveLocationOverride()) {
		currentProfileTabSyncKey += ':';
		currentProfileTabSyncKey += activeOverride->key;
	}

	bool selectCurrentProfileTab = false;
	if (!profileTabSyncInitialized || profileTabSyncKey != currentProfileTabSyncKey) {
		selectedProfileTab = currentProfile;
		profileTabSyncKey = std::move(currentProfileTabSyncKey);
		profileTabSyncInitialized = true;
		selectCurrentProfileTab = true;
	}

	ImGui::SeparatorText("Profiles");
	if (ImGui::BeginTabBar("##AdaptiveBrightnessProfiles", ImGuiTabBarFlags_None)) {
		for (auto profile : kProfileOrder) {
			const ImGuiTabItemFlags tabFlags =
				selectCurrentProfileTab && selectedProfileTab == profile ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
			if (ImGui::BeginTabItem(GetProfileName(profile), nullptr, tabFlags)) {
				selectedProfileTab = profile;
				DrawProfile(profile);
				ImGui::EndTabItem();
			}
		}

		ImGui::EndTabBar();
	}

	DrawLocationOverrides();
	DrawFullPresetControls();

	ImGui::EndDisabled();
}

void AdaptiveBrightness::LoadSettings(json& o_json)
{
	settings = o_json;
	NormalizeExteriorTimeSettings(settings);
	NormalizeBaseProfiles(settings.profiles);
	SetAdvancedControlsOpen(HasAdvancedControlsOpen(settings));
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
	NormalizeExteriorTimeSettings(settings);
	NormalizeBaseProfiles(settings.profiles);
	NormalizeLocationOverrides();
	o_json = settings;
}

void AdaptiveBrightness::RestoreDefaultSettings()
{
	const bool keepAdvancedControlsOpen = advancedControlsOpen;
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
	SetAdvancedControlsOpen(keepAdvancedControlsOpen);
	MarkLocationOverrideLookupDirty();
}

const char* AdaptiveBrightness::GetProfileName(Profile a_profile)
{
	return kProfileNames[ProfileIndex(a_profile)];
}

void AdaptiveBrightness::DrawExteriorTimeSettings()
{
	ImGui::SeparatorText("Exterior Time");
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

void AdaptiveBrightness::DrawProfile(Profile a_profile)
{
	auto& profile = settings.profiles[ProfileIndex(a_profile)];
	const bool exteriorProfile = a_profile == Profile::ExteriorDay || a_profile == Profile::ExteriorNight;

	ImGui::PushID(static_cast<int>(ProfileIndex(a_profile)));
	if (exteriorProfile) {
		DrawExteriorTimeSettings();
		DrawProfileSettings(profile, "Profile Values");
	} else {
		DrawProfileSettings(profile);
	}
	ImGui::PopID();
}

void AdaptiveBrightness::DrawProfileSettings(ProfileSettings& a_profile, const char* a_sectionTitle)
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

	ImGui::Spacing();
	drawSlider("Scene Brightness", a_profile.brightness, kBrightnessMin, kBrightnessMax, "Overall brightness for this profile. Use it when this location type is too dark or too bright.");

	bool advancedControls = advancedControlsOpen;
	if (ImGui::Checkbox("Advanced Controls", &advancedControls))
		SetAdvancedControlsOpen(advancedControls);

	a_profile.advanced = advancedControlsOpen;

	if (advancedControlsOpen) {
		ImGui::SeparatorText("Light Multipliers");
		ImGui::SliderFloat("Directional Light", &a_profile.directionalLightMult, 0.0f, 3.0f, "%.2f");
		ImGui::SliderFloat("Point Lights", &a_profile.pointLightMult, 0.0f, 3.0f, "%.2f");
		ImGui::SliderFloat("Ambient", &a_profile.ambientMult, 0.0f, 3.0f, "%.2f");
		ImGui::SliderFloat("Emissive", &a_profile.emitColorMult, 0.0f, 3.0f, "%.2f");
		ImGui::SliderFloat("Glowmaps", &a_profile.glowmapMult, 0.0f, 3.0f, "%.2f");
		ImGui::SliderFloat("Effects", &a_profile.effectLightingMult, 0.0f, 3.0f, "%.2f");

		ImGui::SeparatorText("Atmosphere Gamma Offsets");
		ImGui::SliderFloat("Sky", &a_profile.skyGammaOffset, kGammaOffsetMin, kGammaOffsetMax, "%.2f");
		ImGui::SliderFloat("Fog", &a_profile.fogGammaOffset, kGammaOffsetMin, kGammaOffsetMax, "%.2f");
		ImGui::SliderFloat("Fog Transparency", &a_profile.fogAlphaGammaOffset, kGammaOffsetMin, kGammaOffsetMax, "%.2f");
		ImGui::SliderFloat("Water", &a_profile.waterGammaOffset, kGammaOffsetMin, kGammaOffsetMax, "%.2f");
		ImGui::SliderFloat("Volumetric Lighting", &a_profile.vlGammaOffset, kGammaOffsetMin, kGammaOffsetMax, "%.2f");
	}

	ImGui::Unindent();
	ClampProfileSettings(a_profile);
}

void AdaptiveBrightness::SetAdvancedControlsOpen(bool a_open)
{
	advancedControlsOpen = a_open;

	for (auto& profile : settings.profiles)
		profile.advanced = advancedControlsOpen;

	for (auto& locationOverride : settings.locationOverrides)
		locationOverride.profile.advanced = advancedControlsOpen;

	if (locationOverrideEditProfile)
		locationOverrideEditProfile->advanced = advancedControlsOpen;
}

void AdaptiveBrightness::DrawGlobalPresetControls()
{
	ImGui::SeparatorText("Global Presets");
	DrawHintText("Global presets store the five profile tabs and exterior timing.");
	DrawHintText("Import overwrites those profile tabs in the current settings. Saved location overrides are not changed.");
	DrawHintText(kImportedChangesSaveHint);
	ImGui::PushID("GlobalPresetControls");

	const auto presetPath = GetPresetPath(globalPresetName, PresetKind::Global);
	DrawPresetNameInput("Global preset", "##GlobalPresetName", globalPresetName, presetPath);

	ImGui::SameLine();
	if (ImGui::Button("Export Global")) {
		ExportGlobalPreset();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Export exterior timing and the five profile tabs. Location overrides are not included.");
	}

	ImGui::SameLine();
	if (ImGui::Button("Import Global")) {
		ImportGlobalPreset();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Replace the five profile tabs in the current settings. Saved location overrides stay unchanged.");
		ImGui::Text("%s", kImportedChangesSaveHint);
	}

	if (!globalPresetStatus.empty())
		ImGui::TextWrapped("%s", globalPresetStatus.c_str());

	ImGui::PopID();
}

void AdaptiveBrightness::DrawLocationOverrides()
{
	ImGui::SeparatorText("Location Override Profiles");
	DrawHintText("Location overrides are per-place profiles. A saved override is used when its location or cell matches where you are.");
	DrawHintText("Import adds overrides from a preset to the override list below. Later edits change this list, not the preset file.");
	DrawHintText(kImportedChangesSaveHint);

	const auto target = GetCurrentLocationOverrideTarget();
	const auto* activeOverride = GetActiveLocationOverride();
	const auto currentProfile = GetCurrentProfileForUI();
	const bool hasSavedTarget = target && FindLocationOverride(target->key) != nullptr;
	const char* currentOverrideButtonLabel = hasSavedTarget ? "Open Current Location Override" : "Create Current Location Override";

	if (activeOverride) {
		ImGui::TextWrapped("Using saved override \"%s\" here. Base profile: %s.", activeOverride->name.c_str(), GetProfileName(currentProfile));
	} else {
		ImGui::TextWrapped("Using base profile %s here. No saved override matches this place.", GetProfileName(currentProfile));
	}

	ImGui::BeginDisabled(!target.has_value());
	if (ImGui::Button(currentOverrideButtonLabel)) {
		SaveCurrentLocationOverride();
	}
	ImGui::EndDisabled();

	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Save this place as an override, or open its existing override.");
	}

	ImGui::SameLine();
	ImGui::TextDisabled("(%zu saved)", settings.locationOverrides.size());

	if (target) {
		ImGui::TextWrapped("Current target: %s (form %s, %s, COC %s)", target->name.c_str(), target->type.c_str(), target->key.c_str(), GetCocLabel(target->cocCode));
	} else {
		ImGui::TextDisabled("No current location or cell form is available.");
	}

	DrawLocationOverridePresetControls();
	ImGui::SeparatorText("Saved Overrides");
	DrawHintText("These saved overrides are matched by location or cell. Click a row to edit it.");

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
			if (ImGui::SmallButton("Edit")) {
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
				ImGui::Text("Copy the coc command for this saved cell.");
			}
			if (ImGui::SmallButton("Delete")) {
				deleteIndex = overrideIndex;
			}

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
		ImGui::SeparatorText("Edit Location Override Profile");
		ImGui::TextWrapped("%s (%s, %s)", selectedOverride->name.c_str(), selectedOverride->type.c_str(), selectedOverride->key.c_str());
		ImGui::PushID(selectedOverride->key.c_str());
		if (auto* editProfile = GetLocationOverrideEditProfile(*selectedOverride)) {
			DrawProfileSettings(*editProfile, "Override Profile Values");
			if (ImGui::Button("Save Edit")) {
				selectedOverride->profile = *editProfile;
				ClearLocationOverrideSelection();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Save these values to the selected override.");
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) {
				ClearLocationOverrideSelection();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Discard changes to the selected override.");
			}
		}
		ImGui::PopID();
	} else if (!selectedLocationOverrideKey.empty()) {
		ClearLocationOverrideSelection();
	}
}

void AdaptiveBrightness::DrawLocationOverridePresetControls()
{
	ImGui::SeparatorText("Override Presets");
	DrawHintText("Override presets store saved location and cell overrides only. They do not include the five profile tabs.");
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
		ImGui::Text("%s", kImportedChangesSaveHint);
	}

	if (!locationOverridePresetStatus.empty())
		ImGui::TextWrapped("%s", locationOverridePresetStatus.c_str());

	ImGui::PopID();
}

void AdaptiveBrightness::DrawFullPresetControls()
{
	ImGui::SeparatorText("Full Presets");
	DrawHintText("Full presets store exterior timing, the five profile tabs, and all saved location overrides.");
	DrawHintText("Import replaces the profile tabs and the saved override list in the current settings.");
	DrawHintText(kImportedChangesSaveHint);
	ImGui::PushID("FullPresetControls");

	const auto presetPath = GetPresetPath(fullPresetName, PresetKind::Full);
	DrawPresetNameInput("Full preset", "##FullPresetName", fullPresetName, presetPath);

	ImGui::SameLine();
	if (ImGui::Button("Export Full")) {
		ExportFullPreset();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Export exterior timing, the five profile tabs, and all saved overrides.");
	}

	ImGui::SameLine();
	if (ImGui::Button("Import Full")) {
		ImportFullPreset();
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Replace the profile tabs and saved override list in the current settings.");
		ImGui::Text("%s", kImportedChangesSaveHint);
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
			"Adaptive Brightness global preset");

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

	advancedControlsOpen = HasAdvancedControlsOpen(settings);
	globalPresetStatus = AppendImportedChangesSaveReminder(std::format("Imported global preset from {}.", resolvedPath->filename().string()));
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
			"Adaptive Brightness location override preset");

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
	NormalizeLocationOverrides();

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

	std::unordered_map<std::string, std::size_t> existingIndices;
	existingIndices.reserve(settings.locationOverrides.size() + overridesJson->size());
	for (std::size_t i = 0; i < settings.locationOverrides.size(); ++i) {
		const auto& locationOverride = settings.locationOverrides[i];
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

	for (auto& locationOverride : importedOverrides) {
		const auto key = locationOverride.key;
		if (auto it = existingIndices.find(key); it != existingIndices.end() && it->second < settings.locationOverrides.size()) {
			settings.locationOverrides[it->second] = std::move(locationOverride);
			++stats.replaced;
		} else {
			existingIndices[key] = settings.locationOverrides.size();
			settings.locationOverrides.push_back(std::move(locationOverride));
		}

		selectedLocationOverrideKey = key;
	}

	ResetLocationOverrideEdit();
	NormalizeLocationOverrides();
	MarkLocationOverrideLookupDirty();
	advancedControlsOpen = HasAdvancedControlsOpen(settings);
	locationOverridePresetStatus = AppendImportedChangesSaveReminder(std::format(
		"Imported {} location override(s) from {} ({} replaced, {} skipped).",
		stats.imported,
		path.filename().string(),
		stats.replaced,
		stats.skipped));
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
			"Adaptive Brightness full preset");
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
	advancedControlsOpen = HasAdvancedControlsOpen(settings);
	fullPresetStatus = AppendImportedChangesSaveReminder(std::format(
		"Imported full preset from {} ({} location override(s), {} skipped).",
		resolvedPath->filename().string(),
		settings.locationOverrides.size(),
		stats.skipped));
	return true;
}

bool AdaptiveBrightness::IsRuntimeEnabled() const
{
	if (!loaded || !settings.enabled)
		return false;

	auto state = globals::state;
	if (state && state->IsMainOrLoadingMenuOpen())
		return false;

	return true;
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

std::optional<AdaptiveBrightness::LocationOverrideTarget> AdaptiveBrightness::GetCurrentLocationOverrideTarget() const
{
	const auto forms = GetCurrentLocationForms();
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

	if (auto target = makeTarget(forms.location, kOverrideTypeLocation))
		return target;

	return makeTarget(forms.cell, kOverrideTypeCell);
}

void AdaptiveBrightness::SaveCurrentLocationOverride()
{
	const auto target = GetCurrentLocationOverrideTarget();
	if (!target)
		return;

	if (auto* existingOverride = FindLocationOverride(target->key)) {
		existingOverride->name = target->name;
		existingOverride->type = target->type;
		existingOverride->cocCode = target->cocCode;
		ClearLocationOverrideSelection();
		selectedLocationOverrideKey = existingOverride->key;
		return;
	}

	LocationOverride locationOverride;
	locationOverride.key = target->key;
	locationOverride.name = target->name;
	locationOverride.type = target->type;
	locationOverride.cocCode = target->cocCode;
	if (const auto* activeOverride = GetActiveLocationOverride()) {
		locationOverride.profile = activeOverride->profile;
	} else {
		locationOverride.profile = settings.profiles[ProfileIndex(target->defaultProfile)];
	}

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
	profileTabSyncKey.clear();
	profileTabSyncInitialized = false;
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

std::size_t AdaptiveBrightness::ResolveLocationOverrideIndex() const
{
	EnsureLocationOverrideLookup();

	if (settings.locationOverrides.empty())
		return kInvalidLocationOverrideIndex;

	const auto forms = GetCurrentLocationForms();
	const uint32_t locationFormID = forms.location ? forms.location->GetFormID() : 0;
	const uint32_t cellFormID = forms.cell ? forms.cell->GetFormID() : 0;

	if (locationOverrideCache.valid &&
		locationOverrideCache.lookupVersion == locationOverrideLookupVersion &&
		locationOverrideCache.locationFormID == locationFormID &&
		locationOverrideCache.cellFormID == cellFormID) {
		return locationOverrideCache.overrideIndex;
	}

	auto resolvedIndex = kInvalidLocationOverrideIndex;

	for (auto* current = forms.location; current; current = current->parentLoc) {
		resolvedIndex = FindLocationOverrideIndexByForm(current);
		if (resolvedIndex != kInvalidLocationOverrideIndex)
			break;
	}

	if (resolvedIndex == kInvalidLocationOverrideIndex)
		resolvedIndex = FindLocationOverrideIndexByForm(forms.cell);

	locationOverrideCache = {
		.locationFormID = locationFormID,
		.cellFormID = cellFormID,
		.lookupVersion = locationOverrideLookupVersion,
		.overrideIndex = resolvedIndex,
		.valid = true,
	};

	return resolvedIndex;
}

void AdaptiveBrightness::NormalizeLocationOverrides()
{
	bool changedLookup = false;

	for (auto it = settings.locationOverrides.begin(); it != settings.locationOverrides.end();) {
		if (!IsValidFormKey(it->key)) {
			it = settings.locationOverrides.erase(it);
			changedLookup = true;
			continue;
		}

		if (it->name.empty())
			it->name = it->key;

		if (it->type != kOverrideTypeCell)
			it->type = kOverrideTypeLocation;

		ClampProfileSettings(it->profile);
		++it;
	}

	if (settings.locationOverrides.size() > 1) {
		std::unordered_set<std::string> seenKeys;
		std::vector<LocationOverride> dedupedOverrides;
		dedupedOverrides.reserve(settings.locationOverrides.size());

		for (auto it = settings.locationOverrides.rbegin(); it != settings.locationOverrides.rend(); ++it) {
			if (seenKeys.insert(it->key).second)
				dedupedOverrides.push_back(*it);
			else
				changedLookup = true;
		}

		std::reverse(dedupedOverrides.begin(), dedupedOverrides.end());
		if (dedupedOverrides.size() != settings.locationOverrides.size())
			settings.locationOverrides = std::move(dedupedOverrides);
	}

	if (changedLookup)
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

	const float hoursIntoDay = HoursSince(dayStart, hour);
	float transition = std::clamp(SafeFinite(settings.transitionHours, 1.0f), 0.0f, 4.0f);
	transition = std::min(transition, dayLength * 0.5f);

	if (transition <= 0.0f)
		return hoursIntoDay < dayLength ? 0.0f : 1.0f;

	float dayFactor = 0.0f;
	if (hoursIntoDay < dayLength) {
		dayFactor = 1.0f;

		if (hoursIntoDay < transition) {
			dayFactor = SmoothStep(0.0f, transition, hoursIntoDay);
		} else if (hoursIntoDay > dayLength - transition) {
			dayFactor = 1.0f - SmoothStep(dayLength - transition, dayLength, hoursIntoDay);
		}
	}

	return 1.0f - std::clamp(dayFactor, 0.0f, 1.0f);
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

	out.directionalLightMult = ClampMultiplier(out.directionalLightMult * masterScale(0.70f) * advancedMult(a_profile.directionalLightMult));
	out.pointLightMult = ClampMultiplier(out.pointLightMult * masterScale(0.75f) * advancedMult(a_profile.pointLightMult));
	out.ambientMult = ClampMultiplier(out.ambientMult * masterScale(0.95f) * advancedMult(a_profile.ambientMult));
	out.emitColorMult = ClampMultiplier(out.emitColorMult * masterScale(0.35f) * advancedMult(a_profile.emitColorMult));
	out.glowmapMult = ClampMultiplier(out.glowmapMult * masterScale(0.35f) * advancedMult(a_profile.glowmapMult));
	out.effectLightingMult = ClampMultiplier(out.effectLightingMult * masterScale(0.55f) * advancedMult(a_profile.effectLightingMult));

	out.skyGamma = ClampGamma(out.skyGamma + masterGammaOffset * 0.90f + advancedOffset(a_profile.skyGammaOffset));
	out.fogGamma = ClampGamma(out.fogGamma + masterGammaOffset * 0.75f + advancedOffset(a_profile.fogGammaOffset));
	out.fogAlphaGamma = ClampGamma(out.fogAlphaGamma + masterGammaOffset * 0.50f + advancedOffset(a_profile.fogAlphaGammaOffset));
	out.waterGamma = ClampGamma(out.waterGamma + masterGammaOffset * 0.75f + advancedOffset(a_profile.waterGammaOffset));
	out.vlGamma = ClampGamma(out.vlGamma + masterGammaOffset * 0.85f + advancedOffset(a_profile.vlGammaOffset));

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
	out.directionalLightMult = lerp(a_a.directionalLightMult, a_b.directionalLightMult);
	out.pointLightMult = lerp(a_a.pointLightMult, a_b.pointLightMult);
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

LinearLighting::Settings AdaptiveBrightness::GetEffectiveLinearLightingSettings(const LinearLighting::Settings& a_linearLightingSettings, bool a_linearLightingEnabled) const
{
	auto baseSettings = a_linearLightingEnabled ? a_linearLightingSettings : GetNeutralLinearLightingSettings();

	if (!IsRuntimeEnabled())
		return baseSettings;

	const auto activeProfiles = GetActiveProfileBlend();
	if (activeProfiles.from == activeProfiles.to)
		return ApplyProfile(baseSettings, *activeProfiles.from);

	const auto fromSettings = ApplyProfile(baseSettings, *activeProfiles.from);
	const auto toSettings = ApplyProfile(baseSettings, *activeProfiles.to);
	return LerpSettings(fromSettings, toSettings, activeProfiles.factor);
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
	constexpr auto displayName = kFeatureName;

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
