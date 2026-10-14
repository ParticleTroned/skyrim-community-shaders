#include <Windows.h>
#include <ankerl/unordered_dense.h>
#include <magic_enum/magic_enum.hpp>
#include <winrt/base.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	void Require(bool condition, const std::source_location& location = std::source_location::current())
	{
		if (!condition)
			throw std::runtime_error("Shader publication check failed at line " + std::to_string(location.line()));
	}

	thread_local unsigned heldMapLocks = 0;
	struct MapMutex
	{
		std::mutex mutex;
		void lock()
		{
			mutex.lock();
			++heldMapLocks;
		}
		void unlock()
		{
			--heldMapLocks;
			mutex.unlock();
		}
		bool try_lock()
		{
			if (!mutex.try_lock())
				return false;
			++heldMapLocks;
			return true;
		}
	};

	struct Observations
	{
		std::atomic<unsigned> created{ 0 }, released{ 0 }, named{ 0 }, wrappersCreated{ 0 }, wrappersDestroyed{ 0 };
		std::atomic<bool> driverUnderLock{ false }, releaseUnderLock{ false }, namingUnderLock{ false };
	} observations;

	struct AllocationControl
	{
		static inline std::atomic<bool> failNext{ false };
		static inline std::function<void()> onAllocation;
	};

	template <class T>
	struct TestAllocator
	{
		using value_type = T;
		TestAllocator() = default;
		template <class U>
		TestAllocator(const TestAllocator<U>&)
		{}
		T* allocate(size_t count)
		{
			if (AllocationControl::failNext.exchange(false))
				throw std::bad_alloc();
			if (AllocationControl::onAllocation) {
				auto action = std::exchange(AllocationControl::onAllocation, {});
				action();
			}
			return std::allocator<T>{}.allocate(count);
		}
		void deallocate(T* memory, size_t count) noexcept { std::allocator<T>{}.deallocate(memory, count); }
		template <class U>
		bool operator==(const TestAllocator<U>&) const noexcept
		{
			return true;
		}
	};

	// Resume blocked work before futures are destroyed, including failed test setup.
	template <class Result>
	struct AsyncGate
	{
		explicit AsyncGate(unsigned required) : requiredArrivals(required), enteredFuture(entered.get_future()), resumeFuture(resume.get_future().share()) {}
		~AsyncGate() { Resume(); }
		void ArriveAndWait()
		{
			if (++arrivals == requiredArrivals)
				entered.set_value();
			resumeFuture.wait();
		}
		bool WaitUntilEntered() { return enteredFuture.wait_for(std::chrono::seconds(3)) == std::future_status::ready; }
		void Resume()
		{
			if (!std::exchange(resumed, true))
				resume.set_value();
		}
		unsigned requiredArrivals;
		std::atomic<unsigned> arrivals{ 0 };
		bool resumed = false;
		std::promise<void> entered, resume;
		std::future<void> enteredFuture;
		std::shared_future<void> resumeFuture;
		std::vector<std::future<Result>> workers;
	};

	struct ShaderObject
	{
		virtual ~ShaderObject() = default;
		unsigned references = 1;
		void AddRef() { ++references; }
		void Release()
		{
			if (heldMapLocks)
				observations.releaseUnderLock = true;
			if (--references == 0) {
				++observations.released;
				delete this;
			}
		}
	};
}

// Only driver and engine boundaries are substituted; publication is extracted from production.
struct ID3D11VertexShader : ShaderObject
{};
struct ID3D11PixelShader : ShaderObject
{};
struct ID3D11ComputeShader : ShaderObject
{};
struct ID3DBlob
{
	std::atomic<unsigned> references{ 1 };
	std::array<uint8_t, 4> bytes{};
	void AddRef() { ++references; }
	void Release() { --references; }
	void* GetBufferPointer() { return bytes.data(); }
	size_t GetBufferSize() const { return bytes.size(); }
};

struct ID3D11Device
{
	std::atomic<unsigned> references{ 1 };
	std::function<void()> duringCreate;
	HRESULT result = S_OK;
	bool outputOnFailure = false;
	bool provideShader = true;
	void AddRef() { ++references; }
	void Release() { --references; }
	template <class T>
	HRESULT Create(T** output)
	{
		if (heldMapLocks)
			observations.driverUnderLock = true;
		if (duringCreate)
			duringCreate();
		if ((SUCCEEDED(result) && provideShader) || outputOnFailure) {
			*output = new T;
			++observations.created;
		}
		return result;
	}
	HRESULT CreateVertexShader(const void*, size_t, void*, ID3D11VertexShader** output) { return Create(output); }
	HRESULT CreatePixelShader(const void*, size_t, void*, ID3D11PixelShader** output) { return Create(output); }
	HRESULT CreateComputeShader(const void*, size_t, void*, ID3D11ComputeShader** output) { return Create(output); }
};

