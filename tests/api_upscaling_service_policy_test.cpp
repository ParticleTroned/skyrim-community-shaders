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
		Capabilities001 fsrCapabilities;
		fsrCapabilities.availableMethodMask =
			1ull << static_cast<std::uint32_t>(Method::kFSR);
		fsrCapabilities.fsrRuntimeUnavailableConditions[static_cast<std::uint32_t>(FSRRuntime::kFSR4)] =
			kConditionProviderCheckPending | kConditionRestartRequired;
		Profile001 fsr4Target;
		fsr4Target.method = Method::kFSR;
		fsr4Target.fsrRuntime = FSRRuntime::kFSR4;
		Check(
			CSX::Api::ResolveFSRRuntimeFallbackConditions(
				fsr4Target,
				fsrCapabilities) == kConditionProviderCheckPending,
			"FSR4 fallback did not retain only its non-blocking provider condition");
		fsr4Target.fsrRuntime = FSRRuntime::kFSR3;
		Check(
			CSX::Api::ResolveFSRRuntimeFallbackConditions(
				fsr4Target,
				fsrCapabilities) == kConditionNone,
			"FSR3 admission inherited the FSR4 provider condition");
		fsr4Target.fsrRuntime = FSRRuntime::kFSR4;
		fsrCapabilities.availableMethodMask = 0;
		Check(
			CSX::Api::ResolveFSRRuntimeFallbackConditions(
				fsr4Target,
				fsrCapabilities) == kConditionNone,
			"FSR4 fallback bypassed base-method unavailability");

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

		const auto fsrFallbackPending = CSX::Api::ResolveUpscalingAdmission(
			kConditionLoadingTransition,
			RequestPurpose::kEnvironmentProfileTransition,
			PersistencePolicy::kRuntimeOnly,
			false,
			kConditionProviderCheckPending);
		Check((fsrFallbackPending.observedConditions & kConditionProviderCheckPending) != 0,
			"FSR fallback admission dropped the pending-provider observation");
		Check(fsrFallbackPending.blockingConditions == 0,
			"FSR fallback admission retained a non-blocking pending-provider condition");
		Check(fsrFallbackPending.route == AdmissionRoute::kLoadingDoorHandoff,
			"FSR fallback admission did not retain the loading-door route");

		const auto fsrFallbackUnavailable = CSX::Api::ResolveUpscalingAdmission(
			kConditionNone,
			RequestPurpose::kDirect,
			PersistencePolicy::kRuntimeOnly,
			false,
			kConditionProviderUnavailable);
		Check((fsrFallbackUnavailable.observedConditions & kConditionProviderUnavailable) != 0,
			"FSR fallback admission dropped the unavailable-provider observation");
		Check(fsrFallbackUnavailable.blockingConditions == 0,
			"FSR fallback admission retained a non-blocking unavailable-provider condition");

		const auto preexistingProviderFailure = CSX::Api::ResolveUpscalingAdmission(
			kConditionProviderUnavailable,
			RequestPurpose::kDirect,
			PersistencePolicy::kRuntimeOnly,
			false,
			kConditionProviderUnavailable);
		Check((preexistingProviderFailure.blockingConditions & kConditionProviderUnavailable) != 0,
			"fallback telemetry suppressed a pre-existing provider failure");
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
