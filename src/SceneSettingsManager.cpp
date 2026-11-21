#include "SceneSettingsManager.h"

#include "Feature.h"
#include "Globals.h"
#include "Menu.h"
#include "Presets/LookSceneRuntime.h"
#include "Presets/SettingsDomains.h"
#include "State.h"
#include "Utils/FileSystem.h"
#include "Utils/Game.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace
{
	void ApplyFeatureSettings(const std::map<std::string, json>& candidate)
	{
		std::map<std::string, json> before;
		std::vector<Feature*> touched;
		try {
			for (const auto& [name, values] : candidate) {
				auto* feature = Feature::FindFeatureByShortName(name);
				if (!feature || !feature->loaded)
					continue;
				feature->SaveSettings(before[name]);
				if (before[name] == values)
					continue;
				touched.push_back(feature);
				auto settings = values;
				feature->LoadSettings(settings);
			}
		} catch (const std::exception& failure) {
			std::string error = failure.what();
			for (auto it = touched.rbegin(); it != touched.rend(); ++it) {
				try {
					(*it)->LoadSettings(before.at((*it)->GetShortName()));
				} catch (const std::exception& rollback) {
					error += "\nScene rollback failed: " + std::string(rollback.what());
				}
			}
			throw std::runtime_error(error);
		}
	}

	bool AreAllEntriesPaused(
		const std::vector<SceneSettingsManager::SettingEntry>& entries,
		SceneSettingsManager::EntrySource source)
	{
		bool found = false;
		for (const auto& entry : entries) {
			if (entry.source != source)
				continue;
			found = true;
			if (!entry.paused)
				return false;
		}
		return found;
	}
}

// --- Path Resolution ---

SceneSettingsManager::BaseSettingsScope::BaseSettingsScope()
{
	auto* manager = GetSingleton();
	if (manager->baseSettingsDepth == 0) {
		Presets::LookSceneRuntime::Suspend();
		manager->resumeInterior = manager->isCurrentlyApplied;
		try {
			if (manager->isCurrentlyApplied)
				manager->RevertToExteriorSettings();
		} catch (...) {
			Presets::LookSceneRuntime::Resume();
			throw;
		}
		manager->isCurrentlyApplied = false;
	}
	++manager->baseSettingsDepth;
}

void SceneSettingsManager::BaseSettingsScope::Finish()
{
	if (!active)
		return;
	auto* manager = GetSingleton();
	if (manager->baseSettingsDepth == 1 && manager->resumeInterior) {
		manager->SaveExteriorSettings(SceneType::InteriorOnly);
		try {
			manager->ApplySettings(SceneType::InteriorOnly);
		} catch (...) {
			manager->savedExteriorSettings.clear();
			throw;
		}
		manager->isCurrentlyApplied = true;
	}
	--manager->baseSettingsDepth;
	if (!manager->baseSettingsDepth)
		Presets::LookSceneRuntime::Resume();
	active = false;
}

SceneSettingsManager::BaseSettingsScope::~BaseSettingsScope()
{
	try {
		Finish();
	} catch (const std::exception& error) {
		auto* manager = GetSingleton();
		--manager->baseSettingsDepth;
		if (!manager->baseSettingsDepth)
			Presets::LookSceneRuntime::Resume();
		manager->queuedCellTransition = true;
		logger::error("Could not restore interior settings: {}", error.what());
	}
}

SceneSettingsManager::LookState SceneSettingsManager::CaptureLookState() const
{
	return { GetEntries(SceneType::InteriorOnly), lookPersistence };
}

void SceneSettingsManager::RestoreLookState(LookState state)
{
	GetEntriesMut(SceneType::InteriorOnly) = std::move(state.interior);
	lookPersistence = state.persistent;
	queuedCellTransition = true;
}

std::array<SceneSettingsManager::EntrySource, 3> SceneSettingsManager::ApplicationOrder() const
{
	return lookPersistence ? std::array{ EntrySource::Overwrite, EntrySource::LookPack, EntrySource::User } :
	                         std::array{ EntrySource::User, EntrySource::Overwrite, EntrySource::LookPack };
}

void SceneSettingsManager::SetLookLayer(const json& layer, const std::string& packId)
{
	BaseSettingsScope scope;
	auto& values = GetEntriesMut(SceneType::InteriorOnly);
	std::erase_if(values, [](const SettingEntry& entry) { return entry.source == EntrySource::LookPack; });
	for (const auto& [feature, settings] : layer.items())
		for (const auto& [key, value] : settings.items())
			values.push_back({ feature, key, value, false, EntrySource::LookPack, packId });
	queuedCellTransition = true;
}

