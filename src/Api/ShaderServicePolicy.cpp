#include "Api/ShaderServicePolicy.h"

namespace CSX::Api
{
	ShaderPolicyDecision EvaluateShaderMutation(
		const ShaderPolicyState& a_state,
		const ShaderPolicyRequest& a_request)
	{
		ShaderPolicyDecision decision;
		if (!a_state.available) {
			decision.status = ShaderAPI::Status::kUnavailable;
			decision.allowed = false;
			decision.reasonCode = "service_unavailable";
			decision.message = "shader state is not initialized";
			return decision;
		}
		constexpr std::uint64_t knownFlags = ShaderAPI::kMutationPersist |
			ShaderAPI::kMutationAllowDisruptive | ShaderAPI::kMutationAllowDestructive;
		if ((a_request.flags & ~knownFlags) != 0) {
			decision.status = ShaderAPI::Status::kInvalidArgument;
			decision.allowed = false;
			decision.reasonCode = "unknown_mutation_flags";
			decision.message = "mutation flags contain bits not defined by shader API v1";
			return decision;
		}
		const bool persistenceFlagAllowed =
			a_request.action == ShaderAPI::MutationAction::kSetCustomShaders ||
			a_request.action == ShaderAPI::MutationAction::kSetDiskCache ||
			a_request.action == ShaderAPI::MutationAction::kSetAsyncCompilation ||
			a_request.action == ShaderAPI::MutationAction::kSetSkipUnchangedShaders ||
			a_request.action == ShaderAPI::MutationAction::kSetFeatureDisabledAtBoot;
		if ((a_request.flags & ShaderAPI::kMutationPersist) && !persistenceFlagAllowed) {
			decision.status = ShaderAPI::Status::kInvalidArgument;
			decision.allowed = false;
			decision.reasonCode = "persistence_not_applicable";
			decision.message = "persist is not defined for this shader API action";
			return decision;
		}
		if (a_request.action != ShaderAPI::MutationAction::kSetFeatureDisabledAtBoot &&
			!a_request.featureName.empty()) {
			decision.status = ShaderAPI::Status::kInvalidArgument;
			decision.allowed = false;
			decision.reasonCode = "feature_name_not_applicable";
			decision.message = "featureName is only valid for feature boot-state mutations";
			return decision;
		}
		if (a_request.expectedStateRevision != 0 &&
			a_request.expectedStateRevision != a_state.stateRevision) {
			decision.status = ShaderAPI::Status::kRevisionConflict;
			decision.allowed = false;
			decision.reasonCode = "state_revision_conflict";
			decision.message = "shader state changed after the client snapshot";
			return decision;
		}
		const bool persistenceRequested =
			(a_request.flags & ShaderAPI::kMutationPersist) != 0 ||
			a_request.action == ShaderAPI::MutationAction::kRestorePreviousDiskCache;
		if (persistenceRequested && a_state.persistentMutationBlocked) {
			decision.status = ShaderAPI::Status::kBusy;
			decision.allowed = false;
			decision.reasonCode = "persistent_mutation_blocked";
			decision.message = "settings persistence is temporarily blocked by save/load activity";
			return decision;
		}

		switch (a_request.action) {
		case ShaderAPI::MutationAction::kSetCustomShaders:
			decision.disruptive = true;
			decision.shaderRecompileExpected = a_request.boolValue;
			break;
		case ShaderAPI::MutationAction::kSetDiskCache:
		case ShaderAPI::MutationAction::kSetAsyncCompilation:
		case ShaderAPI::MutationAction::kSetSkipUnchangedShaders:
			break;
		case ShaderAPI::MutationAction::kSetFeatureDisabledAtBoot:
			if (a_request.featureName.empty() || !a_state.featureFound) {
				decision.status = ShaderAPI::Status::kFeatureNotFound;
				decision.allowed = false;
				decision.reasonCode = "feature_not_found";
				decision.message = "featureName does not identify a known CSX feature";
				return decision;
			}
			decision.restartRequired = true;
			decision.shaderRecompileExpected = true;
			break;
		case ShaderAPI::MutationAction::kClearMemoryCache:
		case ShaderAPI::MutationAction::kCaptureActiveShaders:
		case ShaderAPI::MutationAction::kStopCompilation:
			decision.disruptive = true;
			decision.shaderRecompileExpected = a_request.action != ShaderAPI::MutationAction::kStopCompilation;
			break;
		case ShaderAPI::MutationAction::kClearDiskCache:
		case ShaderAPI::MutationAction::kClearAllCaches:
			decision.disruptive = true;
			decision.destructive = true;
			decision.shaderRecompileExpected = a_request.action == ShaderAPI::MutationAction::kClearAllCaches;
			break;
		case ShaderAPI::MutationAction::kRestorePreviousDiskCache:
			decision.disruptive = true;
			decision.destructive = true;
			decision.restartRequired = true;
			if (a_state.compiling) {
				decision.status = ShaderAPI::Status::kBusy;
				decision.allowed = false;
				decision.reasonCode = "compilation_in_progress";
				decision.message = "the previous cache cannot be restored while shaders are compiling";
				return decision;
			}
			if (!a_state.previousCacheAvailable) {
				decision.status = ShaderAPI::Status::kBlocked;
				decision.allowed = false;
				decision.reasonCode = "previous_cache_unavailable";
				decision.message = "no compatible previous shader cache is available";
				return decision;
			}
			break;
		case ShaderAPI::MutationAction::kAcceptCacheRebuild:
			decision.disruptive = true;
			decision.shaderRecompileExpected = true;
			if (!a_state.diskCacheHeld) {
				decision.status = ShaderAPI::Status::kBlocked;
				decision.allowed = false;
				decision.reasonCode = "cache_not_held";
				decision.message = "the active cache is not awaiting a rebuild decision";
				return decision;
			}
			break;
		default:
			decision.status = ShaderAPI::Status::kInvalidArgument;
			decision.allowed = false;
			decision.reasonCode = "unknown_action";
			decision.message = "mutation action is not defined by shader API v1";
			return decision;
		}

		if (decision.disruptive)
			decision.requiredFlags |= ShaderAPI::kMutationAllowDisruptive;
		if (decision.destructive)
			decision.requiredFlags |= ShaderAPI::kMutationAllowDestructive;
		if ((a_request.flags & decision.requiredFlags) != decision.requiredFlags) {
			decision.status = ShaderAPI::Status::kBlocked;
			decision.allowed = false;
			decision.reasonCode = decision.destructive ? "destructive_consent_required" : "disruptive_consent_required";
			decision.message = decision.destructive ?
				"explicit disruptive and destructive consent flags are required" :
				"explicit disruptive consent is required";
		}
		return decision;
	}
}
