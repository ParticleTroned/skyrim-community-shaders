#include "ShaderCompilationSchedulingPolicy.h"

#include <Windows.h>
#include <atomic>
#include <bit>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <iostream>
#include <iterator>
#include <magic_enum/magic_enum.hpp>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

struct ID3D10Blob
{
	unsigned AddRef() { return 1; }
	unsigned Release() { return 1; }
};
using ID3DBlob = ID3D10Blob;
#include "Utils/ShaderCreation.h"

namespace
{
	using Phase = ShaderCompilationSchedulingPolicy::CompilationPhase;
	std::vector<Phase> priorityPhases;
	bool priorityAvailable = true;
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	class ObservedConditionVariable
	{
	public:
		template <class Lock, class Predicate>
		bool wait(Lock& lock, std::stop_token stop, Predicate predicate)
		{
			return condition.wait(lock, stop, [&] {
				const bool ready = predicate();
				if (!ready) {
					{
						std::lock_guard observedLock(observedMutex);
						++rejectedChecks;
					}
					observedCondition.notify_all();
				}
				return ready;
			});
		}
		void notify_one() { condition.notify_one(); }
		unsigned RejectedChecks()
		{
			std::lock_guard lock(observedMutex);
			return rejectedChecks;
		}
		void WaitForRejectedCheck(unsigned previous)
		{
			std::unique_lock lock(observedMutex);
			observedCondition.wait(lock, [&] { return rejectedChecks > previous; });
		}

	private:
		std::condition_variable_any condition;
		std::condition_variable observedCondition;
		std::mutex observedMutex;
		unsigned rejectedChecks = 0;
	};
}

namespace Util
{
	bool SetCurrentThreadShaderCompilationPriority(Phase phase)
	{
		priorityPhases.push_back(phase);
		return priorityAvailable;
	}
}

namespace logger
{
	template <class... Args>
	void debug(const char*, Args&&...)
	{}
	template <class... Args>
	void info(const char*, Args&&...)
	{}
	template <class... Args>
	void warn(const char*, Args&&...)
	{}
	template <class... Args>
	void error(const char*, Args&&...)
	{}
}

namespace SKSE::stl
{
	template <class Callback>
	struct scope_exit
	{
		Callback callback;
		~scope_exit() { callback(); }
	};
}

namespace SIE
{
	struct ShaderCompilationTask
	{
		size_t id = 0;
		int priority = 0;
		uint64_t generation = 0;
		int64_t enqueuedQpc = 0;
		std::function<void()> work;
		size_t requiredPriorityCalls = 0;
		size_t GetId() const { return id; }
		int GetPriority() const { return priority; }
		uint64_t GetGeneration() const { return generation; }
		void SetGeneration(uint64_t value) { generation = value; }
		int64_t GetEnqueuedQpc() const { return enqueuedQpc; }
		void SetEnqueuedQpc(int64_t value) { enqueuedQpc = value; }
		std::string GetString() const
		{
			Require(requiredPriorityCalls == 0 || priorityPhases.size() == requiredPriorityCalls,
				"Task-key allocation must occur after the worker applies its current priority");
			return std::to_string(id);
		}
		std::filesystem::path GetSourcePath() const { return {}; }
		void Perform(Util::ShaderCreation::Attempt& attempt) const
		{
			if (work)
				work();
			attempt.result = Util::ShaderCreation::Result::Published;
		}
	};
}

#include "shader_compilation_comparison_under_test.h"