json SceneSettingsManager::ExportLookLayer() const
{
	json result = json::object();
	for (const auto source : ApplicationOrder()) {
		for (const auto& entry : GetEntries(SceneType::InteriorOnly)) {
			if (entry.source != source || !IsEntryActive(entry))
				continue;
			json patch = { { entry.settingKey, entry.value } };
			patch = Presets::SelectDomain(entry.featureShortName, patch, Presets::SettingsDomain::Appearance);
			if (patch.empty())
				continue;
			if (!result.contains(entry.featureShortName))
				result[entry.featureShortName] = json::object();
			Presets::Overlay(result[entry.featureShortName], patch);
		}
	}
	return result;
}

void SceneSettingsManager::UseLookPersistence(bool enabled)
{
	if (lookPersistence == enabled)
		return;
	lookPersistence = enabled;
	if (!enabled) {
		auto& values = GetEntriesMut(SceneType::InteriorOnly);
		std::erase_if(values, [](const auto& entry) { return entry.source == EntrySource::User; });
		LoadUserSettings(SceneType::InteriorOnly);
	}
}

void SceneSettingsManager::OverlayBaseSettings(const std::string& feature, json& values, const std::set<std::string>& weatherVariables) const
{
	if (!isCurrentlyApplied)
		return;
	if (const auto base = savedExteriorSettings.find(feature); base != savedExteriorSettings.end())
		for (const auto& [key, value] : base->second.items())
			if (!weatherVariables.contains(key))
				values[key] = value;
}

json SceneSettingsManager::SerializeLookEdits() const
{
	json values = json::array();
	for (const auto& entry : GetEntries(SceneType::InteriorOnly))
		if (entry.source == EntrySource::User)
			values.push_back({ { "feature", entry.featureShortName }, { "setting", entry.settingKey }, { "value", entry.value }, { "paused", entry.paused } });
	return values;
}

void SceneSettingsManager::LoadLookEdits(const json& edits, const json& schemas)
{
	if (!edits.is_array() || edits.size() > 4096)
		throw std::runtime_error("Invalid saved interior Look edits");
	std::vector<SettingEntry> parsed;
	std::set<std::pair<std::string, std::string>> keys;
	for (const auto& entry : edits) {
		if (!entry.is_object() || !entry.contains("feature") || !entry["feature"].is_string() ||
			!entry.contains("setting") || !entry["setting"].is_string() || !entry.contains("value") ||
			!entry.contains("paused") || !entry["paused"].is_boolean())
			throw std::runtime_error("Invalid saved interior Look entry");
		const auto name = entry["feature"].get<std::string>();
		const auto key = entry["setting"].get<std::string>();
		if (name.empty() || key.empty() || !keys.emplace(name, key).second)
			throw std::runtime_error("Missing or duplicate interior setting");
		std::string error;
		if (schemas.contains(name) && !schemas[name].empty() && !Presets::ValidatePatch(json{ { key, entry["value"] } }, schemas[name], error, "/" + name))
			throw std::runtime_error(error);
		parsed.push_back({ name, key, entry["value"], entry["paused"].get<bool>(), EntrySource::User, "" });
	}
	BaseSettingsScope scope;
	auto& entriesForInterior = GetEntriesMut(SceneType::InteriorOnly);
	std::erase_if(entriesForInterior, [](const auto& entry) { return entry.source == EntrySource::User; });
	entriesForInterior.insert(entriesForInterior.end(), parsed.begin(), parsed.end());
}

void SceneSettingsManager::ClearLookEditsUnder(const json& layer)
{
	auto& values = GetEntriesMut(SceneType::InteriorOnly);
	std::erase_if(values, [&](const auto& entry) {
		return entry.source == EntrySource::User && layer.contains(entry.featureShortName) &&
		       layer[entry.featureShortName].contains(entry.settingKey) && !entry.value.is_object();
	});
	for (auto& entry : values) {
		if (entry.source == EntrySource::User && entry.value.is_object() && layer.contains(entry.featureShortName) &&
			layer[entry.featureShortName].contains(entry.settingKey))
			Presets::RemoveCovered(entry.value, layer[entry.featureShortName][entry.settingKey]);
	}
	std::erase_if(values, [](const auto& entry) { return entry.source == EntrySource::User && entry.value.is_object() && entry.value.empty(); });
}

