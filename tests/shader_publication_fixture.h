#pragma once

#include <array>
#include <atomic>
#include <barrier>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <latch>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
struct ResourceCounters
{
	std::atomic_uint releases{ 0 }, live{ 0 }, named{ 0 };
	void (*onRelease)() = nullptr;
} resource;
struct Resource
{
	std::atomic_uint references{ 1 }, releases{ 0 };
	std::string name;
	void AddRef()
	{
		const auto previous = references.fetch_add(1);
		assert(previous > 0);
	}
	void Release()
	{
		const auto previous = references.fetch_sub(1);
		assert(previous > 0);
		if (resource.onRelease)
			resource.onRelease();
		++releases;
		++resource.releases;
		if (previous == 1)
			--resource.live;
	}
};
namespace winrt
{
	template <class T>
	class com_ptr
	{
	public:
		com_ptr() = default;
		com_ptr(const com_ptr&) = delete;
		com_ptr& operator=(const com_ptr&) = delete;
		com_ptr(com_ptr&& other) noexcept : value(other.detach()) {}
		com_ptr& operator=(com_ptr&& other) noexcept
		{
			if (this != &other) {
				if (value)
					value->Release();
				value = other.detach();
			}
			return *this;
		}
		~com_ptr()
		{
			if (value)
				value->Release();
		}
		T* get() const { return value; }
		T** put()
		{
			assert(!value);
			return &value;
		}
		T* detach() { return std::exchange(value, nullptr); }

