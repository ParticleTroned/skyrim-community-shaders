#pragma once

#include "VRAPI/CSupscalingapi.h"

#include <cstddef>

namespace CSX::Api
{
	struct UpscalingAdmissionDecision
	{
		std::uint64_t observedConditions = UpscalingAPI::kConditionNone;
		std::uint64_t blockingConditions = UpscalingAPI::kConditionNone;
		UpscalingAPI::AdmissionRoute route = UpscalingAPI::AdmissionRoute::kNone;
	};

	/** Reports optional FSR4 provider state when the base FSR method can fall back. */
	std::uint64_t ResolveFSRRuntimeFallbackConditions(
		const UpscalingAPI::Profile001& a_target,
		const UpscalingAPI::Capabilities001& a_capabilities) noexcept;

	UpscalingAdmissionDecision ResolveUpscalingAdmission(
		std::uint64_t a_observedConditions,
		UpscalingAPI::RequestPurpose a_purpose,
		UpscalingAPI::PersistencePolicy a_persistence,
		bool a_persistenceSupported,
		std::uint64_t a_nonBlockingObservedConditions = UpscalingAPI::kConditionNone) noexcept;

	bool IsUpscalingRuntimeNoChange(
		bool a_transitionActive,
		bool a_effectiveMatches,
		bool a_stableMatches) noexcept;

	bool HasUpscalingServiceCapacity(
		std::size_t a_commandCount,
		std::size_t a_operationCount,
		std::size_t a_pendingOperationCount,
		std::size_t a_maximumCount) noexcept;
}
