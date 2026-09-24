#include "EngineFixes/ShadowBatchSubmissions.h"
#include "EngineFixes/VRShadowBatchLayout.h"
#include "EngineFixes/VRShadowBatchPolicy.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <intrin.h>
#include <iostream>
#include <latch>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace
{
	std::atomic_size_t allocations{};
	std::atomic_ptrdiff_t failAfter{ -1 };
}

void* operator new(std::size_t size)
{
	if (failAfter.load() >= 0 && failAfter.fetch_sub(1) == 0)
		throw std::bad_alloc{};
	++allocations;
	if (auto* memory = std::malloc(size ? size : 1))
		return memory;
	throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

using DWORD = unsigned long;
using USHORT = unsigned short;
USHORT CaptureStackBackTrace(DWORD, DWORD, void**, void*) { return 0; }
namespace logger
{
	template <class... Args>
	void warn(const char*, Args&&...)
	{}
	template <class... Args>
	void error(const char*, Args&&...)
	{}
}

namespace RE
{
	struct RefCounted
	{
		std::atomic_int refs{};
		std::function<void()> onZero;
	};
	template <class T>
	struct NiPointer
	{
		T* pointer{};
		NiPointer() = default;
		explicit NiPointer(T* value) { reset(value); }
		NiPointer(const NiPointer& other) { reset(other.pointer); }
		NiPointer& operator=(const NiPointer& other)
		{
			reset(other.pointer);
			return *this;
		}
		~NiPointer() { reset(); }
		T* get() const { return pointer; }
		void reset(T* value = nullptr)
		{
			if (value)
				++value->refs;
			if (pointer && --pointer->refs == 0 && pointer->onZero)
				pointer->onZero();
			pointer = value;
		}
	};
	struct Flags
	{
		std::uint64_t value{};
		std::uint64_t underlying() const { return value; }
	};
	struct BSShaderProperty : RefCounted
	{
		Flags flags;
		void* material{};
	};
	struct NiAlphaProperty : RefCounted
	{
		std::uint16_t alphaFlags{};
	};
	struct BSGeometry : RefCounted
	{
		struct Runtime
		{
			NiPointer<BSShaderProperty> shaderProperty;
			NiPointer<NiAlphaProperty> alphaProperty;
		} runtime;
		Runtime& GetGeometryRuntimeData() { return runtime; }
	};
	struct BSShader
	{
		enum class Type
		{
			Utility,
			Lighting
		};
		struct ShaderType
		{
			Type value{ Type::Utility };
			Type get() const { return value; }
		} shaderType;
	};
	struct BSRenderPass
	{
		BSShader* shader{};
		BSShaderProperty* shaderProperty{};
		BSGeometry* geometry{};
		std::uint32_t passEnum{ 0xC0C6 };
		std::uint8_t accumulationHint{}, extraParam{};
		std::uint8_t LODMode{};
		std::uint8_t numLights{}, numShadowLights{}, unk21{};
		std::uint32_t unk24{};
		BSRenderPass* next{};
		BSRenderPass* passGroupNext{};
		void* sceneLights{};
		std::uint32_t cachePoolId{}, pad44{};
	};
	struct BSBatchRenderer
	{
		struct PassGroup
		{
			BSRenderPass* passes[5]{};
			std::uint32_t validPassBits{};
		};
		std::array<std::byte, 0x70> native{};
		std::vector<PassGroup> renderPass;
		std::unordered_map<std::uint32_t, std::uint32_t> renderPassMap;
		std::array<VRShadowBatch::NativeLayout::TechniqueEntry, 64> entries{};
		bool autoClearPasses{ true };
	};
	static_assert(offsetof(BSBatchRenderer, native) == 0);
	static_assert(sizeof(BSBatchRenderer::PassGroup) == 0x30);
}

#include "shadow_batch_hooks.h"

namespace
{
	using Renderer = RE::BSBatchRenderer;
	using Pass = RE::BSRenderPass;
	int calls{};
	std::vector<Pass*> draws;
	std::function<void(Renderer*)> duringDraw;
	void Check(bool value, std::source_location location = std::source_location::current())
	{
		if (!value)
			throw std::runtime_error("hook assertion at line " + std::to_string(location.line()));
	}
	template <class T, std::size_t N>
	void Write(std::array<std::byte, N>& bytes, std::size_t offset, T value)
	{
		Check(offset <= N && sizeof(value) <= N - offset);
		std::memcpy(bytes.data() + offset, &value, sizeof(value));
	}
	void PublishLayout(Renderer* renderer)
	{
		auto& entries = renderer->entries;
		entries.fill({});
		// Reserve identity-hash heads before placing any colliding entries.
		for (const auto& [technique, index] : renderer->renderPassMap) {
			auto& head = entries[technique & (entries.size() - 1)];
			if (!head.next)
				head = { technique, index, 1 };
		}
		for (const auto& [technique, index] : renderer->renderPassMap) {
			auto* head = &entries[technique & (entries.size() - 1)];
			if (head->technique == technique)
				continue;
			while (head->next != 1)
				head = reinterpret_cast<VRShadowBatch::NativeLayout::TechniqueEntry*>(head->next);
			auto empty = std::find_if(entries.begin(), entries.end(), [](const auto& entry) { return !entry.next; });
			Check(empty != entries.end());
			*empty = { technique, index, 1 };
			head->next = reinterpret_cast<std::uintptr_t>(&*empty);
		}
		Write(renderer->native, 0x08, reinterpret_cast<std::uintptr_t>(renderer->renderPass.data()));
		Write(renderer->native, 0x18, static_cast<std::uint32_t>(renderer->renderPass.size()));
		Write(renderer->native, 0x24, std::uint32_t{ 0x2A1 });
		Write(renderer->native, 0x2C, static_cast<std::uint32_t>(entries.size()));
		Write(renderer->native, 0x38, std::uintptr_t{ 1 });
		Write(renderer->native, 0x40, std::uintptr_t{ 1 });
		Write(renderer->native, 0x48, reinterpret_cast<std::uintptr_t>(entries.data()));
	}
	std::uint32_t Index(Pass* pass)
	{
		auto& runtime = pass->geometry->runtime;
		return VRShadowBatch::BucketIndex(runtime.shaderProperty.get()->flags.value,
			runtime.alphaProperty.get() ? runtime.alphaProperty.get()->alphaFlags : 0);
	}
	Renderer::PassGroup& NativeGroup(Renderer* renderer, std::uint32_t technique)
	{
		if (auto found = renderer->renderPassMap.find(technique); found != renderer->renderPassMap.end())
			return renderer->renderPass[found->second];
		renderer->renderPassMap.emplace(technique, static_cast<std::uint32_t>(renderer->renderPass.size()));
		renderer->renderPass.emplace_back();
		PublishLayout(renderer);
		return renderer->renderPass.back();
	}
	void NativeRegister(Renderer* renderer, Pass* pass, std::uint32_t technique)
	{
		++calls;
		auto& group = NativeGroup(renderer, technique);
		const auto bucket = Index(pass);
		pass->passGroupNext = group.passes[bucket];
		group.passes[bucket] = pass;
		group.validPassBits |= 1U << bucket;
	}
	void NativeSorted(Renderer* renderer, Pass* pass, std::uint32_t technique)
	{
		++calls;
		auto& group = NativeGroup(renderer, technique);
		auto** position = &group.passes[Index(pass)];
		while (*position && reinterpret_cast<std::uintptr_t>((*position)->shaderProperty->material) < reinterpret_cast<std::uintptr_t>(pass->shaderProperty->material))
			position = &(*position)->passGroupNext;
		pass->passGroupNext = *position;
		*position = pass;
		group.validPassBits |= 1U << Index(pass);
	}
	void NativeClear(Renderer* renderer)
	{
		for (auto& group : renderer->renderPass) group = {};
	}
	void NativeClearMap(Renderer* renderer)
	{
		renderer->renderPassMap.clear();
		renderer->renderPass.clear();
		PublishLayout(renderer);
	}
	void NativeDestroy(Renderer* renderer)
	{
		NativeClear(renderer);
		NativeClearMap(renderer);
	}
	bool NativeStep(Renderer* renderer, std::uint32_t* technique, std::uint32_t* bucket, void*, std::uint32_t)
	{
		auto index = renderer->renderPassMap.at(*technique);
		auto* pass = renderer->renderPass[index].passes[*bucket];
		std::size_t limit{};
		while (pass) {
			Check(++limit < 10000);
			draws.push_back(pass);
			if (duringDraw)
				duringDraw(renderer);
			pass = pass->passGroupNext;
		}
		if (renderer->autoClearPasses) {
			renderer->renderPass[index].passes[*bucket] = nullptr;
			renderer->renderPass[index].validPassBits &= ~(1U << *bucket);
		}
		++*bucket;
		return false;
	}
	void NativeRange(Renderer* renderer, std::uint32_t first, std::uint32_t last, std::uint32_t flags)
	{
		for (const auto& [technique, index] : renderer->renderPassMap) {
			if (technique < first || technique > last)
				continue;
			for (std::uint32_t bucket = 0; bucket < 5;) {
				auto current = technique;
				NativeStep(renderer, &current, &bucket, nullptr, flags);
			}
		}
	}
	struct Fixture
	{
		RE::BSShader shader;
		RE::BSShaderProperty property;
		RE::BSGeometry geometry;
		Pass pass;
		Fixture()
		{
			geometry.runtime.shaderProperty.reset(&property);
			pass.shader = &shader;
			pass.shaderProperty = &property;
			pass.geometry = &geometry;
		}
	};
	void NativeLayoutRegression()
	{
		using VRShadowBatch::NativeLayout::BucketEmpty;
		alignas(8) std::array<std::byte, 0x50> renderer{};
		alignas(8) std::array<std::byte, 0x80> entries{};
		alignas(8) std::array<std::byte, 0x60> groups{};
		constexpr std::uint32_t technique = 0x14046;
		const auto base = reinterpret_cast<std::uintptr_t>(entries.data());
		auto empty = [&](std::uint32_t bucket = 0) { return BucketEmpty(renderer.data(), technique, bucket); };
		Check(empty());
		Write(renderer, 0x08, reinterpret_cast<std::uintptr_t>(groups.data()));
		Write(renderer, 0x18, std::uint32_t{ 2 });
		// Poison the erroneous fixed-parent offsets with the crash's register values.
		Write(renderer, 0x24, std::uint32_t{ 0x2A1 });
		Write(renderer, 0x40, std::uintptr_t{ 1 });
		Write(renderer, 0x2C, std::uint32_t{ 8 });
		Write(renderer, 0x38, std::uintptr_t{ 1 });
		Write(renderer, 0x48, base);
		Write(entries, 0x60, technique);
		Write(entries, 0x64, std::uint32_t{ 1 });
		Write(entries, 0x68, std::uintptr_t{ 1 });
		for (std::uint32_t bucket = 0; bucket < 5; ++bucket) {
			Check(empty(bucket));
			Write(groups, 0x30 + bucket * 8, std::uintptr_t{ 1 });
			Check(!empty(bucket));
			Write(groups, 0x30 + bucket * 8, std::uintptr_t{ 0 });
		}
		Check(!empty(5));
		Write(groups, 0x30, std::uintptr_t{ 1 });
		Write(entries, 0x60, technique + 8);
		Check(empty());
		Write(entries, 0x68, base + 0x10);
		Write(entries, 0x10, technique);
		Write(entries, 0x14, std::uint32_t{ 1 });
		Write(entries, 0x18, std::uintptr_t{ 1 });
		Check(!empty());
		Write(groups, 0x30, std::uintptr_t{ 0 });
		Check(empty());
		Write(entries, 0x14, std::uint32_t{ 2 });
		Check(!empty());
		Write(entries, 0x14, std::uint32_t{ 1 });
		Write(renderer, 0x08, std::uintptr_t{ 1 });
		Check(!empty());
		Write(renderer, 0x08, reinterpret_cast<std::uintptr_t>(groups.data()));
		Write(entries, 0x10, technique + 16);
		for (const auto next : { base + 0x60, base + 1, base + entries.size(), std::uintptr_t{ 0 }, std::uintptr_t{ 2 } }) {
			Write(entries, 0x18, next);
			Check(!empty());
		}
		Write(entries, 0x18, std::uintptr_t{ 1 });
		Check(empty());
		for (const auto capacity : { 0U, 3U }) {
			Write(renderer, 0x2C, capacity);
			Check(!empty());
		}
		Write(renderer, 0x2C, std::uint32_t{ 8 });
		for (const auto address : { std::uintptr_t{ 0 }, std::uintptr_t{ 1 }, base + 1, std::numeric_limits<std::uintptr_t>::max() - 7 }) {
			Write(renderer, 0x48, address);
			Check(!empty());
		}
		Write(renderer, 0x48, base);
		Write(renderer, 0x38, base + 0x10);
		Check(!empty());
		Write(renderer, 0x38, std::uintptr_t{ 0 });
		Check(!empty());
		Check(!BucketEmpty(nullptr, technique, 0));
	}
	void NativeOwnershipRegression()
	{
		using namespace VRShadowBatch;
		Fixture fixture;
		Renderer renderer;
		const std::unique_ptr<Renderer, decltype(&Destroy)> cleanup(&renderer, Destroy);
		constexpr std::uint32_t technique = 0x14046;
		RegisterUnsorted(&renderer, &fixture.pass, technique);
		const auto key = BucketKey(technique, 0);
		Check(!BucketEmpty(&renderer, key));
		// The production lookup must not consult the harness's logical native map.
		renderer.renderPassMap.clear();
		Check(!BucketEmpty(&renderer, key));
		auto& entry = renderer.entries[technique & 63];
		entry.group = 1;
		Prune(&renderer, *FindState(&renderer));
		Check(FindState(&renderer)->submissions.Active() == 1 && fixture.geometry.refs == 1);
		entry.group = 0;
		NativeClear(&renderer);
		Check(BucketEmpty(&renderer, key));
		Clear<clearMap>(&renderer);
		Check(fixture.geometry.refs == 0);
	}
	void Exercise(bool sorted)
	{
		using namespace VRShadowBatch;
		Fixture a, b;
		Renderer first, second;
		a.pass.passGroupNext = &b.pass;
		b.pass.passGroupNext = &a.pass;
		a.property.material = reinterpret_cast<void*>(1);
		b.property.material = reinterpret_cast<void*>(2);
		const auto submit = sorted ? RegisterSorted : RegisterUnsorted;
		calls = 0;
		submit(&first, &a.pass, 0xC0C6);
		submit(&first, &b.pass, 0xC0C6);
		submit(&first, &a.pass, 0xC0C6);
		Check(calls == 2 && a.geometry.refs == 1 && a.property.refs == 2);
		Check(a.pass.passGroupNext == &b.pass && b.pass.passGroupNext == &a.pass);
		submit(&second, &a.pass, 0xC0C6);
		Check(calls == 3 && a.geometry.refs == 2);
		Check(first.renderPass[0].passes[0] != second.renderPass[0].passes[0]);
		first.autoClearPasses = false;
		draws.clear();
		RenderActiveRange(&first, 0xC0C6, 0xC0C6, 0);
		Check(draws.size() == 2 && draws[0] != &a.pass && draws[1] != &b.pass);
		Check(draws[0]->geometry == (sorted ? &a.geometry : &b.geometry));
		submit(&first, &a.pass, 0xC0C6);
		Check(calls == 3);
		first.autoClearPasses = true;
		RenderActiveRange(&first, 0xC0C6, 0xC0C6, 0);
		Check(a.geometry.refs == 1 && b.geometry.refs == 0);
		submit(&first, &a.pass, 0xC0C6);
		Check(calls == 4 && a.geometry.refs == 2);
		a.pass.extraParam = 1;
		submit(&first, &a.pass, 0xC0C6);
		Check(calls == 5);
		a.property.flags.value = 1ULL << 36;
		submit(&first, &a.pass, 0xC0C6);
		Check(calls == 6 && first.renderPass[0].passes[2]);
		std::uint32_t technique = 0xC0C6, bucket = 0;
		RenderBatch(&first, &technique, &bucket, nullptr, 0);
		Check(bucket == 1 && FindState(&first)->submissions.Active() == 1);
		Clear<clearMap>(&first);
		Check(a.geometry.refs == 1);
		Destroy(&first);
		Destroy(&second);
		Check(registry.empty() && a.geometry.refs == 0 && a.property.refs == 1);
		// Reusing the renderer address is a new lifetime, not an old membership.
		submit(&first, &a.pass, 0xC0C6);
		Check(FindState(&first)->submissions.Active() == 1);
		Destroy(&first);
	}
	void ReentrantAndNativePaths()
	{
		using namespace VRShadowBatch;
		Fixture a;
		Renderer renderer;
		RegisterUnsorted(&renderer, &a.pass, 0xC0C6);
		auto* saved = renderer.renderPass[0].passes[0];
		duringDraw = [&](Renderer* active) {
			Clear<clearPasses>(active);
			Check(a.geometry.refs == 1 && saved->geometry == &a.geometry);
		};
		RenderActiveRange(&renderer, 0xC0C6, 0xC0C6, 0);
		duringDraw = {};
		Check(a.geometry.refs == 0);
		Destroy(&renderer);
		a.pass.numLights = 1;
		RegisterUnsorted(&renderer, &a.pass, 0xC0C6);
		Check(!FindState(&renderer) && renderer.renderPass[0].passes[0] == &a.pass);
		NativeClear(&renderer);
		a.pass.numLights = 0;
		a.shader.shaderType.value = RE::BSShader::Type::Lighting;
		RegisterUnsorted(&renderer, &a.pass, 0x4800002D);
		Check(!FindState(&renderer));
		NativeClear(&renderer);
	}

	void AllocationFailures()
	{
		using namespace VRShadowBatch;
		std::size_t rejected{};
		for (std::ptrdiff_t point = 0; point < 16; ++point) {
			Fixture fixture;
			Renderer renderer;
			NativeGroup(&renderer, 0xC0C6);
			calls = 0;
			failAfter = point;
			RegisterUnsorted(&renderer, &fixture.pass, 0xC0C6);
			failAfter = -1;
			Check(calls == 0 || calls == 1);
			if (!calls) {
				++rejected;
				Check(fixture.geometry.refs == 0 && fixture.property.refs == 1);
				Check(renderer.renderPass[0].passes[0] == nullptr);
				RegisterUnsorted(&renderer, &fixture.pass, 0xC0C6);
				Check(calls == 1);
			}
			Destroy(&renderer);
			Check(fixture.geometry.refs == 0 && registry.empty());
		}
		Check(rejected >= 5);
		std::cout << "allocation failures safely rejected and retried: " << rejected << '\n';
	}

	void PopulatedAllocationFailures(bool separateBuckets = false)
	{
		using namespace VRShadowBatch;
		std::size_t rejected{};
		const auto technique = [separateBuckets](std::size_t index) {
			return 0xC0C6U + (separateBuckets ? static_cast<std::uint32_t>(index) << 16 : 0U);
		};
		for (std::size_t populated = 1; populated <= (separateBuckets ? 32U : 64U); ++populated) {
			for (std::ptrdiff_t point = 0; point < (separateBuckets ? 8 : 4); ++point) {
				std::vector<Fixture> fixtures(populated + 1);
				Renderer renderer;
				const std::unique_ptr<Renderer, decltype(&Destroy)> cleanup(&renderer, Destroy);
				NativeGroup(&renderer, technique(populated));
				calls = 0;
				for (std::size_t index = 0; index < populated; ++index)
					RegisterUnsorted(&renderer, &fixtures[index].pass, technique(index));
				const auto baselineCalls = calls;
				auto state = FindState(&renderer);
				auto* head = renderer.renderPass[0].passes[0];
				Check(static_cast<std::size_t>(baselineCalls) == populated && state && state->submissions.Active() == populated);
				// Vary occupancy so failures exercise both record and populated membership growth.
				failAfter = point;
				RegisterUnsorted(&renderer, &fixtures.back().pass, technique(populated));
				failAfter = -1;
				Check(calls == baselineCalls || calls == baselineCalls + 1);
				const auto accepted = static_cast<std::size_t>(calls);
				Check(state->submissions.Active() == accepted);
				for (std::size_t index = 0; index < fixtures.size(); ++index) {
					const auto retained = index < accepted ? 1 : 0;
					Check(fixtures[index].geometry.refs == retained && fixtures[index].property.refs == retained + 1);
				}
				if (accepted == populated) {
					++rejected;
					Check(renderer.renderPass[0].passes[0] == head);
					RegisterUnsorted(&renderer, &fixtures.front().pass, technique(0));
					RegisterUnsorted(&renderer, &fixtures[populated - 1].pass, technique(populated - 1));
					Check(calls == baselineCalls && state->submissions.Active() == populated);
					RegisterUnsorted(&renderer, &fixtures.back().pass, technique(populated));
				}
				Check(calls == baselineCalls + 1 && state->submissions.Active() == populated + 1);
				for (auto& fixture : fixtures)
					Check(fixture.geometry.refs == 1 && fixture.property.refs == 2);
				draws.clear();
				RenderActiveRange(&renderer, technique(0), technique(populated), 0);
				Check(draws.size() == populated + 1 && state->submissions.Active() == 0);
				for (auto& fixture : fixtures)
					Check(fixture.geometry.refs == 0 && fixture.property.refs == 1);
			}
		}
		Check(rejected > 1 && registry.empty());
		std::cout << "populated " << (separateBuckets ? "bucket" : "entry") << " allocation failures safely rejected and retried: " << rejected << '\n';
	}

	void RegistryCacheLifetimes()
	{
		using namespace VRShadowBatch;
		Fixture first, replacement;
		Renderer renderer;
		RegisterUnsorted(&renderer, &first.pass, 0xC0C6);
		auto oldState = FindState(&renderer);
		std::shared_ptr<BatchState> newState;
		std::latch cached{ 1 }, destroyed{ 1 }, absenceChecked{ 1 }, recreated{ 1 }, reuseChecked{ 1 }, finish{ 1 };
		bool sawOld{}, sawMissing{}, sawReplacement{};
		std::thread worker([&] {
			sawOld = FindState(&renderer) == oldState;
			cached.count_down();
			destroyed.wait();
			sawMissing = !FindState(&renderer);
			absenceChecked.count_down();
			recreated.wait();
			sawReplacement = newState && FindState(&renderer) == newState && newState != oldState;
			reuseChecked.count_down();
			finish.wait();
		});
		cached.wait();
		Destroy(&renderer);
		destroyed.count_down();
		absenceChecked.wait();
		RegisterUnsorted(&renderer, &replacement.pass, 0xC0C6);
		newState = FindState(&renderer);
		std::weak_ptr<BatchState> releasedState = newState;
		recreated.count_down();
		reuseChecked.wait();
		Destroy(&renderer);
		newState.reset();
		// A dormant thread's positive cache must not keep the destroyed renderer's pool alive.
		const bool releasedWhileCached = releasedState.expired();
		finish.count_down();
		worker.join();
		Check(sawOld && sawMissing && sawReplacement && releasedWhileCached);
		Check(oldState->submissions.Active() == 0 && registry.empty());
		Check(first.geometry.refs == 0 && first.property.refs == 1);
		Check(replacement.geometry.refs == 0 && replacement.property.refs == 1);
	}

	void NegativeCacheCreationAndUnwind()
	{
		using namespace VRShadowBatch;
		Renderer renderer;
		Fixture fixture;
		Check(!FindState(&renderer));
		Check(!FindState(&renderer));
		std::thread producer([&] { RegisterUnsorted(&renderer, &fixture.pass, 0xC0C6); });
		producer.join();
		Check(FindState(&renderer) && fixture.geometry.refs == 1);
		duringDraw = [&](Renderer* current) {
			Clear<clearPasses>(current);
			Check(fixture.geometry.refs == 1);
			throw std::runtime_error("native unwind");
		};
		bool propagated{};
		try {
			RenderActiveRange(&renderer, 0xC0C6, 0xC0C6, 0);
		} catch (const std::runtime_error&) {
			propagated = true;
		}
		duringDraw = {};
		Check(propagated && fixture.geometry.refs == 0 && FindState(&renderer)->submissions.Active() == 0);
		Destroy(&renderer);
		Check(!FindState(&renderer) && registry.empty());
	}

	void UnlockedNativeAndOwnerCallbacks()
	{
		using namespace VRShadowBatch;
		Fixture fixture;
		Renderer renderer;
		RegisterUnsorted(&renderer, &fixture.pass, 0xC0C6);
		auto state = FindState(&renderer);
		bool ownerCalled{};
		fixture.geometry.onZero = [&] {
			ownerCalled = true;
			bool acquired{};
			std::thread worker([&] {
				const auto registered = FindState(&renderer);
				acquired = registered == state && state->mutex.try_lock();
				if (acquired)
					state->mutex.unlock();
			});
			worker.join();
			Check(acquired);
		};
		duringDraw = [&](Renderer* active) {
			bool acquired{};
			std::thread worker([&] {
				acquired = state->mutex.try_lock();
				if (acquired) {
					state->mutex.unlock();
					Clear<clearPasses>(active);
				}
			});
			worker.join();
			Check(acquired && fixture.geometry.refs == 1 && !ownerCalled);
		};
		RenderActiveRange(&renderer, 0xC0C6, 0xC0C6, 0);
		duringDraw = {};
		Check(ownerCalled && fixture.geometry.refs == 0);
		Destroy(&renderer);
	}

	void InterleavedRendererCaches()
	{
		using namespace VRShadowBatch;
		std::array<Renderer, 32> renderers;
		std::array<std::shared_ptr<BatchState>, 32> states;
		Fixture fixture;
		for (std::size_t i = 0; i < renderers.size(); ++i) {
			if (i % 2)
				RegisterUnsorted(&renderers[i], &fixture.pass, 0xC0C6);
			states[i] = FindState(&renderers[i]);
		}
		for (unsigned round = 0; round < 64; ++round) {
			for (std::size_t i = 0; i < renderers.size(); ++i)
				Check(FindState(&renderers[i]) == states[i]);
		}
		std::thread replace([&] {
			for (auto& renderer : renderers) {
				Destroy(&renderer);
				RegisterUnsorted(&renderer, &fixture.pass, 0xC0C6);
			}
		});
		replace.join();
		for (std::size_t i = 0; i < renderers.size(); ++i) {
			const auto replacement = FindState(&renderers[i]);
			Check(replacement && replacement != states[i] && replacement->submissions.Active() == 1);
			Check(!states[i] || states[i]->submissions.Active() == 0);
			Destroy(&renderers[i]);
			Check(!FindState(&renderers[i]));
		}
		Check(registry.empty() && fixture.geometry.refs == 0);
	}

	void Benchmark()
	{
		using namespace VRShadowBatch;
		std::cout << "passes,native_us,protected_us,delta_us,protected_warm_allocations\n";
		for (const std::size_t count : { 64, 512, 4096 }) {
			std::vector<Fixture> fixtures(count);
			Renderer renderer;
			auto run = [&](bool protectedPath) {
				const auto begin = std::chrono::steady_clock::now();
				for (int repeat = 0; repeat < 30; ++repeat) {
					for (auto& fixture : fixtures) {
						if (protectedPath)
							RegisterUnsorted(&renderer, &fixture.pass, 0xC0C6);
						else
							NativeRegister(&renderer, &fixture.pass, 0xC0C6);
					}
					draws.clear();
					if (protectedPath)
						RenderActiveRange(&renderer, 0xC0C6, 0xC0C6, 0);
					else
						NativeRange(&renderer, 0xC0C6, 0xC0C6, 0);
				}
				return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count() / 30;
			};
			run(false);
			run(true);
			const auto before = allocations.load();
			std::array<double, 9> native{}, candidate{};
			for (std::size_t round = 0; round < native.size(); ++round) {
				if (round % 2) {
					candidate[round] = run(true);
					native[round] = run(false);
				} else {
					native[round] = run(false);
					candidate[round] = run(true);
				}
			}
			const auto allocated = allocations.load() - before;
			Check(allocated == 0);
			std::sort(native.begin(), native.end());
			std::sort(candidate.begin(), candidate.end());
			std::cout << count << ',' << native[4] << ',' << candidate[4] << ',' << candidate[4] - native[4] << ',' << allocated << '\n';
			Destroy(&renderer);
		}
	}
}

int main(int argc, char** argv)
{
	try {
		using namespace VRShadowBatch;
		registerUnsorted = NativeRegister;
		registerSorted = NativeSorted;
		clearPasses = NativeClear;
		clearMap = NativeClearMap;
		destroy = NativeDestroy;
		renderRange = NativeRange;
		renderStep = NativeStep;
		NativeLayoutRegression();
		NativeOwnershipRegression();
		Exercise(false);
		Exercise(true);
		ReentrantAndNativePaths();
		AllocationFailures();
		PopulatedAllocationFailures();
		PopulatedAllocationFailures(true);
		RegistryCacheLifetimes();
		NegativeCacheCreationAndUnwind();
		UnlockedNativeAndOwnerCallbacks();
		InterleavedRendererCaches();
		if (argc == 2 && std::string_view(argv[1]) == "--benchmark")
			Benchmark();
		std::cout << "production shadow registration/drain/reset/destruction hook tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