std::string SceneSettingsManager::GetSceneTypeName(SceneType type)
{
	switch (type) {
	case SceneType::InteriorOnly:
		return "InteriorOnly";
	default:
		return "Unknown";
	}
}

std::filesystem::path SceneSettingsManager::GetSettingsFilePath(SceneType type)
{
	return Util::PathHelpers::GetSceneSettingsPath() / (GetSceneTypeName(type) + ".json");
}

std::filesystem::path SceneSettingsManager::GetOverwritesPath(SceneType type)
{
	return Util::PathHelpers::GetSceneSettingsPath() / GetSceneTypeName(type);
}

// --- Feature Metadata (static helpers, zero coupling) ---

std::vector<std::string> SceneSettingsManager::GetInteriorRelevantFeatureNames()
{
	// Features that are relevant for interior-only setting overrides.
	// Excludes exterior-only features: terrain, grass, LOD, sky, cloud shadows.
	static const std::unordered_set<std::string> interiorRelevantFeatures = {
		"ScreenSpaceGI",
		"ScreenSpaceShadows",
		"SubsurfaceScattering",
		"LinearLighting",
		"ImageBasedLighting",
		"PostProcessing",
		"ScreenSpacePointLightShadows",
		"ScreenSpaceRayTracing",
		"VanillaFresnel",
	};

	auto allNames = Feature::GetLoadedFeatureNames();
	std::vector<std::string> filtered;
	filtered.reserve(allNames.size());
	for (auto& name : allNames) {
		if (interiorRelevantFeatures.contains(name))
			filtered.push_back(std::move(name));
	}
	return filtered;
}

std::vector<std::string> SceneSettingsManager::GetFeatureSettingKeys(const std::string& featureShortName)
{
	std::vector<std::string> keys;
	auto* feature = Feature::FindFeatureByShortName(featureShortName);
	if (!feature)
		return keys;

	json settings;
	feature->SaveSettings(settings);
	if (!settings.is_object())
		return keys;

	for (auto& [key, _] : settings.items())
		keys.push_back(key);

	std::sort(keys.begin(), keys.end());
	return keys;
}

json SceneSettingsManager::GetFeatureSettingValue(const std::string& featureShortName, const std::string& settingKey)
{
	auto* feature = Feature::FindFeatureByShortName(featureShortName);
	if (!feature)
		return {};

	json settings;
	feature->SaveSettings(settings);
	if (settings.is_object() && settings.contains(settingKey))
		return settings[settingKey];

	return {};
}

SceneSettingsManager::SettingType SceneSettingsManager::DetectSettingType(const json& value)
{
	if (value.is_boolean())
		return SettingType::Boolean;
	if (value.is_number_integer())
		return SettingType::Integer;
	if (value.is_number_float())
		return SettingType::Float;
	if (value.is_string())
		return SettingType::String;
	return SettingType::Unknown;
}

// --- Generic Entry Management ---

std::vector<SceneSettingsManager::SettingEntry>& SceneSettingsManager::GetEntriesMut(SceneType type)
{
	return entries[type];
}

const std::vector<SceneSettingsManager::SettingEntry>& SceneSettingsManager::GetEntries(SceneType type) const
{
	static const std::vector<SettingEntry> empty;
	auto it = entries.find(type);
	return (it != entries.end()) ? it->second : empty;
}

bool SceneSettingsManager::IsEntryActive(const SettingEntry& entry) const
{
	return !entry.paused && !IsFeaturePaused(entry.featureShortName);
}

bool SceneSettingsManager::HasEntryFromSource(SceneType type, const std::string& featureShortName, const std::string& settingKey, EntrySource source) const
{
	for (const auto& entry : GetEntries(type)) {
		if (entry.source == source && entry.featureShortName == featureShortName && entry.settingKey == settingKey)
			return true;
	}
	return false;
}

bool SceneSettingsManager::HasActiveOverwrite(SceneType type, const std::string& featureShortName, const std::string& settingKey) const
{
	if (lookPersistence)
		return false;
	for (const auto& entry : GetEntries(type)) {
		if (entry.source == EntrySource::Overwrite && !entry.paused &&
			entry.featureShortName == featureShortName && entry.settingKey == settingKey)
			return true;
	}
	return false;
}

