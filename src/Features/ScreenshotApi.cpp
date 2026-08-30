#include "Features/ScreenshotApi.h"

#include "BuildProvenance.h"
#include "Features/ScreenshotApiPolicy.h"
#include "Features/ScreenshotFeature.h"
#include "Globals.h"
#include "ScreenshotDevBenchBridge.h"
#include "State.h"
#include "Utils/WinApi.h"
#include "VRAPI/CSpluginapi.h"

#include <Plugin.h>

#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <cmath>
#include <format>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace
{
	using json = nlohmann::json;

	std::string PathUtf8(const std::filesystem::path& a_path)
	{
		const auto value = a_path.u8string();
		return { reinterpret_cast<const char*>(value.data()), value.size() };
	}

	std::filesystem::path ResolveConfiguredCaptureDirectory(
		const std::filesystem::path& a_configured,
		bool a_sequence)
	{
		if (a_configured.empty())
			throw std::runtime_error("the configured screenshot directory is empty");
		if (a_configured.is_absolute())
			return std::filesystem::weakly_canonical(a_configured);

		const auto knownFolder = a_sequence ? Util::GetVideosPath() : Util::GetPicturesPath();
		if (!knownFolder)
			throw std::runtime_error("the Windows capture folder is unavailable");
		const auto root = std::filesystem::weakly_canonical(*knownFolder / "Community Shaders");
		const auto resolved = std::filesystem::weakly_canonical(root / a_configured);
		if (!CSX::ScreenshotPolicy::IsContainedPath(root, resolved))
			throw std::runtime_error("the configured screenshot directory escapes its Windows capture root");
		return resolved;
	}

	std::string FileSha256(const std::filesystem::path& a_path)
	{
		std::ifstream stream(a_path, std::ios::binary);
		if (!stream)
			throw std::runtime_error("could not open committed artifact for hashing");
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		const auto openStatus = BCryptOpenAlgorithmProvider(
			&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
		if (openStatus < 0)
			throw std::runtime_error(std::format("BCryptOpenAlgorithmProvider failed ({:#x})", static_cast<std::uint32_t>(openStatus)));
		DWORD objectBytes = 0;
		DWORD copiedBytes = 0;
		const auto propertyStatus = BCryptGetProperty(
			algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes),
			sizeof(objectBytes), &copiedBytes, 0);
		if (propertyStatus < 0) {
			BCryptCloseAlgorithmProvider(algorithm, 0);
			throw std::runtime_error(std::format("BCryptGetProperty failed ({:#x})", static_cast<std::uint32_t>(propertyStatus)));
		}
		std::vector<UCHAR> hashObject(objectBytes);
		BCRYPT_HASH_HANDLE hash = nullptr;
		const auto createStatus = BCryptCreateHash(
			algorithm, &hash, hashObject.data(), static_cast<ULONG>(hashObject.size()), nullptr, 0, 0);
		if (createStatus < 0) {
			BCryptCloseAlgorithmProvider(algorithm, 0);
			throw std::runtime_error(std::format("BCryptCreateHash failed ({:#x})", static_cast<std::uint32_t>(createStatus)));
		}
		std::vector<UCHAR> buffer(1024 * 1024);
		NTSTATUS hashStatus = 0;
		while (stream && hashStatus >= 0) {
			stream.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
			const auto bytesRead = stream.gcount();
			if (bytesRead > 0)
				hashStatus = BCryptHashData(hash, buffer.data(), static_cast<ULONG>(bytesRead), 0);
		}
		if (!stream.eof() && hashStatus >= 0)
			hashStatus = static_cast<NTSTATUS>(0xC0000185L);  // STATUS_IO_DEVICE_ERROR
		std::array<UCHAR, 32> digest{};
		const auto finishStatus = hashStatus < 0 ? hashStatus : BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
		BCryptDestroyHash(hash);
		BCryptCloseAlgorithmProvider(algorithm, 0);
		if (finishStatus < 0)
			throw std::runtime_error(std::format("SHA-256 hashing failed ({:#x})", static_cast<std::uint32_t>(finishStatus)));

		std::ostringstream result;
		result << std::hex << std::setfill('0');
		for (const auto value : digest)
			result << std::setw(2) << static_cast<unsigned int>(value);
		return result.str();
	}

	json DescribeCommittedArtifact(const std::filesystem::path& a_path)
	{
		std::error_code ec;
		const auto size = std::filesystem::file_size(a_path, ec);
		json artifact = {
			{ "path", PathUtf8(a_path) },
			{ "bytes", ec ? json(nullptr) : json(size) },
			{ "committed", true },
		};
		try {
			artifact["sha256"] = FileSha256(a_path);
		} catch (const std::exception& error) {
			artifact["sha256"] = nullptr;
			artifact["integrityError"] = error.what();
		}
		return artifact;
	}

	void WriteJsonAtomically(
		const std::filesystem::path& a_destination,
		const json& a_document)
	{
		std::filesystem::create_directories(a_destination.parent_path());
		const auto temporary = a_destination.string() + ".tmp";
		{
			std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
			stream << a_document.dump(2);
			stream.flush();
			if (!stream)
				throw std::runtime_error("manifest write failed");
		}
		if (!MoveFileExW(
				std::filesystem::path(temporary).c_str(),
				a_destination.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			throw std::runtime_error(std::format("manifest commit failed with Win32 error {}", GetLastError()));
		}
	}

	std::string SourceName(ScreenshotFeature::VRCaptureSource a_source)
	{
		switch (a_source) {
		case ScreenshotFeature::VRCaptureSource::DesktopMirror:
			return "desktop_mirror";
		case ScreenshotFeature::VRCaptureSource::FramedEye:
		case ScreenshotFeature::VRCaptureSource::FramedStereo:
		case ScreenshotFeature::VRCaptureSource::HMDSubmission:
		default:
			return "hmd_submission";
		}
	}

	std::string ViewName(const ScreenshotFeature& a_feature)
	{
		switch (a_feature.vrCaptureSource) {
		case ScreenshotFeature::VRCaptureSource::FramedStereo:
			return "framed_combined";
		case ScreenshotFeature::VRCaptureSource::FramedEye:
			return a_feature.vrFramedView == ScreenshotFeature::VRFramedView::Right ? "framed_right" : "framed_left";
		case ScreenshotFeature::VRCaptureSource::HMDSubmission:
			return "side_by_side";
		case ScreenshotFeature::VRCaptureSource::DesktopMirror:
		default:
			return "source_native";
		}
	}

	bool IsTerminal(std::string_view a_state)
	{
		return a_state == "completed" || a_state == "completed_with_warnings" ||
		       a_state == "failed" || a_state == "failed_partial" ||
		       a_state == "rejected" || a_state == "cancelled" ||
		       a_state == "cancelled_partial" || a_state == "stopped" ||
		       a_state == "dropped";
	}

	std::string ShortId(std::string_view a_id)
	{
		std::string result;
		result.reserve(8);
		for (char c : a_id) {
			if (c == '-')
				continue;
			result.push_back(c);
			if (result.size() == 8)
				break;
		}
		return result;
	}

}

ScreenshotApi::ScreenshotApi() :
	service(
		{ "csx.screenshot", kContractMajor, kContractMinor, kSchemaRevision },
		{ .maximumCommands = kMaximumCommands, .maximumEvents = kMaximumEvents, .commandRetention = kRetention })
{
	manifestWorkerState = std::make_shared<ManifestWorkerState>();
	manifestWorker = std::thread(&ScreenshotApi::ManifestWorkerLoop, manifestWorkerState);
	service.SetServerMetadataProvider([this] {
		auto metadata = BuildProvenance::GetProducer();
		metadata.update(json{
			{ "csxBuild", CSBuildNumber },
			{ "csxVersion", std::string(Plugin::VERSION_LABEL) },
			{ "featureVersion", "1.0.0" },
			{ "serviceSessionId", service.SessionId() },
			{ "runtime", {
							 { "game", globals::game::isVR ? "SkyrimVR" : "SkyrimSE" },
							 { "presentation", globals::game::isVR ? "openvr" : "dxgi" },
							 { "hmd", "unknown" },
						 } },
			{ "devBenchBuilt", ScreenshotDevBenchBridge::IsBuilt() },
			{ "devBenchRegistered", ScreenshotDevBenchBridge::IsRegistered() },
		});
		return metadata;
	});
}

ScreenshotApi::~ScreenshotApi()
{
	const auto state = manifestWorkerState;
	if (!state)
		return;
	{
		std::lock_guard lock(state->mutex);
		state->stopRequested = true;
	}
	state->condition.notify_all();
	bool exited = false;
	{
		std::unique_lock lock(state->mutex);
		exited = state->condition.wait_for(lock, std::chrono::seconds(2), [&] {
			return state->exited;
		});
	}
	if (!manifestWorker.joinable())
		return;
	if (exited)
		manifestWorker.join();
	else {
		logger::error("Screenshot manifest worker did not stop within two seconds; preserving its isolated state until it exits.");
		manifestWorker.detach();
	}
}

void ScreenshotApi::ManifestWorkerLoop(std::shared_ptr<ManifestWorkerState> a_state)
{
	while (true) {
		ManifestJob job;
		{
			std::unique_lock lock(a_state->mutex);
			a_state->condition.wait(lock, [&] {
				return a_state->stopRequested || !a_state->jobs.empty();
			});
			if (a_state->jobs.empty() && a_state->stopRequested)
				break;
			job = std::move(a_state->jobs.front());
			a_state->jobs.pop_front();
		}
		ManifestResult result{
			.requestId = job.requestId,
			.generation = job.generation,
			.final = job.final,
			.destination = job.destination,
		};
		try {
			WriteJsonAtomically(job.destination, job.document);
			if (job.final) {
				std::error_code ec;
				std::filesystem::remove(job.partialPath, ec);
				result.artifact = DescribeCommittedArtifact(job.destination);
			}
			result.success = true;
		} catch (const std::exception& error) {
			result.error = error.what();
		}
		{
			std::lock_guard lock(a_state->mutex);
			a_state->results.push_back(std::move(result));
			if (a_state->outstanding > 0)
				--a_state->outstanding;
		}
		a_state->condition.notify_all();
	}
	{
		std::lock_guard lock(a_state->mutex);
		a_state->exited = true;
	}
	a_state->condition.notify_all();
}

ScreenshotApi::json ScreenshotApi::HandleRequest(ScreenshotFeature& a_feature, const json& a_request)
{
	return service.Dispatch(
		a_request,
		[this, &a_feature](const json& validatedRequest) {
			{
				std::lock_guard lock(mutex);
				DrainManifestResultsLocked();
				if (persistedSettings.is_null())
					persistedSettings = BuildSettings(a_feature);
				TrimLocked();
			}
			return HandleValidatedRequest(a_feature, validatedRequest);
		},
		[this](std::string_view requestId) {
			std::lock_guard lock(mutex);
			return LookupReceiptLocked(requestId);
		});
}

ScreenshotApi::json ScreenshotApi::HandleValidatedRequest(ScreenshotFeature& a_feature, const json& a_request)
{
	const std::string action = a_request.at("action").get<std::string>();
	if (action == "capabilities") {
		auto response = MakeEnvelope(a_request, true);
		response["result"] = BuildCapabilities(a_feature);
		return response;
	}
	if (action == "status") {
		auto response = MakeEnvelope(a_request, true);
		response["result"] = BuildStatus(a_feature);
		return response;
	}
	if (action == "settings_get") {
		json persisted;
		{
			std::lock_guard lock(mutex);
			persisted = persistedSettings;
		}
		auto response = MakeEnvelope(a_request, true);
		response["result"] = {
			{ "settingsSchemaVersion", 2 },
			{ "effective", BuildSettings(a_feature) },
			{ "persisted", std::move(persisted) },
		};
		return response;
	}
	if (action == "settings_validate" || action == "settings_apply") {
		const auto patch = a_request.value("patch", json::object());
		const auto validation = ValidateSettingsPatch(patch);
		if (!validation["valid"].get<bool>()) {
			auto response = MakeEnvelope(a_request, true);
			response["result"] = validation;
			return response;
		}
		if (action == "settings_apply") {
			const auto scope = a_request.value("scope", std::string{});
			if (scope != "runtime_session" && scope != "persistent_user")
				return MakeError(a_request, "invalid_field", "scope must be runtime_session or persistent_user", "validation", false, "scope");
			ApplySettingsPatch(a_feature, patch);
			if (scope == "persistent_user") {
				if (!globals::state || !globals::state->Save(State::USER))
					return MakeError(a_request, "persistence_failed", "settings were applied at runtime but could not be persisted", "persistence", true);
				std::lock_guard lock(mutex);
				persistedSettings = BuildSettings(a_feature);
			}
		}
		auto response = MakeEnvelope(a_request, true);
		response["result"] = validation;
		response["result"]["effective"] = BuildSettings(a_feature);
		response["result"]["applied"] = action == "settings_apply";
		return response;
	}
	if (action == "capture") {
		if (!a_feature.IsRuntimeEnabled())
			return MakeError(a_request, "feature_disabled", "CSX screenshot capture is disabled", "validation", true);
		json descriptor;
		try {
			descriptor = NormalizeCaptureDescriptor(a_feature, a_request);
		} catch (const std::exception& e) {
			const std::string message = e.what();
			const bool pathError = message.find("destination") != std::string::npos || message.find("directory") != std::string::npos;
			return MakeError(a_request, pathError ? "unsafe_path" : "invalid_capture_descriptor", message);
		}
		std::string requestId;
		{
			std::lock_guard lock(mutex);
			if (!acceptingRequests)
				return MakeError(a_request, "service_stopping", "screenshot admission is closed", "admission", true);
			if (!CSX::ScreenshotPolicy::CanAdmitPendingOperations(CountPendingOperationsLocked()))
				return MakeError(a_request, "operation_capacity", "the screenshot pending-operation limit is full", "admission", true);
			auto& record = CreateRequestLocked("still", a_request, descriptor);
			requestId = record.requestId;
		}
		if (!a_feature.TryStartApiCapture(requestId, descriptor))
			OnSourceTerminal(requestId, "failed", "source_busy");
		auto response = MakeEnvelope(a_request, true);
		{
			std::lock_guard lock(mutex);
			response["result"] = LookupReceiptLocked(requestId);
		}
		return response;
	}
	if (action == "sequence_start") {
		if (!a_feature.IsRuntimeEnabled())
			return MakeError(a_request, "feature_disabled", "CSX screenshot capture is disabled", "validation", true);
		const auto requestedSequence = a_request.value("sequence", json::object());
		const uint32_t frameCount = requestedSequence.value("frameCount", a_feature.sequenceDefaults.frameCount);
		if (frameCount == 0 || frameCount > kMaximumSequenceFrames)
			return MakeError(a_request, "invalid_field", "sequence.frameCount is outside the advertised limit", "validation", false, "sequence.frameCount");
		json descriptorRequest = a_request;
		descriptorRequest["capture"] = requestedSequence.value("capture", json::object());
		const bool sequenceUsesSettings = requestedSequence.value("useSettings", descriptorRequest["capture"].empty());
		descriptorRequest["useSettings"] = sequenceUsesSettings;
		json descriptor;
		try {
			descriptor = NormalizeCaptureDescriptor(
				a_feature,
				descriptorRequest,
				sequenceUsesSettings && a_feature.sequenceDefaults.saveSeparateEyes);
			descriptor["destination"]["resolvedDirectory"] =
				PathUtf8(ResolveDestinationDirectory(a_feature, descriptor, true));
		} catch (const std::exception& e) {
			const std::string message = e.what();
			const bool pathError = message.find("destination") != std::string::npos || message.find("directory") != std::string::npos;
			return MakeError(a_request, pathError ? "unsafe_path" : "invalid_capture_descriptor", message);
		}
		SequenceRecord sequence;
		sequence.requested = requestedSequence;
		sequence.capture = descriptor;
		sequence.frameCount = frameCount;
		const auto schedule = requestedSequence.value("schedule", json::object());
		sequence.scheduleBasis = schedule.value("basis", std::string("game_frames"));
		if (sequence.scheduleBasis != "game_frames" && sequence.scheduleBasis != "wall_clock")
			return MakeError(a_request, "invalid_field", "schedule basis must be game_frames or wall_clock", "validation", false, "sequence.schedule.basis");
		sequence.intervalFrames = std::max(1u, schedule.value("intervalFrames", a_feature.sequenceDefaults.intervalFrames));
		sequence.startDelayFrames = schedule.value("startDelayFrames", 0u);
		sequence.intervalMs = std::max(1u, schedule.value("intervalMs", 100u));
		const auto startDelayMs = schedule.value("startDelayMs", 0u);
		if (sequence.scheduleBasis == "wall_clock" &&
			!CSX::ScreenshotPolicy::IsWallClockScheduleWithinLimit(startDelayMs, sequence.intervalMs, frameCount))
			return MakeError(a_request, "invalid_field", "sequence wall-clock span exceeds the advertised limit", "validation", false, "sequence.schedule");
		if (sequence.scheduleBasis == "game_frames" &&
			!CSX::ScreenshotPolicy::IsGameFrameScheduleWithinLimit(sequence.startDelayFrames, sequence.intervalFrames, frameCount))
			return MakeError(a_request, "invalid_field", "sequence game-frame span exceeds the advertised limit", "validation", false, "sequence.schedule");
		const auto backpressure = requestedSequence.value("backpressure", json::object());
		sequence.backpressurePolicy = backpressure.value("policy", std::string("skip"));
		sequence.maximumConsecutiveSkips = backpressure.value("maximumConsecutiveSkips", 10u);
		if (sequence.backpressurePolicy != "skip" && sequence.backpressurePolicy != "abort")
			return MakeError(a_request, "invalid_field", "backpressure policy must be skip or abort", "validation", false, "sequence.backpressure.policy");
		sequence.failurePolicy = requestedSequence.value("failurePolicy", std::string("continue"));
		if (sequence.failurePolicy != "continue" && sequence.failurePolicy != "abort")
			return MakeError(a_request, "invalid_field", "failurePolicy must be continue or abort", "validation", false, "sequence.failurePolicy");
		const auto packaging = requestedSequence.value("packaging", json::object());
		sequence.frameManifest = packaging.value("frameManifest", true);
		const auto preview = packaging.value("previewVideo", json::object());
		if (preview.value("requested", false) && preview.value("required", false))
			return MakeError(a_request, "optional_component_unavailable", "required preview video packaging is not available", "validation", false, "sequence.packaging.previewVideo");
		sequence.packaging = {
			{ "frameManifest", {
								   { "requested", sequence.frameManifest },
								   { "state", sequence.frameManifest ? "pending" : "not_requested" },
							   } },
			{ "previewVideo", {
								  { "requested", preview.value("requested", false) },
								  { "required", false },
								  { "state", preview.value("requested", false) ? "unsupported" : "not_requested" },
							  } },
		};

		std::string requestId;
		{
			std::lock_guard lock(mutex);
			if (!acceptingRequests)
				return MakeError(a_request, "service_stopping", "screenshot admission is closed", "admission", true);
			if (!CSX::ScreenshotPolicy::CanAdmitPendingOperations(CountPendingOperationsLocked()))
				return MakeError(a_request, "operation_capacity", "the screenshot pending-operation limit is full", "admission", true);
			auto& record = CreateRequestLocked("sequence", a_request, requestedSequence);
			record.expectedArtifacts = CSX::ScreenshotPolicy::ExpectedSequenceArtifacts(sequence.frameManifest);
			requestId = record.requestId;
			sequence.requestId = requestId;
			sequence.nextEngineFrame = (globals::state ? globals::state->frameCount : 0u) + sequence.startDelayFrames;
			sequence.nextWallClock = std::chrono::steady_clock::now() + std::chrono::milliseconds(startDelayMs);
			sequence.directory = ResolveDestinationDirectory(a_feature, descriptor, true) /
			                     ("CS_sequence_" + ShortId(requestId));
			sequence.partialManifestPath = sequence.directory / "sequence.json.partial";
			sequence.finalManifestPath = sequence.directory / "sequence.json";
			sequences.emplace(requestId, std::move(sequence));
			auto& stored = sequences.at(requestId);
			TransitionLocked(record, "running", "sequence.started", { { "frameCount", frameCount }, { "manifestPath", PathUtf8(stored.partialManifestPath) } });
			QueueSequenceManifestLocked(stored, false);
		}
		auto response = MakeEnvelope(a_request, true);
		{
			std::lock_guard lock(mutex);
			response["result"] = LookupReceiptLocked(requestId);
		}
		return response;
	}
	if (action == "sequence_stop" || action == "request_cancel") {
		const auto requestId = a_request.value("requestId", std::string{});
		std::string childToCancel;
		{
			std::lock_guard lock(mutex);
			auto found = requests.find(requestId);
			if (found == requests.end())
				return MakeError(a_request, "request_not_found", "requestId is not retained", "lookup", false, "requestId", requestId);
			if (IsTerminal(found->second.state)) {
				auto response = MakeEnvelope(a_request, true);
				response["result"] = MakeReceipt(found->second);
				response["result"]["alreadyTerminal"] = true;
				return response;
			}
			if (auto sequence = sequences.find(requestId); sequence != sequences.end()) {
				if (action == "sequence_stop") {
					sequence->second.stopRequested = true;
					TransitionLocked(found->second, "stop_requested", "sequence.stop_requested");
				} else {
					sequence->second.cancelRequested = true;
					childToCancel = sequence->second.activeChildRequestId;
					TransitionLocked(found->second, "cancel_requested", "request.cancel_requested");
				}
				TryFinalizeSequenceLocked(sequence->second);
			} else {
				found->second.cancelRequested = true;
				childToCancel = requestId;
				TransitionLocked(found->second, "cancel_requested", "request.cancel_requested", { { "irreversibleWorkMayFinish", true } });
			}
		}
		if (!childToCancel.empty() && a_feature.CancelApiCapture(childToCancel))
			OnSourceTerminal(childToCancel, "cancelled", "client_requested");
		auto response = MakeEnvelope(a_request, true);
		{
			std::lock_guard lock(mutex);
			response["result"] = LookupReceiptLocked(requestId);
		}
		return response;
	}
	if (action == "request_get") {
		const auto requestId = a_request.value("requestId", std::string{});
		std::lock_guard lock(mutex);
		if (!requests.contains(requestId))
			return MakeError(a_request, "request_not_found", "requestId is not retained", "lookup", false, "requestId", requestId);
		auto response = MakeEnvelope(a_request, true);
		response["result"] = LookupReceiptLocked(requestId);
		return response;
	}
	if (action == "request_list") {
		const auto limit = std::clamp(a_request.value("limit", 50u), 1u, 200u);
		const auto stateFilter = a_request.value("state", std::string{});
		json list = json::array();
		std::lock_guard lock(mutex);
		for (auto it = requestOrder.rbegin(); it != requestOrder.rend() && list.size() < limit; ++it) {
			const auto found = requests.find(*it);
			if (found == requests.end() || (!stateFilter.empty() && found->second.state != stateFilter))
				continue;
			list.push_back(MakeReceipt(found->second));
		}
		auto response = MakeEnvelope(a_request, true);
		response["result"] = { { "requests", std::move(list) }, { "retained", requests.size() } };
		return response;
	}
	if (action == "events_poll") {
		const uint64_t after = a_request.value("afterEventId", 0ull);
		const auto limit = std::clamp(a_request.value("limit", 100u), 1u, 500u);
		const auto requestFilter = a_request.value("requestId", std::string{});
		auto response = MakeEnvelope(a_request, true);
		response["result"] = service.PollEvents(after, limit, requestFilter);
		return response;
	}
	if (action == "acknowledge") {
		const auto requestId = a_request.value("requestId", std::string{});
		{
			std::lock_guard lock(mutex);
			if (!requestId.empty()) {
				if (auto found = requests.find(requestId); found != requests.end())
					found->second.acknowledged = true;
				else
					return MakeError(a_request, "request_not_found", "requestId is not retained", "lookup", false, "requestId", requestId);
			}
		}
		uint64_t acknowledgedThrough = service.JournalStatus().value("acknowledgedThroughEventId", 0ull);
		if (a_request.contains("throughEventId"))
			acknowledgedThrough = service.AcknowledgeEvents(a_request["throughEventId"].get<uint64_t>());
		auto response = MakeEnvelope(a_request, true);
		response["result"] = { { "acknowledgedThroughEventId", acknowledgedThrough }, { "requestId", requestId.empty() ? json(nullptr) : json(requestId) } };
		{
			std::lock_guard lock(mutex);
			TrimLocked();
		}
		return response;
	}

	return MakeError(a_request, "unknown_action", "action is not supported", "validation", false, "action");
}

ScreenshotApi::json ScreenshotApi::NormalizeCaptureDescriptor(
	const ScreenshotFeature& a_feature,
	const json& a_request,
	bool a_addSeparateEyeOutputs) const
{
	json capture = a_request.value("capture", json::object());
	const bool useSettings = a_request.value("useSettings", capture.empty());
	if (!capture.is_object())
		throw std::runtime_error("capture must be an object");

	json source = capture.value("source", json::object());
	std::string sourceKind = source.value("kind", useSettings ? SourceName(a_feature.vrCaptureSource) : std::string{});
	if (sourceKind == "settings_default")
		sourceKind = SourceName(a_feature.vrCaptureSource);
	if (!globals::game::isVR && sourceKind == "hmd_submission")
		sourceKind = "desktop_mirror";
	if (sourceKind != "desktop_mirror" && sourceKind != "hmd_submission")
		throw std::runtime_error("capture.source.kind must be desktop_mirror or hmd_submission");
	const auto fallback = source.value(
		"fallback",
		useSettings && sourceKind == "hmd_submission" && ViewName(a_feature) == "side_by_side" ?
			"desktop_mirror" :
			"reject");
	if (fallback != "reject" && fallback != "desktop_mirror")
		throw std::runtime_error("capture.source.fallback must be reject or desktop_mirror");

	json outputs = capture.value("outputs", json::array());
	if (!outputs.is_array())
		throw std::runtime_error("capture outputs must be an array");
	if (outputs.empty()) {
		outputs.push_back({
			{ "view", useSettings ? ViewName(a_feature) : "source_native" },
			{ "dominantEye", a_feature.vrFramedDominantEye == vr::Eye_Right ? "right" : "left" },
			{ "encoding", { { "format", a_feature.sdrUsePng ? "png" : "bmp" }, { "colourContract", "sdr_srgb" } } },
		});
	}
	if (a_addSeparateEyeOutputs && sourceKind == "hmd_submission") {
		const auto encoding = outputs.front().value("encoding", json::object());
		auto containsView = [&outputs](std::string_view a_view) {
			return std::any_of(outputs.begin(), outputs.end(), [a_view](const json& output) {
				return output.value("view", std::string{}) == a_view;
			});
		};
		const bool addLeft = !containsView("left_eye");
		const bool addRight = !containsView("right_eye");
		const std::size_t additions = static_cast<std::size_t>(addLeft) + static_cast<std::size_t>(addRight);
		if (!CSX::ScreenshotPolicy::CanAugmentOutputs(outputs.size(), additions))
			throw std::runtime_error("settings-derived eye outputs exceed the 4-output limit");
		if (addLeft)
			outputs.push_back({ { "view", "left_eye" }, { "encoding", encoding }, { "nameSuffix", "left" } });
		if (addRight)
			outputs.push_back({ { "view", "right_eye" }, { "encoding", encoding }, { "nameSuffix", "right" } });
	}
	if (outputs.empty() || outputs.size() > CSX::ScreenshotPolicy::MaximumOutputsPerFrame)
		throw std::runtime_error("capture outputs must contain 1 to 4 entries");
	static constexpr std::array views = {
		"source_native", "left_eye", "right_eye", "side_by_side", "framed_left", "framed_right", "framed_combined"
	};
	std::unordered_set<std::string> suffixes;
	for (auto& output : outputs) {
		if (!output.is_object())
			throw std::runtime_error("each capture output must be an object");
		const auto view = output.value("view", std::string("source_native"));
		if (std::find(views.begin(), views.end(), view) == views.end())
			throw std::runtime_error("capture output view is unsupported");
		if (sourceKind == "desktop_mirror" && view != "source_native")
			throw std::runtime_error("desktop_mirror supports only source_native outputs");
		auto encoding = output.value("encoding", json::object());
		const auto format = encoding.value("format", std::string("png"));
		if (format != "png" && format != "bmp")
			throw std::runtime_error("encoding format must be png or bmp");
		if (encoding.value("colourContract", std::string("sdr_srgb")) != "sdr_srgb")
			throw std::runtime_error("only the sdr_srgb colour contract is supported");
		output["encoding"] = { { "format", format }, { "colourContract", "sdr_srgb" } };
		if (view.starts_with("framed_")) {
			if (output.contains("crop") && !output["crop"].is_null())
				throw std::runtime_error("framed views do not accept an additional crop");
			if ((output.contains("width") && output["width"].get<uint32_t>() != 2560u) ||
				(output.contains("height") && output["height"].get<uint32_t>() != 1440u))
				throw std::runtime_error("version 1 framed outputs are fixed at 2560 x 1440");
			output["width"] = 2560;
			output["height"] = 1440;
		} else if (output.contains("width") || output.contains("height")) {
			throw std::runtime_error("custom resizing is not supported for native outputs");
		}
		if (output.contains("crop") && output["crop"].is_object()) {
			const auto& crop = output["crop"];
			const float x = crop.value("x", -1.0f);
			const float y = crop.value("y", -1.0f);
			const float width = crop.value("width", -1.0f);
			const float height = crop.value("height", -1.0f);
			if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) ||
				x < 0.0f || y < 0.0f || width <= 0.0f || height <= 0.0f || x + width > 1.0f || y + height > 1.0f)
				throw std::runtime_error("output crop must be a finite normalized rectangle");
		}
		const auto suffix = output.value("nameSuffix", view);
		if (suffix.size() > 48 || !CSX::ScreenshotPolicy::IsSafeWindowsFilenameSegment(suffix) ||
			!suffixes.insert(CSX::ScreenshotPolicy::FilenameCollisionKey(suffix)).second)
			throw std::runtime_error("output nameSuffix values must be unique and path-safe");
		output["nameSuffix"] = suffix;
	}

	json destination = capture.value("destination", json::object());
	const auto policy = destination.value("policy", std::string("settings_default"));
	if (policy != "settings_default" && policy != "game_relative" && policy != "absolute")
		throw std::runtime_error("destination policy is unsupported");
	if (destination.value("overwrite", std::string("never")) != "never")
		throw std::runtime_error("version 1 never overwrites artifacts");
	if (destination.contains("baseName") && !destination["baseName"].is_null()) {
		const auto baseName = destination["baseName"].get<std::string>();
		if (baseName.size() > 96 || !CSX::ScreenshotPolicy::IsSafeWindowsFilenameSegment(baseName))
			throw std::runtime_error("destination.baseName is unsafe");
	}

	json tags = capture.value("tags", json::object());
	if (!tags.is_object() || tags.dump().size() > 4096 || tags.size() > 32)
		throw std::runtime_error("capture tags exceed the advertised bound");
	for (const auto& [key, value] : tags.items()) {
		if (key.size() > 64 || !value.is_string() || value.get_ref<const std::string&>().size() > 256)
			throw std::runtime_error("capture tags must be bounded string pairs");
	}

	const auto clipboard = capture.value("clipboard", a_feature.copyToClipboard ? "file_reference" : "none");
	if (clipboard != "none" && clipboard != "file_reference")
		throw std::runtime_error("capture.clipboard must be none or file_reference");

	json normalized = {
		{ "source", { { "kind", sourceKind }, { "fallback", fallback } } },
		{ "outputs", outputs },
		{ "destination", {
							 { "policy", policy },
							 { "directory", destination.value("directory", json(nullptr)) },
							 { "baseName", destination.value("baseName", json(nullptr)) },
							 { "overwrite", "never" },
						 } },
		{ "clipboard", clipboard },
		{ "tags", std::move(tags) },
	};
	normalized["destination"]["resolvedDirectory"] = PathUtf8(ResolveDestinationDirectory(a_feature, normalized));
	return normalized;
}

