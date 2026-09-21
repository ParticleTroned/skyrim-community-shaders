#include "Features/LightLimitFix/SceneLightSnapshot.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
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
	thread_local std::uint64_t allocationCalls = 0;
	thread_local bool queueLockHeld = false;
	const auto renderThreadID = std::this_thread::get_id();
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
			// Engine workers may destroy withdrawn lights under the lock; render leases must not.
			Require(!queueLockHeld || std::this_thread::get_id() != renderThreadID);
			++destroyed;
		}
		virtual bool IsShadowLight() const
		{
			Require(!queueLockHeld);
			return castsShadow;
		}
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
			Require(references > 0 || sceneOwned);
			++renderCalls;
			a_index += indexStep;
		}
		std::function<void()> onRender;
		unsigned renderCalls = 0;
		std::uint32_t indexStep = 1;
		bool sceneOwned = false;
	};

	using Snapshot = LightLimitFixDetail::SceneLightSnapshot<RE::NiPointer<Light>>;
}

void* operator new(std::size_t a_size)
{
	++allocationCalls;
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
		void IncRefCount()
		{
			Require(!verifyCaptureLock || queueLockHeld);
			++references;
		}
		void DecRefCount()
		{
			if (--references == 0) {
				Require(deleteOnZero && !queueLockHeld);
				delete this;
			}
		}
		std::atomic<unsigned> references{ 1 };
		bool deleteOnZero = false;
		std::unique_ptr<ShadowLight> sunOwner;
		ShadowLight* sunShadowDirLight = nullptr;
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
	static void RenderVRShadowLightsSnapshotBaseline(RE::ShadowSceneNode* a_node, std::uint32_t& a_index);
};

namespace globals::game
{
	bool isVR = false;
}

#define CS_PROFILE_CPU_SCOPE(...) static_cast<void>(0)
#include "fixtures/vr_shadow_dispatch_snapshot.h"
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

void TestNativeWorkInvalidation()
{
	for (unsigned change = 0; change < 5; ++change) {
		std::atomic<unsigned> destroyed{ 0 };
		RE::ShadowSceneNode node;
		auto first = RE::make_nismart<ShadowLight>(destroyed);
		auto withdrawn = RE::make_nismart<ShadowLight>(destroyed);
		auto replacement = RE::make_nismart<ShadowLight>(destroyed);
		node.activeShadowLights = { first, withdrawn, replacement };
		node.shadowLightsAccum = { first.get(), withdrawn.get(), nullptr };
		first->onRender = [&] {
			std::thread cleanup([&] {
				RE::BSSpinLockGuard lock{ node.lightQueueLock };
				switch (change) {
				case 0:
					node.shadowLightsAccum.clear();
					break;
				case 1:
					node.shadowLightsAccum[1] = nullptr;
					break;
				case 2:
					node.shadowLightsAccum.resize(1);
					break;
				case 3:
					node.shadowLightsAccum[1] = replacement.get();
					break;
				case 4:
					node.activeShadowLights.erase(node.activeShadowLights.begin() + 1);
					break;
				}
			});
			cleanup.join();
		};
		std::uint32_t index = 0;
		LightLimitFix::RenderVRShadowLights(&node, index);
		Require(first->renderCalls == 1 && withdrawn->renderCalls == 0);
		Require(index == (change == 3 ? 2u : 1u));
		Require(replacement->renderCalls == (change == 3 ? 1u : 0u));
		Require(!queueLockHeld && node.references == 1);
	}
	std::cout << "Live dispatch respects cleared, terminated, shrunk, replaced and unowned work\n";
}

void TestNativeRenderLifetime()
{
	std::atomic<unsigned> destroyed{ 0 };
	RE::ShadowSceneNode node;
	auto light = RE::make_nismart<ShadowLight>(destroyed);
	auto nextLight = RE::make_nismart<ShadowLight>(destroyed);
	auto* key = light.get();
	light->indexStep = 2;
	node.activeShadowLights.push_back(light);
	node.activeShadowLights.push_back(nextLight);
	node.shadowLightsAccum = { key, key, nextLight.get(), nullptr };
	light->onRender = [&] {
		std::thread cleanup([&] {
			RE::BSSpinLockGuard lock{ node.lightQueueLock };
			node.activeShadowLights.clear();
			node.shadowLightsAccum.clear();
		});
		cleanup.join();
		Require(destroyed == 1 && key->references == 1);
	};
	unsigned nextCalls = 0;
	nextLight->onRender = [&] {
		Require(destroyed == 0);
		++nextCalls;
	};
	light.reset();
	nextLight.reset();
	std::uint32_t index = 0;
	verifyCaptureLock = true;
	LightLimitFix::RenderVRShadowLights(&node, index);
	verifyCaptureLock = false;
	Require(index == 2 && nextCalls == 0 && destroyed == 2 && !queueLockHeld);
	LightLimitFix::RenderVRShadowLights(nullptr, index);
}

