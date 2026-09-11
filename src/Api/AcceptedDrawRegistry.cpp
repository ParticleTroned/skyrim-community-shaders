#include "Api/AcceptedDrawRegistry.h"

namespace CSX::Api
{
	using namespace CSXAcceptedDrawAPI;
	thread_local AcceptedDrawRegistry::Delivery AcceptedDrawRegistry::delivery{};
	std::atomic_uintptr_t AcceptedDrawRegistry::nextToken{ 1 };

	uint32_t AcceptedDrawRegistry::Register(ObserverFn a_observer, void* a_user, uint64_t* a_subscription)
	{
		if (a_subscription)
			*a_subscription = 0;
		if (!a_observer || !a_subscription || IsDispatching())
			return InvalidArgument;
		std::scoped_lock lock(lifecycle);
		for (auto& slot : slots) {
			if (slot.id != 0)
				continue;
			if (nextSubscription == 0)
				return Failed;
			slot.observer = a_observer;
			slot.user = a_user;
			slot.id = nextSubscription++;
			*a_subscription = slot.id;
			slot.state.store(kActive, std::memory_order_release);
			subscribers.fetch_add(1, std::memory_order_release);
			return Success;
		}
		return NotReady;
	}

	uint32_t AcceptedDrawRegistry::Unregister(uint64_t a_subscription)
	{
		if (!a_subscription || IsDispatching())
			return InvalidArgument;
		std::scoped_lock lock(lifecycle);
		for (auto& slot : slots) {
			if (slot.id != a_subscription)
				continue;
			const auto previous = slot.state.fetch_and(~kActive, std::memory_order_acq_rel);
			if (previous & kActive)
				subscribers.fetch_sub(1, std::memory_order_release);
			for (auto state = slot.state.load(std::memory_order_acquire); state != 0; state = slot.state.load(std::memory_order_acquire))
				slot.state.wait(state, std::memory_order_acquire);
			slot.observer = nullptr;
			slot.user = nullptr;
			slot.id = 0;
			return Success;
		}
		return InvalidArgument;
	}

	uint32_t __cdecl AcceptedDrawRegistry::Replay(void* a_token) noexcept
	{
		auto& active = delivery;
		if (!active.registry)
			return InvalidArgument;
		if (!a_token || reinterpret_cast<uintptr_t>(a_token) != active.token || active.replayed) {
			active.registry->rejectedReplays.fetch_add(1, std::memory_order_relaxed);
			return InvalidArgument;
		}
		active.replayed = true;
		try {
			active.native(active.draw->context, active.draw->arguments);
			active.registry->replays.fetch_add(1, std::memory_order_relaxed);
			return Success;
		} catch (...) {
			active.registry->rejectedReplays.fetch_add(1, std::memory_order_relaxed);
			return Failed;
		}
	}

	void AcceptedDrawRegistry::Dispatch(Draw a_draw, NativeReplay a_replay) noexcept
	{
		if (IsDispatching() || !a_replay || !HasObservers())
			return;
		a_draw.drawId = events.fetch_add(1, std::memory_order_relaxed) + 1;
		a_draw.replay = &Replay;
		for (auto& slot : slots) {
			auto state = slot.state.load(std::memory_order_acquire);
			while (state & kActive) {
				if (!slot.state.compare_exchange_weak(state, state + 1, std::memory_order_acq_rel))
					continue;
				const auto token = nextToken.fetch_add(1, std::memory_order_relaxed);
				a_draw.replayToken = reinterpret_cast<void*>(token);
				delivery = { this, a_replay, &a_draw, token, false };
				callbacks.fetch_add(1, std::memory_order_relaxed);
				try {
					slot.observer(&a_draw, slot.user);
				} catch (...) {
					observerFaults.fetch_add(1, std::memory_order_relaxed);
					if (slot.state.fetch_and(~kActive, std::memory_order_acq_rel) & kActive)
						subscribers.fetch_sub(1, std::memory_order_release);
				}
				delivery = {};
				// Only a closing subscription needs to wake a lifecycle waiter.
				if (slot.state.fetch_sub(1, std::memory_order_acq_rel) == 1)
					slot.state.notify_all();
				break;
			}
		}
	}

	AcceptedDrawRegistry::Statistics AcceptedDrawRegistry::Inspect() const
	{
		return { events.load(), callbacks.load(), replays.load(), rejectedReplays.load(), observerFaults.load(), subscribers.load() };
	}
}
