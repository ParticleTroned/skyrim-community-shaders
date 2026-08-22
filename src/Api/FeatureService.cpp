#include "Api/FeatureService.h"

#include "Api/RuntimeThreadAffinity.h"
#include "Api/ServiceFoundation.h"
#include "Api/ServiceRegistry.h"
#include "BuildProvenance.h"
#include "Feature.h"
#include "FeatureConstraints.h"
#include "Globals.h"
#include "State.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

namespace
{
	using json = nlohmann::json;
	using CSX::FeatureAPI::ConstraintDescriptor001;
	using CSX::FeatureAPI::FeatureDescriptor001;
	using CSX::FeatureAPI::MutationReceipt001;
	using CSX::FeatureAPI::MutationRequest001;
	using CSX::FeatureAPI::Preflight001;
	using CSX::FeatureAPI::SettingsSnapshot001;
	using CSX::FeatureAPI::Snapshot001;
	using CSX::FeatureAPI::Status;
	constexpr auto kPreflightLifetime = std::chrono::seconds(30);

	struct OwnedMutation
	{
		CSX::FeatureAPI::MutationAction action = CSX::FeatureAPI::MutationAction::kSetDisabledAtBoot;
		std::uint64_t expectedStateRevision = 0;
		std::uint64_t flags = 0;
		std::string featureShortName;
		bool disabled = false;
		bool operator==(const OwnedMutation&) const = default;
	};
	struct PendingPreflight { OwnedMutation mutation; std::chrono::steady_clock::time_point expiresAt; };
	struct Strings
	{
		std::string shortName, name, displayName, category, installedVersion, requiredVersion,
			shaderDefineName, failureMessage, summary, summaryItemsJson, settingsJson,
			sourceFeature, targetFeature, targetSetting, forcedValueJson, reason,
			token, reasonCode, message;
	};
	struct FlatConstraint
	{
		std::string sourceFeature;
		std::string targetFeature;
		std::string targetSetting;
		json forcedValue;
		std::string reason;
		bool recommendDisableAtBoot = false;
	};

	std::vector<Feature*> Catalog()
	{
		auto values = Feature::GetFeatureList();
		std::ranges::sort(values, {}, [](Feature* a_value) { return a_value->GetShortName(); });
		return values;
	}

	Feature* FindFeature(std::string_view a_shortName)
	{
		for (auto* feature : Catalog())
			if (feature && feature->GetShortName() == a_shortName) return feature;
		return nullptr;
	}

	json VariantJson(const std::variant<bool, int, float>& a_value)
	{
		return std::visit([](auto value) -> json { return value; }, a_value);
	}

	std::vector<FlatConstraint> Constraints()
	{
		std::vector<FlatConstraint> values;
		for (const auto& [target, result] : FeatureConstraints::GetAllActiveConstraints()) {
			for (const auto& source : result.sources) {
				values.push_back({ source.featureShortName, target.featureShortName, target.settingPath,
					VariantJson(result.forcedValue), source.reason, source.recommendDisableAtBoot });
			}
		}
		std::ranges::sort(values, [](const auto& left, const auto& right) {
			return std::tie(left.targetFeature, left.targetSetting, left.sourceFeature) <
			       std::tie(right.targetFeature, right.targetSetting, right.sourceFeature);
		});
		return values;
	}

	class FeatureService
	{
	public:
		Status GetSnapshot(Snapshot001& a_output)
		{
			if (!Owner()) return Status::kWrongThread;
			RefreshRevision();
			const auto features = Catalog();
			const auto loaded = std::ranges::count_if(features, [](Feature* value) { return value && value->loaded; });
			const auto constraints = Constraints();
			auto* state = globals::state;
			a_output = { .structSize = sizeof(Snapshot001), .available = state ? 1u : 0u,
				.persistentMutationBlocked = state && state->IsPersistentMutationBlocked() ? 1u : 0u,
				.saveLoadSafeModeActive = state && state->IsSaveLoadSafeModeActive() ? 1u : 0u,
				.featureCount = static_cast<std::uint32_t>(features.size()), .loadedFeatureCount = static_cast<std::uint32_t>(loaded),
				.activeConstraintCount = static_cast<std::uint32_t>(constraints.size()), .stateRevision = revision,
				.capabilities = CSX::FeatureAPI::ServiceCapabilities, .buildId = BuildProvenance::GetBuildId().data() };
			return Status::kSuccess;
		}

		std::uint32_t GetFeatureCount() { return Owner() ? static_cast<std::uint32_t>(Catalog().size()) : 0; }

