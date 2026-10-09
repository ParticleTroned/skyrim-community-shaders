#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace NeuralRendering
{
	enum class MemoryStartupKind
	{
		Cold,
		Warm
	};

	[[nodiscard]] constexpr const char* ToString(MemoryStartupKind kind) noexcept
	{
		switch (kind) {
		case MemoryStartupKind::Cold:
			return "cold";
		case MemoryStartupKind::Warm:
			return "warm";
		}
		return "unknown";
	}

	enum class MemoryRecoveryPhase
	{
		Ready,
		Retiring,
		Waiting,
		Rebuilding,
		Probation
	};

	[[nodiscard]] constexpr const char* ToString(MemoryRecoveryPhase phase) noexcept
	{
		switch (phase) {
		case MemoryRecoveryPhase::Ready:
			return "ready";
		case MemoryRecoveryPhase::Retiring:
			return "retiring";
		case MemoryRecoveryPhase::Waiting:
			return "waiting_for_headroom";
		case MemoryRecoveryPhase::Rebuilding:
			return "rebuilding";
		case MemoryRecoveryPhase::Probation:
			return "probation";
		}
		return "unknown";
	}

	struct MemoryBudgetSample
	{
		static constexpr std::uint64_t kMaximumAgeMs = 500;
		std::uint64_t budgetBytes = 0;
		std::uint64_t usageBytes = 0;
		bool valid = false;
		std::int32_t result = 0;
		bool simulated = false;
		std::uint64_t sampledAtMs = 0;

		/** Budget samples cannot authorize recovery after a gap in observation. */
		[[nodiscard]] bool IsFresh(std::uint64_t nowMs) const noexcept
		{
			return valid && budgetBytes != 0 && sampledAtMs <= nowMs && nowMs - sampledAtMs <= kMaximumAgeMs;
		}

		[[nodiscard]] std::uint64_t Headroom() const noexcept
		{
			return budgetBytes > usageBytes ? budgetBytes - usageBytes : 0;
		}
	};

	/** Session-only admission; retirement and sustained active health complete recovery. */
	struct MemoryRecoveryPolicy
	{
		static constexpr std::uint64_t kMiB = 1024ull * 1024;
		static constexpr std::uint64_t kMinimumHeadroom = 512 * kMiB;
		static constexpr double kSuspendBudgetRatio = 0.875;
		static constexpr std::uint64_t kResumeBudgetPercent = 85;
		static constexpr double kResumeBudgetRatio = static_cast<double>(kResumeBudgetPercent) / 100;
		static constexpr std::uint64_t kSampleIntervalMs = 250;
		static constexpr std::uint64_t kMinimumOffMs = 5000;
		static constexpr std::uint64_t kStableHeadroomMs = 3000;
		static constexpr std::uint64_t kProbationMs = 10000;
		static constexpr std::uint64_t kMaximumSampleGapMs = MemoryBudgetSample::kMaximumAgeMs;
		static constexpr std::uint64_t kRetryResetMs = 30000;
		static constexpr std::uint32_t kMaximumRetryLevel = 4;
		static constexpr std::uint64_t kDemandMarginBytes = 128 * kMiB;
		static constexpr std::uint64_t kDemandProbeIntervalMs = 120000;
		// Native allocation bytes are private; this is a reserve, not a measurement.
		static constexpr std::uint64_t kNativeFeatureReserve = 128 * kMiB;
		MemoryRecoveryPhase phase = MemoryRecoveryPhase::Ready;
		MemoryStartupKind startupKind = MemoryStartupKind::Cold;
		MemoryBudgetSample sample{};
		std::uint64_t requiredBytes = 0;
		std::uint64_t pendingBytes = 0;
		std::uint64_t admissionDemandBytes = 0;
		std::uint64_t observedRecoveryDemandBytes = 0;
		std::uint64_t knownRecoveryFloorBytes = 0;
		std::uint64_t failedAdmissionCapacityBytes = 0;
		std::uint64_t demandLearnedAtMs = 0;
		bool failedAdmissionBarrier = false;
		std::uint64_t retryAfterMs = 0;
		std::uint64_t retryDelayMs = 0;
		std::uint64_t healthySinceMs = 0;
		std::uint64_t lastHealthySampleMs = 0;
		bool healthyWindow = false;
		std::uint64_t activeSinceMs = 0;
		std::uint64_t lastActiveSuccessMs = 0;
		std::uint64_t lastActiveSampleMs = 0;
		bool activeWindow = false;
		std::uint64_t lastResumeMs = 0;
		std::uint32_t retryLevel = 0;
		std::uint64_t suspensions = 0;
		std::uint64_t retirements = 0;
		std::uint64_t rebuildAttempts = 0;
		std::uint64_t resumes = 0;
		std::uint64_t rapidRelapses = 0;
		std::uint64_t demandProbes = 0;
		std::uint64_t outOfMemoryFailures = 0;
		std::uint64_t bypasses = 0;
		std::uint64_t dlssWarnings = 0;
		bool dlssWarning = false;

		void ClearHealthyWindow() noexcept
		{
			healthyWindow = false;
			healthySinceMs = 0;
			lastHealthySampleMs = 0;
		}

		/** Menus, route gaps and history resets cannot count as successful NR operation. */
		void BreakActiveObservation() noexcept
		{
			activeWindow = false;
			activeSinceMs = lastActiveSuccessMs = lastActiveSampleMs = 0;
			ClearHealthyWindow();
		}

		/** Switch only after completed retirement establishes whether the backend remains initialized. */
		void SetStartupKind(MemoryStartupKind kind) noexcept
		{
			if (kind == startupKind || (kind != MemoryStartupKind::Cold && kind != MemoryStartupKind::Warm))
				return;
			demandByStartup_[startupKind == MemoryStartupKind::Warm ? 1 : 0] = {
				observedRecoveryDemandBytes, knownRecoveryFloorBytes, failedAdmissionCapacityBytes,
				demandLearnedAtMs, failedAdmissionBarrier
			};
			startupKind = kind;
			const auto& demand = demandByStartup_[kind == MemoryStartupKind::Warm ? 1 : 0];
			observedRecoveryDemandBytes = demand.observedBytes;
			knownRecoveryFloorBytes = demand.knownFloorBytes;
			failedAdmissionCapacityBytes = demand.failedCapacityBytes;
			demandLearnedAtMs = demand.learnedAtMs;
			failedAdmissionBarrier = demand.failedBarrier;
			requiredBytes = 0;
			admissionDemandBytes = std::max(observedRecoveryDemandBytes, knownRecoveryFloorBytes);
			attemptObserved = false;
			sample.valid = false;
			BreakActiveObservation();
		}

		/** A different allocation capacity invalidates demand evidence, not retry history. */
		void InvalidateCapacityLearning() noexcept
		{
			demandByStartup_ = {};
			observedRecoveryDemandBytes = knownRecoveryFloorBytes = admissionDemandBytes = 0;
			failedAdmissionCapacityBytes = demandLearnedAtMs = 0;
			failedAdmissionBarrier = false;
			attemptObserved = false;
			BreakActiveObservation();
		}

		void Suspend(std::uint64_t nowMs, bool outOfMemory = false) noexcept
		{
			if (phase == MemoryRecoveryPhase::Retiring || phase == MemoryRecoveryPhase::Waiting)
				return;
			if (attemptObserved) {
				if (attemptWasRecovery && nowMs >= attemptStartedMs && nowMs - attemptStartedMs <= kRetryResetMs)
					++rapidRelapses;
				LearnFailedDemand(nowMs);
			}
			++suspensions;
			phase = MemoryRecoveryPhase::Retiring;
			BreakActiveObservation();
			if (outOfMemory)
				++outOfMemoryFailures;
			retryLevel = std::min(kMaximumRetryLevel, retryLevel + 1);
			retryDelayMs = retryLevel == 1 ? kMinimumOffMs : (retryLevel == 2 ? 15000 : (retryLevel == 3 ? 30000 : 60000));
			retryAfterMs = Add(nowMs, retryDelayMs);
			attemptObserved = false;
		}

		/** Start the minimum off period only after all retiring resources are safe to release. */
		void Retired(std::uint64_t nowMs) noexcept
		{
			if (phase != MemoryRecoveryPhase::Retiring)
				return;
			phase = MemoryRecoveryPhase::Waiting;
			retryAfterMs = std::max(retryAfterMs, Add(nowMs, retryDelayMs));
			ClearHealthyWindow();
			++retirements;
		}

		/** Preserve pressure history when a normal renderer reset releases its resources. */
		void ResourcesRetired(std::uint64_t nowMs) noexcept
		{
			BreakActiveObservation();
			attemptObserved = false;
			sample.valid = false;
			if (phase == MemoryRecoveryPhase::Retiring)
				Retired(nowMs);
			else if (phase == MemoryRecoveryPhase::Rebuilding || phase == MemoryRecoveryPhase::Probation) {
				phase = MemoryRecoveryPhase::Waiting;
				retryAfterMs = std::max(retryAfterMs, Add(nowMs, kMinimumOffMs));
			}
		}

		/** A valid DLSS output can still require NR retirement before another evaluation. */
		void ReportDlssWarning(std::uint64_t nowMs) noexcept
		{
			Suspend(nowMs);
			dlssWarning = true;
			++dlssWarnings;
			ClearHealthyWindow();
			retryAfterMs = std::max(retryAfterMs, Add(nowMs, kMinimumOffMs));
		}

		/** Keep process usage, pending owners and future NR allocation demand distinct. */
		[[nodiscard]] bool Admit(MemoryBudgetSample current, std::uint64_t additionalBytes,
			std::uint64_t nowMs, std::uint64_t otherPendingBytes = 0) noexcept
		{
			current.valid = current.IsFresh(nowMs);
			sample = current;
			requiredBytes = additionalBytes;
			pendingBytes = otherPendingBytes;
			admissionDemandBytes = additionalBytes;
			if (phase == MemoryRecoveryPhase::Retiring)
				return false;
			ObserveAttempt(nowMs);
			if (phase == MemoryRecoveryPhase::Waiting)
				return AdmitRecovery(nowMs);
			if ((phase == MemoryRecoveryPhase::Rebuilding || phase == MemoryRecoveryPhase::Probation) && !current.valid) {
				Suspend(nowMs);
				return false;
			}
			const auto totalAdditional = Add(additionalBytes, otherPendingBytes);
			if (current.valid && (current.Headroom() <= kMinimumHeadroom ||
									 totalAdditional > current.Headroom() - kMinimumHeadroom ||
									 (static_cast<double>(current.usageBytes) + static_cast<double>(totalAdditional)) / static_cast<double>(current.budgetBytes) >= kSuspendBudgetRatio)) {
				Suspend(nowMs);
				return false;
			}
			if (current.valid && additionalBytes && !attemptObserved)
				BeginAttempt(nowMs, false);
			return true;
		}

		/** Call once a complete intended stereo transaction has successfully evaluated. */
		void Succeeded(std::uint64_t nowMs) noexcept
		{
			if (phase == MemoryRecoveryPhase::Rebuilding) {
				phase = MemoryRecoveryPhase::Probation;
				lastResumeMs = nowMs;
				BreakActiveObservation();
			}
			if (phase != MemoryRecoveryPhase::Probation && phase != MemoryRecoveryPhase::Ready)
				return;
			if (!sample.IsFresh(nowMs)) {
				BreakActiveObservation();
				return;
			}
			if (!activeWindow || nowMs < lastActiveSuccessMs || nowMs - lastActiveSuccessMs > kMaximumSampleGapMs ||
				sample.sampledAtMs < lastActiveSampleMs || sample.sampledAtMs - lastActiveSampleMs > kMaximumSampleGapMs) {
				activeWindow = true;
				activeSinceMs = nowMs;
			}
			lastActiveSuccessMs = nowMs;
			lastActiveSampleMs = sample.sampledAtMs;
			const auto activeMs = sample.sampledAtMs >= activeSinceMs ? sample.sampledAtMs - activeSinceMs : 0;
			if (phase == MemoryRecoveryPhase::Probation && activeMs >= kProbationMs) {
				phase = MemoryRecoveryPhase::Ready;
				dlssWarning = false;
				++resumes;
			}
			if (activeMs >= kRetryResetMs) {
				retryLevel = 0;
				failedAdmissionCapacityBytes = 0;
				failedAdmissionBarrier = false;
				attemptObserved = false;
			}
		}

	private:
		struct StartupDemand
		{
			std::uint64_t observedBytes = 0;
			std::uint64_t knownFloorBytes = 0;
			std::uint64_t failedCapacityBytes = 0;
			std::uint64_t learnedAtMs = 0;
			bool failedBarrier = false;
		};
		std::array<StartupDemand, 2> demandByStartup_{};

		bool attemptObserved = false;
		bool attemptWasRecovery = false;
		std::uint64_t attemptStartedMs = 0;
		std::uint64_t attemptBaselineBytes = 0;
		std::uint64_t attemptPeakBytes = 0;
		std::uint64_t attemptBudgetBytes = 0;
		std::uint64_t attemptCapacityBytes = 0;
		std::uint64_t attemptKnownBytes = 0;

		[[nodiscard]] static constexpr std::uint64_t Add(std::uint64_t left, std::uint64_t right) noexcept
		{
			return right > UINT64_MAX - left ? UINT64_MAX : left + right;
		}

		[[nodiscard]] static std::uint64_t RecoveryCapacity(const MemoryBudgetSample& current, std::uint64_t pending) noexcept
		{
			const auto ratioLimit = current.budgetBytes / 100 * kResumeBudgetPercent + current.budgetBytes % 100 * kResumeBudgetPercent / 100;
			const auto ratioRoom = ratioLimit > current.usageBytes ? ratioLimit - current.usageBytes : 0;
			const auto absoluteRoom = current.Headroom() > kMinimumHeadroom ? current.Headroom() - kMinimumHeadroom : 0;
			const auto room = std::min(ratioRoom, absoluteRoom);
			return room > pending ? room - pending : 0;
		}

		void BeginAttempt(std::uint64_t nowMs, bool recovery) noexcept
		{
			attemptObserved = true;
			attemptWasRecovery = recovery;
			attemptStartedMs = nowMs;
			attemptBaselineBytes = attemptPeakBytes = sample.usageBytes;
			attemptBudgetBytes = sample.budgetBytes;
			attemptCapacityBytes = RecoveryCapacity(sample, pendingBytes);
			attemptKnownBytes = requiredBytes;
			knownRecoveryFloorBytes = std::max(knownRecoveryFloorBytes, requiredBytes);
		}

		void ObserveAttempt(std::uint64_t nowMs) noexcept
		{
			if (!attemptObserved || !sample.valid || nowMs < attemptStartedMs || nowMs - attemptStartedMs > kRetryResetMs)
				return;
			attemptPeakBytes = std::max(attemptPeakBytes, sample.usageBytes);
			attemptKnownBytes = std::max(attemptKnownBytes, requiredBytes);
		}

		void LearnFailedDemand(std::uint64_t nowMs) noexcept
		{
			if (nowMs < attemptStartedMs || nowMs - attemptStartedMs > kRetryResetMs)
				return;
			const auto growth = attemptPeakBytes - attemptBaselineBytes;
			// Process growth includes scene uploads; bounded aging prevents permanent exclusion.
			const auto demand = std::min(attemptBudgetBytes, std::max(attemptKnownBytes, Add(growth, kDemandMarginBytes)));
			knownRecoveryFloorBytes = std::max(knownRecoveryFloorBytes, std::min(attemptKnownBytes, attemptBudgetBytes));
			observedRecoveryDemandBytes = std::max(observedRecoveryDemandBytes, demand);
			failedAdmissionCapacityBytes = std::max(failedAdmissionCapacityBytes, Add(attemptCapacityBytes, kDemandMarginBytes));
			failedAdmissionBarrier = true;
			demandLearnedAtMs = nowMs;
		}

		void AgeDemand(std::uint64_t nowMs) noexcept
		{
			if ((!failedAdmissionBarrier && observedRecoveryDemandBytes <= knownRecoveryFloorBytes) ||
				nowMs < demandLearnedAtMs || nowMs - demandLearnedAtMs < kDemandProbeIntervalMs)
				return;
			const auto excess = observedRecoveryDemandBytes > knownRecoveryFloorBytes ? observedRecoveryDemandBytes - knownRecoveryFloorBytes : 0;
			observedRecoveryDemandBytes = knownRecoveryFloorBytes + (excess > kDemandMarginBytes ? excess / 2 : 0);
			failedAdmissionBarrier = false;
			failedAdmissionCapacityBytes = 0;
			demandLearnedAtMs = nowMs;
			++demandProbes;
			ClearHealthyWindow();
		}

		[[nodiscard]] bool AdmitRecovery(std::uint64_t nowMs) noexcept
		{
			if (sample.valid)
				AgeDemand(nowMs);
			admissionDemandBytes = std::max({ requiredBytes, observedRecoveryDemandBytes, knownRecoveryFloorBytes });
			const auto capacity = RecoveryCapacity(sample, pendingBytes);
			if (!sample.valid || sample.Headroom() <= kMinimumHeadroom || pendingBytes > sample.Headroom() - kMinimumHeadroom ||
				(static_cast<double>(sample.usageBytes) + static_cast<double>(pendingBytes)) / static_cast<double>(sample.budgetBytes) > kResumeBudgetRatio ||
				admissionDemandBytes > capacity || (failedAdmissionBarrier && capacity <= failedAdmissionCapacityBytes)) {
				ClearHealthyWindow();
				return false;
			}
			if (!healthyWindow || sample.sampledAtMs < lastHealthySampleMs ||
				sample.sampledAtMs - lastHealthySampleMs > kMaximumSampleGapMs) {
				healthyWindow = true;
				healthySinceMs = sample.sampledAtMs;
			}
			lastHealthySampleMs = sample.sampledAtMs;
			if (sample.sampledAtMs < retryAfterMs || sample.sampledAtMs - healthySinceMs < kStableHeadroomMs)
				return false;
			phase = MemoryRecoveryPhase::Rebuilding;
			++rebuildAttempts;
			BeginAttempt(nowMs, true);
			return true;
		}
	};
}
