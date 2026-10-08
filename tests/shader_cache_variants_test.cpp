#include <Windows.h>
#include <ankerl/unordered_dense.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
	void Require(bool a_condition, const std::source_location& a_location = std::source_location::current())
	{
		if (!a_condition)
			throw std::runtime_error("Shader variant check failed at line " + std::to_string(a_location.line()));
	}

	struct RuntimeShader
	{
		RuntimeShader* shader = this;
		unsigned* releases;
		explicit RuntimeShader(unsigned& a_releases) : releases(&a_releases) {}
		void Release() { ++*releases; }
	};
}

namespace logger
{
	template <class... Args>
	void debug(const char*, Args&&...)
	{}
	template <class... Args>
	void warn(const char*, Args&&...)
	{}
	template <class... Args>
	void info(const char*, Args&&...)
	{}
}

namespace RE
{
	struct BSShader
	{
		enum Type
		{
			Lighting,
			ImageSpace,
			Total
		};
		struct TypeValue
		{
			Type value;
			Type get() const { return value; }
		} shaderType{ Lighting };
		std::string fxpFilename = "Shared";
	};
}

struct Feature
{
	bool loaded = true;
	unsigned clears = 0;
	void ClearShaderCacheScoped() { ++clears; }
	static std::vector<Feature*>& GetFeatureList()
	{
		static std::vector<Feature*> features;
		return features;
	}
};

namespace SIE
{
#include "shader_task_declarations.h"
#include "shader_task_identity_under_test.h"

	ShaderCompilationTask::ShaderCompilationTask(ShaderClass a_class, const RE::BSShader& a_shader, uint32_t a_descriptor) :
		shaderClass(a_class), shader(a_shader), descriptor(a_descriptor), cachedPriority(1)
	{}

	namespace SShaderCache
	{
		std::string GetShaderString(ShaderClass a_class, const RE::BSShader& a_shader, uint32_t, bool)
		{
			return a_shader.fxpFilename + std::to_string(static_cast<int>(a_class));
		}
		std::wstring GetDiskPath(const std::string& a_name, uint32_t a_descriptor, ShaderClass a_class)
		{
			return std::wstring(a_name.begin(), a_name.end()) + L"/" + std::to_wstring(a_descriptor) + L"/" + std::to_wstring(static_cast<int>(a_class));
		}
	}
}

#include "shader_task_comparison.h"

namespace SIE
{
	// Scheduling, engine and disk boundaries are stubbed; the queue admission,
	// capture declarations, tracking, identity and eviction code comes from source.
	struct CompilationSet
	{
		std::mutex compilationMutex;
		std::condition_variable conditionVariable;
		std::set<ShaderCompilationTask, TaskPriorityLess> availableTasks, tasksInProgress, processedTasks;
		std::atomic<uint64_t> generation{ 9 }, totalTasks{ 0 }, totalPriorityWeight{ 0 };
		std::atomic<int64_t> lastResetQpc{ 0 };
		LARGE_INTEGER lastReset{}, lastCalculation{};
		std::unordered_set<size_t> forgotten;
		void Add(const ShaderCompilationTask& task);
		bool IsInProgress(size_t a_id)
		{
			std::lock_guard lock(compilationMutex);
			return std::any_of(tasksInProgress.begin(), tasksInProgress.end(), [&](const auto& a_task) { return a_task.GetId() == a_id; });
		}
		void Forget(const std::unordered_set<size_t>& a_ids)
		{
			forgotten.insert(a_ids.begin(), a_ids.end());
		}
	};

	struct ShaderCache
	{
#include "shader_capture_declarations.h"
#include "shader_capture_state.h"
		struct Blob
		{
			ShaderCompilationTask::Status status = ShaderCompilationTask::Completed;
		};
		std::unordered_map<std::string, Blob> shaderMap;
		std::unordered_set<std::string> deferredEvictions;
		std::mutex mapMutex, vertexShadersMutex, pixelShadersMutex, computeShadersMutex;
		using RuntimeMaps = std::array<std::unordered_map<uint32_t, std::unique_ptr<RuntimeShader>>, RE::BSShader::Total>;
		RuntimeMaps vertexShaders, pixelShaders, computeShaders;
		CompilationSet compilationSet;
		std::vector<std::wstring> deletedPaths;
		unsigned bytecodeQueries = 0;
		bool GetCompletedShader(const ShaderCompilationTask&)
		{
			++bytecodeQueries;
			return !shaderMap.empty();
		}
		void StartActiveShaderCaptureWindow(ActiveShaderCaptureStage a_stage);
		void EvictShaderResources(RE::BSShader::Type a_type, uint32_t a_descriptor, ShaderClass a_shaderClass);
		void EvictShader(const std::string& a_key, RE::BSShader::Type a_type, uint32_t a_descriptor, ShaderClass a_shaderClass);
		void DeleteScopedDiskCacheEntries(const std::vector<std::wstring>& a_paths)
		{
			deletedPaths.insert(deletedPaths.end(), a_paths.begin(), a_paths.end());
		}
	};
}

