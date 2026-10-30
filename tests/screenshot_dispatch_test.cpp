#include "Features/ScreenshotApiPolicy.h"
#include "Features/ScreenshotManifestSnapshot.h"
#include "Features/ScreenshotWorkerThread.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <iterator>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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
	};
	inline State* state = nullptr;
	namespace game
	{
		inline bool isVR = true;
	}
}
struct ScreenshotFeature
{
	bool loaded = true;
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

// Compile the production cancellation callbacks against a one-frame sequence
// whose finalization exposes any child-before-parent cancellation ordering.
struct ScreenshotApi
{
	using json = nlohmann::json;
	using ManifestChildNode = CSX::Screenshot::ManifestChildNode;
	struct RequestRecord
	{
		std::string requestId;
		std::string state = "accepted";
		std::string terminalUtc;
		std::string parentRequestId;
		bool cancelRequested = false;
		bool acknowledged = false;
		unsigned expectedArtifacts = 1;
		unsigned terminalArtifacts = 0;
		std::chrono::steady_clock::time_point terminalAt{};
		json effective = json::object();
		json error = nullptr;
		json errors = json::array();
	};
	struct SequenceRecord
	{
		std::string requestId = "parent";
		std::string activeChildRequestId = "child";
		bool cancelRequested = false;
		bool finalizing = false;
		bool preparationPending = false;
		bool stopRequested = false;
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
		std::string outcome;
	};
	struct DispatchEntry
	{
		std::string requestId;
	};
	struct DirectoryPreparationResult
	{
		std::string requestId;
		bool success = false;
		bool cancelled = false;
		json capture = json::object();
		std::shared_ptr<SequenceRecord::Lease> directoryLease;
		std::string error;
	};
	struct DirectoryPreparationJob
	{
		std::string requestId;
		json capture = json::object();
		std::filesystem::path configuredDirectory;
		bool cancelled = false;
	};
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
	struct WorkerState
	{
		std::mutex mutex;
		std::condition_variable condition;
		std::deque<DirectoryPreparationResult> preparationResults;
		std::deque<DirectoryPreparationJob> preparationJobs;
		std::deque<ManifestJob> jobs;
		std::deque<ManifestResult> results;
		std::deque<std::shared_ptr<const ManifestChildNode>> retiredChildren;
		std::unordered_map<std::string, SequenceRecord> retiredSequences;
		std::size_t outstanding = 0;
		bool stopRequested = false, exited = false;
	};
	using PreparationWorkerState = WorkerState;
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
	std::deque<std::string> requestOrder, sequenceOrder;
	unsigned sequenceCursor = 0, completedArtifacts = 0, failedArtifacts = 0;
	unsigned queuedManifests = 0;
	bool acceptingRequests = true;
	std::unordered_map<std::string, RequestRecord> requests;
	std::unordered_map<std::string, SequenceRecord> sequences;
	std::deque<DispatchEntry> manualDispatchQueue;
	std::deque<DispatchEntry> sequenceDispatchQueue;

	ScreenshotApi()
	{
		manifestWorker = std::make_unique<CSX::Screenshot::WorkerThread<WorkerState>>(manifestWorkerState, &ScreenshotApi::ManifestWorkerLoop);
		preparationWorker = std::make_unique<CSX::Screenshot::WorkerThread<WorkerState>>(preparationWorkerState, &ScreenshotApi::PreparationWorkerLoop);
		requests["parent"] = {};
		requests["parent"].requestId = "parent";
		requests["child"].parentRequestId = "parent";
		sequences["parent"] = {};
	}
	~ScreenshotApi();
	static void PreparationWorkerLoop(std::shared_ptr<PreparationWorkerState>);
	static void ManifestWorkerLoop(std::shared_ptr<ManifestWorkerState>);
	static std::filesystem::path ResolveDestinationDirectory(const json&, const std::filesystem::path& configured, bool, std::filesystem::path* approvedRoot)
	{
		approvedRoot->clear();
		if (configured == "blocked-preparation")
			preparationIo.Wait();
		return configured;
	}
	bool IsTerminal(std::string_view state) const { return state == "failed" || state == "cancelled" || state == "completed" || state == "stopped"; }
	void DrainWorkerResultsLocked();
	bool IsSequenceRecording() const;
	json BuildStatus(const ScreenshotFeature&) const;
	json MakeSequenceReceipt(const RequestRecord&, const SequenceRecord*) const;
	json MakeReceipt(const RequestRecord& record) const { return { { "state", record.state }, { "error", record.error } }; }
	void TrimLocked();
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
	void TransitionLocked(RequestRecord& record, std::string state, std::string_view, json = json::object()) { record.state = std::move(state); }
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
	void FinishSequenceChildLocked(RequestRecord& child)
	{
		if (child.parentRequestId.empty())
			return;
		auto& sequence = sequences.at(child.parentRequestId);
		--sequence.inFlight;
		TryFinalizeSequenceLocked(sequence);
	}
	void MarkSequenceCancellationLocked(SequenceRecord&);
	void CancelQueuedPreparationLocked(std::string_view);
	void CancelQueuedDispatchesLocked(std::string_view, std::string_view);
	void OnFeatureDisabled(std::string_view);
	void BeginShutdown(std::string_view);
	bool DrainForShutdown(std::chrono::milliseconds);
	bool CanAdmitPreparationLocked() const;
	unsigned partialManifests = 0;
};

#include "screenshot_dispatch_under_test.h"

void AwaitResults(const std::shared_ptr<ScreenshotApi::WorkerState>& state, std::size_t preparation, std::size_t manifest)
{
	std::unique_lock lock(state->mutex);
	if (!state->condition.wait_for(lock, std::chrono::seconds(1), [&] {
			return state->preparationResults.size() >= preparation && state->results.size() >= manifest;
		}))
		throw std::runtime_error("production worker did not return expected results");
}

void QueuePreparation(ScreenshotApi& api, std::string requestId, std::filesystem::path directory)
{
	const auto state = api.preparationWorkerState;
	{
		std::lock_guard lock(state->mutex);
		state->preparationJobs.push_back({ .requestId = std::move(requestId), .configuredDirectory = std::move(directory) });
		++state->outstanding;
	}
	state->condition.notify_one();
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
		if (api.partialManifests != 0 || api.queuedManifests != 1 ||
			!api.DrainForShutdown(std::chrono::milliseconds(100)))
			throw std::runtime_error("shutdown published a redundant checkpoint from an undrained preparation result");
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
		if (!api.DrainForShutdown(std::chrono::milliseconds(100)) || api.partialManifests != 0 || api.queuedManifests != 1)
			throw std::runtime_error("stopped preparation queued a redundant partial manifest or failed to drain");
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
			api.requests.at("existing").state != "failed" || api.preparationWorkerState->outstanding != 0 || api.manifestWorkerState->outstanding != 0)
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

int main()
{
	TestProductionWorkerIsolation();
	TestProductionDestructorDeadline();
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
				.error = missingLease ? "" : "destination denied",
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
	return 0;
}