		Status GetFeature(std::uint32_t a_index, FeatureDescriptor001& a_output)
		{
			if (!Owner()) return Status::kWrongThread;
			const auto features = Catalog();
			if (a_index >= features.size()) return Status::kFeatureNotFound;
			auto* feature = features[a_index];
			const auto featureSummary = feature->GetFeatureSummary();
			strings.shortName = feature->GetShortName();
			strings.name = feature->GetName();
			strings.displayName = feature->GetDisplayName();
			strings.category = feature->GetCategory();
			strings.installedVersion = feature->version;
			strings.requiredVersion = Feature::GetFeatureRequiredVersion(strings.shortName);
			strings.shaderDefineName = feature->GetShaderDefineName();
			strings.failureMessage = feature->failedLoadedMessage;
			strings.summary = featureSummary.first;
			strings.summaryItemsJson = json(featureSummary.second).dump();
			std::uint32_t constraintCount = 0;
			for (const auto& value : Constraints()) if (value.sourceFeature == strings.shortName) ++constraintCount;
			auto* state = globals::state;
			a_output = { .structSize = sizeof(FeatureDescriptor001), .shortName = strings.shortName.c_str(),
				.name = strings.name.c_str(), .displayName = strings.displayName.c_str(), .category = strings.category.c_str(),
				.installedVersion = strings.installedVersion.c_str(), .requiredVersion = strings.requiredVersion.c_str(),
				.shaderDefineName = strings.shaderDefineName.c_str(), .failureMessage = strings.failureMessage.c_str(),
				.summary = strings.summary.c_str(), .summaryItemsJson = strings.summaryItemsJson.c_str(),
				.loaded = feature->loaded ? 1u : 0u, .core = feature->IsCore() ? 1u : 0u,
				.inMenu = feature->IsInMenu() ? 1u : 0u, .supportsVR = feature->SupportsVR() ? 1u : 0u,
				.disabledAtBoot = state && state->IsFeatureDisabled(strings.shortName) ? 1u : 0u,
				.runtimeDisabledByMissingDependency = feature->IsRuntimeDisabledByMissingDependency() ? 1u : 0u,
				.hiddenFromUserView = feature->IsHiddenFromUserView() ? 1u : 0u,
				.hiddenInEssentialsMode = feature->IsHiddenInEssentialsMode() ? 1u : 0u,
				.hasFeatureSettings = feature->HasFeatureSettings() ? 1u : 0u,
				.hasShaderDefine = strings.shaderDefineName.empty() ? 0u : 1u, .activeConstraintCount = constraintCount };
			return Status::kSuccess;
		}

		Status GetSettings(const char* a_shortName, SettingsSnapshot001& a_output)
		{
			if (!Owner()) return Status::kWrongThread;
			if (!a_shortName || !*a_shortName) return Status::kInvalidArgument;
			auto* feature = FindFeature(a_shortName);
			if (!feature) return Status::kFeatureNotFound;
			if (!feature->loaded || !feature->HasFeatureSettings()) return Status::kUnavailable;
			json settings = json::object();
			feature->SaveSettings(settings);
			strings.shortName = feature->GetShortName();
			strings.settingsJson = settings.dump();
			a_output = { .structSize = sizeof(SettingsSnapshot001), .featureShortName = strings.shortName.c_str(),
				.settingsJson = strings.settingsJson.c_str() };
			return Status::kSuccess;
		}

		std::uint32_t GetConstraintCount() { return Owner() ? static_cast<std::uint32_t>(Constraints().size()) : 0; }

		Status GetConstraint(std::uint32_t a_index, ConstraintDescriptor001& a_output)
		{
			if (!Owner()) return Status::kWrongThread;
			const auto values = Constraints();
			if (a_index >= values.size()) return Status::kInvalidArgument;
			const auto& value = values[a_index];
			strings.sourceFeature = value.sourceFeature; strings.targetFeature = value.targetFeature;
			strings.targetSetting = value.targetSetting; strings.forcedValueJson = value.forcedValue.dump(); strings.reason = value.reason;
			a_output = { .structSize = sizeof(ConstraintDescriptor001), .sourceFeatureShortName = strings.sourceFeature.c_str(),
				.targetFeatureShortName = strings.targetFeature.c_str(), .targetSettingPath = strings.targetSetting.c_str(),
				.forcedValueJson = strings.forcedValueJson.c_str(), .reason = strings.reason.c_str(),
				.recommendDisableAtBoot = value.recommendDisableAtBoot ? 1u : 0u };
			return Status::kSuccess;
		}