namespace SIE
{
	constexpr int kHeavyPriorityThreshold = 500;
	struct ShaderCache;
	struct CompilationSet
	{
		struct SlowTaskRecord
		{
			std::string key;
			double elapsedMs, queueWaitMs;
			int priority, defineCount;
			uintmax_t sourceSizeBytes;
			DWORD threadId;
			int64_t startQpc;
		};
		std::atomic<uint64_t> generation{ 1 }, totalTasks{}, completedTasks{}, failedTasks{}, totalPriorityWeight{}, slowTasks{}, verySlowTasks{};
		std::atomic<uint32_t> dispatchedTasksInFlight{}, heavyTasksInFlight{};
		std::atomic<int64_t> lastResetQpc{};
		LARGE_INTEGER lastReset{}, lastCalculation{};
		std::mutex compilationMutex, slowTasksMutex;
		ObservedConditionVariable conditionVariable;
		std::set<ShaderCompilationTask, TaskPriorityLess> availableTasks, tasksInProgress, processedTasks;
		std::vector<SlowTaskRecord> slowTaskRecords;
		ShaderCache* owner = nullptr;
		std::atomic<int64_t> completionTime{};
		std::atomic<uint64_t> diskHitTasks{}, diskHitPriorityWeight{}, completedPriorityWeight{}, contentDedupeTasks{}, activeReuseTasks{}, previousReuseTasks{};
		std::atomic<bool> compilationPhaseStarted{};
		LARGE_INTEGER totalTime{}, frequency{};
		CompilationSet()
		{
			QueryPerformanceFrequency(&frequency);
			QueryPerformanceCounter(&lastCalculation);
		}
		static std::string GetHumanTime(double) { return "test"; }
		std::optional<ShaderCompilationTask> WaitTake(std::stop_token stop);
		void Add(const ShaderCompilationTask& task);
		void ReleaseDispatchSlot();
		void Complete(const ShaderCompilationTask& task, Util::ShaderCreation::Result result);
		bool IsCurrentGeneration(const ShaderCompilationTask& task) const { return task.GetGeneration() == generation.load(); }
	};
	struct ShaderCache
	{
		CompilationSet compilationSet;
		std::atomic_bool startupCompilationComplete{}, menuLoaded{}, backgroundCompilation{};
		std::atomic_size_t standalonePreparations{};
		int32_t compilationThreadCount = 3, backgroundCompilationThreadCount = 1;
		unsigned resolvedFailures = 0, retiredTasks = 0;
		bool taskFailed = false;
		ShaderCache() { compilationSet.owner = this; }
		static ShaderCache& Instance();
		bool IsShaderLoadedFromDisk(const std::string&) { return false; }
		bool IsCompiling();
		void TryCompleteStartupCompilationPhase();
		bool SetBackgroundCompilation(bool value);
		void ProcessCompilationSet(std::stop_token stop, ShaderCompilationTask task);
		bool IsTaskStale(uint64_t generation) const { return generation != compilationSet.generation.load(); }
		void ResolvePendingFailure(const std::string&, uint64_t)
		{
			++resolvedFailures;
			taskFailed = true;
		}
		void ApplyDeferredEviction(const std::string&, bool completed)
		{
			Require(completed, "A finished worker must retire its pending eviction");
			++retiredTasks;
		}
	};
}

namespace globals
{
	SIE::ShaderCache* shaderCache = nullptr;
}

namespace SIE
{
#include "shader_compilation_admission_under_test.h"
#include "shader_compilation_mode_under_test.h"
#include "shader_compilation_phase_under_test.h"
#include "shader_compilation_worker_under_test.h"

	ShaderCache& ShaderCache::Instance() { return *globals::shaderCache; }
	struct CompileDedupe
	{
		uint64_t RetainedBytes() const { return 0; }
		uint64_t DroppedBlobs() const { return 0; }
		void Clear() {}
	};
	CompileDedupe& GetCompileDedupe()
	{
		static CompileDedupe value;
		return value;
	}
	void FlushShaderCacheManifest() {}
#include "shader_compilation_completion_under_test.h"
}

namespace
{
	using SIE::ShaderCache;
	struct CacheBinding
	{
		ShaderCache cache;
		CacheBinding()
		{
			globals::shaderCache = &cache;
			priorityAvailable = true;
			priorityPhases.clear();
		}
		~CacheBinding() { globals::shaderCache = nullptr; }
	};
	void Queue(ShaderCache& cache, size_t id, int priority = 1, std::function<void()> work = {})
	{
		cache.compilationSet.Add({ id, priority, 0, 0, std::move(work) });
	}
	SIE::ShaderCompilationTask Take(ShaderCache& cache)
	{
		auto task = cache.compilationSet.WaitTake({});
		Require(task.has_value(), "Queued work below its admission limit must dispatch");
		return *task;
	}

