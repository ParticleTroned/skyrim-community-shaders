#include "Api/ShaderService.h"

#include "Api/RuntimeThreadAffinity.h"
#include "Api/ServiceFoundation.h"
#include "Api/ServiceRegistry.h"
#include "Api/ShaderServicePolicy.h"
#include "BuildProvenance.h"
#include "Feature.h"
#include "Globals.h"
#include "ShaderCache.h"
#include "State.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <limits>
#include <mutex>
#include <optional>
#include <ranges>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace
{
	using CSX::ShaderAPI::FeatureDescriptor001;
	using CSX::ShaderAPI::MutationReceipt001;
	using CSX::ShaderAPI::MutationRequest001;
	using CSX::ShaderAPI::Preflight001;
	using CSX::ShaderAPI::Snapshot001;
	using CSX::ShaderAPI::Status;

	constexpr auto kPreflightLifetime = std::chrono::seconds(30);

	struct OwnedMutation
	{
		CSX::ShaderAPI::MutationAction action = CSX::ShaderAPI::MutationAction::kSetCustomShaders;
		std::uint64_t expectedStateRevision = 0;
		std::uint64_t flags = CSX::ShaderAPI::kMutationNone;
		bool boolValue = false;
		std::string featureName;

		bool operator==(const OwnedMutation&) const = default;
	};

	struct PendingPreflight
	{
		OwnedMutation mutation;
		std::chrono::steady_clock::time_point expiresAt;
	};

	struct FeatureStrings
	{
		std::string shortName;
		std::string displayName;
		std::string version;
		std::string category;
		std::string shaderDefine;
		std::string loadFailure;
	};

	struct ResponseStrings
	{
		std::string token;
		std::string reasonCode;
		std::string message;
		std::string statistics;
	};

	class ShaderService
	{
	public:
		bool IsOwnerThread() const { return CSX::Api::IsRuntimeMainThread(); }

		Status GetSnapshot(Snapshot001& a_output)
		{
			if (!IsOwnerThread())
				return Status::kWrongThread;
			RefreshRevision();
			auto* cache = globals::shaderCache;
			auto* state = globals::state;
			thread_local ResponseStrings strings;
			strings.statistics = cache ? cache->GetShaderStatsString() : std::string{};
			a_output = {
				.structSize = sizeof(Snapshot001),
				.available = cache && state ? 1u : 0u,
				.stateRevision = revision,
				.capabilities = CSX::ShaderAPI::ServiceCapabilities,
				.customShadersRequested = cache && cache->IsEnableRequested() ? 1u : 0u,
				.customShadersEffective = cache && cache->IsEnabled() ? 1u : 0u,
				.customShaderTransitionPending = cache && cache->IsEnableRequested() != cache->IsEnabled() ? 1u : 0u,
				.diskCacheRequested = cache && cache->IsDiskCache() ? 1u : 0u,
				.diskCacheActive = cache && cache->IsDiskCacheActive() ? 1u : 0u,
				.diskCacheHeld = cache && cache->IsDiskCacheHeld() ? 1u : 0u,
				.asyncCompilation = cache && cache->IsAsync() ? 1u : 0u,
				.skipUnchangedShaders = cache && cache->IsSkipUnchangedShaders() ? 1u : 0u,
				.compiling = cache && cache->IsCompiling() ? 1u : 0u,
				.activeShaderCapture = cache && (cache->IsCapturingActiveShaders() || cache->IsAwaitingMenuCloseCapture()) ? 1u : 0u,
				.previousCacheAvailable = cache && cache->HasPreviousDiskCache() ? 1u : 0u,
				.featureSetChanged = cache && cache->HasFeatureSetChanges() ? 1u : 0u,
				.featureSetRevertPending = cache && cache->HasFeatureSetRevertPending() ? 1u : 0u,
				.persistentMutationBlocked = state && state->IsPersistentMutationBlocked() ? 1u : 0u,
				.saveLoadSafeModeActive = state && state->IsSaveLoadSafeModeActive() ? 1u : 0u,
				.totalTasks = cache ? cache->GetTotalTasks() : 0,
				.completedTasks = cache ? cache->GetCompletedTasks() : 0,
				.failedTasks = cache ? cache->GetFailedTasks() : 0,
				.currentFailedShaders = cache ? cache->GetCurrentFailedCount() : 0,
				.memoryCacheHits = cache ? cache->GetCachedHitTasks() : 0,
				.diskCacheHits = cache ? cache->GetDiskHitTasks() : 0,
				.sourceCompiles = cache ? cache->GetSourceCompileTasks() : 0,
				.slowTasks = cache ? cache->GetSlowTasks() : 0,
				.verySlowTasks = cache ? cache->GetVerySlowTasks() : 0,
				.heavyTasksInFlight = cache ? static_cast<std::uint32_t>((std::max)(cache->GetHeavyTasksInFlight(), 0)) : 0,
				.foregroundThreadCount = cache ? static_cast<std::uint32_t>((std::max)(cache->compilationThreadCount, 0)) : 0,
				.backgroundThreadCount = cache ? static_cast<std::uint32_t>((std::max)(cache->backgroundCompilationThreadCount, 0)) : 0,
				.buildId = BuildProvenance::GetBuildId().data(),
				.shaderCacheAbiId = BuildProvenance::GetShaderCacheAbiId().data(),
				.shaderCompilerIdentity = BuildProvenance::GetShaderCompilerIdentity().c_str(),
				.statisticsText = strings.statistics.c_str(),
			};
			return cache && state ? Status::kSuccess : Status::kUnavailable;
		}

		std::uint32_t GetFeatureCount() const
		{
			if (!IsOwnerThread())
				return 0;
			return static_cast<std::uint32_t>((std::min<std::size_t>)(
				Feature::GetFeatureList().size(), std::numeric_limits<std::uint32_t>::max()));
		}

		Status GetFeature(std::uint32_t a_index, FeatureDescriptor001& a_output) const
		{
			if (!IsOwnerThread())
				return Status::kWrongThread;
			const auto& features = Feature::GetFeatureList();
			if (a_index >= features.size())
				return Status::kFeatureNotFound;
			auto* feature = features[a_index];
			if (!feature)
				return Status::kFeatureNotFound;
			thread_local FeatureStrings strings;
			strings.shortName = feature->GetShortName();
			strings.displayName = feature->GetDisplayName();
			strings.version = feature->version;
			strings.category = feature->GetCategory();
			strings.shaderDefine = feature->GetShaderDefineName();
			strings.loadFailure = feature->failedLoadedMessage;
			const bool disabledAtBoot = globals::state && globals::state->IsFeatureDisabled(strings.shortName);
			a_output = {
				.structSize = sizeof(FeatureDescriptor001),
				.shortName = strings.shortName.c_str(),
				.displayName = strings.displayName.c_str(),
				.version = strings.version.c_str(),
				.category = strings.category.c_str(),
				.shaderDefine = strings.shaderDefine.c_str(),
				.loadFailure = strings.loadFailure.c_str(),
				.loaded = feature->loaded ? 1u : 0u,
				.disabledAtBoot = disabledAtBoot ? 1u : 0u,
				.runtimeDisabledByMissingDependency = feature->IsRuntimeDisabledByMissingDependency() ? 1u : 0u,
				.core = feature->IsCore() ? 1u : 0u,
				.visibleInMenu = feature->IsInMenu() ? 1u : 0u,
				.hiddenFromUser = feature->IsHiddenFromUserView() ? 1u : 0u,
				.supportsVR = feature->SupportsVR() ? 1u : 0u,
				.contributesShaderDefines = strings.shaderDefine.empty() ? 0u : 1u,
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
			const auto decision = EvaluateShaderMutation(BuildPolicyState(mutation), BuildPolicyRequest(mutation));
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
				.restartRequired = decision.restartRequired ? 1u : 0u,
				.shaderRecompileExpected = decision.shaderRecompileExpected ? 1u : 0u,
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
			const auto token = a_request.preflightToken ? std::string(a_request.preflightToken) : std::string{};
			if (token.empty())
				return FillFailure(a_output, Status::kPreflightRequired, "execute requires a successful preflight token");
			const auto found = preflights.find(token);
			if (found == preflights.end())
				return FillFailure(a_output, Status::kPreflightExpired, "preflight token is unknown or expired");
			if (!(found->second.mutation == mutation))
				return FillFailure(a_output, Status::kPreflightMismatch, "execute arguments differ from the preflight request");
			preflights.erase(found);

			const auto decision = EvaluateShaderMutation(BuildPolicyState(mutation), BuildPolicyRequest(mutation));
			if (!decision.allowed)
				return FillFailure(a_output, decision.status, decision.message);

			const auto previousRevision = revision;
			bool changed = false;
			bool pending = false;
			Status status = Status::kSuccess;
			bool persistenceRelevant = false;
			bool forcePersistence = false;
			bool persisted = false;
			auto* cache = globals::shaderCache;
			auto* state = globals::state;
			switch (mutation.action) {
			case CSX::ShaderAPI::MutationAction::kSetCustomShaders:
				persistenceRelevant = true;
				changed = cache->IsEnableRequested() != mutation.boolValue;
				cache->SetEnabled(mutation.boolValue);
				pending = cache->IsEnableRequested() != cache->IsEnabled();
				strings.message = "custom shader request applied";
				break;
			case CSX::ShaderAPI::MutationAction::kSetDiskCache:
				persistenceRelevant = true;
				changed = cache->IsDiskCache() != mutation.boolValue;
				cache->SetDiskCache(mutation.boolValue);
				strings.message = "disk cache setting applied";
				break;
			case CSX::ShaderAPI::MutationAction::kSetAsyncCompilation:
				persistenceRelevant = true;
				changed = cache->IsAsync() != mutation.boolValue;
				cache->SetAsync(mutation.boolValue);
				strings.message = "asynchronous compilation setting applied";
				break;
			case CSX::ShaderAPI::MutationAction::kSetSkipUnchangedShaders:
				persistenceRelevant = true;
				changed = cache->IsSkipUnchangedShaders() != mutation.boolValue;
				cache->SetSkipUnchangedShaders(mutation.boolValue);
				strings.message = "skip-unchanged setting applied";
				break;
			case CSX::ShaderAPI::MutationAction::kSetFeatureDisabledAtBoot:
				persistenceRelevant = true;
				changed = state->IsFeatureDisabled(mutation.featureName) != mutation.boolValue;
				state->SetFeatureDisabled(mutation.featureName, mutation.boolValue);
				strings.message = "feature boot state applied; restart required";
				break;
			case CSX::ShaderAPI::MutationAction::kClearMemoryCache:
				cache->Clear();
				changed = true;
				strings.message = "in-memory shader cache cleared";
				break;
			case CSX::ShaderAPI::MutationAction::kClearDiskCache:
				cache->DeleteDiskCache();
				changed = true;
				strings.message = "active and rollback disk caches cleared";
				break;
			case CSX::ShaderAPI::MutationAction::kClearAllCaches:
				cache->Clear();
				cache->DeleteDiskCache();
				changed = true;
				strings.message = "memory, active disk, rollback, and temporary shader caches cleared";
				break;
			case CSX::ShaderAPI::MutationAction::kRestorePreviousDiskCache:
				persistenceRelevant = true;
				forcePersistence = true;
				changed = cache->RestorePreviousDiskCache();
				status = changed ? Status::kSuccess : Status::kBlocked;
				strings.message = changed ? "previous disk cache restored; restart required" : "previous disk cache restore failed";
				break;
			case CSX::ShaderAPI::MutationAction::kAcceptCacheRebuild:
				cache->AcceptCacheRebuild();
				changed = true;
				strings.message = "cache rebuild accepted";
				break;
			case CSX::ShaderAPI::MutationAction::kStopCompilation:
				changed = cache->IsCompiling();
				cache->StopCompilation();
				strings.message = "shader compilation stop requested";
				break;
			case CSX::ShaderAPI::MutationAction::kCaptureActiveShaders:
				cache->BeginActiveShaderCapture();
				changed = true;
				pending = true;
				strings.message = "active-scene shader capture started";
				break;
			default:
				return FillFailure(a_output, Status::kInvalidArgument, "unknown shader mutation action");
			}

			const bool persistenceRequested = persistenceRelevant &&
				(forcePersistence || (mutation.flags & CSX::ShaderAPI::kMutationPersist) != 0);
			if (persistenceRequested && (!forcePersistence || changed)) {
				persisted = state->Save();
				if (!persisted) {
					status = Status::kPersistenceFailed;
					strings.message += "; live state changed but settings persistence failed";
				} else {
					strings.message += "; settings persisted";
				}
			}

			RefreshRevision(true);
			a_output = {
				.structSize = sizeof(MutationReceipt001),
				.status = status,
				.applied = status == Status::kSuccess || status == Status::kPersistenceFailed ? 1u : 0u,
				.changed = changed ? 1u : 0u,
				.pending = pending ? 1u : 0u,
				.restartRequired = decision.restartRequired ? 1u : 0u,
				.shaderRecompileExpected = decision.shaderRecompileExpected ? 1u : 0u,
				.persistenceRequested = persistenceRequested ? 1u : 0u,
				.persisted = persisted ? 1u : 0u,
				.previousStateRevision = previousRevision,
				.stateRevision = revision,
				.message = strings.message.c_str(),
			};
			return status;
		}

	private:
		std::uint64_t revision = 1;
		std::string fingerprint;
		std::unordered_map<std::string, PendingPreflight> preflights;

		static OwnedMutation CopyMutation(const MutationRequest001& a_request)
		{
			return {
				.action = a_request.action,
				.expectedStateRevision = a_request.expectedStateRevision,
				.flags = a_request.flags,
				.boolValue = a_request.boolValue != 0,
				.featureName = a_request.featureName ? a_request.featureName : "",
			};
		}

		static CSX::Api::ShaderPolicyRequest BuildPolicyRequest(const OwnedMutation& a_mutation)
		{
			return { a_mutation.action, a_mutation.expectedStateRevision, a_mutation.flags, a_mutation.boolValue, a_mutation.featureName };
		}

		CSX::Api::ShaderPolicyState BuildPolicyState(const OwnedMutation& a_mutation) const
		{
			auto* cache = globals::shaderCache;
			return {
				.available = cache && globals::state,
				.compiling = cache && cache->IsCompiling(),
				.previousCacheAvailable = cache && cache->HasPreviousDiskCache(),
				.diskCacheHeld = cache && cache->IsDiskCacheHeld(),
				.featureFound = a_mutation.featureName.empty() ? false : FindFeature(a_mutation.featureName) != nullptr,
				.persistentMutationBlocked = globals::state && globals::state->IsPersistentMutationBlocked(),
				.stateRevision = revision,
			};
		}

		static Feature* FindFeature(std::string_view a_shortName)
		{
			const auto& features = Feature::GetFeatureList();
			const auto found = std::ranges::find_if(features, [&](Feature* a_feature) {
				return a_feature && a_feature->GetShortName() == a_shortName;
			});
			return found == features.end() ? nullptr : *found;
		}

		std::string BuildFingerprint() const
		{
			auto* cache = globals::shaderCache;
			auto* state = globals::state;
			if (!cache || !state)
				return "unavailable";
			std::ostringstream value;
			value << cache->IsEnableRequested() << cache->IsEnabled() << cache->IsDiskCache()
			      << cache->IsDiskCacheActive() << cache->IsDiskCacheHeld() << cache->IsAsync()
			      << cache->IsSkipUnchangedShaders() << cache->HasPreviousDiskCache()
			      << cache->HasFeatureSetChanges() << cache->HasFeatureSetRevertPending();
			for (auto* feature : Feature::GetFeatureList()) {
				if (feature)
					value << '|' << feature->GetShortName() << '=' << state->IsFeatureDisabled(feature->GetShortName());
			}
			return value.str();
		}

		void RefreshRevision(bool a_force = false)
		{
			const auto current = BuildFingerprint();
			if (fingerprint.empty()) {
				fingerprint = current;
				return;
			}
			if (a_force || current != fingerprint) {
				fingerprint = current;
				++revision;
				preflights.clear();
			}
		}

		void TrimPreflights()
		{
			const auto now = std::chrono::steady_clock::now();
			for (auto entry = preflights.begin(); entry != preflights.end();) {
				if (entry->second.expiresAt <= now)
					entry = preflights.erase(entry);
				else
					++entry;
			}
		}

		Status FillFailure(MutationReceipt001& a_output, Status a_status, std::string a_message)
		{
			thread_local ResponseStrings strings;
			strings.message = std::move(a_message);
			a_output = {
				.structSize = sizeof(MutationReceipt001),
				.status = a_status,
				.previousStateRevision = revision,
				.stateRevision = revision,
				.message = strings.message.c_str(),
			};
			return a_status;
		}
	};

	ShaderService& ServiceFrom(const void* a_context)
	{
		return *const_cast<ShaderService*>(static_cast<const ShaderService*>(a_context));
	}

	Status GetSnapshot(const void* a_context, Snapshot001* a_output)
	{
		if (!a_context || !a_output)
			return Status::kInvalidArgument;
		if (a_output->structSize < sizeof(Snapshot001))
			return Status::kStructureTooSmall;
		try { return ServiceFrom(a_context).GetSnapshot(*a_output); }
		catch (...) { return Status::kInternalError; }
	}

	std::uint32_t GetFeatureCount(const void* a_context)
	{
		if (!a_context)
			return 0;
		try { return ServiceFrom(a_context).GetFeatureCount(); }
		catch (...) { return 0; }
	}

	Status GetFeatureDescriptor(const void* a_context, std::uint32_t a_index, FeatureDescriptor001* a_output)
	{
		if (!a_context || !a_output)
			return Status::kInvalidArgument;
		if (a_output->structSize < sizeof(FeatureDescriptor001))
			return Status::kStructureTooSmall;
		try { return ServiceFrom(a_context).GetFeature(a_index, *a_output); }
		catch (...) { return Status::kInternalError; }
	}

	Status Preflight(const void* a_context, const MutationRequest001* a_request, Preflight001* a_output)
	{
		if (!a_context || !a_request || !a_output)
			return Status::kInvalidArgument;
		if (a_request->structSize < sizeof(MutationRequest001) || a_output->structSize < sizeof(Preflight001))
			return Status::kStructureTooSmall;
		try { return ServiceFrom(a_context).Preflight(*a_request, *a_output); }
		catch (...) { return Status::kInternalError; }
	}

	Status Execute(const void* a_context, const MutationRequest001* a_request, MutationReceipt001* a_output)
	{
		if (!a_context || !a_request || !a_output)
			return Status::kInvalidArgument;
		if (a_request->structSize < sizeof(MutationRequest001) || a_output->structSize < sizeof(MutationReceipt001))
			return Status::kStructureTooSmall;
		try { return ServiceFrom(a_context).Execute(*a_request, *a_output); }
		catch (...) { return Status::kInternalError; }
	}

	ShaderService& GetService()
	{
		static ShaderService service;
		return service;
	}
}

namespace CSX::Api
{
	const ShaderAPI::Interface001* GetShaderService001()
	{
		static const ShaderAPI::Interface001 serviceInterface{
			.structSize = sizeof(ShaderAPI::Interface001),
			.major = ShaderAPI::ServiceMajor,
			.minor = ShaderAPI::ServiceMinor,
			.schemaRevision = ShaderAPI::SchemaRevision,
			.capabilities = ShaderAPI::ServiceCapabilities,
			.context = &GetService(),
			.GetSnapshot = ::GetSnapshot,
			.GetFeatureCount = ::GetFeatureCount,
			.GetFeatureDescriptor = ::GetFeatureDescriptor,
			.Preflight = ::Preflight,
			.Execute = ::Execute,
		};
		return &serviceInterface;
	}

	void InitializeShaderService()
	{
		static std::once_flag initialized;
		std::call_once(initialized, [] {
			const auto status = GetProcessServiceRegistry().Register({
				.name = ShaderAPI::ServiceName,
				.major = ShaderAPI::ServiceMajor,
				.minor = ShaderAPI::ServiceMinor,
				.schemaRevision = ShaderAPI::SchemaRevision,
				.capabilities = ServiceAPI::kCapabilityInspection |
					ServiceAPI::kCapabilityRuntimeMutation |
					ServiceAPI::kCapabilityPersistentMutation |
					ServiceAPI::kCapabilityDestructiveOperations |
					ServiceAPI::kCapabilityTransactions,
				.interfacePointer = GetShaderService001(),
			});
			if (status != ServiceAPI::Status::kSuccess)
				logger::error("Failed to register {} service ({})", ShaderAPI::ServiceName, static_cast<std::uint32_t>(status));
		});
	}
}
