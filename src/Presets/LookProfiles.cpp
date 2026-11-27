#include "LookProfiles.h"
#include "LookProfileState.h"
#include "LookSceneRuntime.h"

#include "Feature.h"
#include "Features/AdaptiveBrightness.h"
#include "Globals.h"
#include "Menu.h"
#include "Menu/PerformanceTuningRenderer.h"
#include "Menu/SettingsActions.h"
#include "PresetCompatibility.h"
#include "SceneSettingsManager.h"
#include "SettingsOverrideManager.h"
#include "State.h"
#include "Utils/FileSystem.h"
#include "WeatherManager.h"
#include "WeatherVariableRegistry.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace Presets
{
	namespace
	{
		Json CoreAppearance()
		{
			const auto* state = globals::state;
			return { { "Refraction Scale", state->refractionScale }, { "PBR Metal Reflection Scale", state->pbrMetalReflectionScale },
				{ "PBR Metal Highlight Scale", state->pbrMetalHighlightScale } };
		}

		void ApplyCoreAppearance(const Json& values)
		{
			auto* state = globals::state;
			state->refractionScale = std::clamp(values.at("Refraction Scale").get<float>(), 0.0f, 2.0f);
			state->pbrMetalReflectionScale = std::clamp(values.at("PBR Metal Reflection Scale").get<float>(), 0.0f, 2.0f);
			state->pbrMetalHighlightScale = std::clamp(values.at("PBR Metal Highlight Scale").get<float>(), 0.0f, 2.0f);
		}

		Json AvailableEdits(const Json& personal, const Json& schemas)
		{
			if (!personal.is_object())
				throw std::runtime_error("Invalid personal Visuals profile");
			const auto edits = personal.value("edits", Json::object());
			if (!edits.is_object())
				throw std::runtime_error("Invalid personal Visuals edits");
			Json result = Json::object();
			for (const auto& [feature, values] : edits.items()) {
				if (!values.is_object())
					throw std::runtime_error("Invalid personal edits for " + feature);
				if (!schemas.contains(feature) || schemas.at(feature).empty())
					continue;
				std::string error;
				if (!ValidatePatch(values, schemas.at(feature), error, "/" + feature))
					throw std::runtime_error(error);
				result[feature] = SelectDomain(feature, values, SettingsDomain::Appearance);
			}
			return result;
		}

		void RestoreProfileState(const SceneSettingsManager::LookState& scenes, const std::vector<LookPack>& packs, std::string& error)
		{
			try {
				SceneSettingsManager::GetSingleton()->RestoreLookState(scenes);
				LookSceneRuntime::Use(packs.empty() ? nullptr : &packs.front());
			} catch (const std::exception& failure) {
				error += "\nCould not restore profile ownership: " + std::string(failure.what());
			}
		}
	}

	LookProfiles& LookProfiles::Get()
	{
		static LookProfiles instance;
		return instance;
	}

	std::filesystem::path LookProfiles::RootPath() { return Util::PathHelpers::GetCommunityShaderPath() / "Presets"; }

	void LookProfiles::CaptureFactorySettings()
	{
		if (!factory.empty())
			return;
		factory["Advanced"] = CoreAppearance();
		for (auto* feature : Feature::GetFeatureList()) {
			if (!HasProfileSettings(feature->GetShortName()))
				continue;
			Json values;
			feature->SaveSettings(values);
			if (values.is_object())
				factory[feature->GetShortName()] = std::move(values);
		}
	}

	Json LookProfiles::Capture() const
	{
		Json values = { { "Advanced", CoreAppearance() } };
		for (auto* feature : Feature::GetFeatureList()) {
			if (!feature->loaded || !HasProfileSettings(feature->GetShortName()))
				continue;
			const auto name = feature->GetShortName();
			Json settings;
			feature->SaveBaseSettings(settings);
			if (settings.is_object())
				values[name] = std::move(settings);
		}
		return values;
	}

	Json LookProfiles::Schemas() const
	{
		auto schemas = Capture();
		for (auto* feature : Feature::GetFeatureList())
			if (feature->loaded && !HasProfileSettings(feature->GetShortName()))
				schemas[feature->GetShortName()] = Json::object();
		if (schemas.contains("AdaptiveBrightness")) {
			auto& ab = schemas["AdaptiveBrightness"];
			// Dynamic location lists need an entry shape even before the first authored location.
			ab["locationOverrides"] = Json::array({ Json{ { "key", "" }, { "name", "" }, { "type", "Location" },
				{ "cocCode", "" }, { "layered", false }, { "profile", ab.at("globalProfile") },
				{ "skyLighting", { { "MinShadowElevation", 0.f }, { "HorizonFadeHours", 0.f } } } } });
		}
		return schemas;
	}

	Json LookProfiles::Appearance(const Json& values) const
	{
		Json result = Json::object();
		for (const auto& [feature, settings] : values.items()) {
			auto part = SelectDomain(feature, settings, SettingsDomain::Appearance);
			if (!part.empty())
				result[feature] = std::move(part);
		}
		return result;
	}

	Json LookProfiles::BuildBase(const std::vector<LookPack>& packs) const
	{
		auto base = Appearance(installed);
		for (const auto& pack : packs) Overlay(base, pack.baseline);
		return base;
	}

	void LookProfiles::Load(const Json& installedDefaults, const Json& userSettings)
	{
		ready = false;
		loadError.clear();
		savedSelection = userSettings.value("Presets", Json::object());
		if (!savedSelection.is_object())
			savedSelection = Json::object();
		managed = savedSelection.contains("csxLookSchema");
		auto* scenes = SceneSettingsManager::GetSingleton();
		SceneSettingsManager::BaseSettingsScope scope;
		const auto previousScenes = scenes->CaptureLookState();
		const auto previous = Capture();
		try {
			auto defaults = installedDefaults;
			auto* overrides = SettingsOverrideManager::GetSingleton();
			overrides->ApplyGlobalOverrides(defaults);
			installed = factory;
			if (defaults.contains("Advanced"))
				Overlay(installed["Advanced"], SelectDomain("Advanced", defaults["Advanced"], SettingsDomain::Appearance));
			for (auto* feature : Feature::GetFeatureList()) {
				const auto name = feature->GetShortName();
				if (!feature->loaded || !installed.contains(name))
					continue;
				if (defaults.contains(feature->GetName()))
					Overlay(installed[name], defaults[feature->GetName()]);
				overrides->ApplyOverrides(name, installed[name]);
			}
			std::vector<LookPack> packs;
			Json interior = Json::object();
			if (managed) {
				if (savedSelection["csxLookSchema"] != 1)
					throw std::runtime_error("Unsupported saved CSX Look schema");
				const auto schemas = Schemas();
				const auto ids = savedSelection.value("baselinePackIds", Json::array());
				if (!ids.is_array() || ids.size() > 1)
					throw std::runtime_error("CSX selects one Look at a time");
				for (const auto& id : ids) {
					if (!id.is_string())
						throw std::runtime_error("Invalid saved Look identifier");
					auto pack = ReadLookPack(RootPath(), id.get<std::string>(), schemas);
					if (!pack.IsCompatible())
						throw std::runtime_error(pack.id + ": " + pack.CompatibilityDetails());
					packs.push_back(std::move(pack));
				}
				const auto activeId = savedSelection.value("activePackId", std::string{});
				if (activeId != (packs.empty() ? std::string{} : packs.front().id))
					throw std::runtime_error("Interior selection must belong to the selected Look");
				if (!packs.empty())
					interior = packs.front().interior;
				// Parse every saved entry before changing feature values or pack ownership.
				scenes->LoadLookEdits(savedSelection.value("csxInteriorEdits", Json::array()), schemas);
			}
			auto newBase = BuildBase(packs);
			if (managed) {
				auto candidate = previous;
				for (auto* feature : Feature::GetFeatureList()) {
					const auto name = feature->GetShortName();
					if (!feature->loaded || !candidate.contains(name))
						continue;
					if (newBase.contains(name))
						candidate[name] = ReplaceAppearance(name, candidate[name], newBase[name]);
					if (const auto user = userSettings.find(feature->GetName()); user != userSettings.end() && user->is_object())
						Overlay(candidate[name], *user);
				}
				candidate["Advanced"] = newBase.at("Advanced");
				if (userSettings.contains("Advanced"))
					Overlay(candidate["Advanced"], SelectDomain("Advanced", userSettings["Advanced"], SettingsDomain::Appearance));
				const auto id = packs.empty() ? std::string{} : packs.front().id;
				const auto profiles = savedSelection.value("csxProfiles", Json::object());
				if (!profiles.is_object())
					throw std::runtime_error("Invalid personal Visuals profiles");
				if (profiles.contains(id)) {
					const auto& personal = profiles.at(id);
					const auto edits = AvailableEdits(personal, Schemas());
					auto appearance = Appearance(candidate);
					Overlay(appearance, Appearance(edits));
					for (const auto& [feature, values] : appearance.items())
						candidate[feature] = ReplaceAppearance(feature, candidate[feature], values);
					scenes->LoadLookEdits(ReplaceInteriorAppearance(scenes->SerializeLookEdits(), personal.value("interior", Json::array())), Schemas());
				}
				std::string error;
				if (!Apply(candidate, error))
					throw std::runtime_error(error);
			}
			scenes->UseLookPersistence(managed);
			scenes->SetLookLayer(interior, packs.empty() ? "" : packs.front().id);
			LookSceneRuntime::Use(packs.empty() ? nullptr : &packs.front());
			scope.Finish();
			selected = std::move(packs);
			activeBase = std::move(newBase);
		} catch (const std::exception& error) {
			loadError = error.what();
			RestoreProfileState(previousScenes, selected, loadError);
			std::string rollback;
			if (!Apply(previous, rollback))
				loadError += "\n" + rollback;
			try {
				scope.Finish();
			} catch (const std::exception& failure) {
				loadError += "\n" + std::string(failure.what());
			}
			logger::error("Saved Look could not be restored; SettingsUser is protected until another Look is selected or cleared: {}", loadError);
		}
		ready = true;
	}

	void LookProfiles::SaveSelection(Json& settings) const
	{
		if (!managed)
			return;
		auto state = savedSelection;
		if (loadError.empty()) {
			state["csxInteriorEdits"] = SceneSettingsManager::GetSingleton()->SerializeLookEdits();
			const auto id = selected.empty() ? std::string{} : selected.front().id;
			StoreProfileEdits(state, id, Appearance(Capture()), activeBase, state["csxInteriorEdits"]);
			state["baselinePackIds"] = Json::array();
			state["activePackId"] = "";
			for (const auto& pack : selected) {
				state["baselinePackIds"].push_back(pack.id);
				state["activePackId"] = pack.id;
			}
		}
		if (loadError.empty())
			state["csxLookSchema"] = 1;
		settings["Presets"] = std::move(state);
	}

	bool LookProfiles::PrepareSave(Json& settings, std::string& error, Feature* onlyFeature) const
	{
		if (!managed)
			return true;
		if (!loadError.empty()) {
			error = "The saved Look is unavailable. Select or clear it before saving: " + loadError;
			return false;
		}
		const auto previousState = settings.value("Presets", Json::object());
		if (onlyFeature && previousState.value("baselinePackIds", Json::array()) != (selected.empty() ? Json::array() : Json::array({ ActiveId() }))) {
			error = "Save CSX settings first to keep the selected Visuals profile.";
			return false;
		}
		for (auto* feature : Feature::GetFeatureList()) {
			const auto name = feature->GetShortName();
			const auto key = feature->GetName();
			if ((onlyFeature && feature != onlyFeature) || !feature->loaded || !activeBase.contains(name) || !settings.contains(key))
				continue;
			auto& values = settings[key];
			values = PersonalSettings(name, values, activeBase[name]);
		}
		if (!onlyFeature && settings.contains("Advanced")) {
			auto& values = settings["Advanced"];
			values = PersonalSettings("Advanced", values, activeBase.at("Advanced"));
		}
		if (!onlyFeature)
			SaveSelection(settings);
		else {
			Json current;
			SaveSelection(current);
			CopyFeatureProfileState(settings, current, onlyFeature->GetShortName());
		}
		return true;
	}

	bool LookProfiles::Apply(const Json& candidate, std::string& error)
	{
		const auto before = Capture();
		std::vector<Feature*> touched;
		try {
			for (auto* feature : Feature::GetFeatureList()) {
				const auto name = feature->GetShortName();
				if (!feature->loaded || !candidate.contains(name) || (before.contains(name) && before[name] == candidate[name]))
					continue;
				touched.push_back(feature);
				auto settings = candidate[name];
				feature->LoadSettings(settings);
				WeatherVariables::GlobalWeatherRegistry::GetSingleton()->CaptureFeatureUserSettings(name);
				PerformanceTuningRenderer::NotifyFeatureSettingsChanged(feature);
			}
			if (candidate.contains("Advanced"))
				ApplyCoreAppearance(candidate["Advanced"]);
			WeatherManager::GetSingleton()->NotifyUserSettingsChanged();
			return true;
		} catch (const std::exception& failure) {
			error = failure.what();
			for (auto it = touched.rbegin(); it != touched.rend(); ++it) {
				auto* feature = *it;
				try {
					auto old = before.at(feature->GetShortName());
					feature->LoadSettings(old);
					WeatherVariables::GlobalWeatherRegistry::GetSingleton()->CaptureFeatureUserSettings(feature->GetShortName());
					PerformanceTuningRenderer::NotifyFeatureSettingsChanged(feature);
				} catch (const std::exception& rollback) {
					error += "\nRollback failed for " + feature->GetShortName() + ": " + rollback.what();
				}
			}
			ApplyCoreAppearance(before.at("Advanced"));
			WeatherManager::GetSingleton()->NotifyUserSettingsChanged();
			return false;
		}
	}

	const char* LookProfiles::MutationBlockReason() const
	{
		if (!ready)
			return "Look profiles are not ready.";
		if (!PresetCompatibility::GetPublished().ShouldApply())
			return "The existing SettingsUser.json was rejected; resolve it before changing profiles.";
		if (globals::features::adaptiveBrightness.locationOverrideEditProfile)
			return "Apply or cancel your place edits before changing or exporting profiles.";
		return MenuUI::GetGlobalSettingsActionBlockReason(MenuUI::GlobalSettingsAction::Load);
	}

	bool LookProfiles::Change(const std::vector<LookPack>& packs, bool resetEdits, std::string& error)
	{
		if (const auto* reason = MutationBlockReason()) {
			error = reason;
			return false;
		}
		if (!globals::menu->PrepareSettingsMutation()) {
			error = "Could not preserve the saved-settings baseline.";
			return false;
		}
		auto nextSelection = packs;
		SceneSettingsManager::BaseSettingsScope scope;
		const auto current = Capture();
		auto* scenes = SceneSettingsManager::GetSingleton();
		const auto previousScenes = scenes->CaptureLookState();
		try {
			auto nextState = savedSelection;
			auto& profiles = nextState["csxProfiles"];
			if (!profiles.is_object())
				profiles = Json::object();
			const auto oldId = selected.empty() ? std::string{} : selected.front().id;
			const auto newId = packs.empty() ? std::string{} : packs.front().id;
			if (loadError.empty())
				StoreProfileEdits(nextState, oldId, Appearance(current), activeBase, scenes->SerializeLookEdits());
			if (resetEdits)
				profiles.erase(newId);
			const auto personal = profiles.value(newId, Json::object());
			auto base = BuildBase(packs);
			auto look = base;
			const auto edits = AvailableEdits(personal, Schemas());
			Overlay(look, Appearance(edits));
			auto candidate = current;
			for (const auto& [feature, appearance] : look.items())
				if (candidate.contains(feature))
					candidate[feature] = ReplaceAppearance(feature, candidate[feature], appearance);
			if (!Apply(candidate, error))
				throw std::runtime_error(error);
			scenes->LoadLookEdits(ReplaceInteriorAppearance(scenes->SerializeLookEdits(), personal.value("interior", Json::array())), Schemas());
			scenes->UseLookPersistence(true);
			Json interior = Json::object();
			for (const auto& pack : packs) Overlay(interior, pack.interior);
			scenes->SetLookLayer(interior, packs.empty() ? "" : packs.back().id);
			LookSceneRuntime::Use(packs.empty() ? nullptr : &packs.front());
			scope.Finish();
			savedSelection = std::move(nextState);
			selected = std::move(nextSelection);
			managed = true;
			activeBase = std::move(base);
			loadError.clear();
			globals::menu->RequestSettingsDirtyCheck();
			return true;
		} catch (const std::exception& failure) {
			error = failure.what();
			const auto originalError = error;
			RestoreProfileState(previousScenes, selected, error);
			if (error != originalError) {
				loadError = error;
				managed = true;
			}
			std::string rollback;
			if (!Apply(current, rollback)) {
				error += "\n" + rollback;
				loadError = error;
				managed = true;
			}
			try {
				scope.Finish();
			} catch (const std::exception& failure) {
				error += "\n" + std::string(failure.what());
				loadError = error;
				managed = true;
			}
			return false;
		}
	}

	bool LookProfiles::Select(const std::string& id, std::string& error)
	{
		try {
			auto pack = ReadLookPack(RootPath(), id, Schemas());
			if (!pack.IsCompatible()) {
				error = pack.CompatibilityDetails();
				return false;
			}
			return Change({ pack }, false, error);
		} catch (const std::exception& failure) {
			error = failure.what();
			return false;
		}
	}

	bool LookProfiles::Clear(std::string& error)
	{
		try {
			return Change({}, false, error);
		} catch (const std::exception& failure) {
			error = failure.what();
			return false;
		}
	}

	void LookProfiles::OnDefaultsRestored()
	{
		auto base = BuildBase({});
		auto* scenes = SceneSettingsManager::GetSingleton();
		const auto previous = scenes->CaptureLookState();
		SceneSettingsManager::BaseSettingsScope scope;
		try {
			scenes->SetLookLayer(Json::object(), "");
			scenes->LoadLookEdits(Json::array(), Schemas());
			LookSceneRuntime::Use(nullptr);
			scope.Finish();
		} catch (const std::exception& failure) {
			std::string error = failure.what();
			RestoreProfileState(previous, selected, error);
			throw std::runtime_error(error);
		}
		selected.clear();
		loadError.clear();
		activeBase = std::move(base);
	}

	std::string LookProfiles::ActiveName() const
	{
		if (selected.empty())
			return "Installed settings";
		return selected.back().manifest.value("name", selected.back().id);
	}

	Json LookProfiles::Inspect()
	{
		Json result = { { "active", ActiveName() }, { "managed", managed }, { "loadError", loadError }, { "packs", Json::array() } };
		try {
			const auto schemas = Schemas();
			const auto root = RootPath();
			result["supportedLayers"] = { "Baseline", "InteriorOnly", "TimeOfDay", "Weather", "Locations" };
			result["sceneError"] = LookSceneRuntime::Error();
			result["postProcessingAvailable"] = IsLookFeatureAvailable("PostProcessing", schemas);
			result["appearanceFeatures"] = Json::array();
			const auto availableAppearance = Appearance(schemas);
			for (const auto& [name, values] : availableAppearance.items()) {
				(void)values;
				result["appearanceFeatures"].push_back(name);
			}
			if (!std::filesystem::exists(root))
				return result;
			std::vector<std::string> ids;
			for (const auto& entry : std::filesystem::directory_iterator(root)) {
				if (entry.is_directory() && IsPackId(Util::PathToUtf8(entry.path().filename())))
					ids.push_back(Util::PathToUtf8(entry.path().filename()));
				if (ids.size() > 1024)
					throw std::runtime_error("Too many Look folders; limit the presets directory to 1024 packs");
			}
			std::sort(ids.begin(), ids.end());
			for (const auto& id : ids) {
				const auto pack = ReadLookPack(root, id, schemas);
				const auto name = pack.manifest.contains("name") && pack.manifest["name"].is_string() ? pack.manifest["name"].get<std::string>() : id;
				result["packs"].push_back({ { "id", id }, { "name", name }, { "manifest", pack.manifest },
					{ "compatible", pack.IsCompatible() }, { "status", pack.CompatibilityStatus() }, { "summary", pack.CompatibilitySummary() },
					{ "errors", pack.errors }, { "unavailable", pack.unavailable }, { "notices", pack.notices } });
			}
		} catch (const std::exception& error) {
			result["discoveryError"] = error.what();
		}
		return result;
	}

	bool LookProfiles::Export(const std::string& id, std::string& error, const std::filesystem::path& folder)
	{
		if (const auto* reason = MutationBlockReason()) {
			error = reason;
			return false;
		}
		if (!loadError.empty()) {
			error = loadError;
			return false;
		}
		try {
			LookPack pack;
			if (!selected.empty())
				pack = selected.front();
			pack.id = id;
			if (pack.manifest.empty())
				pack.manifest = { { "version", "1.0.0" }, { "description", "CSX appearance preset" } };
			if (selected.size() == 1) {
				for (const auto* key : { "csVersion", "requiredPlugins" })
					if (selected.front().manifest.contains(key))
						pack.manifest[key] = selected.front().manifest[key];
			}
			pack.baseline = Appearance(Capture());
			pack.interior = SceneSettingsManager::GetSingleton()->ExportLookLayer();
			return WriteLookPack(folder.empty() ? RootPath() : folder, pack, Schemas(), error);
		} catch (const std::exception& failure) {
			error = failure.what();
			return false;
		}
	}

	bool LookProfiles::Import(const std::filesystem::path& source, const std::string& id, std::string& error)
	{
		if (const auto* reason = MutationBlockReason()) {
			error = reason;
			return false;
		}
		try {
			LookPack pack;
			bool packSource = false;
			if (std::filesystem::is_directory(source)) {
				packSource = true;
				pack = ReadLookPack(source.parent_path(), Util::PathToUtf8(source.filename()), Schemas());
			} else {
				const auto folder = source.parent_path();
				if (source.filename() == "preset.json" || source.stem() == folder.filename()) {
					packSource = true;
					pack = ReadLookPack(folder.parent_path(), Util::PathToUtf8(folder.filename()), Schemas());
				} else {
					Json converted;
					SceneSettingsManager::BaseSettingsScope scope;
					if (!selected.empty())
						pack = selected.front();
					pack.baseline = Appearance(Capture());
					pack.interior = SceneSettingsManager::GetSingleton()->ExportLookLayer();
					if (!globals::features::adaptiveBrightness.ReadLegacyVisuals(source, converted, error))
						return false;
					pack.baseline["AdaptiveBrightness"] = SelectDomain("AdaptiveBrightness", converted, SettingsDomain::Appearance);
					pack.manifest["description"] = "Imported Visuals profile";
					scope.Finish();
				}
			}
			if (!pack.IsCompatible()) {
				error = pack.CompatibilityDetails();
				return false;
			}
			const auto installedPath = RootPath() / std::filesystem::path(std::u8string(id.begin(), id.end()));
			if (packSource && pack.id == id && !pack.sourceRoot.empty() && std::filesystem::exists(installedPath) && std::filesystem::equivalent(pack.sourceRoot, installedPath))
				return Select(id, error);
			pack.id = id;
			if (!WriteLookPack(RootPath(), pack, Schemas(), error))
				return false;
			return Select(id, error);
		} catch (const std::exception& failure) {
			error = failure.what();
			return false;
		}
	}

	bool LookProfiles::Restore(std::string& error)
	{
		try {
			return Change(selected, true, error);
		} catch (const std::exception& failure) {
			error = failure.what();
			return false;
		}
	}
}
