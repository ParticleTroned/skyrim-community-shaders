#include "Api/WeatherServicePolicy.h"

#include <cstdlib>
#include <iostream>

namespace
{
	void Check(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			std::cerr << a_message << '\n';
			std::exit(1);
		}
	}

	CSX::Api::WeatherPolicyState ReadyState()
	{
		return { .available = true, .skyAvailable = true, .weatherFound = true, .featureFound = true, .featureSupportsWeather = true, .stateRevision = 17 };
	}
}

int main()
{
	using namespace CSX;
	using namespace CSX::Api;

	{
		auto state = ReadyState();
		WeatherPolicyRequest request{ .action = WeatherAPI::MutationAction::kSetWeather,
			.expectedStateRevision = 17,
			.weatherKey = "0x123~Test.esp" };
		const auto result = EvaluateWeatherMutation(state, request);
		Check(!result.allowed && result.status == WeatherAPI::Status::kBlocked,
			"weather selection must require disruptive consent");
		Check((result.requiredFlags & WeatherAPI::kMutationAllowDisruptive) != 0,
			"weather selection did not report its required flag");
	}
	{
		auto state = ReadyState();
		WeatherPolicyRequest request{ .action = WeatherAPI::MutationAction::kSetWeather,
			.expectedStateRevision = 16,
			.flags = WeatherAPI::kMutationAllowDisruptive,
			.weatherKey = "0x123~Test.esp" };
		const auto result = EvaluateWeatherMutation(state, request);
		Check(!result.allowed && result.status == WeatherAPI::Status::kRevisionConflict,
			"stale weather revision was accepted");
	}
	{
		auto state = ReadyState();
		state.weatherLocked = true;
		WeatherPolicyRequest request{ .action = WeatherAPI::MutationAction::kPreviewWeather,
			.expectedStateRevision = 17,
			.flags = WeatherAPI::kMutationAllowDisruptive,
			.weatherKey = "0x123~Test.esp" };
		const auto result = EvaluateWeatherMutation(state, request);
		Check(!result.allowed && result.reasonCode == "weather_locked",
			"temporary preview was accepted while weather lock was active");
	}
	{
		auto state = ReadyState();
		WeatherPolicyRequest request{ .action = WeatherAPI::MutationAction::kSetFeatureOverride,
			.expectedStateRevision = 17,
			.flags = WeatherAPI::kMutationPersist,
			.weatherKey = "0x123~Test.esp",
			.featureName = "IBL",
			.valueJson = R"({"__enabled":true})" };
		const auto result = EvaluateWeatherMutation(state, request);
		Check(result.allowed && result.willPersist && !result.willApplyLive,
			"persist-only override mutation was not admitted");
	}
	{
		auto state = ReadyState();
		state.persistentMutationBlocked = true;
		WeatherPolicyRequest request{ .action = WeatherAPI::MutationAction::kSetFeatureOverride,
			.expectedStateRevision = 17,
			.flags = WeatherAPI::kMutationPersist,
			.weatherKey = "0x123~Test.esp",
			.featureName = "IBL",
			.valueJson = R"({"__enabled":true})" };
		const auto result = EvaluateWeatherMutation(state, request);
		Check(!result.allowed && result.status == WeatherAPI::Status::kBusy,
			"persistent override was accepted during save/load mutation block");
	}
	{
		auto state = ReadyState();
		WeatherPolicyRequest request{ .action = WeatherAPI::MutationAction::kRemoveFeatureOverride,
			.expectedStateRevision = 17,
			.flags = WeatherAPI::kMutationPersist | WeatherAPI::kMutationApplyLive |
			         WeatherAPI::kMutationAllowDisruptive,
			.weatherKey = "0x123~Test.esp",
			.featureName = "IBL" };
		const auto result = EvaluateWeatherMutation(state, request);
		Check(!result.allowed && result.destructive && result.reasonCode == "destructive_consent_required",
			"persisted override removal did not require destructive consent");
	}
	{
		auto state = ReadyState();
		WeatherPolicyRequest request{ .action = WeatherAPI::MutationAction::kSetFeatureOverride,
			.expectedStateRevision = 17,
			.weatherKey = "0x123~Test.esp",
			.featureName = "IBL",
			.valueJson = R"({"__enabled":true})" };
		const auto result = EvaluateWeatherMutation(state, request);
		Check(!result.allowed && result.reasonCode == "override_destination_required",
			"override without persist or applyLive was accepted");
	}

	return 0;
}