		Status Preflight(const MutationRequest001& a_request, Preflight001& a_output)
		{
			if (!Owner()) return FillPreflight(a_output, Status::kWrongThread, "wrong_thread", "feature API calls are main-thread-affine");
			Prune(); RefreshRevision();
			const auto mutation = Own(a_request);
			if (mutation.expectedStateRevision != revision) return FillPreflight(a_output, Status::kRevisionConflict, "revision_conflict", "state changed; request a fresh snapshot");
			if (mutation.action != CSX::FeatureAPI::MutationAction::kSetDisabledAtBoot) return FillPreflight(a_output, Status::kInvalidArgument, "invalid_action", "mutation action is unknown");
			if (!FindFeature(mutation.featureShortName)) return FillPreflight(a_output, Status::kFeatureNotFound, "feature_not_found", "featureShortName is unknown");
			auto* state = globals::state;
			if (!state) return FillPreflight(a_output, Status::kUnavailable, "unavailable", "feature state is unavailable");
			if (state->IsPersistentMutationBlocked()) return FillPreflight(a_output, Status::kBlocked, "persistence_blocked", "persistent mutations are blocked during save/load activity");
			constexpr auto required = CSX::FeatureAPI::kMutationPersist | CSX::FeatureAPI::kMutationAllowDisruptive;
			if ((mutation.flags & required) != required) {
				strings.reasonCode = "acknowledgement_required"; strings.message = "boot configuration requires persist and allowDisruptive";
				a_output = { .structSize = sizeof(Preflight001), .status = Status::kBlocked, .disruptive = 1, .willPersist = 1,
					.stateRevision = revision, .requiredFlags = required, .reasonCode = strings.reasonCode.c_str(), .message = strings.message.c_str() };
				return Status::kBlocked;
			}
			strings.token = CSX::Api::ServiceFoundation::NewId();
			pending.insert_or_assign(strings.token, PendingPreflight{ mutation, std::chrono::steady_clock::now() + kPreflightLifetime });
			strings.reasonCode = "allowed"; strings.message = "change will apply on next launch and may invalidate compiled shaders";
			a_output = { .structSize = sizeof(Preflight001), .status = Status::kSuccess, .allowed = 1, .disruptive = 1, .willPersist = 1,
				.stateRevision = revision, .requiredFlags = required, .token = strings.token.c_str(),
				.reasonCode = strings.reasonCode.c_str(), .message = strings.message.c_str() };
			return Status::kSuccess;
		}

		Status Execute(const MutationRequest001& a_request, MutationReceipt001& a_output)
		{
			if (!Owner()) return FillReceipt(a_output, Status::kWrongThread, "feature API calls are main-thread-affine");
			Prune(); RefreshRevision();
			const auto beforeRevision = revision;
			const auto mutation = Own(a_request);
			if (!a_request.preflightToken || !*a_request.preflightToken) return FillReceipt(a_output, Status::kPreflightRequired, "preflightToken is required");
			const auto found = pending.find(a_request.preflightToken);
			if (found == pending.end()) return FillReceipt(a_output, Status::kPreflightExpired, "preflight token is missing or expired");
			const auto expected = found->second.mutation; pending.erase(found);
			if (expected != mutation) return FillReceipt(a_output, Status::kPreflightMismatch, "mutation does not match preflight request");
			if (mutation.expectedStateRevision != revision) return FillReceipt(a_output, Status::kRevisionConflict, "state changed after preflight");
			auto* state = globals::state;
			if (!state || state->IsPersistentMutationBlocked()) return FillReceipt(a_output, Status::kBlocked, "persistent mutations are currently blocked");
			if (!FindFeature(mutation.featureShortName)) return FillReceipt(a_output, Status::kFeatureNotFound, "featureShortName is unknown");
			const bool previous = state->IsFeatureDisabled(mutation.featureShortName);
			const bool changed = previous != mutation.disabled;
			if (changed) state->SetFeatureDisabled(mutation.featureShortName, mutation.disabled);
			if (!state->Save(State::ConfigMode::USER)) {
				if (changed) state->SetFeatureDisabled(mutation.featureShortName, previous);
				return FillReceipt(a_output, Status::kPersistenceFailed, "settings save failed; in-memory boot state was restored");
			}
			if (changed) ++revision;
			lastSignature = Signature();
			strings.message = changed ? "feature boot state persisted; restart required" : "feature boot state already matched; settings persisted";
			a_output = { .structSize = sizeof(MutationReceipt001), .status = Status::kSuccess, .applied = 1,
				.changed = changed ? 1u : 0u, .persisted = 1, .previousStateRevision = beforeRevision,
				.stateRevision = revision, .message = strings.message.c_str() };
			return Status::kSuccess;
		}