namespace globals::d3d
{
	ID3D11Device* device = nullptr;
}
namespace Util
{
	void SetResourceName(ShaderObject*, const char*, ...)
	{
		++observations.named;
		if (heldMapLocks)
			observations.namingUnderLock = true;
	}
}
namespace logger
{
	template <class... Args>
	void debug(const char*, Args&&...)
	{}
	template <class... Args>
	void error(const char*, Args&&...)
	{}
}
namespace fmt
{
	const char* runtime(const char* text) { return text; }
}
#include "shader_bytecode_map.h"

// The extracted hooks use the bytecode-map declarations above.
#include "shader_bytecode_hooks.h"

namespace RE
{
	struct BSShader
	{
		enum class Type
		{
			Lighting,
			Total
		};
		struct TypeValue
		{
			Type get() const { return Type::Lighting; }
		} shaderType;
	};
	namespace BSGraphics
	{
		template <class T>
		struct Shader
		{
			T* shader = nullptr;
			uint32_t id = 0;
			uint32_t byteCodeSize = 4;
			Shader() { ++observations.wrappersCreated; }
			~Shader() { ++observations.wrappersDestroyed; }
		};
		using VertexShader = Shader<ID3D11VertexShader>;
		using PixelShader = Shader<ID3D11PixelShader>;
		using ComputeShader = Shader<ID3D11ComputeShader>;
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
	struct ShaderCache
	{
		struct
		{
			std::atomic<uint64_t> generation{ 7 };
		} compilationSet;
		std::unique_ptr<int> dependencyTracker;
		MapMutex vertexShadersMutex, pixelShadersMutex, computeShadersMutex;
		template <class T>
		using Maps = std::array<ankerl::unordered_dense::map<uint32_t, std::unique_ptr<T>,
									ankerl::unordered_dense::hash<uint32_t>, std::equal_to<uint32_t>, TestAllocator<std::pair<uint32_t, std::unique_ptr<T>>>>,
			1>;
		Maps<RE::BSGraphics::VertexShader> vertexShaders;
		Maps<RE::BSGraphics::PixelShader> pixelShaders;
		Maps<RE::BSGraphics::ComputeShader> computeShaders;
		bool IsDiskCacheActive() const { return false; }
#include "shader_generation_check.h"