	private:
		T* value = nullptr;
	};
}
using ID3D11VertexShader = Resource;
using ID3D11PixelShader = Resource;
constexpr bool FAILED(int result) { return result < 0; }
namespace RE
{
	struct BSShader
	{
		enum class Type
		{
			Water
		};
		struct
		{
			Type get() const { return Type::Water; }
		} shaderType;
	};
	namespace BSGraphics
	{
		struct VertexShader
		{
			Resource* shader = nullptr;
			unsigned byteCodeSize = 1;
		};
		struct PixelShader
		{
			Resource* shader = nullptr;
		};
	}
}
namespace logger
{
	template <class... T>
	void debug(const char*, T&&...)
	{}
	template <class... T>
	void error(const char*, T&&...)
	{}
	template <class... T>
	void info(const char*, T&&...)
	{}
}
namespace magic_enum
{
	const char* enum_name(RE::BSShader::Type) { return "Water"; }
}
struct Device
{
	std::atomic_uint creates{ 0 };
	bool fail = false;
	std::mutex resourcesMutex;
	std::vector<std::unique_ptr<Resource>> resources;
	std::function<void()> afterCreate;
	void Reset()
	{
		assert(resource.live == 0);
		for (const auto& created : resources)
			assert(created->references == 0 && created->releases == 1);
		resources.clear();
		creates = resource.releases = resource.named = 0;
		resource.onRelease = nullptr;
		fail = false;
		afterCreate = {};
	}
	int CreateVertexShader(const void*, size_t, void*, Resource** output)
	{
		++creates;
		*output = nullptr;
		if (fail)
			return -1;
		{
			std::lock_guard lock(resourcesMutex);
			auto created = std::make_unique<Resource>();
			resources.push_back(std::move(created));
			++resource.live;
			*output = resources.back().get();
		}
		if (afterCreate)
			afterCreate();
		return 0;
	}
	int CreatePixelShader(const void* bytes, size_t size, void* unused, Resource** output)
	{
		return CreateVertexShader(bytes, size, unused, output);
	}
} testDevice;
namespace Util
{
	template <class... Args>
	void SetResourceName(Resource* shader, const char* format, Args... args)
	{
		assert(shader && shader->references > 0 && shader->name.empty());
		std::array<char, 128> name{};
		const auto size = std::snprintf(name.data(), name.size(), format, args...);
		assert(size > 0 && static_cast<size_t>(size) < name.size());
		shader->name.assign(name.data(), static_cast<size_t>(size));
		++resource.named;
	}
}
namespace globals::d3d
{
	auto* device = &testDevice;
}
enum class ShaderClass
{
	Vertex,
	Pixel
};
struct Blob
{
	const void* GetBufferPointer() const { return this; }
	size_t GetBufferSize() const { return 1; }
} blob;
bool failCompile = false;
std::function<void()> afterCompile;
namespace SShaderCache
{
	Blob* CompileShader(ShaderClass, const RE::BSShader&, uint32_t, bool, int*, std::optional<uint64_t>)
	{
		if (afterCompile)
			afterCompile();
		return failCompile ? nullptr : &blob;
	}
	auto CreateVertexShader(const Blob&, const RE::BSShader&, uint32_t)
	{
		return std::make_unique<RE::BSGraphics::VertexShader>();
	}
	auto CreatePixelShader(const Blob&, const RE::BSShader&, uint32_t)
	{
		return std::make_unique<RE::BSGraphics::PixelShader>();
	}
}
struct ShaderCache
{
	~ShaderCache() { ReleaseResources(); }
	void ReleaseResources()
	{
		const auto release = [](auto& families) {
			for (auto& shaders : families) {
				for (auto& [id, shader] : shaders)
					if (shader->shader)
						shader->shader->Release();
				shaders.clear();
			}
		};
		release(vertexShaders);
		release(pixelShaders);
		release(computeShaders);
	}
	struct Queue
	{
		std::atomic_uint64_t generation{ 1 };
		std::array<std::atomic_uint64_t, 1> typeEpochs{};
		std::mutex compilationMutex;
		unsigned clears = 0;
		uint64_t LiveGeneration(RE::BSShader::Type a_type) const;
		bool IsCurrentGeneration(RE::BSShader::Type a_type, std::optional<uint64_t> a_taskGeneration) const;
		void BumpGeneration();
		void Clear()
		{
			++clears;
			generation.fetch_add(1, std::memory_order_release);
		}
	} compilationSet;
	std::atomic_uint64_t graphicsShaderRevision{ 0 };
	std::unique_ptr<int> dependencyTracker;
	std::mutex vertexShadersMutex, pixelShadersMutex;
	std::array<std::unordered_map<uint32_t, std::unique_ptr<RE::BSGraphics::VertexShader>>, 1> vertexShaders;
	std::array<std::unordered_map<uint32_t, std::unique_ptr<RE::BSGraphics::PixelShader>>, 1> pixelShaders;
	decltype(pixelShaders) computeShaders;
	std::mutex computeShadersMutex, mapMutex, hlslMapMutex;
	std::condition_variable mapCV;
	std::unordered_map<int, int> shaderMap, hlslToShaderMap;
	std::unordered_set<int> deferredEvictions;
	std::atomic_uint64_t deferredEvictionCount{ 0 };
	void Clear();
	bool IsDiskCacheActive() const { return false; }
	bool IsTaskStale(std::optional<uint64_t> a_taskGeneration) const;
	bool IsTaskStale(std::optional<uint64_t> a_taskGeneration, RE::BSShader::Type a_type) const;
	RE::BSGraphics::VertexShader* MakeAndAddVertexShader(const RE::BSShader&, uint32_t, std::optional<uint64_t> = {});
	RE::BSGraphics::PixelShader* MakeAndAddPixelShader(const RE::BSShader&, uint32_t, std::optional<uint64_t> = {});
};

struct CompileDedupe
{
	unsigned clears = 0;
	void Clear() { ++clears; }
} compileDedupe;
CompileDedupe& GetCompileDedupe() { return compileDedupe; }

struct Feature
{
	bool loaded = true;
	unsigned clears = 0;
	void (*onClear)() = nullptr;
	void ClearShaderCache()
	{
		++clears;
		if (onClear)
			onClear();
	}
	static std::vector<Feature*>& GetFeatureList()
	{
		static std::vector<Feature*> features;
		return features;
	}
};
namespace globals
{
	ShaderCache* shaderCache = nullptr;
	struct Deferred
	{
		unsigned clears = 0;
		void ClearShaderCache() { ++clears; }
	} deferredStorage;
	auto* deferred = &deferredStorage;
}
namespace Util
{
	inline void ClearShaderCompileFailures() {}
}
std::mutex g_diskCacheMutationMutex;
uint64_t diskGeneration = 0;
void AdvanceDiskCacheGeneration() { ++diskGeneration; }
