#include <Windows.h>
#include <ankerl/unordered_dense.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Utils/RendererOwnership.h"

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
		~RuntimeShader() { ++*releases; }
	};
}

namespace logger
{
	unsigned errorCount = 0;
	template <class... Args>
	void debug(const char*, Args&&...)
	{}
	template <class... Args>
	void warn(const char*, Args&&...)
	{}
	template <class... Args>
	void info(const char*, Args&&...)
	{}
	template <class... Args>
	void error(const char*, Args&&...)
	{
		++errorCount;
	}
}

namespace RE
{
	struct BSShader
	{
		enum Type
		{
			Lighting,
			ImageSpace,
			Grass,
			DistantTree,
			Sky,
			Effect,
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
	unsigned clears = 0, fullClears = 0, preparations = 0;
	std::function<void()> duringClear, duringPrepare;
	std::string GetName() const { return "TestFeature"; }
	void ClearShaderCache()
	{
		++fullClears;
		if (duringClear)
			duringClear();
	}
	void ClearShaderCacheScoped()
	{
		++clears;
		if (duringClear)
			duringClear();
	}
	void PrepareShaderCache()
	{
		++preparations;
		if (duringPrepare)
			duringPrepare();
	}
	static std::vector<Feature*>& GetFeatureList()
	{
		static std::vector<Feature*> features;
		return features;
	}
};

namespace efsw
{
	using WatchID = std::int64_t;
	enum class Actions
	{
		Add,
		Delete,
		Modified,
		Moved
	};
	using Action = Actions;
}

namespace Util::ShaderCreation
{
	struct Attempt;
}

namespace SIE
{
#include "shader_task_declarations.h"
#include "shader_task_identity_under_test.h"

	ShaderCompilationTask::ShaderCompilationTask(ShaderClass a_class, const RE::BSShader& a_shader, uint32_t a_descriptor) :
		shaderClass(a_class), shader(a_shader), descriptor(a_descriptor), cachedPriority(1)
	{}

	namespace SShaderCache
	{
		std::atomic_uint shaderStringCalls{ 0 }, diskPathCalls{ 0 };
		std::string GetShaderString(ShaderClass a_class, const RE::BSShader& a_shader, uint32_t, bool)
		{
			++shaderStringCalls;
			return a_shader.fxpFilename + std::to_string(static_cast<int>(a_class));
		}
		std::wstring GetDiskPath(const std::string& a_name, uint32_t a_descriptor, ShaderClass a_class)
		{
			++diskPathCalls;
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
#include "shader_category_flag.h"
		std::atomic<uint8_t> pendingFeatureClear{ 0 };
		std::atomic_bool pendingStandaloneShaderClear{ false };
		void RequestStandaloneShaderClear();
		void RequestFeatureShaderClear(bool scoped);
		void ProcessPendingFeatureClear();
		std::vector<uint32_t> pixelRequests;
		void GetPixelShader(const RE::BSShader&, uint32_t descriptor) { pixelRequests.push_back(descriptor); }
		void PrewarmDeferredPixelShaders(const RE::BSShader& shader, uint32_t descriptor, bool categories);
		struct Blob
		{
			ShaderCompilationTask::Status status = ShaderCompilationTask::Completed;
		};
		std::unordered_map<std::string, Blob> shaderMap;
		std::unordered_set<std::string> deferredEvictions;
		std::mutex mapMutex, vertexShadersMutex, pixelShadersMutex, computeShadersMutex;
		std::atomic_uint64_t vertexShaderEvictionRevision{ 0 }, pixelShaderEvictionRevision{ 0 }, computeShaderEvictionRevision{ 0 };
		using RuntimeMaps = std::array<std::unordered_map<uint32_t, std::shared_ptr<RuntimeShader>>, RE::BSShader::Total>;
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
	unsigned sourceInvalidations = 0;
	void InvalidateShaderSourceCaches() { ++sourceInvalidations; }
	struct UpdateListener
	{
#include "shader_watcher_action_under_test.h"
		std::mutex actionMutex;
		std::vector<fileAction> queue;
		std::vector<std::filesystem::path> observedPaths;
		void handleFileAction(efsw::WatchID, const std::string&, const std::string&, efsw::Action, std::string);
		void UpdateCache(const std::filesystem::path&, ShaderCache*, bool&);
		void Process(const fileAction& fAction, ShaderCache* cache)
		{
			const auto filePath = std::filesystem::path(fAction.dir) / fAction.filename;
			bool fileDone = false;
			switch (fAction.action) {
#include "shader_watcher_routes_under_test.h"
			default:
				break;
			}
		}
	};
}

namespace globals
{
	namespace game
	{
		int* renderer = nullptr;
	}
	namespace d3d
	{
		int* context = nullptr;
	}
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
	namespace features
	{
		struct LightLimitFix
		{
			bool loaded = false;
			unsigned clears = 0, preparations = 0, waits = 0;
			std::function<void()> duringClear, duringPrepare, duringWait;
			void ClearShaderCache()
			{
				++clears;
				if (duringClear)
					duringClear();
			}
			void PrepareShaderCache()
			{
				++preparations;
				if (duringPrepare)
					duringPrepare();
			}
			bool WaitShaderCacheReady()
			{
				++waits;
				if (duringWait)
					duringWait();
				return true;
			}
		} lightLimitFix;
	}
}

namespace Util
{
	CRITICAL_SECTION rendererLock;
	CRITICAL_SECTION* GetRendererContextLock(int*, int*) { return &rendererLock; }
}

namespace SIE
{
#include "shader_capture_tracking_under_test.h"
#include "shader_capture_under_test.h"
#include "shader_category_prewarm.h"
#include "shader_eviction_under_test.h"
#include "shader_feature_clear_under_test.h"
#include "shader_queue_add_under_test.h"
#include "shader_watcher_handle_under_test.h"
#include "shader_watcher_invalidation_under_test.h"
}

struct Upscaling
{
	bool loaded = true;
	struct
	{
		bool neuralRenderingEnabled = false;
		bool neuralCharacterRenderingEnabled = false;
		bool neuralCharacterSceneStrengthsEnabled = false;
		bool neuralCharacterVisualIsolationEnabled = false;
	} settings;
	bool NeedsCharacterCategoryShaders() const;
};
#include "shader_category_demand.h"

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
		a_cache.ProcessPendingFeatureClear();
	}