void SceneSettingsManager::AddSetting(SceneType type, const std::string& featureShortName, const std::string& settingKey, const json& value)
{
	if (HasEntryFromSource(type, featureShortName, settingKey, EntrySource::User))
		return;

	auto& vec = GetEntriesMut(type);

	SettingEntry entry;
	entry.featureShortName = featureShortName;
	entry.settingKey = settingKey;
	entry.value = value;
	entry.source = EntrySource::User;
	vec.push_back(std::move(entry));
	SaveUserSettings(type);
	ReapplyIfActive();
}

void SceneSettingsManager::RemoveSetting(SceneType type, size_t index)
{
	auto& vec = GetEntriesMut(type);
	if (index >= vec.size() || vec[index].source == EntrySource::LookPack)
		return;

	auto& entry = vec[index];
	if (entry.source == EntrySource::Overwrite && !entry.sourceFilename.empty()) {
		auto filepath = GetOverwritesPath(type) / entry.sourceFilename;
		std::error_code ec;
		if (std::filesystem::remove(filepath, ec))
			logger::info("[SceneSettings] Deleted overwrite file: {}", filepath.string());
		else
			logger::error("[SceneSettings] Failed to delete overwrite file: {} ({})", filepath.string(), ec.message());
	}

	logger::info("[SceneSettings] Removed {} entry: {}.{} (source={})", GetSceneTypeName(type),
		entry.featureShortName, entry.settingKey,
		entry.source == EntrySource::Overwrite ? "overwrite" : "user");

	vec.erase(vec.begin() + static_cast<ptrdiff_t>(index));
	SaveUserSettings(type);
	ReapplyIfActive();
}

void SceneSettingsManager::TogglePauseEntry(SceneType type, size_t index)
{
	auto& vec = GetEntriesMut(type);
	if (index < vec.size() && vec[index].source != EntrySource::LookPack) {
		vec[index].paused = !vec[index].paused;
		if (vec[index].source == EntrySource::User)
			SaveUserSettings(type);
		ReapplyIfActive();
	}
}

bool SceneSettingsManager::HasOverwriteEntries(SceneType type) const
{
	for (const auto& entry : GetEntries(type))
		if (entry.source == EntrySource::Overwrite)
			return true;
	return false;
}

void SceneSettingsManager::SetAllOverwritesPaused(SceneType type, bool paused)
{
	for (auto& entry : GetEntriesMut(type))
		if (entry.source == EntrySource::Overwrite)
			entry.paused = paused;
	ReapplyIfActive();
}

bool SceneSettingsManager::AreAllOverwritesPaused(SceneType type) const
{
	return AreAllEntriesPaused(GetEntries(type), EntrySource::Overwrite);
}

void SceneSettingsManager::DeleteAllOverwrites(SceneType type)
{
	auto overwritesPath = GetOverwritesPath(type);
	std::error_code ec;

	auto& vec = GetEntriesMut(type);
	for (const auto& entry : vec) {
		if (entry.source == EntrySource::Overwrite && !entry.sourceFilename.empty())
			std::filesystem::remove(overwritesPath / entry.sourceFilename, ec);
	}

	std::erase_if(vec, [](const SettingEntry& e) {
		return e.source == EntrySource::Overwrite;
	});

	ReapplyIfActive();
}

void SceneSettingsManager::SetAllUserPaused(SceneType type, bool paused)
{
	for (auto& entry : GetEntriesMut(type))
		if (entry.source == EntrySource::User)
			entry.paused = paused;
	SaveUserSettings(type);
	ReapplyIfActive();
}

bool SceneSettingsManager::AreAllUserPaused(SceneType type) const
{
	return AreAllEntriesPaused(GetEntries(type), EntrySource::User);
}

void SceneSettingsManager::DeleteAllUserSettings(SceneType type)
{
	auto& vec = GetEntriesMut(type);
	std::erase_if(vec, [](const SettingEntry& e) {
		return e.source == EntrySource::User;
	});

	SaveUserSettings(type);
	ReapplyIfActive();
}

void SceneSettingsManager::UpdateEntryValue(SceneType type, size_t index, const json& newValue, bool deferSave)
{
	auto& vec = GetEntriesMut(type);
	if (index >= vec.size() || vec[index].source == EntrySource::LookPack)
		return;

	vec[index].value = newValue;

	if (!deferSave && vec[index].source == EntrySource::User)
		SaveUserSettings(type);

	ReapplyIfActive();
}

// --- Event Handler ---