		RE::BSGraphics::VertexShader* MakeAndAddVertexShader(const RE::BSShader&, uint32_t, std::optional<uint64_t> = std::nullopt);
		RE::BSGraphics::PixelShader* MakeAndAddPixelShader(const RE::BSShader&, uint32_t, std::optional<uint64_t> = std::nullopt);
		RE::BSGraphics::ComputeShader* MakeAndAddComputeShader(const RE::BSShader&, uint32_t, std::optional<uint64_t> = std::nullopt);
		~ShaderCache()
		{
			const auto release = [](auto& maps) {
				for (auto& [id, wrapper] : maps[0])
					wrapper->shader->Release();
			};
			release(vertexShaders);
			release(pixelShaders);
			release(computeShaders);
		}
	};
	namespace SShaderCache
	{
		ID3DBlob blob;
		bool compileFails = false;
		std::function<void()> duringCompile;
		ID3DBlob* CompileShader(ShaderClass, const RE::BSShader&, uint32_t, bool, int*, std::optional<uint64_t>)
		{
			if (duringCompile)
				duringCompile();
			return compileFails ? nullptr : &blob;
		}
		std::unique_ptr<RE::BSGraphics::VertexShader> CreateVertexShader(ID3DBlob&, const RE::BSShader&, uint32_t descriptor)
		{
			auto shader = std::make_unique<RE::BSGraphics::VertexShader>();
			shader->id = descriptor;
			return shader;
		}
		std::unique_ptr<RE::BSGraphics::PixelShader> CreatePixelShader(ID3DBlob&, const RE::BSShader&, uint32_t descriptor)
		{
			auto shader = std::make_unique<RE::BSGraphics::PixelShader>();
			shader->id = descriptor;
			return shader;
		}
		std::unique_ptr<RE::BSGraphics::ComputeShader> CreateComputeShader(ID3DBlob&, const RE::BSShader&, uint32_t descriptor)
		{
			auto shader = std::make_unique<RE::BSGraphics::ComputeShader>();
			shader->id = descriptor;
			return shader;
		}
	}
#include "shader_publication_under_test.h"
}

namespace
{
	void CheckBytecodeHooks()
	{
		constexpr size_t workerCount = 8;
		constexpr size_t entriesPerWorker = 128;
		std::array<uint64_t, workerCount * entriesPerWorker> keys{};
		const uint64_t payload = 0x123456789abcdef0;
		uint64_t residentKey = 0;
		RegisterShaderBytecode(&residentKey, &payload, sizeof(payload));
		const auto& resident = GetShaderBytecode(&residentKey);
		const auto* residentBytes = resident.first.get();
		AsyncGate<void> gate{ workerCount };
		for (size_t worker = 0; worker < workerCount; ++worker) {
			gate.workers.push_back(std::async(std::launch::async, [&, worker] {
				gate.ArriveAndWait();
				for (size_t i = worker * entriesPerWorker; i < (worker + 1) * entriesPerWorker; ++i) {
					keys[i] = static_cast<uint64_t>(i);
					RegisterShaderBytecode(&keys[i], &keys[i], sizeof(keys[i]));
					const auto& own = GetShaderBytecode(&keys[i]);
					Require(own.second == sizeof(keys[i]) && std::memcmp(own.first.get(), &keys[i], sizeof(keys[i])) == 0);
					Require(GetShaderBytecode(&residentKey).first.get() == residentBytes);
					Require(std::memcmp(resident.first.get(), &payload, sizeof(payload)) == 0);
				}
			}));
		}
		const bool started = gate.WaitUntilEntered();
		gate.Resume();
		for (auto& worker : gate.workers)
			worker.get();
		Require(started);
		Require(ShaderBytecodeMap.size() == keys.size() + 1);
		ShaderBytecodeMap.clear();
	}