	void TestFeatureClearBoundary()
	{
		ShaderCache cache;
		Feature first, second, unloaded;
		unloaded.loaded = false;
		Feature::GetFeatureList() = { &first, &second, &unloaded };
		globals::deferred->clears = 0;
		cache.RequestFeatureShaderClear(true);
		cache.RequestFeatureShaderClear(false);
		cache.RequestFeatureShaderClear(true);
		Require(first.clears == 0 && first.fullClears == 0 && globals::deferred->clears == 0);
		std::vector<unsigned> order;
		auto& llf = globals::features::lightLimitFix;
		llf = {};
		llf.loaded = true;
		llf.duringWait = [&] { order.push_back(5); };
		first.duringClear = [&] { order.push_back(1); };
		second.duringClear = [&] { order.push_back(2); };
		first.duringPrepare = [&] {
			order.push_back(3);
			cache.RequestFeatureShaderClear(true);
			throw std::runtime_error("expected preparation failure");
		};
		second.duringPrepare = [&] { order.push_back(4); };

		// A busy renderer leaves the coalesced request pending for the next boundary.
		std::future<void> blocked;
		bool completed = false;
		{
			const Util::RendererOwnership ownership(&Util::rendererLock, true);
			blocked = std::async(std::launch::async, [&] { cache.ProcessPendingFeatureClear(); });
			completed = blocked.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
		}
		blocked.get();
		Require(completed && order.empty() && cache.pendingFeatureClear == 3);
		cache.ProcessPendingFeatureClear();
		Require(order == std::vector<unsigned>({ 1, 2, 3, 4, 5 }) && llf.waits == 1);
		Require(first.fullClears == 1 && second.fullClears == 1 && first.clears == 0 && second.clears == 0);
		Require(unloaded.fullClears == 0 && unloaded.clears == 0 && unloaded.preparations == 0);
		Require(globals::deferred->clears == 1 && cache.pendingFeatureClear == 1);
		order.clear();
		first.duringPrepare = [&] { order.push_back(3); };
		cache.ProcessPendingFeatureClear();
		Require(order == std::vector<unsigned>({ 1, 2, 3, 4, 5 }) && llf.waits == 2);
		Require(first.clears == 1 && second.clears == 1 && first.preparations == 2 && second.preparations == 2);
		Require(globals::deferred->clears == 2 && cache.pendingFeatureClear == 0);
		cache.ProcessPendingFeatureClear();
		Require(globals::deferred->clears == 2 && llf.waits == 2);
		llf = {};
		Feature::GetFeatureList().clear();
	}

