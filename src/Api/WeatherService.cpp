#include "Api/WeatherService.h"

#include "Api/ServiceFoundation.h"
#include "Api/ServiceRegistry.h"
#include "Api/WeatherServicePolicy.h"
#include "BuildProvenance.h"
#include "CSEditor/EditorWindow.h"
#include "Feature.h"
#include "Globals.h"
#include "State.h"
#include "Utils/FileSystem.h"
#include "Utils/Form.h"
#include "WeatherManager.h"
#include "WeatherVariableRegistry.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{
	using CSX::WeatherAPI::FeatureDescriptor001;
	using CSX::WeatherAPI::MutationReceipt001;
	using CSX::WeatherAPI::MutationRequest001;
	using CSX::WeatherAPI::OverrideSnapshot001;
	using CSX::WeatherAPI::Preflight001;
	using CSX::WeatherAPI::Snapshot001;
	using CSX::WeatherAPI::Status;
	using CSX::WeatherAPI::VariableDescriptor001;
	using CSX::WeatherAPI::WeatherDescriptor001;
	using json = nlohmann::json;

	constexpr auto kPreflightLifetime = std::chrono::seconds(30);

	struct OwnedMutation
	{
		CSX::WeatherAPI::MutationAction action = CSX::WeatherAPI::MutationAction::kSetWeather;
		std::uint64_t expectedStateRevision = 0;
		std::uint64_t flags = CSX::WeatherAPI::kMutationNone;
		bool boolValue = false;
		bool accelerate = false;
		std::string weatherKey;
		std::string featureName;
		std::string valueJson;

		bool operator==(const OwnedMutation&) const = default;
	};

	struct PendingPreflight
	{
		OwnedMutation mutation;
		std::chrono::steady_clock::time_point expiresAt;
	};

	struct ResponseStrings
	{
		std::string currentWeatherKey;
		std::string lastWeatherKey;
		std::string defaultWeatherKey;
		std::string overrideWeatherKey;
		std::string lockedWeatherKey;
		std::string token;
		std::string reasonCode;
		std::string message;
	};

	struct WeatherStrings
	{
		std::string weatherKey;
		std::string editorId;
		std::string displayName;
		std::string pluginName;
	};

	struct FeatureStrings
	{
		std::string shortName;
		std::string displayName;
	};

	struct VariableStrings
	{
		std::string featureName;
		std::string variableName;
		std::string displayName;
		std::string tooltip;
		std::string currentValueJson;
		std::string userValueJson;
	};

	struct OverrideStrings
	{
		std::string weatherKey;
		std::string featureName;
		std::string effectiveValueJson;
		std::string persistedValueJson;
	};

	RE::Sky* GetSky()
	{
		return globals::game::sky ? globals::game::sky : RE::Sky::GetSingleton();
	}

	std::vector<RE::TESWeather*> GetWeatherCatalog()
	{
		std::vector<RE::TESWeather*> result;
		if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
			for (auto* weather : dataHandler->GetFormArray<RE::TESWeather>()) {
				if (weather)
					result.push_back(weather);
			}
		}
		std::ranges::sort(result, {}, [](RE::TESWeather* a_weather) {
			return Util::GetFormFileKey(a_weather);
		});
		return result;
	}

	RE::TESWeather* FindWeather(std::string_view a_key)
	{
		if (a_key.empty())
			return nullptr;
		for (auto* weather : GetWeatherCatalog()) {
			if (Util::GetFormFileKey(weather) == a_key ||
				Util::GetFormEditorID(weather) == a_key) {
				return weather;
			}
		}
		return nullptr;
	}

	Feature* FindWeatherFeature(std::string_view a_name)
	{
		if (a_name.empty())
			return nullptr;
		auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();
		for (auto* feature : Feature::GetFeatureList()) {
			if (feature && feature->loaded && feature->GetShortName() == a_name &&
				registry->HasWeatherSupport(feature->GetShortName())) {
				return feature;
			}
		}
		return nullptr;
	}

	std::vector<Feature*> GetWeatherFeatures()
	{
		std::vector<Feature*> result;
		auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();
		for (auto* feature : Feature::GetFeatureList()) {
			if (feature && feature->loaded && registry->HasWeatherSupport(feature->GetShortName()))
				result.push_back(feature);
		}
		std::ranges::sort(result, {}, [](Feature* a_feature) { return a_feature->GetShortName(); });
		return result;
	}

	std::filesystem::path GetWeatherPath(RE::TESWeather* a_weather)
	{
		return Util::PathHelpers::GetCommunityShaderPath() / "Weathers" /
		       (std::string(WeatherManager::GetWeatherKey(a_weather)) + ".json");
	}

	CSX::WeatherAPI::ValueKind GetValueKind(const json& a_value)
	{
		if (a_value.is_boolean())
			return CSX::WeatherAPI::ValueKind::kBoolean;
		if (a_value.is_number_integer() || a_value.is_number_unsigned())
			return CSX::WeatherAPI::ValueKind::kInteger;
		if (a_value.is_number_float())
			return CSX::WeatherAPI::ValueKind::kNumber;
		if (a_value.is_string())
			return CSX::WeatherAPI::ValueKind::kString;
		if (a_value.is_array())
			return CSX::WeatherAPI::ValueKind::kArray;
		if (a_value.is_object())
			return CSX::WeatherAPI::ValueKind::kObject;
		return CSX::WeatherAPI::ValueKind::kUnknown;
	}

	bool ReadWeatherAttachment(RE::TESWeather* a_weather, json& a_root, std::string& a_error)
	{
		const auto result = Util::FileHelpers::ReadJsonFile(GetWeatherPath(a_weather), a_root, a_error);
		if (result == Util::FileHelpers::JsonFileReadResult::NotFound) {
			a_root = json::object();
			return true;
		}
		if (result != Util::FileHelpers::JsonFileReadResult::Success)
			return false;
		if (!a_root.is_object()) {
			a_error = "weather attachment root is not an object";
			return false;
		}
		return true;
	}

	std::map<std::string, json> ExtractFeatureSettings(const json& a_root)
	{
		std::map<std::string, json> result;
		const auto found = a_root.find("featureSettings");
		if (found == a_root.end() || !found->is_object())
			return result;
		for (const auto& [name, value] : found->items())
			result.emplace(name, value);
		return result;
	}

	bool ReadPersistedOverride(
		RE::TESWeather* a_weather,
		std::string_view a_featureName,
		json& a_value,
		bool& a_present,
		std::string& a_error)
	{
		json root;
		if (!ReadWeatherAttachment(a_weather, root, a_error))
			return false;
		a_present = false;
		a_value = json();
		const auto settings = root.find("featureSettings");
		if (settings == root.end() || !settings->is_object())
			return true;
		const auto feature = settings->find(std::string(a_featureName));
		if (feature != settings->end()) {
			a_present = true;
			a_value = *feature;
		}
		return true;
	}

	bool ValidateAndNormalizeOverride(Feature* a_feature, const std::string& a_text, json& a_value, std::string& a_error)
	{
		try {
			a_value = json::parse(a_text);
		} catch (const std::exception& e) {
			a_error = std::format("valueJson is invalid JSON: {}", e.what());
			return false;
		}
		if (!a_value.is_object()) {
			a_error = "valueJson must encode an object";
			return false;
		}
		const auto enabled = a_value.find("__enabled");
		if (enabled == a_value.end() || !enabled->is_boolean()) {
			a_error = "valueJson.__enabled is required and must be boolean";
			return false;
		}
		try {
			a_feature->NormalizeWeatherSettings(a_value);
		} catch (const std::exception& e) {
			a_error = std::format("feature normalization failed: {}", e.what());
			return false;
		}
		auto* featureRegistry = WeatherVariables::GlobalWeatherRegistry::GetSingleton()->GetFeatureRegistry(a_feature->GetShortName());
		if (!featureRegistry) {
			a_error = "feature has no registered weather variables";
			return false;
		}
		std::map<std::string, const WeatherVariables::IWeatherVariable*> variables;
		for (const auto& variable : featureRegistry->GetVariables())
			variables.emplace(variable->GetName(), variable.get());
		for (const auto& [name, value] : a_value.items()) {
			if (name == "__enabled")
				continue;
			const auto found = variables.find(name);
			if (found == variables.end()) {
				a_error = std::format("unknown weather variable '{}.{}'", a_feature->GetShortName(), name);
				return false;
			}
			if (!found->second->IsValidJsonValue(value)) {
				a_error = std::format("invalid value for weather variable '{}.{}'", a_feature->GetShortName(), name);
				return false;
			}
		}
		return true;
	}

	class WeatherService
	{
	public:
		WeatherService() : ownerThread(std::this_thread::get_id()) {}

		bool IsOwnerThread() const { return std::this_thread::get_id() == ownerThread; }

		Status GetSnapshot(Snapshot001& a_output)
		{
			if (!IsOwnerThread())
				return Status::kWrongThread;
			RefreshRevision();
			auto* sky = GetSky();
			auto* state = globals::state;
			auto current = WeatherManager::GetSingleton()->GetCurrentWeathers();
			auto* editor = EditorWindow::GetSingleton();
			thread_local ResponseStrings strings;
			strings.currentWeatherKey = current.currentWeather ? WeatherManager::GetWeatherKey(current.currentWeather) : "";
			strings.lastWeatherKey = current.lastWeather ? WeatherManager::GetWeatherKey(current.lastWeather) : "";
			strings.defaultWeatherKey = sky && sky->defaultWeather ? WeatherManager::GetWeatherKey(sky->defaultWeather) : "";
			strings.overrideWeatherKey = sky && sky->overrideWeather ? WeatherManager::GetWeatherKey(sky->overrideWeather) : "";
			auto* locked = editor->GetLockedWeather();
			strings.lockedWeatherKey = locked ? WeatherManager::GetWeatherKey(locked) : "";
			a_output = {
				.structSize = sizeof(Snapshot001),
				.available = RE::TESDataHandler::GetSingleton() && state ? 1u : 0u,
				.skyAvailable = sky ? 1u : 0u,
				.stateRevision = revision,
				.capabilities = kCapabilities,
				.transitionActive = current.lastWeather && current.lerpFactor < 1.0f ? 1u : 0u,
				.transitionFactor = current.lerpFactor,
				.weatherLocked = editor->IsWeatherLocked() ? 1u : 0u,
				.lockHooksInstalled = EditorWindow::AreWeatherLockHooksInstalled() ? 1u : 0u,
				.persistentMutationBlocked = state && state->IsPersistentMutationBlocked() ? 1u : 0u,
				.saveLoadSafeModeActive = state && state->IsSaveLoadSafeModeActive() ? 1u : 0u,
				.weatherCount = static_cast<std::uint32_t>(GetWeatherCatalog().size()),
				.weatherFeatureCount = static_cast<std::uint32_t>(GetWeatherFeatures().size()),
				.currentWeatherKey = strings.currentWeatherKey.c_str(),
				.lastWeatherKey = strings.lastWeatherKey.c_str(),
				.defaultWeatherKey = strings.defaultWeatherKey.c_str(),
				.overrideWeatherKey = strings.overrideWeatherKey.c_str(),
				.lockedWeatherKey = strings.lockedWeatherKey.c_str(),
				.buildId = BuildProvenance::GetBuildId().data(),
			};
			return a_output.available ? Status::kSuccess : Status::kUnavailable;
		}

		std::uint32_t GetWeatherCount() const
		{
			return IsOwnerThread() ? static_cast<std::uint32_t>(GetWeatherCatalog().size()) : 0;
		}

		Status GetWeather(std::uint32_t a_index, WeatherDescriptor001& a_output) const
		{
			if (!IsOwnerThread())
				return Status::kWrongThread;
			const auto catalog = GetWeatherCatalog();
			if (a_index >= catalog.size())
				return Status::kWeatherNotFound;
			auto* weather = catalog[a_index];
			auto* sky = GetSky();
			auto* editor = EditorWindow::GetSingleton();
			thread_local WeatherStrings strings;
			strings.weatherKey = WeatherManager::GetWeatherKey(weather);
			strings.editorId = Util::GetFormEditorID(weather);
			const char* name = weather->GetName();
			strings.displayName = name && *name ? name : (!strings.editorId.empty() ? strings.editorId : strings.weatherKey);
			if (auto* file = weather->GetFile(0))
				strings.pluginName = file->GetFilename();
			else
				strings.pluginName.clear();
			a_output = {
				.structSize = sizeof(WeatherDescriptor001),
				.weatherKey = strings.weatherKey.c_str(),
				.editorId = strings.editorId.c_str(),
				.displayName = strings.displayName.c_str(),
				.pluginName = strings.pluginName.c_str(),
				.runtimeFormId = weather->GetFormID(),
				.localFormId = weather->GetLocalFormID(),
				.weatherFlags = weather->data.flags.underlying(),
				.isCurrent = sky && sky->currentWeather == weather ? 1u : 0u,
				.isLast = sky && sky->lastWeather == weather ? 1u : 0u,
				.isDefault = sky && sky->defaultWeather == weather ? 1u : 0u,
				.isOverride = sky && sky->overrideWeather == weather ? 1u : 0u,
				.isLocked = editor->GetLockedWeather() == weather ? 1u : 0u,
			};
			return Status::kSuccess;
		}

		std::uint32_t GetFeatureCount() const
		{
			return IsOwnerThread() ? static_cast<std::uint32_t>(GetWeatherFeatures().size()) : 0;
		}

		Status GetFeature(std::uint32_t a_index, FeatureDescriptor001& a_output) const
		{
			if (!IsOwnerThread())
				return Status::kWrongThread;
			const auto features = GetWeatherFeatures();
			if (a_index >= features.size())
				return Status::kFeatureNotFound;
			auto* feature = features[a_index];
			auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();
			auto* featureRegistry = registry->GetFeatureRegistry(feature->GetShortName());
			thread_local FeatureStrings strings;
			strings.shortName = feature->GetShortName();
			strings.displayName = feature->GetDisplayName();
			a_output = {
				.structSize = sizeof(FeatureDescriptor001),
				.shortName = strings.shortName.c_str(),
				.displayName = strings.displayName.c_str(),
				.loaded = feature->loaded ? 1u : 0u,
				.paused = registry->IsFeaturePaused(strings.shortName) ? 1u : 0u,
				.variableCount = featureRegistry ? static_cast<std::uint32_t>(featureRegistry->GetVariables().size()) : 0u,
				.activeOverrideCount = static_cast<std::uint32_t>(WeatherManager::GetSingleton()->GetActiveVariablesForFeature(strings.shortName).size()),
			};
			return Status::kSuccess;
		}

		std::uint32_t GetVariableCount(std::string_view a_featureName) const
		{
			if (!IsOwnerThread())
				return 0;
			auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton()->GetFeatureRegistry(std::string(a_featureName));
			return registry ? static_cast<std::uint32_t>(registry->GetVariables().size()) : 0;
		}

		Status GetVariable(std::string_view a_featureName, std::uint32_t a_index, VariableDescriptor001& a_output) const
		{
			if (!IsOwnerThread())
				return Status::kWrongThread;
			if (!FindWeatherFeature(a_featureName))
				return Status::kFeatureNotFound;
			auto* featureRegistry = WeatherVariables::GlobalWeatherRegistry::GetSingleton()->GetFeatureRegistry(std::string(a_featureName));
			if (!featureRegistry || a_index >= featureRegistry->GetVariables().size())
				return Status::kVariableNotFound;
			const auto& variable = featureRegistry->GetVariables()[a_index];
			json current = json::object();
			json user = json::object();
			variable->SaveToJson(current);
			variable->SaveUserSettingsToJson(user);
			const auto currentValue = current.find(variable->GetName());
			const auto userValue = user.find(variable->GetName());
			thread_local VariableStrings strings;
			strings.featureName = a_featureName;
			strings.variableName = variable->GetName();
			strings.displayName = variable->GetDisplayName();
			strings.tooltip = variable->GetTooltip();
			strings.currentValueJson = currentValue != current.end() ? currentValue->dump() : "null";
			strings.userValueJson = userValue != user.end() ? userValue->dump() : "null";
			a_output = {
				.structSize = sizeof(VariableDescriptor001),
				.featureName = strings.featureName.c_str(),
				.variableName = strings.variableName.c_str(),
				.displayName = strings.displayName.c_str(),
				.tooltip = strings.tooltip.c_str(),
				.valueKind = currentValue != current.end() ? GetValueKind(*currentValue) : CSX::WeatherAPI::ValueKind::kUnknown,
				.activeOverride = WeatherManager::GetSingleton()->IsVariableOverrideActive(strings.featureName, strings.variableName) ? 1u : 0u,
				.currentValueJson = strings.currentValueJson.c_str(),
				.userValueJson = strings.userValueJson.c_str(),
			};
			if (auto* floatVariable = dynamic_cast<WeatherVariables::FloatVariable*>(variable.get())) {
				a_output.hasNumericRange = 1;
				a_output.minimumValue = floatVariable->GetMin();
				a_output.maximumValue = floatVariable->GetMax();
			}
			return Status::kSuccess;
		}

		Status GetOverride(std::string_view a_weatherKey, std::string_view a_featureName, OverrideSnapshot001& a_output) const
		{
			if (!IsOwnerThread())
				return Status::kWrongThread;
			auto* weather = FindWeather(a_weatherKey);
			if (!weather)
				return Status::kWeatherNotFound;
			if (!FindWeatherFeature(a_featureName))
				return Status::kFeatureNotFound;
			std::map<std::string, json> effective;
			WeatherManager::GetSingleton()->GetAllSettingsForWeather(weather, effective);
			const auto effectiveIt = effective.find(std::string(a_featureName));
			json persisted;
			bool persistedPresent = false;
			std::string error;
			if (!ReadPersistedOverride(weather, a_featureName, persisted, persistedPresent, error))
				return Status::kPersistenceFailed;
			thread_local OverrideStrings strings;
			strings.weatherKey = WeatherManager::GetWeatherKey(weather);
			strings.featureName = a_featureName;
			strings.effectiveValueJson = effectiveIt != effective.end() ? effectiveIt->second.dump() : "null";
			strings.persistedValueJson = persistedPresent ? persisted.dump() : "null";
			a_output = {
				.structSize = sizeof(OverrideSnapshot001),
				.weatherKey = strings.weatherKey.c_str(),
				.featureName = strings.featureName.c_str(),
				.effectivePresent = effectiveIt != effective.end() ? 1u : 0u,
				.persistedPresent = persistedPresent ? 1u : 0u,
				.effectiveValueJson = strings.effectiveValueJson.c_str(),
				.persistedValueJson = strings.persistedValueJson.c_str(),
			};
			return Status::kSuccess;
		}

		Status Preflight(const MutationRequest001& a_request, Preflight001& a_output)
		{
			if (!IsOwnerThread())
				return Status::kWrongThread;
			RefreshRevision();
			TrimPreflights();
			const auto mutation = CopyMutation(a_request);
			auto decision = CSX::Api::EvaluateWeatherMutation(BuildPolicyState(mutation), BuildPolicyRequest(mutation));
			if (decision.allowed && mutation.action == CSX::WeatherAPI::MutationAction::kSetFeatureOverride) {
				json normalized;
				std::string error;
				if (!ValidateAndNormalizeOverride(FindWeatherFeature(mutation.featureName), mutation.valueJson, normalized, error)) {
					decision.allowed = false;
					decision.status = Status::kInvalidOverride;
					decision.reasonCode = "invalid_override";
					decision.message = std::move(error);
				}
			}
			thread_local ResponseStrings strings;
			strings.token.clear();
			strings.reasonCode = decision.reasonCode;
			strings.message = decision.message;
			if (decision.allowed) {
				strings.token = CSX::Api::ServiceFoundation::NewId();
				preflights.insert_or_assign(strings.token, PendingPreflight{ mutation, std::chrono::steady_clock::now() + kPreflightLifetime });
			}
			a_output = {
				.structSize = sizeof(Preflight001),
				.status = decision.status,
				.allowed = decision.allowed ? 1u : 0u,
				.disruptive = decision.disruptive ? 1u : 0u,
				.destructive = decision.destructive ? 1u : 0u,
				.willPersist = decision.willPersist ? 1u : 0u,
				.willApplyLive = decision.willApplyLive ? 1u : 0u,
				.stateRevision = revision,
				.requiredFlags = decision.requiredFlags,
				.token = strings.token.empty() ? nullptr : strings.token.c_str(),
				.reasonCode = strings.reasonCode.c_str(),
				.message = strings.message.c_str(),
			};
			return decision.status;
		}

		Status Execute(const MutationRequest001& a_request, MutationReceipt001& a_output)
		{
			if (!IsOwnerThread())
				return Status::kWrongThread;
			RefreshRevision();
			TrimPreflights();
			thread_local ResponseStrings strings;
			strings.message.clear();
			const auto mutation = CopyMutation(a_request);
			const std::string token = a_request.preflightToken ? a_request.preflightToken : "";
			if (token.empty())
				return FillFailure(a_output, Status::kPreflightRequired, "execute requires a successful preflight token");
			const auto found = preflights.find(token);
			if (found == preflights.end())
				return FillFailure(a_output, Status::kPreflightExpired, "preflight token is unknown or expired");
			if (!(found->second.mutation == mutation))
				return FillFailure(a_output, Status::kPreflightMismatch, "execute arguments differ from the preflight request");
			preflights.erase(found);
			const auto decision = CSX::Api::EvaluateWeatherMutation(BuildPolicyState(mutation), BuildPolicyRequest(mutation));
			if (!decision.allowed)
				return FillFailure(a_output, decision.status, decision.message);

			const auto previousRevision = revision;
			bool changed = false;
			bool persisted = false;
			bool liveApplied = false;
			Status status = Status::kSuccess;
			auto* sky = GetSky();
			auto* editor = EditorWindow::GetSingleton();
			auto* manager = WeatherManager::GetSingleton();
			auto* weather = FindWeather(mutation.weatherKey);
			auto* feature = FindWeatherFeature(mutation.featureName);
			switch (mutation.action) {
			case CSX::WeatherAPI::MutationAction::kSetWeather:
				sky->SetWeather(weather, true, mutation.accelerate);
				if (editor->IsWeatherLocked())
					editor->LockWeather(weather);
				changed = true;
				liveApplied = true;
				strings.message = "weather selection applied";
				break;
			case CSX::WeatherAPI::MutationAction::kPreviewWeather:
				sky->ForceWeather(weather, false);
				changed = true;
				liveApplied = true;
				strings.message = "temporary weather preview applied";
				break;
			case CSX::WeatherAPI::MutationAction::kResetWeather:
				sky->ResetWeather();
				changed = true;
				liveApplied = true;
				strings.message = "weather reset requested";
				break;
			case CSX::WeatherAPI::MutationAction::kLockWeather:
				changed = !editor->IsWeatherLocked() || editor->GetLockedWeather() != weather;
				editor->LockWeather(weather);
				liveApplied = true;
				strings.message = "weather lock applied";
				break;
			case CSX::WeatherAPI::MutationAction::kUnlockWeather:
				changed = editor->IsWeatherLocked();
				editor->UnlockWeather();
				liveApplied = changed;
				strings.message = changed ? "weather lock released" : "weather was already unlocked";
				break;
			case CSX::WeatherAPI::MutationAction::kSetFeaturePaused:
				changed = WeatherVariables::GlobalWeatherRegistry::GetSingleton()->IsFeaturePaused(feature->GetShortName()) != mutation.boolValue;
				manager->SetFeaturePaused(feature->GetShortName(), mutation.boolValue);
				liveApplied = changed;
				strings.message = mutation.boolValue ? "feature weather ownership paused" : "feature weather ownership resumed";
				break;
			case CSX::WeatherAPI::MutationAction::kReloadOverrides:
				manager->ClearCache();
				manager->RefreshFeatureOverrides();
				changed = true;
				liveApplied = true;
				strings.message = "per-weather overrides reloaded from disk";
				break;
			case CSX::WeatherAPI::MutationAction::kSetFeatureOverride:
			case CSX::WeatherAPI::MutationAction::kRemoveFeatureOverride:
				status = ApplyOverrideMutation(mutation, weather, feature, persisted, liveApplied, changed, strings.message);
				break;
			default:
				return FillFailure(a_output, Status::kInvalidArgument, "unknown weather mutation action");
			}
			RefreshRevision(changed);
			a_output = {
				.structSize = sizeof(MutationReceipt001),
				.status = status,
				.applied = status == Status::kSuccess ? 1u : 0u,
				.changed = changed ? 1u : 0u,
				.persisted = persisted ? 1u : 0u,
				.liveApplied = liveApplied ? 1u : 0u,
				.previousStateRevision = previousRevision,
				.stateRevision = revision,
				.message = strings.message.c_str(),
			};
			return status;
		}

	private:
		std::thread::id ownerThread;
		std::uint64_t revision = 0;
		std::string fingerprint;
		std::unordered_map<std::string, PendingPreflight> preflights;

		OwnedMutation CopyMutation(const MutationRequest001& a_request) const
		{
			return { a_request.action, a_request.expectedStateRevision, a_request.flags,
				a_request.boolValue != 0, a_request.accelerate != 0,
				a_request.weatherKey ? a_request.weatherKey : "",
				a_request.featureName ? a_request.featureName : "",
				a_request.valueJson ? a_request.valueJson : "" };
		}

		CSX::Api::WeatherPolicyRequest BuildPolicyRequest(const OwnedMutation& a_mutation) const
		{
			return { a_mutation.action, a_mutation.expectedStateRevision, a_mutation.flags,
				a_mutation.boolValue, a_mutation.weatherKey, a_mutation.featureName, a_mutation.valueJson };
		}

		CSX::Api::WeatherPolicyState BuildPolicyState(const OwnedMutation& a_mutation) const
		{
			auto* state = globals::state;
			return {
				.available = RE::TESDataHandler::GetSingleton() && state,
				.skyAvailable = GetSky() != nullptr,
				.weatherFound = FindWeather(a_mutation.weatherKey) != nullptr,
				.featureFound = FindWeatherFeature(a_mutation.featureName) != nullptr,
				.featureSupportsWeather = FindWeatherFeature(a_mutation.featureName) != nullptr,
				.weatherLocked = EditorWindow::GetSingleton()->IsWeatherLocked(),
				.persistentMutationBlocked = state && state->IsPersistentMutationBlocked(),
				.stateRevision = revision,
			};
		}

		std::string BuildFingerprint() const
		{
			json value = json::object();
			auto* sky = GetSky();
			auto key = [](RE::TESWeather* a_weather) { return a_weather ? WeatherManager::GetWeatherKey(a_weather) : std::string{}; };
			value["current"] = sky ? key(sky->currentWeather) : "";
			value["last"] = sky ? key(sky->lastWeather) : "";
			value["default"] = sky ? key(sky->defaultWeather) : "";
			value["override"] = sky ? key(sky->overrideWeather) : "";
			auto* editor = EditorWindow::GetSingleton();
			value["locked"] = editor->IsWeatherLocked() ? key(editor->GetLockedWeather()) : "";
			value["persistenceBlocked"] = globals::state && globals::state->IsPersistentMutationBlocked();
			auto* registry = WeatherVariables::GlobalWeatherRegistry::GetSingleton();
			auto* manager = WeatherManager::GetSingleton();
			for (auto* feature : GetWeatherFeatures())
				value["paused"][feature->GetShortName()] = registry->IsFeaturePaused(feature->GetShortName());
			for (auto* weather : GetWeatherCatalog()) {
				std::map<std::string, json> settings;
				if (manager->GetAllSettingsForWeather(weather, settings))
					value["effectiveOverrides"][WeatherManager::GetWeatherKey(weather)] = settings;
			}
			return value.dump();
		}

		void RefreshRevision(bool a_force = false)
		{
			const auto next = BuildFingerprint();
			if (a_force || revision == 0 || next != fingerprint) {
				++revision;
				fingerprint = next;
				preflights.clear();
			}
		}

		void TrimPreflights()
		{
			const auto now = std::chrono::steady_clock::now();
			for (auto it = preflights.begin(); it != preflights.end();) {
				if (it->second.expiresAt <= now)
					it = preflights.erase(it);
				else
					++it;
			}
		}

		Status FillFailure(MutationReceipt001& a_output, Status a_status, std::string a_message)
		{
			thread_local ResponseStrings strings;
			strings.message = std::move(a_message);
			a_output = { .structSize = sizeof(MutationReceipt001), .status = a_status, .stateRevision = revision, .message = strings.message.c_str() };
			return a_status;
		}

		Status ApplyOverrideMutation(
			const OwnedMutation& a_mutation, RE::TESWeather* a_weather, Feature* a_feature,
			bool& a_persisted, bool& a_liveApplied, bool& a_changed, std::string& a_message)
		{
			json normalized;
			if (a_mutation.action == CSX::WeatherAPI::MutationAction::kSetFeatureOverride) {
				std::string error;
				if (!ValidateAndNormalizeOverride(a_feature, a_mutation.valueJson, normalized, error)) {
					a_message = std::move(error);
					return Status::kInvalidOverride;
				}
			}
			auto* manager = WeatherManager::GetSingleton();
			json persistedRoot;
			std::map<std::string, json> persistedSettings;
			bool persistedChanged = false;
			if ((a_mutation.flags & CSX::WeatherAPI::kMutationPersist) != 0) {
				std::string error;
				if (!ReadWeatherAttachment(a_weather, persistedRoot, error)) {
					a_message = std::format("failed to read weather attachment: {}", error);
					return Status::kPersistenceFailed;
				}
				auto& featureSettings = persistedRoot["featureSettings"];
				if (featureSettings.is_null())
					featureSettings = json::object();
				if (!featureSettings.is_object()) {
					a_message = "weather attachment featureSettings is not an object";
					return Status::kPersistenceFailed;
				}
				const auto persistedIt = featureSettings.find(a_feature->GetShortName());
				if (a_mutation.action == CSX::WeatherAPI::MutationAction::kSetFeatureOverride) {
					persistedChanged = persistedIt == featureSettings.end() || *persistedIt != normalized;
					featureSettings[a_feature->GetShortName()] = normalized;
				} else {
					persistedChanged = persistedIt != featureSettings.end();
					featureSettings.erase(a_feature->GetShortName());
				}
				if (persistedChanged) {
					Util::FileHelpers::EnsureDirectoryExists(GetWeatherPath(a_weather).parent_path());
					if (!Util::FileHelpers::WriteTextFileAtomic(GetWeatherPath(a_weather), persistedRoot.dump(2), error)) {
						a_message = std::format("failed to persist weather attachment: {}", error);
						return Status::kPersistenceFailed;
					}
				}
				persistedSettings = ExtractFeatureSettings(persistedRoot);
				a_persisted = true;
			}

			std::map<std::string, json> effectiveSettings;
			manager->GetAllSettingsForWeather(a_weather, effectiveSettings);
			const auto old = effectiveSettings.find(a_feature->GetShortName());
			const bool hadOld = old != effectiveSettings.end();
			const bool setting = a_mutation.action == CSX::WeatherAPI::MutationAction::kSetFeatureOverride;
			const bool liveChanged = setting ? (!hadOld || old->second != normalized) : hadOld;
			a_changed = persistedChanged ||
			            ((a_mutation.flags & CSX::WeatherAPI::kMutationApplyLive) != 0 && liveChanged);
			if ((a_mutation.flags & CSX::WeatherAPI::kMutationApplyLive) != 0) {
				if (setting)
					effectiveSettings[a_feature->GetShortName()] = normalized;
				else
					effectiveSettings.erase(a_feature->GetShortName());
				manager->StageSettingsForWeather(a_weather, effectiveSettings);
				manager->RefreshFeatureOverrides();
				a_liveApplied = true;
			}
			if (a_persisted) {
				manager->CommitSettingsForWeather(a_weather, persistedSettings);
			}
			a_message = setting ? "feature weather override updated" : "feature weather override removed";
			if (a_persisted)
				a_message += "; persisted";
			if (a_liveApplied)
				a_message += "; applied live";
			return Status::kSuccess;
		}
	};

	WeatherService& GetWeatherService()
	{
		static WeatherService service;
		return service;
	}

	Status GetSnapshotFn(const void* a_context, Snapshot001* a_output)
	{
		if (!a_context || !a_output)
			return Status::kInvalidArgument;
		if (a_output->structSize < sizeof(Snapshot001))
			return Status::kStructureTooSmall;
		try {
			return static_cast<WeatherService*>(const_cast<void*>(a_context))->GetSnapshot(*a_output);
		} catch (...) {
			return Status::kInternalError;
		}
	}

	std::uint32_t GetWeatherCountFn(const void* a_context)
	{
		try {
			return a_context ? static_cast<WeatherService*>(const_cast<void*>(a_context))->GetWeatherCount() : 0;
		} catch (...) {
			return 0;
		}
	}

	Status GetWeatherDescriptorFn(const void* a_context, std::uint32_t a_index, WeatherDescriptor001* a_output)
	{
		if (!a_context || !a_output)
			return Status::kInvalidArgument;
		if (a_output->structSize < sizeof(WeatherDescriptor001))
			return Status::kStructureTooSmall;
		try {
			return static_cast<WeatherService*>(const_cast<void*>(a_context))->GetWeather(a_index, *a_output);
		} catch (...) {
			return Status::kInternalError;
		}
	}

	std::uint32_t GetFeatureCountFn(const void* a_context)
	{
		try {
			return a_context ? static_cast<WeatherService*>(const_cast<void*>(a_context))->GetFeatureCount() : 0;
		} catch (...) {
			return 0;
		}
	}

	Status GetFeatureDescriptorFn(const void* a_context, std::uint32_t a_index, FeatureDescriptor001* a_output)
	{
		if (!a_context || !a_output)
			return Status::kInvalidArgument;
		if (a_output->structSize < sizeof(FeatureDescriptor001))
			return Status::kStructureTooSmall;
		try {
			return static_cast<WeatherService*>(const_cast<void*>(a_context))->GetFeature(a_index, *a_output);
		} catch (...) {
			return Status::kInternalError;
		}
	}

	std::uint32_t GetVariableCountFn(const void* a_context, const char* a_featureName)
	{
		try {
			return a_context && a_featureName ? static_cast<WeatherService*>(const_cast<void*>(a_context))->GetVariableCount(a_featureName) : 0;
		} catch (...) {
			return 0;
		}
	}

	Status GetVariableDescriptorFn(const void* a_context, const char* a_featureName, std::uint32_t a_index, VariableDescriptor001* a_output)
	{
		if (!a_context || !a_featureName || !a_output)
			return Status::kInvalidArgument;
		if (a_output->structSize < sizeof(VariableDescriptor001))
			return Status::kStructureTooSmall;
		try {
			return static_cast<WeatherService*>(const_cast<void*>(a_context))->GetVariable(a_featureName, a_index, *a_output);
		} catch (...) {
			return Status::kInternalError;
		}
	}

	Status GetOverrideFn(const void* a_context, const char* a_weatherKey, const char* a_featureName, OverrideSnapshot001* a_output)
	{
		if (!a_context || !a_weatherKey || !a_featureName || !a_output)
			return Status::kInvalidArgument;
		if (a_output->structSize < sizeof(OverrideSnapshot001))
			return Status::kStructureTooSmall;
		try {
			return static_cast<WeatherService*>(const_cast<void*>(a_context))->GetOverride(a_weatherKey, a_featureName, *a_output);
		} catch (...) {
			return Status::kInternalError;
		}
	}

	Status PreflightFn(const void* a_context, const MutationRequest001* a_request, Preflight001* a_output)
	{
		if (!a_context || !a_request || !a_output)
			return Status::kInvalidArgument;
		if (a_request->structSize < sizeof(MutationRequest001) || a_output->structSize < sizeof(Preflight001))
			return Status::kStructureTooSmall;
		try {
			return static_cast<WeatherService*>(const_cast<void*>(a_context))->Preflight(*a_request, *a_output);
		} catch (...) {
			return Status::kInternalError;
		}
	}

	Status ExecuteFn(const void* a_context, const MutationRequest001* a_request, MutationReceipt001* a_output)
	{
		if (!a_context || !a_request || !a_output)
			return Status::kInvalidArgument;
		if (a_request->structSize < sizeof(MutationRequest001) || a_output->structSize < sizeof(MutationReceipt001))
			return Status::kStructureTooSmall;
		try {
			return static_cast<WeatherService*>(const_cast<void*>(a_context))->Execute(*a_request, *a_output);
		} catch (...) {
			return Status::kInternalError;
		}
	}
}