RE::BSEventNotifyControl SceneSettingsManager::MenuOpenCloseEventHandler::ProcessEvent(
	const RE::MenuOpenCloseEvent* a_event,
	RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (a_event && a_event->menuName == RE::LoadingMenu::MENU_NAME && !a_event->opening) {
		// Defer cell transition to next frame ? cell data isn't available yet
		// when this event fires. Same pattern as Skylighting::queuedResetSkylighting.
		GetSingleton()->queuedCellTransition = true;
	}

	return RE::BSEventNotifyControl::kContinue;
}

// --- Scene Application ---

void SceneSettingsManager::Update()
{
	// Revert interior overrides on main/loading menu (same check as LinearLighting)
	if (isCurrentlyApplied) {
		bool isMainOrLoading = globals::state->isMainMenuOpen || globals::state->isLoadingMenuOpen;
		if (isMainOrLoading) {
			RevertToExteriorSettings();
			isCurrentlyApplied = false;
		}
	}

	if (queuedCellTransition) {
		queuedCellTransition = false;
		OnCellTransition();
	}
}

void SceneSettingsManager::OnCellTransition()
{
	if (baseSettingsDepth != 0) {
		queuedCellTransition = true;
		return;
	}
	if (globals::state->isMainMenuOpen || globals::state->isLoadingMenuOpen)
		return;
	// Match Skylighting's interior detection: sky mode != kFull
	bool interior = true;
	if (auto sky = globals::game::sky)
		interior = sky->mode.get() != RE::Sky::Mode::kFull;

	if (interior && !isCurrentlyApplied) {
		SaveExteriorSettings(SceneType::InteriorOnly);
		ApplySettings(SceneType::InteriorOnly);
		isCurrentlyApplied = true;
	} else if (!interior && isCurrentlyApplied) {
		RevertToExteriorSettings();
		isCurrentlyApplied = false;
	}
}

void SceneSettingsManager::ReapplyIfActive()
{
	if (!isCurrentlyApplied)
		return;

	// Full revert + re-apply so removed/paused entries get exterior values restored
	BaseSettingsScope scope;
	scope.Finish();
}

bool SceneSettingsManager::IsSettingControlled(const std::string& featureShortName, const std::string& settingKey) const
{
	if (Presets::LookSceneRuntime::Controls(featureShortName, settingKey))
		return true;
	if (!isCurrentlyApplied)
		return false;
	if (IsFeaturePaused(featureShortName))
		return false;

	// Check all scene types for active overrides
	for (const auto& [type, vec] : entries) {
		for (const auto& entry : vec) {
			if (entry.paused)
				continue;
			if (entry.featureShortName == featureShortName && entry.settingKey == settingKey)
				return true;
		}
	}
	return false;
}

bool SceneSettingsManager::HasActiveSettingsForFeature(const std::string& featureShortName) const
{
	if (Presets::LookSceneRuntime::Controls(featureShortName))
		return true;
	if (!isCurrentlyApplied)
		return false;

	for (const auto& [type, vec] : entries) {
		for (const auto& entry : vec) {
			if (!entry.paused && entry.featureShortName == featureShortName)
				return true;
		}
	}
	return false;
}

bool SceneSettingsManager::IsFeaturePaused(const std::string& featureShortName) const
{
	auto it = featurePauseStates.find(featureShortName);
	return it != featurePauseStates.end() && it->second;
}

void SceneSettingsManager::SetFeaturePaused(const std::string& featureShortName, bool paused)
{
	BaseSettingsScope scope;
	featurePauseStates[featureShortName] = paused;
	scope.Finish();
}

// --- Apply / Revert ---

void SceneSettingsManager::SaveExteriorSettings(SceneType type)
{
	// Collect which keys per feature need saving (only the keys we'll override)
	std::map<std::string, std::set<std::string>> keysToSave;
	for (const auto& entry : GetEntries(type)) {
		if (IsEntryActive(entry))
			keysToSave[entry.featureShortName].insert(entry.settingKey);
	}

	// Save only the specific keys we'll override, not the entire settings blob
	for (const auto& [shortName, keys] : keysToSave) {
		auto* feature = Feature::FindFeatureByShortName(shortName);
		if (!feature || !feature->loaded)
			continue;

		json fullSettings;
		feature->SaveBaseSettings(fullSettings);

		// Merge into existing saved settings (don't overwrite keys saved by other scene types)
		json& partial = savedExteriorSettings[shortName];
		if (!partial.is_object())
			partial = json::object();

		for (const auto& key : keys) {
			if (fullSettings.contains(key) && !partial.contains(key))
				partial[key] = fullSettings[key];
		}
	}
}

