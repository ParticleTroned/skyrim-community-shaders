#pragma once

#include <algorithm>
#include <cstdint>

namespace NeuralRendering
{
	enum class MemoryRecoveryPhase
	{
		Ready,
		Retiring,
		Waiting,
		Rebuilding
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

	/** Session-only admission; retirement must complete before rebuilding. */
	struct MemoryRecoveryPolicy
	{
		static constexpr std::uint64_t kMiB = 1024ull * 1024;
		static constexpr std::uint64_t kMinimumHeadroom = 512 * kMiB;
		static constexpr double kSuspendBudgetRatio = 0.875;
		static constexpr double kResumeBudgetRatio = 0.85;
		static constexpr std::uint64_t kSampleIntervalMs = 250;
		static constexpr std::uint64_t kStableHeadroomMs = 500;
		static constexpr std::uint64_t kMaximumSampleGapMs = MemoryBudgetSample::kMaximumAgeMs;
		static constexpr std::uint64_t kRetryResetMs = 30000;
		static constexpr std::uint32_t kMaximumRetryLevel = 6;
		// Native allocation bytes are private; this is a reserve, not a measurement.
		static constexpr std::uint64_t kNativeFeatureReserve = 128 * kMiB;
		MemoryRecoveryPhase phase = MemoryRecoveryPhase::Ready;
		MemoryBudgetSample sample{};
		std::uint64_t requiredBytes = 0;
		std::uint64_t retryAfterMs = 0;
		std::uint64_t healthySinceMs = 0;
		std::uint64_t lastHealthySampleMs = 0;
		bool healthyWindow = false;
		std::uint64_t lastResumeMs = 0;
		std::uint32_t retryLevel = 0;
		std::uint64_t suspensions = 0;
		std::uint64_t retirements = 0;
		std::uint64_t resumes = 0;
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

		void Suspend(std::uint64_t nowMs, bool outOfMemory = false) noexcept
		{
			if (phase == MemoryRecoveryPhase::Retiring || phase == MemoryRecoveryPhase::Waiting)
				return;
			const bool wasReady = phase == MemoryRecoveryPhase::Ready;
			++suspensions;
			phase = MemoryRecoveryPhase::Retiring;
			ClearHealthyWindow();
			if (outOfMemory)
				++outOfMemoryFailures;
			if (wasReady && resumes && nowMs >= lastResumeMs && nowMs - lastResumeMs >= kRetryResetMs)
				retryLevel = 0;
			retryLevel = std::min(kMaximumRetryLevel, retryLevel + 1);
			const auto delay = std::max<std::uint64_t>(outOfMemory ? 2000 : 500,
				std::min<std::uint64_t>(10000, 500ull << (retryLevel - 1)));
			retryAfterMs = nowMs + delay;
		}

		void Retired() noexcept
		{
			if (phase != MemoryRecoveryPhase::Retiring)
				return;
			phase = MemoryRecoveryPhase::Waiting;
			ClearHealthyWindow();
			++retirements;
		}

		/** A valid DLSS output can still require NR retirement before another evaluation. */
		void ReportDlssWarning(std::uint64_t nowMs) noexcept
		{
			Suspend(nowMs);
			dlssWarning = true;
			++dlssWarnings;
			ClearHealthyWindow();
			retryAfterMs = std::max(retryAfterMs, nowMs + kStableHeadroomMs);
		}

		/** Unknown samples preserve ordinary admission but cannot admit recovery. */
		[[nodiscard]] bool Admit(MemoryBudgetSample current, std::uint64_t additionalBytes, std::uint64_t nowMs) noexcept
		{
			current.valid = current.IsFresh(nowMs);
			sample = current;
			requiredBytes = additionalBytes;
			if (phase == MemoryRecoveryPhase::Retiring)
				return false;
			if (phase == MemoryRecoveryPhase::Rebuilding && !current.valid) {
				Suspend(nowMs);
				return false;
			}
			const auto ratio = current.valid && current.budgetBytes ?
			                       static_cast<double>(current.usageBytes) / static_cast<double>(current.budgetBytes) :
			                       0.0;
			if (phase == MemoryRecoveryPhase::Waiting) {
				if (!current.valid || current.sampledAtMs < retryAfterMs || ratio > kResumeBudgetRatio ||
					current.Headroom() <= kMinimumHeadroom || additionalBytes > current.Headroom() - kMinimumHeadroom ||
					(static_cast<double>(current.usageBytes) + static_cast<double>(additionalBytes)) / static_cast<double>(current.budgetBytes) > kResumeBudgetRatio) {
					ClearHealthyWindow();
					return false;
				}
				if (!healthyWindow || current.sampledAtMs < lastHealthySampleMs ||
					current.sampledAtMs - lastHealthySampleMs > kMaximumSampleGapMs) {
					healthyWindow = true;
					healthySinceMs = current.sampledAtMs;
				}
				lastHealthySampleMs = current.sampledAtMs;
				if (current.sampledAtMs - healthySinceMs < kStableHeadroomMs)
					return false;
				phase = MemoryRecoveryPhase::Rebuilding;
				return true;
			}

			if (current.valid && (ratio >= kSuspendBudgetRatio || current.Headroom() <= kMinimumHeadroom ||
									 additionalBytes > current.Headroom() - kMinimumHeadroom ||
									 (static_cast<double>(current.usageBytes) + static_cast<double>(additionalBytes)) / static_cast<double>(current.budgetBytes) >= kSuspendBudgetRatio)) {
				Suspend(nowMs);
				return false;
			}
			return true;
		}

		void Succeeded(std::uint64_t nowMs) noexcept
		{
			if (phase == MemoryRecoveryPhase::Rebuilding) {
				phase = MemoryRecoveryPhase::Ready;
				dlssWarning = false;
				++resumes;
				lastResumeMs = nowMs;
			}
		}
	};
}
