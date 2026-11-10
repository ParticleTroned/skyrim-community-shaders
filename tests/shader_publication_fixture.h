#pragma once

#include <array>
#include <atomic>
#include <barrier>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
struct Resource
{
	unsigned releases = 0;
	void (*onRelease)() = nullptr;
	void Release()
	{
		if (onRelease)
			onRelease();
		++releases;
	}
} resource;
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
	unsigned creates = 0;
	bool fail = false;
	int CreateVertexShader(const void*, size_t, void*, Resource** output)
	{
		++creates;
		*output = fail ? nullptr : &resource;
		return fail ? -1 : 0;
	}
	int CreatePixelShader(const void* bytes, size_t size, void* unused, Resource** output)
	{
		return CreateVertexShader(bytes, size, unused, output);
	}
} testDevice;
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
std::barrier<> rendezvous{ 2 };
bool concurrent = false, failCompile = false;
namespace SShaderCache
{
	Blob* CompileShader(ShaderClass, const RE::BSShader&, uint32_t, bool, int*, std::optional<uint64_t>)
	{
		if (concurrent)
			rendezvous.arrive_and_wait();
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
