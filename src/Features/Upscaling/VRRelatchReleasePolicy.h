#pragma once

#include <cstdint>
#include <limits>
#include <span>

namespace VRRelatchReleasePolicy
{
	struct RetryHistory
	{
		bool valid = false;
		std::uint64_t epoch = 0;
		std::uint64_t requestID = 0;
		std::uint32_t retries = 0;
		std::uint32_t readinessDeferrals = 0;
		std::uint32_t backendDeferrals = 0;
		std::uint32_t pressureDeferrals = 0;
		std::uint32_t retirementDeferrals = 0;
		std::uint32_t failures = 0;
	};

	struct RetryAdmission
	{
		RetryHistory beforeWait{};
		RetryHistory afterOwnedWait{};
		RetryHistory current{};
		bool immutableSettingsTransition = false;
		bool recoveryOwned = false;
		bool providerNeutralRecovery = false;
		bool emergencyRecovery = false;
		bool presentationDeadlineFallback = false;
	};

	/** Unsaturated counters must account for every retry without another failure class. */
	[[nodiscard]] constexpr bool IsUnambiguousBackendHistory(const RetryHistory& a_history) noexcept
	{
		return a_history.valid && a_history.epoch != 0 && a_history.requestID != 0 &&
		       a_history.retries != std::numeric_limits<std::uint32_t>::max() &&
		       a_history.backendDeferrals == a_history.retries &&
		       a_history.readinessDeferrals <= a_history.retries &&
		       a_history.pressureDeferrals == 0 && a_history.retirementDeferrals == 0 &&
		       a_history.failures == 0;
	}

	/** Qualifies one observed owned wait without changing its recorded Backend retry. */
	[[nodiscard]] constexpr bool CanUseOwnedRetryRelease(const RetryAdmission& a_state) noexcept
	{
		const auto& before = a_state.beforeWait;
		const auto& after = a_state.afterOwnedWait;
		const auto& current = a_state.current;
		return a_state.immutableSettingsTransition && !a_state.recoveryOwned &&
		       !a_state.providerNeutralRecovery && !a_state.emergencyRecovery &&
		       !a_state.presentationDeadlineFallback &&
		       IsUnambiguousBackendHistory(before) && IsUnambiguousBackendHistory(after) &&
		       IsUnambiguousBackendHistory(current) &&
		       before.epoch == after.epoch && after.epoch == current.epoch &&
		       before.requestID == after.requestID && after.requestID == current.requestID &&
		       before.retries == before.readinessDeferrals &&
		       static_cast<std::uint64_t>(after.retries) == static_cast<std::uint64_t>(before.retries) + 1u &&
		       after.readinessDeferrals == before.readinessDeferrals &&
		       current.readinessDeferrals >= after.readinessDeferrals &&
		       static_cast<std::uint64_t>(current.retries) == static_cast<std::uint64_t>(current.readinessDeferrals) + 1u;
	}

	struct IntermediateRetirementRecord
	{
		std::uint64_t epoch = 0;
		std::uint32_t targetGeneration = 0;
		std::uint64_t serial = 0;
	};

	struct IntermediateRetirementEvidence
	{
		bool observed = false;
		bool pendingAtBegin = true;
		std::uint64_t lastIssuedAtBegin = 0;
		std::uint64_t ownedLastIssued = 0;
		std::uint64_t currentLastIssued = 0;
		std::uint64_t completedSerial = 0;
		std::uint32_t pendingSets = 0;
		std::span<const IntermediateRetirementRecord> pending{};
		// A full retirement queue alone does not prove an admission was rejected.
		bool admissionBlocked = false;
		bool fenceFailed = false;
	};

	/** Every outstanding set must remain in the exact operation's detached ownership. */
	[[nodiscard]] constexpr bool IsOwnedIntermediateCleanupOnly(
		std::uint64_t a_epoch, std::uint32_t a_targetGeneration,
		const IntermediateRetirementEvidence& a_state) noexcept
	{
		if (a_epoch == 0 || a_targetGeneration == 0 || !a_state.observed || a_state.pendingAtBegin ||
			a_state.admissionBlocked || a_state.fenceFailed || a_state.pendingSets != a_state.pending.size() ||
			a_state.ownedLastIssued == std::numeric_limits<std::uint64_t>::max() ||
			a_state.ownedLastIssued < a_state.lastIssuedAtBegin ||
			a_state.currentLastIssued != a_state.ownedLastIssued ||
			a_state.completedSerial < a_state.lastIssuedAtBegin || a_state.completedSerial > a_state.ownedLastIssued) {
			return false;
		}
		auto accountedSerial = a_state.completedSerial;
		for (const auto& retirement : a_state.pending) {
			if (retirement.epoch != a_epoch || retirement.targetGeneration != a_targetGeneration ||
				retirement.serial <= a_state.lastIssuedAtBegin || retirement.serial != accountedSerial + 1u) {
				return false;
			}
			accountedSerial = retirement.serial;
		}
		return accountedSerial == a_state.ownedLastIssued;
	}