ScreenshotApi::json ScreenshotApi::BuildSettings(const ScreenshotFeature& a_feature) const
{
	return {
		{ "Enabled", a_feature.IsRuntimeEnabled() },
		{ "Destination", { { "Policy", "settings_default" }, { "Directory", a_feature.screenshotPath }, { "Overwrite", "never" } } },
		{ "Encoding", { { "Format", a_feature.sdrUsePng ? "png" : "bmp" }, { "ColourContract", "sdr_srgb" } } },
		{ "Clipboard", a_feature.copyToClipboard ? "file_reference" : "none" },
		{ "VR", {
					{ "Source", SourceName(a_feature.vrCaptureSource) },
					{ "View", ViewName(a_feature) },
					{ "DominantEye", a_feature.vrFramedDominantEye == vr::Eye_Right ? "right" : "left" },
					{ "ApplyCrop", a_feature.applyCropToScreenshot },
				} },
		{ "Sequence", {
						  { "Destination", { { "Policy", "settings_default" }, { "Directory", a_feature.frameCapturePath }, { "Overwrite", "never" } } },
						  { "Encoding", { { "Format", a_feature.frameCaptureUsePng ? "png" : "bmp" }, { "ColourContract", "sdr_srgb" } } },
						  { "FrameCount", a_feature.sequenceDefaults.frameCount },
						  { "Schedule", { { "Basis", "game_frames" }, { "IntervalFrames", a_feature.sequenceDefaults.intervalFrames } } },
						  { "Backpressure", { { "Policy", "skip" }, { "MaximumConsecutiveSkips", 10 } } },
						  { "FailurePolicy", "continue" },
						  { "Outputs", { { "SeparateEyes", a_feature.sequenceDefaults.saveSeparateEyes } } },
						  { "Packaging", { { "PreviewVideo", { { "Requested", a_feature.sequenceDefaults.writePreviewVideo }, { "FramesPerSecond", a_feature.sequenceDefaults.previewFramesPerSecond } } } } },
					  } },
	};
}

