#include <Windows.h>
#include <ankerl/unordered_dense.h>
#include <magic_enum/magic_enum.hpp>
#include <winrt/base.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <set>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Utils/GenerationClaim.h"
#include "Utils/ShaderLifetime.h"

namespace
{
	std::atomic<unsigned> arraysAllocated{ 0 }, arraysFreed{ 0 };
	thread_local bool failNextGlobalAllocation = false;
	thread_local bool failNextMetadataAllocation = false;
	thread_local unsigned heldMetadataLocks = 0;
	thread_local std::function<void()> afterMetadataUnlock;
	struct MetadataMutex
	{
		std::mutex mutex;
		std::atomic<DWORD> owner{ 0 };
		void lock()
		{
			mutex.lock();
			owner = GetCurrentThreadId();
			++heldMetadataLocks;
		}
		void unlock()
		{
			owner = 0;
			--heldMetadataLocks;
			mutex.unlock();
			if (!heldMetadataLocks && afterMetadataUnlock) {
				auto action = std::exchange(afterMetadataUnlock, {});
				action();
			}
		}
		bool try_lock()
		{
			if (owner == GetCurrentThreadId() || !mutex.try_lock())
				return false;
			owner = GetCurrentThreadId();
			++heldMetadataLocks;
			return true;
		}
		bool HeldByCurrentThread() const { return owner == GetCurrentThreadId(); }
	};
	void Require(bool condition, const std::source_location& location = std::source_location::current())
	{
		if (!condition)
			throw std::runtime_error("Shader publication check failed at line " + std::to_string(location.line()));
	}