	void TestFeatureClearInvalidationFailure()
	{
		for (unsigned failedIndex = 0; failedIndex < 2; ++failedIndex) {
			ShaderCache cache;
			std::array<Feature, 3> features;
			Feature::GetFeatureList() = { &features[0], &features[1], &features[2] };
			std::vector<unsigned> order;
			for (unsigned index = 0; index < features.size(); ++index) {
				features[index].duringClear = [&, index] { order.push_back(index + 1); };
				features[index].duringPrepare = [&, index] { order.push_back(index + 4); };
			}
			features[failedIndex].duringClear = [&] {
				order.push_back(failedIndex + 1);
				auto concurrent = std::async(std::launch::async, [&] { cache.RequestFeatureShaderClear(false); });
				concurrent.get();
				if (failedIndex == 0)
					throw std::runtime_error("expected invalidation failure");
				throw 1;
			};
			const auto errorsBefore = logger::errorCount;
			cache.RequestFeatureShaderClear(true);
			cache.ProcessPendingFeatureClear();
			Require(order.size() == failedIndex + 1 && cache.pendingFeatureClear == 3);
			Require(logger::errorCount == errorsBefore + 1);
			for (unsigned index = 0; index < features.size(); ++index) {
				Require(features[index].clears == (index <= failedIndex ? 1u : 0u));
				Require(features[index].fullClears == 0 && features[index].preparations == 0);
			}
			features[failedIndex].duringClear = [&] { order.push_back(failedIndex + 1); };
			order.clear();
			cache.ProcessPendingFeatureClear();
			Require(order == std::vector<unsigned>({ 1, 2, 3, 4, 5, 6 }) && cache.pendingFeatureClear == 0);
			for (const auto& feature : features)
				Require(feature.fullClears == 1 && feature.preparations == 1);
			Require(logger::errorCount == errorsBefore + 1);
			Feature::GetFeatureList().clear();
		}
	}

	void TestStandaloneClearBoundary()
	{
		ShaderCache cache;
		Feature unrelated;
		Feature::GetFeatureList() = { &unrelated };
		auto& llf = globals::features::lightLimitFix;
		llf = {};
		llf.loaded = true;
		std::vector<unsigned> order;
		llf.duringClear = [&] { order.push_back(1); };
		llf.duringPrepare = [&] { order.push_back(2); };
		llf.duringWait = [&] { order.push_back(3); };
		const auto deferredClears = globals::deferred->clears;
		cache.RequestStandaloneShaderClear();
		cache.RequestStandaloneShaderClear();
		std::future<void> denied;
		bool returned = false;
		{
			const Util::RendererOwnership ownership(&Util::rendererLock, true);
			denied = std::async(std::launch::async, [&] { cache.ProcessPendingFeatureClear(); });
			returned = denied.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
		}
		denied.get();
		Require(returned && order.empty() && cache.pendingStandaloneShaderClear);
		cache.ProcessPendingFeatureClear();
		Require(order == std::vector<unsigned>({ 1, 2, 3 }) && !cache.pendingStandaloneShaderClear);
		Require(llf.clears == 1 && llf.preparations == 1 && llf.waits == 1);
		Require(unrelated.clears == 0 && unrelated.fullClears == 0 && unrelated.preparations == 0 && globals::deferred->clears == deferredClears);
		cache.ProcessPendingFeatureClear();
		Require(llf.waits == 1);
		llf.duringPrepare = [&] { throw std::runtime_error("expected standalone preparation failure"); };
		cache.RequestStandaloneShaderClear();
		const auto errorsBefore = logger::errorCount;
		cache.ProcessPendingFeatureClear();
		Require(cache.pendingStandaloneShaderClear && llf.waits == 1 && logger::errorCount == errorsBefore + 1);
		llf.duringPrepare = [&] { order.push_back(2); };
		order.clear();
		cache.ProcessPendingFeatureClear();
		Require(order == std::vector<unsigned>({ 1, 2, 3 }) && !cache.pendingStandaloneShaderClear && llf.waits == 2);
		order.clear();
		unrelated.duringClear = [&] { order.push_back(4); };
		unrelated.duringPrepare = [&] { order.push_back(5); };
		cache.RequestFeatureShaderClear(false);
		cache.RequestStandaloneShaderClear();
		cache.ProcessPendingFeatureClear();
		Require(order == std::vector<unsigned>({ 4, 5, 3 }) && !cache.pendingStandaloneShaderClear && llf.waits == 3);
		llf.loaded = false;
		cache.RequestStandaloneShaderClear();
		cache.ProcessPendingFeatureClear();
		Require(!cache.pendingStandaloneShaderClear && llf.waits == 3);
		llf = {};
		Feature::GetFeatureList().clear();
	}

