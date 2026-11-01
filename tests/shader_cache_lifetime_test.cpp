#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <unordered_map>

namespace
{
	std::atomic_int liveStores{}, liveManifests{}, manifestLoads{}, failures{}, writerObservations{}, flushObservations{};

	void ObserveStores(const char* boundary)
	{
		if (liveStores.load() != 2) {
			std::fprintf(stderr, "managed stores destroyed before %s\n", boundary);
			++failures;
		}
		if (liveManifests.load() != 1) {
			std::fprintf(stderr, "loose manifest destroyed before %s\n", boundary);
			++failures;
		}
	}

	void VerifyDestructionOrder()
	{
		if (liveStores != 0 || liveManifests != 0 || manifestLoads != 1 ||
			writerObservations != 1 || flushObservations != 1)
			++failures;
		if (failures != 0) {
			std::fprintf(stderr, "Shader cache lifetime: %d failures\n", failures.load());
			std::_Exit(1);
		}
		std::puts("Shader cache lifetime: both storage owners outlive writer join and final flush; manifest loading remains lazy");
	}

	void WaitForStop(std::stop_token token)
	{
		std::mutex mutex;
		std::condition_variable_any ready;
		std::unique_lock lock(mutex);
		ready.wait(lock, token, [] { return false; });
	}
}

// Only native I/O, logging, and compilation work are replaced. Ownership,
// singleton initialization, thread launch, and teardown use production code.
namespace logger
{
	template <class... Args>
	void debug(Args&&...)
	{}
	template <class... Args>
	void info(Args&&...)
	{}
	template <class... Args>
	void warn(Args&&...)
	{}
}
namespace Util::ShaderCachePack
{
	enum class LayoutState
	{
		Absent
	};
	struct Store
	{
		Store() { ++liveStores; }
		~Store() { --liveStores; }
	};
}
namespace Util::ShaderCacheManifest
{
	struct Manifest
	{
		Manifest() { ++liveManifests; }
		~Manifest() { --liveManifests; }
		void Load(const wchar_t*) { ++manifestLoads; }
	};
}
namespace SIE
{
	struct ShaderFileDependencyTracker
	{};
	struct CompilationPool
	{
		std::size_t get_thread_count() const { return 1; }
		void purge() {}
		bool wait_for(std::chrono::milliseconds) const { return true; }
		void wait() {}
	};
	bool IsEnvVarTruthy(const char*) { return false; }
	void FlushShaderCacheManifest()
	{
		++flushObservations;
		ObserveStores("final manifest flush");
	}
	class ShaderCache
	{
	public:
#include "shader_cache_lifetime_members.h"
		ShaderCache();
		~ShaderCache();
		void SetBackgroundCompilation(bool) {}
		void StopFileWatcher() {}
		void Clear() {}
		void SetSaveLoadDiskPersistenceBlocked(bool) {}
		void ManageCompilationSet(std::stop_token token) { WaitForStop(token); }
		void ProcessDeferredDiskWrites(std::stop_token token)
		{
			WaitForStop(token);
			++writerObservations;
			ObserveStores("persistence worker shutdown");
		}

		int compilationThreadCount = 1;
		int backgroundCompilationThreadCount = 1;
		CompilationPool compilationPool;
		std::unique_ptr<ShaderFileDependencyTracker> dependencyTracker;
		std::jthread managementJthread, deferredDiskWriterJthread;
		std::atomic_bool acceptDeferredDiskWrites{ true };
		std::mutex deferredDiskWritesMutex;
		std::condition_variable_any deferredDiskWritesCV;
		std::unordered_map<int, int> deferredDiskWrites;
		std::deque<int> deferredDiskWriteOrder;
		std::size_t deferredDiskWritesInFlight = 0;
		bool deferredManifestFlushPending = false;
	};
}

#include "shader_cache_lifetime_under_test.h"

int main()
{
	if (std::atexit(VerifyDestructionOrder) != 0)
		return 1;
	(void)SIE::ShaderCache::Instance();
	if (manifestLoads != 0) {
		std::fputs("cache construction loaded the loose manifest eagerly\n", stderr);
		++failures;
	}
	(void)SIE::GetShaderCacheManifest();
	(void)SIE::GetShaderCacheManifest();
	auto& packs = SIE::ManagedPacks();
	packs.optimized = std::make_unique<Util::ShaderCachePack::Store>();
	packs.developer = std::make_unique<Util::ShaderCachePack::Store>();
}