namespace CSX::Api
{
	const WeatherAPI::Interface001* GetWeatherService001()
	{
		static const WeatherAPI::Interface001 service{
			.structSize = sizeof(WeatherAPI::Interface001),
			.major = WeatherAPI::ServiceMajor,
			.minor = WeatherAPI::ServiceMinor,
			.schemaRevision = WeatherAPI::SchemaRevision,
			.capabilities = WeatherAPI::ServiceCapabilities,
			.context = &GetWeatherService(),
			.GetSnapshot = &GetSnapshotFn,
			.GetWeatherCount = &GetWeatherCountFn,
			.GetWeatherDescriptor = &GetWeatherDescriptorFn,
			.GetFeatureCount = &GetFeatureCountFn,
			.GetFeatureDescriptor = &GetFeatureDescriptorFn,
			.GetVariableCount = &GetVariableCountFn,
			.GetVariableDescriptor = &GetVariableDescriptorFn,
			.GetOverride = &GetOverrideFn,
			.Preflight = &PreflightFn,
			.Execute = &ExecuteFn,
		};
		return &service;
	}

	void InitializeWeatherService()
	{
		static std::once_flag initialized;
		std::call_once(initialized, [] {
			const auto status = GetProcessServiceRegistry().Register({
				WeatherAPI::ServiceName,
				WeatherAPI::ServiceMajor,
				WeatherAPI::ServiceMinor,
				WeatherAPI::SchemaRevision,
				ServiceAPI::kCapabilityInspection | ServiceAPI::kCapabilityRuntimeMutation |
					ServiceAPI::kCapabilityPersistentMutation | ServiceAPI::kCapabilityDestructiveOperations |
					ServiceAPI::kCapabilityTransactions,
				GetWeatherService001(),
			});
			if (status != ServiceAPI::Status::kSuccess)
				logger::error("Failed to register CSX weather service ({})", static_cast<std::uint32_t>(status));
			else
				logger::info("Registered CSX weather service ABI {}.{}", WeatherAPI::ServiceMajor, WeatherAPI::ServiceMinor);
		});
	}
}