ScreenshotApi::json ScreenshotApi::ValidateSettingsPatch(const json& a_patch) const
{
	json errors = json::array();
	if (!a_patch.is_object())
		errors.push_back({ { "field", "patch" }, { "code", "wrong_type" }, { "message", "patch must be an object" } });
	auto checkUInt = [&errors](const json& object, std::string_view key, uint32_t min, uint32_t max, std::string_view path) {
		if (!object.contains(key))
			return;
		if (!object[key].is_number_unsigned() && !object[key].is_number_integer())
			errors.push_back({ { "field", path }, { "code", "wrong_type" } });
		else {
			const auto value = object[key].get<int64_t>();
			if (value < min || value > max)
				errors.push_back({ { "field", path }, { "code", "out_of_range" } });
		}
	};
	auto checkConfiguredDirectory = [&errors](
										const json& a_destination,
										std::string_view a_field,
										bool a_sequence) {
		if (!a_destination.contains("Directory") || !a_destination["Directory"].is_string())
			return;
		try {
			(void)ResolveConfiguredCaptureDirectory(
				std::filesystem::u8path(a_destination["Directory"].get<std::string>()),
				a_sequence);
		} catch (const std::exception& e) {
			errors.push_back({ { "field", a_field }, { "code", "unsafe_path" }, { "message", e.what() } });
		}
	};
	if (a_patch.is_object()) {
		if (a_patch.contains("Enabled") && !a_patch["Enabled"].is_boolean())
			errors.push_back({ { "field", "Enabled" }, { "code", "wrong_type" } });
		if (const auto destination = a_patch.find("Destination"); destination != a_patch.end()) {
			if (!destination->is_object()) {
				errors.push_back({ { "field", "Destination" }, { "code", "wrong_type" } });
			} else {
				if (destination->contains("Directory") && !(*destination)["Directory"].is_string())
					errors.push_back({ { "field", "Destination.Directory" }, { "code", "wrong_type" } });
				else
					checkConfiguredDirectory(*destination, "Destination.Directory", false);
				if (destination->contains("Policy") && (!(*destination)["Policy"].is_string() || (*destination)["Policy"] != "settings_default"))
					errors.push_back({ { "field", "Destination.Policy" }, { "code", "unsupported_value" } });
				if (destination->contains("Overwrite") && (!(*destination)["Overwrite"].is_string() || (*destination)["Overwrite"] != "never"))
					errors.push_back({ { "field", "Destination.Overwrite" }, { "code", "unsupported_value" } });
			}
		}
		if (const auto encoding = a_patch.find("Encoding"); encoding != a_patch.end()) {
			if (!encoding->is_object()) {
				errors.push_back({ { "field", "Encoding" }, { "code", "wrong_type" } });
			} else {
				if (encoding->contains("Format") && (!(*encoding)["Format"].is_string() || ((*encoding)["Format"] != "png" && (*encoding)["Format"] != "bmp")))
					errors.push_back({ { "field", "Encoding.Format" }, { "code", "unsupported_value" } });
				if (encoding->contains("ColourContract") && (!(*encoding)["ColourContract"].is_string() || (*encoding)["ColourContract"] != "sdr_srgb"))
					errors.push_back({ { "field", "Encoding.ColourContract" }, { "code", "unsupported_value" } });
			}
		}
		if (a_patch.contains("Clipboard") && (!a_patch["Clipboard"].is_string() ||
												 (a_patch["Clipboard"] != "none" && a_patch["Clipboard"] != "file_reference")))
			errors.push_back({ { "field", "Clipboard" }, { "code", "unsupported_value" } });
		if (const auto seq = a_patch.find("Sequence"); seq != a_patch.end()) {
			if (!seq->is_object())
				errors.push_back({ { "field", "Sequence" }, { "code", "wrong_type" } });
			else {
				checkUInt(*seq, "FrameCount", 1, kMaximumSequenceFrames, "Sequence.FrameCount");
				if (const auto destination = seq->find("Destination"); destination != seq->end()) {
					if (!destination->is_object()) {
						errors.push_back({ { "field", "Sequence.Destination" }, { "code", "wrong_type" } });
					} else {
						if (destination->contains("Directory") && !(*destination)["Directory"].is_string())
							errors.push_back({ { "field", "Sequence.Destination.Directory" }, { "code", "wrong_type" } });
						else
							checkConfiguredDirectory(*destination, "Sequence.Destination.Directory", true);
						if (destination->contains("Policy") && (!(*destination)["Policy"].is_string() || (*destination)["Policy"] != "settings_default"))
							errors.push_back({ { "field", "Sequence.Destination.Policy" }, { "code", "unsupported_value" } });
						if (destination->contains("Overwrite") && (!(*destination)["Overwrite"].is_string() || (*destination)["Overwrite"] != "never"))
							errors.push_back({ { "field", "Sequence.Destination.Overwrite" }, { "code", "unsupported_value" } });
					}
				}
				if (seq->contains("Schedule")) {
					if (!(*seq)["Schedule"].is_object())
						errors.push_back({ { "field", "Sequence.Schedule" }, { "code", "wrong_type" } });
					else {
						checkUInt((*seq)["Schedule"], "IntervalFrames", 1, 1000000, "Sequence.Schedule.IntervalFrames");
						if ((*seq)["Schedule"].contains("Basis") && (!(*seq)["Schedule"]["Basis"].is_string() || (*seq)["Schedule"]["Basis"] != "game_frames"))
							errors.push_back({ { "field", "Sequence.Schedule.Basis" }, { "code", "unsupported_value" } });
					}
				}
				if (seq->contains("Outputs")) {
					if (!(*seq)["Outputs"].is_object())
						errors.push_back({ { "field", "Sequence.Outputs" }, { "code", "wrong_type" } });
					else if ((*seq)["Outputs"].contains("SeparateEyes") && !(*seq)["Outputs"]["SeparateEyes"].is_boolean())
						errors.push_back({ { "field", "Sequence.Outputs.SeparateEyes" }, { "code", "wrong_type" } });
				}
				if (seq->contains("Packaging")) {
					if (!(*seq)["Packaging"].is_object()) {
						errors.push_back({ { "field", "Sequence.Packaging" }, { "code", "wrong_type" } });
					} else if ((*seq)["Packaging"].contains("PreviewVideo")) {
						const auto& preview = (*seq)["Packaging"]["PreviewVideo"];
						if (!preview.is_object()) {
							errors.push_back({ { "field", "Sequence.Packaging.PreviewVideo" }, { "code", "wrong_type" } });
						} else {
							if (preview.contains("Requested") && !preview["Requested"].is_boolean())
								errors.push_back({ { "field", "Sequence.Packaging.PreviewVideo.Requested" }, { "code", "wrong_type" } });
							checkUInt(preview, "FramesPerSecond", 1, 240, "Sequence.Packaging.PreviewVideo.FramesPerSecond");
						}
					}
				}
			}
		}
	}
	return { { "valid", errors.empty() }, { "errors", std::move(errors) }, { "normalizedPatch", a_patch } };
}

