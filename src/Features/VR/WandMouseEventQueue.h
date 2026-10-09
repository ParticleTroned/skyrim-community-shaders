#pragma once

#include <array>
#include <cstddef>
#include <deque>
#include <optional>
#include <utility>

/** Preserves wand button edges and their pointer positions across ImGui frames. */
template <class Pointer, std::size_t ButtonCount>
class WandMouseEventQueue
{
public:
	static constexpr std::size_t MaximumPendingEvents = 64;

	struct Event
	{
		int button = 0;
		bool down = false;
		Pointer pointer{};
	};

	/** Queues state changes only; overflow clears stale input and requires caller cancellation. */
	bool Push(Event a_event)
	{
		if (a_event.button < 0 || static_cast<std::size_t>(a_event.button) >= ButtonCount ||
			queuedStates[a_event.button] == a_event.down)
			return false;
		if (events.size() >= MaximumPendingEvents) {
			Clear();
			return false;
		}
		queuedStates[a_event.button] = a_event.down;
		events.push_back(std::move(a_event));
		return true;
	}

	/** Dispatches one edge per frame so a short press cannot collapse into its release. */
	std::optional<Event> PopForFrame(int a_frame)
	{
		if (events.empty() || lastDispatchFrame == a_frame)
			return std::nullopt;
		lastDispatchFrame = a_frame;
		Event event = std::move(events.front());
		events.pop_front();
		return event;
	}

	[[nodiscard]] bool HasPendingPress(int a_button) const
	{
		return HasPendingPress(a_button, [](const Pointer&) { return true; });
	}

	/** Tests every queued press so later taps cannot hide an earlier pointer owner. */
	template <class Predicate>
	[[nodiscard]] bool HasPendingPress(int a_button, Predicate&& a_matches) const
	{
		for (const auto& event : events) {
			if (event.button == a_button && event.down && a_matches(event.pointer))
				return true;
		}
		return false;
	}

	void Clear()
	{
		events.clear();
		queuedStates.fill(false);
		lastDispatchFrame = -1;
	}

private:
	std::deque<Event> events;
	std::array<bool, ButtonCount> queuedStates{};
	int lastDispatchFrame = -1;
};