	void TestWatcherActions()
	{
		SIE::UpdateListener listener;
		using Action = efsw::Actions;
		listener.handleFileAction(1, "Data/Shaders", "First.hlsl", Action::Modified, "");
		listener.handleFileAction(1, "Data/Shaders", "First.hlsl", Action::Modified, "");
		listener.handleFileAction(1, "Data/Shaders", "Second.hlsl", Action::Modified, "");
		listener.handleFileAction(1, "Data/OtherShaders", "Second.hlsl", Action::Modified, "");
		listener.handleFileAction(2, "Data/OtherShaders", "Second.hlsl", Action::Modified, "");
		listener.handleFileAction(2, "Data/OtherShaders", "Second.hlsl", Action::Delete, "");
		listener.handleFileAction(2, "Data/OtherShaders", "Second.hlsl", Action::Moved, "Old.hlsl");
		listener.handleFileAction(2, "Data/OtherShaders", "Second.hlsl", Action::Moved, "Different.hlsl");
		listener.handleFileAction(2, "Data/OtherShaders", "Second.hlsl", Action::Moved, "Different.hlsl");
		listener.handleFileAction(1, "Data/Shaders", "First.hlsl", Action::Modified, "");
		Require(listener.queue.size() == 8 && listener.queue[1].filename == "Second.hlsl" && listener.queue[5].oldFilename == "Old.hlsl");
		ShaderCache cache;
		listener.observedPaths.clear();
		const auto invalidations = SIE::sourceInvalidations;
		listener.Process({ 1, "Data/Shaders", "Archived.bak", Action::Moved, "OldProgram.HLSL" }, &cache);
		Require(listener.observedPaths == std::vector<std::filesystem::path>({ "Data/Shaders/Archived.bak", "Data/Shaders/OldProgram.HLSL" }));
		Require(cache.pendingStandaloneShaderClear && SIE::sourceInvalidations == invalidations + 2);
		cache.pendingStandaloneShaderClear = false;
		listener.Process({ 1, "Data/Shaders", "Unrelated.bak", Action::Modified, "" }, &cache);
		Require(!cache.pendingStandaloneShaderClear);
		listener.Process({ 1, "Data/Shaders", "MissingProgram.hlsl", Action::Delete, "" }, &cache);
		Require(cache.pendingStandaloneShaderClear);
		cache.pendingStandaloneShaderClear = false;
		listener.Process({ 1, "Data/Shaders/Common", "Changed.HLSLI", Action::Modified, "" }, &cache);
		Require(cache.pendingStandaloneShaderClear);
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
		unsigned releases = 0;
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
		for (uint32_t descriptor : { 10u, 11u }) {
			const auto id = ShaderCompilationTask::MakeId(ShaderClass::Pixel, shader.shaderType.get(), descriptor);
			const auto& info = cache.capturedShaders.at(id);
			Require(info.descriptor == descriptor && info.key == key);
			Require(info.diskPath == SIE::SShaderCache::GetDiskPath(shader.fxpFilename, descriptor, ShaderClass::Pixel));
			cache.pixelShaders[RE::BSShader::Lighting][descriptor] = std::make_shared<RuntimeShader>(releases);
		}
		std::thread bulkLoad([&] { cache.TrackActiveShader(ShaderClass::Pixel, shader, 88); });
		bulkLoad.join();
		Require(cache.capturedShaders.size() == 2);
		cache.shaderMap[key] = {};
		const auto compilationGeneration = cache.compilationSet.generation.load();
		FinishWindow(cache, true);
		Require(cache.activeShaderCaptureStage == Stage::AwaitingMenuClose);
		Require(cache.lastScopedClearCount == 2 && releases == 2);
		Require(cache.shaderMap.empty() && cache.clearedBytecodeThisCaptureCycle.size() == 1);
		Require(cache.compilationSet.forgotten.size() == 2 && cache.deletedPaths.size() == 2);
		Require(cache.deletedPaths[0] != cache.deletedPaths[1]);
		Require(cache.pixelShaderEvictionRevision == 2 && cache.vertexShaderEvictionRevision == 0 && cache.computeShaderEvictionRevision == 0);
		Require(cache.compilationSet.generation == compilationGeneration);
		Require(globals::deferred->clears == 1 && loaded.clears == 1 && unloaded.clears == 0);

		cache.shaderMap[key] = {};
		cache.pixelShaders[RE::BSShader::Lighting][10] = std::make_shared<RuntimeShader>(releases);
		cache.pixelShaders[RE::BSShader::Lighting][12] = std::make_shared<RuntimeShader>(releases);
		cache.TickActiveShaderCapture(false);
		Require(cache.activeShaderCaptureStage == Stage::SecondWindow);
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 12);
		FinishWindow(cache, false);
		Require(cache.activeShaderCaptureStage == Stage::Idle);
		Require(cache.lastScopedClearCount == 1 && releases == 3);
		Require(cache.pixelShaderEvictionRevision == 3 && cache.compilationSet.generation == compilationGeneration);
		Require(cache.shaderMap.contains(key) && cache.pixelShaders[RE::BSShader::Lighting].contains(10));
		Require(cache.compilationSet.forgotten.size() == 3 && cache.deletedPaths.size() == 3);
		Require(globals::deferred->clears == 1 && loaded.clears == 1);
		cache.BeginActiveShaderCapture();
		Require(cache.clearedThisCaptureCycle.empty() && cache.clearedBytecodeThisCaptureCycle.empty());
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		FinishWindow(cache, true);
		Require(releases == 4 && cache.shaderMap.empty());
		Require(cache.pixelShaderEvictionRevision == 4 && cache.compilationSet.generation == compilationGeneration);
		Feature::GetFeatureList().clear();
	}