void ScreenshotApi::ApplySettingsPatch(ScreenshotFeature& a_feature, const json& a_patch) const
{
	if (a_patch.contains("Enabled"))
		a_feature.SetEnabled(a_patch["Enabled"].get<bool>());
	if (a_patch.contains("Destination") && a_patch["Destination"].is_object() && a_patch["Destination"].contains("Directory"))
		a_feature.screenshotPath = a_patch["Destination"]["Directory"].get<std::string>();
	if (a_patch.contains("Encoding") && a_patch["Encoding"].is_object() && a_patch["Encoding"].contains("Format"))
		a_feature.sdrUsePng = a_patch["Encoding"]["Format"].get<std::string>() != "bmp";
	if (a_patch.contains("Clipboard"))
		a_feature.copyToClipboard = a_patch["Clipboard"].get<std::string>() == "file_reference";
	if (const auto seq = a_patch.find("Sequence"); seq != a_patch.end() && seq->is_object()) {
		if (seq->contains("Destination") && (*seq)["Destination"].is_object() && (*seq)["Destination"].contains("Directory"))
			a_feature.frameCapturePath = (*seq)["Destination"]["Directory"].get<std::string>();
		if (seq->contains("FrameCount"))
			a_feature.sequenceDefaults.frameCount = (*seq)["FrameCount"].get<uint32_t>();
		if (seq->contains("Schedule") && (*seq)["Schedule"].is_object() && (*seq)["Schedule"].contains("IntervalFrames"))
			a_feature.sequenceDefaults.intervalFrames = (*seq)["Schedule"]["IntervalFrames"].get<uint32_t>();
		if (seq->contains("Outputs") && (*seq)["Outputs"].is_object() && (*seq)["Outputs"].contains("SeparateEyes"))
			a_feature.sequenceDefaults.saveSeparateEyes = (*seq)["Outputs"]["SeparateEyes"].get<bool>();
		if (seq->contains("Packaging") && (*seq)["Packaging"].is_object() && (*seq)["Packaging"].contains("PreviewVideo")) {
			const auto& preview = (*seq)["Packaging"]["PreviewVideo"];
			if (preview.contains("Requested"))
				a_feature.sequenceDefaults.writePreviewVideo = preview["Requested"].get<bool>();
			if (preview.contains("FramesPerSecond"))
				a_feature.sequenceDefaults.previewFramesPerSecond = preview["FramesPerSecond"].get<uint32_t>();
		}
	}
}

