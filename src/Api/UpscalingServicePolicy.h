#pragma once

#include "VRAPI/CSupscalingapi.h"

#include <cstddef>

namespace CSX::Api
{
	/** Accept only successful, unchanged observations from the native API. */
	inline bool IsUpscalingSnapshotBracketStable(
		UpscalingAPI::Status a_beforeStatus, const UpscalingAPI::Snapshot001& a_before,
		UpscalingAPI::Status a_afterStatus, const UpscalingAPI::Snapshot001& a_after) noexcept
	{
		return a_beforeStatus == UpscalingAPI::Status::kSuccess &&
		       a_afterStatus == UpscalingAPI::Status::kSuccess && a_before.stateRevision != 0 &&
		       a_before.stateRevision == a_after.stateRevision &&
		       a_before.capabilityRevision == a_after.capabilityRevision;
	}

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