	struct EngineRetirementEvidence
	{
		bool observed = false;
		bool pendingAtBegin = true;
		bool recreated = false;
		bool reconciled = false;
		bool supported = false;
		std::uint32_t issuedReferenceCount = 0;
		std::uint32_t capturedPointerCount = 0;
		std::uint32_t provenPointerCount = 0;
		std::uint32_t replacedPointerCount = 0;
		std::uint32_t retainedUnprovenPointerCount = 0;
		std::uint32_t restoredPointerCount = 0;
		std::uint32_t poisonReferenceCount = 0;
		bool pending = false;
		std::uint64_t oldestEpoch = 0;
		std::uint64_t newestEpoch = 0;
		std::uint32_t pendingGenerations = 0;
		std::uint32_t pendingReleaseCount = 0;
		std::uint32_t fenceFailures = 0;
		bool admissionBlocked = false;
	};

	/** Reconciled displaced references may await release while new targets remain owned. */
	[[nodiscard]] constexpr bool IsOwnedEngineCleanupOnly(std::uint64_t a_epoch, const EngineRetirementEvidence& a_state) noexcept
	{
		if (a_epoch == 0 || !a_state.observed || a_state.pendingAtBegin || a_state.admissionBlocked ||
			a_state.fenceFailures != 0 || a_state.retainedUnprovenPointerCount != 0 ||
			a_state.restoredPointerCount != 0 || a_state.poisonReferenceCount != 0) {
			return false;
		}
		if (!a_state.recreated) {
			return !a_state.pending && a_state.pendingGenerations == 0 && a_state.pendingReleaseCount == 0 &&
			       a_state.issuedReferenceCount == 0 && a_state.capturedPointerCount == 0 &&
			       a_state.provenPointerCount == 0 && a_state.replacedPointerCount == 0;
		}
		if (!a_state.reconciled || !a_state.supported ||
			a_state.capturedPointerCount == std::numeric_limits<std::uint32_t>::max() ||
			a_state.provenPointerCount > a_state.capturedPointerCount ||
			a_state.replacedPointerCount > a_state.provenPointerCount ||
			a_state.issuedReferenceCount != a_state.replacedPointerCount) {
			return false;
		}
		if (!a_state.pending)
			return a_state.pendingGenerations == 0 && a_state.pendingReleaseCount == 0;
		return a_state.oldestEpoch == a_epoch && a_state.newestEpoch == a_epoch &&
		       a_state.pendingGenerations == 1 && a_state.issuedReferenceCount != 0 &&
		       a_state.pendingReleaseCount == a_state.issuedReferenceCount;
	}

	struct TrimEvidence
	{
		bool observed = false;
		bool pendingAtBegin = true;
		bool required = false;
		std::uint64_t ownerEpoch = 0;
		bool pending = false;
		bool cleanupOnlyRapidRelatch = false;
		bool completedSuccessfully = false;
		std::uint32_t fenceFailures = 0;
		// This operation's failures, excluding unrelated completed trim history.
		std::uint32_t failures = 0;
	};

	/** Only a post-admission rapid-relatch trim may remain as deferred housekeeping. */
	[[nodiscard]] constexpr bool IsOwnedTrimSatisfied(std::uint64_t a_epoch, const TrimEvidence& a_state) noexcept
	{
		if (a_epoch == 0 || !a_state.observed || a_state.pendingAtBegin ||
			a_state.fenceFailures != 0 || a_state.failures != 0) {
			return false;
		}
		if (!a_state.required)
			return !a_state.pending;
		return a_state.ownerEpoch == a_epoch &&
		       (a_state.pending ? a_state.cleanupOnlyRapidRelatch : a_state.completedSuccessfully);
	}

	struct ReleaseObligations
	{
		std::uint64_t epoch = 0;
		std::uint32_t targetGeneration = 0;
		bool physicalMutationCompleted = false;
		bool sharedCleanupSatisfied = false;
		bool memoryAdmissionSatisfied = false;
		IntermediateRetirementEvidence intermediate{};
		EngineRetirementEvidence engine{};
		TrimEvidence trim{};
	};

	/** Proves completed blocking work without prematurely releasing deferred resources. */
	[[nodiscard]] constexpr bool AreReleaseObligationsSatisfied(const ReleaseObligations& a_state) noexcept
	{
		return a_state.physicalMutationCompleted && a_state.sharedCleanupSatisfied &&
		       a_state.memoryAdmissionSatisfied &&
		       IsOwnedIntermediateCleanupOnly(a_state.epoch, a_state.targetGeneration, a_state.intermediate) &&
		       IsOwnedEngineCleanupOnly(a_state.epoch, a_state.engine) && IsOwnedTrimSatisfied(a_state.epoch, a_state.trim);
	}
}
