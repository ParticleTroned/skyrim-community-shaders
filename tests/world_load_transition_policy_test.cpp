#include "Utils/WorldLoadTransitionPolicy.h"

#include <array>
#include <limits>

int main()
{
	using Util::WorldLoadTransition::Advance;
	using Util::WorldLoadTransition::EngineSignals;
	using Util::WorldLoadTransition::Window;
	constexpr uint32_t grace = 120;
	constexpr uint32_t fallback = 36000;
	constexpr EngineSignals saveOnly{ .saving = true };
	static_assert(saveOnly.RequiresPersistenceGuard());
	static_assert(!saveOnly.ReplacesWorldState());
	static_assert(!EngineSignals{}.RequiresPersistenceGuard());

	// Repeated normal/quick/auto saves protect disk persistence but never start
	// a rendered-world transition, including the frames following each save.
	Window ordinarySave;
	for (uint32_t frame = 1; frame <= 400; ++frame) {
		ordinarySave = Advance(ordinarySave, frame < 200 ? saveOnly : EngineSignals{}, frame, grace, fallback);
		if (ordinarySave.active || ordinarySave.startFrame != 0 || ordinarySave.endFrame != 0)
			return 1;
	}

	// All engine evidence of world replacement remains authoritative. Saving
	// concurrently must neither clear its grace early nor extend it afterward.
	constexpr std::array worldSignals{
		EngineSignals{ .loading = true, .saving = true },
		EngineSignals{ .initializingForms = true },
		EngineSignals{ .deferredFormInitialization = true },
		EngineSignals{ .positioningPlayer = true },
	};
	for (const auto signal : worldSignals) {
		if (!signal.ReplacesWorldState() || !signal.RequiresPersistenceGuard())
			return 2;
		auto window = Advance({}, signal, 100, grace, fallback);
		if (!window.active || window.startFrame != 100 || window.endFrame != 220)
			return 3;
		window = Advance(window, signal, 105, grace, fallback);
		if (window.endFrame != 225)
			return 4;
		for (uint32_t frame = 106; frame < 225; ++frame) {
			window = Advance(window, saveOnly, frame, grace, fallback);
			if (!window.active || window.startFrame != 100 || window.endFrame != 225)
				return 5;
		}
		window = Advance(window, saveOnly, 225, grace, fallback);
		if (window.active || window.startFrame != 0 || window.endFrame != 0)
			return 6;
	}

	// PreLoad with no completion deadline is conservative even if a singleton
	// temporarily supplies no signals, or an ordinary save occurs in that gap.
	Window preLoad{ true, 100, 0 };
	preLoad = Advance(preLoad, {}, 220, grace, fallback);
	preLoad = Advance(preLoad, saveOnly, 221, grace, fallback);
	if (!preLoad.active || preLoad.endFrame != 0)
		return 7;
	const auto beforeTimeout = Advance(preLoad, {}, 100 + fallback - 1, grace, fallback);
	if (!beforeTimeout.active)
		return 8;
	if (Advance(preLoad, {}, 100 + fallback, grace, fallback).active)
		return 9;

	// PostLoad/NewGame completion establishes a deadline. A save-only callback
	// does not replace it; a subsequent genuine transition extends protection.
	Window completedLoad{ true, 100, 220 };
	completedLoad = Advance(completedLoad, saveOnly, 219, grace, fallback);
	if (!completedLoad.active || completedLoad.endFrame != 220)
		return 10;
	completedLoad = Advance(completedLoad, { .positioningPlayer = true }, 220, grace, fallback);
	if (!completedLoad.active || completedLoad.endFrame != 340)
		return 11;
	if (Advance(completedLoad, {}, 339, grace, fallback).active == false ||
		Advance(completedLoad, {}, 340, grace, fallback).active)
		return 12;

	// Save activity near counter wrap must not turn a genuine load grace into
	// an immediate release or extend that load for an entire counter cycle.
	constexpr uint32_t lastFrame = std::numeric_limits<uint32_t>::max();
	auto wrapped = Advance({}, { .loading = true }, lastFrame - 60, grace, fallback);
	if (!wrapped.active || wrapped.endFrame != 59)
		return 13;
	wrapped = Advance(wrapped, saveOnly, lastFrame, grace, fallback);
	if (!wrapped.active)
		return 14;
	if (!Advance(wrapped, saveOnly, 58, grace, fallback).active ||
		Advance(wrapped, saveOnly, 59, grace, fallback).active)
		return 15;
	if (Util::WorldLoadTransition::ExtendDeadline(lastFrame - 10, lastFrame - 20, grace) != 99)
		return 16;

	// Save-persistence and mutation guards share these modular deadline
	// primitives. Their wrapped deadlines must remain pending until the exact
	// post-wrap frame, and an older concurrent request must not shorten them.
	const uint32_t wrappedPersistenceEnd =
		Util::WorldLoadTransition::ExtendDeadline(0, lastFrame - 20, grace);
	if (wrappedPersistenceEnd != 99 ||
		Util::WorldLoadTransition::ReachedDeadline(lastFrame, wrappedPersistenceEnd) ||
		Util::WorldLoadTransition::ReachedDeadline(98, wrappedPersistenceEnd) ||
		!Util::WorldLoadTransition::ReachedDeadline(99, wrappedPersistenceEnd)) {
		return 17;
	}
	const uint32_t preservedLongerDeadline =
		Util::WorldLoadTransition::ExtendDeadline(
			wrappedPersistenceEnd, lastFrame - 10, 5);
	if (preservedLongerDeadline != wrappedPersistenceEnd)
		return 18;
	const uint32_t extendedAfterWrap =
		Util::WorldLoadTransition::ExtendDeadline(
			wrappedPersistenceEnd, 10, grace);
	if (extendedAfterWrap != 130 ||
		Util::WorldLoadTransition::ReachedDeadline(129, extendedAfterWrap) ||
		!Util::WorldLoadTransition::ReachedDeadline(130, extendedAfterWrap)) {
		return 19;
	}

	// Zero is reserved as the unfinished-transition sentinel even when ordinary
	// unsigned addition would place a bounded grace period exactly on zero.
	if (Util::WorldLoadTransition::ExtendDeadline(0, lastFrame - 10, 11) != 1)
		return 20;

	return 0;
}