	template <class Make, class Maps>
	void CheckStage(Make make, Maps maps, MapMutex SIE::ShaderCache::* mutex)
	{
		ID3D11Device device;
		SIE::ShaderCache cache;
		globals::d3d::device = &device;
		RE::BSShader shader;
		const auto create = [&](uint32_t descriptor, std::optional<uint64_t> generation = std::nullopt) {
			return (cache.*make)(shader, descriptor, generation);
		};
		const auto live = cache.compilationSet.generation.load();
		const auto mapSize = [&] { return (cache.*maps)[0].size(); };
		auto* resident = create(1, live);
		Require(resident && resident->shader && resident->id == 1 && mapSize() == 1);
		Require(device.references == 1 && SIE::SShaderCache::blob.references == 1);

		globals::d3d::device = nullptr;
		Require(create(2, live) == nullptr && mapSize() == 1);
		globals::d3d::device = &device;
		device.provideShader = false;
		Require(create(2, live) == nullptr && mapSize() == 1);
		device.provideShader = true;

		const auto releases = observations.released.load();
		auto* originalObject = resident->shader;
		Require(create(1, live) == resident);
		Require(resident->shader == originalObject && observations.released == releases + 1);
		Require(mapSize() == 1);

		device.result = E_FAIL;
		for (bool partial : { false, true }) {
			device.outputOnFailure = partial;
			const auto before = observations.released.load();
			Require(create(2, live) == nullptr && mapSize() == 1);
			Require(observations.released == before + (partial ? 1u : 0u));
		}
		device.result = S_OK;
		device.outputOnFailure = false;
		SIE::SShaderCache::compileFails = true;
		Require(create(2, live) == nullptr && mapSize() == 1);
		SIE::SShaderCache::compileFails = false;

		const auto beforeStale = observations.created.load();
		Require(create(2, live - 1) == nullptr && observations.created == beforeStale);
		SIE::SShaderCache::duringCompile = [&] { ++cache.compilationSet.generation; };
		Require(create(2) == nullptr && observations.created == beforeStale);
		SIE::SShaderCache::duringCompile = {};

		// A resident lookup and invalidation must finish while the driver is blocked.
		for (bool synchronous : { false, true }) {
			AsyncGate<decltype(resident)> gate{ 1 };
			bool ownsInputs = false;
			device.duringCreate = [&] {
				ownsInputs = device.references >= 2 && SIE::SShaderCache::blob.references >= 2;
				gate.ArriveAndWait();
			};
			const auto generation = cache.compilationSet.generation.load();
			const auto before = observations.released.load();
			gate.workers.push_back(std::async(std::launch::async, [&] {
				return create(2, synchronous ? std::nullopt : std::optional{ generation });
			}));
			const bool started = gate.WaitUntilEntered();
			bool lookupSucceeded = false;
			{
				std::unique_lock lock(cache.*mutex, std::try_to_lock);
				if (started && lock.owns_lock()) {
					lookupSucceeded = (cache.*maps)[0].at(1).get() == resident;
					++cache.compilationSet.generation;
				}
			}
			gate.Resume();
			auto* result = gate.workers[0].get();
			device.duringCreate = {};
			Require(started && ownsInputs && lookupSucceeded);
			Require(result == nullptr && mapSize() == 1 && observations.released == before + 1);
			Require(device.references == 1 && SIE::SShaderCache::blob.references == 1);
		}

		// Two candidates prepared together must resolve to one stable published wrapper.
		AsyncGate<decltype(resident)> gate{ 2 };
		device.duringCreate = [&] { gate.ArriveAndWait(); };
		const auto beforeDuplicate = observations.released.load();
		gate.workers.push_back(std::async(std::launch::async, [&] { return create(3); }));
		gate.workers.push_back(std::async(std::launch::async, [&] { return create(3); }));
		const bool overlapped = gate.WaitUntilEntered();
		gate.Resume();
		auto* firstResult = gate.workers[0].get();
		auto* secondResult = gate.workers[1].get();
		device.duringCreate = {};
		Require(overlapped && firstResult && firstResult == secondResult);
		Require(mapSize() == 2 && observations.released == beforeDuplicate + 1);
		Require(device.references == 1 && SIE::SShaderCache::blob.references == 1);

		// A failed slot allocation cannot consume a prepared wrapper or its driver object.
		const auto beforeFailure = observations.released.load();
		const auto wrappersBeforeFailure = observations.wrappersDestroyed.load();
		AllocationControl::failNext = true;
		bool allocationFailed = false;
		try {
			create(4);
		} catch (const std::bad_alloc&) {
			allocationFailed = true;
		}
		AllocationControl::failNext = false;
		Require(allocationFailed && mapSize() == 2);
		Require((cache.*maps)[0].at(1).get() == resident && (cache.*maps)[0].at(3).get() == firstResult);
		Require(observations.released == beforeFailure + 1 && observations.wrappersDestroyed == wrappersBeforeFailure + 1);
		Require(device.references == 1 && SIE::SShaderCache::blob.references == 1);

		// Invalidation during slot allocation must remove the empty slot before returning.
		const auto beforeInvalidatedSlot = observations.released.load();
		AllocationControl::onAllocation = [&] { ++cache.compilationSet.generation; };
		Require(create(4) == nullptr && mapSize() == 2);
		Require(!AllocationControl::onAllocation);
		Require(observations.released == beforeInvalidatedSlot + 1);
		Require((cache.*maps)[0].at(1).get() == resident && (cache.*maps)[0].at(3).get() == firstResult);
		Require(create(4) != nullptr && mapSize() == 3);
	}
}

int main()
{
	try {
		CheckBytecodeHooks();
		CheckStage(&SIE::ShaderCache::MakeAndAddVertexShader, &SIE::ShaderCache::vertexShaders, &SIE::ShaderCache::vertexShadersMutex);
		CheckStage(&SIE::ShaderCache::MakeAndAddPixelShader, &SIE::ShaderCache::pixelShaders, &SIE::ShaderCache::pixelShadersMutex);
		CheckStage(&SIE::ShaderCache::MakeAndAddComputeShader, &SIE::ShaderCache::computeShaders, &SIE::ShaderCache::computeShadersMutex);
		Require(!observations.driverUnderLock && !observations.releaseUnderLock && !observations.namingUnderLock);
		Require(observations.created == observations.released && observations.named > 0);
		Require(observations.wrappersCreated == observations.wrappersDestroyed);
		std::cout << "All three shader stages: resident lookups, stale generations, duplicate races, allocation/generation races, failure cleanup and input ownership passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
