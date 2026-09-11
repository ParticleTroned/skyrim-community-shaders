#include "Features/Upscaling/VRRelatchReleasePolicy.h"

#include <array>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <span>

namespace
{
	using namespace VRRelatchReleasePolicy;

	constexpr RetryAdmission CleanRetryAdmission(std::uint32_t a_readiness = 0)
	{
		RetryAdmission admission{};
		admission.immutableSettingsTransition = true;
		admission.beforeWait = {
			.valid = true,
			.epoch = 7,
			.requestID = 11,
			.retries = a_readiness,
			.readinessDeferrals = a_readiness,
			.backendDeferrals = a_readiness,
		};
		admission.afterOwnedWait = admission.beforeWait;
		++admission.afterOwnedWait.retries;
		++admission.afterOwnedWait.backendDeferrals;
		admission.current = admission.afterOwnedWait;
		return admission;
	}

	constexpr bool OneOwnedRetryPreservesOrdinaryReadiness()
	{
		auto admission = CleanRetryAdmission();
		if (!CanUseOwnedRetryRelease(admission))
			return false;
		admission = CleanRetryAdmission(3);
		if (!CanUseOwnedRetryRelease(admission))
			return false;
		++admission.current.retries;
		++admission.current.backendDeferrals;
		++admission.current.readinessDeferrals;
		if (!CanUseOwnedRetryRelease(admission))
			return false;
		admission.afterOwnedWait = admission.beforeWait;
		return !CanUseOwnedRetryRelease(admission) && !CanUseOwnedRetryRelease({});
	}

	constexpr bool MixedAndUnownedHistoriesFailClosed()
	{
		constexpr std::array disqualifyingCounters{
			&RetryHistory::pressureDeferrals, &RetryHistory::retirementDeferrals, &RetryHistory::failures
		};
		for (const auto counter : disqualifyingCounters) {
			for (const auto history : { &RetryAdmission::beforeWait, &RetryAdmission::afterOwnedWait, &RetryAdmission::current }) {
				auto admission = CleanRetryAdmission();
				(admission.*history).*counter = 1;
				if (CanUseOwnedRetryRelease(admission))
					return false;
			}
		}
		for (const auto history : { &RetryAdmission::beforeWait, &RetryAdmission::afterOwnedWait, &RetryAdmission::current }) {
			for (const auto owner : { &RetryHistory::epoch, &RetryHistory::requestID }) {
				auto admission = CleanRetryAdmission();
				++((admission.*history).*owner);
				if (CanUseOwnedRetryRelease(admission))
					return false;
			}
		}
		auto admission = CleanRetryAdmission();
		++admission.current.retries;
		if (CanUseOwnedRetryRelease(admission))
			return false;
		++admission.current.backendDeferrals;
		if (CanUseOwnedRetryRelease(admission))
			return false;
		admission = CleanRetryAdmission(1);
		admission.beforeWait.readinessDeferrals = 0;
		return !CanUseOwnedRetryRelease(admission);
	}

	constexpr bool SaturationAndRecoveryCannotQualify()
	{
		constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
		auto admission = CleanRetryAdmission(maximum - 2);
		if (!CanUseOwnedRetryRelease(admission))
			return false;
		++admission.current.retries;
		++admission.current.backendDeferrals;
		++admission.current.readinessDeferrals;
		if (CanUseOwnedRetryRelease(admission))
			return false;
		for (const auto flag : { &RetryAdmission::recoveryOwned, &RetryAdmission::providerNeutralRecovery,
				 &RetryAdmission::emergencyRecovery, &RetryAdmission::presentationDeadlineFallback }) {
			admission = CleanRetryAdmission();
			admission.*flag = true;
			if (CanUseOwnedRetryRelease(admission))
				return false;
		}
		admission = CleanRetryAdmission();
		admission.immutableSettingsTransition = false;
		return !CanUseOwnedRetryRelease(admission);
	}

	constexpr bool DetachedIntermediateSuffixIsAccountedExactly()
	{
		std::array records{
			IntermediateRetirementRecord{ 7, 9, 41 }, IntermediateRetirementRecord{ 7, 9, 42 }
		};
		IntermediateRetirementEvidence evidence{
			.observed = true,
			.pendingAtBegin = false,
			.lastIssuedAtBegin = 40,
			.ownedLastIssued = 42,
			.currentLastIssued = 42,
			.completedSerial = 40,
			.pendingSets = 2,
			.pending = records
		};
		if (!IsOwnedIntermediateCleanupOnly(7, 9, evidence))
			return false;
		evidence.completedSerial = 41;
		evidence.pendingSets = 1;
		evidence.pending = std::span(records).subspan(1);
		if (!IsOwnedIntermediateCleanupOnly(7, 9, evidence))
			return false;
		evidence.completedSerial = 42;
		evidence.pendingSets = 0;
		evidence.pending = {};
		if (!IsOwnedIntermediateCleanupOnly(7, 9, evidence))
			return false;
		evidence.completedSerial = 41;
		return !IsOwnedIntermediateCleanupOnly(7, 9, evidence);
	}

