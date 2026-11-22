#include "ShaderCompilationEstimate.h"
#include "ShaderCompilationSchedulingPolicy.h"

#include <Windows.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fmt/format.h>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace logger
{
	std::atomic<unsigned> warnings = 0;
	template <class... T>
	void warn(const char*, T&&...)
	{
		++warnings;
	}
	template <class... T>
	void info(const char*, T&&...)
	{}
	template <class... T>
	void debug(const char*, T&&...)
	{}
}
namespace BuildProvenance
{
	std::string compiler = "fixture-compiler";
	const std::string& GetShaderCompilerIdentity() { return compiler; }
}
namespace Util
{
	std::string PathToUtf8(const std::filesystem::path& a_path) { return a_path.string(); }
}
namespace Util::PathHelpers
{
	std::filesystem::path logPath;
	unsigned lookups = 0;
	std::filesystem::path GetLogPath()
	{
		++lookups;
		return logPath;
	}
}
namespace Util::FileHelpers
{
	bool WriteTextFileDirect(const std::filesystem::path&, std::string_view, std::string&)
	{
		throw std::runtime_error("Timing history must never use a destructive direct-write fallback");
	}
	bool WriteTextFileAtomic(const std::filesystem::path&, std::string_view, std::string&, bool = true, bool = true);
#define WriteTextFileAtomic WriteTextFileAtomicImpl
#include "compile_history_atomic.h"
#undef WriteTextFileAtomic
	std::function<void()> beforeWrite;
	std::atomic<unsigned> writes = 0;
	bool WriteTextFileAtomic(const std::filesystem::path& a_path, std::string_view a_bytes, std::string& a_error, bool a_fallback, bool a_overwrite)
	{
		++writes;
		if (beforeWrite)
			beforeWrite();
		return WriteTextFileAtomicImpl(a_path, a_bytes, a_error, a_fallback, a_overwrite);
	}
}
namespace SIE
{
	struct ShaderCompilationTask
	{
		bool current = true;
		std::size_t id = 1;
		std::size_t GetId() const { return id; }
		std::string GetString() const { return std::to_string(id); }
		bool operator<(const ShaderCompilationTask& a_other) const { return id < a_other.id; }
	};
}
namespace globals
{
	struct Cache
	{
		static Cache& Instance();
		bool success = true, diskHit = false;
		bool GetCompletedShader(const SIE::ShaderCompilationTask&) const { return success; }
		bool IsShaderLoadedFromDisk(const std::string&) const { return diskHit; }
		int compilationThreadCount = 4, backgroundCompilationThreadCount = 1;
		std::atomic<bool> backgroundCompilation = false;
		struct Pool
		{
			mutable unsigned queries = 0;
			unsigned get_thread_count() const
			{
				++queries;
				return 4;
			}
		} compilationPool;
	} cache;
	auto* shaderCache = &cache;
	Cache& Cache::Instance() { return cache; }
}
namespace SIE
{
#include "compile_history_constants.h"
	using ShaderCache = globals::Cache;
	struct CompilationSet
	{
		CompilationSet()
		{
			QueryPerformanceFrequency(&frequency);
			QueryPerformanceCounter(&lastReset);
			lastCalculation = lastReset;
		}
		std::mutex compilationMutex;
		LARGE_INTEGER frequency{}, lastReset{}, lastCalculation{}, totalTime{};
		std::atomic<int64_t> completionTime = 0;
		int64_t estimateBatchStartQpc = 0, estimateBatchEndQpc = 0;
		std::set<ShaderCompilationTask> availableTasks, tasksInProgress, processedTasks;
		void NotifyIfComplete() {}
		void Complete(const ShaderCompilationTask&, double, bool, std::string_view);
		void LoadEstimateHistory();
		std::string PrepareEstimate(const ShaderCompilationTask&, std::string_view);
		void MarkEstimateSourceCompile(const ShaderCompilationTask&);
		bool IsCurrentGeneration(const ShaderCompilationTask& a_task) const { return a_task.current; }
#include "compile_history_state.h"
		ShaderCompilationEstimate::Snapshot GetEstimate();
		static std::string GetHumanTime(double a_ms) { return std::format("{:.0f}ms", a_ms); }
		std::string GetStatsString(bool a_timeOnly = false, bool a_elapsedOnly = false);
		std::atomic<uint64_t> completedTasks = 0, totalTasks = 0, failedTasks = 0;
		std::atomic<uint64_t> cacheHitTasks = 0, diskHitTasks = 0, sourceCompileTasks = 0;
	};
#include "compile_history_completion.h"
#include "compile_history_display.h"
#include "compile_history_estimate.h"
#include "compile_history_persistence.h"
}

