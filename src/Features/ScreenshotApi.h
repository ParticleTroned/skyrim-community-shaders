#pragma once

#include "Api/ServiceFoundation.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct ScreenshotFeature;

/**
 * Versioned, process-local coordinator for screenshot and frame-sequence work.
 *
 * The coordinator owns operation identity, idempotency, receipts, the event
 * journal, and sequence scheduling. ScreenshotFeature remains the sole owner
 * of render acquisition and encoding resources.
 */
class ScreenshotApi
{
public:
	using json = nlohmann::json;

	ScreenshotApi();
	~ScreenshotApi();

	json HandleRequest(ScreenshotFeature& a_feature, const json& a_request);
	void Tick(ScreenshotFeature& a_feature, uint64_t a_engineFrame);

	void OnSourceWaiting(std::string_view a_requestId);
	void OnSourceFallback(std::string_view a_requestId, std::string_view a_reason);
	void OnArtifactQueued(std::string_view a_requestId, const std::filesystem::path& a_path);
	void OnArtifactEncoding(std::string_view a_requestId);
	void OnArtifactTerminal(
		std::string_view a_requestId,
		bool a_success,
		const std::filesystem::path& a_path,
		std::string_view a_error = {});
	void OnSourceTerminal(std::string_view a_requestId, std::string_view a_state, std::string_view a_error = {});
	void CancelAll(std::string_view a_reason);

	json BuildStatus(const ScreenshotFeature& a_feature) const;
	json BuildCapabilities(const ScreenshotFeature& a_feature) const;
	bool IsSequenceRecording() const;

private:
	struct RequestRecord
	{
		std::string requestId;
		std::string kind;
		std::string state;
		std::string clientId;
		std::string commandId;
		std::string parentRequestId;
		uint32_t sequenceOrdinal = 0;
		uint64_t scheduledEngineFrame = 0;
		uint64_t scheduledTimestampUs = 0;
		uint64_t eventIndex = 0;
		std::string acceptedUtc;
		std::string terminalUtc;
		json requested = json::object();
		json effective = json::object();
		json artifacts = json::array();
		json warnings = json::array();
		json error = nullptr;
		bool acknowledged = false;
		bool cancelRequested = false;
		uint32_t expectedArtifacts = 1;
		uint32_t terminalArtifacts = 0;
		uint32_t successfulArtifacts = 0;
		std::chrono::steady_clock::time_point createdAt = std::chrono::steady_clock::now();
		std::chrono::steady_clock::time_point terminalAt{};
	};

	struct SequenceRecord
	{
		std::string requestId;
		json capture = json::object();
		uint32_t frameCount = 0;
		uint32_t intervalFrames = 1;
		uint32_t startDelayFrames = 0;
		uint32_t intervalMs = 0;
		uint32_t maximumConsecutiveSkips = 10;
		uint32_t nextOrdinal = 1;
		uint32_t scheduled = 0;
		uint32_t acquired = 0;
		uint32_t written = 0;
		uint32_t dropped = 0;
		uint32_t failed = 0;
		uint32_t consecutiveSkips = 0;
		uint32_t inFlight = 0;
		uint64_t nextEngineFrame = 0;
		std::chrono::steady_clock::time_point nextWallClock{};
		std::chrono::steady_clock::time_point absoluteDeadline{};
		std::string scheduleBasis = "game_frames";
		std::string backpressurePolicy = "skip";
		std::string failurePolicy = "continue";
		bool stopRequested = false;
		bool cancelRequested = false;
		bool finalizing = false;
		bool frameManifest = true;
		std::filesystem::path directory;
		std::filesystem::path partialManifestPath;
		std::filesystem::path finalManifestPath;
		json children = json::array();
		json packaging = json::object();
	};

	struct DueFrame
	{
		std::string parentRequestId;
		std::string childRequestId;
		uint32_t ordinal = 0;
		json capture = json::object();
	};

	struct ManifestJob
	{
		std::string requestId;
		bool final = false;
		std::filesystem::path destination;
		std::filesystem::path partialPath;
		json manifest = json::object();
		json packaging = json::object();
	};