ScreenshotApi::json ScreenshotApi::BuildCapabilities(const ScreenshotFeature&) const
{
	return {
		{ "schema", "urn:csx:devbench:screenshot:1" },
		{ "sources", { "desktop_mirror", "hmd_submission" } },
		{ "views", { "source_native", "left_eye", "right_eye", "side_by_side", "framed_left", "framed_right", "framed_combined" } },
		{ "formats", { "png", "bmp" } },
		{ "colourContracts", { "sdr_srgb" } },
		{ "scheduleBases", { "game_frames", "wall_clock" } },
		{ "pathPolicies", { "settings_default", "game_relative", "absolute" } },
		{ "optional", {
						  { "separateEyeArtifacts", true },
						  { "clipboardFileReference", true },
						  { "previewVideo", { { "available", false }, { "encoders", json::array() }, { "runsAfterFrameFinalization", true } } },
					  } },
		{ "limits", {
						{ "activeSourceCaptures", 1 },
						{ "outstandingArtifacts", 2 },
						{ "pendingOperations", CSX::ScreenshotPolicy::MaximumPendingOperations },
						{ "maximumOutputsPerFrame", 4 },
						{ "maximumSequenceFrames", kMaximumSequenceFrames },
						{ "maximumSequenceDurationMs", CSX::ScreenshotPolicy::MaximumSequenceDurationMs },
						{ "maximumSequenceSpanFrames", CSX::ScreenshotPolicy::MaximumSequenceSpanFrames },
						{ "maximumRetainedTerminalRequests", kMaximumRequests },
						{ "maximumRetainedEvents", kMaximumEvents },
						{ "retentionSeconds", std::chrono::duration_cast<std::chrono::seconds>(kRetention).count() },
					} },
	};
}

ScreenshotApi::json ScreenshotApi::BuildStatus(const ScreenshotFeature& a_feature) const
{
	// Snapshot feature-owned locks before the journal lock. Capture transitions
	// deliberately acquire them in the opposite phase and then publish events.
	const auto activeRequestId = a_feature.GetActiveCaptureRequestId();
	const auto outstandingArtifacts = a_feature.GetOutstandingArtifactCount();
	auto journal = service.JournalStatus();
	std::lock_guard lock(mutex);
	json last = nullptr;
	for (auto it = requestOrder.rbegin(); it != requestOrder.rend(); ++it) {
		if (const auto found = requests.find(*it); found != requests.end() && IsTerminal(found->second.state)) {
			last = { { "requestId", found->second.requestId }, { "state", found->second.state }, { "terminalUtc", found->second.terminalUtc } };
			break;
		}
	}
	std::size_t pending = 0;
	std::size_t activeSequences = 0;
	for (const auto& [_, record] : requests)
		if (!IsTerminal(record.state))
			++pending;
	for (const auto& [_, sequence] : sequences)
		if (!sequence.finalizing)
			++activeSequences;
	journal["retainedRequests"] = requests.size();
	return {
		{ "feature", { { "loaded", a_feature.loaded }, { "enabled", a_feature.IsRuntimeEnabled() }, { "settingsSchemaVersion", 2 } } },
		{ "sourceReadiness", {
								 { "desktopPresentObserved", globals::state && globals::state->frameCount != 0 },
								 { "openVrSubmitHookInstalled", globals::game::isVR },
								 { "lastAcceptedEyeFrame", nullptr },
								 { "loadingMenuOpen", globals::state && globals::state->isLoadingMenuOpen },
							 } },
		{ "dispatcher", { { "activeAcquisitionRequestId", activeRequestId.empty() ? json(nullptr) : json(activeRequestId) }, { "pendingOperations", pending }, { "activeSequences", activeSequences } } },
		{ "worker", { { "outstandingArtifacts", outstandingArtifacts }, { "capacity", 2 }, { "completedArtifacts", completedArtifacts }, { "failedArtifacts", failedArtifacts } } },
		{ "journal", std::move(journal) },
		{ "lastTerminalRequest", std::move(last) },
	};
}

bool ScreenshotApi::IsSequenceRecording() const
{
	std::lock_guard lock(mutex);
	return std::any_of(sequences.begin(), sequences.end(), [](const auto& entry) {
		const auto& sequence = entry.second;
		return !sequence.finalizing && !sequence.stopRequested && !sequence.cancelRequested &&
		       sequence.nextOrdinal <= sequence.frameCount;
	});
}

ScreenshotApi::json ScreenshotApi::MakeEnvelope(const json& a_request, bool a_ok) const
{
	return service.MakeEnvelope(a_request, a_ok);
}

