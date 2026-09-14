#pragma once

#include "VRAPI/CSacceptedDrawapi.h"

#include <array>
#include <atomic>
#include <mutex>

namespace CSX::Api
{
	/** Bounded, allocation-free render dispatch; lifecycle waits stay off callbacks. */
	class AcceptedDrawRegistry
	{
	public:
		using NativeReplay = void (*)(ID3D11DeviceContext*, const CSXAcceptedDrawAPI::Arguments&);
		struct Statistics
		{
			uint64_t events, callbacks, replays, rejectedReplays, observerFaults;
			uint32_t subscribers;
		};
		/** Reserve a bounded slot; reject lifecycle calls made from any observer. */
		uint32_t Register(CSXAcceptedDrawAPI::ObserverFn a_observer, void* a_user, uint64_t* a_subscription);
		/** Deactivate a slot and wait for its callbacks before releasing borrowed user state. */
		uint32_t Unregister(uint64_t a_subscription);
		/** Lock-free hint allowing the render path to skip resource inspection. */
		bool HasObservers() const { return subscribers.load(std::memory_order_acquire) != 0; }
		/** Deliver synchronously with single-use replay; contain and deactivate throwing observers. */
		void Dispatch(CSXAcceptedDrawAPI::Draw a_draw, NativeReplay a_replay) noexcept;
		/** Read diagnostic counters; concurrent updates need not form one coherent snapshot. */
		Statistics Inspect() const;
		/** True throughout a callback and its replay on this thread, across registries. */
		static bool IsDispatching() { return delivery.registry != nullptr; }

	private:
		static constexpr uint32_t kActive = 1u << 31;
		struct Slot
		{
			std::atomic_uint32_t state{ 0 };
			CSXAcceptedDrawAPI::ObserverFn observer = nullptr;
			void* user = nullptr;
			uint64_t id = 0;
		};
		struct Delivery
		{
			AcceptedDrawRegistry* registry = nullptr;
			NativeReplay native = nullptr;
			const CSXAcceptedDrawAPI::Draw* draw = nullptr;
			uintptr_t token = 0;
			bool replayed = false;
		};
		static thread_local Delivery delivery;
		static std::atomic_uintptr_t nextToken;
		static uint32_t __cdecl Replay(void* a_token) noexcept;
		std::array<Slot, 8> slots;
		std::mutex lifecycle;
		uint64_t nextSubscription = 1;
		std::atomic_uint32_t subscribers{ 0 };
		std::atomic_uint64_t events{ 0 }, callbacks{ 0 }, replays{ 0 }, rejectedReplays{ 0 }, observerFaults{ 0 };
	};
}
