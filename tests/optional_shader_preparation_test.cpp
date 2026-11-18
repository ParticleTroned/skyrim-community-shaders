#include "ShaderCompilationSchedulingPolicy.h"
#include "Utils/ShaderSpecializationRequests.h"

#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace RE
{
	class BSShader;
}
namespace SKSE::stl
{
	template <class F>
	struct scope_exit
	{
		F action;
		~scope_exit() { action(); }
	};
}
namespace Util
{
	bool SetCurrentThreadCooperativeBackgroundPriority() { return true; }
}
namespace logger
{
	bool fail = false;
	template <class... T>
	void error(T&&...)
	{
		if (fail)
			throw std::runtime_error("logger failure");
	}
}
struct DeferredFixture
{
	void PrewarmDiscoveredComposites() {}
};
namespace globals
{
	DeferredFixture* deferred = nullptr;
}
namespace SIE
{
	struct CompilationSet
	{
		mutable std::mutex compilationMutex;
		std::condition_variable_any conditionVariable;
		std::atomic_uint32_t dispatchedTasksInFlight{ 0 };
		std::set<int> availableTasks;
	};
	struct ShaderCache
	{
		using Mode = ShaderCompilationSchedulingPolicy::PreparationMode;
		enum class OptionalStage
		{
			Cache,
			Source,
			Device
		};
		std::atomic<Mode> preparationMode{ Mode::StartupOnly };
		std::atomic_bool startupCompilationComplete{ false }, menuLoaded{ false }, backgroundCompilation{ false };
		std::atomic_uint32_t pendingCompositePreparations{ 0 };
		CompilationSet compilationSet;
		int compilationThreadCount = 4, backgroundCompilationThreadCount = 2;
		struct Pool
		{
			std::vector<std::function<void()>> tasks;
			bool fail = false;
			size_t get_thread_count() const { return 4; }
			void detach_task(std::function<void()> task)
			{
				if (fail)
					throw std::runtime_error("queue failure");
				tasks.push_back(std::move(task));
			}
			void Run()
			{
				auto work = std::move(tasks);
				tasks.clear();
				for (auto& task : work) task();
			}
		} compilationPool;
		uint64_t testRevision = 1, inputGeneration = 1;
		mutable uint64_t revisionReads = 0;
		uint64_t GetGraphicsShaderRevision() const
		{
			++revisionReads;
			return testRevision;
		}
		uint64_t GetShaderPreparationInputGeneration() const { return inputGeneration; }
		Mode GetPreparationMode() const { return preparationMode.load(); }
		bool IsBlockingShaderStartup() const;
		void SetPreparationMode(Mode);
		bool TryAdmitOptionalShaderStage(OptionalStage);
		void SetShaderPreparationTransition(uint8_t, bool);
		void SetShaderPreparationMainMenu(bool);
		using OptionalPreparationIdentity = ShaderCompilationSchedulingPolicy::PreparationIdentity;
		OptionalPreparationIdentity CaptureOptionalPreparationIdentity(uint64_t = 0) const;
		bool CanQueueOptionalShaderPreparation(uint64_t, OptionalPreparationIdentity);
		bool QueueOptionalShaderPreparation(uint64_t, OptionalPreparationIdentity, std::function<bool()>, std::function<void()>);
		bool Queue(uint64_t key, std::function<bool()> run, std::function<void()> publish, uint64_t owner = 0,
			std::optional<uint64_t> revision = std::nullopt, std::optional<uint64_t> inputs = std::nullopt)
		{
			auto identity = CaptureOptionalPreparationIdentity(owner);
			if (revision)
				identity.revision = *revision;
			if (inputs)
				identity.inputs = *inputs;
			return QueueOptionalShaderPreparation(key, identity, std::move(run), std::move(publish));
		}
		bool CanQueue(uint64_t key, uint64_t owner = 0) { return CanQueueOptionalShaderPreparation(key, CaptureOptionalPreparationIdentity(owner)); }