ScreenshotApi::json ScreenshotApi::MakeError(const json& a_request, std::string_view a_code, std::string_view a_message, std::string_view a_phase, bool a_retryable, std::string_view a_field, std::string_view a_requestId) const
{
	return service.MakeError(a_request, a_code, a_message, a_phase, a_retryable, a_field, a_requestId);
}

ScreenshotApi::RequestRecord& ScreenshotApi::CreateRequestLocked(std::string a_kind, const json& a_request, json a_effective, std::string a_parentRequestId, uint32_t a_sequenceOrdinal, std::string a_requestId)
{
	RequestRecord record;
	record.requestId = a_requestId.empty() ? CSX::Api::ServiceFoundation::NewId() : std::move(a_requestId);
	record.kind = std::move(a_kind);
	record.state = "accepted";
	record.clientId = a_request.value("clientId", std::string("internal"));
	record.commandId = a_request.value("commandId", CSX::Api::ServiceFoundation::NewId());
	record.parentRequestId = std::move(a_parentRequestId);
	record.sequenceOrdinal = a_sequenceOrdinal;
	record.acceptedUtc = CSX::Api::ServiceFoundation::TimestampUtc();
	record.requested = a_request;
	record.effective = std::move(a_effective);
	if (record.kind != "sequence" && record.effective.contains("outputs") && record.effective["outputs"].is_array())
		record.expectedArtifacts = std::max(1u, static_cast<uint32_t>(record.effective["outputs"].size()));
	const auto id = record.requestId;
	auto [it, inserted] = requests.emplace(id, std::move(record));
	if (!inserted)
		throw std::runtime_error("duplicate screenshot request identity");
	requestOrder.push_back(id);
	AppendEventLocked(it->second, "request.accepted");
	TrimLocked();
	return it->second;
}

void ScreenshotApi::AppendEventLocked(RequestRecord& a_record, std::string_view a_type, json a_payload)
{
	service.AppendEvent(a_record.requestId, ++a_record.eventIndex, a_type, std::move(a_payload));
}

void ScreenshotApi::TransitionLocked(RequestRecord& a_record, std::string a_state, std::string_view a_eventType, json a_payload)
{
	if (IsTerminal(a_record.state))
		return;
	a_record.state = std::move(a_state);
	if (IsTerminal(a_record.state)) {
		a_record.terminalUtc = CSX::Api::ServiceFoundation::TimestampUtc();
		a_record.terminalAt = std::chrono::steady_clock::now();
	}
	AppendEventLocked(a_record, a_eventType, std::move(a_payload));
}

ScreenshotApi::json ScreenshotApi::MakeReceipt(const RequestRecord& a_record) const
{
	json receipt = {
		{ "requestId", a_record.requestId },
		{ "kind", a_record.kind },
		{ "state", a_record.state },
		{ "clientId", a_record.clientId },
		{ "commandId", a_record.commandId },
		{ "acceptedUtc", a_record.acceptedUtc },
		{ "terminalUtc", a_record.terminalUtc.empty() ? json(nullptr) : json(a_record.terminalUtc) },
		{ "requested", a_record.requested },
		{ "effective", a_record.effective },
		{ "actual", a_record.actual },
		{ "artifacts", a_record.artifacts },
		{ "warnings", a_record.warnings },
		{ "errors", a_record.errors },
		{ "error", a_record.error },
		{ "acknowledged", a_record.acknowledged },
		{ "artifactProgress", { { "expected", a_record.expectedArtifacts }, { "terminal", a_record.terminalArtifacts }, { "successful", a_record.successfulArtifacts } } },
	};
	if (!a_record.parentRequestId.empty()) {
		receipt["parentRequestId"] = a_record.parentRequestId;
		receipt["sequenceOrdinal"] = a_record.sequenceOrdinal;
	}
	return receipt;
}

ScreenshotApi::json ScreenshotApi::MakeSequenceReceipt(const RequestRecord& a_record, const SequenceRecord* a_sequence) const
{
	auto receipt = MakeReceipt(a_record);
	if (!a_sequence)
		return receipt;
	receipt["counts"] = {
		{ "requested", a_sequence->frameCount },
		{ "scheduled", a_sequence->scheduled },
		{ "acquired", a_sequence->acquired },
		{ "written", a_sequence->written },
		{ "dropped", a_sequence->dropped },
		{ "failed", a_sequence->failed },
		{ "cancelled", a_sequence->cancelled },
		{ "inFlight", a_sequence->inFlight },
	};
	receipt["manifest"] = {
		{ "partialPath", a_sequence->frameManifest ? json(PathUtf8(a_sequence->partialManifestPath)) : json(nullptr) },
		{ "finalPath", a_sequence->frameManifest &&
							   a_sequence->packaging["frameManifest"].value("state", std::string{}) == "written" ?
						   json(PathUtf8(a_sequence->finalManifestPath)) :
						   json(nullptr) },
	};
	receipt["packaging"] = a_sequence->packaging;
	return receipt;
}

ScreenshotApi::json ScreenshotApi::LookupReceiptLocked(std::string_view a_requestId) const
{
	const auto found = requests.find(std::string(a_requestId));
	if (found == requests.end())
		return nullptr;
	const auto sequence = sequences.find(found->second.requestId);
	return MakeSequenceReceipt(found->second, sequence == sequences.end() ? nullptr : &sequence->second);
}

void ScreenshotApi::TrimLocked()
{
	const auto now = std::chrono::steady_clock::now();

	auto eraseRequest = [this](auto position) {
		const auto id = *position;
		requests.erase(id);
		sequences.erase(id);
		service.ForgetRequest(id);
		return requestOrder.erase(position);
	};
	for (auto position = requestOrder.begin(); position != requestOrder.end();) {
		const auto found = requests.find(*position);
		if (found == requests.end()) {
			position = requestOrder.erase(position);
			continue;
		}
		const bool expired = IsTerminal(found->second.state) && found->second.terminalAt != std::chrono::steady_clock::time_point{} &&
		                     now - found->second.terminalAt >= kRetention;
		if (IsTerminal(found->second.state) && (found->second.acknowledged || expired))
			position = eraseRequest(position);
		else
			++position;
	}
	while (requestOrder.size() > kMaximumRequests) {
		auto position = std::find_if(requestOrder.begin(), requestOrder.end(), [this](const std::string& id) {
			const auto found = requests.find(id);
			return found == requests.end() || IsTerminal(found->second.state);
		});
		if (position == requestOrder.end())
			break;
		eraseRequest(position);
	}
	service.Trim();
}

std::size_t ScreenshotApi::CountPendingOperationsLocked() const
{
	return std::ranges::count_if(requests, [](const auto& entry) {
		return entry.second.kind != "sequence_frame" && !IsTerminal(entry.second.state);
	});
}

void ScreenshotApi::OnSourceWaiting(
	std::string_view a_requestId,
	std::string_view a_actualSourceKind)
{
	std::lock_guard lock(mutex);
	if (auto found = requests.find(std::string(a_requestId)); found != requests.end() && !IsTerminal(found->second.state)) {
		auto& actualSource = found->second.actual["source"];
		if (!actualSource.is_object())
			actualSource = found->second.effective.value("source", json::object());
		actualSource["kind"] = a_actualSourceKind;
		TransitionLocked(found->second, "waiting_source", "source.waiting");
	}
}

void ScreenshotApi::OnSourceFallback(
	std::string_view a_requestId,
	std::string_view a_reason,
	std::string_view a_actualSourceKind)
{
	std::lock_guard lock(mutex);
	if (auto found = requests.find(std::string(a_requestId)); found != requests.end()) {
		found->second.warnings.push_back({ { "code", "source_fallback" }, { "message", a_reason } });
		found->second.actual["fallbacks"].push_back({ { "reason", a_reason } });
		found->second.actual["source"]["fallbackApplied"] = true;
		found->second.actual["source"]["fallbackReason"] = a_reason;
		if (!a_actualSourceKind.empty())
			found->second.actual["source"]["kind"] = a_actualSourceKind;
		AppendEventLocked(found->second, "source.fallback", { { "reason", a_reason } });
	}
}

void ScreenshotApi::OnArtifactQueued(std::string_view a_requestId, const std::filesystem::path& a_path)
{
	std::lock_guard lock(mutex);
	if (auto found = requests.find(std::string(a_requestId)); found != requests.end() && !IsTerminal(found->second.state)) {
		found->second.sourceAcquired = true;
		TransitionLocked(found->second, "queued", "artifact.queued", { { "path", PathUtf8(a_path) } });
	}
}

void ScreenshotApi::OnArtifactEncoding(std::string_view a_requestId)
{
	std::lock_guard lock(mutex);
	if (auto found = requests.find(std::string(a_requestId)); found != requests.end() && !IsTerminal(found->second.state))
		TransitionLocked(found->second, "encoding", "artifact.encoding");
}