void SceneSettingsManager::ApplySettings(SceneType type)
{
	std::map<std::string, json> candidate;
	for (const auto source : ApplicationOrder()) {
		for (const auto& entry : GetEntries(type)) {
			if (entry.source != source || !IsEntryActive(entry))
				continue;
			auto* feature = Feature::FindFeatureByShortName(entry.featureShortName);
			if (!feature || !feature->loaded)
				continue;
			auto [it, inserted] = candidate.try_emplace(entry.featureShortName);
			auto& settings = it->second;
			if (inserted)
				feature->SaveSettings(settings);
			if (!settings.contains(entry.settingKey))
				continue;
			const json patch{ { entry.settingKey, entry.value } };
			if (entry.source == EntrySource::LookPack) {
				auto appearance = Presets::SelectDomain(entry.featureShortName, settings, Presets::SettingsDomain::Appearance);
				Presets::Overlay(appearance, patch);
				settings = Presets::ReplaceAppearance(entry.featureShortName, settings, appearance);
			} else if (lookPersistence && entry.source == EntrySource::User) {
				Presets::Overlay(settings, patch);
			} else
				settings[entry.settingKey] = entry.value;
		}
	}
	ApplyFeatureSettings(candidate);
}

void SceneSettingsManager::RevertToExteriorSettings()
{
	std::map<std::string, json> candidate;
	for (const auto& [shortName, savedKeys] : savedExteriorSettings) {
		auto* feature = Feature::FindFeatureByShortName(shortName);
		if (!feature || !feature->loaded)
			continue;
		auto& current = candidate[shortName];
		feature->SaveSettings(current);
		for (const auto& [key, value] : savedKeys.items()) current[key] = value;
	}
	ApplyFeatureSettings(candidate);
	savedExteriorSettings.clear();
}

// --- Persistence ---

void SceneSettingsManager::SaveUserSettings(SceneType type)
{
	if (lookPersistence) {
		if (globals::menu)
			globals::menu->RequestSettingsDirtyCheck();
		return;
	}
	auto path = GetSettingsFilePath(type);
	Util::FileHelpers::EnsureDirectoryExists(path.parent_path());

	auto& vec = GetEntries(type);
	json data = json::array();
	for (const auto& entry : vec) {
		if (entry.source != EntrySource::User)
			continue;

		json item;
		item["feature"] = entry.featureShortName;
		item["setting"] = entry.settingKey;
		item["value"] = entry.value;
		item["paused"] = entry.paused;
		data.push_back(std::move(item));
	}

	auto typeName = GetSceneTypeName(type);
	try {
		std::string errorMessage;
		if (Util::FileHelpers::WriteTextFileAtomic(path, data.dump(2), errorMessage))
			logger::info("[SceneSettings] Saved {} {} user settings", data.size(), typeName);
		else
			logger::error("[SceneSettings] Failed to save {} settings: {}", typeName, errorMessage);
	} catch (const std::exception& e) {
		logger::error("[SceneSettings] Failed to save {} settings: {}", typeName, e.what());
	}
}

void SceneSettingsManager::LoadUserSettings(SceneType type)
{
	if (lookPersistence)
		return;
	auto path = GetSettingsFilePath(type);
	auto typeName = GetSceneTypeName(type);

	std::error_code ec;
	if (!std::filesystem::exists(path, ec))
		return;

	try {
		std::ifstream file(path);
		if (!file.is_open())
			return;

		json data = json::parse(file);
		if (!data.is_array())
			return;

		auto& vec = GetEntriesMut(type);
		for (const auto& item : data) {
			if (!item.contains("feature") || !item.contains("setting") || !item.contains("value"))
				continue;

			SettingEntry entry;
			entry.featureShortName = item["feature"].get<std::string>();
			entry.settingKey = item["setting"].get<std::string>();
			entry.value = item["value"];
			entry.paused = item.value("paused", false);
			entry.source = EntrySource::User;

			if (!Feature::FindFeatureByShortName(entry.featureShortName))
				continue;

			if (!HasEntryFromSource(type, entry.featureShortName, entry.settingKey, EntrySource::User))
				vec.push_back(std::move(entry));
		}

		logger::info("[SceneSettings] Loaded {} {} user settings", data.size(), typeName);
	} catch (const std::exception& e) {
		logger::error("[SceneSettings] Failed to load {} settings: {}", typeName, e.what());
	}
}