	void CheckWorkerPriorityAndProgress()
	{
		CacheBinding binding;
		auto& cache = binding.cache;
		unsigned performed = 0;
		for (const auto phase : { Phase::Startup, Phase::InGame }) {
			cache.startupCompilationComplete = phase == Phase::InGame;
			for (const bool applied : { true, false }) {
				priorityAvailable = applied;
				const auto before = performed;
				Queue(cache, before + 1, 1, [&] {
					Require(priorityPhases.size() == before + 1 && priorityPhases.back() == phase,
						"Compilation must run after the worker applies its current phase priority");
					++performed;
				});
				auto task = Take(cache);
				task.requiredPriorityCalls = priorityPhases.size() + 1;
				cache.ProcessCompilationSet({}, task);
				Require(priorityPhases.back() == phase && performed == before + 1,
					"The actual worker must select its current phase and compile despite failed priority application");
				Require(cache.compilationSet.dispatchedTasksInFlight == 0 && cache.compilationSet.completedTasks == performed,
					"Priority application must preserve task completion and release every admitted slot");
			}
		}
		Require(priorityPhases.size() == 4 && cache.retiredTasks == 4,
			"Reused workers must reapply phase priority and retire each completed task");
		Require(cache.compilationThreadCount == 3 && cache.backgroundCompilationThreadCount == 1,
			"Priority changes must not alter either configured worker budget");
	}

	void CheckTerminalAdmissionRecovery()
	{
		for (unsigned condition = 0; condition < 6; ++condition) {
			CacheBinding binding;
			auto& cache = binding.cache;
			cache.compilationThreadCount = 1;
			std::stop_source stop;
			unsigned performed = 0;
			Queue(cache, 1, 1, [&] {
				++performed;
				if (condition == 2)
					throw std::runtime_error("Injected compiler failure");
				if (condition == 3)
					throw 1;
				if (condition == 4)
					stop.request_stop();
				if (condition == 5)
					++cache.compilationSet.generation;
			});
			auto first = Take(cache);
			Queue(cache, 2);
			if (condition == 0)
				stop.request_stop();
			if (condition == 1)
				++cache.compilationSet.generation;
			const auto rejected = cache.compilationSet.conditionVariable.RejectedChecks();
			std::optional<SIE::ShaderCompilationTask> next;
			std::jthread waiter([&](std::stop_token token) { next = cache.compilationSet.WaitTake(token); });
			cache.compilationSet.conditionVariable.WaitForRejectedCheck(rejected);
			cache.ProcessCompilationSet(stop.get_token(), first);
			waiter.join();
			Require(next && next->GetId() == 2 && cache.compilationSet.dispatchedTasksInFlight == 1,
				"Every terminal worker path must release its slot and wake queued work");
			Require(performed == (condition < 2 ? 0u : 1u),
				"Stopped or stale work must terminate before priority application and compilation");
			Require(priorityPhases.size() == (condition < 2 ? 0u : 1u),
				"Only a current running task may apply worker priority");
			Require(cache.resolvedFailures == (condition == 2 || condition == 3 ? 1u : 0u),
				"Standard and unknown compiler exceptions must resolve pending failures");
			Require(cache.compilationSet.completedTasks + cache.compilationSet.failedTasks == (condition == 2 || condition == 3 ? 1u : 0u),
				"Stopped tasks must release admission independently of completion bookkeeping");
			cache.compilationSet.ReleaseDispatchSlot();
			Require(cache.compilationSet.dispatchedTasksInFlight == 0,
				"Recovered dispatch admission must return to zero without underflow");
		}
	}

	void CheckPublicationCompletion()
	{
		using Util::ShaderCreation::Result;
		for (auto result : { Result::Published, Result::CompileFailed, Result::ReflectionFailed, Result::CreationFailed }) {
			CacheBinding binding;
			auto& cache = binding.cache;
			Queue(cache, 1);
			auto task = Take(cache);
			cache.compilationSet.Complete(task, result);
			cache.compilationSet.ReleaseDispatchSlot();
			Require(cache.compilationSet.completedTasks == (result == Result::Published ? 1u : 0u), "Only a published runtime wrapper completes successfully");
			Require(cache.compilationSet.failedTasks == (result == Result::Published ? 0u : 1u), "Bytecode alone cannot turn runtime failure into success");
			Queue(cache, 1);
			Require(cache.compilationSet.availableTasks.empty(), "Terminal work cannot restart from every draw");
		}
	}