void ScreenshotApi::OnArtifactTerminal(
	std::string_view a_requestId,
	bool a_success,
	const std::filesystem::path& a_path,
	std::string_view a_error,
	json a_actual)
{
	if (a_requestId.empty())
		return;
	const auto artifact = a_success ? DescribeCommittedArtifact(a_path) : json(nullptr);
	std::lock_guard lock(mutex);
	const auto found = requests.find(std::string(a_requestId));
	if (found == requests.end() || IsTerminal(found->second.state))
		return;
	auto& record = found->second;
	if (record.terminalArtifacts >= record.expectedArtifacts)
		return;
	if (a_success) {
		auto committedArtifact = artifact;
		if (!a_actual.empty())
			committedArtifact["actual"] = a_actual;
		record.artifacts.push_back(committedArtifact);
		if (!a_actual.empty())
			record.actual["artifacts"].push_back(a_actual);
		if (artifact.contains("integrityError"))
			record.warnings.push_back({ { "code", "artifact_hash_failed" }, { "message", artifact["integrityError"] } });
		++record.successfulArtifacts;
		++completedArtifacts;
		AppendEventLocked(record, "artifact.written", committedArtifact);
	} else {
		++failedArtifacts;
		const json error = { { "code", "artifact_failed" }, { "message", a_error.empty() ? "screenshot artifact failed" : std::string(a_error) }, { "phase", "encoding" }, { "path", a_path.empty() ? json(nullptr) : json(PathUtf8(a_path)) } };
		record.errors.push_back(error);
		if (record.error.is_null())
			record.error = error;
		AppendEventLocked(record, "artifact.failed", error);
	}
	++record.terminalArtifacts;
	if (record.terminalArtifacts < record.expectedArtifacts)
		return;

	const bool allSucceeded = record.successfulArtifacts == record.expectedArtifacts;
	std::string terminal;
	if (record.cancelRequested)
		terminal = record.successfulArtifacts == 0 ? "cancelled" : "cancelled_partial";
	else if (!allSucceeded)
		terminal = record.successfulArtifacts == 0 ? "failed" : "failed_partial";
	else
		terminal = record.warnings.empty() ? "completed" : "completed_with_warnings";
	TransitionLocked(record, terminal, "request.terminal");
	FinishSequenceChildLocked(record);
}

void ScreenshotApi::OnSourceTerminal(std::string_view a_requestId, std::string_view a_state, std::string_view a_error)
{
	if (a_requestId.empty())
		return;
	std::lock_guard lock(mutex);
	const auto found = requests.find(std::string(a_requestId));
	if (found == requests.end() || IsTerminal(found->second.state))
		return;
	found->second.error = a_error.empty() ? json(nullptr) : json({ { "code", a_error }, { "message", a_error }, { "phase", "source" } });
	if (!found->second.error.is_null())
		found->second.errors.push_back(found->second.error);
	TransitionLocked(found->second, std::string(a_state), "request.terminal", { { "reason", a_error } });
	FinishSequenceChildLocked(found->second);
}

void ScreenshotApi::FinishSequenceChildLocked(RequestRecord& a_child)
{
	if (a_child.parentRequestId.empty() || a_child.sequenceFinished)
		return;
	a_child.sequenceFinished = true;
	const auto sequenceIt = sequences.find(a_child.parentRequestId);
	if (sequenceIt == sequences.end())
		return;
	auto& sequence = sequenceIt->second;
	if (sequence.inFlight > 0)
		--sequence.inFlight;
	if (sequence.activeChildRequestId == a_child.requestId)
		sequence.activeChildRequestId.clear();
	if (a_child.sourceAcquired)
		++sequence.acquired;
	if (a_child.successfulArtifacts != 0) {
		++sequence.written;
		sequence.consecutiveSkips = 0;
	}
	const auto primaryErrorCode = a_child.error.is_object() ? a_child.error.value("code", std::string{}) : std::string{};
	if (a_child.state == "dropped" || primaryErrorCode == "source_busy" || primaryErrorCode == "encoder_backpressure") {
		++sequence.dropped;
		++sequence.consecutiveSkips;
	} else if (a_child.state == "cancelled" || a_child.state == "cancelled_partial") {
		++sequence.cancelled;
	} else if (a_child.state == "failed" || a_child.state == "failed_partial") {
		++sequence.failed;
	}
	sequence.children.push_back({
		{ "ordinal", a_child.sequenceOrdinal },
		{ "requestId", a_child.requestId },
		{ "state", a_child.state },
		{ "scheduledEngineFrame", a_child.scheduledEngineFrame },
		{ "scheduledTimestampUs", a_child.scheduledTimestampUs },
		{ "requested", a_child.requested },
		{ "effective", a_child.effective },
		{ "actual", a_child.actual },
		{ "artifacts", a_child.artifacts },
		{ "warnings", a_child.warnings },
		{ "errors", a_child.errors },
		{ "error", a_child.error },
	});
	const bool childSucceeded = a_child.state == "completed" || a_child.state == "completed_with_warnings";
	if ((sequence.failurePolicy == "abort" && !childSucceeded) ||
		(sequence.backpressurePolicy == "abort" && sequence.dropped != 0) ||
		(sequence.maximumConsecutiveSkips != 0 && sequence.consecutiveSkips >= sequence.maximumConsecutiveSkips)) {
		sequence.stopRequested = true;
	}
	if (sequence.children.size() >= sequence.nextCheckpointChildCount) {
		QueueSequenceManifestLocked(sequence, false);
		sequence.nextCheckpointChildCount = std::min<std::size_t>(
			kMaximumSequenceFrames,
			sequence.nextCheckpointChildCount * 2);
	}
	TryFinalizeSequenceLocked(sequence);
}

void ScreenshotApi::TryFinalizeSequenceLocked(SequenceRecord& a_sequence)
{
	const bool schedulingComplete = a_sequence.nextOrdinal > a_sequence.frameCount;
	if (!(schedulingComplete || a_sequence.stopRequested || a_sequence.cancelRequested) || a_sequence.inFlight != 0)
		return;
	if (a_sequence.finalizing)
		return;
	a_sequence.finalizing = true;
	const auto parent = requests.find(a_sequence.requestId);
	if (parent == requests.end())
		return;
	TransitionLocked(parent->second, "finalizing", "sequence.finalizing");
	if (a_sequence.frameManifest)
		QueueSequenceManifestLocked(a_sequence, true);
	else
		FinalizeSequenceLocked(a_sequence, nullptr);
}

void ScreenshotApi::FinalizeSequenceLocked(
	SequenceRecord& a_sequence,
	const ManifestResult* a_manifestResult)
{
	const auto parent = requests.find(a_sequence.requestId);
	if (parent == requests.end() || IsTerminal(parent->second.state))
		return;
	const bool manifestWritten = !a_sequence.frameManifest ||
	                             (a_manifestResult && a_manifestResult->success);
	if (a_sequence.frameManifest) {
		parent->second.expectedArtifacts = 1;
		parent->second.terminalArtifacts = 1;
		if (manifestWritten) {
			parent->second.artifacts.push_back(a_manifestResult->artifact);
			parent->second.successfulArtifacts = 1;
			if (a_manifestResult->artifact.contains("integrityError"))
				parent->second.warnings.push_back({ { "code", "artifact_hash_failed" }, { "message", a_manifestResult->artifact["integrityError"] } });
		} else {
			parent->second.successfulArtifacts = 0;
			parent->second.error = {
				{ "code", "manifest_failed" },
				{ "message", a_manifestResult ? a_manifestResult->error : "final manifest was not committed" },
				{ "phase", "packaging" },
			};
			parent->second.errors.push_back(parent->second.error);
		}
	}
	std::string terminal;
	if (!manifestWritten)
		terminal = a_sequence.written == 0 ? "failed" : "failed_partial";
	else if (a_sequence.cancelRequested)
		terminal = a_sequence.written == 0 ? "cancelled" : "cancelled_partial";
	else if (a_sequence.stopRequested)
		terminal = "stopped";
	else if (a_sequence.failed != 0)
		terminal = a_sequence.written == 0 ? "failed" : "failed_partial";
	else if (a_sequence.dropped != 0 || !parent->second.warnings.empty() ||
			 a_sequence.packaging["previewVideo"].value("state", std::string{}) == "unsupported")
		terminal = "completed_with_warnings";
	else
		terminal = "completed";
	TransitionLocked(parent->second, terminal, "request.terminal", { { "manifestPath", a_sequence.frameManifest && manifestWritten ? json(PathUtf8(a_sequence.finalManifestPath)) : json(nullptr) } });
}

ScreenshotApi::json ScreenshotApi::BuildSequenceManifestLocked(
	const SequenceRecord& a_sequence,
	bool a_final) const
{
	json packaging = a_sequence.packaging;
	packaging["frameManifest"] = {
		{ "requested", true },
		{ "state", a_final ? "written" : "partial" },
		{ "path", PathUtf8(a_final ? a_sequence.finalManifestPath : a_sequence.partialManifestPath) },
	};
	const auto parent = requests.find(a_sequence.requestId);
	const auto updatedUtc = CSX::Api::ServiceFoundation::TimestampUtc();
	std::string terminalOutcome;
	if (a_sequence.cancelRequested)
		terminalOutcome = a_sequence.written == 0 ? "cancelled" : "cancelled_partial";
	else if (a_sequence.stopRequested)
		terminalOutcome = "stopped";
	else if (a_sequence.failed != 0)
		terminalOutcome = a_sequence.written == 0 ? "failed" : "failed_partial";
	else if (a_sequence.dropped != 0 ||
			 (parent != requests.end() && !parent->second.warnings.empty()) ||
			 a_sequence.packaging["previewVideo"].value("state", std::string{}) == "unsupported")
		terminalOutcome = "completed_with_warnings";
	else
		terminalOutcome = "completed";
	const bool fallbacksPresent = std::ranges::any_of(a_sequence.children, [](const json& child) {
		return std::ranges::any_of(child.value("warnings", json::array()), [](const json& warning) {
			return warning.value("code", std::string{}) == "source_fallback";
		});
	});
	return {
		{ "contract", { { "name", "csx.screenshot" }, { "major", kContractMajor }, { "minor", kContractMinor }, { "schemaRevision", kSchemaRevision } } },
		{ "producer", BuildProvenance::GetProducer() },
		{ "sessionId", service.SessionId() },
		{ "requestId", a_sequence.requestId },
		{ "state", a_final ? "final" : "partial" },
		{ "terminalOutcome", a_final ? json(terminalOutcome) : json(nullptr) },
		{ "client", parent != requests.end() ? json({ { "clientId", parent->second.clientId }, { "commandId", parent->second.commandId } }) : json::object() },
		{ "acceptedUtc", parent != requests.end() ? json(parent->second.acceptedUtc) : json(nullptr) },
		{ "completedUtc", a_final ? json(updatedUtc) : json(nullptr) },
		{ "requested", a_sequence.requested },
		{ "effective", a_sequence.capture },
		{ "actual", { { "children", a_sequence.children.size() }, { "fallbacksPresent", fallbacksPresent } } },
		{ "counts", { { "requested", a_sequence.frameCount }, { "scheduled", a_sequence.scheduled }, { "acquired", a_sequence.acquired }, { "written", a_sequence.written }, { "dropped", a_sequence.dropped }, { "failed", a_sequence.failed }, { "cancelled", a_sequence.cancelled }, { "inFlight", a_sequence.inFlight } } },
		{ "children", a_sequence.children },
		{ "warnings", parent != requests.end() ? parent->second.warnings : json::array() },
		{ "errors", parent != requests.end() ? parent->second.errors : json::array() },
		{ "packaging", packaging },
		{ "updatedUtc", updatedUtc },
	};
}