void TestEmptyNativeRender()
{
	std::atomic<unsigned> destroyed{ 0 };
	RE::ShadowSceneNode node;
	auto light = RE::make_nismart<ShadowLight>(destroyed);
	node.activeShadowLights.push_back(light);
	const auto before = loggedFailures;
	auto checkEmpty = [&](std::uint32_t a_index) {
		const auto initialIndex = a_index;
		allocationsUntilFailure = 0;
		LightLimitFix::RenderVRShadowLights(&node, a_index);
		Require(allocationsUntilFailure == 0);
		allocationsUntilFailure = -1;
		Require(a_index == initialIndex && !queueLockHeld && loggedFailures == before);
		Require(light->references == 2 && light->renderCalls == 0 && node.references == 1);
	};
	checkEmpty(0);
	node.shadowLightsAccum = { nullptr, light.get() };
	checkEmpty(0);
	checkEmpty(2);
	checkEmpty(UINT32_MAX);
	std::cout << "Empty native passes allocate nothing\n";
}

void TestCompletedLightRelease()
{
	std::atomic<unsigned> destroyed{ 0 };
	RE::ShadowSceneNode node;
	auto first = RE::make_nismart<ShadowLight>(destroyed);
	auto second = RE::make_nismart<ShadowLight>(destroyed);
	node.activeShadowLights = { first, second };
	node.shadowLightsAccum = { first.get(), second.get(), nullptr };
	first->onRender = [&] {
		std::thread cleanup([&] {
			RE::BSSpinLockGuard lock{ node.lightQueueLock };
			node.activeShadowLights.erase(node.activeShadowLights.begin());
		});
		cleanup.join();
		Require(destroyed == 0);
	};
	unsigned secondCalls = 0;
	second->onRender = [&] {
		Require(destroyed == 1);
		++secondCalls;
	};
	first.reset();
	second.reset();
	std::uint32_t index = 0;
	LightLimitFix::RenderVRShadowLights(&node, index);
	Require(index == 2 && secondCalls == 1 && destroyed == 1 && node.references == 1);
}

void TestSceneOwnedSunRender(bool a_withdraw)
{
	std::atomic<unsigned> destroyed{ 0 };
	RE::ShadowSceneNode node;
	node.sunOwner = std::make_unique<ShadowLight>(destroyed);
	node.sunShadowDirLight = node.sunOwner.get();
	auto* sun = node.sunShadowDirLight;
	sun->sceneOwned = true;
	sun->indexStep = 3;
	auto first = RE::make_nismart<ShadowLight>(destroyed);
	auto last = RE::make_nismart<ShadowLight>(destroyed);
	node.activeShadowLights = { first, last };
	node.shadowLightsAccum = { first.get(), sun, sun, sun, last.get(), nullptr };
	unsigned sunCalls = 0;
	unsigned lastCalls = 0;
	sun->onRender = [&] {
		Require(sun->references == 0 && node.references == 2);
		++sunCalls;
		if (!a_withdraw)
			return;
		std::thread cleanup([&] {
			RE::BSSpinLockGuard lock{ node.lightQueueLock };
			node.activeShadowLights.clear();
			node.shadowLightsAccum.clear();
		});
		cleanup.join();
		Require(destroyed == 2);
	};
	last->onRender = [&] { ++lastCalls; };
	first.reset();
	last.reset();
	std::uint32_t index = 0;
	verifyCaptureLock = true;
	LightLimitFix::RenderVRShadowLights(&node, index);
	verifyCaptureLock = false;
	Require(index == (a_withdraw ? 4u : 5u) && sunCalls == 1 && lastCalls == (a_withdraw ? 0u : 1u));
	Require(destroyed == (a_withdraw ? 2u : 0u) && sun->references == 0 && node.references == 1);

	// Another unretained light cannot acquire the scene-owned sun exemption.
	node.shadowLightsAccum = { reinterpret_cast<ShadowLight*>(0x33509950), sun };
	index = 0;
	LightLimitFix::RenderVRShadowLights(&node, index);
	Require(index == 0 && sunCalls == 1 && node.references == 1);
}