	void TestCaptureFormattingAdmission()
	{
		ShaderCache cache;
		RE::BSShader shader;
		globals::state->developer = false;
		SIE::SShaderCache::shaderStringCalls = 0;
		SIE::SShaderCache::diskPathCalls = 0;
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		Require(SIE::SShaderCache::shaderStringCalls == 0 && SIE::SShaderCache::diskPathCalls == 0);
		cache.BeginActiveShaderCapture();
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		Require(SIE::SShaderCache::shaderStringCalls == 1 && SIE::SShaderCache::diskPathCalls == 1);
		const auto taskId = ShaderCompilationTask::MakeId(ShaderClass::Pixel, shader.shaderType.get(), 10);
		const auto first = cache.capturedShaders.at(taskId);

		// A capture retains its first submission even if live formatting inputs change.
		shader.fxpFilename = "Changed";
		for (unsigned repeat = 0; repeat < 3; ++repeat)
			cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		Require(SIE::SShaderCache::shaderStringCalls == 1 && SIE::SShaderCache::diskPathCalls == 1);
		const auto& unchanged = cache.capturedShaders.at(taskId);
		Require(unchanged.key == first.key && unchanged.diskPath == first.diskPath && unchanged.lastUsed == first.lastUsed);
		Require(unchanged.shaderType == first.shaderType && unchanged.shaderClass == first.shaderClass && unchanged.descriptor == first.descriptor);
		Require(unchanged.drawCalls == 1 && unchanged.isActive);
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 11);
		cache.TrackActiveShader(ShaderClass::Vertex, shader, 10);
		RE::BSShader grass = shader;
		grass.shaderType.value = RE::BSShader::Grass;
		cache.TrackActiveShader(ShaderClass::Pixel, grass, 10);
		Require(cache.capturedShaders.size() == 4 && cache.activeShaders.empty());
		Require(SIE::SShaderCache::shaderStringCalls == 4 && SIE::SShaderCache::diskPathCalls == 4);
		for (const auto& [id, info] : cache.capturedShaders)
			Require(id == ShaderCompilationTask::MakeId(info.shaderClass, info.shaderType, info.descriptor));

