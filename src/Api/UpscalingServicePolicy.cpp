#include "Api/UpscalingServicePolicy.h"

namespace CSX::Api
{
	std::uint64_t ResolveFSRRuntimeFallbackConditions(
		const UpscalingAPI::Profile001& a_target,
		const UpscalingAPI::Capabilities001& a_capabilities) noexcept
	{
		using namespace UpscalingAPI;
		if (a_target.method != Method::kFSR ||
			a_target.fsrRuntime != FSRRuntime::kFSR4) {
			return kConditionNone;
		}

		const auto fsrMethodBit =
			1ull << static_cast<std::uint32_t>(Method::kFSR);
		if ((a_capabilities.availableMethodMask & fsrMethodBit) == 0)
			return kConditionNone;

		const auto fsr4Index = static_cast<std::uint32_t>(FSRRuntime::kFSR4);
		return a_capabilities.fsrRuntimeUnavailableConditions[fsr4Index] &
		       (kConditionProviderCheckPending | kConditionProviderUnavailable);
	}

	UpscalingAdmissionDecision ResolveUpscalingAdmission(
		std::uint64_t a_observedConditions,
		UpscalingAPI::RequestPurpose a_purpose,
		UpscalingAPI::PersistencePolicy a_persistence,
		bool a_persistenceSupported,
		std::uint64_t a_nonBlockingObservedConditions) noexcept
	{
		using namespace UpscalingAPI;
		const auto nonBlockingProviderConditions =
			a_nonBlockingObservedConditions &
			(kConditionProviderCheckPending | kConditionProviderUnavailable);
		UpscalingAdmissionDecision decision{
			.observedConditions =
				a_observedConditions | nonBlockingProviderConditions,
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
			(decision.blockingConditions & hardConditions) == 0;
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
