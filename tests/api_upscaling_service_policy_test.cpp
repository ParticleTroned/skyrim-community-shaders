#include "Api/UpscalingServicePolicy.h"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace CSX::UpscalingAPI;

namespace
{
	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition)
			throw std::runtime_error(std::string(a_message));
	}
}

int main()
{
	try {
		const auto directLoading = CSX::Api::ResolveUpscalingAdmission(
			kConditionLoadingTransition | kConditionTransitionPending,
			RequestPurpose::kDirect,
			PersistencePolicy::kRuntimeOnly,
			false);
		Check(directLoading.blockingConditions != 0, "direct request bypassed loading ownership");
		Check(directLoading.route == AdmissionRoute::kDirect, "direct request changed admission route");

		const auto environmentLoading = CSX::Api::ResolveUpscalingAdmission(
			kConditionLoadingTransition | kConditionTransitionPending,
			RequestPurpose::kEnvironmentProfileTransition,
			PersistencePolicy::kRuntimeOnly,
			false);
		Check(environmentLoading.blockingConditions == 0, "environment transition did not receive loading-door handoff");
		Check(environmentLoading.observedConditions != 0, "admission hid the observed loading conditions");
		Check(environmentLoading.route == AdmissionRoute::kLoadingDoorHandoff, "loading-door route was not reported");

		const auto relatch = CSX::Api::ResolveUpscalingAdmission(
			kConditionLoadingTransition | kConditionRelatchPending,
			RequestPurpose::kEnvironmentProfileTransition,
			PersistencePolicy::kRuntimeOnly,
			false);
		Check((relatch.blockingConditions & kConditionRelatchPending) != 0, "loading handoff bypassed relatch ownership");

		const auto providerPending = CSX::Api::ResolveUpscalingAdmission(
			kConditionLoadingTransition | kConditionProviderCheckPending,
			RequestPurpose::kEnvironmentProfileTransition,
			PersistencePolicy::kRuntimeOnly,
			false);
		Check((providerPending.blockingConditions & kConditionProviderCheckPending) != 0, "loading handoff bypassed provider readiness");

		const auto persistenceUnavailable = CSX::Api::ResolveUpscalingAdmission(
			kConditionLoadingTransition,
			RequestPurpose::kEnvironmentProfileTransition,
			PersistencePolicy::kPersistWhenStable,
			false);
		Check((persistenceUnavailable.observedConditions & kConditionPersistenceUnavailable) != 0, "unsupported persistence was not observed");
		Check((persistenceUnavailable.blockingConditions & kConditionPersistenceUnavailable) != 0, "unsupported persistence was admitted");

		const auto persistenceSupported = CSX::Api::ResolveUpscalingAdmission(
			kConditionLoadingTransition | kConditionTransitionPending,
			RequestPurpose::kEnvironmentProfileTransition,
			PersistencePolicy::kPersistWhenStable,
			true);
		Check(persistenceSupported.blockingConditions == 0, "supported persistence blocked a valid loading handoff");

		Check(
			CSX::Api::IsUpscalingRuntimeNoChange(
				false,
				true,
				true),
			"a converged runtime-only target depended on the separate configured profile");
		Check(
			CSX::Api::IsUpscalingRuntimeNoChange(
				false,
				true,
				true),
			"a settled physical target depended on a historical controller request slot");
		Check(
			!CSX::Api::IsUpscalingRuntimeNoChange(
				false,
				false,
				true),
			"a divergent effective target was reported as no-change");
		Check(
			!CSX::Api::IsUpscalingRuntimeNoChange(
				false,
				true,
				false),
			"a divergent stable target was reported as no-change");
		Check(
			!CSX::Api::IsUpscalingRuntimeNoChange(
				true,
				true,
				true),
			"an active transition was reported as no-change");

		Check(
			CSX::Api::HasUpscalingServiceCapacity(1023, 1023, 0, 1024),
			"available command capacity was rejected");
		Check(
			!CSX::Api::HasUpscalingServiceCapacity(1024, 0, 0, 1024),
			"command capacity did not fail closed");
		Check(
			!CSX::Api::HasUpscalingServiceCapacity(0, 1024, 0, 1024),
			"operation capacity did not fail closed");
		Check(
			!CSX::Api::HasUpscalingServiceCapacity(1, 1023, 1, 1024),
			"pending operation admission exceeded the shared operation bound");
		Check(
			CSX::Api::HasUpscalingServiceCapacity(1, 1022, 1, 1024),
			"pending operation admission rejected remaining capacity");
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
