#pragma once

#include <cstdint>
#include <optional>

namespace NeuralRendering
{
	inline constexpr std::uint64_t kCombatResumeDelayMs = 3000;

	enum class CombatPausePhase : std::uint8_t
	{
		Disabled,
		Ready,
		Combat,
		Cooldown,
		Unavailable
	};

	struct CombatPauseSnapshot
	{
		CombatPausePhase phase = CombatPausePhase::Disabled;
		bool sampled = false;
		std::uint32_t frame = 0;
		std::uint64_t remainingMs = 0;

		/** Whether NR admission must use the ordinary rendering fallback. */
		[[nodiscard]] constexpr bool IsPaused() const noexcept
		{
			return phase != CombatPausePhase::Disabled && phase != CombatPausePhase::Ready;
		}
	};

	[[nodiscard]] constexpr const char* GetCombatPausePhaseName(CombatPausePhase a_phase) noexcept
	{
		switch (a_phase) {
		case CombatPausePhase::Disabled:
			return "disabled";
		case CombatPausePhase::Ready:
			return "ready";
		case CombatPausePhase::Combat:
			return "combat";
		case CombatPausePhase::Cooldown:
			return "cooldown";
		case CombatPausePhase::Unavailable:
			return "unavailable";
		default:
			return "unknown";
		}
	}

	/** Caller serializes access; one immutable decision serves every route and eye in a frame. */
	class CombatPausePolicy
	{
	public:
		[[nodiscard]] constexpr bool HasFrame(std::uint32_t a_frame) const noexcept
		{
			return snapshot.sampled && snapshot.frame == a_frame;
		}

		[[nodiscard]] constexpr CombatPauseSnapshot GetSnapshot() const noexcept { return snapshot; }

		/** Missing player state or paused gameplay interrupts the continuous safe interval. */
		constexpr CombatPauseSnapshot Update(std::uint32_t a_frame, bool a_enabled,
			std::uint64_t a_nowMs, std::optional<bool> a_inCombat) noexcept
		{
			if (HasFrame(a_frame))
				return snapshot;
			if (snapshot.sampled && (a_frame != snapshot.frame + 1u || a_nowMs < lastSampleMs))
				snapshot.phase = CombatPausePhase::Unavailable;
			snapshot.sampled = true;
			snapshot.frame = a_frame;
			snapshot.remainingMs = 0;
			lastSampleMs = a_nowMs;
			if (!a_enabled) {
				snapshot.phase = CombatPausePhase::Disabled;
			} else if (!a_inCombat) {
				snapshot.phase = CombatPausePhase::Unavailable;
			} else if (*a_inCombat) {
				snapshot.phase = CombatPausePhase::Combat;
			} else if (snapshot.IsPaused()) {
				if (snapshot.phase != CombatPausePhase::Cooldown)
					cooldownStartedMs = a_nowMs;
				const auto elapsed = a_nowMs - cooldownStartedMs;
				if (elapsed >= kCombatResumeDelayMs) {
					snapshot.phase = CombatPausePhase::Ready;
				} else {
					snapshot.phase = CombatPausePhase::Cooldown;
					snapshot.remainingMs = kCombatResumeDelayMs - elapsed;
				}
			} else {
				snapshot.phase = CombatPausePhase::Ready;
			}
			return snapshot;
		}

	private:
		CombatPauseSnapshot snapshot;
		std::uint64_t lastSampleMs = 0;
		std::uint64_t cooldownStartedMs = 0;
	};
}
