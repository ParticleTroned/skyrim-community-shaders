#include "Features/LightLimitFix/SceneLightSnapshot.h"

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <source_location>
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

	void Require(bool a_condition, std::source_location a_location = std::source_location::current())
	{
		if (!a_condition) {
			std::cerr << "Scene light assertion failed at " << a_location.file_name() << ':' << a_location.line() << '\n';
			std::abort();
		}
	}

	struct Light
	{
		explicit Light(std::atomic<unsigned>& a_destroyed) : destroyed(a_destroyed) {}
		virtual ~Light()
		{
			Require(!queueLockHeld);
			++destroyed;
		}
		virtual bool IsShadowLight() const { return castsShadow; }
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
		bool castsShadow = true;
		std::atomic<unsigned>& destroyed;
	};

	struct ShadowLight : Light
	{
		using Light::Light;
		void Render(std::uint32_t& a_index)
		{
			Require(!queueLockHeld);
			if (onRender)
				onRender();
			Require(references > 0);
			++renderCalls;
			a_index += indexStep;
		}
		std::function<void()> onRender;
		unsigned renderCalls = 0;
		std::uint32_t indexStep = 1;
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
	using BSShadowLight = ShadowLight;

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
		std::vector<ShadowLight*> shadowLightsAccum;
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
	static void RenderVRShadowLights(RE::ShadowSceneNode* a_node, std::uint32_t& a_index);
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

void TestNativeRenderLifetime()
{
	std::atomic<unsigned> destroyed{ 0 };
	RE::ShadowSceneNode node;
	auto light = RE::make_nismart<ShadowLight>(destroyed);
	auto* key = light.get();
	light->indexStep = 2;
	node.activeShadowLights.push_back(light);
	node.shadowLightsAccum = { key, key, nullptr };
	light->onRender = [&] {
		std::thread cleanup([&] {
			RE::BSSpinLockGuard lock{ node.lightQueueLock };
			node.activeShadowLights.clear();
			node.shadowLightsAccum.clear();
		});
		cleanup.join();
		Require(destroyed == 0);
	};
	light.reset();
	std::uint32_t index = 0;
	verifyCaptureLock = true;
	LightLimitFix::RenderVRShadowLights(&node, index);
	verifyCaptureLock = false;
	Require(index == 2 && destroyed == 1 && !queueLockHeld);
	LightLimitFix::RenderVRShadowLights(nullptr, index);
}

void TestNativeRenderSelection()
{
	std::atomic<unsigned> destroyed{ 0 };
	RE::ShadowSceneNode node;
	auto first = RE::make_nismart<ShadowLight>(destroyed);
	auto second = RE::make_nismart<ShadowLight>(destroyed);
	node.lightQueueRemove.push_back(first);
	node.unk190.push_back(second);
	node.shadowLightsAccum = { first.get(), second.get(), nullptr };
	std::uint32_t index = 0;
	LightLimitFix::RenderVRShadowLights(&node, index);
	Require(index == 2 && first->renderCalls == 1 && second->renderCalls == 1);

	first->castsShadow = false;
	index = 0;
	LightLimitFix::RenderVRShadowLights(&node, index);
	Require(index == 0 && first->renderCalls == 1 && second->renderCalls == 1);
	first->castsShadow = true;

	node.shadowLightsAccum[0] = reinterpret_cast<ShadowLight*>(0x33509950);
	index = 0;
	LightLimitFix::RenderVRShadowLights(&node, index);
	Require(index == 0 && first->renderCalls == 1);
	node.shadowLightsAccum = { first.get() };
	first->indexStep = 0;
	LightLimitFix::RenderVRShadowLights(&node, index);
	Require(index == 0 && first->renderCalls == 2);
	first->indexStep = 2;
	LightLimitFix::RenderVRShadowLights(&node, index);
	Require(index == 2 && first->renderCalls == 3);
}

void TestNativeRenderAllocationFailures()
{
	unsigned failures = 0;
	for (int failureAfter = 0; failureAfter < 64; ++failureAfter) {
		std::atomic<unsigned> destroyed{ 0 };
		RE::ShadowSceneNode node;
		auto light = RE::make_nismart<ShadowLight>(destroyed);
		node.activeShadowLights.push_back(light);
		node.shadowLightsAccum = { light.get(), nullptr };
		const auto before = loggedFailures;
		std::uint32_t index = 0;
		allocationsUntilFailure = failureAfter;
		LightLimitFix::RenderVRShadowLights(&node, index);
		allocationsUntilFailure = -1;
		Require(!queueLockHeld && light->references == 2);
		if (index == 1) {
			Require(failures > 0 && light->renderCalls == 1 && loggedFailures == before);
			std::cout << failures << " native render allocation failures passed\n";
			return;
		}
		++failures;
		Require(index == 0 && light->renderCalls == 0 && loggedFailures == before + 1);
	}
	Require(false);
}

void TestNativeRenderUnwind()
{
	std::atomic<unsigned> destroyed{ 0 };
	RE::ShadowSceneNode node;
	auto light = RE::make_nismart<ShadowLight>(destroyed);
	node.activeShadowLights.push_back(light);
	node.shadowLightsAccum = { light.get(), nullptr };
	light->onRender = [] { throw std::bad_alloc{}; };
	std::uint32_t index = 0;
	bool propagated = false;
	const auto before = loggedFailures;
	try {
		LightLimitFix::RenderVRShadowLights(&node, index);
	} catch (const std::bad_alloc&) {
		propagated = true;
	}
	Require(propagated && index == 0 && !queueLockHeld);
	Require(light->references == 2 && loggedFailures == before);
}

int main()
{
	TestOwnership();
	TestCapture();
	TestAllocationFailures();
	TestEnumerationOwnership();
	TestNativeRenderLifetime();
	TestNativeRenderSelection();
	TestNativeRenderAllocationFailures();
	TestNativeRenderUnwind();
	std::cout << "Scene light NiPointer ownership, native capture integration and allocation failures passed\n";
}