namespace
{
	using json = nlohmann::json;
	using namespace ShaderCompilationEstimate;

	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	std::string Bytes(const std::filesystem::path& a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
	}

	void WriteFixture(const std::filesystem::path& a_path, const std::string& a_bytes)
	{
		std::filesystem::create_directories(a_path.parent_path());
		std::ofstream(a_path, std::ios::binary) << a_bytes;
	}

	void Learn(SIE::CompilationSet& a_set, Category a_category, double a_ms)
	{
		std::scoped_lock lock(a_set.compilationMutex);
		a_set.estimate->Queue(a_category);
		a_set.estimate->Start(1, a_category, 0);
		a_set.estimate->Complete(1, a_ms, Outcome::Compiled, true);
		a_set.estimateHistoryDirty = true;
	}

	void AllowCheckpoint(SIE::CompilationSet& a_set)
	{
		a_set.estimateHistoryNextSaveQpc = 0;
	}

	void RestartCheckpointAndFailureRecovery()
	{
		SIE::CompilationSet first;
		first.LoadEstimateHistory();
		first.PrepareEstimate({}, "runtime-contract");
		const auto path = first.estimateHistoryPath;
		Require(path == Util::PathHelpers::logPath.parent_path() / "CSX-CompileTimeHistory.json", "History must live beside CSX.log");
		Require(!std::filesystem::exists(path), "Startup cannot create an empty history file");
		Learn(first, { 1, 1000 }, 500);
		first.SaveEstimateHistory();
		const auto saved = Bytes(path);
		Require(json::parse(saved)["categories"][0]["milliseconds"] == 500, "First completion checkpoint must persist real measurements");
		Learn(first, { 2, 2000 }, 750);
		first.SaveEstimateHistory();
		Require(first.estimateHistoryDirty && Bytes(path) == saved, "Frequent completions must be throttled without losing dirty state");
		first.FinishStartupEstimate();
		Require(!first.estimate && json::parse(Bytes(path))["categories"].size() == 2, "Final flush must bypass throttling and release startup state");

		const auto beforeRestart = Bytes(path);
		SIE::CompilationSet restarted;
		restarted.LoadEstimateHistory();
		restarted.PrepareEstimate({}, "runtime-contract");
		restarted.SaveEstimateHistory();
		Require(Bytes(path) == beforeRestart && restarted.estimate->History().size() == 2, "Restart must load history without truncating or rewriting it");
		Learn(restarted, { 1, 1000 }, 1000);
		const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		Require(handle != INVALID_HANDLE_VALUE, "Lock fixture destination against replacement");
		restarted.SaveEstimateHistory();
		CloseHandle(handle);
		Require(restarted.estimateHistoryDirty && Bytes(path) == beforeRestart, "Failed replacement must retain the original and pending update");
		AllowCheckpoint(restarted);
		restarted.SaveEstimateHistory();
		const auto updated = json::parse(Bytes(path));
		Require(!restarted.estimateHistoryDirty && updated["categories"].size() == 2 && updated["categories"][0]["milliseconds"] > 500,
			"Retry must update measured categories while retaining untouched saved categories");
		const auto afterRetry = Bytes(path);
		restarted.FinishStartupEstimate();
		Require(Bytes(path) == afterRetry, "Finalization must not rewrite unchanged history");

		SIE::CompilationSet incompatible;
		incompatible.LoadEstimateHistory();
		incompatible.PrepareEstimate({}, "different-runtime-contract");
		Require(incompatible.estimate->History().empty(), "Incompatible contexts cannot seed timings");
		incompatible.FinishStartupEstimate();
		Require(Bytes(path) == afterRetry, "Incompatible startup without measurements must leave history intact");
	}

