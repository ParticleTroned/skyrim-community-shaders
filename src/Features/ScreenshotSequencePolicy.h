#pragma once

#include <chrono>
#include <cstdint>

namespace CSX::ScreenshotPolicy
{
	// Catch-up receipts must not monopolize a render frame after a long stall.
	inline constexpr std::uint32_t MaximumSequenceSlotsPerTick = 8;

	/** Render-observed cadence; admission and paused time do not consume slots. */
	struct SequenceClock
	{
		using Clock = std::chrono::steady_clock;
		uint64_t nextFrame = 0;
		Clock::time_point nextTime{};
		bool started = false;
		bool paused = false;

		/** Returns false on clock regression; the owner must stop and drain. */
		bool Observe(uint64_t a_frame, Clock::time_point a_now, bool a_paused,
			uint32_t a_startDelayFrames, uint32_t a_startDelayMs)
		{
			if (observed && (a_frame < lastFrame || a_now < lastTime))
				return false;
			observed = true;
			lastFrame = a_frame;
			lastTime = a_now;
			if (!started) {
				if (!a_paused) {
					nextFrame = a_frame + a_startDelayFrames;
					nextTime = a_now + std::chrono::milliseconds(a_startDelayMs);
					started = true;
				}
			} else if (a_paused && !paused) {
				pausedFrame = a_frame;
				pausedTime = a_now;
			} else if (!a_paused && paused) {
				nextFrame += a_frame - pausedFrame;
				nextTime += a_now - pausedTime;
			}
			paused = a_paused;
			return true;
		}

		/** Wall-clock slots remain eligible until the following interval begins. */
		static bool Missed(bool a_gameFrames, uint64_t a_targetFrame, Clock::time_point a_targetTime,
			uint32_t a_intervalMs, uint64_t a_frame, Clock::time_point a_now)
		{
			return a_gameFrames ? a_frame > a_targetFrame :
			                      a_now >= a_targetTime && a_now - a_targetTime >= std::chrono::milliseconds(a_intervalMs);
		}

		/** Checks the current slot without moving its original deadline. */
		bool Due(bool a_gameFrames, uint64_t a_frame, Clock::time_point a_now) const
		{
			return started && !paused && (a_gameFrames ? a_frame >= nextFrame : a_now >= nextTime);
		}

		/** Consumes exactly one slot, including explicitly dropped slots. */
		void Advance(uint32_t a_intervalFrames, uint32_t a_intervalMs)
		{
			nextFrame += a_intervalFrames;
			nextTime += std::chrono::milliseconds(a_intervalMs);
		}

	private:
		bool observed = false;
		uint64_t lastFrame = 0;
		uint64_t pausedFrame = 0;
		Clock::time_point lastTime{};
		Clock::time_point pausedTime{};
	};
}