	constexpr bool ForeignMissingAndFailedIntermediateDebtIsRejected()
	{
		std::array records{ IntermediateRetirementRecord{ 7, 9, 41 } };
		const IntermediateRetirementEvidence clean{
			.observed = true,
			.pendingAtBegin = false,
			.lastIssuedAtBegin = 40,
			.ownedLastIssued = 41,
			.currentLastIssued = 41,
			.completedSerial = 40,
			.pendingSets = 1,
			.pending = records
		};
		for (const auto flag : { &IntermediateRetirementEvidence::pendingAtBegin,
				 &IntermediateRetirementEvidence::admissionBlocked, &IntermediateRetirementEvidence::fenceFailed }) {
			auto evidence = clean;
			evidence.*flag = true;
			if (IsOwnedIntermediateCleanupOnly(7, 9, evidence))
				return false;
		}
		for (const auto corrupt : { IntermediateRetirementRecord{ 8, 9, 41 }, IntermediateRetirementRecord{ 7, 8, 41 },
				 IntermediateRetirementRecord{ 7, 9, 40 }, IntermediateRetirementRecord{ 7, 9, 42 } }) {
			records[0] = corrupt;
			if (IsOwnedIntermediateCleanupOnly(7, 9, clean))
				return false;
		}
		records[0] = { 7, 9, 41 };
		auto evidence = clean;
		++evidence.currentLastIssued;
		if (IsOwnedIntermediateCleanupOnly(7, 9, evidence))
			return false;
		evidence = clean;
		evidence.pendingSets = 0;
		if (IsOwnedIntermediateCleanupOnly(7, 9, evidence))
			return false;
		evidence = clean;
		evidence.ownedLastIssued = std::numeric_limits<std::uint64_t>::max();
		evidence.currentLastIssued = evidence.ownedLastIssued;
		return !IsOwnedIntermediateCleanupOnly(7, 9, evidence);
	}

	constexpr EngineRetirementEvidence CleanEngineEvidence()
	{
		return {
			.observed = true,
			.pendingAtBegin = false,
			.recreated = true,
			.reconciled = true,
			.supported = true,
			.issuedReferenceCount = 5,
			.capturedPointerCount = 8,
			.provenPointerCount = 8,
			.replacedPointerCount = 5,
			.pending = true,
			.oldestEpoch = 7,
			.newestEpoch = 7,
			.pendingGenerations = 1,
			.pendingReleaseCount = 5
		};
	}

	constexpr bool EngineReleaseRequiresExactReconciledOwnership()
	{
		auto evidence = CleanEngineEvidence();
		if (!IsOwnedEngineCleanupOnly(7, evidence))
			return false;
		for (const auto counter : { &EngineRetirementEvidence::retainedUnprovenPointerCount,
				 &EngineRetirementEvidence::restoredPointerCount, &EngineRetirementEvidence::poisonReferenceCount,
				 &EngineRetirementEvidence::fenceFailures }) {
			evidence = CleanEngineEvidence();
			evidence.*counter = 1;
			if (IsOwnedEngineCleanupOnly(7, evidence))
				return false;
		}
		for (const auto flag : { &EngineRetirementEvidence::observed, &EngineRetirementEvidence::reconciled,
				 &EngineRetirementEvidence::supported }) {
			evidence = CleanEngineEvidence();
			evidence.*flag = false;
			if (IsOwnedEngineCleanupOnly(7, evidence))
				return false;
		}
		for (const auto flag : { &EngineRetirementEvidence::pendingAtBegin, &EngineRetirementEvidence::admissionBlocked }) {
			evidence = CleanEngineEvidence();
			evidence.*flag = true;
			if (IsOwnedEngineCleanupOnly(7, evidence))
				return false;
		}
		for (const auto counter : { &EngineRetirementEvidence::issuedReferenceCount,
				 &EngineRetirementEvidence::pendingReleaseCount, &EngineRetirementEvidence::pendingGenerations }) {
			evidence = CleanEngineEvidence();
			++(evidence.*counter);
			if (IsOwnedEngineCleanupOnly(7, evidence))
				return false;
		}
		evidence = CleanEngineEvidence();
		evidence.oldestEpoch = 6;
		if (IsOwnedEngineCleanupOnly(7, evidence))
			return false;
		evidence = CleanEngineEvidence();
		evidence.provenPointerCount = 4;
		if (IsOwnedEngineCleanupOnly(7, evidence))
			return false;
		evidence = CleanEngineEvidence();
		evidence.pending = false;
		evidence.pendingGenerations = 0;
		evidence.pendingReleaseCount = 0;
		return IsOwnedEngineCleanupOnly(7, evidence);
	}