	void MalformedHistory()
	{
		const auto valid = json::parse(Bytes(std::filesystem::current_path() / "SKSE" / "CSX-CompileTimeHistory.json"));
		Util::PathHelpers::logPath = std::filesystem::current_path() / "another-runtime" / "CSX.log";
		const auto path = Util::PathHelpers::logPath.parent_path() / "CSX-CompileTimeHistory.json";
		std::vector<std::string> invalid{ "broken json", std::string(SIE::kMaximumCompileTimeHistoryBytes + 1, ' ') };
		for (const auto& patch : std::vector<json>{
				 { { "version", 2 } }, { { "compiler", "different-compiler" } }, { { "processors", -1 } },
				 { { "context", std::string(257, 'x') } }, { { "categories", json::array({ valid["categories"][0], valid["categories"][0] }) } } }) {
			auto document = valid;
			document.update(patch);
			invalid.push_back(document.dump());
		}
		for (const auto& patch : std::vector<json>{
				 { { "category", kCategoryCount } }, { { "samples", 0 } }, { { "samples", 9 } },
				 { { "milliseconds", -1 } }, { { "milliseconds", kMaximumSampleMs + 1 } }, { { "milliseconds", "500" } } }) {
			auto document = valid;
			document["categories"][0].update(patch);
			invalid.push_back(document.dump());
		}
		for (const auto& bytes : invalid) {
			WriteFixture(path, bytes);
			SIE::CompilationSet malformed;
			malformed.LoadEstimateHistory();
			malformed.PrepareEstimate({}, "runtime-contract");
			Require(malformed.estimate->History().empty(), "Malformed or incompatible history cannot seed partial timings");
			malformed.FinishStartupEstimate();
			Require(Bytes(path) == bytes, "Rejected data cannot be deleted at startup");
		}
	}

