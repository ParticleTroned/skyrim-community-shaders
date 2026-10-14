#include "Utils/ResidentShaderCache.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	std::atomic_size_t allocations{};
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
}

void* operator new(std::size_t size)
{
	++allocations;
	if (auto* memory = std::malloc(size ? size : 1))
		return memory;
	throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

struct ID3D11Device
{};
namespace RE
{
	namespace BSGraphics
	{
		struct VertexShader
		{
			uint32_t id = 0;
		};
		struct PixelShader
		{
			uint32_t id = 0;
		};
		struct ComputeShader
		{
			uint32_t id = 0;
		};
	}
	struct BSShader
	{
		enum class Type
		{
			Lighting,
			Grass,
			DistantTree,
			Sky,
			Effect,
			ImageSpace,
			Total
		};
		struct TypeValue
		{
			Type value = Type::Lighting;
			Type get() const { return value; }
			unsigned underlying() const { return static_cast<unsigned>(value); }
			bool operator==(Type other) const { return value == other; }
		} shaderType;
		const char* fxpFilename = "Lighting";
		bool supported = true;
	};
}
namespace logger
{
	template <class... T>
	void debug(const char*, T&&...)
	{}
}
struct TestState
{
	bool developer = false, developerShaderEnabled = true;
	bool enableVShaders = true, enablePShaders = true, enableCShaders = true;
	bool deferredPixelShaderMissing = false;
	bool IsDeveloperMode() const { return developer; }
	bool IsShaderEnabled(const RE::BSShader&) const { return developerShaderEnabled; }
};
struct TestDeferred
{
	bool deferredPass = true;
	bool captureReady = true;
	unsigned invalidations = 0;
	void InvalidateCharacterCategoryCapture()
	{
		captureReady = false;
		++invalidations;
	}
};
namespace globals
{
	TestState* state;
	TestDeferred* deferred;
	namespace game
	{
		bool isVR = false;
	}
	namespace d3d
	{
		ID3D11Device* device;
	}
}
namespace SIE
{
	enum class ShaderClass
	{
		Vertex,
		Pixel,
		Compute
	};
	namespace SShaderCache
	{
		bool descriptorAvailable = true;
		std::optional<uint32_t> imageSpaceDescriptor;
		bool ResolveImageSpaceDescriptor(const RE::BSShader& shader, uint32_t& descriptor)
		{
			if (!descriptorAvailable)
				return false;
			if (shader.shaderType == RE::BSShader::Type::ImageSpace && imageSpaceDescriptor)
				descriptor = *imageSpaceDescriptor;
			return true;
		}
		std::string GetShaderString(ShaderClass, const RE::BSShader&, uint32_t descriptor, bool)
		{
			return std::to_string(descriptor);
		}
	}
	struct TestMutex
	{
		std::mutex mutex;
		std::size_t acquisitions = 0;
		std::function<void()> afterUnlock;
		void lock()
		{
			mutex.lock();
			++acquisitions;
		}
		void unlock()
		{
			mutex.unlock();
			if (afterUnlock) {
				auto action = std::exchange(afterUnlock, {});
				action();
			}
		}
	};
	struct ShaderCache
	{
#include "shader_resident_category.h"
		template <class T>
		using Maps = std::array<std::unordered_map<uint32_t, std::shared_ptr<T>>,
			static_cast<unsigned>(RE::BSShader::Type::Total)>;
		Maps<RE::BSGraphics::VertexShader> vertexShaders;
		Maps<RE::BSGraphics::PixelShader> pixelShaders;
		Maps<RE::BSGraphics::ComputeShader> computeShaders;
		TestMutex vertexShadersMutex, pixelShadersMutex, computeShadersMutex;
		std::shared_ptr<const uint8_t> residentShaderCacheOwner = std::make_shared<const uint8_t>();
		std::atomic<uint64_t> vertexShaderEvictionRevision{ 0 };
		std::atomic<uint64_t> pixelShaderEvictionRevision{ 0 };
		std::atomic<uint64_t> computeShaderEvictionRevision{ 0 };
		std::atomic<unsigned> activeShaderCaptureFramesRemaining{ 0 };
		int blockedKeyIndex = -1;
		std::string blockedKey;
		std::vector<uint32_t> blockedIDs;
		bool safeLoad = false, async = true, compilationSucceeds = true;
		std::array<unsigned, 3> compiles{}, tracked{};
		uint32_t lastTrackedDescriptor = 0;
		struct Task
		{
			ShaderClass stage;
			const RE::BSShader& shader;
			uint32_t descriptor;
		};
		struct Queue
		{
			unsigned count = 0;
			uint32_t lastDescriptor = 0;
			ShaderClass lastStage = ShaderClass::Vertex;
			void Add(Task task)
			{
				++count;
				lastDescriptor = task.descriptor;
				lastStage = task.stage;
			}
		} compilationSet;
		static bool IsSupportedShader(const RE::BSShader& shader) { return shader.supported; }
		bool IsSaveLoadSafeModeActive() const { return safeLoad; }
		bool ShouldUseAsyncCompilation() const { return async; }
		void TrackActiveShader(ShaderClass stage, const RE::BSShader&, uint32_t descriptor)
		{
			++tracked[static_cast<unsigned>(stage)];
			lastTrackedDescriptor = descriptor;
		}
		template <class T>
		std::shared_ptr<T> Compile(Maps<T>& maps, ShaderClass stage, const RE::BSShader& shader, uint32_t descriptor)
		{
			++compiles[static_cast<unsigned>(stage)];
			if (!compilationSucceeds)
				return {};
			auto result = std::make_shared<T>();
			result->id = descriptor;
			maps[shader.shaderType.underlying()].emplace(descriptor, result);
			return result;
		}
		std::shared_ptr<RE::BSGraphics::VertexShader> MakeAndAddVertexShader(const RE::BSShader& shader, uint32_t descriptor)
		{
			return Compile(vertexShaders, ShaderClass::Vertex, shader, descriptor);
		}
		std::shared_ptr<RE::BSGraphics::PixelShader> MakeAndAddPixelShader(const RE::BSShader& shader, uint32_t descriptor)
		{
			return Compile(pixelShaders, ShaderClass::Pixel, shader, descriptor);
		}
		std::shared_ptr<RE::BSGraphics::ComputeShader> MakeAndAddComputeShader(const RE::BSShader& shader, uint32_t descriptor)
		{
			return Compile(computeShaders, ShaderClass::Compute, shader, descriptor);
		}
		std::shared_ptr<RE::BSGraphics::VertexShader> GetVertexShaderIfCached(const RE::BSShader&, uint32_t);
		std::shared_ptr<RE::BSGraphics::VertexShader> GetVertexShader(const RE::BSShader&, uint32_t);
		std::shared_ptr<RE::BSGraphics::PixelShader> GetPixelShader(const RE::BSShader&, uint32_t, PixelShaderLookup = PixelShaderLookup::Compile);
		std::shared_ptr<RE::BSGraphics::ComputeShader> GetComputeShader(const RE::BSShader&, uint32_t);
		void PrewarmDeferredPixelShaders(const RE::BSShader&, uint32_t, bool);
	};

// Engine, compilation and state boundaries are stubbed; selection and its cache are production code.
#include "shader_resident_lookup_under_test.h"
#include "shader_resident_release_under_test.h"
}
namespace
{
	struct Fixture
	{
		TestState state;
		TestDeferred deferred;
		ID3D11Device device;
		SIE::ShaderCache cache;
		RE::BSShader shader;
		Fixture()
		{
			globals::state = &state;
			globals::deferred = &deferred;
			globals::d3d::device = &device;
			SIE::SShaderCache::descriptorAvailable = true;
			SIE::SShaderCache::imageSpaceDescriptor.reset();
		}
	};
	using Cache = SIE::ShaderCache;
	using Stage = SIE::ShaderClass;
	using Lookup = Cache::PixelShaderLookup;
	using Type = RE::BSShader::Type;
	const auto vertexLookup = [](Cache& cache, const RE::BSShader& shader, uint32_t descriptor) {
		return cache.GetVertexShader(shader, descriptor);
	};
	const auto pixelLookup = [](Cache& cache, const RE::BSShader& shader, uint32_t descriptor) {
		return cache.GetPixelShader(shader, descriptor);
	};
	const auto computeLookup = [](Cache& cache, const RE::BSShader& shader, uint32_t descriptor) {
		return cache.GetComputeShader(shader, descriptor);
	};
	const auto cachedVertexLookup = [](Cache& cache, const RE::BSShader& shader, uint32_t descriptor) {
		return cache.GetVertexShaderIfCached(shader, descriptor);
	};
	const auto cachedPixelLookup = [](Cache& cache, const RE::BSShader& shader, uint32_t descriptor) {
		return cache.GetPixelShader(shader, descriptor, Lookup::Cached);
	};

	template <class Maps>
	auto Publish(Maps& maps, const RE::BSShader& shader, uint32_t descriptor)
	{
		using Wrapper = typename Maps::value_type::mapped_type::element_type;
		auto result = std::make_shared<Wrapper>();
		result->id = descriptor;
		maps[shader.shaderType.underlying()].insert_or_assign(descriptor, result);
		return result;
	}

	template <class Get, class Maps>
	void CheckWarmHits(Get get, Maps Cache::* maps, SIE::TestMutex Cache::* mutex)
	{
		Fixture f;
		auto resident = Publish(f.cache.*maps, f.shader, 7);
		Require(get(f.cache, f.shader, 7) == resident, "An initial hit must acquire the authoritative owner");
		const auto locked = (f.cache.*mutex).acquisitions;
		Require(locked == 1, "The first positive lookup must take its stage lock");
		const auto allocated = allocations.load();
		for (unsigned repeat = 0; repeat < 1000; ++repeat)
			Require(get(f.cache, f.shader, 7) == resident, "Repeated hits must preserve the exact wrapper");
		Require((f.cache.*mutex).acquisitions == locked, "Repeated positive lookups must bypass their stage mutex");
		Require(allocations.load() == allocated, "Repeated production getter hits must allocate zero times");
	}

	template <class Get, class Maps>
	void CheckMissPublication(Get get, Maps Cache::* maps, SIE::TestMutex Cache::* mutex, Stage stage)
	{
		Fixture f;
		Require(!get(f.cache, f.shader, 13), "A cold asynchronous selection must have no shader");
		Require(!get(f.cache, f.shader, 13), "A repeated missing selection must remain a real lookup");
		Require((f.cache.*mutex).acquisitions == 2 && f.cache.compilationSet.count == 2,
			"A miss cannot suppress the existing compilation scheduling path");
		Require(f.cache.compilationSet.lastStage == stage && f.cache.compilationSet.lastDescriptor == 13,
			"Missing work must keep the exact stage and descriptor");
		auto resident = Publish(f.cache.*maps, f.shader, 13);
		Require(get(f.cache, f.shader, 13) == resident, "Publication must be visible on the next selection after a miss");
		const auto locked = (f.cache.*mutex).acquisitions;
		Require(get(f.cache, f.shader, 13) == resident && (f.cache.*mutex).acquisitions == locked,
			"The first published hit must warm the local cache");
		f.cache.safeLoad = true;
		Require(get(f.cache, f.shader, 13) == resident, "Save/load protection must preserve already resident shaders");
		const auto queued = f.cache.compilationSet.count;
		Require(!get(f.cache, f.shader, 14) && f.cache.compilationSet.count == queued,
			"Save/load protection must leave cold shaders unscheduled");
		f.cache.safeLoad = false;
		f.cache.async = false;
		f.cache.compilationSucceeds = false;
		Require(!get(f.cache, f.shader, 15), "Failed synchronous compilation must return no shader");
		f.cache.compilationSucceeds = true;
		Require(get(f.cache, f.shader, 15) != nullptr && f.cache.compiles[static_cast<unsigned>(stage)] == 2,
			"Failed synchronous compilation must not become a sticky local miss");
	}

	template <class Get, class Maps>
	void CheckSelectionGates(Get get, Maps Cache::* maps, SIE::TestMutex Cache::* mutex,
		bool TestState::* enabled, Stage stage)
	{
		Fixture f;
		auto resident = Publish(f.cache.*maps, f.shader, 19);
		Require(get(f.cache, f.shader, 19) == resident, "The policy fixture must begin with a warm hit");
		const auto locked = (f.cache.*mutex).acquisitions;
		f.state.*enabled = false;
		Require(!get(f.cache, f.shader, 19), "A warm cache must respect its stage-enable control");
		f.state.*enabled = true;
		f.shader.supported = false;
		Require(!get(f.cache, f.shader, 19), "A warm cache must respect shader support policy");
		f.state.developer = true;
		f.state.developerShaderEnabled = false;
		Require(!get(f.cache, f.shader, 19), "Developer mode must preserve the shader selection mask");
		f.state.developerShaderEnabled = true;
		Require(get(f.cache, f.shader, 19) == resident, "Developer selection must permit its enabled unsupported shader");
		f.cache.blockedKeyIndex = 0;
		f.cache.blockedKey = "19";
		Require(!get(f.cache, f.shader, 19), "A developer block must override an already warm shader");
		Require(f.cache.blockedIDs == std::vector<uint32_t>{ 19 }, "Developer blocks must still record their descriptor");
		f.cache.blockedKeyIndex = -1;
		f.shader.supported = true;
		f.state.developer = false;
		f.cache.activeShaderCaptureFramesRemaining = 1;
		const auto tracked = f.cache.tracked[static_cast<unsigned>(stage)];
		for (unsigned repeat = 0; repeat < 3; ++repeat)
			Require(get(f.cache, f.shader, 19) == resident, "Capture must preserve the resident selection");
		Require(f.cache.tracked[static_cast<unsigned>(stage)] == tracked + 3 && f.cache.lastTrackedDescriptor == 19,
			"Every warm selection must still reach the active capture hook");
		f.cache.activeShaderCaptureFramesRemaining = 0;
		SIE::SShaderCache::descriptorAvailable = false;
		Require(!get(f.cache, f.shader, 19), "Descriptor-resolution failure must override a warm selection");
		SIE::SShaderCache::descriptorAvailable = true;
		Require((f.cache.*mutex).acquisitions == locked, "Rejected policy gates and accepted warm hits must avoid map locks");
	}

	template <class Get, class Maps>
	void CheckContextAndIdentity(Get get, Maps Cache::* maps, SIE::TestMutex Cache::* mutex)
	{
		Fixture f;
		auto lighting = Publish(f.cache.*maps, f.shader, 23);
		Require(get(f.cache, f.shader, 23) == lighting, "Lighting must acquire its own descriptor owner");
		f.shader.shaderType.value = Type::Grass;
		auto grass = Publish(f.cache.*maps, f.shader, 23);
		Require(get(f.cache, f.shader, 23) == grass && grass != lighting,
			"The same descriptor must preserve shader-type identity");
		f.shader.shaderType.value = Type::ImageSpace;
		auto resolved = Publish(f.cache.*maps, f.shader, 29);
		SIE::SShaderCache::imageSpaceDescriptor = 29;
		Require(get(f.cache, f.shader, 777) == resolved && get(f.cache, f.shader, 888) == resolved,
			"Local lookup keys must use the resolved image-space descriptor");
		SIE::SShaderCache::imageSpaceDescriptor.reset();
		ID3D11Device replacement;
		globals::d3d::device = &replacement;
		const auto locked = (f.cache.*mutex).acquisitions;
		Require(get(f.cache, f.shader, 29) == resolved && (f.cache.*mutex).acquisitions == locked + 1,
			"A new device must reacquire the authoritative map owner");
		Cache other;
		auto another = Publish(other.*maps, f.shader, 29);
		Require(get(other, f.shader, 29) == another && another != resolved && (other.*mutex).acquisitions == 1,
			"Equal keys in another cache instance must never reuse the previous instance's owner");
	}

	template <class Get, class Maps>
	void CheckThreadIsolation(Get get, Maps Cache::* maps, SIE::TestMutex Cache::* mutex)
	{
		Fixture f;
		auto resident = Publish(f.cache.*maps, f.shader, 41);
		Require(get(f.cache, f.shader, 41) == resident, "The main thread must warm its own local entry");
		bool exact = false;
		std::size_t initialLocks = 0, repeatedLocks = 0;
		std::thread worker([&] {
			const auto before = (f.cache.*mutex).acquisitions;
			exact = get(f.cache, f.shader, 41) == resident;
			initialLocks = (f.cache.*mutex).acquisitions - before;
			for (unsigned repeat = 0; repeat < 100; ++repeat)
				exact = get(f.cache, f.shader, 41) == resident && exact;
			repeatedLocks = (f.cache.*mutex).acquisitions - before - initialLocks;
		});
		worker.join();
		Require(exact && initialLocks == 1 && repeatedLocks == 0,
			"Another thread must independently acquire and warm its authoritative shader owner");
		const auto locked = (f.cache.*mutex).acquisitions;
		Require(get(f.cache, f.shader, 41) == resident && (f.cache.*mutex).acquisitions == locked,
			"Worker selection must leave the main thread's positive entry intact");
	}

	template <class Get, class Maps>
	void CheckReusedCacheAddress(Get get, Maps Cache::* maps, SIE::TestMutex Cache::* mutex)
	{
		Fixture f;
		alignas(Cache) std::array<std::byte, sizeof(Cache)> storage;
		const auto destroy = [](Cache* cache) { std::destroy_at(cache); };
		std::unique_ptr<Cache, decltype(destroy)> first(std::construct_at(reinterpret_cast<Cache*>(storage.data())), destroy);
		auto previous = Publish(first.get()->*maps, f.shader, 43);
		Require(get(*first, f.shader, 43) == previous, "The original cache instance must warm a resident entry");
		first.reset();
		std::unique_ptr<Cache, decltype(destroy)> second(std::construct_at(reinterpret_cast<Cache*>(storage.data())), destroy);
		auto current = Publish(second.get()->*maps, f.shader, 43);
		Require(get(*second, f.shader, 43) == current && current != previous && (second.get()->*mutex).acquisitions == 1,
			"A fresh cache at the same address must reject a still-live shader from the previous instance");
	}
	template <class Get, class Maps>
	void CheckEviction(Get get, Maps Cache::* maps, SIE::TestMutex Cache::* mutex,
		std::atomic<uint64_t> Cache::* revision)
	{
		Fixture f;
		auto previous = Publish(f.cache.*maps, f.shader, 47);
		Require(get(f.cache, f.shader, 47) == previous, "Eviction must begin with an already warm old owner");
		const auto epoch = (f.cache.*revision).load();
		SIE::ReleaseShader(f.cache.*maps, f.cache.*mutex, f.cache.*revision, f.shader.shaderType.get(), 47);
		Require((f.cache.*revision).load() == epoch + 1,
			"Production single-key eviction must advance the stage's resident revision");
		const auto locked = (f.cache.*mutex).acquisitions;
		Require(!get(f.cache, f.shader, 47) && (f.cache.*mutex).acquisitions == locked + 1,
			"A still-live evicted owner cannot satisfy the next lookup without the authoritative map");
		Require(previous->id == 47, "A previously returned owner must remain valid after production eviction");
		auto current = Publish(f.cache.*maps, f.shader, 47);
		Require(get(f.cache, f.shader, 47) == current && current != previous,
			"Publication after eviction must replace the old local selection immediately");
		const auto reacquired = (f.cache.*mutex).acquisitions;
		Require(get(f.cache, f.shader, 47) == current && (f.cache.*mutex).acquisitions == reacquired,
			"A replacement owner must warm normally after eviction");
	}

	template <class Get, class Maps>
	void CheckFillEvictionRace(Get get, Maps Cache::* maps, SIE::TestMutex Cache::* mutex,
		std::atomic<uint64_t> Cache::* revision)
	{
		Fixture f;
		auto previous = Publish(f.cache.*maps, f.shader, 53);
		decltype(previous) current;
		const auto epoch = (f.cache.*revision).load();
		(f.cache.*mutex).afterUnlock = [&] {
			SIE::ReleaseShader(f.cache.*maps, f.cache.*mutex, f.cache.*revision, f.shader.shaderType.get(), 53);
			current = Publish(f.cache.*maps, f.shader, 53);
		};
		Require(get(f.cache, f.shader, 53) == previous && current && current != previous,
			"An owner acquired before map unlock must survive overlapping eviction and publication");
		Require((f.cache.*revision).load() == epoch + 1 && previous->id == 53,
			"Overlapping eviction must advance the epoch while keeping the acquired old owner valid");
		const auto locked = (f.cache.*mutex).acquisitions;
		Require(get(f.cache, f.shader, 53) == current && (f.cache.*mutex).acquisitions == locked + 1,
			"A fill stored after overlapping eviction must retain its captured old epoch and reacquire the replacement");
		const auto reacquired = (f.cache.*mutex).acquisitions;
		Require(get(f.cache, f.shader, 53) == current && (f.cache.*mutex).acquisitions == reacquired,
			"The replacement selected after an overlapping clear must become the next warm hit");
	}
	void CheckExactCachedLookups()
	{
		Fixture f;
		for (unsigned repeat = 0; repeat < 2; ++repeat) {
			Require(!f.cache.GetVertexShaderIfCached(f.shader, 31), "Exact vertex misses must stay empty");
			Require(!f.cache.GetPixelShader(f.shader, 31, Lookup::Cached), "Exact pixel misses must stay empty");
		}
		Require(f.cache.compilationSet.count == 0 && f.cache.vertexShadersMutex.acquisitions == 2 &&
					f.cache.pixelShadersMutex.acquisitions == 2,
			"Exact misses must check the maps without scheduling compilation");
		auto vertex = Publish(f.cache.vertexShaders, f.shader, 31);
		auto pixel = Publish(f.cache.pixelShaders, f.shader, 31);
		Require(f.cache.GetVertexShaderIfCached(f.shader, 31) == vertex &&
					f.cache.GetPixelShader(f.shader, 31, Lookup::Cached) == pixel,
			"Exact misses must expose subsequently published wrappers immediately");
		f.state.enableVShaders = false;
		Require(f.cache.GetVertexShaderIfCached(f.shader, 31) == vertex,
			"Exact cached vertex inspection must retain its independent stage-control contract");
		f.shader.shaderType.value = Type::Total;
		Require(!f.cache.GetVertexShaderIfCached(f.shader, 31), "An invalid exact vertex type must fail without indexing");
	}

	void CheckVRExclusion()
	{
		Fixture f;
		f.shader.fxpFilename = "OBBOcclusionTesting";
		auto vertex = Publish(f.cache.vertexShaders, f.shader, 37);
		auto pixel = Publish(f.cache.pixelShaders, f.shader, 37);
		globals::game::isVR = false;
		Require(f.cache.GetVertexShader(f.shader, 37) == vertex && f.cache.GetPixelShader(f.shader, 37) == pixel,
			"The occlusion producer must keep its existing flat selection behavior");
		globals::game::isVR = true;
		f.state.deferredPixelShaderMissing = true;
		Require(!f.cache.GetVertexShader(f.shader, 37) && !f.cache.GetPixelShader(f.shader, 37, Lookup::Draw),
			"VR occlusion exclusion must override warm vertex and pixel hits");
		Require(!f.state.deferredPixelShaderMissing && f.cache.compilationSet.count == 0,
			"An excluded Draw must reset its missing flag without scheduling work");
		globals::game::isVR = false;
	}

	void CheckCategoryFallback(Type type, bool categoryRequested)
	{
		Fixture f;
		f.shader.shaderType.value = type;
		const auto flag = Cache::GetCharacterCategoryFlag(type);
		const auto requested = 0x10001u | (categoryRequested ? flag : 0u);
		const auto opposite = requested ^ flag;
		auto fallback = Publish(f.cache.pixelShaders, f.shader, opposite);
		for (unsigned repeat = 0; repeat < 3; ++repeat) {
			f.deferred.captureReady = true;
			f.state.deferredPixelShaderMissing = true;
			Require(f.cache.GetPixelShader(f.shader, requested, Lookup::Draw) == fallback,
				"A missing requested category must use the compatible resident opposite layout");
			Require(!f.state.deferredPixelShaderMissing && !f.deferred.captureReady,
				"Every fallback Draw must reset its missing flag and invalidate category capture");
		}
		Require(f.deferred.invalidations == 3 && f.cache.compilationSet.count == 3,
			"A cached fallback cannot bypass repeated category invalidation or requested compilation");
		Require(f.cache.pixelShadersMutex.acquisitions == 4,
			"Requested misses must remain authoritative while repeated opposite-layout hits bypass the lock");
		f.deferred.deferredPass = false;
		Require(!f.cache.GetPixelShader(f.shader, requested, Lookup::Draw),
			"A forward Draw cannot reuse a deferred layout fallback");
		f.deferred.deferredPass = true;
		const auto queued = f.cache.compilationSet.count;
		Require(!f.cache.GetPixelShader(f.shader, requested, Lookup::Cached) && f.cache.compilationSet.count == queued,
			"Cached selection must remain exact and cannot schedule or select fallback");
		auto resident = Publish(f.cache.pixelShaders, f.shader, requested);
		f.deferred.captureReady = true;
		Require(f.cache.GetPixelShader(f.shader, requested, Lookup::Draw) == resident,
			"Newly published requested layout must replace the fallback immediately");
		const auto invalidations = f.deferred.invalidations;
		const auto locks = f.cache.pixelShadersMutex.acquisitions;
		for (unsigned repeat = 0; repeat < 10; ++repeat)
			Require(f.cache.GetPixelShader(f.shader, requested, Lookup::Draw) == resident,
				"An exact category owner must stay stable on repeated Draws");
		Require(f.deferred.captureReady && f.deferred.invalidations == invalidations &&
					f.cache.pixelShadersMutex.acquisitions == locks,
			"An exact warm Draw must preserve category readiness and avoid stage-map locks");
	}

	void CheckMissingCategoryDraw()
	{
		Fixture f;
		Require(!f.cache.GetPixelShader(f.shader, 0x10021, Lookup::Draw) && f.state.deferredPixelShaderMissing,
			"A deferred Draw missing both layouts must set its missing flag");
		f.state.enablePShaders = false;
		Require(!f.cache.GetPixelShader(f.shader, 0x10021, Lookup::Draw) && !f.state.deferredPixelShaderMissing,
			"A disabled pixel stage must reset a previous Draw's missing flag");
		f.state.enablePShaders = true;
		f.shader.shaderType.value = Type::ImageSpace;
		Require(!f.cache.GetPixelShader(f.shader, 0x10021, Lookup::Draw) && !f.state.deferredPixelShaderMissing,
			"A type without category layouts must preserve ordinary missing-selection behavior");
	}
}

int main()
{
	try {
		CheckWarmHits(vertexLookup, &Cache::vertexShaders, &Cache::vertexShadersMutex);
		CheckWarmHits(pixelLookup, &Cache::pixelShaders, &Cache::pixelShadersMutex);
		CheckWarmHits(computeLookup, &Cache::computeShaders, &Cache::computeShadersMutex);
		CheckWarmHits(cachedVertexLookup, &Cache::vertexShaders, &Cache::vertexShadersMutex);
		CheckWarmHits(cachedPixelLookup, &Cache::pixelShaders, &Cache::pixelShadersMutex);
		CheckThreadIsolation(vertexLookup, &Cache::vertexShaders, &Cache::vertexShadersMutex);
		CheckThreadIsolation(pixelLookup, &Cache::pixelShaders, &Cache::pixelShadersMutex);
		CheckThreadIsolation(computeLookup, &Cache::computeShaders, &Cache::computeShadersMutex);
		CheckReusedCacheAddress(vertexLookup, &Cache::vertexShaders, &Cache::vertexShadersMutex);
		CheckReusedCacheAddress(pixelLookup, &Cache::pixelShaders, &Cache::pixelShadersMutex);
		CheckReusedCacheAddress(computeLookup, &Cache::computeShaders, &Cache::computeShadersMutex);
		CheckEviction(vertexLookup, &Cache::vertexShaders, &Cache::vertexShadersMutex, &Cache::vertexShaderEvictionRevision);
		CheckEviction(pixelLookup, &Cache::pixelShaders, &Cache::pixelShadersMutex, &Cache::pixelShaderEvictionRevision);
		CheckEviction(computeLookup, &Cache::computeShaders, &Cache::computeShadersMutex, &Cache::computeShaderEvictionRevision);
		CheckFillEvictionRace(vertexLookup, &Cache::vertexShaders, &Cache::vertexShadersMutex, &Cache::vertexShaderEvictionRevision);
		CheckFillEvictionRace(pixelLookup, &Cache::pixelShaders, &Cache::pixelShadersMutex, &Cache::pixelShaderEvictionRevision);
		CheckFillEvictionRace(computeLookup, &Cache::computeShaders, &Cache::computeShadersMutex, &Cache::computeShaderEvictionRevision);
		for (bool vr : { false, true }) {
			globals::game::isVR = vr;
			CheckMissPublication(vertexLookup, &Cache::vertexShaders, &Cache::vertexShadersMutex, Stage::Vertex);
			CheckMissPublication(pixelLookup, &Cache::pixelShaders, &Cache::pixelShadersMutex, Stage::Pixel);
			CheckMissPublication(computeLookup, &Cache::computeShaders, &Cache::computeShadersMutex, Stage::Compute);
			CheckSelectionGates(vertexLookup, &Cache::vertexShaders, &Cache::vertexShadersMutex, &TestState::enableVShaders, Stage::Vertex);
			CheckSelectionGates(pixelLookup, &Cache::pixelShaders, &Cache::pixelShadersMutex, &TestState::enablePShaders, Stage::Pixel);
			CheckSelectionGates(computeLookup, &Cache::computeShaders, &Cache::computeShadersMutex, &TestState::enableCShaders, Stage::Compute);
			CheckContextAndIdentity(vertexLookup, &Cache::vertexShaders, &Cache::vertexShadersMutex);
			CheckContextAndIdentity(pixelLookup, &Cache::pixelShaders, &Cache::pixelShadersMutex);
			CheckContextAndIdentity(computeLookup, &Cache::computeShaders, &Cache::computeShadersMutex);
			CheckExactCachedLookups();
			for (auto type : { Type::Lighting, Type::Grass, Type::DistantTree, Type::Sky, Type::Effect })
				for (bool categories : { false, true })
					CheckCategoryFallback(type, categories);
			CheckMissingCategoryDraw();
		}
		CheckVRExclusion();
		std::cout << "Production shader getter cache hits, zero allocations, publication, selection gates, contexts and category fallback passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