	void CheckBudgetTransitions()
	{
		CacheBinding binding;
		auto& cache = binding.cache;
		for (size_t id = 1; id <= 5; ++id)
			Queue(cache, id, static_cast<int>(id));
		Require(Take(cache).GetId() == 5 && Take(cache).GetId() == 4 && Take(cache).GetId() == 3,
			"Admission must preserve the production heavy-first queue order");
		std::optional<SIE::ShaderCompilationTask> next;
		auto rejected = cache.compilationSet.conditionVariable.RejectedChecks();
		std::jthread waiter([&](std::stop_token token) { next = cache.compilationSet.WaitTake(token); });
		cache.compilationSet.conditionVariable.WaitForRejectedCheck(rejected);
		rejected = cache.compilationSet.conditionVariable.RejectedChecks();
		Require(!cache.SetBackgroundCompilation(true), "Switching to background must return the prior mode");
		cache.compilationSet.conditionVariable.WaitForRejectedCheck(rejected);
		for (unsigned remaining = 2; remaining > 0; --remaining) {
			rejected = cache.compilationSet.conditionVariable.RejectedChecks();
			cache.compilationSet.ReleaseDispatchSlot();
			cache.compilationSet.conditionVariable.WaitForRejectedCheck(rejected);
			Require(cache.compilationSet.dispatchedTasksInFlight == remaining,
				"Reducing the budget must wait until existing admission is strictly below the new cap");
		}
		cache.compilationSet.ReleaseDispatchSlot();
		waiter.join();
		Require(next && next->GetId() == 2 && cache.compilationSet.dispatchedTasksInFlight == 1,
			"Background dispatch must resume only after admission drops below its cap");
		next.reset();
		rejected = cache.compilationSet.conditionVariable.RejectedChecks();
		std::jthread increased([&](std::stop_token token) { next = cache.compilationSet.WaitTake(token); });
		cache.compilationSet.conditionVariable.WaitForRejectedCheck(rejected);
		Require(cache.SetBackgroundCompilation(false), "Returning to foreground must return the prior mode");
		increased.join();
		Require(next && next->GetId() == 1 && cache.compilationSet.dispatchedTasksInFlight == 2,
			"Increasing the budget must wake dispatch without waiting for a task completion");
		cache.compilationSet.ReleaseDispatchSlot();
		cache.compilationSet.ReleaseDispatchSlot();
		next.reset();
		rejected = cache.compilationSet.conditionVariable.RejectedChecks();
		std::jthread stopped([&](std::stop_token token) { next = cache.compilationSet.WaitTake(token); });
		cache.compilationSet.conditionVariable.WaitForRejectedCheck(rejected);
		stopped.request_stop();
		stopped.join();
		Require(!next && cache.compilationSet.dispatchedTasksInFlight == 0,
			"A stopped dispatcher must return without admitting an empty queue");
	}

	void CheckStartupPhaseBoundary()
	{
		CacheBinding binding;
		auto& cache = binding.cache;
		cache.TryCompleteStartupCompilationPhase();
		Require(!cache.startupCompilationComplete, "An idle queue before DataLoaded must remain in startup");
		cache.menuLoaded = true;
		cache.compilationSet.totalTasks = 1;
		cache.TryCompleteStartupCompilationPhase();
		Require(!cache.startupCompilationComplete, "Active engine work must retain startup scheduling");
		cache.SetBackgroundCompilation(true);
		cache.TryCompleteStartupCompilationPhase();
		Require(!cache.startupCompilationComplete, "Background boot admission must not complete the startup phase");
		cache.compilationSet.completedTasks = 1;
		cache.standalonePreparations = 1;
		cache.TryCompleteStartupCompilationPhase();
		Require(!cache.startupCompilationComplete, "Standalone producers must retain startup after engine work drains");
		cache.standalonePreparations = 0;
		cache.TryCompleteStartupCompilationPhase();
		Require(cache.startupCompilationComplete, "The phase must complete after DataLoaded and both producers drain");
		cache.compilationSet.totalTasks = 2;
		cache.TryCompleteStartupCompilationPhase();
		Require(cache.startupCompilationComplete, "Later recompilation must preserve the completed startup phase");
	}
}

int main()
{
	try {
		CheckWorkerPriorityAndProgress();
		CheckTerminalAdmissionRecovery();
		CheckPublicationCompletion();
		CheckBudgetTransitions();
		CheckStartupPhaseBoundary();
		std::cout << "Shader compilation scheduling: phase priority, progress, admission recovery and budget wakeups passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