	void ConcurrentWritersAndImmediatePrediction()
	{
		Util::PathHelpers::logPath = std::filesystem::current_path() / "parallel" / "CSX.log";
		SIE::CompilationSet set;
		set.LoadEstimateHistory();
		set.PrepareEstimate({}, "new-contract");
		Learn(set, { 1, 1000 }, 500);
		{
			std::unique_lock writerLock(set.estimateHistoryMutex);
			auto checkpoint = std::async(std::launch::async, [&] { set.SaveEstimateHistory(); });
			const auto status = checkpoint.wait_for(std::chrono::seconds(1));
			writerLock.unlock();
			checkpoint.get();
			Require(status == std::future_status::ready, "Concurrent checkpoint writers must not block compiler workers");
			Require(set.estimateHistoryDirty, "A skipped concurrent checkpoint must retain pending work");
		}
		set.FinishStartupEstimate();
		Require(!set.estimate && json::parse(Bytes(set.estimateHistoryPath))["categories"].size() == 1, "Final flush must retain the skipped checkpoint");

		SIE::CompilationSet initial;
		for (unsigned i = 0; i < 100; ++i)
			initial.estimate->Queue({ 1, 1000 });
		const auto snapshot = initial.GetEstimate();
		Require(!snapshot.calibrated && snapshot.remainingMs == 25'000, "An immediate cold estimate must use configured worker capacity");
		const auto display = initial.GetStatsString(true);
		Require(display.find("~25000ms") != std::string::npos && display.find("range") != std::string::npos,
			"The actual overlay formatter must show a prediction before calibration");
	}

	void CompletionCheckpointsOnlyLearnedSourceWork()
	{
		Util::PathHelpers::logPath = std::filesystem::current_path() / "completion" / "CSX.log";
		SIE::CompilationSet set;
		set.LoadEstimateHistory();
		const auto context = set.PrepareEstimate({}, "runtime-contract");
		const auto before = Util::FileHelpers::writes.load();
		auto complete = [&](std::size_t a_id, double a_ms, bool a_source, std::string_view a_context) {
			SIE::ShaderCompilationTask task{ true, a_id };
			set.estimate->Queue({ 1, 1000 });
			set.estimate->Start(a_id, { 1, 1000 }, 0);
			set.tasksInProgress.insert(task);
			++set.totalTasks;
			set.Complete(task, a_ms, a_source, a_context);
		};
		complete(1, std::numeric_limits<double>::quiet_NaN(), true, context);
		complete(2, 1000, true, "stale-context");
		complete(3, 10, false, {});
		Require(Util::FileHelpers::writes == before && !set.estimateHistoryDirty && set.estimate->History().empty(),
			"Invalid, stale and reused completions must not schedule history writes");
		complete(4, 500, true, context);
		Require(Util::FileHelpers::writes == before + 1 && !set.estimateHistoryDirty,
			"A valid source completion must automatically checkpoint its learned cost");
		complete(5, 600, true, context);
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		Require(Util::FileHelpers::writes == before + 1 && set.estimateHistoryDirty && !set.IsEstimateHistorySaveDue(now.QuadPart),
			"The completion path must reject checkpoint work before the next deadline");
		set.FinishStartupEstimate();
		const auto saved = Bytes(set.estimateHistoryPath);
		const auto writes = Util::FileHelpers::writes.load();
		const auto lookups = Util::PathHelpers::lookups;
		globals::cache.compilationPool.queries = 0;
		set.LoadEstimateHistory();
		Require(set.PrepareEstimate({}, "runtime-contract").empty(), "Gameplay cannot recreate the timing context");
		set.MarkEstimateSourceCompile({});
		set.Complete({ true, 6 }, 999'000, true, context);
		set.SaveEstimateHistory();
		set.FinishStartupEstimate();
		Require(!set.GetEstimate().enabled && !set.estimate && !set.IsEstimateHistorySaveDue(now.QuadPart),
			"Gameplay completions and repeated finalization cannot reactivate the estimator");
		Require(Util::FileHelpers::writes == writes && Bytes(set.estimateHistoryPath) == saved &&
					Util::PathHelpers::lookups == lookups && globals::cache.compilationPool.queries == 0,
			"Gameplay must bypass history I/O, history lookup and ETA worker-capacity calculation");
	}

	void ClosingStopsLearningBeforeWriterDrain()
	{
		Util::PathHelpers::logPath = std::filesystem::current_path() / "closing" / "CSX.log";
		SIE::CompilationSet set;
		set.LoadEstimateHistory();
		const auto context = set.PrepareEstimate({}, "runtime-contract");
		Learn(set, { 1, 1000 }, 500);
		std::promise<void> entered, release;
		auto enteredFuture = entered.get_future();
		auto releaseFuture = release.get_future().share();
		std::atomic<unsigned> attempts = 0;
		Util::FileHelpers::beforeWrite = [&] {
			if (++attempts == 1) {
				entered.set_value();
				releaseFuture.wait();
				throw std::runtime_error("Simulated checkpoint failure during startup close");
			}
		};
		auto checkpoint = std::async(std::launch::async, [&] { set.SaveEstimateHistory(); });
		const bool writerEntered = enteredFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
		auto finalization = std::async(std::launch::async, [&] { set.FinishStartupEstimate(); });
		bool closed = false;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (std::chrono::steady_clock::now() < deadline) {
			{
				std::scoped_lock lock(set.compilationMutex);
				closed = !set.estimate;
			}
			if (closed)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		const bool waiting = finalization.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout;
		set.Complete({ true, 100 }, 999'000, true, context);
		const bool disabled = !set.GetEstimate().enabled;
		release.set_value();
		checkpoint.get();
		finalization.get();
		Util::FileHelpers::beforeWrite = {};
		Require(writerEntered && closed && waiting && disabled,
			"Startup close must disable collection before waiting for the current writer");
		Require(attempts == 2 && json::parse(Bytes(set.estimateHistoryPath))["categories"][0]["milliseconds"] == 500,
			"Finalization must retry a failed in-flight checkpoint without learning late gameplay timings");
	}

	void MissingLogDirectoryDisablesCheckpoints()
	{
		Util::PathHelpers::logPath.clear();
		SIE::CompilationSet set;
		const auto writes = Util::FileHelpers::writes.load();
		set.LoadEstimateHistory();
		set.PrepareEstimate({}, "runtime-contract");
		Learn(set, { 1, 1000 }, 500);
		Require(!set.IsEstimateHistorySaveDue(std::numeric_limits<int64_t>::max()),
			"Unavailable history destinations must not cause checkpoint attempts on every completion");
		set.FinishStartupEstimate();
		Require(!set.estimate && Util::FileHelpers::writes == writes, "Missing log directories must not prevent startup shutdown");
	}
}

int main()
{
	const auto previous = std::filesystem::current_path();
	const auto root = previous / "Testing" / "ShaderCompilationHistory" /
	                  std::format("{}-{}", GetCurrentProcessId(), std::chrono::steady_clock::now().time_since_epoch().count());
	Require(std::filesystem::create_directories(root), "Create isolated history fixture");
	std::filesystem::current_path(root);
	Util::PathHelpers::logPath = root / "SKSE" / "CSX.log";
	RestartCheckpointAndFailureRecovery();
	MalformedHistory();
	ConcurrentWritersAndImmediatePrediction();
	CompletionCheckpointsOnlyLearnedSourceWork();
	ClosingStopsLearningBeforeWriterDrain();
	MissingLogDirectoryDisablesCheckpoints();
	std::filesystem::current_path(previous);
	std::filesystem::remove_all(root);
}