void TestWithdrawnSun()
{
	std::atomic<unsigned> destroyed{ 0 };
	RE::ShadowSceneNode node;
	node.sunOwner = std::make_unique<ShadowLight>(destroyed);
	node.sunShadowDirLight = node.sunOwner.get();
	auto* sun = node.sunShadowDirLight;
	sun->sceneOwned = true;
	auto first = RE::make_nismart<ShadowLight>(destroyed);
	node.activeShadowLights = { first };
	node.shadowLightsAccum = { first.get(), sun, nullptr };
	first->onRender = [&] {
		RE::BSSpinLockGuard lock{ node.lightQueueLock };
		node.shadowLightsAccum[1] = nullptr;
	};
	std::uint32_t index = 0;
	LightLimitFix::RenderVRShadowLights(&node, index);
	Require(index == 1 && sun->renderCalls == 0 && sun->references == 0 && node.references == 1);
}

void TestSceneOwnerLifetime()
{
	for (const bool throws : { false, true }) {
		std::atomic<unsigned> destroyed{ 0 };
		auto* node = new RE::ShadowSceneNode;
		node->references = 0;
		node->deleteOnZero = true;
		RE::NiPointer<RE::ShadowSceneNode> owner{ node };
		node->sunOwner = std::make_unique<ShadowLight>(destroyed);
		node->sunShadowDirLight = node->sunOwner.get();
		auto* sun = node->sunShadowDirLight;
		sun->sceneOwned = true;
		node->shadowLightsAccum = { sun, nullptr };
		sun->onRender = [&] {
			Require(node->references == 2 && sun->references == 0);
			owner.reset();
			Require(destroyed == 0 && node->references == 1);
			if (throws)
				throw std::bad_alloc{};
		};
		std::uint32_t index = 0;
		bool propagated = false;
		const auto before = loggedFailures;
		try {
			LightLimitFix::RenderVRShadowLights(node, index);
		} catch (const std::bad_alloc&) {
			propagated = true;
		}
		Require(propagated == throws && index == (throws ? 0u : 1u));
		Require(!owner && destroyed == 1 && !queueLockHeld && loggedFailures == before);
	}
}

void TestSceneOwnedSunWithoutAllocation()
{
	std::atomic<unsigned> destroyed{ 0 };
	RE::ShadowSceneNode node;
	node.sunOwner = std::make_unique<ShadowLight>(destroyed);
	node.sunShadowDirLight = node.sunOwner.get();
	auto* sun = node.sunShadowDirLight;
	sun->sceneOwned = true;
	node.shadowLightsAccum = { sun, nullptr };
	const auto before = loggedFailures;
	std::uint32_t index = 0;
	allocationsUntilFailure = 0;
	LightLimitFix::RenderVRShadowLights(&node, index);
	Require(allocationsUntilFailure == 0);
	allocationsUntilFailure = -1;
	Require(loggedFailures == before && index == 1 && sun->renderCalls == 1);
	Require(node.references == 1 && sun->references == 0 && destroyed == 0 && !queueLockHeld);
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
	node.shadowLightsAccum = { first.get(), second.get() };
	second->indexStep = UINT32_MAX;
	index = 1;
	LightLimitFix::RenderVRShadowLights(&node, index);
	Require(index == 0 && first->renderCalls == 3 && second->renderCalls == 2);
}

