#include "Features/ScreenshotApiPolicy.h"

#include <deque>
#include <iterator>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

// Compile the production cancellation callbacks against a one-frame sequence
// whose finalization exposes any child-before-parent cancellation ordering.
struct ScreenshotApi
{
	using json = nlohmann::json;
	struct RequestRecord
	{
		std::string state = "accepted";
		std::string parentRequestId;
		bool cancelRequested = false;
		json error = nullptr;
		json errors = json::array();
	};
	struct SequenceRecord
	{
		std::string requestId = "parent";
		std::string activeChildRequestId = "child";
		bool cancelRequested = false;
		bool finalizing = false;
		unsigned inFlight = 1;
		std::string outcome;
	};
	struct DispatchEntry
	{
		std::string requestId;
	};
	std::mutex mutex;
	bool acceptingRequests = true;
	std::unordered_map<std::string, RequestRecord> requests;
	std::unordered_map<std::string, SequenceRecord> sequences;
	std::deque<DispatchEntry> manualDispatchQueue;
	std::deque<DispatchEntry> sequenceDispatchQueue;

	ScreenshotApi()
	{
		requests["parent"] = {};
		requests["child"].parentRequestId = "parent";
		sequences["parent"] = {};
	}
	bool IsTerminal(std::string_view state) const { return state == "cancelled" || state == "completed"; }
	void DrainManifestResultsLocked() {}
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
