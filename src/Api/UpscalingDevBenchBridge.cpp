#include "Api/UpscalingDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Api/ServiceRegistry.h"
#	include "BuildProvenance.h"
#	include "VRAPI/CSserviceapi.h"
#	include "VRAPI/CSupscalingapi.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <algorithm>
#	include <atomic>
#	include <cctype>
#	include <limits>
#	include <stdexcept>
#	include <string>
#	include <string_view>
#	include <vector>

namespace
{
	using json = nlohmann::json;
	using namespace CSX;

	std::atomic_bool g_installAttempted{ false };
	std::atomic_bool g_registered{ false };

	std::string Copy(const char* a_value)
	{
		return a_value ? a_value : "";
	}

	std::string Lower(std::string a_value)
	{
		std::transform(a_value.begin(), a_value.end(), a_value.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return a_value;
	}

	const char* ToString(ServiceAPI::Status a_status)
	{
		switch (a_status) {
		case ServiceAPI::Status::kSuccess: return "success";
		case ServiceAPI::Status::kInvalidArgument: return "invalid_argument";
		case ServiceAPI::Status::kStructureTooSmall: return "structure_too_small";
		case ServiceAPI::Status::kIncompatibleRegistryVersion: return "incompatible_registry_version";
		case ServiceAPI::Status::kServiceNotFound: return "service_not_found";
		case ServiceAPI::Status::kIncompatibleServiceVersion: return "incompatible_service_version";
		case ServiceAPI::Status::kMissingCapabilities: return "missing_capabilities";
		case ServiceAPI::Status::kAlreadyRegistered: return "already_registered";
		case ServiceAPI::Status::kInternalError: return "internal_error";
		}
		return "unknown";
	}

	const char* ToString(UpscalingAPI::Status a_status)
	{
		switch (a_status) {
		case UpscalingAPI::Status::kSuccess: return "success";
		case UpscalingAPI::Status::kInvalidArgument: return "invalid_argument";
		case UpscalingAPI::Status::kStructureTooSmall: return "structure_too_small";
		case UpscalingAPI::Status::kUnsupportedRuntime: return "unsupported_runtime";
		case UpscalingAPI::Status::kUnsupportedProfile: return "unsupported_profile";
		case UpscalingAPI::Status::kServiceUnavailable: return "service_unavailable";
		case UpscalingAPI::Status::kStateConflict: return "state_conflict";
		case UpscalingAPI::Status::kBlocked: return "blocked";
		case UpscalingAPI::Status::kBusy: return "busy";
		case UpscalingAPI::Status::kIdempotencyConflict: return "idempotency_conflict";
		case UpscalingAPI::Status::kOperationNotFound: return "operation_not_found";
		case UpscalingAPI::Status::kBufferTooSmall: return "buffer_too_small";
		case UpscalingAPI::Status::kInternalError: return "internal_error";
		}
		return "unknown";
	}

	template <class T>
	json Named(T a_value, const char* a_name)
	{
		return { { "value", static_cast<std::uint32_t>(a_value) }, { "name", a_name } };
	}

	json Named(UpscalingAPI::Status a_value)
	{
		return Named(a_value, ToString(a_value));
	}

	json Named(ServiceAPI::Status a_value)
	{
		return Named(a_value, ToString(a_value));
	}

	const char* ToString(UpscalingAPI::Method a_value)
	{
		switch (a_value) {
		case UpscalingAPI::Method::kNone: return "none";
		case UpscalingAPI::Method::kTAA: return "taa";
		case UpscalingAPI::Method::kFSR: return "fsr";
		case UpscalingAPI::Method::kDLSS: return "dlss";
		}
		return "unknown";
	}

	const char* ToString(UpscalingAPI::QualityMode a_value)
	{
		switch (a_value) {
		case UpscalingAPI::QualityMode::kNativeAA: return "native_aa";
		case UpscalingAPI::QualityMode::kHoshipa: return "hoshipa";
		case UpscalingAPI::QualityMode::kUltraQuality: return "ultra_quality";
		case UpscalingAPI::QualityMode::kQuality: return "quality";
		case UpscalingAPI::QualityMode::kBalanced: return "balanced";
		case UpscalingAPI::QualityMode::kPerformance: return "performance";
		case UpscalingAPI::QualityMode::kUltraPerformance: return "ultra_performance";
		}
		return "unknown";
	}

	const char* ToString(UpscalingAPI::DLSSProfile a_value)
	{
		switch (a_value) {
		case UpscalingAPI::DLSSProfile::kJ: return "J";
		case UpscalingAPI::DLSSProfile::kK: return "K";
		case UpscalingAPI::DLSSProfile::kL: return "L";
		case UpscalingAPI::DLSSProfile::kM: return "M";
		case UpscalingAPI::DLSSProfile::kF: return "F";
		case UpscalingAPI::DLSSProfile::kE: return "E";
		}
		return "unknown";
	}

	const char* ToString(UpscalingAPI::FSRRuntime a_value)
	{
		switch (a_value) {
		case UpscalingAPI::FSRRuntime::kFSR3: return "fsr3";
		case UpscalingAPI::FSRRuntime::kFSR4: return "fsr4";
		}
		return "unknown";
	}

	#define CSX_ENUM_STRING_FUNCTION(EnumType, ...) \
		const char* ToString(EnumType a_value) { switch (a_value) { __VA_ARGS__ } return "unknown"; }

	CSX_ENUM_STRING_FUNCTION(UpscalingAPI::RuntimeKind,
		case UpscalingAPI::RuntimeKind::kUnknown: return "unknown";
		case UpscalingAPI::RuntimeKind::kSkyrimSE: return "skyrim_se";
		case UpscalingAPI::RuntimeKind::kSkyrimAE: return "skyrim_ae";
		case UpscalingAPI::RuntimeKind::kSkyrimVR: return "skyrim_vr";)
	CSX_ENUM_STRING_FUNCTION(UpscalingAPI::RenderScaleStatus,
		case UpscalingAPI::RenderScaleStatus::kDisabled: return "disabled";
		case UpscalingAPI::RenderScaleStatus::kIneligibleMethod: return "ineligible_method";
		case UpscalingAPI::RenderScaleStatus::kNativeQuality: return "native_quality";
		case UpscalingAPI::RenderScaleStatus::kRuntimeBlocked: return "runtime_blocked";
		case UpscalingAPI::RenderScaleStatus::kPendingRelatch: return "pending_relatch";
		case UpscalingAPI::RenderScaleStatus::kActive: return "active";
		case UpscalingAPI::RenderScaleStatus::kRestartRequired: return "restart_required";)
	CSX_ENUM_STRING_FUNCTION(UpscalingAPI::TransitionState,
		case UpscalingAPI::TransitionState::kIdle: return "idle";
		case UpscalingAPI::TransitionState::kRequested: return "requested";
		case UpscalingAPI::TransitionState::kWaitingForSafePoint: return "waiting_for_safe_point";
		case UpscalingAPI::TransitionState::kPreparing: return "preparing";
		case UpscalingAPI::TransitionState::kApplying: return "applying";
		case UpscalingAPI::TransitionState::kStabilizing: return "stabilizing";
		case UpscalingAPI::TransitionState::kActive: return "active";)
	CSX_ENUM_STRING_FUNCTION(UpscalingAPI::PreflightDecision,
		case UpscalingAPI::PreflightDecision::kNoChange: return "no_change";
		case UpscalingAPI::PreflightDecision::kApplySynchronously: return "apply_synchronously";
		case UpscalingAPI::PreflightDecision::kQueue: return "queue";
		case UpscalingAPI::PreflightDecision::kBlocked: return "blocked";
		case UpscalingAPI::PreflightDecision::kUnsupported: return "unsupported";)
	CSX_ENUM_STRING_FUNCTION(UpscalingAPI::AdmissionRoute,
		case UpscalingAPI::AdmissionRoute::kNone: return "none";
		case UpscalingAPI::AdmissionRoute::kDirect: return "direct";
		case UpscalingAPI::AdmissionRoute::kDeferredSafePoint: return "deferred_safe_point";
		case UpscalingAPI::AdmissionRoute::kLoadingDoorHandoff: return "loading_door_handoff";)
	CSX_ENUM_STRING_FUNCTION(UpscalingAPI::ApplyDisposition,
		case UpscalingAPI::ApplyDisposition::kRejected: return "rejected";
		case UpscalingAPI::ApplyDisposition::kNoChange: return "no_change";
		case UpscalingAPI::ApplyDisposition::kAppliedSynchronously: return "applied_synchronously";
		case UpscalingAPI::ApplyDisposition::kQueued: return "queued";)
	CSX_ENUM_STRING_FUNCTION(UpscalingAPI::OperationState,
		case UpscalingAPI::OperationState::kQueued: return "queued";
		case UpscalingAPI::OperationState::kWaitingForSafePoint: return "waiting_for_safe_point";
		case UpscalingAPI::OperationState::kPreparing: return "preparing";
		case UpscalingAPI::OperationState::kApplying: return "applying";
		case UpscalingAPI::OperationState::kStabilizing: return "stabilizing";
		case UpscalingAPI::OperationState::kCompleted: return "completed";
		case UpscalingAPI::OperationState::kFailed: return "failed";
		case UpscalingAPI::OperationState::kSuperseded: return "superseded";)
	CSX_ENUM_STRING_FUNCTION(UpscalingAPI::EventType,
		case UpscalingAPI::EventType::kAccepted: return "accepted";
		case UpscalingAPI::EventType::kQueued: return "queued";
		case UpscalingAPI::EventType::kStateChanged: return "state_changed";
		case UpscalingAPI::EventType::kCompleted: return "completed";
		case UpscalingAPI::EventType::kFailed: return "failed";
		case UpscalingAPI::EventType::kSuperseded: return "superseded";
		case UpscalingAPI::EventType::kPersisted: return "persisted";)

	#undef CSX_ENUM_STRING_FUNCTION

	template <class T>
	json Named(T a_value)
	{
		return Named(a_value, ToString(a_value));
	}

	UpscalingAPI::Method ParseMethod(const json& a_value)
	{
		const auto value = Lower(a_value.get<std::string>());
		if (value == "none") return UpscalingAPI::Method::kNone;
		if (value == "taa") return UpscalingAPI::Method::kTAA;
		if (value == "fsr") return UpscalingAPI::Method::kFSR;
		if (value == "dlss") return UpscalingAPI::Method::kDLSS;
		throw std::runtime_error("target.method must be none, taa, fsr, or dlss");
	}

	UpscalingAPI::QualityMode ParseQuality(const json& a_value)
	{
		const auto value = Lower(a_value.get<std::string>());
		if (value == "native_aa") return UpscalingAPI::QualityMode::kNativeAA;
		if (value == "hoshipa") return UpscalingAPI::QualityMode::kHoshipa;
		if (value == "ultra_quality") return UpscalingAPI::QualityMode::kUltraQuality;
		if (value == "quality") return UpscalingAPI::QualityMode::kQuality;
		if (value == "balanced") return UpscalingAPI::QualityMode::kBalanced;
		if (value == "performance") return UpscalingAPI::QualityMode::kPerformance;
		if (value == "ultra_performance") return UpscalingAPI::QualityMode::kUltraPerformance;
		throw std::runtime_error("target.qualityMode is invalid");
	}

	UpscalingAPI::DLSSProfile ParseDLSSProfile(const json& a_value)
	{
		const auto value = Lower(a_value.get<std::string>());
		if (value == "j") return UpscalingAPI::DLSSProfile::kJ;
		if (value == "k") return UpscalingAPI::DLSSProfile::kK;
		if (value == "l") return UpscalingAPI::DLSSProfile::kL;
		if (value == "m") return UpscalingAPI::DLSSProfile::kM;
		if (value == "f") return UpscalingAPI::DLSSProfile::kF;
		if (value == "e") return UpscalingAPI::DLSSProfile::kE;
		throw std::runtime_error("target.dlssProfile must be J, K, L, M, F, or E");
	}

	UpscalingAPI::FSRRuntime ParseFSRRuntime(const json& a_value)
	{
		const auto value = Lower(a_value.get<std::string>());
		if (value == "fsr3") return UpscalingAPI::FSRRuntime::kFSR3;
		if (value == "fsr4") return UpscalingAPI::FSRRuntime::kFSR4;
		throw std::runtime_error("target.fsrRuntime must be fsr3 or fsr4");
	}

	UpscalingAPI::Profile001 ParseProfile(const json& a_value)
	{
		if (!a_value.is_object())
			throw std::runtime_error("target must be an object");
		UpscalingAPI::Profile001 profile;
		profile.method = ParseMethod(a_value.at("method"));
		profile.qualityMode = ParseQuality(a_value.at("qualityMode"));
		profile.renderScaleMode = a_value.value("renderScaleMode", false) ? 1u : 0u;
		if (a_value.contains("dlssProfile"))
			profile.dlssProfile = ParseDLSSProfile(a_value.at("dlssProfile"));
		if (a_value.contains("fsrRuntime"))
			profile.fsrRuntime = ParseFSRRuntime(a_value.at("fsrRuntime"));
		return profile;
	}

	json Profile(const UpscalingAPI::Profile001& a_value)
	{
		return {
			{ "method", Named(a_value.method) },
			{ "qualityMode", Named(a_value.qualityMode) },
			{ "renderScaleMode", a_value.renderScaleMode != 0 },
			{ "dlssProfile", Named(a_value.dlssProfile) },
			{ "fsrRuntime", Named(a_value.fsrRuntime) },
		};
	}

	json ProducerIdentity(const ServiceAPI::ProducerIdentity001& a_value)
	{
		return {
			{ "component", Copy(a_value.component) },
			{ "buildId", Copy(a_value.buildId) },
			{ "artifactSha256", Copy(a_value.artifactSha256) },
			{ "sourceCommit", Copy(a_value.sourceCommit) },
			{ "sourceDescribe", Copy(a_value.sourceDescribe) },
			{ "configuration", Copy(a_value.configuration) },
			{ "shaderCacheAbiId", Copy(a_value.shaderCacheAbiId) },
			{ "shaderCompilerIdentity", Copy(a_value.shaderCompilerIdentity) },
			{ "manifestError", Copy(a_value.manifestError) },
			{ "sourceDirty", a_value.sourceDirty != 0 },
			{ "manifestVerified", a_value.manifestVerified != 0 },
		};
	}

	json Descriptor(const ServiceAPI::ServiceDescriptor001& a_value)
	{
		return {
			{ "name", Copy(a_value.name) },
			{ "major", a_value.major },
			{ "minor", a_value.minor },
			{ "schemaRevision", a_value.schemaRevision },
			{ "capabilities", a_value.capabilities },
		};
	}

	json Conditions(std::uint64_t a_mask)
	{
		json names = json::array();
		static constexpr std::pair<UpscalingAPI::Condition, const char*> values[]{
			{ UpscalingAPI::kConditionRaceSexMenu, "race_sex_menu" },
			{ UpscalingAPI::kConditionRaceSexStartupTail, "race_sex_startup_tail" },
			{ UpscalingAPI::kConditionLoadingTransition, "loading_transition" },
			{ UpscalingAPI::kConditionRelatchPending, "relatch_pending" },
			{ UpscalingAPI::kConditionTransitionPending, "transition_pending" },
			{ UpscalingAPI::kConditionOpenCompositeUpscaling, "open_composite_upscaling" },
			{ UpscalingAPI::kConditionFirstWorldFramePending, "first_world_frame_pending" },
			{ UpscalingAPI::kConditionPostLoadRecovery, "post_load_recovery" },
			{ UpscalingAPI::kConditionProviderCheckPending, "provider_check_pending" },
			{ UpscalingAPI::kConditionProviderUnavailable, "provider_unavailable" },
			{ UpscalingAPI::kConditionRestartRequired, "restart_required" },
			{ UpscalingAPI::kConditionPersistenceUnavailable, "persistence_unavailable" },
			{ UpscalingAPI::kConditionResourceRecovery, "resource_recovery" },
		};
		for (const auto& [flag, name] : values) {
			if ((a_mask & static_cast<std::uint64_t>(flag)) != 0)
				names.push_back(name);
		}
		return { { "mask", a_mask }, { "names", std::move(names) } };
	}

	json QueryRegistry(const ServiceAPI::Registry001*& a_registry, const UpscalingAPI::Interface001*& a_api)
	{
		a_registry = Api::GetNativeServiceRegistry001();
		if (!a_registry)
			return { { "error", "native service registry unavailable" } };

		ServiceAPI::ProducerIdentity001 identity;
		const auto identityStatus = a_registry->GetProducerIdentity(a_registry->context, &identity);
		ServiceAPI::ServiceQuery001 query;
		query.name = UpscalingAPI::ServiceName;
		query.major = UpscalingAPI::ServiceMajor;
		query.minimumMinor = UpscalingAPI::ServiceMinor;
		query.maximumMinor = UpscalingAPI::ServiceMinor;
		query.requiredCapabilities = ServiceAPI::kCapabilityInspection;
		const void* outputInterface = nullptr;
		ServiceAPI::ServiceDescriptor001 descriptor;
		const auto queryStatus = a_registry->QueryService(
			a_registry->context, &query, &outputInterface, &descriptor);
		a_api = queryStatus == ServiceAPI::Status::kSuccess ?
			static_cast<const UpscalingAPI::Interface001*>(outputInterface) : nullptr;

		json result{
			{ "registry", {
				{ "abiMajor", a_registry->abiMajor },
				{ "abiMinor", a_registry->abiMinor },
				{ "structSize", a_registry->structSize },
				{ "identityStatus", Named(identityStatus) },
				{ "producer", ProducerIdentity(identity) },
				{ "queryStatus", Named(queryStatus) },
				{ "service", Descriptor(descriptor) },
			} },
		};
		if (a_api) {
			result["registry"]["interface"] = {
				{ "structSize", a_api->structSize },
				{ "abiMajor", a_api->abiMajor },
				{ "abiMinor", a_api->abiMinor },
			};
		}
		return result;
	}

	json RegistryInventory()
	{
		const auto* registry = Api::GetNativeServiceRegistry001();
		if (!registry)
			return { { "error", "native service registry unavailable" } };
		ServiceAPI::ProducerIdentity001 identity;
		const auto identityStatus = registry->GetProducerIdentity(registry->context, &identity);
		json services = json::array();
		const auto count = registry->GetServiceCount(registry->context);
		for (std::uint32_t i = 0; i < count; ++i) {
			ServiceAPI::ServiceDescriptor001 descriptor;
			const auto status = registry->GetServiceDescriptor(registry->context, i, &descriptor);
			services.push_back({ { "index", i }, { "status", Named(status) }, { "descriptor", Descriptor(descriptor) } });
		}
		return {
			{ "action", "registry" },
			{ "registry", {
				{ "structSize", registry->structSize },
				{ "abiMajor", registry->abiMajor },
				{ "abiMinor", registry->abiMinor },
				{ "identityStatus", Named(identityStatus) },
				{ "producer", ProducerIdentity(identity) },
				{ "serviceCount", count },
				{ "services", std::move(services) },
			} },
		};
	}

	json Snapshot(const UpscalingAPI::Snapshot001& a_value)
	{
		return {
			{ "stateRevision", a_value.stateRevision },
			{ "capabilityRevision", a_value.capabilityRevision },
			{ "profilePresence", a_value.profilePresence },
			{ "flags", a_value.flags },
			{ "observedConditions", Conditions(a_value.observedConditions) },
			{ "transitionState", Named(a_value.transitionState) },
			{ "renderScaleStatus", Named(a_value.renderScaleStatus) },
			{ "activeOperationId", a_value.activeOperationId },
			{ "profiles", {
				{ "configured", Profile(a_value.configured) },
				{ "requested", Profile(a_value.requested) },
				{ "applying", Profile(a_value.applying) },
				{ "effective", Profile(a_value.effective) },
				{ "stable", Profile(a_value.stable) },
				{ "persisted", Profile(a_value.persisted) },
			} },
			{ "dimensions", {
				{ "displayEyeWidth", a_value.displayEyeWidth },
				{ "displayEyeHeight", a_value.displayEyeHeight },
				{ "renderEyeWidth", a_value.renderEyeWidth },
				{ "renderEyeHeight", a_value.renderEyeHeight },
			} },
		};
	}

	std::uint64_t ExpectedRevision(const json& a_args)
	{
		return a_args.contains("expectedStateRevision") ?
			a_args.at("expectedStateRevision").get<std::uint64_t>() : UpscalingAPI::AnyStateRevision;
	}

	UpscalingAPI::RequestPurpose Purpose(const json& a_args)
	{
		const auto value = Lower(a_args.value("purpose", std::string("direct")));
		if (value == "direct") return UpscalingAPI::RequestPurpose::kDirect;
		if (value == "environment_profile_transition") return UpscalingAPI::RequestPurpose::kEnvironmentProfileTransition;
		throw std::runtime_error("purpose must be direct or environment_profile_transition");
	}

	UpscalingAPI::PersistencePolicy Persistence(const json& a_args)
	{
		const auto value = Lower(a_args.value("persistence", std::string("runtime_only")));
		if (value == "runtime_only") return UpscalingAPI::PersistencePolicy::kRuntimeOnly;
		if (value == "persist_when_stable") return UpscalingAPI::PersistencePolicy::kPersistWhenStable;
		throw std::runtime_error("persistence must be runtime_only or persist_when_stable");
	}

	json BuildResult(const json& a_args)
	{
		const auto action = Lower(a_args.value("action", std::string("snapshot")));
		if (action == "registry")
			return RegistryInventory();

		const ServiceAPI::Registry001* registry = nullptr;
		const UpscalingAPI::Interface001* api = nullptr;
		auto output = QueryRegistry(registry, api);
		output["action"] = action;
		if (!api)
			return output;

		if (action == "capabilities") {
			UpscalingAPI::Capabilities001 value;
			const auto status = api->GetCapabilities(api->context, &value);
			output["status"] = Named(status);
			output["capabilities"] = {
				{ "revision", value.revision },
				{ "runtime", Named(value.runtime) },
				{ "capabilityMask", value.capabilities },
				{ "supportedMethodMask", value.supportedMethodMask },
				{ "availableMethodMask", value.availableMethodMask },
				{ "pendingMethodMask", value.pendingMethodMask },
				{ "supportedQualityModeMask", value.supportedQualityModeMask },
				{ "supportedDLSSProfileMask", value.supportedDLSSProfileMask },
				{ "supportedFSRRuntimeMask", value.supportedFSRRuntimeMask },
				{ "methodUnavailableConditions", json::array({ Conditions(value.methodUnavailableConditions[0]), Conditions(value.methodUnavailableConditions[1]), Conditions(value.methodUnavailableConditions[2]), Conditions(value.methodUnavailableConditions[3]) }) },
				{ "fsrRuntimeUnavailableConditions", json::array({ Conditions(value.fsrRuntimeUnavailableConditions[0]), Conditions(value.fsrRuntimeUnavailableConditions[1]) }) },
				{ "qualityResolutionScales", value.qualityResolutionScales },
				{ "maximumEventPageSize", value.maximumEventPageSize },
				{ "eventRetentionCapacity", value.eventRetentionCapacity },
				{ "commandRetentionMilliseconds", value.commandRetentionMilliseconds },
			};
			return output;
		}

		if (action == "snapshot") {
			UpscalingAPI::Snapshot001 value;
			const auto status = api->GetSnapshot(api->context, &value);
			output["status"] = Named(status);
			output["snapshot"] = Snapshot(value);
			return output;
		}

		if (action == "preflight") {
			UpscalingAPI::PreflightRequest001 request;
			request.expectedStateRevision = ExpectedRevision(a_args);
			request.purpose = Purpose(a_args);
			request.persistence = Persistence(a_args);
			request.target = ParseProfile(a_args.at("target"));
			UpscalingAPI::PreflightResult001 result;
			const auto status = api->PreflightProfile(api->context, &request, &result);
			output["status"] = Named(status);
			output["preflight"] = {
				{ "evaluatedStateRevision", result.evaluatedStateRevision },
				{ "decision", Named(result.decision) },
				{ "admissionRoute", Named(result.admissionRoute) },
				{ "observedConditions", Conditions(result.observedConditions) },
				{ "blockingConditions", Conditions(result.blockingConditions) },
				{ "retryable", result.retryable != 0 },
				{ "requiresRestart", result.requiresRestart != 0 },
				{ "willPersist", result.willPersist != 0 },
				{ "normalizedTarget", Profile(result.normalizedTarget) },
				{ "predictedDimensions", {
					{ "displayEyeWidth", result.predictedDisplayEyeWidth },
					{ "displayEyeHeight", result.predictedDisplayEyeHeight },
					{ "renderEyeWidth", result.predictedRenderEyeWidth },
					{ "renderEyeHeight", result.predictedRenderEyeHeight },
				} },
			};
			return output;
		}

		if (action == "apply") {
			if (!a_args.contains("expectedBuildId"))
				throw std::runtime_error("apply requires expectedBuildId");
			const auto clientId = a_args.at("clientId").get<std::string>();
			const auto commandId = a_args.at("commandId").get<std::string>();
			const auto reason = a_args.value("reason", std::string("DevBench upscaling API test"));
			UpscalingAPI::ApplyRequest001 request;
			request.clientId = clientId.c_str();
			request.commandId = commandId.c_str();
			request.reason = reason.c_str();
			request.expectedStateRevision = ExpectedRevision(a_args);
			request.purpose = Purpose(a_args);
			request.persistence = Persistence(a_args);
			request.target = ParseProfile(a_args.at("target"));
			UpscalingAPI::ApplyResult001 result;
			const auto callStatus = api->ApplyProfile(api->context, &request, &result);
			output["status"] = Named(callStatus);
			output["apply"] = {
				{ "resultStatus", Named(result.status) },
				{ "disposition", Named(result.disposition) },
				{ "admissionRoute", Named(result.admissionRoute) },
				{ "idempotentReplay", result.idempotentReplay != 0 },
				{ "retryable", result.retryable != 0 },
				{ "requiresRestart", result.requiresRestart != 0 },
				{ "willPersist", result.willPersist != 0 },
				{ "admittedStateRevision", result.admittedStateRevision },
				{ "resultingStateRevision", result.resultingStateRevision },
				{ "operationId", result.operationId },
				{ "observedConditions", Conditions(result.observedConditions) },
				{ "blockingConditions", Conditions(result.blockingConditions) },
				{ "normalizedTarget", Profile(result.normalizedTarget) },
			};
			return output;
		}

		if (action == "operation") {
			const auto operationId = a_args.at("operationId").get<std::uint64_t>();
			UpscalingAPI::OperationSnapshot001 result;
			const auto status = api->GetOperation(api->context, operationId, &result);
			output["status"] = Named(status);
			output["operation"] = {
				{ "operationId", result.operationId },
				{ "state", Named(result.state) },
				{ "result", Named(result.result) },
				{ "flags", result.flags },
				{ "acceptedStateRevision", result.acceptedStateRevision },
				{ "latestStateRevision", result.latestStateRevision },
				{ "observedConditions", Conditions(result.observedConditions) },
				{ "blockingConditions", Conditions(result.blockingConditions) },
				{ "eventIndex", result.eventIndex },
				{ "target", Profile(result.target) },
				{ "effective", Profile(result.effective) },
			};
			return output;
		}

		if (action == "events") {
			UpscalingAPI::EventQuery001 query;
			query.afterEventId = a_args.value("afterEventId", std::uint64_t{ 0 });
			query.operationId = a_args.value("operationId", std::uint64_t{ 0 });
			query.limit = std::clamp(a_args.value("limit", 100u), 1u, 500u);
			std::vector<UpscalingAPI::Event001> events(query.limit);
			UpscalingAPI::EventPage001 page;
			const auto status = api->ReadEvents(api->context, &query, events.data(), static_cast<std::uint32_t>(events.size()), &page);
			json values = json::array();
			for (std::uint32_t i = 0; i < page.returnedEventCount && i < events.size(); ++i) {
				const auto& event = events[i];
				values.push_back({
					{ "eventId", event.eventId },
					{ "operationId", event.operationId },
					{ "eventIndex", event.eventIndex },
					{ "stateRevision", event.stateRevision },
					{ "type", Named(event.type) },
					{ "operationState", Named(event.operationState) },
					{ "result", Named(event.result) },
					{ "observedConditions", Conditions(event.observedConditions) },
				});
			}
			output["status"] = Named(status);
			output["eventPage"] = {
				{ "returnedEventCount", page.returnedEventCount },
				{ "oldestRetainedEventId", page.oldestRetainedEventId },
				{ "latestEventId", page.latestEventId },
				{ "nextEventId", page.nextEventId },
				{ "cursorExpired", page.cursorExpired != 0 },
				{ "moreAvailable", page.moreAvailable != 0 },
				{ "events", std::move(values) },
			};
			return output;
		}

		return {
			{ "error", "unknown action" },
			{ "action", action },
			{ "supported", json::array({ "registry", "capabilities", "snapshot", "preflight", "apply", "operation", "events" }) },
		};
	}

	void ToolHandler(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
	{
		json output;
		try {
			json args = json::object();
			if (a_argsJson && *a_argsJson)
				args = json::parse(a_argsJson);
			if (!args.is_object())
				throw std::runtime_error("arguments must be a JSON object");
			if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args))
				output = std::move(*mismatch);
			else
				output = BuildResult(args);
		} catch (const std::exception& e) {
			output = { { "error", "invalid request" }, { "detail", e.what() } };
		} catch (...) {
			output = { { "error", "unknown handler error" } };
		}
		BuildProvenance::AttachProducer(output);
		try {
			const auto serialized = output.dump();
			a_write(a_sink, serialized.c_str());
		} catch (...) {
			a_write(a_sink, R"({"error":"response serialization failed"})");
		}
	}
}

