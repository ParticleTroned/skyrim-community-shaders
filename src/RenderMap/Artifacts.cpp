#include "RenderMap/Artifacts.h"

#include "RenderMap/Serialization.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <format>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace CSX::RenderMap
{
	namespace
	{
		using json = nlohmann::json;

		std::string FileSha256(const std::filesystem::path& a_path)
		{
			std::ifstream stream(a_path, std::ios::binary);
			if (!stream)
				throw std::runtime_error("could not open committed render-map artifact for hashing");

			BCRYPT_ALG_HANDLE algorithm = nullptr;
			if (const auto status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0); status < 0)
				throw std::runtime_error(std::format("BCryptOpenAlgorithmProvider failed ({:#x})", static_cast<std::uint32_t>(status)));

			DWORD objectBytes = 0;
			DWORD copiedBytes = 0;
			if (const auto status = BCryptGetProperty(
					algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes),
					sizeof(objectBytes), &copiedBytes, 0);
				status < 0) {
				BCryptCloseAlgorithmProvider(algorithm, 0);
				throw std::runtime_error(std::format("BCryptGetProperty failed ({:#x})", static_cast<std::uint32_t>(status)));
			}

			std::vector<UCHAR> hashObject(objectBytes);
			BCRYPT_HASH_HANDLE hash = nullptr;
			if (const auto status = BCryptCreateHash(
					algorithm, &hash, hashObject.data(), static_cast<ULONG>(hashObject.size()), nullptr, 0, 0);
				status < 0) {
				BCryptCloseAlgorithmProvider(algorithm, 0);
				throw std::runtime_error(std::format("BCryptCreateHash failed ({:#x})", static_cast<std::uint32_t>(status)));
			}

			std::vector<UCHAR> buffer(1024 * 1024);
			NTSTATUS hashStatus = 0;
			while (stream && hashStatus >= 0) {
				stream.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
				if (const auto bytesRead = stream.gcount(); bytesRead > 0)
					hashStatus = BCryptHashData(hash, buffer.data(), static_cast<ULONG>(bytesRead), 0);
			}
			if (!stream.eof() && hashStatus >= 0)
				hashStatus = static_cast<NTSTATUS>(0xC0000185L);

			std::array<UCHAR, 32> digest{};
			const auto finishStatus = hashStatus < 0 ? hashStatus :
			                                           BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
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

		bool WriteTextFileAtomicNoReplace(
			const std::filesystem::path& a_path,
			std::string_view a_contents,
			std::string& a_error)
		{
			a_error.clear();
			std::error_code ec;
			std::filesystem::create_directories(a_path.parent_path(), ec);
			if (ec) {
				a_error = std::format("could not create artifact directory: {}", ec.message());
				return false;
			}
			if (std::filesystem::exists(a_path, ec) || ec) {
				a_error = ec ? std::format("could not inspect artifact destination: {}", ec.message()) :
				               "artifact destination already exists";
				return false;
			}

			static std::atomic_uint64_t stagingSequence{ 1 };
			auto staging = a_path;
			staging += std::format(L".{}.{}.tmp", GetCurrentProcessId(),
				stagingSequence.fetch_add(1, std::memory_order_relaxed));
			{
				std::ofstream stream(staging, std::ios::binary | std::ios::trunc);
				if (!stream) {
					a_error = "could not open artifact staging file";
					return false;
				}
				stream.write(a_contents.data(), static_cast<std::streamsize>(a_contents.size()));
				stream.flush();
				stream.close();
				if (!stream) {
					std::filesystem::remove(staging, ec);
					a_error = "could not finish artifact staging write";
					return false;
				}
			}

			if (!MoveFileExW(staging.c_str(), a_path.c_str(), MOVEFILE_WRITE_THROUGH)) {
				const auto error = GetLastError();
				std::filesystem::remove(staging, ec);
				a_error = std::format("could not atomically commit artifact (Windows error {})", error);
				return false;
			}
			return true;
		}

		json DescribeArtifact(
			std::string_view a_kind,
			const std::filesystem::path& a_path,
			std::string_view a_mediaType,
			bool a_complete)
		{
			std::error_code ec;
			const auto bytes = std::filesystem::file_size(a_path, ec);
			if (ec)
				throw std::runtime_error(std::format("could not determine artifact size: {}", ec.message()));
			return {
				{ "kind", a_kind },
				{ "path", a_path.filename().string() },
				{ "mediaType", a_mediaType },
				{ "sha256", FileSha256(a_path) },
				{ "bytes", bytes },
				{ "complete", a_complete },
			};
		}

		std::uint64_t LostEventCount(const CaptureStatistics& a_statistics)
		{
			return a_statistics.droppedStopped +
			       a_statistics.droppedEventLimit + a_statistics.droppedByteLimit;
		}

		json SerializeGapEvent(
			const CompletedCapture& a_capture,
			std::uint32_t a_processId,
			std::uint64_t a_sequence,
			std::uint64_t a_dropped)
		{
			return {
				{ "schema", {
								{ "name", "csx.render-event" },
								{ "major", 1 },
								{ "minor", 17 },
								{ "producerVersion", "collector-v1" },
							} },
				{ "captureId", a_capture.descriptor.captureId },
				{ "sequence", a_sequence },
				{ "timestampQpc", a_capture.snapshot.endTimestampTicks },
				{ "processId", a_processId },
				{ "threadId", 0 },
				{ "frame", {
							   { "cpuFrame", nullptr },
							   { "sceneEpoch", nullptr },
							   { "submissionEpoch", nullptr },
							   { "eye", "unknown" },
							   { "eyeMask", nullptr },
						   } },
				{ "execution", {
								   { "observationDomain", "unknown" },
								   { "commandStreamSequence", nullptr },
								   { "gpuTimestampTicks", nullptr },
								   { "gpuTimestampFrequencyHz", nullptr },
							   } },
				{ "deviceContextObservationId", nullptr },
				{ "type", "gap" },
				{ "scopes", {
								{ "renderPass", nullptr },
								{ "technique", nullptr },
								{ "geometry", nullptr },
								{ "commandList", nullptr },
							} },
				{ "causes", json::array() },
				{ "manifestRefs", json::array() },
				{ "engineRefs", json::array() },
				{ "observationRefs", json::array() },
				{ "payload", {
								 { "droppedEventCount", a_dropped },
								 { "reason", a_capture.snapshot.statistics.droppedByteLimit != 0 ? "byte-limit" : a_capture.snapshot.statistics.droppedEventLimit != 0 ? "event-limit" :
																																										 "stop-race" },
							 } },
				{ "extensions", { { "csx.syntheticGap", true } } },
			};
		}
	}

	CaptureArtifactBundle WriteCaptureArtifacts(
		const CompletedCapture& a_capture,
		const CaptureArtifactContext& a_context,
		std::uint32_t a_processId)
	{
		CaptureArtifactBundle bundle;
		bundle.directory = a_context.outputRoot / a_capture.descriptor.captureId;
		try {
			if (a_context.outputRoot.empty())
				throw std::runtime_error("render-map artifact root is unavailable");

			std::set<std::string> eventKinds;
			std::string eventsJsonl;
			for (const auto& event : a_capture.snapshot.events) {
				auto serialized = SerializeEvent(
					event, a_capture.descriptor.captureId, a_processId, &a_capture.snapshot);
				eventKinds.insert(serialized["type"].get<std::string>());
				eventsJsonl += serialized.dump();
				eventsJsonl.push_back('\n');
			}

			const auto lostEvents = LostEventCount(a_capture.snapshot.statistics);
			if (lostEvents != 0) {
				eventKinds.insert("gap");
				eventsJsonl += SerializeGapEvent(
					a_capture, a_processId, a_capture.snapshot.events.size(), lostEvents)
				                   .dump();
				eventsJsonl.push_back('\n');
			}
			const auto observedEventKinds = eventKinds;
			// Retain the legacy non-empty field for manifest 1.x consumers. It is an
			// output inventory, not the capture selector; an empty events artifact has
			// historically been represented by the synthetic capture-marker label.
			if (eventKinds.empty())
				eventKinds.insert("capture-marker");

			const auto eventsPath = bundle.directory / "events.jsonl";
			if (!WriteTextFileAtomicNoReplace(eventsPath, eventsJsonl, bundle.error))
				return bundle;
			const auto& snapshot = a_capture.snapshot;
			const auto serializedEventCount = snapshot.events.size() + (lostEvents == 0 ? 0 : 1);
			const bool truncated = lostEvents != 0;
			const bool structurallyIncomplete = snapshot.statistics.scopeOverflow != 0 ||
			                                    snapshot.statistics.scopeMismatch != 0 || snapshot.statistics.droppedShaderObservations != 0 ||
			                                    snapshot.statistics.droppedStageShaderObservations != 0 ||
			                                    snapshot.statistics.droppedResourceObservations != 0 ||
			                                    snapshot.statistics.droppedTargetViewObservations != 0 ||
			                                    snapshot.statistics.droppedTargetBindingObservations != 0 ||
			                                    snapshot.statistics.droppedSceneObjectObservations != 0 ||
			                                    snapshot.statistics.droppedGeometryObservations != 0 ||
			                                    snapshot.statistics.droppedMaterialStateObservations != 0;
			const bool terminalFailure = snapshot.stopReason == StopReason::kShutdown ||
			                             snapshot.stopReason == StopReason::kFailure;
			const bool incomplete = truncated || structurallyIncomplete || terminalFailure;
			bundle.eventsArtifact = DescribeArtifact(
				"events-jsonl", eventsPath, "application/x-ndjson", !incomplete);

			json completionErrors = json::array();
			if (snapshot.statistics.scopeOverflow != 0)
				completionErrors.push_back("scope depth overflowed during capture");
			if (snapshot.statistics.scopeMismatch != 0)
				completionErrors.push_back("scope nesting mismatch occurred during capture");
			if (snapshot.statistics.droppedShaderObservations != 0)
				completionErrors.push_back("shader observation capacity was exceeded during capture");
			if (snapshot.statistics.droppedStageShaderObservations != 0)
				completionErrors.push_back("stage shader observation capacity was exceeded during capture");
			if (snapshot.statistics.droppedResourceObservations != 0)
				completionErrors.push_back("resource observation capacity was exceeded during capture");
			if (snapshot.statistics.droppedTargetViewObservations != 0)
				completionErrors.push_back("target view observation capacity was exceeded during capture");
			if (snapshot.statistics.droppedTargetBindingObservations != 0)
				completionErrors.push_back("target binding observation capacity was exceeded during capture");
			if (snapshot.statistics.droppedSceneObjectObservations != 0)
				completionErrors.push_back("scene object observation capacity was exceeded during capture");
			if (snapshot.statistics.droppedGeometryObservations != 0)
				completionErrors.push_back("geometry observation capacity was exceeded during capture");
			if (snapshot.statistics.droppedMaterialStateObservations != 0)
				completionErrors.push_back("material state observation capacity was exceeded during capture");
			if (terminalFailure)
				completionErrors.push_back("capture ended during shutdown or failure handling");
			const auto summary = SerializeCaptureSummary(a_capture);
			json extensions = a_context.extensions.is_object() ?
			                      a_context.extensions :
			                      json::object();
			extensions.update({
				{ "csx.processId", a_processId },
				{ "csx.sessionGeneration", snapshot.sessionGeneration },
				{ "csx.acceptedEventCount", snapshot.events.size() },
				{ "csx.filteredEventCount", snapshot.statistics.filtered },
				{ "csx.requestedEventKinds", SerializeEventKindMask(snapshot.config.requestedEventKindMask) },
				{ "csx.resolvedEventKinds", SerializeEventKindMask(snapshot.config.eventKindMask) },
				{ "csx.boundaryRejectionCount", snapshot.statistics.droppedFrameLimit + snapshot.statistics.droppedTimeLimit },
				{ "csx.stopRaceRejectionCount", snapshot.statistics.droppedStopped },
				{ "csx.shaderObservationCount", snapshot.shaderObservations.size() },
				{ "csx.droppedShaderObservationCount", snapshot.statistics.droppedShaderObservations },
				{ "csx.stageShaderObservationCount", snapshot.stageShaderObservations.size() },
				{ "csx.droppedStageShaderObservationCount", snapshot.statistics.droppedStageShaderObservations },
				{ "csx.resourceObservationCount", snapshot.resourceObservations.size() },
				{ "csx.droppedResourceObservationCount", snapshot.statistics.droppedResourceObservations },
				{ "csx.targetViewObservationCount", snapshot.targetViewObservations.size() },
				{ "csx.droppedTargetViewObservationCount", snapshot.statistics.droppedTargetViewObservations },
				{ "csx.targetBindingObservationCount", snapshot.targetBindingObservations.size() },
				{ "csx.droppedTargetBindingObservationCount", snapshot.statistics.droppedTargetBindingObservations },
				{ "csx.sceneObjectObservationCount", snapshot.sceneObjectObservations.size() },
				{ "csx.droppedSceneObjectObservationCount", snapshot.statistics.droppedSceneObjectObservations },
				{ "csx.geometryObservationCount", snapshot.geometryObservations.size() },
				{ "csx.droppedGeometryObservationCount", snapshot.statistics.droppedGeometryObservations },
				{ "csx.materialStateObservationCount", snapshot.materialStateObservations.size() },
				{ "csx.droppedMaterialStateObservationCount", snapshot.statistics.droppedMaterialStateObservations },
			});

			json manifest = {
				{ "schema", {
								{ "name", "csx.render-capture-manifest" },
								{ "major", 1 },
								{ "minor", 7 },
								{ "producerVersion", "collector-v1" },
							} },
				{ "captureId", a_capture.descriptor.captureId },
				{ "status", incomplete ? "incomplete" : "complete" },
				{ "createdAtUtc", a_context.createdAtUtc },
				{ "producer", a_context.producer },
				{ "capabilities", a_context.capabilities },
				{ "inputs", a_context.inputs },
				{ "environment", a_context.environment },
				{ "scenario", a_context.scenario },
				{ "bounds", {
								{ "eventKinds", eventKinds },
								{ "requestedEventKinds", SerializeEventKindMask(snapshot.config.requestedEventKindMask) },
								{ "resolvedEventKinds", SerializeEventKindMask(snapshot.config.eventKindMask) },
								{ "observedEventKinds", observedEventKinds },
								{ "maxFrames", snapshot.config.maxFrames },
								{ "maxDurationMs", std::chrono::duration_cast<std::chrono::milliseconds>(snapshot.config.maxDuration).count() },
								{ "maxEvents", snapshot.config.maxEvents },
								{ "maxBytes", snapshot.config.maxBytes },
								{ "maxShaderObservations", snapshot.config.maxShaderObservations },
								{ "maxStageShaderObservations", snapshot.config.maxStageShaderObservations },
								{ "maxResourceObservations", snapshot.config.maxResourceObservations },
								{ "maxTargetViewObservations", snapshot.config.maxTargetViewObservations },
								{ "maxTargetBindingObservations", snapshot.config.maxTargetBindingObservations },
								{ "maxSceneObjectObservations", snapshot.config.maxSceneObjectObservations },
								{ "maxGeometryObservations", snapshot.config.maxGeometryObservations },
								{ "maxMaterialStateObservations", snapshot.config.maxMaterialStateObservations },
								{ "geometryShaderTypes", SerializeGeometryShaderTypeMask(snapshot.config.geometryShaderTypeMask) },
								{ "executionWithinSelectedGeometry", snapshot.config.executionWithinSelectedGeometry },
								{ "pointerPolicy", "retain" },
							} },
				{ "clock", {
							   { "source", "QueryPerformanceCounter" },
							   { "frequencyHz", snapshot.clockFrequencyHz },
							   { "startTick", snapshot.startTimestampTicks },
						   } },
				{ "artifacts", json::array({ bundle.eventsArtifact }) },
				{ "completion", {
									{ "reason", summary["completion"]["reason"] },
									{ "firstSequence", serializedEventCount == 0 ? json(nullptr) : json(0) },
									{ "lastSequence", serializedEventCount == 0 ? json(nullptr) : json(serializedEventCount - 1) },
									{ "eventCount", serializedEventCount },
									{ "droppedEventCount", lostEvents },
									{ "truncated", truncated },
									{ "collectorOverheadUs", nullptr },
									{ "errors", completionErrors },
								} },
				{ "extensions", std::move(extensions) },
			};

			const auto manifestPath = bundle.directory / "capture-manifest.json";
			if (!WriteTextFileAtomicNoReplace(manifestPath, manifest.dump(2), bundle.error))
				return bundle;
			bundle.manifestArtifact = {
				{ "kind", "capture-manifest" },
				{ "path", manifestPath.string() },
				{ "mediaType", "application/json" },
				{ "sha256", FileSha256(manifestPath) },
				{ "bytes", std::filesystem::file_size(manifestPath) },
				{ "complete", true },
			};
			bundle.success = true;
			return bundle;
		} catch (const std::exception& error) {
			bundle.error = error.what();
			return bundle;
		}
	}

	nlohmann::json SerializeArtifactBundle(const CaptureArtifactBundle& a_bundle)
	{
		return {
			{ "success", a_bundle.success },
			{ "directory", a_bundle.directory.empty() ? json(nullptr) : json(a_bundle.directory.string()) },
			{ "events", a_bundle.eventsArtifact.empty() ? json(nullptr) : a_bundle.eventsArtifact },
			{ "manifest", a_bundle.manifestArtifact.empty() ? json(nullptr) : a_bundle.manifestArtifact },
			{ "error", a_bundle.error.empty() ? json(nullptr) : json(a_bundle.error) },
		};
	}
}
