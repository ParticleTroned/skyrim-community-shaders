#include "Features/ScreenshotApi.h"

#include "Features/ScreenshotFeature.h"
#include "BuildProvenance.h"
#include "Globals.h"
#include "ScreenshotDevBenchBridge.h"
#include "State.h"
#include "VRAPI/CSpluginapi.h"

#include <Plugin.h>

#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <cmath>
#include <fstream>
#include <format>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace
{
	using json = nlohmann::json;

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
		std::array<UCHAR, 1024 * 1024> buffer{};
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
		const auto finishStatus = hashStatus < 0 ? hashStatus : BCryptFinishHash(
			hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
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
			{ "path", a_path.string() },
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
	service.SetServerMetadataProvider([this] {
		auto metadata = BuildProvenance::GetProducer();
		metadata.update(json{
			{ "csxBuild", CSBuildNumber },
			{ "csxVersion", std::string(Plugin::VERSION_LABEL) },
			{ "featureVersion", "1.0.0" },
			{ "serviceSessionId", service.SessionId() },
			{ "runtime", {
				{ "game", globals::game::isVR ? "SkyrimVR" : "SkyrimSE" },
				{ "presentation", globals::game::isVR ? "openvr" : "dxgi" }, { "hmd", "unknown" },
			} },
			{ "devBenchBuilt", ScreenshotDevBenchBridge::IsBuilt() },
			{ "devBenchRegistered", ScreenshotDevBenchBridge::IsRegistered() },
		});
		return metadata;
	});
}

ScreenshotApi::json ScreenshotApi::HandleRequest(ScreenshotFeature& a_feature, const json& a_request)
{
	return service.Dispatch(
		a_request,
		[this, &a_feature](const json& validatedRequest) {
			{
				std::lock_guard lock(mutex);
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
			descriptor = NormalizeCaptureDescriptor(a_feature, descriptorRequest);
		} catch (const std::exception& e) {
			const std::string message = e.what();
			const bool pathError = message.find("destination") != std::string::npos || message.find("directory") != std::string::npos;
			return MakeError(a_request, pathError ? "unsafe_path" : "invalid_capture_descriptor", message);
		}
		if (sequenceUsesSettings && a_feature.sequenceDefaults.saveSeparateEyes &&
			descriptor["source"].value("kind", std::string{}) == "hmd_submission") {
			const auto encoding = descriptor["outputs"].front().value("encoding", json::object());
			auto containsView = [&descriptor](std::string_view a_view) {
				return std::any_of(descriptor["outputs"].begin(), descriptor["outputs"].end(), [a_view](const json& output) {
					return output.value("view", std::string{}) == a_view;
				});
			};
			if (!containsView("left_eye"))
				descriptor["outputs"].push_back({ { "view", "left_eye" }, { "encoding", encoding }, { "nameSuffix", "left" } });
			if (!containsView("right_eye"))
				descriptor["outputs"].push_back({ { "view", "right_eye" }, { "encoding", encoding }, { "nameSuffix", "right" } });
		}

		SequenceRecord sequence;
		sequence.capture = descriptor;
		sequence.frameCount = frameCount;
		const auto schedule = requestedSequence.value("schedule", json::object());
		sequence.scheduleBasis = schedule.value("basis", std::string("game_frames"));
		if (sequence.scheduleBasis != "game_frames" && sequence.scheduleBasis != "wall_clock")
			return MakeError(a_request, "invalid_field", "schedule basis must be game_frames or wall_clock", "validation", false, "sequence.schedule.basis");
		sequence.intervalFrames = std::max(1u, schedule.value("intervalFrames", a_feature.sequenceDefaults.intervalFrames));
		sequence.startDelayFrames = schedule.value("startDelayFrames", 0u);
		sequence.intervalMs = std::max(1u, schedule.value("intervalMs", 100u));
		if (static_cast<uint64_t>(sequence.intervalMs) * frameCount > kMaximumSequenceDurationMs)
			return MakeError(a_request, "invalid_field", "sequence wall-clock duration exceeds the advertised limit", "validation", false, "sequence.schedule.intervalMs");
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
			auto& record = CreateRequestLocked("sequence", a_request, requestedSequence);
			requestId = record.requestId;
			sequence.requestId = requestId;
			sequence.nextEngineFrame = (globals::state ? globals::state->frameCount : 0u) + sequence.startDelayFrames;
			sequence.nextWallClock = std::chrono::steady_clock::now() + std::chrono::milliseconds(schedule.value("startDelayMs", 0u));
			sequence.directory = ResolveDestinationDirectory(a_feature, descriptor) /
				("CS_sequence_" + ShortId(requestId));
			sequence.partialManifestPath = sequence.directory / "sequence.json.partial";
			sequence.finalManifestPath = sequence.directory / "sequence.json";
			sequences.emplace(requestId, std::move(sequence));
			auto& stored = sequences.at(requestId);
			TransitionLocked(record, "running", "sequence.started", {
				{ "frameCount", frameCount }, { "manifestPath", stored.partialManifestPath.string() }
			});
			WriteSequenceManifestLocked(stored, false);
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
		const bool cancelledWaitingSource = action == "request_cancel" && a_feature.CancelApiCapture(requestId);
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
				TransitionLocked(found->second, "cancel_requested", "request.cancel_requested");
			}
			TryFinalizeSequenceLocked(sequence->second);
		} else {
			found->second.cancelRequested = true;
			if (cancelledWaitingSource) {
				TransitionLocked(found->second, "cancelled", "request.terminal", { { "reason", "client_requested" } });
				FinishSequenceChildLocked(found->second, false, {}, "client_requested");
			} else {
				TransitionLocked(found->second, "cancel_requested", "request.cancel_requested", { { "irreversibleWorkMayFinish", true } });
			}
		}
		auto response = MakeEnvelope(a_request, true);
		response["result"] = LookupReceiptLocked(requestId);
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

ScreenshotApi::json ScreenshotApi::NormalizeCaptureDescriptor(const ScreenshotFeature& a_feature, const json& a_request) const
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
	if (outputs.empty()) {
		outputs.push_back({
			{ "view", useSettings ? ViewName(a_feature) : "source_native" },
			{ "dominantEye", a_feature.vrFramedDominantEye == vr::Eye_Right ? "right" : "left" },
			{ "encoding", { { "format", a_feature.sdrUsePng ? "png" : "bmp" }, { "colourContract", "sdr_srgb" } } },
		});
	}
	if (!outputs.is_array() || outputs.empty() || outputs.size() > 4)
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
		if (suffix.empty() || suffix.size() > 48 || suffix.find_first_of("/\\") != std::string::npos || !suffixes.insert(suffix).second)
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
		if (baseName.empty() || baseName.size() > 96 || baseName.find_first_of("/\\") != std::string::npos)
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
	normalized["destination"]["resolvedDirectory"] = ResolveDestinationDirectory(a_feature, normalized).string();
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
		if (!object.contains(key)) return;
		if (!object[key].is_number_unsigned() && !object[key].is_number_integer())
			errors.push_back({ { "field", path }, { "code", "wrong_type" } });
		else {
			const auto value = object[key].get<int64_t>();
			if (value < min || value > max)
				errors.push_back({ { "field", path }, { "code", "out_of_range" } });
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
	if (a_patch.contains("Enabled")) a_feature.SetEnabled(a_patch["Enabled"].get<bool>());
	if (a_patch.contains("Destination") && a_patch["Destination"].is_object() && a_patch["Destination"].contains("Directory"))
		a_feature.screenshotPath = a_patch["Destination"]["Directory"].get<std::string>();
	if (a_patch.contains("Encoding") && a_patch["Encoding"].is_object() && a_patch["Encoding"].contains("Format"))
		a_feature.sdrUsePng = a_patch["Encoding"]["Format"].get<std::string>() != "bmp";
	if (a_patch.contains("Clipboard")) a_feature.copyToClipboard = a_patch["Clipboard"].get<std::string>() == "file_reference";
	if (const auto seq = a_patch.find("Sequence"); seq != a_patch.end() && seq->is_object()) {
		if (seq->contains("FrameCount")) a_feature.sequenceDefaults.frameCount = (*seq)["FrameCount"].get<uint32_t>();
		if (seq->contains("Schedule") && (*seq)["Schedule"].is_object() && (*seq)["Schedule"].contains("IntervalFrames"))
			a_feature.sequenceDefaults.intervalFrames = (*seq)["Schedule"]["IntervalFrames"].get<uint32_t>();
		if (seq->contains("Outputs") && (*seq)["Outputs"].is_object() && (*seq)["Outputs"].contains("SeparateEyes"))
			a_feature.sequenceDefaults.saveSeparateEyes = (*seq)["Outputs"]["SeparateEyes"].get<bool>();
		if (seq->contains("Packaging") && (*seq)["Packaging"].is_object() && (*seq)["Packaging"].contains("PreviewVideo")) {
			const auto& preview = (*seq)["Packaging"]["PreviewVideo"];
			if (preview.contains("Requested")) a_feature.sequenceDefaults.writePreviewVideo = preview["Requested"].get<bool>();
			if (preview.contains("FramesPerSecond")) a_feature.sequenceDefaults.previewFramesPerSecond = preview["FramesPerSecond"].get<uint32_t>();
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
			{ "activeSourceCaptures", 1 }, { "outstandingArtifacts", 2 }, { "pendingOperations", 64 },
			{ "maximumOutputsPerFrame", 4 }, { "maximumSequenceFrames", kMaximumSequenceFrames },
			{ "maximumSequenceDurationMs", kMaximumSequenceDurationMs },
			{ "maximumRetainedTerminalRequests", kMaximumRequests }, { "maximumRetainedEvents", kMaximumEvents },
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
		if (!IsTerminal(record.state)) ++pending;
	for (const auto& [_, sequence] : sequences)
		if (!sequence.finalizing) ++activeSequences;
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
		{ "requestId", a_record.requestId }, { "kind", a_record.kind }, { "state", a_record.state },
		{ "clientId", a_record.clientId }, { "commandId", a_record.commandId }, { "acceptedUtc", a_record.acceptedUtc },
		{ "terminalUtc", a_record.terminalUtc.empty() ? json(nullptr) : json(a_record.terminalUtc) },
		{ "effective", a_record.effective }, { "artifacts", a_record.artifacts }, { "warnings", a_record.warnings },
		{ "error", a_record.error }, { "acknowledged", a_record.acknowledged },
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
		{ "requested", a_sequence->frameCount }, { "scheduled", a_sequence->scheduled }, { "acquired", a_sequence->acquired },
		{ "written", a_sequence->written }, { "dropped", a_sequence->dropped }, { "failed", a_sequence->failed }, { "inFlight", a_sequence->inFlight },
	};
	receipt["manifest"] = {
		{ "partialPath", a_sequence->frameManifest ? json(a_sequence->partialManifestPath.string()) : json(nullptr) },
		{ "finalPath", a_sequence->frameManifest && std::filesystem::exists(a_sequence->finalManifestPath) ? json(a_sequence->finalManifestPath.string()) : json(nullptr) },
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

void ScreenshotApi::OnSourceWaiting(std::string_view a_requestId)
{
	std::lock_guard lock(mutex);
	if (auto found = requests.find(std::string(a_requestId)); found != requests.end() && !IsTerminal(found->second.state))
		TransitionLocked(found->second, "waiting_source", "source.waiting");
}

void ScreenshotApi::OnSourceFallback(std::string_view a_requestId, std::string_view a_reason)
{
	std::lock_guard lock(mutex);
	if (auto found = requests.find(std::string(a_requestId)); found != requests.end()) {
		found->second.warnings.push_back({ { "code", "source_fallback" }, { "message", a_reason } });
		AppendEventLocked(found->second, "source.fallback", { { "reason", a_reason } });
	}
}

void ScreenshotApi::OnArtifactQueued(std::string_view a_requestId, const std::filesystem::path& a_path)
{
	std::lock_guard lock(mutex);
	if (auto found = requests.find(std::string(a_requestId)); found != requests.end() && !IsTerminal(found->second.state))
		TransitionLocked(found->second, "queued", "artifact.queued", { { "path", a_path.string() } });
}

void ScreenshotApi::OnArtifactEncoding(std::string_view a_requestId)
{
	std::lock_guard lock(mutex);
	if (auto found = requests.find(std::string(a_requestId)); found != requests.end() && !IsTerminal(found->second.state))
		TransitionLocked(found->second, "encoding", "artifact.encoding");
}

void ScreenshotApi::OnArtifactTerminal(std::string_view a_requestId, bool a_success, const std::filesystem::path& a_path, std::string_view a_error)
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
		record.artifacts.push_back(artifact);
		if (artifact.contains("integrityError"))
			record.warnings.push_back({ { "code", "artifact_hash_failed" }, { "message", artifact["integrityError"] } });
		++record.successfulArtifacts;
		++completedArtifacts;
		AppendEventLocked(record, "artifact.written", artifact);
	} else {
		++failedArtifacts;
		record.error = { { "code", "artifact_failed" }, { "message", a_error.empty() ? "screenshot artifact failed" : std::string(a_error) }, { "phase", "encoding" } };
		AppendEventLocked(record, "artifact.failed", record.error);
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
	FinishSequenceChildLocked(record, allSucceeded, a_path, allSucceeded ? std::string_view{} : a_error);
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
	TransitionLocked(found->second, std::string(a_state), "request.terminal", { { "reason", a_error } });
	FinishSequenceChildLocked(found->second, false, {}, a_error);
}

void ScreenshotApi::FinishSequenceChildLocked(RequestRecord& a_child, bool a_success, const std::filesystem::path& a_path, std::string_view a_error)
{
	if (a_child.parentRequestId.empty())
		return;
	const auto sequenceIt = sequences.find(a_child.parentRequestId);
	if (sequenceIt == sequences.end())
		return;
	auto& sequence = sequenceIt->second;
	if (sequence.inFlight > 0) --sequence.inFlight;
	if (a_success) {
		++sequence.acquired;
		++sequence.written;
		sequence.consecutiveSkips = 0;
	} else if (a_child.state == "dropped" || a_error == "source_busy" || a_error == "encoder_backpressure") {
		++sequence.dropped;
		++sequence.consecutiveSkips;
	} else {
		++sequence.failed;
	}
	json childArtifact = nullptr;
	if (a_success && !a_child.artifacts.empty())
		childArtifact = a_child.artifacts.back();
	sequence.children.push_back({
		{ "ordinal", a_child.sequenceOrdinal }, { "requestId", a_child.requestId }, { "state", a_child.state },
		{ "artifact", std::move(childArtifact) },
		{ "path", a_path.empty() ? json(nullptr) : json(a_path.string()) }, { "error", a_error.empty() ? json(nullptr) : json(a_error) },
	});
	if ((sequence.failurePolicy == "abort" && !a_success) ||
		(sequence.backpressurePolicy == "abort" && sequence.dropped != 0) ||
		(sequence.maximumConsecutiveSkips != 0 && sequence.consecutiveSkips >= sequence.maximumConsecutiveSkips)) {
		sequence.stopRequested = true;
	}
	if (sequence.children.size() % 10 == 0)
		WriteSequenceManifestLocked(sequence, false);
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
	const bool manifestWritten = WriteSequenceManifestLocked(a_sequence, true);
	if (a_sequence.frameManifest) {
		parent->second.expectedArtifacts = 1;
		parent->second.terminalArtifacts = 1;
		if (manifestWritten) {
			const auto artifact = DescribeCommittedArtifact(a_sequence.finalManifestPath);
			parent->second.artifacts.push_back(artifact);
			parent->second.successfulArtifacts = 1;
			if (artifact.contains("integrityError"))
				parent->second.warnings.push_back({ { "code", "artifact_hash_failed" }, { "message", artifact["integrityError"] } });
		} else {
			parent->second.successfulArtifacts = 0;
		}
	} else {
		parent->second.expectedArtifacts = 0;
		parent->second.terminalArtifacts = 0;
		parent->second.successfulArtifacts = 0;
	}
	std::string terminal;
	if (a_sequence.cancelRequested)
		terminal = a_sequence.written == 0 ? "cancelled" : "cancelled_partial";
	else if (a_sequence.stopRequested)
		terminal = "stopped";
	else if (a_sequence.failed != 0)
		terminal = a_sequence.written == 0 ? "failed" : "failed_partial";
	else if (a_sequence.dropped != 0 || !manifestWritten || !parent->second.warnings.empty() ||
		a_sequence.packaging["previewVideo"].value("state", std::string{}) == "unsupported")
		terminal = "completed_with_warnings";
	else
		terminal = "completed";
	TransitionLocked(parent->second, terminal, "request.terminal", {
		{ "manifestPath", a_sequence.frameManifest && manifestWritten ? json(a_sequence.finalManifestPath.string()) : json(nullptr) }
	});
}

bool ScreenshotApi::WriteSequenceManifestLocked(SequenceRecord& a_sequence, bool a_final)
{
	if (!a_sequence.frameManifest)
		return true;
	try {
		std::filesystem::create_directories(a_sequence.directory);
		json manifestPackaging = a_sequence.packaging;
		manifestPackaging["frameManifest"] = {
			{ "requested", true },
			{ "state", a_final ? "written" : "partial" },
			{ "path", (a_final ? a_sequence.finalManifestPath : a_sequence.partialManifestPath).string() },
		};
		json manifest = {
			{ "contract", { { "name", "csx.screenshot" }, { "major", kContractMajor }, { "minor", kContractMinor }, { "schemaRevision", kSchemaRevision } } },
			{ "sessionId", service.SessionId() }, { "requestId", a_sequence.requestId }, { "state", a_final ? "final" : "partial" },
			{ "capture", a_sequence.capture },
			{ "counts", { { "requested", a_sequence.frameCount }, { "scheduled", a_sequence.scheduled }, { "written", a_sequence.written }, { "dropped", a_sequence.dropped }, { "failed", a_sequence.failed }, { "inFlight", a_sequence.inFlight } } },
			{ "children", a_sequence.children }, { "packaging", manifestPackaging }, { "updatedUtc", CSX::Api::ServiceFoundation::TimestampUtc() },
		};
		const auto destination = a_final ? a_sequence.finalManifestPath : a_sequence.partialManifestPath;
		const auto temporary = destination.string() + ".tmp";
		{
			std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
			stream << manifest.dump(2);
			stream.flush();
			if (!stream)
				throw std::runtime_error("manifest write failed");
		}
		if (!MoveFileExW(
				std::filesystem::path(temporary).c_str(),
				destination.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			throw std::runtime_error(std::format("manifest commit failed with Win32 error {}", GetLastError()));
		}
		if (a_final) {
			std::error_code ec;
			std::filesystem::remove(a_sequence.partialManifestPath, ec);
		}
		a_sequence.packaging = std::move(manifestPackaging);
		return true;
	} catch (const std::exception& e) {
		logger::error("Screenshot sequence manifest write failed: {}", e.what());
		if (const auto parent = requests.find(a_sequence.requestId); parent != requests.end())
			parent->second.warnings.push_back({ { "code", "manifest_write_failed" }, { "message", e.what() } });
		a_sequence.packaging["frameManifest"] = {
			{ "requested", true }, { "state", "failed" }, { "error", e.what() }
		};
		return false;
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
			{ "directory", sequence.directory.string() },
			{ "baseName", std::format("frame_{:06}", dueFrame.ordinal) },
			{ "overwrite", "never" },
		};
		++sequence.scheduled;
		++sequence.inFlight;
		sequence.nextEngineFrame = a_engineFrame + sequence.intervalFrames;
		sequence.nextWallClock = now + std::chrono::milliseconds(sequence.intervalMs);
		json childRequest = {
			{ "action", "capture" }, { "clientId", "sequence:" + sequence.requestId },
			{ "commandId", std::format("frame:{}", dueFrame.ordinal) }, { "contractMajor", kContractMajor },
		};
		auto& child = CreateRequestLocked("sequence_frame", childRequest, dueFrame.capture, sequence.requestId, dueFrame.ordinal, dueFrame.childRequestId);
		AppendEventLocked(child, "sequence.frame_scheduled", { { "ordinal", dueFrame.ordinal }, { "engineFrame", a_engineFrame } });
		return dueFrame;
	}
	return std::nullopt;
}

void ScreenshotApi::Tick(ScreenshotFeature& a_feature, uint64_t a_engineFrame)
{
	std::optional<DueFrame> due;
	{
		std::lock_guard lock(mutex);
		for (auto& [_, sequence] : sequences)
			TryFinalizeSequenceLocked(sequence);
		due = PrepareDueFrameLocked(a_engineFrame);
	}
	if (!due)
		return;
	if (!a_feature.TryStartApiCapture(due->childRequestId, due->capture, due->parentRequestId, due->ordinal))
		OnSourceTerminal(due->childRequestId, "dropped", "source_busy");
}

void ScreenshotApi::CancelAll(std::string_view a_reason)
{
	std::lock_guard lock(mutex);
	for (auto& [_, record] : requests) {
		if (IsTerminal(record.state))
			continue;
		record.error = { { "code", "shutdown" }, { "message", a_reason }, { "phase", "shutdown" } };
		TransitionLocked(record, record.artifacts.empty() ? "cancelled" : "cancelled_partial", "request.terminal", { { "reason", a_reason } });
	}
	for (auto& [_, sequence] : sequences) {
		sequence.cancelRequested = true;
		sequence.inFlight = 0;
		TryFinalizeSequenceLocked(sequence);
	}
}

std::filesystem::path ScreenshotApi::ResolveDestinationDirectory(const ScreenshotFeature& a_feature, const json& a_capture)
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
		requested = a_feature.screenshotPath;
		if (requested.empty())
			throw std::runtime_error("the configured screenshot directory is empty");
		if (requested.is_relative())
			requested = gameDirectory / requested;
		return std::filesystem::weakly_canonical(requested);
	}

	const auto directory = destination.value("directory", std::string{});
	if (directory.empty())
		throw std::runtime_error("destination.directory is required by the selected policy");
	requested = std::filesystem::path(directory);
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