void TestNativeOwnerSources()
{
	for (unsigned source = 0; source < 5; ++source) {
		std::atomic<unsigned> destroyed{ 0 };
		RE::ShadowSceneNode node;
		auto light = RE::make_nismart<ShadowLight>(destroyed);
		std::tuple lists{ &node.activeLights, &node.activeShadowLights, &node.lightQueueAdd, &node.lightQueueRemove, &node.unk190 };
		unsigned currentList = 0;
		std::apply([&](auto*... a_list) {
			(([&] {
				if (currentList++ == source)
					a_list->push_back(light);
			}()),
				...);
		},
			lists);
		node.shadowLightsAccum = { light.get(), nullptr };
		light->onRender = [&] {
			Require(light->references == 3 && node.references == 2);
			allocationsUntilFailure = 0;
		};
		const auto before = loggedFailures;
		std::uint32_t index = 0;
		verifyCaptureLock = true;
		LightLimitFix::RenderVRShadowLights(&node, index);
		Require(allocationsUntilFailure == 0);
		allocationsUntilFailure = -1;
		verifyCaptureLock = false;
		Require(!queueLockHeld && light->references == 2 && node.references == 1);
		Require(index == 1 && light->renderCalls == 1 && loggedFailures == before);
	}
	std::cout << "All five owner sources pass; dispatch allocates nothing after index capture\n";
}

void TestOwnerIndexMutation()
{
	for (unsigned change = 0; change < 5; ++change) {
		std::atomic<unsigned> destroyed{ 0 };
		RE::ShadowSceneNode node;
		auto first = RE::make_nismart<ShadowLight>(destroyed);
		auto second = RE::make_nismart<ShadowLight>(destroyed);
		auto added = RE::make_nismart<ShadowLight>(destroyed);
		node.activeShadowLights = { first, second };
		node.shadowLightsAccum = { first.get(), second.get(), nullptr };
		if (change == 4)
			node.lightQueueAdd.push_back(second);
		first->onRender = [&] {
			RE::BSSpinLockGuard lock{ node.lightQueueLock };
			switch (change) {
			case 0:
				node.activeShadowLights.erase(node.activeShadowLights.begin());
				break;
			case 1:
				node.activeShadowLights.reserve(node.activeShadowLights.capacity() + 16);
				break;
			case 2:
				node.lightQueueRemove.push_back(second);
				node.activeShadowLights.pop_back();
				break;
			case 3:
				node.activeShadowLights.push_back(added);
				node.shadowLightsAccum[1] = added.get();
				break;
			case 4:
				node.activeShadowLights.pop_back();
				break;
			}
		};
		std::uint32_t index = 0;
		verifyCaptureLock = true;
		LightLimitFix::RenderVRShadowLights(&node, index);
		verifyCaptureLock = false;
		Require(index == 2 && first->renderCalls == 1 && node.references == 1);
		Require(second->renderCalls == (change == 3 ? 0u : 1u));
		Require(added->renderCalls == (change == 3 ? 1u : 0u));
	}
	std::cout << "Non-owning hints survive shifts, reallocations, queue moves, new keys and duplicate owners\n";
}

