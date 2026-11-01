#include "ShaderCompilationSchedulingPolicy.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <shared_mutex>

namespace logger
{
	template <class... Args>
	void info(Args&&...)
	{}
}

// Metadata file writes, cache replacement, and the compiler queue are native
// boundaries. Readiness, state flags, scheduling, and maintenance calls below
// are extracted unchanged from production, including the public declarations.
namespace SIE
{
	std::shared_mutex g_diskCacheMutationMutex;
	unsigned generation = 1;
	void AdvanceDiskCacheGeneration() { ++generation; }
	struct CompilationCounters
	{
		std::atomic_uint64_t totalTasks{}, completedTasks{}, failedTasks{};
	};
	struct ShaderCache
	{
#include "shader_cache_readiness_members.h"
		CompilationCounters compilationSet;
		unsigned infoWrites = 0, commits = 0, clears = 0;
		void WriteDiskCacheInfo()
		{
			assert(!IsCompiling() && IsDiskCacheActive() && !saveLoadDiskPersistenceBlocked);
			++infoWrites;
		}
		void CommitFeatureSetChange()
		{
			assert(!IsCompiling() && IsDiskCache() && featureSetChanged && !saveLoadDiskPersistenceBlocked);
			++commits;
			featureSetChanged = false;
			diskCacheHeld = false;
		}
		void Clear()
		{
			++clears;
			compilationSet.totalTasks = 0;
			compilationSet.completedTasks = 0;
			compilationSet.failedTasks = 0;
		}
	};
}
namespace globals
{
	SIE::ShaderCache* shaderCache = nullptr;
}

#include "shader_cache_readiness_under_test.h"

namespace
{
	void Tick(SIE::ShaderCache& cache)
	{
		globals::shaderCache = &cache;
		FrameMaintenanceForTest();
	}

	void IdleAndCompletedStartup()
	{
		for (const bool alreadyCompleted : { false, true }) {
			SIE::ShaderCache cache;
			cache.compilationSet.totalTasks = alreadyCompleted ? 3 : 0;
			cache.compilationSet.completedTasks = alreadyCompleted ? 2 : 0;
			cache.compilationSet.failedTasks = alreadyCompleted ? 1 : 0;
			Tick(cache);
			assert(cache.infoWrites == 0 && cache.commits == 0);
			cache.TryCompleteStartupCompilationPhase();
			assert(!cache.startupCompilationComplete);
			DataLoadedForTest(&cache);
			assert(cache.menuLoaded && cache.startupCompilationComplete);
			assert(cache.infoWrites == 1 && !cache.diskCacheInfoWritePending);
			Tick(cache);
			assert(cache.infoWrites == 1);
		}
	}

	void BackgroundCompilationAndSaveLoad()
	{
		SIE::ShaderCache cache;
		cache.compilationSet.totalTasks = 3;
		cache.compilationSet.completedTasks = 1;
		DataLoadedForTest(&cache);
		assert(cache.menuLoaded && !cache.startupCompilationComplete);
		assert(cache.diskCacheInfoWritePending && cache.infoWrites == 0);
		Tick(cache);
		assert(cache.infoWrites == 0);
		cache.saveLoadDiskPersistenceBlocked = true;
		cache.compilationSet.completedTasks = 2;
		cache.compilationSet.failedTasks = 1;
		cache.TryCompleteStartupCompilationPhase();
		assert(cache.startupCompilationComplete && cache.infoWrites == 0);
		Tick(cache);
		assert(cache.diskCacheInfoWritePending && cache.infoWrites == 0);
		cache.saveLoadDiskPersistenceBlocked = false;
		Tick(cache);
		assert(!cache.diskCacheInfoWritePending && cache.infoWrites == 1);
		Tick(cache);
		assert(cache.infoWrites == 1);
	}

	void BlockedCachesDoNotPublish()
	{
		for (unsigned condition = 0; condition < 3; ++condition) {
			SIE::ShaderCache cache;
			cache.isDiskCache = condition != 0;
			cache.diskCacheHeld = condition == 1;
			cache.featureSetRevertPending = condition == 2;
			cache.WriteDiskCacheInfoWhenReady();
			assert(!cache.diskCacheInfoWritePending);
			Tick(cache);
			assert(cache.infoWrites == 0 && cache.commits == 0);
		}
		SIE::ShaderCache rollback;
		rollback.featureSetChanged = true;
		rollback.featureSetRevertPending = true;
		rollback.WriteDiskCacheInfoWhenReady();
		assert(!rollback.diskCacheInfoWritePending && rollback.commits == 0);
	}

	void LateStateChangesCancelPendingPublication()
	{
		for (unsigned condition = 0; condition < 3; ++condition) {
			SIE::ShaderCache cache;
			cache.compilationSet.totalTasks = 1;
			cache.WriteDiskCacheInfoWhenReady();
			assert(cache.diskCacheInfoWritePending);
			if (condition == 0) {
				const auto before = SIE::generation;
				cache.SetDiskCache(false);
				assert(SIE::generation == before + 1 && !cache.diskCacheInfoWritePending);
				cache.SetDiskCache(true);
			} else if (condition == 1) {
				cache.diskCacheHeld = true;
			} else {
				cache.featureSetRevertPending = true;
			}
			cache.compilationSet.completedTasks = 1;
			Tick(cache);
			assert(!cache.diskCacheInfoWritePending && cache.infoWrites == 0 && cache.commits == 0);
		}
	}

	void FeatureSetCommitAndPendingClear()
	{
		SIE::ShaderCache cache;
		cache.diskCacheHeld = true;
		cache.featureSetChanged = true;
		cache.compilationSet.totalTasks = 1;
		cache.WriteDiskCacheInfoWhenReady();
		assert(cache.diskCacheInfoWritePending && cache.commits == 0);
		cache.compilationSet.completedTasks = 1;
		cache.saveLoadDiskPersistenceBlocked = true;
		Tick(cache);
		assert(cache.commits == 0 && cache.diskCacheInfoWritePending);
		cache.saveLoadDiskPersistenceBlocked = false;
		Tick(cache);
		assert(cache.commits == 1 && cache.infoWrites == 0 && !cache.diskCacheInfoWritePending);
		Tick(cache);
		assert(cache.commits == 1);

		SIE::ShaderCache cleared;
		cleared.compilationSet.totalTasks = 1;
		cleared.WriteDiskCacheInfoWhenReady();
		cleared.RequestClear();
		Tick(cleared);
		assert(cleared.clears == 1 && cleared.infoWrites == 1 && !cleared.diskCacheInfoWritePending);
	}
}

int main()
{
	IdleAndCompletedStartup();
	BackgroundCompilationAndSaveLoad();
	BlockedCachesDoNotPublish();
	LateStateChangesCancelPendingPublication();
	FeatureSetCommitAndPendingClear();
	std::puts("Shader cache readiness: actual declarations, startup/frame handoffs, deferred publication and terminal gates passed");
}
