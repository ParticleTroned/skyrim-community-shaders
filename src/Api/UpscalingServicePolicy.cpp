#include "Api/UpscalingServicePolicy.h"

namespace CSX::Api
{
	UpscalingAdmissionDecision ResolveUpscalingAdmission(
		std::uint64_t a_observedConditions,
		UpscalingAPI::RequestPurpose a_purpose,
		UpscalingAPI::PersistencePolicy a_persistence,
		bool a_persistenceSupported) noexcept
	{
		using namespace UpscalingAPI;
		UpscalingAdmissionDecision decision{
			.observedConditions = a_observedConditions,
			.blockingConditions = a_observedConditions,
			.route = AdmissionRoute::kDirect,
		};

		if (a_persistence == PersistencePolicy::kPersistWhenStable &&
			!a_persistenceSupported) {
			decision.observedConditions |= kConditionPersistenceUnavailable;
			decision.blockingConditions |= kConditionPersistenceUnavailable;
		}

		if (a_purpose != RequestPurpose::kEnvironmentProfileTransition)
			return decision;

		constexpr std::uint64_t hardConditions =
			kConditionRaceSexMenu |
			kConditionRaceSexStartupTail |
			kConditionOpenCompositeUpscaling |
			kConditionRelatchPending |
			kConditionProviderCheckPending |
			kConditionProviderUnavailable |
			kConditionPersistenceUnavailable;
		const bool loadingDoorCandidate =
			(decision.observedConditions & kConditionLoadingTransition) != 0 &&
			(decision.observedConditions & hardConditions) == 0;
		if (!loadingDoorCandidate)
			return decision;

		decision.blockingConditions &=
			~(kConditionLoadingTransition | kConditionTransitionPending);
		decision.route = AdmissionRoute::kLoadingDoorHandoff;
		return decision;
	}

	bool IsUpscalingRuntimeNoChange(
		bool a_transitionActive,
		bool a_effectiveMatches,
		bool a_stableMatches) noexcept
	{
		return !a_transitionActive &&
		       a_effectiveMatches &&
		       a_stableMatches;
	}

	bool HasUpscalingServiceCapacity(
		std::size_t a_commandCount,
		std::size_t a_operationCount,
		std::size_t a_pendingOperationCount,
		std::size_t a_maximumCount) noexcept
	{
		return a_maximumCount != 0 &&
		       a_commandCount < a_maximumCount &&
		       a_pendingOperationCount < a_maximumCount &&
		       a_operationCount < a_maximumCount - a_pendingOperationCount;
	}
}
