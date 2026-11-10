#include "Features/Upscaling/NeuralRendering/CharacterSettings.h"
#include "Utils/ShaderTrackingKeyCache.h"
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
		std::array<D3D_SHADER_MACRO, 64> macros{};
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

	ShaderCompilationTask::ShaderCompilationTask(ShaderClass a_class, const RE::BSShader& a_shader, uint32_t a_descriptor, bool a_precompile) :
		shaderClass(a_class), shader(a_shader), descriptor(a_descriptor), cachedPriority(a_shader.shaderType.get() == RE::BSShader::Lighting ? 600 : 1), precompile(a_precompile)
	{}

	namespace SShaderCache
	{
		unsigned keyBuilds = 0;
		void GetShaderDefines(const RE::BSShader& a_shader, uint32_t, std::span<D3D_SHADER_MACRO> a_defines)
		{
			std::copy(a_shader.macros.begin(), a_shader.macros.end(), a_defines.begin());
		}
		std::string GetShaderString(ShaderClass a_class, const RE::BSShader& a_shader, uint32_t, bool,
			const std::array<D3D_SHADER_MACRO, 64>& a_defines)
		{
			++keyBuilds;
			auto key = a_shader.fxpFilename + std::to_string(static_cast<int>(a_class));
			for (const auto& macro : a_defines) {
				if (!macro.Name)
					break;
				key += ":" + std::string(macro.Name) + "=" + (macro.Definition ? macro.Definition : "");
			}
			return key;
		}
		std::string GetShaderString(ShaderClass a_class, const RE::BSShader& a_shader, uint32_t a_descriptor, bool a_hashKey)
		{
			return GetShaderString(a_class, a_shader, a_descriptor, a_hashKey, a_shader.macros);
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
	constexpr int kHeavyPriorityThreshold = 500;
	// Scheduling, engine and disk boundaries are stubbed; the queue admission,
	// capture declarations, tracking, identity and eviction code comes from source.
	struct CompilationSet
	{
		std::mutex compilationMutex;
		std::condition_variable conditionVariable;
		std::set<ShaderCompilationTask, TaskPriorityLess> availableTasks, tasksInProgress, processedTasks;
		std::atomic<uint64_t> generation{ 9 }, totalTasks{ 0 }, totalPriorityWeight{ 0 };
		std::array<std::atomic<uint64_t>, RE::BSShader::Total> typeEpochs{};
		std::atomic<uint32_t> dispatchedTasksInFlight{ 0 }, heavyTasksInFlight{ 0 };
#include "shader_queue_generation.h"
		std::atomic<int64_t> lastResetQpc{ 0 };
		LARGE_INTEGER lastReset{}, lastCalculation{};
		std::unordered_set<size_t> forgotten;
		void Add(const ShaderCompilationTask& task);
		void DropPrecompilesLocked();
		void ClearTypeLocked(RE::BSShader::Type a_type);
		void ReleaseDispatchSlot(const ShaderCompilationTask& a_task);
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
		std::vector<uint32_t> pixelRequests;
		std::atomic_bool backgroundCompilation{ false };
		void GetPixelShader(const RE::BSShader&, uint32_t descriptor, PixelShaderLookup = PixelShaderLookup::Compile) { pixelRequests.push_back(descriptor); }
		void PrewarmDeferredPixelShaders(const RE::BSShader& shader, uint32_t descriptor, bool categories, bool a_precompile = false);
		struct Blob
		{
			ShaderCompilationTask::Status status = ShaderCompilationTask::Completed;
		};
		std::unordered_map<std::string, Blob> shaderMap;
		std::unordered_set<std::string> deferredEvictions;
		std::mutex mapMutex, vertexShadersMutex, pixelShadersMutex, computeShadersMutex;
		using RuntimeMaps = std::array<std::unordered_map<uint32_t, std::unique_ptr<RuntimeShader>>, RE::BSShader::Total>;
		RuntimeMaps vertexShaders, pixelShaders, computeShaders;
		std::atomic_uint64_t graphicsShaderRevision{ 0 };
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
		uint64_t definesGeneration = 0;
		uint64_t GetShaderDefinesGeneration() const { return definesGeneration; }
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
#include "shader_category_prewarm.h"
#include "shader_eviction_under_test.h"
#include "shader_queue_add_under_test.h"
#include "shader_queue_clear_type.h"
#include "shader_queue_release_slot.h"
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
	NeuralRendering::CharacterSettings GetCharacterNeuralRenderingSettings() const
	{
		NeuralRendering::CharacterSettings policy;
		policy.enabled = settings.neuralCharacterRenderingEnabled;
		policy.sceneStrengthsEnabled = settings.neuralCharacterSceneStrengthsEnabled;
		return policy;
	}
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
	}

	void TestTrackingKeyInputs()
	{
		Util::ShaderTrackingKeyCache keys;
		unsigned builds = 0;
		const auto build = [&] { return std::to_string(++builds); };
		char name[] = "FEATURE_A";
		char value[] = "0";
		std::array<D3D_SHADER_MACRO, 3> macros{ { { name, value }, { nullptr, nullptr }, { "IGNORED", nullptr } } };
		Require(keys.GetOrCreate(1, "Lighting", macros, build) == "1");
		Require(keys.GetOrCreate(1, "Lighting", macros, build) == "1");
		value[0] = '1';
		Require(keys.GetOrCreate(1, "Lighting", macros, build) == "2");
		name[8] = 'B';
		Require(keys.GetOrCreate(1, "Lighting", macros, build) == "3");
		macros[1] = { "ENABLESSR", nullptr };
		macros[2] = { nullptr, nullptr };
		Require(keys.GetOrCreate(1, "Lighting", macros, build) == "4");
		macros[1].Definition = "";
		Require(keys.GetOrCreate(1, "Lighting", macros, build) == "4");
		Require(keys.GetOrCreate(1, "OtherSource", macros, build) == "5");
		Require(keys.GetOrCreate(2, "OtherSource", macros, build) == "6");
		macros[1] = { nullptr, nullptr };
		Require(keys.GetOrCreate(1, "OtherSource", macros, build) == "7");
		keys.Clear();
		Require(keys.GetOrCreate(1, "OtherSource", macros, build) == "8");
	}

	void TestTrackingKeyRetention()
	{
		Util::ShaderTrackingKeyCache keys;
		unsigned builds = 0;
		const auto build = [&] { return std::to_string(++builds); };
		for (std::uint64_t id = 0; id < Util::ShaderTrackingKeyCache::kMaxEntries; ++id)
			keys.GetOrCreate(id, "Lighting", {}, build);
		bool threw = false;
		try {
			keys.GetOrCreate(Util::ShaderTrackingKeyCache::kMaxEntries, "Lighting", {}, []() -> std::string {
				throw std::runtime_error("injected key-build failure");
			});
		} catch (const std::runtime_error&) {
			threw = true;
		}
		Require(threw && keys.GetOrCreate(0, "Lighting", {}, build) == "1");
		const auto last = keys.GetOrCreate(Util::ShaderTrackingKeyCache::kMaxEntries, "Lighting", {}, build);
		Require(builds == Util::ShaderTrackingKeyCache::kMaxEntries + 1);
		keys.GetOrCreate(0, "Lighting", {}, build);
		Require(builds == Util::ShaderTrackingKeyCache::kMaxEntries + 2);
		Require(keys.GetOrCreate(Util::ShaderTrackingKeyCache::kMaxEntries, "Lighting", {}, build) == last);
	}

	void TestTrackingKeyIntegration()
	{
		ShaderCache cache;
		RE::BSShader shader;
		globals::state->developer = false;
		SIE::SShaderCache::keyBuilds = 0;
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		Require(SIE::SShaderCache::keyBuilds == 0 && cache.activeShaders.empty());
		globals::state->developer = true;
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		Require(SIE::SShaderCache::keyBuilds == 1 && cache.activeShaders.size() == 1);
		Require(cache.activeShaders.begin()->second.drawCalls == 2);
		shader.macros[0] = { "ENABLESSR", nullptr };
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		Require(SIE::SShaderCache::keyBuilds == 2 && cache.activeShaders.size() == 2);
		++globals::state->definesGeneration;
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		Require(SIE::SShaderCache::keyBuilds == 3);
		globals::state->developer = false;
		cache.BeginActiveShaderCapture();
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		Require(SIE::SShaderCache::keyBuilds == 3 && cache.capturedShaders.size() == 1);
		std::thread bulkLoad([&] { cache.TrackActiveShader(ShaderClass::Pixel, shader, 99); });
		bulkLoad.join();
		Require(SIE::SShaderCache::keyBuilds == 3 && cache.capturedShaders.size() == 1);
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
		Require(queue.totalPriorityWeight == first.GetPriority() + second.GetPriority() + 2 && cache.bytecodeQueries == 0);
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

	void TestDemandPreservesQueuedWork()
	{
		ShaderCache cache;
		globals::shaderCache = &cache;
		RE::BSShader shader;
		auto& queue = cache.compilationSet;
		ShaderCompilationTask unused(ShaderClass::Pixel, shader, 10, true);
		ShaderCompilationTask demanded(ShaderClass::Pixel, shader, 11, true);
		ShaderCompilationTask running(ShaderClass::Pixel, shader, 12, true);
		queue.Add(unused);
		queue.Add(demanded);
		queue.Add(running);
		const auto original = *queue.availableTasks.find(demanded);
		queue.Add({ ShaderClass::Pixel, shader, 11 });
		const auto promoted = *queue.availableTasks.find(demanded);
		Require(!promoted.IsPrecompile() && promoted.GetGeneration() == original.GetGeneration());
		Require(promoted.GetEnqueuedQpc() == original.GetEnqueuedQpc() && queue.totalTasks == 3);
		queue.tasksInProgress.insert(*queue.availableTasks.find(running));
		queue.availableTasks.erase(running);
		cache.backgroundCompilation = true;
		queue.DropPrecompilesLocked();
		Require(queue.availableTasks.size() == 1 && queue.availableTasks.contains(demanded));
		Require(queue.tasksInProgress.contains(running) && queue.totalTasks == 2);
		Require(queue.totalPriorityWeight == 2 * (demanded.GetPriority() + 1));
		queue.Add(unused);
		Require(queue.totalTasks == 2);
		queue.Add({ ShaderClass::Pixel, shader, 10 });
		Require(queue.totalTasks == 3 && !queue.availableTasks.find(unused)->IsPrecompile());
		cache.backgroundCompilation = false;
		queue.Add({ ShaderClass::Vertex, shader, 13, true });
		Require(queue.totalTasks == 4);
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
		Require(cache.lastScopedClearCount == 2 && releases == 2 && cache.graphicsShaderRevision == 2);
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
		Require(cache.lastScopedClearCount == 1 && releases == 3 && cache.graphicsShaderRevision == 3);
		Require(cache.shaderMap.contains(key) && cache.pixelShaders[RE::BSShader::Lighting].contains(10));
		Require(cache.compilationSet.forgotten.size() == 3 && cache.deletedPaths.size() == 3);
		Require(globals::deferred->clears == 1 && loaded.clears == 1);
		cache.BeginActiveShaderCapture();
		Require(cache.clearedThisCaptureCycle.empty() && cache.clearedBytecodeThisCaptureCycle.empty());
		cache.TrackActiveShader(ShaderClass::Pixel, shader, 10);
		FinishWindow(cache, true);
		Require(releases == 4 && cache.shaderMap.empty() && cache.graphicsShaderRevision == 4);
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

	void TestScopedInvalidation()
	{
		RE::BSShader lighting, imageSpace;
		imageSpace.shaderType.value = RE::BSShader::ImageSpace;
		SIE::CompilationSet queue;
		ShaderCompilationTask lightingRequest(ShaderClass::Pixel, lighting, 1);
		ShaderCompilationTask lightingQueued(ShaderClass::Vertex, lighting, 2);
		ShaderCompilationTask imageRequest(ShaderClass::Pixel, imageSpace, 3);
		ShaderCompilationTask imageQueued(ShaderClass::Vertex, imageSpace, 4);
		ShaderCompilationTask imageFinished(ShaderClass::Pixel, imageSpace, 5);
		for (const auto& task : { lightingRequest, lightingQueued, imageRequest, imageQueued, imageFinished })
			queue.Add(task);
		auto dispatch = [&](const ShaderCompilationTask& task) {
			auto entry = queue.availableTasks.extract(task);
			auto dispatched = entry.value();
			queue.tasksInProgress.insert(std::move(entry));
			++queue.dispatchedTasksInFlight;
			if (dispatched.GetPriority() >= SIE::kHeavyPriorityThreshold)
				++queue.heavyTasksInFlight;
			return dispatched;
		};
		const auto lightingRunning = dispatch(lightingRequest);
		const auto imageRunning = dispatch(imageRequest);
		queue.processedTasks.insert(queue.availableTasks.extract(imageFinished));
		const auto standaloneGeneration = queue.generation.load();
		{
			std::lock_guard lock(queue.compilationMutex);
			queue.ClearTypeLocked(RE::BSShader::ImageSpace);
		}
		Require(queue.availableTasks.size() == 1 && queue.availableTasks.contains(lightingQueued));
		Require(queue.tasksInProgress.size() == 1 && queue.tasksInProgress.contains(lightingRunning));
		Require(queue.processedTasks.empty());
		// Finished work stays in cumulative progress; only canceled unfinished work is removed.
		Require(queue.totalTasks == 3 && queue.totalPriorityWeight == 2 * (lightingRunning.GetPriority() + 1) + imageFinished.GetPriority() + 1);
		Require(queue.dispatchedTasksInFlight == 2 && queue.heavyTasksInFlight == 1);
		Require(queue.generation == standaloneGeneration);
		Require(queue.IsCurrentGeneration(lightingRunning) && !queue.IsCurrentGeneration(imageRunning));
		queue.Add(imageRunning);
		const auto replacement = *queue.availableTasks.find(imageRunning);
		Require(queue.IsCurrentGeneration(replacement));
		Require(replacement.GetGeneration() != imageRunning.GetGeneration());
		queue.ReleaseDispatchSlot(imageRunning);
		Require(queue.tasksInProgress.contains(lightingRunning) && queue.availableTasks.contains(replacement));
		Require(queue.dispatchedTasksInFlight == 1 && queue.heavyTasksInFlight == 1);
		queue.ReleaseDispatchSlot(lightingRunning);
		Require(queue.dispatchedTasksInFlight == 0 && queue.heavyTasksInFlight == 0);
		++queue.generation;
		Require(!queue.IsCurrentGeneration(lightingRunning) && !queue.IsCurrentGeneration(replacement));
		Require(queue.generation != standaloneGeneration);
	}

	void TestUntruncatedTypeEpoch()
	{
		SIE::CompilationSet queue;
		queue.typeEpochs[RE::BSShader::ImageSpace] = (uint64_t{ 1 } << 24) - 1;
		const auto previous = queue.LiveGeneration(RE::BSShader::ImageSpace);
		const auto other = queue.LiveGeneration(RE::BSShader::Lighting);
		{
			std::lock_guard lock(queue.compilationMutex);
			queue.ClearTypeLocked(RE::BSShader::ImageSpace);
		}
		Require(queue.LiveGeneration(RE::BSShader::ImageSpace) == previous + 1);
		Require(queue.LiveGeneration(RE::BSShader::Lighting) == other);
		Require(queue.totalTasks == 0 && queue.totalPriorityWeight == 0);
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
		TestTrackingKeyInputs();
		TestTrackingKeyRetention();
		TestTrackingKeyIntegration();
		TestQueueWithSharedBytecode();
		TestDemandPreservesQueuedWork();
		TestCaptureAndClearVariants(false);
		TestCaptureAndClearVariants(true);
		TestProtectedVariants();
		TestStageAndTypeIdentity();
		TestScopedInvalidation();
		TestUntruncatedTypeEpoch();
		TestCategoryVariantIdentity();
		TestCategoryPrewarm();
		std::cout << "Shader cache variant checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
