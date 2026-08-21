#pragma once

#include "VRAPI/CSupscalingapi.h"

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
}