void ScreenshotApi::QueueSequenceManifestLocked(SequenceRecord& a_sequence, bool a_final)
{
	if (!a_sequence.frameManifest)
		return;
	ManifestJob job{
		.requestId = a_sequence.requestId,
		.generation = ++a_sequence.manifestGeneration,
		.final = a_final,
		.destination = a_final ? a_sequence.finalManifestPath : a_sequence.partialManifestPath,
		.partialPath = a_sequence.partialManifestPath,
		.document = BuildSequenceManifestLocked(a_sequence, a_final),
	};
	if (a_final)
		a_sequence.finalManifestGeneration = job.generation;
	const auto state = manifestWorkerState;
	{
		std::lock_guard workerLock(state->mutex);
		if (!a_final) {
			std::erase_if(state->jobs, [&](const ManifestJob& queued) {
				if (queued.requestId != job.requestId || queued.final)
					return false;
				if (state->outstanding > 0)
					--state->outstanding;
				return true;
			});
		}
		state->jobs.push_back(std::move(job));
		++state->outstanding;
	}
	state->condition.notify_one();
}

void ScreenshotApi::DrainManifestResultsLocked()
{
	std::deque<ManifestResult> completed;
	{
		std::lock_guard workerLock(manifestWorkerState->mutex);
		completed.swap(manifestWorkerState->results);
	}
	for (auto& result : completed) {
		const auto sequence = sequences.find(result.requestId);
		if (sequence == sequences.end())
			continue;
		auto& record = sequence->second;
		if (result.final) {
			if (result.generation != record.finalManifestGeneration)
				continue;
			record.packaging["frameManifest"] = result.success ?
			                                        json({ { "requested", true }, { "state", "written" }, { "path", PathUtf8(result.destination) } }) :
			                                        json({ { "requested", true }, { "state", "failed" }, { "error", result.error } });
			FinalizeSequenceLocked(record, &result);
		} else if (result.success && result.generation <= record.manifestGeneration) {
			record.packaging["frameManifest"] = {
				{ "requested", true }, { "state", "partial" }, { "path", PathUtf8(result.destination) }
			};
		} else if (!result.success) {
			logger::warn("Screenshot partial manifest checkpoint failed: {}", result.error);
		}
	}
}

std::optional<ScreenshotApi::DueFrame> ScreenshotApi::PrepareDueFrameLocked(uint64_t a_engineFrame)
{
	const auto now = std::chrono::steady_clock::now();
	for (auto& [_, sequence] : sequences) {
		if (sequence.finalizing || sequence.stopRequested || sequence.cancelRequested || sequence.inFlight != 0 || sequence.nextOrdinal > sequence.frameCount)
			continue;
		const bool due = sequence.scheduleBasis == "game_frames" ? a_engineFrame >= sequence.nextEngineFrame : now >= sequence.nextWallClock;
		if (!due)
			continue;
		DueFrame dueFrame;
		dueFrame.parentRequestId = sequence.requestId;
		dueFrame.childRequestId = CSX::Api::ServiceFoundation::NewId();
		dueFrame.ordinal = sequence.nextOrdinal++;
		dueFrame.capture = sequence.capture;
		dueFrame.capture["destination"] = {
			{ "policy", "absolute" },
			{ "directory", PathUtf8(sequence.directory) },
			{ "baseName", std::format("frame_{:06}", dueFrame.ordinal) },
			{ "overwrite", "never" },
		};
		++sequence.scheduled;
		++sequence.inFlight;
		sequence.activeChildRequestId = dueFrame.childRequestId;
		sequence.nextEngineFrame = a_engineFrame + sequence.intervalFrames;
		sequence.nextWallClock = now + std::chrono::milliseconds(sequence.intervalMs);
		json childRequest = {
			{ "action", "capture" },
			{ "clientId", "sequence:" + sequence.requestId },
			{ "commandId", std::format("frame:{}", dueFrame.ordinal) },
			{ "contractMajor", kContractMajor },
		};
		auto& child = CreateRequestLocked("sequence_frame", childRequest, dueFrame.capture, sequence.requestId, dueFrame.ordinal, dueFrame.childRequestId);
		child.scheduledEngineFrame = a_engineFrame;
		child.scheduledTimestampUs = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
		AppendEventLocked(child, "sequence.frame_scheduled", {
																 { "ordinal", dueFrame.ordinal },
																 { "engineFrame", a_engineFrame },
																 { "monotonicTimestampUs", child.scheduledTimestampUs },
															 });
		return dueFrame;
	}
	return std::nullopt;
}

void ScreenshotApi::Tick(ScreenshotFeature& a_feature, uint64_t a_engineFrame)
{
	std::optional<DueFrame> due;
	{
		std::lock_guard lock(mutex);
		DrainManifestResultsLocked();
		if (!a_feature.IsRuntimeEnabled()) {
			for (auto& [_, sequence] : sequences) {
				if (!sequence.finalizing)
					sequence.cancelRequested = true;
			}
		}
		for (auto& [_, sequence] : sequences)
			TryFinalizeSequenceLocked(sequence);
		if (a_feature.IsRuntimeEnabled() && acceptingRequests)
			due = PrepareDueFrameLocked(a_engineFrame);
	}
	if (!due)
		return;
	if (!a_feature.TryStartApiCapture(due->childRequestId, due->capture, due->parentRequestId, due->ordinal))
		OnSourceTerminal(due->childRequestId, "dropped", "source_busy");
}

void ScreenshotApi::OnFeatureDisabled(std::string_view a_reason)
{
	std::lock_guard lock(mutex);
	for (auto& [_, sequence] : sequences) {
		if (sequence.finalizing)
			continue;
		sequence.cancelRequested = true;
		if (auto parent = requests.find(sequence.requestId); parent != requests.end() && !IsTerminal(parent->second.state))
			TransitionLocked(parent->second, "cancel_requested", "request.cancel_requested", { { "reason", a_reason } });
		TryFinalizeSequenceLocked(sequence);
	}
}

void ScreenshotApi::BeginShutdown(std::string_view a_reason)
{
	std::lock_guard lock(mutex);
	acceptingRequests = false;
	DrainManifestResultsLocked();
	for (auto& [_, sequence] : sequences) {
		if (sequence.finalizing)
			continue;
		sequence.cancelRequested = true;
		if (auto parent = requests.find(sequence.requestId); parent != requests.end() && !IsTerminal(parent->second.state)) {
			parent->second.error = { { "code", "shutdown" }, { "message", a_reason }, { "phase", "shutdown" } };
			parent->second.errors.push_back(parent->second.error);
			TransitionLocked(parent->second, "cancel_requested", "request.cancel_requested", { { "reason", a_reason } });
		}
		TryFinalizeSequenceLocked(sequence);
	}
}

bool ScreenshotApi::DrainForShutdown(std::chrono::milliseconds a_timeout)
{
	const auto deadline = std::chrono::steady_clock::now() + a_timeout;
	bool drained = false;
	{
		std::unique_lock lock(manifestWorkerState->mutex);
		drained = manifestWorkerState->condition.wait_until(lock, deadline, [this] {
			return manifestWorkerState->outstanding == 0;
		});
	}
	{
		std::lock_guard lock(mutex);
		DrainManifestResultsLocked();
	}
	return drained;
}

std::filesystem::path ScreenshotApi::ResolveDestinationDirectory(
	const ScreenshotFeature& a_feature,
	const json& a_capture,
	bool a_sequence)
{
	const auto destination = a_capture.value("destination", json::object());
	const auto policy = destination.value("policy", std::string("settings_default"));
	wchar_t executable[MAX_PATH]{};
	const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		throw std::runtime_error("game directory is unavailable");
	const auto gameDirectory = std::filesystem::weakly_canonical(std::filesystem::path(executable).parent_path());

	std::filesystem::path requested;
	if (policy == "settings_default") {
		requested = a_sequence ? a_feature.frameCapturePath : a_feature.screenshotPath;
		return ResolveConfiguredCaptureDirectory(requested, a_sequence);
	}

	const auto directory = destination.value("directory", std::string{});
	if (directory.empty())
		throw std::runtime_error("destination.directory is required by the selected policy");
	requested = std::filesystem::u8path(directory);
	if (policy == "absolute") {
		if (!requested.is_absolute())
			throw std::runtime_error("absolute destination policy requires an absolute directory");
		return std::filesystem::weakly_canonical(requested);
	}
	if (requested.is_absolute())
		throw std::runtime_error("game_relative destination policy requires a relative directory");
	const auto resolved = std::filesystem::weakly_canonical(gameDirectory / requested);
	const auto relative = std::filesystem::relative(resolved, gameDirectory);
	if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
		throw std::runtime_error("game_relative destination escapes the game directory");
	return resolved;
}
