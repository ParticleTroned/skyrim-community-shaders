#include "Features/LightLimitFix/SceneLightSnapshot.h"

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>

#include "RE/N/NiSmartPointer.h"

namespace
{
	thread_local int allocationsUntilFailure = -1;
	thread_local bool queueLockHeld = false;
	bool verifyCaptureLock = false;
	unsigned loggedFailures = 0;

	void Require(bool a_condition)
	{
		if (!a_condition)
			std::abort();
	}

	struct Light
	{
		explicit Light(std::atomic<unsigned>& a_destroyed) : destroyed(a_destroyed) {}
		virtual ~Light()
		{
			Require(!queueLockHeld);
			++destroyed;
		}
		virtual bool IsShadowLight() const { return true; }
		void IncRefCount()
		{
			Require(!verifyCaptureLock || queueLockHeld);
			++references;
		}
		void DecRefCount()
		{
			if (--references == 0)
				delete this;
		}
		std::atomic<unsigned> references{ 0 };
		std::atomic<unsigned>& destroyed;
	};

	struct ShadowLight : Light
	{
		using Light::Light;
	};

	using Snapshot = LightLimitFixDetail::SceneLightSnapshot<RE::NiPointer<Light>>;
}

void* operator new(std::size_t a_size)
{
	if (allocationsUntilFailure == 0) {
		allocationsUntilFailure = -1;
		throw std::bad_alloc{};
	}
	if (allocationsUntilFailure > 0)
		--allocationsUntilFailure;
	if (auto* memory = std::malloc(a_size ? a_size : 1))
		return memory;
	throw std::bad_alloc{};
}

void operator delete(void* a_memory) noexcept { std::free(a_memory); }
void operator delete(void* a_memory, std::size_t) noexcept { std::free(a_memory); }

namespace logger
{
	void error(const char*) { ++loggedFailures; }
}

namespace RE
{
	using BSLight = Light;

	struct BSSpinLockGuard
	{
		explicit BSSpinLockGuard(std::mutex& a_mutex) : lock(a_mutex) { queueLockHeld = true; }
		~BSSpinLockGuard() { queueLockHeld = false; }
		std::lock_guard<std::mutex> lock;
	};
	struct ShadowSceneNode
	{
		std::vector<NiPointer<Light>> activeLights;
		std::vector<NiPointer<ShadowLight>> activeShadowLights;
		std::vector<NiPointer<Light>> lightQueueAdd;
		std::vector<NiPointer<Light>> lightQueueRemove;
		std::vector<NiPointer<Light>> unk190;
		std::mutex lightQueueLock;
		ShadowSceneNode& GetRuntimeData() { return *this; }
	};
}

struct LightLimitFix
{
	using SceneLightSnapshot = Snapshot;
	std::unordered_map<RE::ShadowSceneNode*, Snapshot> sceneLightSnapshots;
	bool sceneLightSnapshotFailed = false;
	const Snapshot* GetSceneLightSnapshot(RE::ShadowSceneNode* a_node);
};

namespace globals::game
{
	bool isVR = false;
}

#define CS_PROFILE_CPU_SCOPE(...) static_cast<void>(0)
#include "scene_light_snapshot_under_test.h"

const Snapshot* Capture(LightLimitFix& a_fix, RE::ShadowSceneNode* a_node)
{
	verifyCaptureLock = true;
	const auto* snapshot = a_fix.GetSceneLightSnapshot(a_node);
	verifyCaptureLock = false;
	return snapshot;
}

void TestOwnership()
{
	std::atomic<unsigned> destroyed{ 0 };
	std::mutex queueLock;
	std::vector<RE::NiPointer<Light>> active;
	active.push_back(RE::make_nismart<Light>(destroyed));
	auto* passLight = active.front().get();
	{
		Snapshot snapshot;
		{
			std::lock_guard lock{ queueLock };
			for (const auto& owner : active)
				snapshot.Retain(owner, true);
		}
		// Reproduce a worker dropping the engine's last reference before geometry setup.
		std::thread cleanup([&] {
			std::lock_guard lock{ queueLock };
			active.clear();
		});
		cleanup.join();
		Require(destroyed == 0);
		Require(snapshot.Find(passLight) == passLight);
		Require(snapshot.Find(passLight)->IsShadowLight());
		Require(snapshot.ActiveLights().size() == 1);
	}
	Require(destroyed == 1);
	{
		Snapshot nextFrame;
		// No memory read or reference acquisition is permitted through this expired key.
		Require(nextFrame.Find(passLight) == nullptr);
		Require(nextFrame.Find(nullptr) == nullptr);
		Require(nextFrame.Find(reinterpret_cast<Light*>(0x33509950)) == nullptr);
	}

	{
		Snapshot snapshot;
		auto queued = RE::make_nismart<Light>(destroyed);
		auto second = RE::make_nismart<ShadowLight>(destroyed);
		auto* queuedKey = queued.get();
		snapshot.Retain(queued, false);
		Require(snapshot.Find(queuedKey) == queuedKey);
		Require(snapshot.ActiveLights().empty());
		snapshot.Retain(second, true);
		snapshot.Retain(queued, true);
		snapshot.Retain(queued, true);
		snapshot.Retain(queued, false);
		snapshot.Retain(RE::NiPointer<Light>{}, true);
		Require(snapshot.ActiveLights().size() == 2);
		Require(snapshot.ActiveLights()[0] == second.get());
		Require(snapshot.ActiveLights()[1] == queuedKey);
		Require(queued->references == 2);
		queued.reset();
		second.reset();
		Require(destroyed == 1);
		Require(snapshot.Find(queuedKey)->IsShadowLight());
	}
	Require(destroyed == 3);
}

