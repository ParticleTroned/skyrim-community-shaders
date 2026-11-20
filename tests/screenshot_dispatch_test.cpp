#include "Features/ScreenshotApiPolicy.h"
#include "Features/ScreenshotBurstPolicy.h"
#include "Features/ScreenshotManifestSnapshot.h"
#include "Features/ScreenshotSequencePolicy.h"
#include "Features/ScreenshotWorkerThread.h"
#include "Utils/JsonCleanup.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <filesystem>
#include <iterator>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <new>
#include <nlohmann/json.hpp>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	constexpr auto kNoAllocationFailure = std::numeric_limits<std::size_t>::max();
	thread_local bool trackPreparationAllocations = false;
	thread_local std::size_t preparationAllocationsRemaining = kNoAllocationFailure;
	std::atomic<std::size_t> requestedPreparationAllocations{ kNoAllocationFailure };
	std::atomic<std::size_t> observedPreparationAllocations{ 0 };
	std::atomic<std::size_t> rejectedPreparationAllocations{ 0 };
	bool failNextChildSnapshot = false;
}

void* operator new(std::size_t size)
{
	if (trackPreparationAllocations) {
		++observedPreparationAllocations;
		if (preparationAllocationsRemaining == 0) {
			++rejectedPreparationAllocations;
			throw std::bad_alloc{};
		}
		if (preparationAllocationsRemaining != kNoAllocationFailure)
			--preparationAllocationsRemaining;
	}
	if (auto memory = std::malloc(size ? size : 1))
		return memory;
	throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

namespace logger
{
	template <class... T>
	void warn(T&&...)
	{}
	template <class... T>
	void error(T&&...)
	{}
}

struct BlockingIo
{
	std::mutex mutex;
	std::condition_variable condition;
	bool entered = false, released = false;
	void Wait()
	{
		std::unique_lock lock(mutex);
		entered = true;
		condition.notify_all();
		condition.wait(lock, [&] { return released; });
	}
	void AwaitEntry()
	{
		std::unique_lock lock(mutex);
		if (!condition.wait_for(lock, std::chrono::seconds(1), [&] { return entered; }))
			throw std::runtime_error("production worker did not enter injected filesystem call");
	}
	void Release()
	{
		{
			std::lock_guard lock(mutex);
			released = true;
		}
		condition.notify_all();
	}
	void Reset()
	{
		std::lock_guard lock(mutex);
		entered = released = false;
	}
};
BlockingIo preparationIo, manifestIo;
namespace CSX::ScreenshotStorage
{
	struct DirectoryLease
	{
		std::filesystem::path path;
		std::filesystem::path Path() const { return path; }
		std::filesystem::path Destination() const { return path == "handle-corrected" ? "actual-destination" : path; }
		void VerifyDirectChild(const std::filesystem::path&) const {}
		static std::shared_ptr<DirectoryLease> CreateExclusive(const std::filesystem::path& path, const std::string&, const std::filesystem::path& = {})
		{
			if (path == "denied")
				throw std::runtime_error("destination denied");
			return std::make_shared<DirectoryLease>(path);
		}
	};
}
bool DeleteFileW(const std::filesystem::path::value_type*) { return false; }
constexpr unsigned ERROR_FILE_NOT_FOUND = 2;
unsigned GetLastError() { return ERROR_FILE_NOT_FOUND; }
nlohmann::json WriteJsonAtomically(const CSX::ScreenshotStorage::DirectoryLease&, const std::filesystem::path& path, const nlohmann::json&, unsigned, bool)
{
	if (path == "blocked-manifest")
		manifestIo.Wait();
	if (path == "denied-manifest")
		throw std::runtime_error("manifest denied");
	return { { "committed", true } };
}
nlohmann::json DescribeCommittedArtifact(const std::filesystem::path&, const nlohmann::json& committed) { return committed; }
namespace globals
{
	struct State
	{
		unsigned frameCount = 0;
		bool isLoadingMenuOpen = false;
	} testState;
	auto* state = &testState;
	namespace game
	{
		inline bool isVR = true;
		struct UI
		{
			bool paused = false;
			bool GameIsPaused() const { return paused; }
		} testUI;
		auto* ui = &testUI;
	}
}
struct ScreenshotFeature
{
	bool loaded = true;
	std::filesystem::path frameCapturePath = "prepared";
	bool IsRuntimeEnabled() const { return true; }
	std::string GetActiveCaptureRequestId() const { return {}; }
	unsigned GetOutstandingCaptureJobCount() const { return 0; }
};
namespace Util
{
	std::string PathToUtf8(const std::filesystem::path& path) { return path.string(); }
}
constexpr auto kRetention = std::chrono::seconds(1);
constexpr unsigned kMaximumRequests = 128;
constexpr unsigned kMaximumSequenceFrames = 10000;

// Compile the production cancellation callbacks against a one-frame sequence
// whose finalization exposes any child-before-parent cancellation ordering.
struct ScreenshotApi
{
	using json = nlohmann::json;
	using ManifestChildNode = CSX::Screenshot::ManifestChildNode;
	struct RequestRecord
	{
		std::string requestId;
		std::string kind = "sequence_frame";
		std::string state = "accepted";
		std::string terminalUtc;
		std::string parentRequestId;
		bool cancelRequested = false;
		bool acknowledged = false;
		unsigned expectedArtifacts = 1;
		unsigned terminalArtifacts = 0;
		unsigned successfulArtifacts = 0;
		unsigned sequenceOrdinal = 0;
		uint64_t scheduledEngineFrame = 0, scheduledTimestampUs = 0;
		std::chrono::steady_clock::time_point terminalAt{};
		json effective = json::object();
		json requested = json::object(), actual = json::object(), artifacts = json::array(), warnings = json::array();
		bool sourceAcquired = false;
		bool sequenceFinished = false;
		std::exception_ptr completionFailure, finalizationFailure;
		std::string failureState = "failed", partialFailureState = "failed_partial";
		std::exception_ptr finalizationJournalFailure;
		json error = nullptr;
		json errors = json::array();
	};
	struct SequenceRecord
	{
		std::string requestId = "parent";
		std::string activeChildRequestId = "child";
		bool cancelRequested = false;
		bool stopRequested = false;
		bool finalizing = false;
		bool preparationPending = false;
		bool frameManifest = true;
		unsigned nextOrdinal = 1;
		unsigned frameCount = 1;
		unsigned scheduled = 0, acquired = 0, written = 0, dropped = 0, failed = 0, cancelled = 0;
		unsigned manifestGeneration = 0, finalManifestGeneration = 0;
		json capture = json::object(), effective = json::object();
		json packaging = { { "frameManifest", { { "requested", true }, { "state", "pending" } } } };
		std::filesystem::path directory, partialManifestPath, finalManifestPath;
		using Lease = CSX::ScreenshotStorage::DirectoryLease;
		std::shared_ptr<Lease> directoryLease;
		std::shared_ptr<const ManifestChildNode> manifestChildren;
		unsigned inFlight = 1;
		unsigned consecutiveSkips = 0, maximumConsecutiveSkips = 0;
		std::size_t childCount = 0, nextCheckpointChildCount = 10;
		std::string failurePolicy = "continue", backpressurePolicy = "skip";
		std::string outcome;
		CSX::ScreenshotPolicy::SequenceClock clock;
		std::string scheduleBasis = "game_frames";
		uint32_t intervalFrames = 1;
		uint32_t intervalMs = 100;
		uint32_t startDelayFrames = 0;
		uint32_t startDelayMs = 0;
		std::exception_ptr manifestReportingFailure;
		ScreenshotBurst::Plan burst;
	};
	struct DispatchEntry
	{
		std::string requestId;
		json capture;
	};
	struct DirectoryPreparationJob
	{
		std::string requestId;
		json capture = json::object();
		std::filesystem::path configuredDirectory;
		bool success = false;
		bool cancelled = false;
		std::shared_ptr<SequenceRecord::Lease> directoryLease;
		std::exception_ptr failure;
	};
	static DirectoryPreparationJob SnapshotBurstPreparation(
		SequenceRecord& sequence, const json& requestedSequence, const json& descriptor, const ScreenshotFeature& a_feature)
	{
		const auto& requestId = sequence.requestId;
		const auto& schedule = requestedSequence.at("schedule");
		const bool sequenceUsesSettings = false;
		// Compile sequence_start validation and its admitted snapshot without the game boundary.
#include "screenshot_burst_preparation_under_test.h"
		return DirectoryPreparationJob(std::move(preparation));
	}
	struct ManifestJob
	{
		std::string requestId;
		unsigned generation = 0;
		bool final = false;
		std::filesystem::path destination, partialPath;
		std::shared_ptr<SequenceRecord::Lease> directoryLease;
		json header = json::object();
		std::shared_ptr<const ManifestChildNode> children;
	};
	struct ManifestResult
	{
		std::string requestId;
		unsigned generation = 0;
		bool final = false, success = false;
		std::filesystem::path destination;
		json artifact = nullptr;
		std::string error;
	};
	struct PreparationWorkerState
	{
		~PreparationWorkerState();
		std::mutex mutex;
		std::condition_variable condition;
		std::list<DirectoryPreparationJob> preparationResults;
		std::list<DirectoryPreparationJob> preparationJobs;
		std::list<DirectoryPreparationJob> activePreparation;
		std::deque<ManifestJob> jobs;
		std::deque<ManifestResult> results;
		std::deque<std::shared_ptr<const ManifestChildNode>> retiredChildren;
		std::unordered_map<std::string, SequenceRecord> retiredSequences;
		std::size_t outstanding = 0;
		bool stopRequested = false, exited = false;
	};
	using WorkerState = PreparationWorkerState;
	using ManifestWorkerState = WorkerState;
	std::shared_ptr<WorkerState> manifestWorkerState = std::make_shared<WorkerState>();
	std::shared_ptr<WorkerState> preparationWorkerState = std::make_shared<WorkerState>();
	std::unique_ptr<CSX::Screenshot::WorkerThread<WorkerState>> manifestWorker, preparationWorker;
	struct Service
	{
		json JournalStatus() const { return json::object(); }
		void ForgetRequest(const std::string&) {}
		void Trim() {}
	} service;
	mutable std::mutex mutex;
	std::deque<std::string> requestOrder;
	unsigned completedArtifacts = 0, failedArtifacts = 0;
	unsigned queuedManifests = 0;
	bool acceptingRequests = true;
	std::unordered_map<std::string, RequestRecord> requests;
	std::unordered_map<std::string, SequenceRecord> sequences;
	std::deque<std::string> sequenceOrder;
	std::size_t sequenceCursor = 0;
	std::deque<DispatchEntry> manualDispatchQueue;
	std::deque<DispatchEntry> sequenceDispatchQueue;

	ScreenshotApi()
	{
		manifestWorker = std::make_unique<CSX::Screenshot::WorkerThread<WorkerState>>(manifestWorkerState, &ScreenshotApi::ManifestWorkerLoop);
		preparationWorker = std::make_unique<CSX::Screenshot::WorkerThread<WorkerState>>(preparationWorkerState, &ScreenshotApi::PreparationWorkerLoop);
		requests["parent"] = {};
		requests["parent"].requestId = "parent";
		requests["child"].parentRequestId = "parent";
		requests["child"].requestId = "child";
		sequences["parent"] = {};
	}
	~ScreenshotApi();
	static void PreparationWorkerLoop(std::shared_ptr<PreparationWorkerState>);
	static void ManifestWorkerLoop(std::shared_ptr<ManifestWorkerState>);
	static std::filesystem::path ResolveDestinationDirectory(const json&, const std::filesystem::path& configured, bool, std::filesystem::path* approvedRoot)
	{
		trackPreparationAllocations = false;
		const bool blockedAllocation = configured == "blocked-allocation-test";
		if (blockedAllocation)
			preparationIo.Wait();
		if (configured == "allocation-test" || blockedAllocation) {
			observedPreparationAllocations = rejectedPreparationAllocations = 0;
			preparationAllocationsRemaining = requestedPreparationAllocations;
			trackPreparationAllocations = true;
		}
		approvedRoot->clear();
		if (configured == "blocked-preparation")
			preparationIo.Wait();
		return configured;
	}
	void DrainWorkerResultsLocked();
	bool IsSequenceRecording() const;
	json BuildStatus(const ScreenshotFeature&) const;
	json MakeSequenceReceipt(const RequestRecord&, const SequenceRecord*) const;
	json MakeReceipt(const RequestRecord& record) const { return { { "state", record.state }, { "error", record.error } }; }
	void TrimLocked();
	bool HasPendingWorkLocked(const RequestRecord&) const;
	std::size_t CountPendingOperationsLocked() const;
	void AppendEventLocked(RequestRecord&, std::string_view, json) {}
	void QueueSequenceManifestLocked(SequenceRecord&, bool final)
	{
		++queuedManifests;
		if (!final)
			++partialManifests;
	}
	void FinalizeSequenceLocked(SequenceRecord& sequence, const ManifestResult* result)
	{
		requests.at(sequence.requestId).state = result && result->success ? "completed" : "failed";
	}
	void TransitionLocked(RequestRecord&, std::string, std::string_view, json = json::object());
	void TryFinalizeSequenceLocked(SequenceRecord& sequence)
	{
		if (!sequence.preparationPending && sequence.inFlight == 0 && !sequence.finalizing &&
			(sequence.nextOrdinal > sequence.frameCount || sequence.stopRequested || sequence.cancelRequested)) {
			sequence.finalizing = true;
			sequence.outcome = sequence.cancelRequested ? "cancelled" : "completed";
			requests.at(sequence.requestId).state = sequence.outcome;
			if (sequence.frameManifest && sequence.directoryLease)
				QueueSequenceManifestLocked(sequence, true);
		}
	}
	void FailSequenceFinalizationLocked(SequenceRecord&, std::exception_ptr) noexcept;
	void ReleaseSequenceStorageLocked(SequenceRecord&) noexcept;
	void FailRequestCompletionLocked(RequestRecord& record, std::exception_ptr) noexcept
	{
		record.state = "failed";
		FinishSequenceChildLocked(record);
	}
	void FinishSequenceChildLocked(RequestRecord&) noexcept;
	void MarkSequenceCancellationLocked(SequenceRecord&);
	void CancelQueuedPreparationLocked(std::string_view);
	void CancelQueuedDispatchesLocked(std::string_view, std::string_view);
	void OnFeatureDisabled(std::string_view);
	void BeginShutdown(std::string_view);
	bool DrainForShutdown(std::chrono::milliseconds);
	bool CanAdmitPreparationLocked() const;
	unsigned partialManifests = 0;
	struct DueFrame
	{
		std::string parentRequestId;
		std::string childRequestId;
		uint32_t ordinal;
	};
	std::optional<DueFrame> PrepareDueFrameLocked(uint64_t, std::chrono::steady_clock::time_point);
	std::vector<std::pair<std::string, std::string>> UpdateSequenceClocksLocked(uint64_t, std::chrono::steady_clock::time_point);
	DueFrame CreateSequenceFrameLocked(SequenceRecord& sequence, uint64_t, std::chrono::steady_clock::time_point)
	{
		const auto ordinal = sequence.nextOrdinal++;
		const auto id = sequence.requestId + std::to_string(ordinal);
		requests[id].requestId = id;
		requests[id].parentRequestId = sequence.requestId;
		++sequence.inFlight;
		sequence.clock.Advance(sequence.intervalFrames, sequence.intervalMs);
		return { sequence.requestId, id, ordinal };
	}
	void FinishSourceLocked(RequestRecord&, std::string_view, std::string_view);
	bool RemoveQueuedDispatchLocked(std::string_view id)
	{
		return std::erase_if(sequenceDispatchQueue, [id](const auto& entry) { return entry.requestId == id; }) != 0;
	}
};

#include "screenshot_dispatch_under_test.h"

void ScreenshotApi::TransitionLocked(RequestRecord& record, std::string state, std::string_view, json payload)
{
	const Util::JsonCleanupGuard cleanup(payload);
	if (IsTerminal(record.state))
		return;
	record.state = std::move(state);
	if (IsTerminal(record.state)) {
		if (failNextChildSnapshot && !record.parentRequestId.empty()) {
			failNextChildSnapshot = false;
			preparationAllocationsRemaining = 0;
			trackPreparationAllocations = true;
		}
		FinishSequenceChildLocked(record);
	}
}
#include "screenshot_schedule_test.h"

void AwaitResults(const std::shared_ptr<ScreenshotApi::WorkerState>& state, std::size_t preparation, std::size_t manifest)
{
	std::unique_lock lock(state->mutex);
	if (!state->condition.wait_for(lock, std::chrono::seconds(1), [&] {
			return state->preparationResults.size() >= preparation && state->results.size() >= manifest;
		}))
		throw std::runtime_error("production worker did not return expected results");
}

void QueuePreparation(const std::shared_ptr<ScreenshotApi::WorkerState>& state, std::string requestId, std::filesystem::path directory)
{
	{
		std::lock_guard lock(state->mutex);
		state->preparationJobs.push_back({
			.requestId = std::move(requestId),
			.capture = { { "destination", { { "policy", "settings_default" }, { "overwrite", "never" } } } },
			.configuredDirectory = std::move(directory),
		});
		++state->outstanding;
	}
	state->condition.notify_one();
}

void QueuePreparation(ScreenshotApi& api, std::string requestId, std::filesystem::path directory)
{
	QueuePreparation(api.preparationWorkerState, std::move(requestId), std::move(directory));
}

void QueuePublication(ScreenshotApi& api, std::filesystem::path destination)
{
	auto& sequence = api.sequences["existing"];
	sequence.requestId = "existing";
	sequence.finalManifestGeneration = 1;
	api.requests["existing"].state = "finalizing";
	const auto state = api.manifestWorkerState;
	{
		std::lock_guard lock(state->mutex);
		state->jobs.push_back({ .requestId = "existing", .generation = 1, .final = true, .destination = std::move(destination), .directoryLease = std::make_shared<ScreenshotApi::SequenceRecord::Lease>() });
		++state->outstanding;
	}
	state->condition.notify_one();
}

void PrepareParent(ScreenshotApi& api)
{
	api.requests.erase("child");
	api.requests.at("parent").state = "preparing";
	auto& sequence = api.sequences.at("parent");
	sequence.preparationPending = true;
	sequence.inFlight = 0;
}

void TestProductionWorkerIsolation()
{
	{
		ScreenshotApi api;
		PrepareParent(api);
		QueuePreparation(api, "parent", "handle-corrected");
		AwaitResults(api.preparationWorkerState, 1, 0);
		std::lock_guard lock(api.preparationWorkerState->mutex);
		const auto& result = api.preparationWorkerState->preparationResults.front();
		if (!result.success || result.capture["destination"]["resolvedDirectory"] != "actual-destination")
			throw std::runtime_error("production preparation published a pre-open destination instead of its retained handle path");
	}
	{
		ScreenshotApi api;
		PrepareParent(api);
		QueuePreparation(api, "parent", "prepared");
		AwaitResults(api.preparationWorkerState, 1, 0);
		api.BeginShutdown("shutdown after preparation completed before result publication");
		if (!api.DrainForShutdown(std::chrono::milliseconds(100)) || api.partialManifests != 0 || api.queuedManifests != 0 ||
			api.requests.at("parent").state != "cancelled" || api.sequences.at("parent").directoryLease)
			throw std::runtime_error("shutdown adopted or published an undrained preparation result");
	}
	{
		manifestIo.Reset();
		ScreenshotApi api;
		QueuePublication(api, "blocked-manifest");
		manifestIo.AwaitEntry();
		api.preparationWorkerState->outstanding = 64;
		if (api.CanAdmitPreparationLocked())
			throw std::runtime_error("full preparation lane admitted additional work");
		api.preparationWorkerState->outstanding = 0;
		{
			std::lock_guard lock(api.manifestWorkerState->mutex);
			api.manifestWorkerState->retiredChildren.resize(256);
			if (api.manifestWorkerState->outstanding != 1)
				throw std::runtime_error("snapshot retirement unexpectedly counted as publication");
		}
		if (api.CanAdmitPreparationLocked())
			throw std::runtime_error("stalled snapshot retirement admitted unbounded new sequences");
		{
			std::lock_guard lock(api.manifestWorkerState->mutex);
			api.manifestWorkerState->retiredChildren.clear();
		}
		if (!api.CanAdmitPreparationLocked())
			throw std::runtime_error("released capacity failed to reopen preparation admission");
		manifestIo.Release();
		AwaitResults(api.manifestWorkerState, 0, 1);
		if (!api.DrainForShutdown(std::chrono::milliseconds(100)))
			throw std::runtime_error("capacity regression did not retire its blocking publication");
	}
	for (const bool stop : { false, true }) {
		preparationIo.Reset();
		ScreenshotApi api;
		PrepareParent(api);
		QueuePreparation(api, "parent", "blocked-preparation");
		preparationIo.AwaitEntry();
		QueuePublication(api, "committed");
		AwaitResults(api.manifestWorkerState, 0, 1);
		if (api.DrainForShutdown(std::chrono::milliseconds(20)) || api.requests.at("existing").state != "completed")
			throw std::runtime_error("stalled preparation blocked an existing sequence's terminal publication");
		if (stop)
			api.sequences.at("parent").stopRequested = true;
		else
			api.OnFeatureDisabled("cancel while preparation is active");
		if (!api.sequences.at("parent").preparationPending)
			throw std::runtime_error("cancelled synchronous I/O was falsely reported complete");
		preparationIo.Release();
		AwaitResults(api.preparationWorkerState, 1, 0);
		if (!api.DrainForShutdown(std::chrono::milliseconds(100)) || api.partialManifests != 0 || api.queuedManifests != 0 ||
			api.requests.at("parent").state != (stop ? "stopped" : "cancelled") || api.sequences.at("parent").directoryLease ||
			api.requests.at("existing").state != "completed")
			throw std::runtime_error("stopped or cancelled preparation adopted storage or disturbed an independent publication");
	}
	{
		manifestIo.Reset();
		ScreenshotApi api;
		PrepareParent(api);
		QueuePublication(api, "blocked-manifest");
		manifestIo.AwaitEntry();
		QueuePreparation(api, "parent", "prepared");
		AwaitResults(api.preparationWorkerState, 1, 0);
		if (api.DrainForShutdown(std::chrono::milliseconds(20)) || api.sequences.at("parent").preparationPending)
			throw std::runtime_error("stalled manifest blocked independent destination preparation");
		manifestIo.Release();
		AwaitResults(api.manifestWorkerState, 0, 1);
		if (!api.DrainForShutdown(std::chrono::milliseconds(100)))
			throw std::runtime_error("released manifest worker did not drain");
	}
	{
		ScreenshotApi api;
		PrepareParent(api);
		QueuePreparation(api, "parent", "denied");
		QueuePublication(api, "denied-manifest");
		AwaitResults(api.preparationWorkerState, 1, 0);
		AwaitResults(api.manifestWorkerState, 0, 1);
		if (!api.DrainForShutdown(std::chrono::milliseconds(100)) || api.requests.at("parent").state != "failed" ||
			api.requests.at("parent").error.at("message") != "destination denied" || api.requests.at("existing").state != "failed" ||
			api.preparationWorkerState->outstanding != 0 || api.manifestWorkerState->outstanding != 0)
			throw std::runtime_error("filesystem failure did not retire both isolated worker jobs");
	}
	{
		preparationIo.Reset();
		ScreenshotApi api;
		PrepareParent(api);
		QueuePreparation(api, "blocking", "blocked-preparation");
		preparationIo.AwaitEntry();
		QueuePreparation(api, "parent", "denied");
		api.BeginShutdown("shutdown before queued preparation");
		preparationIo.Release();
		AwaitResults(api.preparationWorkerState, 2, 0);
		if (!api.DrainForShutdown(std::chrono::milliseconds(100)) || api.requests.at("parent").state != "cancelled" ||
			api.sequences.at("parent").directoryLease || api.queuedManifests != 0)
			throw std::runtime_error("queued shutdown cancellation performed destination I/O or retained a pending manifest");
	}
	if (!CSX::ScreenshotPolicy::CanAdmitManifestJob(191, false) || CSX::ScreenshotPolicy::CanAdmitManifestJob(192, false) ||
		!CSX::ScreenshotPolicy::CanAdmitManifestJob(255, true) || CSX::ScreenshotPolicy::CanAdmitManifestJob(256, true))
		throw std::runtime_error("bounded manifest capacity failed to reserve terminal publication slots");
}

void TestBurstPreparationSnapshot()
{
	using json = ScreenshotApi::json;
	for (const bool vr : { false, true }) {
		globals::game::isVR = vr;
		ScreenshotApi api;
		PrepareParent(api);
		auto& sequence = api.sequences.at("parent");
		const json descriptor = {
			{ "source", { { "kind", vr ? "hmd_submission" : "desktop_mirror" }, { "fallback", "reject" } } },
			{ "outputs", json::array({ { { "view", vr ? "side_by_side" : "source_native" } } }) },
			{ "destination", { { "policy", "settings_default" }, { "overwrite", "never" } } },
			{ "clipboard", "first_output" },
		};
		const json requestedSequence = {
			{ "frameCount", 3 },
			{ "schedule", { { "intervalFrames", 1 }, { "pausePolicy", "hold" } } },
			{ "burst", { { "regions", json::array({
										  { { "x", 8 }, { "y", 12 }, { "width", 24 }, { "height", 16 } },
										  { { "x", 4 }, { "y", 40 }, { "width", 24 }, { "height", 8 } },
									  }) },
						   { "maximumBytes", 65536 } } },
		};
		sequence.capture = descriptor;
		auto preparation = ScreenshotApi::SnapshotBurstPreparation(sequence, requestedSequence, descriptor, ScreenshotFeature{});
		if (!sequence.burst || sequence.capture.at("clipboard") != "none")
			throw std::runtime_error("sequence_start did not apply native burst validation and clipboard suppression");
		{
			std::lock_guard lock(api.preparationWorkerState->mutex);
			api.preparationWorkerState->preparationJobs.push_back(std::move(preparation));
			++api.preparationWorkerState->outstanding;
		}
		api.preparationWorkerState->condition.notify_one();
		AwaitResults(api.preparationWorkerState, 1, 0);
		api.DrainWorkerResultsLocked();
		for (const auto* capture : { &sequence.capture, &sequence.effective.at("capture"), &api.requests.at("parent").effective.at("capture") }) {
			if (!capture->contains("burst") || capture->at("burst") != requestedSequence.at("burst") || capture->at("clipboard") != "none")
				throw std::runtime_error("destination preparation discarded the admitted burst regions or restored clipboard output");
			const auto plan = ScreenshotBurst::Parse(capture->at("burst"), 1, vr ? 2u : 1u);
			if (plan.regions.size() != 2 || plan.width != 24 || plan.height != 24 || plan.maximumBytes != 65536)
				throw std::runtime_error("prepared capture no longer selects the requested native region atlas");
		}
		if (sequence.preparationPending || api.requests.at("parent").state != "running" ||
			sequence.capture.at("destination").at("resolvedDirectory") != "prepared")
			throw std::runtime_error("burst preparation failed to adopt destination custody");
	}
	globals::game::isVR = true;
}

void TestFailedSequenceRetainsActiveStorage()
{
	for (const unsigned pressure : { 0u, 1u, 2u }) {
		ScreenshotApi api;
		auto& parent = api.requests.at("parent");
		parent.kind = "sequence";
		parent.state = "running";
		auto& sequence = api.sequences.at("parent");
		sequence.directoryLease = std::make_shared<ScreenshotApi::SequenceRecord::Lease>();
		const std::weak_ptr<ScreenshotApi::SequenceRecord::Lease> lease = sequence.directoryLease;
		sequence.frameCount = 4;
		const auto epoch = std::chrono::steady_clock::now();
		sequence.clock.Observe(100, epoch, false, 0, 0);
		api.requestOrder.push_back("parent");
		api.sequenceOrder.push_back("parent");
		failNextChildSnapshot = true;
		rejectedPreparationAllocations = 0;
		const auto due = api.PrepareDueFrameLocked(101, epoch);
		trackPreparationAllocations = false;
		if (due || failNextChildSnapshot || rejectedPreparationAllocations != 1 ||
			parent.state != "failed" || !parent.finalizationFailure || !sequence.finalizing ||
			!sequence.stopRequested || sequence.inFlight != 1 || lease.expired() ||
			!api.HasPendingWorkLocked(parent) || api.CountPendingOperationsLocked() != 1)
			throw std::runtime_error("a dropped-child snapshot failure released storage still owned by the acquiring frame");
		const auto originalFailure = parent.finalizationFailure;
		if (pressure == 0)
			parent.acknowledged = true;
		else if (pressure == 1)
			parent.terminalAt -= kRetention;
		else {
			for (unsigned index = 0; index < kMaximumRequests; ++index) {
				const auto id = "active-" + std::to_string(index);
				api.requests[id].requestId = id;
				api.requests[id].kind = "still";
				api.requestOrder.push_back(id);
			}
		}
		api.TrimLocked();
		if (!api.requests.contains("parent") || !api.sequences.contains("parent") || lease.expired())
			throw std::runtime_error("terminal retention erased a failed sequence before its active child drained");
		auto& child = api.requests.at("child");
		child.state = "completed";
		child.sourceAcquired = true;
		child.successfulArtifacts = 1;
		sequence.nextCheckpointChildCount = 1;
		preparationAllocationsRemaining = 0;
		observedPreparationAllocations = 0;
		trackPreparationAllocations = true;
		api.FinishSequenceChildLocked(child);
		api.FinishSequenceChildLocked(child);
		trackPreparationAllocations = false;
		if (observedPreparationAllocations != 0 || !lease.expired() || sequence.inFlight != 0 ||
			sequence.written != 1 || sequence.acquired != 1 || !sequence.activeChildRequestId.empty() ||
			parent.state != "failed" || parent.finalizationFailure != originalFailure ||
			sequence.childCount != 0 || api.queuedManifests != 0 || api.HasPendingWorkLocked(parent) ||
			api.CountPendingOperationsLocked() != (pressure == 2 ? kMaximumRequests : 0))
			throw std::runtime_error("failed-parent drain allocated, retried publication or lost exact storage retirement");
		api.TrimLocked();
		if (api.requests.contains("parent") || api.sequences.contains("parent"))
			throw std::runtime_error("fully drained failed sequence did not return to ordinary retention");
	}
}

void TestPreparationAllocationFailures()
{
	const auto run = [](std::size_t successfulAllocations, bool expectFailure) {
		ScreenshotApi api;
		PrepareParent(api);
		requestedPreparationAllocations = successfulAllocations;
		QueuePreparation(api, "parent", "allocation-test");
		AwaitResults(api.preparationWorkerState, 1, 0);
		{
			std::lock_guard lock(api.preparationWorkerState->mutex);
			const auto& result = api.preparationWorkerState->preparationResults.front();
			if (static_cast<bool>(result.failure) != expectFailure || result.success == expectFailure ||
				api.preparationWorkerState->outstanding != 1 || !api.preparationWorkerState->activePreparation.empty() ||
				!api.preparationWorkerState->preparationJobs.empty())
				throw std::runtime_error("allocation failure lost or duplicated its admitted preparation node");
		}
		const auto allocations = observedPreparationAllocations.load();
		if ((rejectedPreparationAllocations != 0) != expectFailure)
			throw std::runtime_error("preparation fault did not reach the requested allocation boundary");
		api.DrainWorkerResultsLocked();
		if (api.preparationWorkerState->outstanding != 0 || !api.preparationWorkerState->preparationResults.empty() ||
			api.sequences.at("parent").preparationPending || !api.CanAdmitPreparationLocked() ||
			api.requests.at("parent").state != (expectFailure ? "failed" : "running"))
			throw std::runtime_error("preparation completion failed to retire its pending slot exactly once");
		api.DrainWorkerResultsLocked();
		if (api.preparationWorkerState->outstanding != 0)
			throw std::runtime_error("repeated result drain retired a preparation slot twice");
		return allocations;
	};
	const auto allocations = run(kNoAllocationFailure, false);
	if (allocations == 0 || allocations > 512)
		throw std::runtime_error("preparation allocation sweep is missing or unbounded");
	for (std::size_t beforeFailure = 0; beforeFailure < allocations; ++beforeFailure)
		run(beforeFailure, true);
	// Exhaust the budget after success: publication and the next idle iteration must not allocate.
	if (run(allocations, false) != allocations)
		throw std::runtime_error("successful preparation allocated after its measured work completed");
	std::fprintf(stderr, "Preparation: persistent allocation sweep passed at %zu boundaries\n", allocations);

	preparationIo.Reset();
	ScreenshotApi api;
	PrepareParent(api);
	requestedPreparationAllocations = 0;
	QueuePreparation(api, "blocking", "blocked-preparation");
	preparationIo.AwaitEntry();
	QueuePreparation(api, "parent", "allocation-test");
	for (std::size_t index = 2; index < CSX::ScreenshotPolicy::MaximumPreparationJobs; ++index)
		QueuePreparation(api, "missing-" + std::to_string(index), "allocation-test");
	if (api.CanAdmitPreparationLocked())
		throw std::runtime_error("full preparation lane admitted work before its failures were drained");
	preparationIo.Release();
	AwaitResults(api.preparationWorkerState, CSX::ScreenshotPolicy::MaximumPreparationJobs, 0);
	if (api.CanAdmitPreparationLocked())
		throw std::runtime_error("completed preparation failures released capacity before coordinator retirement");
	api.DrainWorkerResultsLocked();
	if (api.preparationWorkerState->outstanding != 0 || !api.CanAdmitPreparationLocked() ||
		!api.preparationWorkerState->preparationResults.empty() || api.sequences.at("parent").preparationPending ||
		api.requests.at("parent").state != "failed")
		throw std::runtime_error("allocation failures or missing sequence records leaked bounded preparation capacity");
	api.DrainWorkerResultsLocked();
	if (api.preparationWorkerState->outstanding != 0)
		throw std::runtime_error("bounded preparation capacity was retired more than once");
	requestedPreparationAllocations = kNoAllocationFailure;
}

void TestDetachedPreparationAllocationFailure()
{
	preparationIo.Reset();
	auto released = std::make_shared<std::atomic<bool>>(false);
	auto state = std::shared_ptr<ScreenshotApi::WorkerState>(new ScreenshotApi::WorkerState, [released](auto* owned) {
		delete owned;
		*released = true;
	});
	auto worker = std::make_unique<CSX::Screenshot::WorkerThread<ScreenshotApi::WorkerState>>(
		state, &ScreenshotApi::PreparationWorkerLoop);
	requestedPreparationAllocations = 0;
	QueuePreparation(state, "detached", "blocked-allocation-test");
	preparationIo.AwaitEntry();
	if (worker->ShutdownUntil(std::chrono::steady_clock::now()))
		throw std::runtime_error("blocked preparation unexpectedly joined at its expired shutdown deadline");
	worker.reset();
	state.reset();
	preparationIo.Release();
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
	while (!*released && std::chrono::steady_clock::now() < deadline)
		std::this_thread::yield();
	if (!*released || rejectedPreparationAllocations != 1)
		throw std::runtime_error("detached preparation failed to destroy its undrained state under persistent allocation failure");
	requestedPreparationAllocations = kNoAllocationFailure;
}

void TestProductionDestructorDeadline()
{
	preparationIo.Reset();
	manifestIo.Reset();
	auto api = std::make_unique<ScreenshotApi>();
	PrepareParent(*api);
	QueuePreparation(*api, "parent", "blocked-preparation");
	QueuePublication(*api, "blocked-manifest");
	preparationIo.AwaitEntry();
	manifestIo.AwaitEntry();
	auto preparation = api->preparationWorkerState;
	auto manifest = api->manifestWorkerState;
	std::weak_ptr<ScreenshotApi::WorkerState> weakPreparation = preparation, weakManifest = manifest;
	const auto started = std::chrono::steady_clock::now();
	api.reset();
	const auto elapsed = std::chrono::steady_clock::now() - started;
	if (elapsed < std::chrono::milliseconds(1800) || elapsed > std::chrono::seconds(3) ||
		weakPreparation.expired() || weakManifest.expired())
		throw std::runtime_error("production destructor missed its common deadline or released active worker state");
	preparationIo.Release();
	manifestIo.Release();
	for (const auto& state : { preparation, manifest }) {
		std::unique_lock lock(state->mutex);
		if (!state->condition.wait_for(lock, std::chrono::seconds(1), [&] { return state->exited; }))
			throw std::runtime_error("detached production worker did not finish after filesystem release");
	}
	preparation.reset();
	manifest.reset();
	const auto releaseDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
	while ((!weakPreparation.expired() || !weakManifest.expired()) && std::chrono::steady_clock::now() < releaseDeadline)
		std::this_thread::yield();
	if (!weakPreparation.expired() || !weakManifest.expired())
		throw std::runtime_error("completed detached worker retained its isolated state");
}

void RunDispatchTests()
{
	std::fputs("Dispatch: worker isolation\n", stderr);
	TestProductionWorkerIsolation();
	std::fputs("Dispatch: burst preparation snapshot\n", stderr);
	TestBurstPreparationSnapshot();
	std::fputs("Dispatch: failed sequence storage custody\n", stderr);
	TestFailedSequenceRetainsActiveStorage();
	std::fputs("Dispatch: preparation allocation failures\n", stderr);
	TestPreparationAllocationFailures();
	std::fputs("Dispatch: detached preparation allocation failure\n", stderr);
	TestDetachedPreparationAllocationFailure();
	std::fputs("Dispatch: destructor deadline\n", stderr);
	TestProductionDestructorDeadline();
	std::fputs("Dispatch: recording status\n", stderr);
	{
		ScreenshotApi api;
		auto& sequence = api.sequences.at("parent");
		sequence.preparationPending = true;
		if (api.IsSequenceRecording())
			throw std::runtime_error("pending destination preparation reported recording");
		sequence.directoryLease = std::make_shared<ScreenshotApi::SequenceRecord::Lease>();
		if (api.IsSequenceRecording())
			throw std::runtime_error("pending preparation reported recording with an early lease");
		sequence.preparationPending = false;
		sequence.directoryLease.reset();
		if (api.IsSequenceRecording())
			throw std::runtime_error("sequence without directory custody reported recording");
		sequence.directoryLease = std::make_shared<ScreenshotApi::SequenceRecord::Lease>();
		api.requests.at("parent").state = "running";
		if (!api.IsSequenceRecording())
			throw std::runtime_error("prepared running sequence did not report recording");
		for (const unsigned terminalState : { 0u, 1u, 2u, 3u }) {
			sequence.stopRequested = terminalState == 0;
			sequence.cancelRequested = terminalState == 1;
			sequence.finalizing = terminalState == 2;
			sequence.nextOrdinal = terminalState == 3 ? sequence.frameCount + 1 : 1;
			if (api.IsSequenceRecording())
				throw std::runtime_error("stopped, cancelled, finalizing or exhausted sequence reported recording");
		}
	}
	std::fputs("Dispatch: preparation failure and trim\n", stderr);
	for (const bool requestedManifest : { false, true }) {
		for (const bool missingLease : { false, true }) {
			ScreenshotApi api;
			api.requests.erase("child");
			auto& parent = api.requests.at("parent");
			parent.state = "preparing";
			parent.expectedArtifacts = requestedManifest ? 1 : 0;
			auto& sequence = api.sequences.at("parent");
			sequence.inFlight = 0;
			sequence.frameManifest = requestedManifest;
			sequence.preparationPending = true;
			if (!requestedManifest)
				sequence.packaging["frameManifest"] = { { "requested", false }, { "state", "not_requested" } };
			api.preparationWorkerState->outstanding = 1;
			api.preparationWorkerState->preparationResults.push_back({
				.requestId = "parent",
				.success = missingLease,
				.failure = missingLease ? nullptr : std::make_exception_ptr(std::runtime_error("destination denied")),
			});
			api.DrainWorkerResultsLocked();
			const auto receipt = api.MakeSequenceReceipt(parent, &sequence);
			if (receipt["state"] != "failed" || receipt["error"]["code"] != "destination_unavailable" ||
				!receipt["manifest"]["finalPath"].is_null() || receipt["counts"]["inFlight"] != 0 ||
				receipt["counts"]["scheduled"] != 0 || receipt["counts"]["written"] != 0 ||
				api.BuildStatus(ScreenshotFeature{})["dispatcher"]["activeSequences"] != 0 ||
				api.IsSequenceRecording() || sequence.preparationPending || !sequence.finalizing ||
				api.queuedManifests != 0 || parent.terminalArtifacts != parent.expectedArtifacts)
				throw std::runtime_error("failed preparation remained active or lacked a terminal receipt");
			if (requestedManifest && (receipt["packaging"]["frameManifest"]["state"] != "failed" ||
										 !receipt["packaging"]["frameManifest"]["path"].is_null() ||
										 receipt["packaging"]["frameManifest"]["error"] != receipt["error"]))
				throw std::runtime_error("failed preparation left its manifest pending");
			parent.acknowledged = true;
			api.requestOrder.push_back("parent");
			api.sequenceOrder.push_back("parent");
			api.TrimLocked();
			api.DrainWorkerResultsLocked();
			if (!api.requests.empty() || !api.sequences.empty() || !api.sequenceOrder.empty() ||
				!api.preparationWorkerState->preparationResults.empty() || api.queuedManifests != 0)
				throw std::runtime_error("failed preparation could not be safely acknowledged and trimmed");
		}
	}
	std::fputs("Dispatch: scheduler\n", stderr);
	RunScreenshotScheduleTests();
	std::fputs("Dispatch: cancellation\n", stderr);
	for (const bool shutdown : { false, true }) {
		ScreenshotApi api;
		api.sequenceDispatchQueue.push_back({ "child" });
		api.manualDispatchQueue.push_back({ "still" });
		api.requests["still"] = {};
		if (shutdown)
			api.BeginShutdown("shutdown");
		else
			api.OnFeatureDisabled("feature_disabled");
		if (api.sequences.at("parent").outcome != "cancelled" ||
			api.requests.at("child").state != "cancelled" || api.requests.at("still").state != "cancelled" ||
			!api.sequenceDispatchQueue.empty() || !api.manualDispatchQueue.empty() ||
			api.acceptingRequests == shutdown)
			throw std::runtime_error("disabling or shutting down a queued final frame lost cancellation");
	}
	ScreenshotApi popped;
	popped.MarkSequenceCancellationLocked(popped.sequences.at("parent"));
	if (!popped.requests.at("child").cancelRequested ||
		CSX::ScreenshotPolicy::ResolveBusyDispatch(true, popped.requests.at("child").cancelRequested, false) !=
			CSX::ScreenshotPolicy::BusyDispatchDisposition::Cancel)
		throw std::runtime_error("parent cancellation did not reach its child after dispatch popped it");
}

int main()
{
	try {
		RunDispatchTests();
		return 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Dispatch fixture failed: %s\n", error.what());
		return 1;
	}
}
