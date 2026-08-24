#pragma once

#include "VRAPI/CSweatherapi.h"

#include <cstdint>
#include <string>

namespace CSX::Api
{
	struct WeatherPolicyState
	{
		bool available = false;
		bool skyAvailable = false;
		bool weatherFound = false;
		bool featureFound = false;
		bool featureSupportsWeather = false;
		bool weatherLocked = false;
		bool persistentMutationBlocked = false;
		std::uint64_t stateRevision = 0;
	};

	struct WeatherPolicyRequest
	{
		WeatherAPI::MutationAction action = WeatherAPI::MutationAction::kSetWeather;
		std::uint64_t expectedStateRevision = 0;
		std::uint64_t flags = WeatherAPI::kMutationNone;
		bool boolValue = false;
		std::string weatherKey;
		std::string featureName;
		std::string valueJson;
	};

	struct WeatherPolicyDecision
	{
		WeatherAPI::Status status = WeatherAPI::Status::kSuccess;
		bool allowed = true;
		bool disruptive = false;
		bool destructive = false;
		bool willPersist = false;
		bool willApplyLive = false;
		std::uint64_t requiredFlags = WeatherAPI::kMutationNone;
		std::string reasonCode;
		std::string message;
	};

	WeatherPolicyDecision EvaluateWeatherMutation(
		const WeatherPolicyState& a_state,
		const WeatherPolicyRequest& a_request);
}
