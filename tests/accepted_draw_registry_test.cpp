#include "Api/AcceptedDrawGeometryScope.h"
#include "Api/AcceptedDrawRegistry.h"

#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
	using namespace CSXAcceptedDrawAPI;
	using CSX::Api::AcceptedDrawRegistry;
	void Check(bool a_value, const char* a_message)
	{
		if (!a_value)
			throw std::runtime_error(a_message);
	}
	Draw MakeDraw()
	{
		return { sizeof(Draw), Version, 0, reinterpret_cast<ID3D11DeviceContext*>(0x1000),
			reinterpret_cast<void*>(0x2000), reinterpret_cast<ID3D11Texture2D*>(0x3000),
			MainScene, PackedStereo2D, { IndexedInstanced, 12, 2, 3, -4, 5 }, nullptr, nullptr };
	}
	uint32_t nativeCalls = 0;
	void Native(ID3D11DeviceContext* a_context, const Arguments& a_args)
	{
		Check(a_context == MakeDraw().context && a_args.kind == IndexedInstanced && a_args.indexCount == 12 &&
				  a_args.instanceCount == 2 && a_args.startIndex == 3 && a_args.baseVertex == -4 && a_args.startInstance == 5,
			"replay changed draw arguments");
		++nativeCalls;
	}
	struct Observer
	{
		AcceptedDrawRegistry* registry;
		uint64_t subscription = 0, lastId = 0;
		uint32_t calls = 0;
		ReplayFn replay = nullptr;
		void* staleToken = nullptr;
		bool passed = true;
		static void __cdecl Receive(const Draw* a_draw, void* a_user)
		{
			auto& self = *static_cast<Observer*>(a_user);
			++self.calls;
			self.passed &= a_draw->drawId > self.lastId && a_draw->geometry == MakeDraw().geometry && a_draw->sceneDepth == MakeDraw().sceneDepth;
			self.lastId = a_draw->drawId;
			self.passed &= self.registry->Unregister(self.subscription) == InvalidArgument;
			uint64_t blocked = 42;
			self.passed &= self.registry->Register(&Receive, &self, &blocked) == InvalidArgument && blocked == 0;
			if (self.staleToken)
				self.passed &= a_draw->replay(self.staleToken) == InvalidArgument;
			self.replay = a_draw->replay;
			self.staleToken = a_draw->replayToken;
			auto wrongThread = std::async(std::launch::async, [a_draw] { return a_draw->replay(a_draw->replayToken); });
			self.passed &= wrongThread.get() == InvalidArgument;
			self.registry->Dispatch(MakeDraw(), &Native);
			self.passed &= a_draw->replay(a_draw->replayToken) == Success;
			self.passed &= a_draw->replay(a_draw->replayToken) == InvalidArgument;
		}
	};

	void TestReplay()
	{
		AcceptedDrawRegistry registry;
		Observer first{ &registry }, second{ &registry };
		Check(registry.Register(nullptr, nullptr, &first.subscription) == InvalidArgument, "null callback accepted");
		Check(registry.Register(&Observer::Receive, &first, &first.subscription) == Success, "register first failed");
		Check(registry.Register(&Observer::Receive, &second, &second.subscription) == Success, "register second failed");
		registry.Dispatch(MakeDraw(), &Native);
		registry.Dispatch(MakeDraw(), &Native);
		Check(first.passed && second.passed && first.calls == 2 && second.calls == 2 && nativeCalls == 4, "replay lifetime/recursion/arguments contract failed");
		Check(first.replay(first.staleToken) == InvalidArgument, "out-of-callback replay accepted");
		Check(registry.Unregister(first.subscription) == Success && registry.Unregister(second.subscription) == Success, "unregister failed");
		Check(registry.Unregister(first.subscription) == InvalidArgument, "stale subscription accepted");
		registry.Dispatch(MakeDraw(), &Native);
		Check(!registry.HasObservers() && registry.Inspect().events == 2 && registry.Inspect().replays == 4, "empty registry still dispatched");
	}

	struct BlockingObserver
	{
		std::promise<void> entered;
		std::shared_future<void> release;
		static void __cdecl Receive(const Draw*, void* a_user)
		{
			auto& self = *static_cast<BlockingObserver*>(a_user);
			self.entered.set_value();
			self.release.wait();
		}
	};
	void TestQuiescence()
	{
		AcceptedDrawRegistry registry;
		std::promise<void> release;
		BlockingObserver observer{ {}, release.get_future().share() };
		uint64_t subscription = 0;
		Check(registry.Register(&BlockingObserver::Receive, &observer, &subscription) == Success, "blocking registration failed");
		auto entered = observer.entered.get_future();
		std::jthread render([&] { registry.Dispatch(MakeDraw(), &Native); });
		entered.wait();
		std::promise<void> closing;
		auto closed = std::async(std::launch::async, [&] { closing.set_value(); return registry.Unregister(subscription); });
		closing.get_future().wait();
		const auto beforeRelease = closed.wait_for(std::chrono::milliseconds(50));
		release.set_value();
		Check(beforeRelease == std::future_status::timeout && closed.get() == Success, "unregister returned before callback quiescence");
		render.join();
		registry.Dispatch(MakeDraw(), &Native);
		Check(registry.Inspect().callbacks == 1, "callback after unregister");
	}

	void __cdecl Throw(const Draw*, void*) { throw std::runtime_error("observer fault"); }
	void TestFaultAndCapacity()
	{
		AcceptedDrawRegistry registry;
		std::array<uint64_t, 8> subscriptions{};
		for (auto& id : subscriptions)
			Check(registry.Register(&Throw, nullptr, &id) == Success, "bounded slot registration failed");
		uint64_t extra = 42;
		Check(registry.Register(&Throw, nullptr, &extra) == NotReady && !extra, "capacity overflow accepted");
		registry.Dispatch(MakeDraw(), &Native);
		registry.Dispatch(MakeDraw(), &Native);
		Check(registry.Inspect().observerFaults == 8 && !registry.HasObservers(), "faulty observers not disabled");
		for (auto id : subscriptions)
			Check(registry.Unregister(id) == Success, "faulted subscription could not unregister");
		Check(registry.Register(&Throw, nullptr, &extra) == Success && extra > subscriptions.back(), "subscription token reused");
		Check(registry.Unregister(extra) == Success, "reused slot unregister failed");
	}

	void TestGeometryScopes()
	{
		CSX::Api::AcceptedDrawGeometryScope scope;
		int first = 1, second = 2, geometry = 3;
		Check(!scope.Current() && scope.Begin(&first), "initial scope not empty");
		Check(!scope.Current(), "geometry leaked during setup");
		scope.Activate(&first, &geometry);
		Check(scope.Current() == &geometry && scope.Begin(&second) && !scope.Current(), "nested setup inherited parent geometry");
		scope.Activate(&second, &second);
		Check(scope.Current() == &second, "nested geometry unavailable");
		scope.Suspend();
		Check(!scope.Current() && scope.End(&second) && scope.Current() == &geometry, "nested restore lost parent attribution");
		Check(!scope.End(&second) && !scope.Current(), "mismatched restore retained stale geometry");
		for (unsigned i = 0; i < 32; ++i) {
			Check(scope.Begin(&first), "scope capacity too small");
			scope.Activate(&first, &geometry);
		}
		Check(!scope.Begin(&second) && !scope.Current(), "overflow inherited geometry");
		Check(!scope.End(&first) && !scope.Current(), "unverified overflow restore exposed stale geometry");
		for (unsigned i = 0; i < 32; ++i)
			Check(!scope.End(&first) && !scope.Current(), "invalidated scope revived while unwinding");
		Check(scope.Begin(&second), "fresh scope failed after overflow");
		scope.Activate(&second, &geometry);
		Check(scope.Current() == &geometry && scope.End(&second) && !scope.Current(), "fresh scope did not recover");
	}
}

int main()
{
	try {
		TestReplay();
		TestQuiescence();
		TestFaultAndCapacity();
		TestGeometryScopes();
		std::cout << "Accepted draw registry: replay, lifetime, thread, recursion, quiescence, faults and capacity passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