void TestOwnerIndexAllocationFailures()
{
	unsigned failures = 0;
	for (int failureAfter = 0; failureAfter < 64; ++failureAfter) {
		std::atomic<unsigned> destroyed{ 0 };
		RE::ShadowSceneNode node;
		auto light = RE::make_nismart<ShadowLight>(destroyed);
		node.activeShadowLights.push_back(light);
		node.activeLights.push_back(RE::make_nismart<Light>(destroyed));
		node.lightQueueAdd.push_back(RE::make_nismart<Light>(destroyed));
		node.shadowLightsAccum = { light.get(), nullptr };
		const auto before = loggedFailures;
		std::uint32_t index = 0;
		allocationsUntilFailure = failureAfter;
		LightLimitFix::RenderVRShadowLights(&node, index);
		allocationsUntilFailure = -1;
		Require(!queueLockHeld && light->references == 2 && node.references == 1 && destroyed == 0);
		if (index == 1) {
			Require(failures > 0 && light->renderCalls == 1 && loggedFailures == before);
			std::cout << failures << " owner index allocation failures passed\n";
			return;
		}
		++failures;
		Require(index == 0 && light->renderCalls == 0 && loggedFailures == before + 1);
		LightLimitFix::RenderVRShadowLights(&node, index);
		Require(index == 1 && light->renderCalls == 1 && loggedFailures == before + 1);
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
	light->onRender = [&] {
		{
			RE::BSSpinLockGuard lock{ node.lightQueueLock };
			node.activeShadowLights.clear();
		}
		Require(destroyed == 0);
		throw std::bad_alloc{};
	};
	light.reset();
	std::uint32_t index = 0;
	bool propagated = false;
	const auto before = loggedFailures;
	try {
		LightLimitFix::RenderVRShadowLights(&node, index);
	} catch (const std::bad_alloc&) {
		propagated = true;
	}
	Require(propagated && index == 0 && !queueLockHeld);
	Require(destroyed == 1 && loggedFailures == before && node.references == 1);
}

void BenchmarkNativeDispatch()
{
	struct Case
	{
		unsigned ordinary;
		unsigned shadow;
		bool sun;
	};
	constexpr std::array cases{
		Case{ 0, 0, false }, Case{ 2048, 0, true }, Case{ 128, 4, false },
		Case{ 2048, 8, false }, Case{ 2048, 64, false }, Case{ 0, 128, false },
		Case{ 0, 1024, false }, Case{ 2048, 1024, false }
	};
	constexpr unsigned repetitions = 100;
	constexpr unsigned rounds = 9;
	std::cout << "ordinary,shadow,sun,baseline_us,candidate_us,delta_us,baseline_allocs,candidate_allocs,baseline_min_us,baseline_max_us,candidate_min_us,candidate_max_us\n";
	for (const auto& input : cases) {
		std::atomic<unsigned> destroyed{ 0 };
		RE::ShadowSceneNode node;
		for (unsigned i = 0; i < input.ordinary; ++i)
			node.activeLights.push_back(RE::make_nismart<Light>(destroyed));
		for (unsigned i = 0; i < input.shadow; ++i) {
			node.activeShadowLights.push_back(RE::make_nismart<ShadowLight>(destroyed));
			node.shadowLightsAccum.push_back(node.activeShadowLights.back().get());
		}
		if (input.sun) {
			node.sunOwner = std::make_unique<ShadowLight>(destroyed);
			node.sunShadowDirLight = node.sunOwner.get();
			node.sunShadowDirLight->sceneOwned = true;
			node.shadowLightsAccum.push_back(node.sunShadowDirLight);
		}
		node.shadowLightsAccum.push_back(nullptr);
		const std::array renderers{ &LightLimitFix::RenderVRShadowLightsSnapshotBaseline, &LightLimitFix::RenderVRShadowLights };
		std::array<std::array<double, rounds>, 2> timings{};
		std::array<std::uint64_t, 2> allocations{};
		for (unsigned round = 0; round < rounds; ++round) {
			for (unsigned turn = 0; turn < renderers.size(); ++turn) {
				const auto variant = (round + turn) % renderers.size();
				const auto before = allocationCalls;
				const auto start = std::chrono::steady_clock::now();
				for (unsigned iteration = 0; iteration < repetitions; ++iteration) {
					std::uint32_t index = 0;
					renderers[variant](&node, index);
					Require(index == input.shadow + (input.sun ? 1u : 0u));
				}
				timings[variant][round] = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / repetitions;
				allocations[variant] = (allocationCalls - before) / repetitions;
			}
		}
		for (auto& samples : timings)
			std::ranges::sort(samples);
		const auto baseline = timings[0][rounds / 2];
		const auto candidate = timings[1][rounds / 2];
		std::cout << input.ordinary << ',' << input.shadow << ',' << input.sun << ',' << std::fixed << std::setprecision(3)
				  << baseline << ',' << candidate << ',' << candidate - baseline << ',' << allocations[0] << ',' << allocations[1]
				  << ',' << timings[0].front() << ',' << timings[0].back() << ',' << timings[1].front() << ',' << timings[1].back() << '\n';
	}
}

int main(int a_argc, char** a_argv)
{
	if (a_argc == 2 && std::string_view(a_argv[1]) == "--benchmark") {
		BenchmarkNativeDispatch();
		return 0;
	}
	TestOwnership();
	TestCapture();
	TestAllocationFailures();
	TestEnumerationOwnership();
	TestNativeWorkInvalidation();
	TestNativeRenderLifetime();
	TestEmptyNativeRender();
	TestCompletedLightRelease();
	TestSceneOwnedSunRender(false);
	TestSceneOwnedSunRender(true);
	TestWithdrawnSun();
	TestSceneOwnerLifetime();
	TestSceneOwnedSunWithoutAllocation();
	TestNativeRenderSelection();
	TestNativeOwnerSources();
	TestOwnerIndexMutation();
	TestOwnerIndexAllocationFailures();
	TestNativeRenderUnwind();
	std::cout << "Scene light ownership, live native dispatch, sun ownership and snapshot allocation failures passed\n";
}