	constexpr bool TrimCannotHideBlockingOrFailedObligations()
	{
		const TrimEvidence clean{
			.observed = true,
			.pendingAtBegin = false,
			.required = true,
			.ownerEpoch = 7,
			.pending = true,
			.cleanupOnlyRapidRelatch = true
		};
		if (!IsOwnedTrimSatisfied(7, clean))
			return false;
		auto evidence = clean;
		evidence.cleanupOnlyRapidRelatch = false;
		if (IsOwnedTrimSatisfied(7, evidence))
			return false;
		evidence = clean;
		evidence.pendingAtBegin = true;
		if (IsOwnedTrimSatisfied(7, evidence))
			return false;
		for (const auto counter : { &TrimEvidence::fenceFailures, &TrimEvidence::failures }) {
			evidence = clean;
			evidence.*counter = 1;
			if (IsOwnedTrimSatisfied(7, evidence))
				return false;
		}
		evidence = clean;
		evidence.ownerEpoch = 6;
		if (IsOwnedTrimSatisfied(7, evidence))
			return false;
		evidence = clean;
		evidence.pending = false;
		if (IsOwnedTrimSatisfied(7, evidence))
			return false;
		evidence.completedSuccessfully = true;
		return IsOwnedTrimSatisfied(7, evidence);
	}

	constexpr bool PartialWorkCannotBecomeReleaseEvidence()
	{
		const ReleaseObligations clean{
			.epoch = 7,
			.targetGeneration = 9,
			.physicalMutationCompleted = true,
			.sharedCleanupSatisfied = true,
			.memoryAdmissionSatisfied = true,
			.intermediate = { .observed = true, .pendingAtBegin = false },
			.engine = CleanEngineEvidence(),
			.trim = { .observed = true, .pendingAtBegin = false, .required = true, .ownerEpoch = 7, .pending = true, .cleanupOnlyRapidRelatch = true }
		};
		if (!AreReleaseObligationsSatisfied(clean) || AreReleaseObligationsSatisfied({}))
			return false;
		for (const auto flag : { &ReleaseObligations::physicalMutationCompleted,
				 &ReleaseObligations::sharedCleanupSatisfied, &ReleaseObligations::memoryAdmissionSatisfied }) {
			auto evidence = clean;
			evidence.*flag = false;
			if (AreReleaseObligationsSatisfied(evidence))
				return false;
		}
		auto evidence = clean;
		evidence.targetGeneration = 0;
		return !AreReleaseObligationsSatisfied(evidence);
	}

	static_assert(OneOwnedRetryPreservesOrdinaryReadiness());
	static_assert(MixedAndUnownedHistoriesFailClosed());
	static_assert(SaturationAndRecoveryCannotQualify());
	static_assert(DetachedIntermediateSuffixIsAccountedExactly());
	static_assert(ForeignMissingAndFailedIntermediateDebtIsRejected());
	static_assert(EngineReleaseRequiresExactReconciledOwnership());
	static_assert(TrimCannotHideBlockingOrFailedObligations());
	static_assert(PartialWorkCannotBecomeReleaseEvidence());
}

int main()
{
	const bool passed = OneOwnedRetryPreservesOrdinaryReadiness() && MixedAndUnownedHistoriesFailClosed() &&
	                    SaturationAndRecoveryCannotQualify() && DetachedIntermediateSuffixIsAccountedExactly() &&
	                    ForeignMissingAndFailedIntermediateDebtIsRejected() && EngineReleaseRequiresExactReconciledOwnership() &&
	                    TrimCannotHideBlockingOrFailedObligations() && PartialWorkCannotBecomeReleaseEvidence();
	std::puts(passed ? "VR relatch release policy: 8 groups passed" : "VR relatch release policy failed");
	return passed ? 0 : 1;
}
