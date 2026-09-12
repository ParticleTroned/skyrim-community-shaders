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

	UpscalingAdmissionDecision ResolveUpscalingAdmission(
		std::uint64_t a_observedConditions,
		UpscalingAPI::RequestPurpose a_purpose,
		UpscalingAPI::PersistencePolicy a_persistence,
		bool a_persistenceSupported) noexcept;

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
