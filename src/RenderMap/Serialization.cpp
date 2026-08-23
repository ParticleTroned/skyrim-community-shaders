#include "RenderMap/Serialization.h"

#include <algorithm>
#include <array>
#include <format>
#include <string>

namespace CSX::RenderMap
{
	namespace
	{
		using json = nlohmann::json;

		const char* EventKindName(EventKind a_kind) noexcept
		{
			static constexpr std::array names{
				"capture-marker", "gap", "frame-begin", "frame-end",
				"scene-accumulation-begin", "scene-accumulation-end", "eye-begin", "eye-end",
				"object-observed", "geometry-observed", "material-observed", "render-pass-created",
				"render-pass-enter", "render-pass-exit", "technique-begin", "technique-end",
				"geometry-setup-begin", "geometry-setup-end", "pipeline-object-created", "pipeline-bind",
				"render-target-bind", "depth-source-ready", "visibility-candidate", "visibility-result-ready",
				"visibility-consumed", "cull-decision", "draw", "dispatch", "finish-command-list",
				"execute-command-list",
			};
			const auto index = static_cast<std::size_t>(a_kind);
			return index < names.size() ? names[index] : "gap";
		}

		const char* StopReasonName(StopReason a_reason) noexcept
		{
			switch (a_reason) {
			case StopReason::kRequested: return "requested";
			case StopReason::kFrameLimit: return "frame-limit";
			case StopReason::kTimeLimit: return "time-limit";
			case StopReason::kEventLimit: return "event-limit";
			case StopReason::kByteLimit: return "byte-limit";
			case StopReason::kShutdown: return "shutdown";
			case StopReason::kFailure: return "failure";
			default: return "failure";
			}
		}

		const char* EyeName(Eye a_eye) noexcept
		{
			switch (a_eye) {
			case Eye::kLeft: return "left";
			case Eye::kRight: return "right";
			case Eye::kBoth: return "both";
			case Eye::kMono: return "mono";
			default: return "unknown";
			}
		}

		json OptionalFrame(std::uint64_t a_value)
		{
			return a_value == kUnknownFrame ? json(nullptr) : json(a_value);
		}

		json PointerEvidence(std::uint64_t a_value)
		{
			return a_value == 0 ? json(nullptr) : json(std::format("0x{:X}", a_value));
		}

		json ScopeId(
			const ScopeBinding& a_scope,
			std::string_view a_kind,
			std::uint64_t a_generation)
		{
			return a_scope.observationId == 0 ? json(nullptr) :
				json(std::format("obs-{}-{}-g{}", a_kind, a_scope.observationId, a_generation));
		}

		json SerializePayload(const EventPayload& a_payload)
		{
			switch (static_cast<PayloadSchema>(a_payload.schema)) {
			case PayloadSchema::kRenderPassBoundary:
				return {
					{ "schema", "render-pass-boundary-v1" },
					{ "renderPassPointer", PointerEvidence(a_payload.words[0]) },
					{ "geometryPointer", PointerEvidence(a_payload.words[1]) },
					{ "technique", a_payload.words[2] },
					{ "passEnum", a_payload.words[3] },
					{ "renderFlags", a_payload.words[4] },
					{ "alphaTest", a_payload.words[5] != 0 },
				};
			case PayloadSchema::kTechniqueBoundary:
				return {
					{ "schema", "technique-boundary-v1" },
					{ "shaderPointer", PointerEvidence(a_payload.words[0]) },
					{ "shaderType", a_payload.words[1] },
					{ "vertexDescriptor", a_payload.words[2] },
					{ "pixelDescriptor", a_payload.words[3] },
					{ "callerRva", std::format("0x{:X}", a_payload.words[4]) },
					{ "skipPixelShader", a_payload.words[5] != 0 },
				};
			case PayloadSchema::kGeometryBoundary:
				return {
					{ "schema", "geometry-boundary-v1" },
					{ "shaderPointer", PointerEvidence(a_payload.words[0]) },
					{ "renderPassPointer", PointerEvidence(a_payload.words[1]) },
					{ "geometryPointer", PointerEvidence(a_payload.words[2]) },
					{ "shaderType", a_payload.words[3] },
					{ "passEnum", a_payload.words[4] },
					{ "renderFlags", a_payload.words[5] },
				};
			default:
				return {
					{ "schema", std::format("unknown-{}", a_payload.schema) },
					{ "words", a_payload.words },
				};
			}
		}
	}

	nlohmann::json SerializeBounds(const CollectorConfig& a_config)
	{
		return {
			{ "maxFrames", a_config.maxFrames },
			{ "maxDurationMs", std::chrono::duration_cast<std::chrono::milliseconds>(a_config.maxDuration).count() },
			{ "maxEvents", a_config.maxEvents },
			{ "maxBytes", a_config.maxBytes },
			{ "maxScopeDepth", a_config.maxScopeDepth },
			{ "pointerPolicy", "retain" },
		};
	}

