#include "Features/ScreenshotApiPolicy.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <iterator>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace logger
{
	template <class... T>
	void warn(T&&...)
	{}
}
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
std::string PathUtf8(const std::filesystem::path& path) { return path.string(); }
constexpr auto kRetention = std::chrono::seconds(1);
constexpr unsigned kMaximumRequests = 128;

// Compile the production cancellation callbacks against a one-frame sequence
// whose finalization exposes any child-before-parent cancellation ordering.
struct ScreenshotApi
{
	using json = nlohmann::json;
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
		struct Lease
		{
			std::filesystem::path Path() const { return {}; }
		};
		std::shared_ptr<Lease> directoryLease;
		std::shared_ptr<int> manifestChildren;
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
		json capture = json::object();
		std::shared_ptr<SequenceRecord::Lease> directoryLease;
		std::string error;
	};
	struct ManifestResult
	{
		std::string requestId;
		unsigned generation = 0;
		bool final = false, success = false;
		std::filesystem::path destination;
		std::string error;
	};
	struct WorkerState
	{
		std::mutex mutex;
		std::condition_variable condition;
		std::deque<DirectoryPreparationResult> preparationResults;
		std::deque<ManifestResult> results;
		std::deque<std::shared_ptr<int>> retiredChildren;
	};
	std::shared_ptr<WorkerState> manifestWorkerState = std::make_shared<WorkerState>();
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
		requests["parent"] = {};
		requests["parent"].requestId = "parent";
		requests["child"].parentRequestId = "parent";
		sequences["parent"] = {};
	}
	bool IsTerminal(std::string_view state) const { return state == "failed" || state == "cancelled" || state == "completed"; }
	void DrainWorkerResultsLocked();
	bool IsSequenceRecording() const;
	json BuildStatus(const ScreenshotFeature&) const;
	json MakeSequenceReceipt(const RequestRecord&, const SequenceRecord*) const;
	json MakeReceipt(const RequestRecord& record) const { return { { "state", record.state }, { "error", record.error } }; }
	void TrimLocked();
	void AppendEventLocked(RequestRecord&, std::string_view, json) {}
	void QueueSequenceManifestLocked(SequenceRecord&, bool) { ++queuedManifests; }
	void FinalizeSequenceLocked(SequenceRecord&, const ManifestResult*) {}
	void TransitionLocked(RequestRecord& record, std::string state, std::string_view, json = json::object()) { record.state = std::move(state); }
	void TryFinalizeSequenceLocked(SequenceRecord& sequence)
	{
		if (sequence.inFlight == 0 && !sequence.finalizing) {
			sequence.finalizing = true;
			sequence.outcome = sequence.cancelRequested ? "cancelled" : "completed";
			requests.at(sequence.requestId).state = sequence.outcome;
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
	void CancelQueuedDispatchesLocked(std::string_view, std::string_view);
	void OnFeatureDisabled(std::string_view);
	void BeginShutdown(std::string_view);
};

#include "screenshot_dispatch_under_test.h"

int main()
{
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
			api.manifestWorkerState->preparationResults.push_back({
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
				!api.manifestWorkerState->preparationResults.empty() || api.queuedManifests != 0)
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