namespace globals
{
	struct State
	{
		bool developer = false;
		bool IsDeveloperMode() const { return developer; }
	} stateStorage;
	struct Deferred
	{
		unsigned clears = 0;
		void ClearShaderCache() { ++clears; }
	} deferredStorage;
	State* state = &stateStorage;
	Deferred* deferred = &deferredStorage;
	SIE::ShaderCache* shaderCache = nullptr;
}

namespace SIE
{
#include "shader_capture_tracking_under_test.h"
#include "shader_capture_under_test.h"
#include "shader_eviction_under_test.h"
#include "shader_queue_add_under_test.h"
}

namespace
{
	using SIE::ShaderCache;
	using SIE::ShaderClass;
	using SIE::ShaderCompilationTask;
	using Stage = ShaderCache::ActiveShaderCaptureStage;

	void FinishWindow(ShaderCache& a_cache, bool a_menuVisible)
	{
		a_cache.activeShaderCaptureFramesRemaining.store(1);
		a_cache.TickActiveShaderCapture(a_menuVisible);
	}

	void TestQueueWithSharedBytecode()
	{
		ShaderCache cache;
		globals::shaderCache = &cache;
		cache.shaderMap["shared"] = {};
		RE::BSShader shader;
		ShaderCompilationTask first(ShaderClass::Pixel, shader, 10);
		ShaderCompilationTask second(ShaderClass::Pixel, shader, 11);
		auto& queue = cache.compilationSet;
		queue.Add(first);
		queue.Add(first);
		queue.Add(second);
		Require(queue.availableTasks.size() == 2 && queue.totalTasks == 2);
		Require(queue.totalPriorityWeight == 4 && cache.bytecodeQueries == 0);
		for (const auto& task : queue.availableTasks) {
			Require(task.GetGeneration() == 9 && task.GetEnqueuedQpc() > 0);
		}
		const auto sessionStart = queue.lastResetQpc.load();
		queue.availableTasks.clear();
		queue.tasksInProgress.insert(first);
		queue.processedTasks.insert(second);
		queue.Add(first);
		queue.Add(second);
		Require(queue.availableTasks.empty() && queue.totalTasks == 2);
		Require(queue.lastResetQpc == sessionStart);
		globals::shaderCache = nullptr;
	}