	nlohmann::json SerializeControllerStatus(const ControllerSnapshot& a_status)
	{
		json active = nullptr;
		if (a_status.active) {
			active = {
				{ "captureId", a_status.active->captureId },
				{ "numericId", a_status.active->numericId },
				{ "bounds", SerializeBounds(a_status.active->config) },
			};
		}
		return {
			{ "capturing", a_status.active.has_value() },
			{ "active", std::move(active) },
			{ "completedCaptureIds", a_status.completedCaptureIds },
		};
	}

	nlohmann::json SerializeCaptureSummary(const CompletedCapture& a_capture)
	{
		const auto& snapshot = a_capture.snapshot;
		const auto dropped = snapshot.statistics.droppedStopped +
			snapshot.statistics.droppedEventLimit + snapshot.statistics.droppedByteLimit +
			snapshot.statistics.droppedFrameLimit + snapshot.statistics.droppedTimeLimit;
		return {
			{ "captureId", a_capture.descriptor.captureId },
			{ "numericId", a_capture.descriptor.numericId },
			{ "state", "complete" },
			{ "bounds", SerializeBounds(snapshot.config) },
			{ "clock", {
				{ "source", "QueryPerformanceCounter" },
				{ "frequencyHz", snapshot.clockFrequencyHz },
				{ "startTick", snapshot.startTimestampTicks },
				{ "endTick", snapshot.endTimestampTicks },
			} },
			{ "completion", {
				{ "reason", StopReasonName(snapshot.stopReason) },
				{ "eventCount", snapshot.events.size() },
				{ "attemptedEventCount", snapshot.statistics.attempted },
				{ "droppedEventCount", dropped },
				{ "scopeOverflowCount", snapshot.statistics.scopeOverflow },
				{ "scopeMismatchCount", snapshot.statistics.scopeMismatch },
				{ "truncated", snapshot.stopReason == StopReason::kEventLimit || snapshot.stopReason == StopReason::kByteLimit },
			} },
		};
	}

	nlohmann::json SerializeEvent(
		const EventRecord& a_event,
		std::string_view a_captureId,
		std::uint32_t a_processId)
	{
		return {
			{ "schema", {
				{ "name", "csx.render-event" }, { "major", a_event.schemaMajor },
				{ "minor", a_event.schemaMinor }, { "producerVersion", "collector-v1" },
			} },
			{ "captureId", a_captureId },
			{ "sequence", a_event.sequence },
			{ "timestampQpc", a_event.timestampTicks },
			{ "processId", a_processId },
			{ "threadId", a_event.threadId },
			{ "frame", {
				{ "cpuFrame", OptionalFrame(a_event.frame.cpuFrame) },
				{ "sceneEpoch", OptionalFrame(a_event.frame.sceneEpoch) },
				{ "submissionEpoch", OptionalFrame(a_event.frame.submissionEpoch) },
				{ "eye", EyeName(a_event.frame.eye) },
				{ "eyeMask", a_event.frame.eyeMask == 0 ? json(nullptr) : json(a_event.frame.eyeMask) },
			} },
			{ "deviceContextObservationId", nullptr },
			{ "type", EventKindName(a_event.kind) },
			{ "scopes", {
				{ "renderPass", ScopeId(a_event.scopes.renderPass, "render-pass", a_event.sessionGeneration) },
				{ "technique", ScopeId(a_event.scopes.technique, "technique", a_event.sessionGeneration) },
				{ "geometry", ScopeId(a_event.scopes.geometry, "geometry", a_event.sessionGeneration) },
				{ "commandList", ScopeId(a_event.scopes.commandList, "command-list", a_event.sessionGeneration) },
			} },
			{ "causes", json::array() },
			{ "manifestRefs", json::array() },
			{ "engineRefs", json::array() },
			{ "observationRefs", json::array() },
			{ "payload", SerializePayload(a_event.payload) },
			{ "extensions", {
				{ "csx.captureNumericId", a_event.captureNumericId },
				{ "csx.sessionGeneration", a_event.sessionGeneration },
				{ "csx.scopeTokens", {
					{ "renderPass", a_event.scopes.renderPass.token }, { "technique", a_event.scopes.technique.token },
					{ "geometry", a_event.scopes.geometry.token }, { "commandList", a_event.scopes.commandList.token },
				} },
			} },
		};
	}

	nlohmann::json SerializeEventPage(
		const CompletedCapture& a_capture,
		std::size_t a_offset,
		std::size_t a_limit,
		std::uint32_t a_processId)
	{
		const auto& records = a_capture.snapshot.events;
		const auto offset = std::min(a_offset, records.size());
		const auto count = std::min(a_limit, records.size() - offset);
		json events = json::array();
		for (std::size_t index = 0; index < count; ++index)
			events.push_back(SerializeEvent(records[offset + index], a_capture.descriptor.captureId, a_processId));
		return {
			{ "captureId", a_capture.descriptor.captureId },
			{ "offset", offset },
			{ "returnedCount", count },
			{ "totalCount", records.size() },
			{ "nextOffset", offset + count },
			{ "moreAvailable", offset + count < records.size() },
			{ "events", std::move(events) },
		};
	}
}
