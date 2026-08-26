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
				"execute-command-list", "shader-observed", "stage-shader-observed", "technique-resolved",
				"device-context-observed", "target-view-observed",
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

		json ShaderObservationId(std::uint64_t a_observationId, std::uint64_t a_generation)
		{
			return a_observationId == 0 ? json(nullptr) :
				json(std::format("obs-shader-{}-g{}", a_observationId, a_generation));
		}

		json DeviceContextObservationId(std::uint64_t a_observationId, std::uint64_t a_generation)
		{
			return a_observationId == 0 ? json(nullptr) :
				json(std::format("obs-device-context-{}-g{}", a_observationId, a_generation));
		}

		const char* ShaderStageName(ShaderStage a_stage) noexcept
		{
			switch (a_stage) {
			case ShaderStage::kPixel: return "pixel";
			case ShaderStage::kCompute: return "compute";
			default: return "vertex";
			}
		}

		const char* StageShaderKind(ShaderStage a_stage) noexcept
		{
			switch (a_stage) {
			case ShaderStage::kPixel: return "pixel-shader";
			case ShaderStage::kCompute: return "compute-shader";
			default: return "vertex-shader";
			}
		}

		const char* DrawOperationName(DrawOperation a_operation) noexcept
		{
			switch (a_operation) {
			case DrawOperation::kDrawIndexed: return "draw-indexed";
			case DrawOperation::kDrawInstanced: return "draw-instanced";
			case DrawOperation::kDrawIndexedInstanced: return "draw-indexed-instanced";
			case DrawOperation::kDrawAuto: return "draw-auto";
			case DrawOperation::kDrawInstancedIndirect: return "draw-instanced-indirect";
			case DrawOperation::kDrawIndexedInstancedIndirect: return "draw-indexed-instanced-indirect";
			default: return "draw";
			}
		}

		const char* DispatchOperationName(DispatchOperation a_operation) noexcept
		{
			return a_operation == DispatchOperation::kDispatchIndirect ? "dispatch-indirect" : "dispatch";
		}

		const char* ShaderSelectionRouteName(ShaderSelectionRoute a_route) noexcept
		{
			switch (a_route) {
			case ShaderSelectionRoute::kEngine: return "engine";
			case ShaderSelectionRoute::kCSXCache: return "csx-cache";
			case ShaderSelectionRoute::kCSXFallback: return "csx-fallback";
			case ShaderSelectionRoute::kSkipped: return "skipped";
			case ShaderSelectionRoute::kMissing: return "missing";
			default: return "unknown";
			}
		}

		json StageShaderObservationId(
			ShaderStage a_stage,
			std::uint64_t a_observationId,
			std::uint64_t a_generation)
		{
			return a_observationId == 0 ? json(nullptr) :
				json(std::format("obs-{}-shader-{}-g{}", ShaderStageName(a_stage), a_observationId, a_generation));
		}

		const char* TargetViewKindName(TargetViewKind a_kind) noexcept
		{
			return a_kind == TargetViewKind::kDepthTarget ? "depth-target" : "render-target";
		}

		json TargetViewObservationId(
			TargetViewKind a_kind,
			std::uint64_t a_observationId,
			std::uint64_t a_generation)
		{
			return a_observationId == 0 ? json(nullptr) :
				json(std::format("obs-{}-{}-g{}", TargetViewKindName(a_kind), a_observationId, a_generation));
		}

		json TargetBindingObservationId(std::uint64_t a_observationId, std::uint64_t a_generation)
		{
			return a_observationId == 0 ? json(nullptr) :
				json(std::format("obs-target-binding-{}-g{}", a_observationId, a_generation));
		}

		const ShaderObservationRecord* FindShaderObservation(
			const CaptureSnapshot* a_snapshot,
			std::uint64_t a_observationId) noexcept
		{
			if (!a_snapshot || a_observationId == 0)
				return nullptr;
			const auto found = std::find_if(
				a_snapshot->shaderObservations.begin(), a_snapshot->shaderObservations.end(),
				[&](const ShaderObservationRecord& a_record) {
					return a_record.observationId == a_observationId;
				});
			return found == a_snapshot->shaderObservations.end() ? nullptr : std::addressof(*found);
		}

		const StageShaderObservationRecord* FindStageShaderObservation(
			const CaptureSnapshot* a_snapshot,
			std::uint64_t a_observationId) noexcept
		{
			if (!a_snapshot || a_observationId == 0)
				return nullptr;
			const auto found = std::find_if(
				a_snapshot->stageShaderObservations.begin(), a_snapshot->stageShaderObservations.end(),
				[&](const StageShaderObservationRecord& a_record) {
					return a_record.observationId == a_observationId;
				});
			return found == a_snapshot->stageShaderObservations.end() ? nullptr : std::addressof(*found);
		}

		const TargetViewObservationRecord* FindTargetViewObservation(
			const CaptureSnapshot* a_snapshot,
			std::uint64_t a_observationId) noexcept
		{
			if (!a_snapshot || a_observationId == 0)
				return nullptr;
			const auto found = std::find_if(
				a_snapshot->targetViewObservations.begin(), a_snapshot->targetViewObservations.end(),
				[&](const TargetViewObservationRecord& a_record) {
					return a_record.observationId == a_observationId;
				});
			return found == a_snapshot->targetViewObservations.end() ? nullptr : std::addressof(*found);
		}

		const TargetBindingObservationRecord* FindTargetBindingObservation(
			const CaptureSnapshot* a_snapshot,
			std::uint64_t a_observationId) noexcept
		{
			if (!a_snapshot || a_observationId == 0)
				return nullptr;
			const auto found = std::find_if(
				a_snapshot->targetBindingObservations.begin(), a_snapshot->targetBindingObservations.end(),
				[&](const TargetBindingObservationRecord& a_record) {
					return a_record.observationId == a_observationId;
				});
			return found == a_snapshot->targetBindingObservations.end() ? nullptr : std::addressof(*found);
		}

		template <std::size_t N>
		json OptionalStoredString(const std::array<char, N>& a_value)
		{
			return a_value[0] == '\0' ? json(nullptr) : json(a_value.data());
		}

		json SerializeShaderObservation(
			const ShaderObservationRecord* a_observation,
			const EventPayload& a_payload,
			std::uint64_t a_generation)
		{
			const auto observationId = a_payload.words[0];
			if (!a_observation) {
				return {
					{ "schema", "shader-observation-v1" },
					{ "shaderObservationId", ShaderObservationId(observationId, a_generation) },
					{ "shaderPointer", PointerEvidence(a_payload.words[1]) },
					{ "pointerGeneration", a_payload.words[2] },
					{ "shaderType", a_payload.words[3] },
					{ "identityDetailsAvailable", false },
				};
			}
			return {
				{ "schema", "shader-observation-v1" },
				{ "shaderObservationId", ShaderObservationId(observationId, a_generation) },
				{ "shaderPointer", PointerEvidence(a_observation->pointerEvidence) },
				{ "pointerGeneration", a_observation->pointerGeneration },
				{ "shaderType", a_observation->shaderType },
				{ "fxpFilename", OptionalStoredString(a_observation->fxpFilename) },
				{ "imageSpaceName", OptionalStoredString(a_observation->imageSpaceName) },
				{ "definesSuffix", OptionalStoredString(a_observation->definesSuffix) },
				{ "truncated", {
					{ "fxpFilename", a_observation->fxpFilenameTruncated },
					{ "imageSpaceName", a_observation->imageSpaceNameTruncated },
					{ "definesSuffix", a_observation->definesSuffixTruncated },
				} },
				{ "identityDetailsAvailable", true },
				{ "identityBasis", json::array({ "pointer", "shaderType", "fxpFilename", "imageSpaceName", "definesSuffix" }) },
			};
		}

		json SerializeStageShaderObservation(
			const StageShaderObservationRecord* a_observation,
			const EventPayload& a_payload,
			std::uint64_t a_generation)
		{
			const auto stage = static_cast<ShaderStage>(a_payload.words[4]);
			const auto observationId = a_payload.words[0];
			if (!a_observation) {
				return {
					{ "schema", "stage-shader-observation-v1" },
					{ "stageShaderObservationId", StageShaderObservationId(stage, observationId, a_generation) },
					{ "stage", ShaderStageName(stage) },
					{ "d3dObjectPointer", PointerEvidence(a_payload.words[1]) },
					{ "wrapperPointer", PointerEvidence(a_payload.words[2]) },
					{ "pointerGeneration", a_payload.words[3] },
					{ "wrapperDescriptor", a_payload.words[5] },
					{ "bytecodeSize", a_payload.words[6] },
					{ "identityDetailsAvailable", false },
				};
			}
			return {
				{ "schema", "stage-shader-observation-v1" },
				{ "stageShaderObservationId", StageShaderObservationId(
					a_observation->stage, observationId, a_generation) },
				{ "stage", ShaderStageName(a_observation->stage) },
				{ "d3dObjectPointer", PointerEvidence(a_observation->pointerEvidence) },
				{ "wrapperPointer", PointerEvidence(a_observation->wrapperEvidence) },
				{ "pointerGeneration", a_observation->pointerGeneration },
				{ "wrapperDescriptor", a_observation->wrapperDescriptor },
				{ "bytecodeSize", a_observation->bytecodeSize },
				{ "bytecodeSha256", OptionalStoredString(a_observation->bytecodeSha256) },
				{ "cachePath", OptionalStoredString(a_observation->cachePath) },
				{ "truncated", {
					{ "bytecodeSha256", a_observation->bytecodeSha256Truncated },
					{ "cachePath", a_observation->cachePathTruncated },
				} },
				{ "identityDetailsAvailable", true },
				{ "identityBasis", json::array({
					"stage", "wrapper", "d3dObject", "wrapperDescriptor", "bytecodeSha256", "cachePath" }) },
			};
		}

		json SerializeTargetBinding(
			const TargetBindingObservationRecord* a_binding,
			std::uint64_t a_observationId,
			std::uint64_t a_generation)
		{
			json renderTargets = json::array();
			if (a_binding) {
				for (std::size_t index = 0; index < a_binding->renderTargetCount; ++index) {
					renderTargets.push_back(TargetViewObservationId(
						TargetViewKind::kRenderTarget,
						a_binding->renderTargetObservationIds[index], a_generation));
				}
			}
			return {
				{ "schema", "render-target-binding-v1" },
				{ "targetBindingObservationId", TargetBindingObservationId(a_observationId, a_generation) },
				{ "renderTargetObservationIds", std::move(renderTargets) },
				{ "depthTargetObservationId", TargetViewObservationId(
					TargetViewKind::kDepthTarget,
					a_binding ? a_binding->depthTargetObservationId : 0, a_generation) },
				{ "identityDetailsAvailable", a_binding != nullptr },
			};
		}

		json SerializeObservationRefs(
			const EventRecord& a_event,
			const CaptureSnapshot* a_snapshot)
		{
			auto appendDeviceContext = [&](json& a_refs, const char* a_role, std::uint64_t a_pointerEvidence) {
				if (a_event.deviceContextObservationId == 0)
					return;
				a_refs.push_back({
					{ "id", DeviceContextObservationId(
						a_event.deviceContextObservationId, a_event.sessionGeneration) },
					{ "kind", "device-context" },
					{ "role", a_role },
					{ "pointerEvidence", PointerEvidence(a_pointerEvidence) },
				});
			};
			auto appendTargetBinding = [&](json& a_refs) {
				if (a_event.targetBindingObservationId == 0)
					return;
				a_refs.push_back({
					{ "id", TargetBindingObservationId(
						a_event.targetBindingObservationId, a_event.sessionGeneration) },
					{ "kind", "pipeline-state" },
					{ "role", "output-merger-binding" },
					{ "pointerEvidence", nullptr },
				});
				const auto* binding = FindTargetBindingObservation(a_snapshot, a_event.targetBindingObservationId);
				if (!binding)
					return;
				for (std::size_t index = 0; index < binding->renderTargetCount; ++index) {
					const auto id = binding->renderTargetObservationIds[index];
					if (id == 0)
						continue;
					const auto* target = FindTargetViewObservation(a_snapshot, id);
					a_refs.push_back({
						{ "id", TargetViewObservationId(
							TargetViewKind::kRenderTarget, id, a_event.sessionGeneration) },
						{ "kind", "render-target" },
						{ "role", std::format("bound-render-target-{}", index) },
						{ "pointerEvidence", PointerEvidence(target ? target->pointerEvidence : 0) },
					});
				}
				if (binding->depthTargetObservationId != 0) {
					const auto* target = FindTargetViewObservation(a_snapshot, binding->depthTargetObservationId);
					a_refs.push_back({
						{ "id", TargetViewObservationId(
							TargetViewKind::kDepthTarget,
							binding->depthTargetObservationId, a_event.sessionGeneration) },
						{ "kind", "depth-target" },
						{ "role", "bound-depth-target" },
						{ "pointerEvidence", PointerEvidence(target ? target->pointerEvidence : 0) },
					});
				}
			};
			std::uint64_t observationId = 0;
			std::uint64_t pointerEvidence = 0;
			const char* role = nullptr;
			switch (static_cast<PayloadSchema>(a_event.payload.schema)) {
			case PayloadSchema::kTechniqueBoundary:
				observationId = a_event.payload.words[0];
				pointerEvidence = a_event.payload.words[1];
				role = "active-shader";
				break;
			case PayloadSchema::kShaderObservation:
				observationId = a_event.payload.words[0];
				pointerEvidence = a_event.payload.words[1];
				role = "first-observed";
				break;
			case PayloadSchema::kStageShaderObservation: {
				const auto stage = static_cast<ShaderStage>(a_event.payload.words[4]);
				const auto* observation = FindStageShaderObservation(a_snapshot, a_event.payload.words[0]);
				return json::array({ {
					{ "id", StageShaderObservationId(stage, a_event.payload.words[0], a_event.sessionGeneration) },
					{ "kind", StageShaderKind(stage) },
					{ "role", "first-observed" },
					{ "pointerEvidence", PointerEvidence(observation ? observation->pointerEvidence : a_event.payload.words[1]) },
				} });
			}
			case PayloadSchema::kTechniqueResolution: {
				json refs = json::array();
				for (const auto [stage, word] : std::array{
					std::pair{ ShaderStage::kVertex, std::size_t{ 4 } },
					std::pair{ ShaderStage::kPixel, std::size_t{ 5 } } }) {
					const auto id = a_event.payload.words[word];
					if (id == 0)
						continue;
					const auto* observation = FindStageShaderObservation(a_snapshot, id);
					refs.push_back({
						{ "id", StageShaderObservationId(stage, id, a_event.sessionGeneration) },
						{ "kind", StageShaderKind(stage) },
						{ "role", "selected" },
						{ "pointerEvidence", PointerEvidence(observation ? observation->pointerEvidence : 0) },
					});
				}
				return refs;
			}
			case PayloadSchema::kDrawCall: {
				json refs = json::array();
				appendDeviceContext(refs, "immediate-context", a_event.payload.words[0]);
				appendTargetBinding(refs);
				for (const auto [stage, word] : std::array{
					std::pair{ ShaderStage::kVertex, std::size_t{ 2 } },
					std::pair{ ShaderStage::kPixel, std::size_t{ 3 } } }) {
					const auto id = a_event.payload.words[word];
					if (id == 0)
						continue;
					const auto* observation = FindStageShaderObservation(a_snapshot, id);
					refs.push_back({
						{ "id", StageShaderObservationId(stage, id, a_event.sessionGeneration) },
						{ "kind", StageShaderKind(stage) },
						{ "role", "bound-at-draw" },
						{ "pointerEvidence", PointerEvidence(observation ? observation->pointerEvidence : 0) },
					});
				}
				return refs;
			}
			case PayloadSchema::kDispatchCall: {
				json refs = json::array();
				appendDeviceContext(refs, "immediate-context", a_event.payload.words[0]);
				const auto id = a_event.payload.words[2];
				if (id == 0)
					return refs;
				const auto* observation = FindStageShaderObservation(a_snapshot, id);
				refs.push_back({
					{ "id", StageShaderObservationId(ShaderStage::kCompute, id, a_event.sessionGeneration) },
					{ "kind", StageShaderKind(ShaderStage::kCompute) },
					{ "role", "bound-at-dispatch" },
					{ "pointerEvidence", PointerEvidence(observation ? observation->pointerEvidence : 0) },
				});
				return refs;
			}
			case PayloadSchema::kDeviceContextObservation: {
				json refs = json::array();
				appendDeviceContext(refs, "first-observed", a_event.payload.words[1]);
				return refs;
			}
			case PayloadSchema::kTargetViewObservation: {
				const auto kind = static_cast<TargetViewKind>(a_event.payload.words[3]);
				return json::array({ {
					{ "id", TargetViewObservationId(kind, a_event.payload.words[0], a_event.sessionGeneration) },
					{ "kind", TargetViewKindName(kind) },
					{ "role", "first-observed" },
					{ "pointerEvidence", PointerEvidence(a_event.payload.words[1]) },
				} });
			}
			case PayloadSchema::kTargetBinding: {
				json refs = json::array();
				appendDeviceContext(refs, "immediate-context", a_event.payload.words[1]);
				appendTargetBinding(refs);
				return refs;
			}
			default:
				return json::array();
			}
			if (observationId == 0)
				return json::array();
			if (const auto* observation = FindShaderObservation(a_snapshot, observationId))
				pointerEvidence = observation->pointerEvidence;
			return json::array({ {
				{ "id", ShaderObservationId(observationId, a_event.sessionGeneration) },
				{ "kind", "shader" },
				{ "role", role },
				{ "pointerEvidence", PointerEvidence(pointerEvidence) },
			} });
		}

		json SerializePayload(
			const EventPayload& a_payload,
			std::uint64_t a_generation,
			const CaptureSnapshot* a_snapshot,
			std::uint64_t a_targetBindingObservationId)
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
					{ "schema", "technique-boundary-v2" },
					{ "shaderObservationId", ShaderObservationId(a_payload.words[0], a_generation) },
					{ "shaderPointer", PointerEvidence(a_payload.words[1]) },
					{ "shaderType", a_payload.words[2] },
					{ "vertexDescriptor", a_payload.words[3] },
					{ "pixelDescriptor", a_payload.words[4] },
					{ "callerRva", std::format("0x{:X}", a_payload.words[5]) },
					{ "skipPixelShader", a_payload.words[6] != 0 },
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
			case PayloadSchema::kShaderObservation:
				return SerializeShaderObservation(
					FindShaderObservation(a_snapshot, a_payload.words[0]), a_payload, a_generation);
			case PayloadSchema::kStageShaderObservation:
				return SerializeStageShaderObservation(
					FindStageShaderObservation(a_snapshot, a_payload.words[0]), a_payload, a_generation);
			case PayloadSchema::kTechniqueResolution: {
				const auto flags = a_payload.words[6];
				return {
					{ "schema", "technique-resolution-v1" },
					{ "inputVertexDescriptor", a_payload.words[0] },
					{ "inputPixelDescriptor", a_payload.words[1] },
					{ "resolvedVertexDescriptor", a_payload.words[2] },
					{ "resolvedPixelDescriptor", a_payload.words[3] },
					{ "vertexShaderObservationId", StageShaderObservationId(
						ShaderStage::kVertex, a_payload.words[4], a_generation) },
					{ "pixelShaderObservationId", StageShaderObservationId(
						ShaderStage::kPixel, a_payload.words[5], a_generation) },
					{ "vertexRoute", ShaderSelectionRouteName(
						static_cast<ShaderSelectionRoute>(flags & 0xFFu)) },
					{ "pixelRoute", ShaderSelectionRouteName(
						static_cast<ShaderSelectionRoute>((flags >> 8u) & 0xFFu)) },
					{ "shaderFound", (flags & (1ull << 16u)) != 0 },
					{ "skipPixelShader", (flags & (1ull << 17u)) != 0 },
				};
			}
			case PayloadSchema::kDrawCall: {
				const auto operation = static_cast<DrawOperation>(a_payload.words[1]);
				json arguments;
				switch (operation) {
				case DrawOperation::kDrawIndexed:
					arguments = { { "indexCount", a_payload.words[4] },
						{ "startIndexLocation", a_payload.words[5] },
						{ "baseVertexLocation", static_cast<std::int32_t>(a_payload.words[6]) } };
					break;
				case DrawOperation::kDrawInstanced:
					arguments = { { "vertexCountPerInstance", a_payload.words[4] },
						{ "instanceCount", a_payload.words[5] },
						{ "startVertexLocation", a_payload.words[6] },
						{ "startInstanceLocation", a_payload.words[7] } };
					break;
				case DrawOperation::kDrawIndexedInstanced:
					arguments = { { "indexCountPerInstance", a_payload.words[4] },
						{ "instanceCount", a_payload.words[5] },
						{ "startIndexLocation", a_payload.words[6] },
						{ "baseVertexLocation", static_cast<std::int32_t>(a_payload.words[7] & 0xFFFFFFFFu) },
						{ "startInstanceLocation", a_payload.words[7] >> 32u } };
					break;
				case DrawOperation::kDrawInstancedIndirect:
				case DrawOperation::kDrawIndexedInstancedIndirect:
					arguments = { { "argumentBufferPointer", PointerEvidence(a_payload.words[4]) },
						{ "alignedByteOffset", a_payload.words[5] } };
					break;
				case DrawOperation::kDrawAuto:
					arguments = json::object();
					break;
				default:
					arguments = { { "vertexCount", a_payload.words[4] },
						{ "startVertexLocation", a_payload.words[5] } };
					break;
				}
				return {
					{ "schema", "draw-call-v2" },
					{ "operation", DrawOperationName(operation) },
					{ "immediateContextPointer", PointerEvidence(a_payload.words[0]) },
					{ "vertexShaderObservationId", StageShaderObservationId(
						ShaderStage::kVertex, a_payload.words[2], a_generation) },
					{ "pixelShaderObservationId", StageShaderObservationId(
						ShaderStage::kPixel, a_payload.words[3], a_generation) },
					{ "targetBindingObservationId", TargetBindingObservationId(
						a_targetBindingObservationId, a_generation) },
					{ "arguments", std::move(arguments) },
				};
			}
			case PayloadSchema::kDispatchCall: {
				const auto operation = static_cast<DispatchOperation>(a_payload.words[1]);
				json arguments = operation == DispatchOperation::kDispatchIndirect ? json{
					{ "argumentBufferPointer", PointerEvidence(a_payload.words[3]) },
					{ "alignedByteOffset", a_payload.words[4] },
				} : json{
					{ "threadGroupCountX", a_payload.words[3] },
					{ "threadGroupCountY", a_payload.words[4] },
					{ "threadGroupCountZ", a_payload.words[5] },
				};
				return {
					{ "schema", "dispatch-call-v1" },
					{ "operation", DispatchOperationName(operation) },
					{ "immediateContextPointer", PointerEvidence(a_payload.words[0]) },
					{ "computeShaderObservationId", StageShaderObservationId(
						ShaderStage::kCompute, a_payload.words[2], a_generation) },
					{ "arguments", std::move(arguments) },
				};
			}
			case PayloadSchema::kDeviceContextObservation:
				return {
					{ "schema", "device-context-observation-v1" },
					{ "deviceContextObservationId", DeviceContextObservationId(
						a_payload.words[0], a_generation) },
					{ "contextPointer", PointerEvidence(a_payload.words[1]) },
					{ "pointerGeneration", a_payload.words[2] },
					{ "kind", a_payload.words[3] == 1 ? "immediate" : "unknown" },
					{ "creationEvidence", "initial-immediate-context" },
				};
			case PayloadSchema::kTargetViewObservation: {
				const auto kind = static_cast<TargetViewKind>(a_payload.words[3]);
				const auto* observation = FindTargetViewObservation(a_snapshot, a_payload.words[0]);
				return {
					{ "schema", "target-view-observation-v1" },
					{ "targetViewObservationId", TargetViewObservationId(
						kind, a_payload.words[0], a_generation) },
					{ "kind", TargetViewKindName(kind) },
					{ "d3dObjectPointer", PointerEvidence(
						observation ? observation->pointerEvidence : a_payload.words[1]) },
					{ "pointerGeneration", observation ? observation->pointerGeneration : a_payload.words[2] },
				};
			}
			case PayloadSchema::kTargetBinding:
				return SerializeTargetBinding(
					FindTargetBindingObservation(a_snapshot, a_payload.words[0]),
					a_payload.words[0], a_generation);
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
			{ "maxShaderObservations", a_config.maxShaderObservations },
			{ "maxStageShaderObservations", a_config.maxStageShaderObservations },
			{ "maxTargetViewObservations", a_config.maxTargetViewObservations },
			{ "maxTargetBindingObservations", a_config.maxTargetBindingObservations },
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
			{ "capturing", a_status.accepting },
			{ "state", !a_status.active ? "idle" : (a_status.accepting ? "capturing" : "awaiting-finalization") },
			{ "active", std::move(active) },
			{ "completedCaptureIds", a_status.completedCaptureIds },
		};
	}

	nlohmann::json SerializeCaptureSummary(const CompletedCapture& a_capture)
	{
		const auto& snapshot = a_capture.snapshot;
		const auto dropped = snapshot.statistics.droppedStopped +
			snapshot.statistics.droppedEventLimit + snapshot.statistics.droppedByteLimit;
		const auto structurallyTruncated = snapshot.statistics.droppedShaderObservations != 0 ||
			snapshot.statistics.droppedStageShaderObservations != 0 ||
			snapshot.statistics.droppedTargetViewObservations != 0 ||
			snapshot.statistics.droppedTargetBindingObservations != 0;
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
				{ "boundaryRejectionCount", snapshot.statistics.droppedFrameLimit + snapshot.statistics.droppedTimeLimit },
				{ "stopRaceRejectionCount", snapshot.statistics.droppedStopped },
				{ "scopeOverflowCount", snapshot.statistics.scopeOverflow },
				{ "scopeMismatchCount", snapshot.statistics.scopeMismatch },
				{ "shaderObservationCount", snapshot.shaderObservations.size() },
				{ "droppedShaderObservationCount", snapshot.statistics.droppedShaderObservations },
				{ "stageShaderObservationCount", snapshot.stageShaderObservations.size() },
				{ "droppedStageShaderObservationCount", snapshot.statistics.droppedStageShaderObservations },
				{ "targetViewObservationCount", snapshot.targetViewObservations.size() },
				{ "droppedTargetViewObservationCount", snapshot.statistics.droppedTargetViewObservations },
				{ "targetBindingObservationCount", snapshot.targetBindingObservations.size() },
				{ "droppedTargetBindingObservationCount", snapshot.statistics.droppedTargetBindingObservations },
				{ "truncated", dropped != 0 || structurallyTruncated },
			} },
		};
	}

	nlohmann::json SerializeEvent(
		const EventRecord& a_event,
		std::string_view a_captureId,
		std::uint32_t a_processId,
		const CaptureSnapshot* a_snapshot)
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
			{ "execution", {
				{ "observationDomain", "cpu-call" },
				{ "commandStreamSequence", a_event.commandStreamSequence == 0 ?
					json(nullptr) : json(a_event.commandStreamSequence) },
				{ "gpuTimestampTicks", nullptr },
				{ "gpuTimestampFrequencyHz", nullptr },
			} },
			{ "deviceContextObservationId", DeviceContextObservationId(
				a_event.deviceContextObservationId, a_event.sessionGeneration) },
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
			{ "observationRefs", SerializeObservationRefs(a_event, a_snapshot) },
			{ "payload", SerializePayload(
				a_event.payload, a_event.sessionGeneration, a_snapshot,
				a_event.targetBindingObservationId) },
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
			events.push_back(SerializeEvent(
				records[offset + index], a_capture.descriptor.captureId, a_processId, &a_capture.snapshot));
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