	thread_local unsigned heldMapLocks = 0;
	struct MapMutex
	{
		std::mutex mutex;
		std::function<void()> beforeUnlock;
		void lock()
		{
			mutex.lock();
			++heldMapLocks;
		}
		void unlock()
		{
			if (beforeUnlock)
				beforeUnlock();
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
		std::function<void()> duringFinalRelease;
		void AddRef() { ++references; }
		void Release()
		{
			if (heldMapLocks)
				observations.releaseUnderLock = true;
			if (--references == 0) {
				if (duringFinalRelease)
					duringFinalRelease();
				++observations.released;
				delete this;
			}
		}
	};
}

// A one-shot, thread-local failure isolates the production shared-owner allocation.
void* operator new(std::size_t size)
{
	if (std::exchange(failNextGlobalAllocation, false) ||
		(heldMetadataLocks && std::exchange(failNextMetadataAllocation, false)))
		throw std::bad_alloc();
	if (void* memory = std::malloc(size ? size : 1))
		return memory;
	throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void* operator new[](std::size_t size)
{
	auto* memory = ::operator new(size);
	++arraysAllocated;
	return memory;
}
void operator delete[](void* memory) noexcept
{
	++arraysFreed;
	::operator delete(memory);
}
void operator delete[](void* memory, std::size_t) noexcept { ::operator delete[](memory); }

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
	ULONG AddRef() { return ++references; }
	ULONG Release() { return --references; }
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
namespace SKSE::stl
{
	template <class Action>
	struct scope_exit
	{
		Action action;
		~scope_exit() { action(); }
	};
}
namespace Util
{
	namespace ContentHash
	{
		struct Hash128
		{
			uint64_t low = 0, high = 0;
		};
	}
	namespace ShaderContentReuse
	{
		struct Identity
		{};
	}
	std::string WStringToString(const std::wstring& path)
	{
		std::string result;
		result.reserve(path.size());
		for (const auto character : path) {
			if (static_cast<uint32_t>(character) > 0x7f)
				throw std::invalid_argument("Fixture shader paths must contain only ASCII characters");
			result.push_back(static_cast<char>(character));
		}
		return result;
	}
	std::string FixFilePath(const std::string& path) { return path; }
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
	void trace(const char*, Args&&...)
	{}
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

#include "shader_reflection_fixture.h"

namespace RE
{
	struct BSShader
	{
		enum class Type
		{
			Lighting,
			ImageSpace,
			Total
		};
		struct TypeValue
		{
			Type value = Type::Lighting;
			Type get() const { return value; }
			bool operator==(Type other) const { return value == other; }
		} shaderType;
		std::string fxpFilename = "Lighting.hlsl";
	};
	struct BSImagespaceShader : BSShader
	{
		std::string originalShaderName = "ImageSpace.hlsl";
	};
	namespace BSGraphics
	{
		namespace Vertex
		{
			enum Attribute
			{
				VA_POSITION,
				VA_TEXCOORD0,
				VA_TEXCOORD1,
				VA_NORMAL,
				VA_BINORMAL,
				VA_COLOR,
				VA_SKINNING,
				VA_INSTANCEDATA,
				VA_LANDDATA,
				VA_EYEDATA
			};
		}

		template <class T>
		struct Shader
		{
			T* shader = nullptr;
			uint32_t id = 0;
			uint32_t byteCodeSize = 4;
			uint64_t vertexDesc = 0;
			std::array<int8_t, 8> constantTable{};
			struct ConstantBuffer
			{
				ID3D11Buffer* buffer = nullptr;
				void* data = nullptr;
			};
			std::array<ConstantBuffer, 3> constantBuffers{};
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
	std::mutex g_diskCacheMutationMutex;
	std::function<void()> duringDiskAdvance;
	void AdvanceDiskCacheGeneration()
	{
		if (duringDiskAdvance)
			duringDiskAdvance();
	}
	enum class ShaderClass
	{
		Vertex,
		Pixel,
		Compute
	};
	using std::chrono::system_clock;
	struct ShaderCompilationTask
	{
		enum Status
		{
			Pending,
			Completed,
			Failed
		};
	};
	struct ShaderCacheResult
	{
		ID3DBlob* blob = nullptr;
		ShaderCompilationTask::Status status = ShaderCompilationTask::Completed;
		system_clock::time_point compileTime{};
		bool loadedFromDisk = false;
		uint64_t generation = 0;
	};
	struct ShaderCache
	{
		struct
		{
			std::atomic<uint64_t> generation{ 7 };
			std::function<void()> duringBump;
			void BumpGeneration()
			{
				++generation;
				if (duringBump)
					duringBump();
			}
			void Clear() { ++generation; }
		} compilationSet;
		std::unique_ptr<int> dependencyTracker;
		MetadataMutex mapMutex, hlslMapMutex;
		struct Notification
		{
			unsigned count = 0;
			void notify_all() { ++count; }
		} mapCV;
		struct hlslRecord
		{
			std::string key;
			RE::BSShader::Type type;
			uint32_t descriptor;
			ShaderClass shaderClass;
			std::wstring diskPath;
			Util::ContentHash::Hash128 compileStateDigest, packCompileStateDigest;
			bool developerMode = false;
			std::optional<Util::ContentHash::Hash128> sourceDigest;
			Microsoft::WRL::ComPtr<ID3DBlob> compiledBlob;
			std::optional<Util::ShaderContentReuse::Identity> contentIdentity;
			bool operator<(const hlslRecord& other) const { return key < other.key; }
		};
		std::unordered_map<std::string, ShaderCacheResult> shaderMap;
		std::unordered_map<std::string, int> deferredEvictions;
		std::unordered_map<std::string, std::set<hlslRecord>> hlslToShaderMap;
		bool ApplyDeferredEviction(const std::string&) { return false; }
		bool AddCompletedShader(const std::string&, ShaderClass, const RE::BSShader&, uint32_t,
			ID3DBlob*, const std::wstring&, const Util::ContentHash::Hash128&, const Util::ContentHash::Hash128&,
			bool, bool, std::optional<uint64_t>, std::optional<Util::ContentHash::Hash128>, std::optional<Util::ShaderContentReuse::Identity>);
		std::atomic<size_t> deferredEvictionCount{ 0 };
		void RequestFeatureShaderClear(bool) {}
		void Clear();
		void Clear(RE::BSShader::Type);
		void ClearShaderMap(RE::BSShader::Type);
		MapMutex vertexShadersMutex, pixelShadersMutex, computeShadersMutex;
		std::atomic_uint64_t vertexShaderEvictionRevision{ 0 }, pixelShaderEvictionRevision{ 0 }, computeShaderEvictionRevision{ 0 };
		template <class T>
		using Maps = std::array<ankerl::unordered_dense::map<uint32_t, std::shared_ptr<T>,
									ankerl::unordered_dense::hash<uint32_t>, std::equal_to<uint32_t>, TestAllocator<std::pair<uint32_t, std::shared_ptr<T>>>>,
			2>;
		Maps<RE::BSGraphics::VertexShader> vertexShaders;
		Maps<RE::BSGraphics::PixelShader> pixelShaders;
		Maps<RE::BSGraphics::ComputeShader> computeShaders;
		bool IsDiskCacheActive() const { return false; }
#include "shader_generation_check.h"

		std::shared_ptr<RE::BSGraphics::VertexShader> MakeAndAddVertexShader(const RE::BSShader&, uint32_t, std::optional<uint64_t> = std::nullopt);
		std::shared_ptr<RE::BSGraphics::PixelShader> MakeAndAddPixelShader(const RE::BSShader&, uint32_t, std::optional<uint64_t> = std::nullopt);
		std::shared_ptr<RE::BSGraphics::ComputeShader> MakeAndAddComputeShader(const RE::BSShader&, uint32_t, std::optional<uint64_t> = std::nullopt);
	};
	namespace SShaderCache
	{
		std::string GetTypeFromShaderString(const std::string& key) { return key; }
		std::string GetShaderString(ShaderClass, const RE::BSShader&, uint32_t descriptor, bool) { return std::to_string(descriptor); }
		std::wstring GetShaderPath(const std::string& path) { return { path.begin(), path.end() }; }
		ID3DBlob blob;
		bool compileFails = false;
		std::function<void()> duringCompile;
		ID3DBlob* CompileShader(ShaderClass, const RE::BSShader&, uint32_t, bool, int*, std::optional<uint64_t>)
		{
			if (duringCompile)
				duringCompile();
			return compileFails ? nullptr : &blob;
		}
		int GetVariableIndex(ShaderClass, const RE::BSShader&, const char* name)
		{
			return std::string_view(name) == "Known" ? 2 : -1;
		}
#include "shader_reflection_under_test.h"

// Factories require the reflected metadata implementation above.
#include "shader_factories_under_test.h"
	}
#include "shader_result_traits_under_test.h"

// Publication shares the extracted result traits and real generation-claim utility.
#include "shader_add_completed_under_test.h"
#include "shader_clear_metadata_under_test.h"
#include "shader_publication_under_test.h"
#include "shader_release_under_test.h"
#include "shader_retirement_under_test.h"

// Cache clears depend on the retirement helpers above.
#include "shader_full_clear_under_test.h"
#include "shader_type_clear_under_test.h"
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

	void CheckReflectionFailures()
	{
		using ReflectionFixture::Failure;
		ID3D11Device device;
		globals::d3d::device = &device;
		SIE::ShaderCache cache;
		RE::BSShader shader;
		shader.shaderType.value = RE::BSShader::Type::ImageSpace;
		for (auto failure : { Failure::Reflect, Failure::NullReflector, Failure::ShaderDesc,
				 Failure::Buffer, Failure::NullBuffer, Failure::Variable, Failure::NullVariable,
				 Failure::Type, Failure::NullType }) {
			ReflectionFixture::failure = failure;
			const auto before = observations.created.load();
			Require(!cache.MakeAndAddVertexShader(shader, 1));
			Require(!cache.MakeAndAddPixelShader(shader, 1));
			Require(cache.vertexShaders[1].empty() && cache.pixelShaders[1].empty());
			Require(observations.created == before);
		}
		for (auto failure : { Failure::Input, Failure::NullSemantic }) {
			ReflectionFixture::failure = failure;
			const auto before = observations.created.load();
			Require(!cache.MakeAndAddVertexShader(shader, 1));
			Require(observations.created == before);
		}
		ReflectionFixture::failure = Failure::None;
		auto valid = cache.MakeAndAddVertexShader(shader, 1);
		Require(valid && valid->vertexDesc && valid->constantTable[2] == 1);
		Require(cache.MakeAndAddPixelShader(shader, 1) != nullptr);
		ReflectionFixture::bufferCount = 0;
		auto absent = cache.MakeAndAddVertexShader(shader, 2);
		Require(absent && absent->constantBuffers[2].buffer == nullptr && absent->constantBuffers[2].data);
		ReflectionFixture::bufferCount = 1;
		ReflectionFixture::bufferName = "FeatureData";
		auto custom = cache.MakeAndAddPixelShader(shader, 2);
		Require(custom && custom->constantTable[2] == 0);
		ReflectionFixture::bufferName = "PerGeometry";
		ReflectionFixture::variableName = "Unknown";
		Require(cache.MakeAndAddPixelShader(shader, 3) != nullptr);
		ReflectionFixture::variableName = "Known";
	}

	void CheckTrailingBytecode()
	{
		const auto allocated = arraysAllocated.load(), freed = arraysFreed.load();
		{
			RE::BSShader shader;
			SIE::SShaderCache::blob.bytes = { 1, 7, 11, 23 };
			auto wrapper = SIE::SShaderCache::CreateVertexShader(SIE::SShaderCache::blob, shader, 9);
			Require(wrapper->id == 9);
			Require(std::memcmp(reinterpret_cast<uint8_t*>(wrapper.get()) + sizeof(*wrapper),
						SIE::SShaderCache::blob.GetBufferPointer(), SIE::SShaderCache::blob.GetBufferSize()) == 0);
		}
		Require(arraysAllocated == allocated + 1 && arraysFreed == freed + 1);
		bool overflowRejected = false;
		try {
			Util::ShaderLifetime::MakeWithTrailingBytes<RE::BSGraphics::VertexShader>(std::numeric_limits<std::size_t>::max());
		} catch (const std::bad_array_new_length&) {
			overflowRejected = true;
		}
		Require(overflowRejected && arraysAllocated == allocated + 1);
	}

	void CheckOwnerAllocationFailure()
	{
		auto wrapper = Util::ShaderLifetime::MakeWithTrailingBytes<RE::BSGraphics::VertexShader>(128);
		winrt::com_ptr<ID3D11VertexShader> resource;
		resource.attach(new ID3D11VertexShader);
		++observations.created;
		wrapper->shader = resource.get();
		const auto released = observations.released.load();
		const auto destroyed = observations.wrappersDestroyed.load();
		bool failed = false;
		failNextGlobalAllocation = true;
		try {
			Util::ShaderLifetime::Share(std::move(wrapper), std::move(resource));
		} catch (const std::bad_alloc&) {
			failed = true;
		}
		const bool injected = !std::exchange(failNextGlobalAllocation, false);
		Require(failed && injected && !wrapper && !resource);
		Require(observations.released == released + 1 && observations.wrappersDestroyed == destroyed + 1);
		Require(!observations.releaseUnderLock);
	}

	void CheckMetadataTransactions()
	{
		ID3DBlob blob;
		SIE::ShaderCache cache;
		RE::BSShader shader;
		const auto publish = [&](uint64_t generation) {
			return cache.AddCompletedShader("Lighting", SIE::ShaderClass::Vertex, shader, 1,
				&blob, L"cache/1", {}, {}, false, false, generation, {}, {});
		};
		bool cleared = false;
		afterMetadataUnlock = [&] {
			cache.Clear();
			cleared = true;
		};
		Require(publish(cache.compilationSet.generation.load()) && cleared);
		Require(cache.shaderMap.empty() && cache.hlslToShaderMap.empty() && blob.references == 1);

		// A producer admitted after clear's transaction keeps its source association.
		bool published = false;
		uint64_t admittedGeneration = 0;
		afterMetadataUnlock = [&] {
			admittedGeneration = cache.compilationSet.generation.load();
			published = publish(admittedGeneration);
		};
		cache.Clear();
		Require(published && cache.shaderMap.contains("Lighting"));
		Require(cache.hlslToShaderMap.at(shader.fxpFilename).begin()->key == "Lighting");
		Require(cache.compilationSet.generation == admittedGeneration + 1 && blob.references == 2);
		const auto staleGeneration = cache.compilationSet.generation.load();
		cache.Clear();
		Require(!publish(staleGeneration));
		Require(cache.shaderMap.empty() && cache.hlslToShaderMap.empty() && blob.references == 1);

		// Failed source-record allocation must still release metadata locks and wake waiters.
		const auto generation = cache.compilationSet.generation.load();
		cache.shaderMap["Lighting"] = { nullptr, SIE::ShaderCompilationTask::Pending, {}, false, generation };
		cache.hlslToShaderMap[shader.fxpFilename] = {};
		const auto notifications = cache.mapCV.count;
		failNextMetadataAllocation = true;
		const bool accepted = publish(generation);
		const bool injected = !std::exchange(failNextMetadataAllocation, false);
		Require(!accepted && injected && heldMetadataLocks == 0);
		Require(!cache.mapMutex.HeldByCurrentThread() && !cache.hlslMapMutex.HeldByCurrentThread());
		Require(cache.shaderMap.empty() && cache.hlslToShaderMap.at(shader.fxpFilename).empty());
		Require(cache.mapCV.count == notifications + 1 && blob.references == 1);
		Require(publish(generation));
		Require(cache.shaderMap.contains("Lighting") && cache.hlslToShaderMap.at(shader.fxpFilename).size() == 1);
		cache.Clear();
	}

	void CheckRetainedBinding()
	{
		int native = 0;
		int* current = nullptr;
		unsigned releases = 0;
		bool releasedWhileCurrent = false;
		const auto make = [&] {
			return std::shared_ptr<int>(new int(1), [&](int* value) {
				releasedWhileCurrent |= current == value;
				++releases;
				delete value;
			});
		};
		Util::ShaderLifetime::RetainedBinding<int> binding;
		auto first = make();
		std::weak_ptr<int> firstLifetime = first;
		binding.Set(current, first);
		first.reset();
		current = &native;
		Require(!firstLifetime.expired() && releases == 0);
		binding.Set(current, {});
		Require(current == nullptr && firstLifetime.expired() && releases == 1);
		binding.Set(current, make());
		auto next = make();
		binding.Set(current, next);
		Require(current == next.get() && releases == 2 && !releasedWhileCurrent);
		binding.Set(current, {});
		next.reset();
		Require(releases == 3 && !releasedWhileCurrent);
	}

	template <class Make, class Maps>
	void CheckLifetime(Make make, Maps maps, MapMutex SIE::ShaderCache::* mutex, std::atomic_uint64_t SIE::ShaderCache::* revision)
	{
		for (unsigned operation = 0; operation < 3; ++operation) {
			ID3D11Device device;
			SIE::ShaderCache cache;
			globals::d3d::device = &device;
			RE::BSShader shader;
			const auto create = [&](uint32_t descriptor, RE::BSShader::Type type = RE::BSShader::Type::Lighting) {
				shader.shaderType.value = type;
				return (cache.*make)(shader, descriptor, std::nullopt);
			};
			auto reader = create(20);
			auto selectedOwner = reader;
			create(21);
			create(22, RE::BSShader::Type::ImageSpace);
			const auto selectedRevision = (cache.*revision).load();
			SIE::ReleaseShader(cache.*maps, cache.*mutex, cache.*revision, RE::BSShader::Type::Lighting, 99);
			SIE::ReleaseShader(cache.*maps, cache.*mutex, cache.*revision, RE::BSShader::Type::Total, 20);
			Require((cache.*revision).load() == selectedRevision);
			using Wrapper = typename decltype(reader)::element_type;
			Util::ShaderLifetime::RetainedBinding<Wrapper> binding;
			Wrapper* nativeCurrent = nullptr;
			binding.Set(nativeCurrent, reader);
			Require(nativeCurrent == reader.get());
			std::weak_ptr<Wrapper> retired = reader;
			auto* oldResource = reader->shader;
			const auto before = observations.released.load();
			AsyncGate<void> gate{ 1 };
			gate.workers.push_back(std::async(std::launch::async, [reader = std::move(reader), &gate, oldResource]() mutable {
				gate.ArriveAndWait();
				Require(reader->id == 20 && reader->shader == oldResource);
				reader.reset();
			}));
			Require(gate.WaitUntilEntered());
			bool invalidatedUnderLock = false;
			(cache.*mutex).beforeUnlock = [&] {
				invalidatedUnderLock = heldMapLocks > 0 && (cache.*revision).load() > selectedRevision && !(cache.*maps)[0].contains(20);
			};
			if (operation == 0)
				SIE::ReleaseShader(cache.*maps, cache.*mutex, cache.*revision, RE::BSShader::Type::Lighting, 20);
			else
				SIE::RetireShaders(cache.*maps, cache.*mutex, cache.*revision, operation == 1 ? std::optional<size_t>{ 0 } : std::nullopt);
			(cache.*mutex).beforeUnlock = {};
			Require(invalidatedUnderLock && (cache.*revision).load() > selectedRevision);
			Require(!(cache.*maps)[0].contains(20));
			Require((cache.*maps)[0].contains(21) == (operation == 0));
			Require((cache.*maps)[1].contains(22) == (operation != 2));
			Require(observations.released == before + operation && !retired.expired());
			Require(nativeCurrent->id == 20 && nativeCurrent->shader == oldResource);

			// A reader survives both native replacement and release of the retained binding.
			Wrapper native;
			nativeCurrent = &native;
			binding.Set(nativeCurrent, {});
			Require(nativeCurrent == nullptr && !retired.expired());
			auto replacement = create(23);
			binding.Set(nativeCurrent, replacement);
			Require(nativeCurrent == replacement.get() && !retired.expired());
			Require(observations.released == before + operation);
			gate.Resume();
			gate.workers[0].get();
			Require(!retired.expired() && observations.released == before + operation);
			selectedOwner.reset();
			Require(retired.expired() && observations.released == before + operation + 1);
			const auto replacementRevision = (cache.*revision).load();
			SIE::RetireShaders(cache.*maps, cache.*mutex, cache.*revision);
			Require((cache.*revision).load() > replacementRevision);
			Require(nativeCurrent == replacement.get() && nativeCurrent->id == 23);
		}
	}

	template <class Make, class Maps>
	void CheckClearOrdering(Make make, Maps maps, std::atomic_uint64_t SIE::ShaderCache::* revision)
	{
		for (bool scoped : { false, true }) {
			ID3D11Device device;
			SIE::ShaderCache cache;
			globals::d3d::device = &device;
			RE::BSShader shader;
			const auto create = [&](uint32_t descriptor) { return (cache.*make)(shader, descriptor, std::nullopt); };
			auto target = create(30);
			shader.shaderType.value = RE::BSShader::Type::ImageSpace;
			create(32);
			shader.shaderType.value = RE::BSShader::Type::Lighting;
			cache.shaderMap["Lighting"] = {};
			cache.shaderMap["ImageSpace"] = {};
			cache.hlslToShaderMap["Lighting.hlsl"] = {};
			cache.hlslToShaderMap["ImageSpace.hlsl"] = {};
			const auto generation = cache.compilationSet.generation.load();
			const auto selectedRevision = (cache.*revision).load();
			bool recreated = false, ordered = false, metadataLocked = false, diskOrdered = scoped;
			cache.compilationSet.duringBump = [&] {
				metadataLocked = cache.mapMutex.HeldByCurrentThread() && (scoped || cache.hlslMapMutex.HeldByCurrentThread());
			};
			SIE::duringDiskAdvance = [&] {
				diskOrdered = cache.shaderMap.empty() && cache.hlslToShaderMap.empty() &&
				              cache.compilationSet.generation == generation + 1 && heldMetadataLocks == 0 &&
				              (cache.*maps)[0].contains(30);
			};
			target->shader->duringFinalRelease = [&] {
				std::unique_lock metadata(cache.mapMutex, std::try_to_lock);
				ordered = metadata.owns_lock() && cache.compilationSet.generation > generation && (cache.*revision).load() > selectedRevision;
				if (metadata.owns_lock()) {
					ordered &= !cache.shaderMap.contains("Lighting") && cache.shaderMap.contains("ImageSpace") == scoped;
					metadata.unlock();
				}
				if (ordered)
					recreated = create(31) != nullptr;
			};
			target.reset();
			if (scoped)
				cache.Clear(RE::BSShader::Type::Lighting);
			else
				cache.Clear();
			SIE::duringDiskAdvance = {};
			Require(metadataLocked && diskOrdered && cache.compilationSet.generation == generation + 2);
			Require(ordered && recreated && (cache.*maps)[0].contains(31) && !(cache.*maps)[0].contains(30));
			Require((cache.*maps)[1].contains(32) == scoped);
			Require(cache.vertexShaderEvictionRevision.load() > 0 && cache.pixelShaderEvictionRevision.load() > 0 && cache.computeShaderEvictionRevision.load() > 0);
		}
	}

	template <class Make, class Maps>
	void CheckStage(Make make, Maps maps, MapMutex SIE::ShaderCache::* mutex, std::atomic_uint64_t SIE::ShaderCache::* revision)
	{
		ID3D11Device device;
		SIE::ShaderCache cache;
		globals::d3d::device = &device;
		RE::BSShader shader;
		const auto create = [&](uint32_t descriptor, std::optional<uint64_t> generation = std::nullopt) {
			return (cache.*make)(shader, descriptor, generation);
		};
		const auto live = cache.compilationSet.generation.load();
		const auto selectedRevision = (cache.*revision).load();
		const auto mapSize = [&] { return (cache.*maps)[0].size(); };
		auto resident = create(1, live);
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
					lookupSucceeded = (cache.*maps)[0].at(1) == resident;
					++cache.compilationSet.generation;
				}
			}
			gate.Resume();
			auto result = gate.workers[0].get();
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
		auto firstResult = gate.workers[0].get();
		auto secondResult = gate.workers[1].get();
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
		Require((cache.*maps)[0].at(1) == resident && (cache.*maps)[0].at(3) == firstResult);
		Require(observations.released == beforeFailure + 1 && observations.wrappersDestroyed == wrappersBeforeFailure + 1);
		Require(device.references == 1 && SIE::SShaderCache::blob.references == 1);

		// Invalidation during slot allocation must remove the empty slot before returning.
		const auto beforeInvalidatedSlot = observations.released.load();
		AllocationControl::onAllocation = [&] { ++cache.compilationSet.generation; };
		Require(create(4) == nullptr && mapSize() == 2);
		Require(!AllocationControl::onAllocation);
		Require(observations.released == beforeInvalidatedSlot + 1);
		Require((cache.*maps)[0].at(1) == resident && (cache.*maps)[0].at(3) == firstResult);
		Require(create(4) != nullptr && mapSize() == 3);
		Require((cache.*revision).load() == selectedRevision);
	}
}

int main()
{
	try {
		CheckReflectionFailures();
		CheckTrailingBytecode();
		CheckOwnerAllocationFailure();
		CheckMetadataTransactions();
		CheckBytecodeHooks();
		CheckRetainedBinding();
		CheckStage(&SIE::ShaderCache::MakeAndAddVertexShader, &SIE::ShaderCache::vertexShaders, &SIE::ShaderCache::vertexShadersMutex, &SIE::ShaderCache::vertexShaderEvictionRevision);
		CheckLifetime(&SIE::ShaderCache::MakeAndAddVertexShader, &SIE::ShaderCache::vertexShaders, &SIE::ShaderCache::vertexShadersMutex, &SIE::ShaderCache::vertexShaderEvictionRevision);
		CheckClearOrdering(&SIE::ShaderCache::MakeAndAddVertexShader, &SIE::ShaderCache::vertexShaders, &SIE::ShaderCache::vertexShaderEvictionRevision);
		CheckStage(&SIE::ShaderCache::MakeAndAddPixelShader, &SIE::ShaderCache::pixelShaders, &SIE::ShaderCache::pixelShadersMutex, &SIE::ShaderCache::pixelShaderEvictionRevision);
		CheckLifetime(&SIE::ShaderCache::MakeAndAddPixelShader, &SIE::ShaderCache::pixelShaders, &SIE::ShaderCache::pixelShadersMutex, &SIE::ShaderCache::pixelShaderEvictionRevision);
		CheckClearOrdering(&SIE::ShaderCache::MakeAndAddPixelShader, &SIE::ShaderCache::pixelShaders, &SIE::ShaderCache::pixelShaderEvictionRevision);
		CheckStage(&SIE::ShaderCache::MakeAndAddComputeShader, &SIE::ShaderCache::computeShaders, &SIE::ShaderCache::computeShadersMutex, &SIE::ShaderCache::computeShaderEvictionRevision);
		CheckLifetime(&SIE::ShaderCache::MakeAndAddComputeShader, &SIE::ShaderCache::computeShaders, &SIE::ShaderCache::computeShadersMutex, &SIE::ShaderCache::computeShaderEvictionRevision);
		CheckClearOrdering(&SIE::ShaderCache::MakeAndAddComputeShader, &SIE::ShaderCache::computeShaders, &SIE::ShaderCache::computeShaderEvictionRevision);
		Require(!observations.driverUnderLock && !observations.releaseUnderLock && !observations.namingUnderLock);
		Require(observations.created == observations.released && observations.named > 0);
		Require(observations.wrappersCreated == observations.wrappersDestroyed);
		Require(arraysAllocated == arraysFreed);
		std::cout << "All three shader stages: resident lookups, stale generations, duplicate races, allocation/generation races, failure cleanup, retirement, metadata transactions and retained engine bindings passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