void TestCapture()
{
	std::atomic<unsigned> destroyed{ 0 };
	RE::ShadowSceneNode node;
	std::tuple lists{ &node.activeLights, &node.activeShadowLights, &node.lightQueueAdd, &node.lightQueueRemove, &node.unk190 };
	auto forEachList = [&](auto action) {
		std::apply([&](auto*... list) { (action(*list), ...); }, lists);
	};
	forEachList([&](auto& list) {
		using LightType = typename std::remove_reference_t<decltype(list)>::value_type::element_type;
		list.push_back(RE::make_nismart<LightType>(destroyed));
	});
	LightLimitFix fix;
	Require(Capture(fix, nullptr) == nullptr);
	const auto* snapshot = Capture(fix, &node);
	Require(snapshot && snapshot->ActiveLights().size() == 2);
	forEachList([&](const auto& list) { Require(snapshot->Find(list.front().get()) == list.front().get()); });
	Require(snapshot->ActiveLights()[0] == node.activeLights.front().get());
	Require(snapshot->ActiveLights()[1] == node.activeShadowLights.front().get());
	auto* firstKey = node.activeLights.front().get();
	std::thread cleanup([&] {
		RE::BSSpinLockGuard lock{ node.lightQueueLock };
		forEachList([](auto& list) { list.clear(); });
	});
	cleanup.join();
	Require(destroyed == 0);
	Require(Capture(fix, &node) == snapshot);
	Require(snapshot->Find(firstKey)->IsShadowLight());
	node.activeLights.push_back(RE::make_nismart<Light>(destroyed));
	Require(snapshot->Find(node.activeLights.front().get()) == nullptr);
	fix.sceneLightSnapshots.clear();
	Require(destroyed == 5);
	Require(Capture(fix, &node)->Find(node.activeLights.front().get()) != nullptr);
}

void TestAllocationFailures()
{
	unsigned failures = 0;
	for (int failureAfter = 0; failureAfter < 64; ++failureAfter) {
		std::atomic<unsigned> destroyed{ 0 };
		RE::ShadowSceneNode node;
		node.activeLights.push_back(RE::make_nismart<Light>(destroyed));
		node.activeShadowLights.push_back(RE::make_nismart<ShadowLight>(destroyed));
		LightLimitFix fix;
		const auto before = loggedFailures;
		allocationsUntilFailure = failureAfter;
		const auto* snapshot = Capture(fix, &node);
		allocationsUntilFailure = -1;
		Require(!queueLockHeld);
		if (snapshot) {
			Require(failures > 0 && snapshot->ActiveLights().size() == 2);
			Require(loggedFailures == before);
			std::cout << failures << " injected allocation failures passed\n";
			return;
		}
		++failures;
		Require(fix.sceneLightSnapshotFailed && fix.sceneLightSnapshots.empty());
		Require(node.activeLights.front()->references == 1);
		Require(node.activeShadowLights.front()->references == 1);
		Require(loggedFailures == before + 1);
		Require(Capture(fix, &node) == nullptr);
		Require(loggedFailures == before + 1);
		fix.sceneLightSnapshots.clear();
		fix.sceneLightSnapshotFailed = false;
		Require(Capture(fix, &node)->ActiveLights().size() == 2);
	}
	Require(false);
}

void TestEnumerationOwnership()
{
	for (bool vr : { false, true }) {
		globals::game::isVR = vr;
		std::atomic<unsigned> destroyed{ 0 };
		RE::ShadowSceneNode node;
		node.activeShadowLights.push_back(RE::make_nismart<ShadowLight>(destroyed));
		LightLimitFix fix;
		const auto* snapshot = vr ? Capture(fix, &node) : nullptr;
		unsigned visits = 0;
		EnumerateSceneLights(&node, snapshot, [&](Light* light) {
			++visits;
			node.activeShadowLights.front().reset();
			Require(destroyed == 0);
			Require(light->IsShadowLight());
		});
		Require(visits == 1 && destroyed == (vr ? 0u : 1u));
		fix.sceneLightSnapshots.clear();
		Require(destroyed == 1);
	}
}

int main()
{
	TestOwnership();
	TestCapture();
	TestAllocationFailures();
	TestEnumerationOwnership();
	std::cout << "Scene light NiPointer ownership, native capture integration and allocation failures passed\n";
}