namespace CSX::Api::UpscalingDevBenchBridge
{
	void Install()
	{
		if (g_registered.load(std::memory_order_acquire))
			return;
		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench) {
			logger::info("UpscalingDevBenchBridge: devbench host not present yet; registration remains retryable");
			return;
		}
		if (g_installAttempted.exchange(true, std::memory_order_acq_rel))
			return;
		static constexpr const char* descriptor =
			R"({"description":"Exercise the registered public csx.upscaling ABI through DevBench. Mutations require exact build identity and idempotency keys.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["registry","capabilities","snapshot","preflight","apply","operation","events"],"default":"snapshot"},"expectedBuildId":{"type":"string"},"expectedStateRevision":{"type":"integer","minimum":0},"target":{"type":"object","properties":{"method":{"type":"string","enum":["none","taa","fsr","dlss"]},"qualityMode":{"type":"string","enum":["native_aa","hoshipa","ultra_quality","quality","balanced","performance","ultra_performance"]},"renderScaleMode":{"type":"boolean"},"dlssProfile":{"type":"string","enum":["J","K","L","M","F","E"]},"fsrRuntime":{"type":"string","enum":["fsr3","fsr4"]}},"required":["method","qualityMode"]},"purpose":{"type":"string","enum":["direct","environment_profile_transition"]},"persistence":{"type":"string","enum":["runtime_only","persist_when_stable"]},"clientId":{"type":"string"},"commandId":{"type":"string"},"reason":{"type":"string"},"operationId":{"type":"integer","minimum":0},"afterEventId":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":500}}}})";
		devBench->RegisterTool("communityshaders.upscaling_api", descriptor, &ToolHandler, nullptr);
		g_registered.store(true, std::memory_order_release);
		logger::info("UpscalingDevBenchBridge: registered communityshaders.upscaling_api with devbench build {}", devBench->GetBuildNumber());
	}

	bool IsBuilt() { return true; }
	bool IsRegistered() { return g_registered.load(std::memory_order_acquire); }
}

#else

namespace CSX::Api::UpscalingDevBenchBridge
{
	void Install() {}
	bool IsBuilt() { return false; }
	bool IsRegistered() { return false; }
}

#endif