		std::thread bulkLoad([&] { cache.TrackActiveShader(ShaderClass::Compute, shader, 12); });
		bulkLoad.join();
		Require(cache.capturedShaders.size() == 4);
		Require(SIE::SShaderCache::shaderStringCalls == 4 && SIE::SShaderCache::diskPathCalls == 4);
		cache.activeShaderCaptureFramesRemaining.store(0);
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 12);
		Require(SIE::SShaderCache::shaderStringCalls == 4 && SIE::SShaderCache::diskPathCalls == 4);
		cache.StartActiveShaderCaptureWindow(Stage::SecondWindow);
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		Require(cache.capturedShaders.size() == 1);
		Require(SIE::SShaderCache::shaderStringCalls == 5 && SIE::SShaderCache::diskPathCalls == 5);
		const auto& restarted = cache.capturedShaders.at(taskId);
		Require(restarted.key != first.key && restarted.diskPath != first.diskPath && restarted.drawCalls == 1);
	}

	void TestDeveloperFormattingAdmission()
	{
		ShaderCache cache;
		RE::BSShader shader;
		globals::state->developer = true;
		SIE::SShaderCache::shaderStringCalls = 0;
		SIE::SShaderCache::diskPathCalls = 0;
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 11);
		Require(SIE::SShaderCache::shaderStringCalls == 2 && SIE::SShaderCache::diskPathCalls == 1);
		Require(cache.activeShaders.size() == 1 && cache.capturedShaders.empty());
		const auto key = cache.activeShaders.begin()->first;
		Require(cache.activeShaders.at(key).descriptor == 10 && cache.activeShaders.at(key).drawCalls == 2);
		cache.BeginActiveShaderCapture();
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		const auto taskId = ShaderCompilationTask::MakeId(ShaderClass::Pixel, shader.shaderType.get(), 10);
		const auto first = cache.capturedShaders.at(taskId);
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		Require(SIE::SShaderCache::shaderStringCalls == 4 && SIE::SShaderCache::diskPathCalls == 2);
		Require(cache.activeShaders.at(key).drawCalls == 4 && cache.capturedShaders.size() == 1);
		Require(cache.capturedShaders.at(taskId).drawCalls == 1 && cache.capturedShaders.at(taskId).lastUsed == first.lastUsed);
		std::thread developerLoad([&] { cache.TrackActiveShader(ShaderClass::Pixel, shader, 11); });
		developerLoad.join();
		Require(SIE::SShaderCache::shaderStringCalls == 5 && SIE::SShaderCache::diskPathCalls == 2);
		Require(cache.activeShaders.at(key).drawCalls == 5 && cache.capturedShaders.size() == 1);
		cache.activeShaderCaptureFramesRemaining.store(0);
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		Require(SIE::SShaderCache::shaderStringCalls == 6 && SIE::SShaderCache::diskPathCalls == 2);
		Require(cache.activeShaders.at(key).drawCalls == 6);
		cache.StartActiveShaderCaptureWindow(Stage::SecondWindow);
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		shader.fxpFilename = "Alternate";
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		Require(SIE::SShaderCache::shaderStringCalls == 8 && SIE::SShaderCache::diskPathCalls == 4);
		Require(cache.activeShaders.size() == 2 && cache.activeShaders.at(key).drawCalls == 7);
		Require(cache.capturedShaders.size() == 1 && cache.capturedShaders.at(taskId).key == key);
		Require(cache.capturedShaders.at(taskId).diskPath == first.diskPath && cache.capturedShaders.at(taskId).drawCalls == 1);
		globals::state->developer = false;
	}

	void TestProtectedVariants()
	{
		for (unsigned protection = 0; protection < 3; ++protection) {
			unsigned releases = 0;
			ShaderCache cache;
			globals::state->developer = false;
			RE::BSShader shader;
			const auto key = SIE::SShaderCache::GetShaderString(ShaderClass::Pixel, shader, 7, true);
			cache.shaderMap[key] = {};
			cache.pixelShaders[RE::BSShader::Lighting][7] = std::make_shared<RuntimeShader>(releases);
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
			Require(cache.pixelShaderEvictionRevision == 0);
			cache.shaderMap[key].status = ShaderCompilationTask::Completed;
			cache.compilationSet.tasksInProgress.clear();
			cache.deferredEvictions.clear();
			cache.TickActiveShaderCapture(false);
			cache.TrackActiveShader(ShaderClass::Pixel, shader, 7);
			FinishWindow(cache, false);
			Require(releases == 1 && cache.shaderMap.empty());
			Require(cache.pixelShaderEvictionRevision == 1);
		}
	}

	void TestCategoryVariantIdentity()
	{
		for (const auto type : { RE::BSShader::Lighting, RE::BSShader::Grass,
				 RE::BSShader::DistantTree, RE::BSShader::Sky, RE::BSShader::Effect }) {
			const auto flag = ShaderCache::GetCharacterCategoryFlag(type);
			Require(flag != 0 && (flag & (flag - 1)) == 0);
			RE::BSShader shader;
			shader.shaderType.value = type;
			const uint32_t baseline = 1u;
			ShaderCompilationTask plain(ShaderClass::Pixel, shader, baseline);
			ShaderCompilationTask categories(ShaderClass::Pixel, shader, baseline | flag);
			Require(plain.GetId() != categories.GetId());
			SIE::CompilationSet queue;
			queue.Add(plain);
			queue.Add(categories);
			queue.Add(plain);
			Require(queue.availableTasks.size() == 2);
		}
		Require(ShaderCache::GetCharacterCategoryFlag(RE::BSShader::ImageSpace) == 0);
	}

	void TestCategoryPrewarm()
	{
		for (unsigned mode = 0; mode < 32; ++mode) {
			Upscaling upscaling;
			upscaling.loaded = (mode & 1) != 0;
			upscaling.settings.neuralRenderingEnabled = (mode & 2) != 0;
			upscaling.settings.neuralCharacterRenderingEnabled = (mode & 4) != 0;
			upscaling.settings.neuralCharacterSceneStrengthsEnabled = (mode & 8) != 0;
			upscaling.settings.neuralCharacterVisualIsolationEnabled = (mode & 16) != 0;
			const bool expected = mode == 23 || mode == 27 || mode == 31;
			Require(upscaling.NeedsCharacterCategoryShaders() == expected);
			for (const auto type : { RE::BSShader::Lighting, RE::BSShader::Grass,
					 RE::BSShader::DistantTree, RE::BSShader::Sky, RE::BSShader::Effect, RE::BSShader::ImageSpace }) {
				RE::BSShader shader;
				shader.shaderType.value = type;
				const auto flag = ShaderCache::GetCharacterCategoryFlag(type);
				for (const bool priorCategoryLayout : { false, true }) {
					ShaderCache cache;
					constexpr uint32_t descriptor = 0x10001u;
					cache.PrewarmDeferredPixelShaders(shader, descriptor | (priorCategoryLayout ? flag : 0),
						upscaling.NeedsCharacterCategoryShaders());
					Require(cache.pixelRequests.size() == (expected && flag ? 2u : 1u));
					Require(cache.pixelRequests.front() == descriptor);
					if (expected && flag)
						Require(cache.pixelRequests.back() == (descriptor | flag));
				}
			}
		}
	}

	void TestStageAndTypeIdentity()
	{
		unsigned releases = 0;
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
		cache.vertexShaders[RE::BSShader::Lighting][4] = std::make_shared<RuntimeShader>(releases);
		cache.pixelShaders[RE::BSShader::Lighting][4] = std::make_shared<RuntimeShader>(releases);
		cache.pixelShaders[RE::BSShader::ImageSpace][4] = std::make_shared<RuntimeShader>(releases);
		cache.computeShaders[RE::BSShader::ImageSpace][4] = std::make_shared<RuntimeShader>(releases);
		FinishWindow(cache, true);
		Require(releases == 4 && cache.compilationSet.forgotten.size() == 4);
		Require(cache.vertexShaderEvictionRevision == 1 && cache.pixelShaderEvictionRevision == 2 && cache.computeShaderEvictionRevision == 1);
		Require(cache.deletedPaths.size() == 3);
	}
}

int main()
{
	InitializeCriticalSection(&Util::rendererLock);
	try {
		TestFeatureClearBoundary();
		TestFeatureClearInvalidationFailure();
		TestStandaloneClearBoundary();
		TestWatcherActions();
		TestQueueWithSharedBytecode();
		TestCaptureAndClearVariants(false);
		TestCaptureAndClearVariants(true);
		TestCaptureFormattingAdmission();
		TestDeveloperFormattingAdmission();
		TestProtectedVariants();
		TestStageAndTypeIdentity();
		TestCategoryVariantIdentity();
		TestCategoryPrewarm();
		DeleteCriticalSection(&Util::rendererLock);
		std::cout << "Shader cache variant checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		DeleteCriticalSection(&Util::rendererLock);
		std::cerr << error.what() << '\n';
		return 1;
	}
}
