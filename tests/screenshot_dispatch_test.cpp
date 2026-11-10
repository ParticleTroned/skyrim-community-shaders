#include "Features/ScreenshotApiPolicy.h"
#include "Features/ScreenshotSequencePolicy.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <iterator>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace globals
{
	struct State
	{
		bool isLoadingMenuOpen = false;
	} testState;
	auto* state = &testState;
	namespace game
	{
		struct UI
		{
			bool paused = false;
			bool GameIsPaused() const { return paused; }
		} testUI;
		auto* ui = &testUI;
	}
}

// Compile the production cancellation callbacks against a one-frame sequence
// whose finalization exposes any child-before-parent cancellation ordering.
struct ScreenshotApi
{
	using json = nlohmann::json;
	struct RequestRecord
	{
		std::string kind = "sequence_frame";
		std::string state = "accepted";
		std::string parentRequestId;
		bool cancelRequested = false;
		bool sourceAcquired = false;
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
		unsigned inFlight = 1;
		std::string outcome;
		CSX::ScreenshotPolicy::SequenceClock clock;
		std::string scheduleBasis = "game_frames";
		uint32_t nextOrdinal = 1;
		uint32_t frameCount = 100;
		uint32_t intervalFrames = 1;
		uint32_t intervalMs = 100;
		uint32_t startDelayFrames = 0;
		uint32_t startDelayMs = 0;
		uint32_t dropped = 0;
	};
	struct DispatchEntry
	{
		std::string requestId;
	};
	std::mutex mutex;
	bool acceptingRequests = true;
	std::unordered_map<std::string, RequestRecord> requests;
	std::unordered_map<std::string, SequenceRecord> sequences;
	std::deque<std::string> sequenceOrder;
	std::size_t sequenceCursor = 0;
	std::deque<DispatchEntry> manualDispatchQueue;
	std::deque<DispatchEntry> sequenceDispatchQueue;

	ScreenshotApi()
	{
		requests["parent"] = {};
		requests["child"].parentRequestId = "parent";
		sequences["parent"] = {};
	}
	bool IsTerminal(std::string_view state) const { return state == "cancelled" || state == "completed" || state == "dropped"; }
	void DrainManifestResultsLocked() {}
	void TransitionLocked(RequestRecord& record, std::string state, std::string_view, json = json::object()) { record.state = std::move(state); }
	void TryFinalizeSequenceLocked(SequenceRecord& sequence)
	{
		if (sequence.inFlight == 0 && !sequence.finalizing &&
			(sequence.cancelRequested || sequence.stopRequested || sequence.nextOrdinal > sequence.frameCount)) {
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
		sequence.dropped += child.state == "dropped";
		if (auto active = requests.find(sequence.activeChildRequestId); active != requests.end() && &active->second == &child)
			sequence.activeChildRequestId.clear();
		TryFinalizeSequenceLocked(sequence);
	}
	void MarkSequenceCancellationLocked(SequenceRecord&);
	void CancelQueuedDispatchesLocked(std::string_view, std::string_view);
	void OnFeatureDisabled(std::string_view);
	void BeginShutdown(std::string_view);
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
	void AppendEventLocked(RequestRecord&, std::string_view, json) {}
};

#include "screenshot_dispatch_under_test.h"
#include "screenshot_schedule_test.h"

int main()
{
	RunScreenshotScheduleTests();
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