	void TestCaptureAndClearVariants(bool a_developer)
	{
		ShaderCache cache;
		globals::state->developer = a_developer;
		globals::deferred->clears = 0;
		Feature loaded, unloaded;
		unloaded.loaded = false;
		Feature::GetFeatureList() = { &loaded, &unloaded };
		RE::BSShader shader;
		const auto key = SIE::SShaderCache::GetShaderString(ShaderClass::Pixel, shader, 10, true);
		if (a_developer)
			cache.TrackActiveShader(ShaderClass::Pixel, shader, 99);
		cache.BeginActiveShaderCapture();
		cache.BeginActiveShaderCapture();
		for (uint32_t descriptor : { 10u, 11u, 10u })
			cache.TrackActiveShader(ShaderClass::Pixel, shader, descriptor);
		Require(cache.capturedShaders.size() == 2);
		Require(cache.activeShaders.size() == (a_developer ? 1u : 0u));
		if (a_developer)
			Require(cache.activeShaders.at(key).descriptor == 99 && cache.activeShaders.at(key).drawCalls == 4);
		unsigned releases = 0;
		for (uint32_t descriptor : { 10u, 11u }) {
			const auto id = ShaderCompilationTask::MakeId(ShaderClass::Pixel, shader.shaderType.get(), descriptor);
			const auto& info = cache.capturedShaders.at(id);
			Require(info.descriptor == descriptor && info.key == key);
			Require(info.diskPath == SIE::SShaderCache::GetDiskPath(shader.fxpFilename, descriptor, ShaderClass::Pixel));
			cache.pixelShaders[RE::BSShader::Lighting][descriptor] = std::make_unique<RuntimeShader>(releases);
		}
		std::thread bulkLoad([&] { cache.TrackActiveShader(ShaderClass::Pixel, shader, 88); });
		bulkLoad.join();
		Require(cache.capturedShaders.size() == 2);
		cache.shaderMap[key] = {};
		FinishWindow(cache, true);
		Require(cache.activeShaderCaptureStage == Stage::AwaitingMenuClose);
		Require(cache.lastScopedClearCount == 2 && releases == 2);
		Require(cache.shaderMap.empty() && cache.clearedBytecodeThisCaptureCycle.size() == 1);
		Require(cache.compilationSet.forgotten.size() == 2 && cache.deletedPaths.size() == 2);
		Require(cache.deletedPaths[0] != cache.deletedPaths[1]);
		Require(globals::deferred->clears == 1 && loaded.clears == 1 && unloaded.clears == 0);

		cache.shaderMap[key] = {};
		cache.pixelShaders[RE::BSShader::Lighting][10] = std::make_unique<RuntimeShader>(releases);
		cache.pixelShaders[RE::BSShader::Lighting][12] = std::make_unique<RuntimeShader>(releases);
		cache.TickActiveShaderCapture(false);
		Require(cache.activeShaderCaptureStage == Stage::SecondWindow);
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 12);
		FinishWindow(cache, false);
		Require(cache.activeShaderCaptureStage == Stage::Idle);
		Require(cache.lastScopedClearCount == 1 && releases == 3);
		Require(cache.shaderMap.contains(key) && cache.pixelShaders[RE::BSShader::Lighting].contains(10));
		Require(cache.compilationSet.forgotten.size() == 3 && cache.deletedPaths.size() == 3);
		Require(globals::deferred->clears == 1 && loaded.clears == 1);
		cache.BeginActiveShaderCapture();
		Require(cache.clearedThisCaptureCycle.empty() && cache.clearedBytecodeThisCaptureCycle.empty());
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		FinishWindow(cache, true);
		Require(releases == 4 && cache.shaderMap.empty());
		Feature::GetFeatureList().clear();
	}

	void TestProtectedVariants()
	{
		for (unsigned protection = 0; protection < 3; ++protection) {
			ShaderCache cache;
			globals::state->developer = false;
			RE::BSShader shader;
			const auto key = SIE::SShaderCache::GetShaderString(ShaderClass::Pixel, shader, 7, true);
			cache.shaderMap[key] = {};
			unsigned releases = 0;
			cache.pixelShaders[RE::BSShader::Lighting][7] = std::make_unique<RuntimeShader>(releases);
			if (protection == 0)
				cache.shaderMap[key].status = ShaderCompilationTask::Pending;
			else if (protection == 1)
				cache.compilationSet.tasksInProgress.emplace(ShaderClass::Pixel, shader, 7);
			else
				cache.deferredEvictions.insert(key);
			cache.BeginActiveShaderCapture();
			cache.TrackActiveShader(ShaderClass::Pixel, shader, 7);
			FinishWindow(cache, true);
			Require(releases == 0 && cache.shaderMap.contains(key));
			Require(cache.clearedThisCaptureCycle.empty() && cache.clearedBytecodeThisCaptureCycle.empty());
			Require(cache.compilationSet.forgotten.empty() && cache.deletedPaths.empty());
			cache.shaderMap[key].status = ShaderCompilationTask::Completed;
			cache.compilationSet.tasksInProgress.clear();
			cache.deferredEvictions.clear();
			cache.TickActiveShaderCapture(false);
			cache.TrackActiveShader(ShaderClass::Pixel, shader, 7);
			FinishWindow(cache, false);
			Require(releases == 1 && cache.shaderMap.empty());
		}
	}

	void TestStageAndTypeIdentity()
	{
		ShaderCache cache;
		globals::state->developer = false;
		RE::BSShader lighting, imageSpace;
		imageSpace.shaderType.value = RE::BSShader::ImageSpace;
		cache.TrackActiveShader(ShaderClass::Vertex, lighting, 4);
		Require(cache.capturedShaders.empty() && cache.activeShaders.empty());
		cache.BeginActiveShaderCapture();
		cache.TrackActiveShader(ShaderClass::Vertex, lighting, 4);
		cache.TrackActiveShader(ShaderClass::Pixel, lighting, 4);
		cache.TrackActiveShader(ShaderClass::Compute, imageSpace, 4);
		cache.TrackActiveShader(ShaderClass::Pixel, imageSpace, 4);
		Require(cache.capturedShaders.size() == 4);
		unsigned releases = 0;
		cache.vertexShaders[RE::BSShader::Lighting][4] = std::make_unique<RuntimeShader>(releases);
		cache.pixelShaders[RE::BSShader::Lighting][4] = std::make_unique<RuntimeShader>(releases);
		cache.pixelShaders[RE::BSShader::ImageSpace][4] = std::make_unique<RuntimeShader>(releases);
		cache.computeShaders[RE::BSShader::ImageSpace][4] = std::make_unique<RuntimeShader>(releases);
		FinishWindow(cache, true);
		Require(releases == 4 && cache.compilationSet.forgotten.size() == 4);
		Require(cache.deletedPaths.size() == 3);
	}
}

int main()
{
	try {
		TestQueueWithSharedBytecode();
		TestCaptureAndClearVariants(false);
		TestCaptureAndClearVariants(true);
		TestProtectedVariants();
		TestStageAndTypeIdentity();
		std::cout << "Shader cache variant checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