	private:
		std::uint64_t revision = 1;
		std::optional<std::string> lastSignature;
		std::unordered_map<std::string, PendingPreflight> pending;
		Strings strings;
		bool Owner() const { return CSX::Api::IsRuntimeMainThread(); }
		static OwnedMutation Own(const MutationRequest001& value) { return { value.action, value.expectedStateRevision, value.flags,
			value.featureShortName ? value.featureShortName : "", value.disabled != 0 }; }
		std::string Signature() const
		{
			std::string value;
			auto* state = globals::state;
			for (auto* feature : Catalog()) {
				value += feature->GetShortName(); value += feature->loaded ? ":1:" : ":0:"; value += feature->version;
				value += state && state->IsFeatureDisabled(feature->GetShortName()) ? ":D;" : ":E;";
			}
			return value;
		}
		void RefreshRevision() { const auto now = Signature(); if (lastSignature && *lastSignature != now) ++revision; lastSignature = now; }
		Status FillPreflight(Preflight001& output, Status status, std::string reason, std::string message)
		{
			strings.reasonCode = std::move(reason); strings.message = std::move(message);
			output = { .structSize = sizeof(Preflight001), .status = status, .stateRevision = revision,
				.reasonCode = strings.reasonCode.c_str(), .message = strings.message.c_str() }; return status;
		}
		Status FillReceipt(MutationReceipt001& output, Status status, std::string message)
		{
			strings.message = std::move(message); output = { .structSize = sizeof(MutationReceipt001), .status = status,
				.stateRevision = revision, .message = strings.message.c_str() }; return status;
		}
		void Prune()
		{
			const auto now = std::chrono::steady_clock::now();
			for (auto it = pending.begin(); it != pending.end();) it = it->second.expiresAt <= now ? pending.erase(it) : std::next(it);
		}
	};

	FeatureService& Service() { static FeatureService value; return value; }

	template <class T> Status Checked(const void* context, T* output, auto&& call)
	{
		if (!context || !output) return Status::kInvalidArgument;
		if (output->structSize < sizeof(T)) return Status::kStructureTooSmall;
		try { return call(*static_cast<FeatureService*>(const_cast<void*>(context)), *output); }
		catch (...) { return Status::kInternalError; }
	}
	Status SnapshotFn(const void* c, Snapshot001* o) { return Checked(c, o, [](auto& s, auto& v) { return s.GetSnapshot(v); }); }
	std::uint32_t CountFn(const void* c) { try { return c ? static_cast<FeatureService*>(const_cast<void*>(c))->GetFeatureCount() : 0; } catch (...) { return 0; } }
	Status FeatureFn(const void* c, std::uint32_t i, FeatureDescriptor001* o) { return Checked(c, o, [i](auto& s, auto& v) { return s.GetFeature(i, v); }); }
	Status SettingsFn(const void* c, const char* n, SettingsSnapshot001* o) { if (!n) return Status::kInvalidArgument; return Checked(c, o, [n](auto& s, auto& v) { return s.GetSettings(n, v); }); }
	std::uint32_t ConstraintCountFn(const void* c) { try { return c ? static_cast<FeatureService*>(const_cast<void*>(c))->GetConstraintCount() : 0; } catch (...) { return 0; } }
	Status ConstraintFn(const void* c, std::uint32_t i, ConstraintDescriptor001* o) { return Checked(c, o, [i](auto& s, auto& v) { return s.GetConstraint(i, v); }); }
	Status PreflightFn(const void* c, const MutationRequest001* r, Preflight001* o) { if (!r || r->structSize < sizeof(MutationRequest001)) return Status::kInvalidArgument; return Checked(c, o, [r](auto& s, auto& v) { return s.Preflight(*r, v); }); }
	Status ExecuteFn(const void* c, const MutationRequest001* r, MutationReceipt001* o) { if (!r || r->structSize < sizeof(MutationRequest001)) return Status::kInvalidArgument; return Checked(c, o, [r](auto& s, auto& v) { return s.Execute(*r, v); }); }
}

namespace CSX::Api
{
	const FeatureAPI::Interface001* GetFeatureService001()
	{
		static const FeatureAPI::Interface001 api{ .structSize = sizeof(FeatureAPI::Interface001), .major = 1, .minor = 0,
			.schemaRevision = 1, .capabilities = FeatureAPI::ServiceCapabilities, .context = &Service(),
			.GetSnapshot = &SnapshotFn, .GetFeatureCount = &CountFn, .GetFeatureDescriptor = &FeatureFn,
			.GetFeatureSettings = &SettingsFn, .GetConstraintCount = &ConstraintCountFn,
			.GetConstraintDescriptor = &ConstraintFn, .Preflight = &PreflightFn, .Execute = &ExecuteFn };
		return &api;
	}

	void InitializeFeatureService()
	{
		static std::once_flag initialized;
		std::call_once(initialized, [] {
			const auto status = GetProcessServiceRegistry().Register({ FeatureAPI::ServiceName, 1, 0, 1,
				ServiceAPI::kCapabilityInspection | ServiceAPI::kCapabilityPersistentMutation | ServiceAPI::kCapabilityTransactions,
				GetFeatureService001() });
			if (status == ServiceAPI::Status::kSuccess) logger::info("Registered CSX feature service ABI 1.0");
			else logger::error("Failed to register CSX feature service ({})", static_cast<std::uint32_t>(status));
		});
	}
}
