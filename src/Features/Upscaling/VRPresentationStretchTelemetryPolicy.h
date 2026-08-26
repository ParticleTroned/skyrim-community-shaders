#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace VRPresentationStretchTelemetryPolicy
{
	inline constexpr std::uint64_t kCompositorCycleTokenMaximum =
		std::numeric_limits<std::uint64_t>::max() >> 1u;
	inline constexpr std::uint64_t kMaximumAcceptedPresentationStretchFrames = 2;
	inline constexpr std::uint8_t kCompleteStereoEyeMask = 0x3;

	enum class ObservationKind : std::uint8_t
	{
		AllowedStretch,
		VendorFailureStretch,
		BoundsMismatchFallback,
		Other
	};

	struct Observation
	{
		ObservationKind kind = ObservationKind::Other;
		std::uint32_t eyeIndex = 0;
		std::uint32_t frame = 0;
		std::uint64_t compositorCycleToken = 0;
		// QueryPerformanceCounter ticks. Zero means timing was unavailable.
		std::uint64_t qpc = 0;
	};

	struct EpisodeMetrics
	{
		// Monotonic, saturating counters. Durations are QPC ticks.
		std::uint64_t episodes = 0;
		std::uint64_t completedEpisodes = 0;
		std::uint64_t timedCompletedEpisodes = 0;
		std::uint64_t completedFrames = 0;
		std::uint64_t completedQpcTicks = 0;
		std::uint64_t maximumFrames = 0;
		std::uint64_t maximumQpcTicks = 0;
		bool active = false;
		std::uint64_t activeFrames = 0;
		std::uint64_t activeStartQpc = 0;
		std::uint64_t activeLastAcceptedQpc = 0;
		bool activeQpcTimingValid = false;
		std::uint64_t activeLastCycleToken = 0;
		std::uint32_t activeLastFrame = 0;
	};

	struct PendingCycle
	{
		bool valid = false;
		bool allObservedEyesAllowed = true;
		std::uint8_t observedEyeMask = 0;
		std::uint32_t frame = 0;
		std::uint64_t compositorCycleToken = 0;
		std::uint64_t firstQpc = 0;
		std::uint64_t lastObservedQpc = 0;
		bool qpcTimingValid = false;
	};

	struct State
	{
		EpisodeMetrics metrics{};
		PendingCycle pending{};
		std::uint64_t lastObservedQpc = 0;
		std::uint64_t lastFinalizedQpc = 0;
	};

	struct Snapshot
	{
		std::uint64_t episodes = 0;
		std::uint64_t completedEpisodes = 0;
		std::uint64_t timedCompletedEpisodes = 0;
		std::uint64_t completedFrames = 0;
		std::uint64_t completedQpcTicks = 0;
		std::uint64_t maximumFrames = 0;
		std::uint64_t maximumQpcTicks = 0;
		bool active = false;
		std::uint64_t activeFrames = 0;
		std::uint64_t activeQpcTicks = 0;
		bool activeQpcTimingAvailable = false;
	};

	struct StopResult
	{
		Snapshot snapshot{};
		bool activeAtStop = false;
		std::uint64_t activeFramesAtStop = 0;
		std::uint64_t activeQpcTicksAtStop = 0;
		bool activeQpcTimingAvailableAtStop = false;
		bool incompleteStereoCycleAtStop = false;
		std::uint8_t incompleteStereoEyeMaskAtStop = 0;
	};

	[[nodiscard]] constexpr std::uint64_t SaturatingAdd(
		std::uint64_t a_left,
		std::uint64_t a_right) noexcept
	{
		const auto maximum = std::numeric_limits<std::uint64_t>::max();
		return a_right > maximum - a_left ? maximum : a_left + a_right;
	}

	constexpr void SaturatingIncrement(std::uint64_t& a_counter) noexcept
	{
		if (a_counter != std::numeric_limits<std::uint64_t>::max())
			++a_counter;
	}

	[[nodiscard]] constexpr bool IsSameCycle(
		const PendingCycle& a_pending,
		const Observation& a_observation) noexcept
	{
		if (!a_pending.valid)
			return false;
		if (a_pending.compositorCycleToken != 0 ||
			a_observation.compositorCycleToken != 0) {
			return a_pending.compositorCycleToken != 0 &&
			       a_pending.compositorCycleToken ==
			           a_observation.compositorCycleToken;
		}
		return a_pending.frame == a_observation.frame;
	}

	[[nodiscard]] constexpr bool IsConsecutiveCycle(
		std::uint64_t a_previousCycle,
		std::uint32_t a_previousFrame,
		std::uint64_t a_currentCycle,
		std::uint32_t a_currentFrame) noexcept
	{
		if (a_previousCycle != 0 && a_currentCycle != 0) {
			return a_previousCycle == kCompositorCycleTokenMaximum ?
			           a_currentCycle == 1u :
			           a_previousCycle < kCompositorCycleTokenMaximum &&
			               a_currentCycle == a_previousCycle + 1u;
		}
		return a_previousFrame == std::numeric_limits<std::uint32_t>::max() ?
		           a_currentFrame == 1u :
		           a_currentFrame == a_previousFrame + 1u;
	}

	constexpr void CompleteActiveEpisode(
		EpisodeMetrics& a_metrics,
		std::uint64_t a_endQpc) noexcept
	{
		if (!a_metrics.active)
			return;

		SaturatingIncrement(a_metrics.completedEpisodes);
		a_metrics.completedFrames =
			SaturatingAdd(a_metrics.completedFrames, a_metrics.activeFrames);
		a_metrics.maximumFrames =
			std::max(a_metrics.maximumFrames, a_metrics.activeFrames);
		if (a_metrics.activeQpcTimingValid &&
			a_metrics.activeStartQpc != 0 &&
			a_metrics.activeLastAcceptedQpc != 0 &&
			a_endQpc != 0 &&
			a_endQpc >= a_metrics.activeStartQpc &&
			a_endQpc >= a_metrics.activeLastAcceptedQpc) {
			const std::uint64_t duration =
				a_endQpc - a_metrics.activeStartQpc;
			SaturatingIncrement(a_metrics.timedCompletedEpisodes);
			a_metrics.completedQpcTicks =
				SaturatingAdd(a_metrics.completedQpcTicks, duration);
			a_metrics.maximumQpcTicks =
				std::max(a_metrics.maximumQpcTicks, duration);
		}

		a_metrics.active = false;
		a_metrics.activeFrames = 0;
		a_metrics.activeStartQpc = 0;
		a_metrics.activeLastAcceptedQpc = 0;
		a_metrics.activeQpcTimingValid = false;
		a_metrics.activeLastCycleToken = 0;
		a_metrics.activeLastFrame = 0;
	}

	constexpr void StartActiveEpisode(
		EpisodeMetrics& a_metrics,
		const PendingCycle& a_cycle,
		bool a_qpcTimingValid) noexcept
	{
		SaturatingIncrement(a_metrics.episodes);
		a_metrics.active = true;
		a_metrics.activeFrames = 1;
		a_metrics.activeStartQpc = a_cycle.firstQpc;
		a_metrics.activeLastAcceptedQpc = a_cycle.lastObservedQpc;
		a_metrics.activeQpcTimingValid = a_qpcTimingValid;
		a_metrics.activeLastCycleToken = a_cycle.compositorCycleToken;
		a_metrics.activeLastFrame = a_cycle.frame;
	}

	constexpr void FinalizePendingCycle(
		State& a_state,
		bool a_finalizeIncomplete = false) noexcept
	{
		if (!a_state.pending.valid)
			return;
		if (a_state.pending.observedEyeMask != kCompleteStereoEyeMask &&
			!a_finalizeIncomplete)
			return;

		auto& metrics = a_state.metrics;
		const auto& cycle = a_state.pending;
		const bool allowedCycle =
			cycle.observedEyeMask == kCompleteStereoEyeMask &&
			cycle.allObservedEyesAllowed;
		const bool cycleQpcTimingValid =
			cycle.qpcTimingValid && cycle.firstQpc != 0 &&
			cycle.lastObservedQpc != 0 &&
			cycle.firstQpc >= a_state.lastFinalizedQpc;
		if (!allowedCycle) {
			CompleteActiveEpisode(metrics, cycle.firstQpc);
			if (cycleQpcTimingValid)
				a_state.lastFinalizedQpc = cycle.lastObservedQpc;
			a_state.pending = {};
			return;
		}

		const bool continuesActiveEpisode =
			metrics.active &&
			IsConsecutiveCycle(
				metrics.activeLastCycleToken,
				metrics.activeLastFrame,
				cycle.compositorCycleToken,
				cycle.frame);
		if (!continuesActiveEpisode) {
			// A cycle gap ends at the last accepted observation so missing wall
			// time is never attributed to the preceding episode.
			CompleteActiveEpisode(metrics, metrics.activeLastAcceptedQpc);
			StartActiveEpisode(metrics, cycle, cycleQpcTimingValid);
		} else {
			metrics.activeFrames = SaturatingAdd(metrics.activeFrames, 1u);
			metrics.activeQpcTimingValid =
				metrics.activeQpcTimingValid && cycleQpcTimingValid &&
				cycle.firstQpc >= metrics.activeLastAcceptedQpc;
			metrics.activeLastAcceptedQpc =
				std::max(metrics.activeLastAcceptedQpc, cycle.lastObservedQpc);
			metrics.activeLastCycleToken = cycle.compositorCycleToken;
			metrics.activeLastFrame = cycle.frame;
		}
		if (cycleQpcTimingValid)
			a_state.lastFinalizedQpc = cycle.lastObservedQpc;
		a_state.pending = {};
	}

	constexpr void Observe(State& a_state, const Observation& a_observation) noexcept
	{
		if (a_observation.eyeIndex >= 2)
			return;
		const bool qpcTimingValid =
			a_observation.qpc != 0 &&
			(a_state.lastObservedQpc == 0 ||
				a_observation.qpc >= a_state.lastObservedQpc);
		if (qpcTimingValid)
			a_state.lastObservedQpc = a_observation.qpc;

		if (!IsSameCycle(a_state.pending, a_observation)) {
			FinalizePendingCycle(a_state, true);
			a_state.pending.valid = true;
			a_state.pending.frame = a_observation.frame;
			a_state.pending.compositorCycleToken =
				a_observation.compositorCycleToken;
			a_state.pending.firstQpc = a_observation.qpc;
			a_state.pending.lastObservedQpc = a_observation.qpc;
			a_state.pending.qpcTimingValid = qpcTimingValid;
		} else {
			a_state.pending.qpcTimingValid =
				a_state.pending.qpcTimingValid && qpcTimingValid &&
				a_observation.qpc >= a_state.pending.lastObservedQpc;
			a_state.pending.lastObservedQpc =
				std::max(a_state.pending.lastObservedQpc, a_observation.qpc);
		}

		const auto eyeMask = static_cast<std::uint8_t>(1u << a_observation.eyeIndex);
		a_state.pending.observedEyeMask |= eyeMask;
		if (a_observation.kind != ObservationKind::AllowedStretch)
			a_state.pending.allObservedEyesAllowed = false;
	}

	[[nodiscard]] constexpr Snapshot Inspect(
		const State& a_state,
		std::uint64_t a_nowQpc) noexcept
	{
		State projected = a_state;
		FinalizePendingCycle(projected);
		const auto& metrics = projected.metrics;
		Snapshot snapshot{
			.episodes = metrics.episodes,
			.completedEpisodes = metrics.completedEpisodes,
			.timedCompletedEpisodes = metrics.timedCompletedEpisodes,
			.completedFrames = metrics.completedFrames,
			.completedQpcTicks = metrics.completedQpcTicks,
			.maximumFrames = metrics.maximumFrames,
			.maximumQpcTicks = metrics.maximumQpcTicks,
			.active = metrics.active,
			.activeFrames = metrics.activeFrames,
		};
		if (metrics.active &&
			metrics.activeQpcTimingValid &&
			metrics.activeStartQpc != 0 &&
			metrics.activeLastAcceptedQpc != 0 &&
			a_nowQpc != 0 &&
			a_nowQpc >= metrics.activeStartQpc &&
			a_nowQpc >= metrics.activeLastAcceptedQpc) {
			snapshot.activeQpcTicks = a_nowQpc - metrics.activeStartQpc;
			snapshot.activeQpcTimingAvailable = true;
		}
		return snapshot;
	}

	[[nodiscard]] constexpr StopResult Stop(
		State& a_state,
		std::uint64_t a_nowQpc) noexcept
	{
		const bool incompleteStereoCycle =
			a_state.pending.valid &&
			a_state.pending.observedEyeMask != kCompleteStereoEyeMask;
		const std::uint8_t incompleteStereoEyeMask =
			incompleteStereoCycle ? a_state.pending.observedEyeMask : 0;
		if (!incompleteStereoCycle)
			FinalizePendingCycle(a_state);
		const Snapshot active = Inspect(a_state, a_nowQpc);
		StopResult result{
			.activeAtStop = active.active,
			.activeFramesAtStop = active.activeFrames,
			.activeQpcTicksAtStop = active.activeQpcTicks,
			.activeQpcTimingAvailableAtStop =
				active.activeQpcTimingAvailable,
			.incompleteStereoCycleAtStop = incompleteStereoCycle,
			.incompleteStereoEyeMaskAtStop = incompleteStereoEyeMask,
		};
		CompleteActiveEpisode(a_state.metrics, a_nowQpc);
		// The incomplete cycle remains evidence only; it never contributes a frame.
		a_state.pending = {};
		result.snapshot = Inspect(a_state, a_nowQpc);
		return result;
	}

	constexpr void Reset(State& a_state) noexcept
	{
		a_state = {};
	}
}