		void TickOptionalShaderPreparation(bool, bool, bool, uint64_t);
		uint64_t GetShaderPreparationWindowGeneration();
		void BeginCompositePreparation() { ++pendingCompositePreparations; }
		void TryCompleteStartupCompilationPhase() {}
#include "optional_members.h"
		bool Dispatch()
		{
			std::lock_guard lock(compilationSet.compilationMutex);
			return DispatchOptionalPreparationLocked();
		}
		void Tick(bool loading = false, bool safe = false)
		{
			TickOptionalShaderPreparation(loading, false, safe, GetShaderPreparationWindowGeneration());
		}
	};
#include "optional_phase.h"
#include "optional_scheduler.h"
}

int main()
{
	using Cache = SIE::ShaderCache;
	using Mode = Cache::Mode;
	using Stage = Cache::OptionalStage;
	Cache cache;
	assert(cache.TryAdmitOptionalShaderStage(Stage::Cache));
	cache.backgroundCompilation = true;
	assert(!cache.TryAdmitOptionalShaderStage(Stage::Source));
	cache.menuLoaded = true;
	cache.SetPreparationMode(Mode::Gameplay);
	assert(!cache.TryAdmitOptionalShaderStage(Stage::Device));
	cache.Tick();
	cache.SetPreparationMode(Mode::StartupOnly);
	for (auto stage : { Stage::Cache, Stage::Source, Stage::Device })
		assert(!cache.TryAdmitOptionalShaderStage(stage));
	cache.SetPreparationMode(Mode::Transitions);
	cache.SetShaderPreparationTransition(1, true);
	assert(cache.TryAdmitOptionalShaderStage(Stage::Cache));
	cache.SetShaderPreparationTransition(1, false);
	assert(!cache.TryAdmitOptionalShaderStage(Stage::Device));
	// An older physical observation cannot clear a newer loading generation.
	const auto observed = cache.GetShaderPreparationWindowGeneration();
	cache.SetShaderPreparationTransition(1, true);
	cache.TickOptionalShaderPreparation(false, false, false, observed);
	assert(cache.TryAdmitOptionalShaderStage(Stage::Cache));
	cache.Tick(false, true);
	assert(!cache.TryAdmitOptionalShaderStage(Stage::Cache));
	// Independent sleep/wait intervals survive a loading close; selection menus
	// have no admission path. Stop closes the matching active interval.
	cache.SetShaderPreparationTransition(2, true);
	cache.SetShaderPreparationTransition(4, true);
	cache.SetShaderPreparationTransition(2, false);
	assert(cache.TryAdmitOptionalShaderStage(Stage::Source));
	cache.SetShaderPreparationTransition(4, false);
	assert(!cache.TryAdmitOptionalShaderStage(Stage::Source));

	unsigned compiles = 0, creates = 0, published = 0;
	bool bytecode = false;
	assert(cache.Queue(1, [&] {
		if (!bytecode) {
			if (!cache.TryAdmitOptionalShaderStage(Stage::Source)) return false;
			++compiles;
			bytecode = true;
			cache.SetShaderPreparationTransition(1, false);
		}
		if (!cache.TryAdmitOptionalShaderStage(Stage::Device)) return false;
		++creates;
		return true; }, [&] { ++published; }));
	assert(!cache.Queue(1, [] { return true; }, {}));
	assert(!cache.Dispatch());
	cache.SetShaderPreparationTransition(1, true);
	assert(cache.Dispatch());
	cache.compilationPool.Run();
	assert(compiles == 1 && creates == 0 && published == 0);
	assert(cache.optionalInFlight == 0 && cache.compilationSet.dispatchedTasksInFlight == 0);
	assert(!cache.Dispatch());
	cache.SetPreparationMode(Mode::Gameplay);
	assert(cache.Dispatch());
	cache.compilationPool.Run();
	cache.Tick(false, true);
	assert(compiles == 1 && creates == 1 && published == 0);
	cache.Tick();
	assert(published == 1 && !cache.Dispatch());

	// Completed results stay dormant in lifecycle protection and publish only
	// within the configured owner budget once the guard closes.
	Cache bounded;
	bounded.backgroundCompilation = true;
	bounded.menuLoaded = true;
	bounded.SetPreparationMode(Mode::Gameplay);
	bounded.Tick();
	unsigned boundedPublications = 0;
	for (uint64_t key : { 1, 2, 3, 4, 5 })
		assert(bounded.Queue(key, [] { return true; }, [&] { ++boundedPublications; }));
	while (bounded.Dispatch())
		bounded.compilationPool.Run();
	bounded.Tick(false, true);
	assert(boundedPublications == 0);
	bounded.Tick();
	assert(boundedPublications == 2);
	bounded.Tick();
	assert(boundedPublications == 4);
	bounded.Tick();
	assert(boundedPublications == 5);

	// Lighting, Effect and Composite share the same configured and total limits.
	for (uint64_t key : { 2, 3, 4 })
		assert(cache.Queue(key, [] { return true; }, {}));
	cache.compilationSet.availableTasks.insert(7);
	assert(!cache.Dispatch());
	cache.compilationSet.availableTasks.clear();
	cache.compilationSet.dispatchedTasksInFlight = 2;
	assert(!cache.Dispatch());
	cache.compilationSet.dispatchedTasksInFlight = 0;
	assert(cache.Dispatch() && cache.Dispatch() && !cache.Dispatch());
	cache.compilationPool.Run();
	assert(cache.Dispatch());
	cache.compilationPool.Run();
	assert(!cache.Dispatch());

	logger::fail = true;
	assert(cache.Queue(6, []() -> bool { throw std::runtime_error("compiler failure"); }, {}));
	assert(cache.Dispatch());
	cache.compilationPool.Run();
	assert(cache.optionalInFlight == 0 && cache.compilationSet.dispatchedTasksInFlight == 0 && !cache.Dispatch());
	logger::fail = false;
	cache.compilationPool.fail = true;
	assert(cache.Queue(5, [] { assert(false); return true; }, {}));
	assert(cache.Dispatch());
	assert(cache.optionalInFlight == 0 && cache.compilationSet.dispatchedTasksInFlight == 0);
	assert(!cache.Dispatch());
	cache.compilationPool.fail = false;
	assert(cache.CanQueue(1, 1));
	assert(cache.Queue(1, [] { return true; }, {}, 1));
	assert(cache.Dispatch());
	cache.compilationPool.Run();
	++cache.inputGeneration;
	assert(cache.CanQueue(1));
	cache.Tick();
	assert(cache.optionalPreparations.empty());
	assert(!cache.Queue(1, [] { return true; }, {}, 0, cache.testRevision, cache.inputGeneration - 1));
	Cache startup;
	assert(startup.Queue(1, [] { return true; }, {}));
	assert(startup.pendingCompositePreparations == 1 && startup.Dispatch());
	startup.compilationPool.Run();
	assert(startup.pendingCompositePreparations == 0);
	cache.SetShaderPreparationMainMenu(true);
	cache.SetShaderPreparationMainMenu(false);
	cache.SetShaderPreparationTransition(1, true);
	assert(!cache.TryAdmitOptionalShaderStage(Stage::Cache));
	cache.Tick(true, true);
	assert(!cache.TryAdmitOptionalShaderStage(Stage::Cache));
	cache.Tick();
	assert(cache.TryAdmitOptionalShaderStage(Stage::Cache));
	for (uint64_t key = 0; key < Util::ShaderSpecializationRequests::kMaximumRequests; ++key)
		assert(cache.Queue(key, [] { return true; }, {}));
	assert(!cache.CanQueue(UINT64_MAX));
	assert(!cache.Queue(UINT64_MAX, [] { return true; }, {}));
	cache.Tick();
	cache.revisionReads = 0;
	for (unsigned frame = 0; frame < 100; ++frame)
		cache.Tick();
	assert(cache.revisionReads < 10000);

	++cache.testRevision;
	cache.Tick();
	assert(cache.optionalPreparations.empty());

	// Published close is ordered against every ready worker's admission check.
	cache.SetPreparationMode(Mode::Transitions);
	cache.SetShaderPreparationTransition(1, true);
	std::atomic_bool closed{ false };
	std::thread worker([&] {
		while (!closed.load(std::memory_order_acquire))
			(void)cache.TryAdmitOptionalShaderStage(Stage::Source);
		for (unsigned i = 0; i < 1000; ++i)
			assert(!cache.TryAdmitOptionalShaderStage(Stage::Device));
	});
	cache.SetShaderPreparationTransition(1, false);
	closed.store(true, std::memory_order_release);
	worker.join();
}