void SceneSettingsManager::DiscoverOverwrites(SceneType type)
{
	auto overwritesPath = GetOverwritesPath(type);
	auto typeName = GetSceneTypeName(type);

	logger::info("[SceneSettings] Discovering {} overwrites in: {}", typeName, overwritesPath.string());

	std::error_code ec;
	if (!std::filesystem::exists(overwritesPath, ec)) {
		logger::info("[SceneSettings] Overwrites directory does not exist: {}", overwritesPath.string());
		return;
	}

	auto& vec = GetEntriesMut(type);
	int filesFound = 0;
	int overwritesLoaded = 0;
	for (const auto& dirEntry : std::filesystem::directory_iterator(overwritesPath, ec)) {
		if (ec) {
			logger::error("[SceneSettings] Error iterating {} overwrites directory: {}", typeName, ec.message());
			break;
		}
		if (!dirEntry.is_regular_file() || dirEntry.path().extension() != ".json")
			continue;

		auto filename = dirEntry.path().filename().string();
		filesFound++;

		try {
			if (dirEntry.file_size() > MAX_OVERWRITE_FILE_SIZE) {
				logger::warn("[SceneSettings] Skipping overwrite '{}': file too large", filename);
				continue;
			}

			std::ifstream file(dirEntry.path());
			if (!file.is_open()) {
				logger::warn("[SceneSettings] Skipping overwrite '{}': could not open file", filename);
				continue;
			}

			json data = json::parse(file);

			// Resolve feature name: explicit _feature field, or infer from filename (ModName_FeatureName.json)
			std::string featureShortName = data.value("_feature", "");
			if (featureShortName.empty()) {
				auto stem = dirEntry.path().stem().string();
				auto lastUnderscore = stem.rfind('_');
				if (lastUnderscore != std::string::npos) {
					auto candidate = stem.substr(lastUnderscore + 1);
					if (Feature::FindFeatureByShortName(candidate)) {
						featureShortName = candidate;
						logger::info("[SceneSettings] Inferred feature '{}' from filename '{}'", featureShortName, filename);
					}
				}
			}

			if (featureShortName.empty()) {
				logger::warn("[SceneSettings] Skipping overwrite '{}': no _feature field and could not infer feature from filename", filename);
				continue;
			}

			if (!Feature::FindFeatureByShortName(featureShortName)) {
				logger::warn("[SceneSettings] Skipping overwrite '{}': feature '{}' not found", filename, featureShortName);
				continue;
			}

			// Count non-metadata settings ? must have exactly one
			int settingCount = 0;
			std::string settingKey;
			json settingValue;
			for (auto& [key, val] : data.items()) {
				if (!key.starts_with("_")) {
					settingCount++;
					if (settingCount == 1) {
						settingKey = key;
						settingValue = val;
					}
				}
			}

			if (settingCount != 1) {
				logger::warn("[SceneSettings] Skipping overwrite '{}': expected 1 setting, found {}", filename, settingCount);
				continue;
			}

			if (HasEntryFromSource(type, featureShortName, settingKey, EntrySource::Overwrite)) {
				logger::warn("[SceneSettings] Skipping overwrite '{}': duplicate overwrite for {}.{}", filename, featureShortName, settingKey);
				continue;
			}

			SettingEntry entry;
			entry.featureShortName = featureShortName;
			entry.settingKey = settingKey;
			entry.value = settingValue;
			entry.source = EntrySource::Overwrite;
			entry.sourceFilename = filename;

			vec.push_back(std::move(entry));

			overwritesLoaded++;
			logger::info("[SceneSettings] Loaded {} overwrite: {} -> {}.{}", typeName, filename, featureShortName, settingKey);
		} catch (const std::exception& e) {
			logger::error("[SceneSettings] Failed to load {} overwrite '{}': {}", typeName, filename, e.what());
		}
	}

	logger::info("[SceneSettings] {} overwrite discovery complete. Found {} JSON files, loaded {} overwrites", typeName, filesFound, overwritesLoaded);
}

void SceneSettingsManager::LoadAll()
{
	DiscoverOverwrites(SceneType::InteriorOnly);
	LoadUserSettings(SceneType::InteriorOnly);
	// Future: add other scene types here
}
