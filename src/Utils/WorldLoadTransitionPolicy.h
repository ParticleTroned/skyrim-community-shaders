#pragma once

#include <algorithm>
#include <cstdint>

namespace Util::WorldLoadTransition
{
	struct EngineSignals
	{
		bool loading = false;
		bool saving = false;
		bool initializingForms = false;
		bool deferredFormInitialization = false;
		bool positioningPlayer = false;

		constexpr bool ReplacesWorldState() const
		{
			return loading || initializingForms || deferredFormInitialization || positioningPlayer;
		}

		constexpr bool RequiresPersistenceGuard() const
		{
			return saving || ReplacesWorldState();
		}
	};

	struct Window
	{
		bool active = false;
		uint32_t startFrame = 0;
		uint32_t endFrame = 0;
	};

	constexpr bool ReachedDeadline(uint32_t a_frame, uint32_t a_deadline)
	{
		return a_frame - a_deadline < 0x80000000u;
	}

	constexpr uint32_t ExtendDeadline(uint32_t a_previousEndFrame, uint32_t a_frame, uint32_t a_graceFrames)
	{
		// Bounded frame intervals retain their ordering across frame-counter wrap.
		// Zero remains the sentinel for an unfinished PreLoad notification.
		uint32_t endFrame = a_frame + std::clamp(a_graceFrames, 1u, 0x7ffffffeu);
		if (endFrame == 0)
			endFrame = 1;
		return a_previousEndFrame == 0 || ReachedDeadline(a_frame, a_previousEndFrame) ||
		               ReachedDeadline(endFrame, a_previousEndFrame) ?
		           endFrame :
		           a_previousEndFrame;
	}

	// Only actual world replacement extends this window. Saving the existing
	// world has a separate persistence grace and cannot start or extend it.
	constexpr Window Advance(
		Window a_window,
		EngineSignals a_signals,
		uint32_t a_frame,
		uint32_t a_graceFrames,
		uint32_t a_fallbackFrames)
	{
		if (a_signals.ReplacesWorldState()) {
			if (!a_window.active)
				a_window.startFrame = a_frame;
			a_window.active = true;
			a_window.endFrame = ExtendDeadline(a_window.endFrame, a_frame, a_graceFrames);
		} else if (a_window.active) {
			const bool completedGrace = a_window.endFrame != 0 && ReachedDeadline(a_frame, a_window.endFrame);
			const bool missingCompletionTimedOut =
				a_window.endFrame == 0 && a_window.startFrame != 0 &&
				a_frame - a_window.startFrame >= a_fallbackFrames;
			if (completedGrace || missingCompletionTimedOut)
				a_window = {};
		}
		return a_window;
	}
}
