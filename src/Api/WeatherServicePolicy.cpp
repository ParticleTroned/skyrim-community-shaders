#include "Api/WeatherServicePolicy.h"

namespace CSX::Api
{
	WeatherPolicyDecision EvaluateWeatherMutation(
		const WeatherPolicyState& a_state,
		const WeatherPolicyRequest& a_request)
	{
		WeatherPolicyDecision decision;
		if (!a_state.available) {
			decision.status = WeatherAPI::Status::kUnavailable;
			decision.allowed = false;
			decision.reasonCode = "service_unavailable";
			decision.message = "weather state is not initialized";
			return decision;
		}

		constexpr std::uint64_t knownFlags = WeatherAPI::kMutationPersist |
		                                     WeatherAPI::kMutationApplyLive | WeatherAPI::kMutationAllowDisruptive |
		                                     WeatherAPI::kMutationAllowDestructive;
		if ((a_request.flags & ~knownFlags) != 0) {
			decision.status = WeatherAPI::Status::kInvalidArgument;
			decision.allowed = false;
			decision.reasonCode = "unknown_mutation_flags";
			decision.message = "mutation flags contain bits not defined by weather API v1";
			return decision;
		}
		if (a_request.expectedStateRevision != 0 &&
			a_request.expectedStateRevision != a_state.stateRevision) {
			decision.status = WeatherAPI::Status::kRevisionConflict;
			decision.allowed = false;
			decision.reasonCode = "state_revision_conflict";
			decision.message = "weather control state changed after the client snapshot";
			return decision;
		}

		const bool isOverrideMutation =
			a_request.action == WeatherAPI::MutationAction::kSetFeatureOverride ||
			a_request.action == WeatherAPI::MutationAction::kRemoveFeatureOverride;
		decision.willPersist = (a_request.flags & WeatherAPI::kMutationPersist) != 0;
		decision.willApplyLive = (a_request.flags & WeatherAPI::kMutationApplyLive) != 0;
		if (!isOverrideMutation && (decision.willPersist || decision.willApplyLive)) {
			decision.status = WeatherAPI::Status::kInvalidArgument;
			decision.allowed = false;
			decision.reasonCode = "override_flags_not_applicable";
			decision.message = "persist and applyLive are defined only for feature override mutations";
			return decision;
		}
		if (isOverrideMutation && !decision.willPersist && !decision.willApplyLive) {
			decision.status = WeatherAPI::Status::kInvalidArgument;
			decision.allowed = false;
			decision.reasonCode = "override_destination_required";
			decision.message = "an override mutation must request persistence, live application, or both";
			return decision;
		}
		if (decision.willPersist && a_state.persistentMutationBlocked) {
			decision.status = WeatherAPI::Status::kBusy;
			decision.allowed = false;
			decision.reasonCode = "persistent_mutation_blocked";
			decision.message = "weather persistence is temporarily blocked by save/load activity";
			return decision;
		}

		auto requireWeather = [&]() {
			if (a_request.weatherKey.empty() || !a_state.weatherFound) {
				decision.status = WeatherAPI::Status::kWeatherNotFound;
				decision.allowed = false;
				decision.reasonCode = "weather_not_found";
				decision.message = "weatherKey does not identify a loaded TESWeather record";
				return false;
			}
			return true;
		};
		auto requireSky = [&]() {
			if (!a_state.skyAvailable) {
				decision.status = WeatherAPI::Status::kUnavailable;
				decision.allowed = false;
				decision.reasonCode = "sky_unavailable";
				decision.message = "the Sky singleton is unavailable";
				return false;
			}
			return true;
		};
		auto requireFeature = [&]() {
			if (a_request.featureName.empty() || !a_state.featureFound || !a_state.featureSupportsWeather) {
				decision.status = WeatherAPI::Status::kFeatureNotFound;
				decision.allowed = false;
				decision.reasonCode = "weather_feature_not_found";
				decision.message = "featureName does not identify a loaded feature with registered weather variables";
				return false;
			}
			return true;
		};

		switch (a_request.action) {
		case WeatherAPI::MutationAction::kSetWeather:
		case WeatherAPI::MutationAction::kLockWeather:
			if (!requireSky() || !requireWeather())
				return decision;
			decision.disruptive = true;
			break;
		case WeatherAPI::MutationAction::kPreviewWeather:
			if (!requireSky() || !requireWeather())
				return decision;
			if (a_state.weatherLocked) {
				decision.status = WeatherAPI::Status::kBlocked;
				decision.allowed = false;
				decision.reasonCode = "weather_locked";
				decision.message = "temporary preview is blocked while weather lock is active";
				return decision;
			}
			decision.disruptive = true;
			break;
		case WeatherAPI::MutationAction::kResetWeather:
			if (!requireSky())
				return decision;
			if (a_state.weatherLocked) {
				decision.status = WeatherAPI::Status::kBlocked;
				decision.allowed = false;
				decision.reasonCode = "weather_locked";
				decision.message = "unlock weather before resetting it";
				return decision;
			}
			decision.disruptive = true;
			break;
		case WeatherAPI::MutationAction::kUnlockWeather:
			if (!a_state.weatherLocked) {
				decision.message = "weather is already unlocked";
				break;
			}
			decision.disruptive = true;
			break;
		case WeatherAPI::MutationAction::kSetFeaturePaused:
			if (!requireFeature())
				return decision;
			decision.disruptive = true;
			break;
		case WeatherAPI::MutationAction::kReloadOverrides:
			decision.disruptive = true;
			break;
		case WeatherAPI::MutationAction::kSetFeatureOverride:
			if (!requireWeather() || !requireFeature())
				return decision;
			if (a_request.valueJson.empty()) {
				decision.status = WeatherAPI::Status::kInvalidOverride;
				decision.allowed = false;
				decision.reasonCode = "override_value_required";
				decision.message = "valueJson is required when setting a feature override";
				return decision;
			}
			decision.disruptive = decision.willApplyLive;
			break;
		case WeatherAPI::MutationAction::kRemoveFeatureOverride:
			if (!requireWeather() || !requireFeature())
				return decision;
			if (!a_request.valueJson.empty()) {
				decision.status = WeatherAPI::Status::kInvalidArgument;
				decision.allowed = false;
				decision.reasonCode = "override_value_not_applicable";
				decision.message = "valueJson is not valid when removing a feature override";
				return decision;
			}
			decision.disruptive = decision.willApplyLive;
			decision.destructive = decision.willPersist;
			break;
		default:
			decision.status = WeatherAPI::Status::kInvalidArgument;
			decision.allowed = false;
			decision.reasonCode = "unknown_action";
			decision.message = "mutation action is not defined by weather API v1";
			return decision;
		}

		if (decision.disruptive)
			decision.requiredFlags |= WeatherAPI::kMutationAllowDisruptive;
		if (decision.destructive)
			decision.requiredFlags |= WeatherAPI::kMutationAllowDestructive;
		if ((a_request.flags & decision.requiredFlags) != decision.requiredFlags) {
			decision.status = WeatherAPI::Status::kBlocked;
			decision.allowed = false;
			decision.reasonCode = decision.destructive ?
			                          "destructive_consent_required" :
			                          "disruptive_consent_required";
			decision.message = decision.destructive ?
			                       "explicit disruptive and destructive consent flags are required" :
			                       "explicit disruptive consent is required";
		}
		return decision;
	}
}
