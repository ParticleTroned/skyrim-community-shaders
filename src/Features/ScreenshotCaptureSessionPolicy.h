#pragma once

#include <algorithm>
#include <cstdint>

namespace ScreenshotCaptureSessionPolicy
{
	inline constexpr std::uint32_t kMaxFrameCount = 240;
	inline constexpr std::uint32_t kMaxFrameInterval = 120;
	inline constexpr std::uint32_t kMaxPreviewFramesPerSecond = 60;

	enum class CaptureSurface
	{
		BoundedProductionSession,
		DiagnosticTexture
	};

	struct AccessDecision
	{
		bool allowed = false;
		bool requiresDeveloperMode = true;
	};

	/**
	 * Keeps ordinary, bounded screenshot sessions independent of the saved log
	 * level while retaining the Developer Mode boundary for diagnostic readback.
	 * The DevBench build option and a present DevBench host remain the opt-in
	 * boundary for external automation control.
	 */
	[[nodiscard]] constexpr AccessDecision ResolveAccess(
		CaptureSurface a_surface,
		bool a_developerMode) noexcept
	{
		switch (a_surface) {
		case CaptureSurface::BoundedProductionSession:
			return { true, false };
		case CaptureSurface::DiagnosticTexture:
			return { a_developerMode, true };
		}
		return {};
	}

	struct ValidatedRequest
	{
		std::uint32_t frameCount = 1;
		std::uint32_t frameInterval = 1;
		std::uint32_t previewFramesPerSecond = 15;
	};

	constexpr ValidatedRequest Validate(
		std::uint32_t a_frameCount,
		std::uint32_t a_frameInterval,
		std::uint32_t a_previewFramesPerSecond)
	{
		return {
			.frameCount = std::clamp(a_frameCount, 1u, kMaxFrameCount),
			.frameInterval = std::clamp(a_frameInterval, 1u, kMaxFrameInterval),
			.previewFramesPerSecond = std::clamp(
				a_previewFramesPerSecond,
				1u,
				kMaxPreviewFramesPerSecond),
		};
	}

	struct CycleGate
	{
		std::uint64_t nextEligibleCycle = 0;
		bool initialized = false;

		[[nodiscard]] constexpr bool IsEligible(std::uint64_t a_cycle) const
		{
			return !initialized || a_cycle >= nextEligibleCycle;
		}

		constexpr void RecordAccepted(
			std::uint64_t a_cycle,
			std::uint32_t a_interval)
		{
			initialized = true;
			const auto interval = std::max(a_interval, 1u);
			const auto remaining = UINT64_MAX - a_cycle;
			nextEligibleCycle = remaining < interval ? UINT64_MAX : a_cycle + interval;
		}
	};

	enum class CompletionState
	{
		Draining,
		Cancelled,
		Failed,
		Complete
	};

	struct Completion
	{
		CompletionState state = CompletionState::Failed;
		bool needsDefaultError = false;
	};

	constexpr Completion ResolveCompletion(
		std::uint32_t a_framesQueued,
		std::uint32_t a_framesFinished,
		std::uint32_t a_framesSaved,
		std::uint32_t a_framesFailed,
		bool a_cancelRequested,
		bool a_hasError)
	{
		if (a_framesFinished < a_framesQueued)
			return { CompletionState::Draining, false };
		if (a_cancelRequested)
			return { CompletionState::Cancelled, false };
		if (a_framesSaved == 0 || a_framesFailed != 0 || a_hasError)
			return { CompletionState::Failed, !a_hasError };
		return { CompletionState::Complete, false };
	}
}