	CSX::Api::ServiceFoundation service;
	mutable std::mutex mutex;
	std::unordered_map<std::string, RequestRecord> requests;
	std::deque<std::string> requestOrder;
	std::unordered_map<std::string, SequenceRecord> sequences;
	json persistedSettings = nullptr;
	uint64_t completedArtifacts = 0;
	uint64_t failedArtifacts = 0;
	std::mutex manifestMutex;
	std::condition_variable_any manifestCV;
	std::deque<ManifestJob> manifestJobs;
	std::jthread manifestWriter;

	static constexpr uint32_t kContractMajor = 1;
	static constexpr uint32_t kContractMinor = 0;
	static constexpr uint32_t kSchemaRevision = 1;
	static constexpr std::size_t kMaximumRequests = 256;
	static constexpr std::size_t kMaximumEvents = 4096;
	static constexpr std::size_t kMaximumCommands = 1024;
	static constexpr std::size_t kMaximumPendingOperations = 64;
	static constexpr auto kRetention = std::chrono::hours(1);
	static constexpr uint32_t kMaximumSequenceFrames = 10000;
	static constexpr uint32_t kMaximumSequenceDurationMs = 3600000;
	static constexpr uint32_t kMaximumSequenceStartDelayMs = 300000;
	static constexpr uint32_t kMaximumSequenceStartDelayFrames = 18000;
	static constexpr std::size_t kMaximumActiveSequences = 8;
	static constexpr std::uint64_t kMaximumReservedSequenceChildren = 10000;

	json HandleValidatedRequest(ScreenshotFeature& a_feature, const json& a_request);
	json NormalizeCaptureDescriptor(const ScreenshotFeature& a_feature, const json& a_request, bool a_sequence = false) const;
	json ValidateSettingsPatch(const json& a_patch) const;
	void ApplySettingsPatch(ScreenshotFeature& a_feature, const json& a_patch) const;
	json BuildSettings(const ScreenshotFeature& a_feature) const;

	json MakeEnvelope(const json& a_request, bool a_ok) const;
	json MakeError(
		const json& a_request,
		std::string_view a_code,
		std::string_view a_message,
		std::string_view a_phase = "validation",
		bool a_retryable = false,
		std::string_view a_field = {},
		std::string_view a_requestId = {}) const;
	json MakeReceipt(const RequestRecord& a_record) const;
	json MakeSequenceReceipt(const RequestRecord& a_record, const SequenceRecord* a_sequence) const;
	json LookupReceiptLocked(std::string_view a_requestId) const;

	RequestRecord& CreateRequestLocked(
		std::string a_kind,
		const json& a_request,
		json a_effective,
		std::string a_parentRequestId = {},
		uint32_t a_sequenceOrdinal = 0,
		std::string a_requestId = {});
	void TransitionLocked(RequestRecord& a_record, std::string a_state, std::string_view a_eventType, json a_payload = json::object());
	void AppendEventLocked(RequestRecord& a_record, std::string_view a_type, json a_payload = json::object());
	void TrimLocked();
	std::size_t PendingOperationCountLocked() const;
	bool HasPendingCapacityLocked() const;
	bool HasSequenceCapacityLocked(std::uint32_t a_frameCount) const;
	void ExpireSequencesLocked(std::chrono::steady_clock::time_point a_now);
	void FinishSequenceChildLocked(RequestRecord& a_child, bool a_success, const std::filesystem::path& a_path, std::string_view a_error);
	void TryFinalizeSequenceLocked(SequenceRecord& a_sequence);
	void QueueSequenceManifestLocked(SequenceRecord& a_sequence, bool a_final);
	void RunManifestWriter(std::stop_token a_stopToken);
	static bool WriteManifestJob(const ManifestJob& a_job, std::string& a_error);
	void CompleteManifestJob(ManifestJob a_job, bool a_success, std::string a_error, json a_artifact);
	void CompleteSequenceFinalizationLocked(SequenceRecord& a_sequence, RequestRecord& a_parent, bool a_manifestWritten, json a_artifact);
	std::optional<DueFrame> PrepareDueFrameLocked(uint64_t a_engineFrame);

	static std::filesystem::path ResolveDestinationDirectory(
		const ScreenshotFeature& a_feature,
		const json& a_capture,
		bool a_sequence = false);
};
