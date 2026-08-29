#include "RenderMap/Serialization.h"

#include <algorithm>
#include <array>
#include <bit>
#include <format>
#include <string>

namespace CSX::RenderMap
{
	namespace
	{
		using json = nlohmann::json;

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

		json ResourceVersionObservationId(std::uint64_t a_observationId, std::uint64_t a_generation)
		{
			return a_observationId == 0 ? json(nullptr) :
				json(std::format("obs-resource-version-{}-g{}", a_observationId, a_generation));
		}

		json CpuMapObservationId(std::uint64_t a_observationId, std::uint64_t a_generation)
		{
			return a_observationId == 0 ? json(nullptr) :
				json(std::format("obs-cpu-map-{}-g{}", a_observationId, a_generation));
		}

		json SubmissionObservationId(std::uint64_t a_observationId, std::uint64_t a_generation)
		{
			return a_observationId == 0 ? json(nullptr) :
				json(std::format("obs-submission-{}-g{}", a_observationId, a_generation));
		}

		json GeometrySetupObservationId(std::uint64_t a_observationId, std::uint64_t a_generation)
		{
			return a_observationId == 0 ? json(nullptr) :
				json(std::format("obs-geometry-{}-g{}", a_observationId, a_generation));
		}

		float UnpackFloat(std::uint64_t a_value, bool a_high) noexcept
		{
			const auto bits = static_cast<std::uint32_t>(a_high ? a_value >> 32u : a_value);
			return std::bit_cast<float>(bits);
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
			switch (a_kind) {
			case TargetViewKind::kDepthTarget: return "depth-target";
			case TargetViewKind::kShaderResource: return "shader-resource-view";
			case TargetViewKind::kUnorderedAccess: return "unordered-access-view";
			default: return "render-target";
			}
		}

		const char* ResourceDimensionName(ResourceDimension a_dimension) noexcept
		{
			switch (a_dimension) {
			case ResourceDimension::kBuffer: return "buffer";
			case ResourceDimension::kTexture1D: return "texture-1d";
			case ResourceDimension::kTexture2D: return "texture-2d";
			case ResourceDimension::kTexture3D: return "texture-3d";
			default: return "unknown";
			}
		}

		const char* ResourceMapTypeName(std::uint32_t a_mapType) noexcept
		{
			switch (a_mapType) {
			case 1: return "read";
			case 2: return "write";
			case 3: return "read-write";
			case 4: return "write-discard";
			case 5: return "write-no-overwrite";
			default: return "unknown";
			}
		}

		const char* ResourceStageName(ResourceStage a_stage) noexcept
		{
			switch (a_stage) {
			case ResourceStage::kHull: return "hull";
			case ResourceStage::kDomain: return "domain";
			case ResourceStage::kGeometry: return "geometry";
			case ResourceStage::kPixel: return "pixel";
			case ResourceStage::kCompute: return "compute";
			case ResourceStage::kOutputMerger: return "output-merger";
			default: return "vertex";
			}
		}

		const char* ResourceBindingSourceName(ResourceBindingSource a_source) noexcept
		{
			switch (a_source) {
			case ResourceBindingSource::kPostCallQuery: return "post-call-query";
			case ResourceBindingSource::kCaptureStateSnapshot: return "capture-state-snapshot";
			default: return "requested-call";
			}
		}

		const char* TargetBindingSourceName(TargetBindingSource a_source) noexcept
		{
			switch (a_source) {
			case TargetBindingSource::kCaptureStateSnapshot: return "capture-state-snapshot";
			case TargetBindingSource::kPostCallQuery: return "post-call-query";
			default: return "observed-call";
			}
		}

		json ResourceObservationId(std::uint64_t a_observationId, std::uint64_t a_generation)
		{
			return a_observationId == 0 ? json(nullptr) :
				json(std::format("obs-resource-{}-g{}", a_observationId, a_generation));
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

		json SceneObjectObservationId(std::uint64_t a_observationId, std::uint64_t a_generation)
		{
			return a_observationId == 0 ? json(nullptr) :
				json(std::format("obs-scene-object-{}-g{}", a_observationId, a_generation));
		}

		json GeometryObservationId(std::uint64_t a_observationId, std::uint64_t a_generation)
		{
			return a_observationId == 0 ? json(nullptr) :
				json(std::format("obs-geometry-{}-g{}", a_observationId, a_generation));
		}

		json MaterialStateObservationId(std::uint64_t a_observationId, std::uint64_t a_generation)
		{
			return a_observationId == 0 ? json(nullptr) :
				json(std::format("obs-material-state-{}-g{}", a_observationId, a_generation));
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

		const ResourceObservationRecord* FindResourceObservation(
			const CaptureSnapshot* a_snapshot,
			std::uint64_t a_observationId) noexcept
		{
			if (!a_snapshot || a_observationId == 0)
				return nullptr;
			const auto found = std::find_if(
				a_snapshot->resourceObservations.begin(), a_snapshot->resourceObservations.end(),
				[&](const ResourceObservationRecord& a_record) {
					return a_record.observationId == a_observationId;
				});
			return found == a_snapshot->resourceObservations.end() ? nullptr : std::addressof(*found);
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

		const SceneObjectObservationRecord* FindSceneObjectObservation(
			const CaptureSnapshot* a_snapshot, std::uint64_t a_observationId) noexcept
		{
			if (!a_snapshot || a_observationId == 0)
				return nullptr;
			const auto found = std::find_if(
				a_snapshot->sceneObjectObservations.begin(), a_snapshot->sceneObjectObservations.end(),
				[&](const SceneObjectObservationRecord& a_record) { return a_record.observationId == a_observationId; });
			return found == a_snapshot->sceneObjectObservations.end() ? nullptr : std::addressof(*found);
		}

		const GeometryObservationRecord* FindGeometryObservation(
			const CaptureSnapshot* a_snapshot, std::uint64_t a_observationId) noexcept
		{
			if (!a_snapshot || a_observationId == 0)
				return nullptr;
			const auto found = std::find_if(
				a_snapshot->geometryObservations.begin(), a_snapshot->geometryObservations.end(),
				[&](const GeometryObservationRecord& a_record) { return a_record.observationId == a_observationId; });
			return found == a_snapshot->geometryObservations.end() ? nullptr : std::addressof(*found);
		}

		const MaterialStateObservationRecord* FindMaterialStateObservation(
			const CaptureSnapshot* a_snapshot, std::uint64_t a_observationId) noexcept
		{
			if (!a_snapshot || a_observationId == 0)
				return nullptr;
			const auto found = std::find_if(
				a_snapshot->materialStateObservations.begin(), a_snapshot->materialStateObservations.end(),
				[&](const MaterialStateObservationRecord& a_record) { return a_record.observationId == a_observationId; });
			return found == a_snapshot->materialStateObservations.end() ? nullptr : std::addressof(*found);
		}

		template <std::size_t N>
		json OptionalStoredString(const std::array<char, N>& a_value)
		{
			return a_value[0] == '\0' ? json(nullptr) : json(a_value.data());
		}

		const char* MaterialTextureRoleName(MaterialTextureRole a_role) noexcept
		{
			switch (a_role) {
			case MaterialTextureRole::kRuntimeMaterialList: return "runtime-material-list";
			case MaterialTextureRole::kEffectSource: return "effect-source";
			case MaterialTextureRole::kEffectGreyscale: return "effect-greyscale";
			case MaterialTextureRole::kWaterStaticReflection: return "water-static-reflection";
			case MaterialTextureRole::kWaterNormal1: return "water-normal-1";
			case MaterialTextureRole::kWaterNormal2: return "water-normal-2";
			case MaterialTextureRole::kWaterNormal3: return "water-normal-3";
			case MaterialTextureRole::kWaterNormal4: return "water-normal-4";
			default: return "unknown";
			}
		}

		json SerializeShaderObservation(
			const ShaderObservationRecord* a_observation,
			const EventPayload& a_payload,
			std::uint64_t a_generation)
		{
			const auto observationId = a_payload.words[0];
			if (!a_observation) {
				return {
					{ "schema", "shader-observation-v2" },
					{ "shaderObservationId", ShaderObservationId(observationId, a_generation) },
					{ "shaderPointer", PointerEvidence(a_payload.words[1]) },
					{ "pointerGeneration", a_payload.words[2] },
					{ "shaderType", a_payload.words[3] },
					{ "identityDetailsAvailable", false },
				};
			}
			return {
				{ "schema", "shader-observation-v2" },
				{ "shaderObservationId", ShaderObservationId(observationId, a_generation) },
				{ "shaderPointer", PointerEvidence(a_observation->pointerEvidence) },
				{ "pointerGeneration", a_observation->pointerGeneration },
				{ "shaderType", a_observation->shaderType },
				{ "fxpFilename", OptionalStoredString(a_observation->fxpFilename) },
				{ "imageSpaceName", OptionalStoredString(a_observation->imageSpaceName) },
				{ "compileSourceName", OptionalStoredString(a_observation->compileSourceName) },
				{ "definesSuffix", OptionalStoredString(a_observation->definesSuffix) },
				{ "truncated", {
					{ "fxpFilename", a_observation->fxpFilenameTruncated },
					{ "imageSpaceName", a_observation->imageSpaceNameTruncated },
					{ "compileSourceName", a_observation->compileSourceNameTruncated },
					{ "definesSuffix", a_observation->definesSuffixTruncated },
				} },
				{ "identityDetailsAvailable", true },
				{ "identityBasis", json::array({ "pointer", "shaderType", "fxpFilename", "imageSpaceName", "compileSourceName", "definesSuffix" }) },
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
					{ "schema", "stage-shader-observation-v3" },
					{ "stageShaderObservationId", StageShaderObservationId(stage, observationId, a_generation) },
					{ "stage", ShaderStageName(stage) },
					{ "d3dObjectPointer", PointerEvidence(a_payload.words[1]) },
					{ "wrapperPointer", PointerEvidence(a_payload.words[2]) },
					{ "pointerGeneration", a_payload.words[3] },
					{ "wrapperDescriptor", a_payload.words[5] },
					{ "bytecodeSize", a_payload.words[6] },
					{ "engineAliases", json::array() },
					{ "engineAliasTotalCount", 0 },
					{ "identityDetailsAvailable", false },
				};
			}
			json engineAliases = json::array();
			for (std::uint32_t index = 0; index < a_observation->engineAliasCount; ++index) {
				engineAliases.push_back({
					{ "loaderType", OptionalStoredString(a_observation->engineAliases[index].loaderType) },
					{ "compileSourceName", OptionalStoredString(a_observation->engineAliases[index].compileSourceName) },
					{ "descriptor", a_observation->engineAliases[index].descriptor },
					{ "truncated", {
						{ "loaderType", a_observation->engineAliases[index].loaderTypeTruncated },
						{ "compileSourceName", a_observation->engineAliases[index].compileSourceNameTruncated },
					} },
				});
			}
			return {
				{ "schema", "stage-shader-observation-v3" },
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
				{ "engineAliases", std::move(engineAliases) },
				{ "engineAliasTotalCount", a_observation->engineAliasTotalCount },
				{ "truncated", {
					{ "bytecodeSha256", a_observation->bytecodeSha256Truncated },
					{ "cachePath", a_observation->cachePathTruncated },
					{ "engineAliases", a_observation->engineAliasesTruncated },
				} },
				{ "identityDetailsAvailable", true },
				{ "identityBasis", json::array({
					"stage", "wrapper", "d3dObject", "wrapperDescriptor", "bytecodeSha256", "cachePath",
					"engineAliases" }) },
			};
		}

		json SerializeSceneObjectObservation(
			const SceneObjectObservationRecord* a_observation,
			const EventPayload& a_payload,
			std::uint64_t a_generation)
		{
			const auto observationId = a_payload.words[0];
			if (!a_observation) {
				return {
					{ "schema", "scene-object-observation-v1" },
					{ "sceneObjectObservationId", SceneObjectObservationId(observationId, a_generation) },
					{ "referencePointer", PointerEvidence(a_payload.words[1]) },
					{ "pointerGeneration", a_payload.words[2] },
					{ "identityDetailsAvailable", false },
				};
			}
			return {
				{ "schema", "scene-object-observation-v1" },
				{ "sceneObjectObservationId", SceneObjectObservationId(observationId, a_generation) },
				{ "referencePointer", PointerEvidence(a_observation->pointerEvidence) },
				{ "pointerGeneration", a_observation->pointerGeneration },
				{ "referenceFormId", std::format("0x{:08X}", a_observation->referenceFormId) },
				{ "baseFormId", a_observation->baseFormId == 0 ? json(nullptr) : json(std::format("0x{:08X}", a_observation->baseFormId)) },
				{ "referenceName", OptionalStoredString(a_observation->referenceName) },
				{ "baseFormName", OptionalStoredString(a_observation->baseFormName) },
				{ "referenceFormDynamic", a_observation->referenceFormDynamic },
				{ "baseFormDynamic", a_observation->baseFormDynamic },
				{ "truncated", {
					{ "referenceName", a_observation->referenceNameTruncated },
					{ "baseFormName", a_observation->baseFormNameTruncated },
				} },
				{ "identityDetailsAvailable", true },
			};
		}

		json SerializeGeometryObservation(
			const GeometryObservationRecord* a_observation,
			const EventPayload& a_payload,
			std::uint64_t a_generation)
		{
			const auto observationId = a_payload.words[0];
			if (!a_observation) {
				return {
					{ "schema", "geometry-observation-v1" },
					{ "geometryObservationId", GeometryObservationId(observationId, a_generation) },
					{ "geometryPointer", PointerEvidence(a_payload.words[1]) },
					{ "pointerGeneration", a_payload.words[2] },
					{ "identityDetailsAvailable", false },
				};
			}
			return {
				{ "schema", "geometry-observation-v1" },
				{ "geometryObservationId", GeometryObservationId(observationId, a_generation) },
				{ "geometryPointer", PointerEvidence(a_observation->pointerEvidence) },
				{ "pointerGeneration", a_observation->pointerGeneration },
				{ "runtimeTypeName", OptionalStoredString(a_observation->runtimeTypeName) },
				{ "name", OptionalStoredString(a_observation->name) },
				{ "geometryType", a_observation->geometryType },
				{ "vertexDescriptor", a_observation->vertexDescriptor },
				{ "sceneObjectObservationId", SceneObjectObservationId(
					a_observation->sceneObjectObservationId, a_generation) },
				{ "worldTransform", a_observation->worldTransformAvailable ? json(a_observation->worldTransform) : json(nullptr) },
				{ "worldBound", a_observation->worldBoundAvailable ? json(a_observation->worldBound) : json(nullptr) },
				{ "truncated", {
					{ "runtimeTypeName", a_observation->runtimeTypeNameTruncated },
					{ "name", a_observation->nameTruncated },
				} },
				{ "identityDetailsAvailable", true },
			};
		}

		json SerializeMaterialStateObservation(
			const MaterialStateObservationRecord* a_observation,
			const EventPayload& a_payload,
			std::uint64_t a_generation)
		{
			const auto observationId = a_payload.words[0];
			if (!a_observation) {
				return {
					{ "schema", "material-state-observation-v1" },
					{ "materialStateObservationId", MaterialStateObservationId(observationId, a_generation) },
					{ "shaderPropertyPointer", PointerEvidence(a_payload.words[1]) },
					{ "materialPointer", PointerEvidence(a_payload.words[2]) },
					{ "stateRevision", a_payload.words[3] },
					{ "fingerprint", std::format("0x{:016X}", a_payload.words[4]) },
					{ "textureBindings", json::array() },
					{ "textureBindingsTruncated", false },
					{ "identityDetailsAvailable", false },
				};
			}
			json textureBindings = json::array();
			for (std::size_t index = 0; index < a_observation->textureBindingCount; ++index) {
				const auto& binding = a_observation->textureBindings[index];
				textureBindings.push_back({
					{ "role", MaterialTextureRoleName(binding.role) },
					{ "bindingIndex", binding.bindingIndex },
					{ "niSourceTexturePointer", PointerEvidence(binding.niSourceTextureEvidence) },
					{ "path", OptionalStoredString(binding.path) },
					{ "resourceObservationId", ResourceObservationId(binding.resourceObservationId, a_generation) },
					{ "pathTruncated", binding.pathTruncated },
				});
			}
			return {
				{ "schema", "material-state-observation-v1" },
				{ "materialStateObservationId", MaterialStateObservationId(observationId, a_generation) },
				{ "stateRevision", a_observation->stateRevision },
				{ "fingerprint", std::format("0x{:016X}", a_observation->fingerprint) },
				{ "shaderPropertyPointer", PointerEvidence(a_observation->shaderPropertyEvidence) },
				{ "shaderPropertyRuntimeTypeName", OptionalStoredString(a_observation->shaderPropertyRuntimeTypeName) },
				{ "shaderPropertyFlags", a_observation->shaderPropertyFlags },
				{ "alpha", a_observation->alpha },
				{ "engineMaterialType", a_observation->engineMaterialType },
				{ "materialPointer", PointerEvidence(a_observation->materialEvidence) },
				{ "materialType", a_observation->materialType },
				{ "feature", a_observation->feature },
				{ "hashKey", a_observation->hashKey },
				{ "textureBindings", std::move(textureBindings) },
				{ "textureBindingsTruncated", a_observation->textureBindingsTruncated },
				{ "shaderPropertyAvailable", a_observation->shaderPropertyAvailable },
				{ "materialAvailable", a_observation->materialAvailable },
				{ "truncated", {
					{ "shaderPropertyRuntimeTypeName", a_observation->shaderPropertyRuntimeTypeNameTruncated },
				} },
				{ "identityDetailsAvailable", true },
			};
		}

		json SerializeTargetBinding(
			const TargetBindingObservationRecord* a_binding,
			std::uint64_t a_observationId,
			std::uint64_t a_generation,
			TargetBindingSource a_source)
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
				{ "schema", "render-target-binding-v2" },
				{ "source", TargetBindingSourceName(a_source) },
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
			case PayloadSchema::kSceneObjectObservation:
				return json::array({ {
					{ "id", SceneObjectObservationId(a_event.payload.words[0], a_event.sessionGeneration) },
					{ "kind", "scene-object" },
					{ "role", "first-observed" },
					{ "pointerEvidence", PointerEvidence(a_event.payload.words[1]) },
				} });
			case PayloadSchema::kGeometryObservation: {
				json refs = json::array({ {
					{ "id", GeometryObservationId(a_event.payload.words[0], a_event.sessionGeneration) },
					{ "kind", "geometry" },
					{ "role", "first-observed" },
					{ "pointerEvidence", PointerEvidence(a_event.payload.words[1]) },
				} });
				if (a_event.payload.words[5] != 0) {
					const auto* object = FindSceneObjectObservation(a_snapshot, a_event.payload.words[5]);
					refs.push_back({
						{ "id", SceneObjectObservationId(a_event.payload.words[5], a_event.sessionGeneration) },
						{ "kind", "scene-object" },
						{ "role", "represented-object" },
						{ "pointerEvidence", PointerEvidence(object ? object->pointerEvidence : 0) },
					});
				}
				return refs;
			}
			case PayloadSchema::kMaterialStateObservation:
				return json::array({ {
					{ "id", MaterialStateObservationId(a_event.payload.words[0], a_event.sessionGeneration) },
					{ "kind", "material" },
					{ "role", "first-observed" },
					{ "pointerEvidence", PointerEvidence(a_event.payload.words[1]) },
				} });
			case PayloadSchema::kGeometryBoundaryV2: {
				json refs = json::array();
				if (a_event.payload.words[6] != 0) {
					const auto* geometry = FindGeometryObservation(a_snapshot, a_event.payload.words[6]);
					refs.push_back({
						{ "id", GeometryObservationId(a_event.payload.words[6], a_event.sessionGeneration) },
						{ "kind", "geometry" },
						{ "role", "setup-geometry" },
						{ "pointerEvidence", PointerEvidence(geometry ? geometry->pointerEvidence : a_event.payload.words[2]) },
					});
				}
				if (a_event.payload.words[7] != 0) {
					const auto* material = FindMaterialStateObservation(a_snapshot, a_event.payload.words[7]);
					refs.push_back({
						{ "id", MaterialStateObservationId(a_event.payload.words[7], a_event.sessionGeneration) },
						{ "kind", "material" },
						{ "role", "setup-material" },
						{ "pointerEvidence", PointerEvidence(material ? material->shaderPropertyEvidence : 0) },
					});
				}
				return refs;
			}
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
				if (a_event.preparedGeometrySetupObservationId != 0) {
					refs.push_back({
						{ "id", GeometrySetupObservationId(
							a_event.preparedGeometrySetupObservationId, a_event.sessionGeneration) },
						{ "kind", "geometry-setup" },
						{ "role", "prepared-at-draw" },
						{ "pointerEvidence", nullptr },
					});
				}
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
				json refs = json::array({ {
					{ "id", TargetViewObservationId(kind, a_event.payload.words[0], a_event.sessionGeneration) },
					{ "kind", TargetViewKindName(kind) },
					{ "role", "first-observed" },
					{ "pointerEvidence", PointerEvidence(a_event.payload.words[1]) },
				} });
				if (a_event.payload.words[4] != 0) {
					refs.push_back({
						{ "id", ResourceObservationId(a_event.payload.words[4], a_event.sessionGeneration) },
						{ "kind", "resource" }, { "role", "view-resource" }, { "pointerEvidence", nullptr },
					});
				}
				return refs;
			}
			case PayloadSchema::kResourceObservation:
				return json::array({ {
					{ "id", ResourceObservationId(a_event.payload.words[0], a_event.sessionGeneration) },
					{ "kind", "resource" }, { "role", "first-observed" },
					{ "pointerEvidence", PointerEvidence(a_event.payload.words[1]) },
				} });
			case PayloadSchema::kResourceViewBinding: {
				if (a_event.payload.words[0] == 0)
					return json::array();
				const auto kind = static_cast<ResourceBindingKind>(a_event.payload.words[1]) == ResourceBindingKind::kShaderResource ?
					TargetViewKind::kShaderResource : TargetViewKind::kUnorderedAccess;
				return json::array({ {
					{ "id", TargetViewObservationId(kind, a_event.payload.words[0], a_event.sessionGeneration) },
					{ "kind", TargetViewKindName(kind) }, { "role", "bound-view" }, { "pointerEvidence", nullptr },
				} });
			}
			case PayloadSchema::kResourceViewStateObserved:
				return json::array();
			case PayloadSchema::kResourceFlow: {
				json refs = json::array();
				for (const auto [word, roleName] : std::array{
					std::pair{ std::size_t{ 1 }, "source" }, std::pair{ std::size_t{ 2 }, "destination" } }) {
					if (a_event.payload.words[word] != 0) {
						refs.push_back({
							{ "id", ResourceObservationId(a_event.payload.words[word], a_event.sessionGeneration) },
							{ "kind", "resource" }, { "role", roleName }, { "pointerEvidence", nullptr },
						});
					}
				}
				return refs;
			}
			case PayloadSchema::kResourceCpuAccess: {
				json refs = json::array();
				if (a_event.payload.words[1] != 0) {
					refs.push_back({
						{ "id", CpuMapObservationId(a_event.payload.words[1], a_event.sessionGeneration) },
						{ "kind", "resource-cpu-access" }, { "role", "map-lifetime" },
						{ "pointerEvidence", nullptr },
					});
				}
				refs.push_back({
						{ "id", ResourceObservationId(a_event.payload.words[2], a_event.sessionGeneration) },
						{ "kind", "resource" }, { "role", "cpu-accessed-resource" },
						{ "pointerEvidence", nullptr },
					});
				return refs;
			}
			case PayloadSchema::kResourceVersion:
				return json::array({
					{
						{ "id", ResourceVersionObservationId(a_event.payload.words[0], a_event.sessionGeneration) },
						{ "kind", "resource-version" }, { "role", "first-observed" }, { "pointerEvidence", nullptr },
					},
					{
						{ "id", ResourceObservationId(a_event.payload.words[1], a_event.sessionGeneration) },
						{ "kind", "resource" }, { "role", "versioned-resource" }, { "pointerEvidence", nullptr },
					},
				});
			case PayloadSchema::kVisibilityResult:
				return json::array({ {
					{ "id", ResourceVersionObservationId(a_event.payload.words[0], a_event.sessionGeneration) },
					{ "kind", "resource-version" }, { "role", "visibility-result" }, { "pointerEvidence", nullptr },
				} });
			case PayloadSchema::kVisibilitySubmission:
				return json::array({
					{
						{ "id", SubmissionObservationId(a_event.payload.words[0], a_event.sessionGeneration) },
						{ "kind", "submission" }, { "role", "visibility-consumer" }, { "pointerEvidence", nullptr },
					},
					{
						{ "id", ResourceVersionObservationId(a_event.payload.words[4], a_event.sessionGeneration) },
						{ "kind", "resource-version" }, { "role", "consumed-visibility-result" }, { "pointerEvidence", nullptr },
					},
				});
			case PayloadSchema::kEyeSubmission:
				return json::array({ {
					{ "id", ResourceObservationId(a_event.payload.words[0], a_event.sessionGeneration) },
					{ "kind", "resource" }, { "role", "submitted-eye-texture" }, { "pointerEvidence", nullptr },
				} });
			case PayloadSchema::kCullDecision:
				return json::array({ {
					{ "id", ResourceVersionObservationId(a_event.payload.words[0], a_event.sessionGeneration) },
					{ "kind", "resource-version" }, { "role", "read-back-visibility-result" }, { "pointerEvidence", nullptr },
				} });
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
			std::uint64_t a_targetBindingObservationId,
			std::uint64_t a_submissionObservationId,
			std::uint64_t a_preparedGeometrySetupObservationId)
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
			case PayloadSchema::kGeometryBoundaryV2:
				return {
					{ "schema", "geometry-boundary-v2" },
					{ "shaderPointer", PointerEvidence(a_payload.words[0]) },
					{ "renderPassPointer", PointerEvidence(a_payload.words[1]) },
					{ "geometryPointer", PointerEvidence(a_payload.words[2]) },
					{ "shaderType", a_payload.words[3] },
					{ "passEnum", a_payload.words[4] },
					{ "renderFlags", a_payload.words[5] },
					{ "geometryObservationId", GeometryObservationId(a_payload.words[6], a_generation) },
					{ "materialStateObservationId", MaterialStateObservationId(a_payload.words[7], a_generation) },
				};
			case PayloadSchema::kSceneObjectObservation:
				return SerializeSceneObjectObservation(
					FindSceneObjectObservation(a_snapshot, a_payload.words[0]), a_payload, a_generation);
			case PayloadSchema::kGeometryObservation:
				return SerializeGeometryObservation(
					FindGeometryObservation(a_snapshot, a_payload.words[0]), a_payload, a_generation);
			case PayloadSchema::kMaterialStateObservation:
				return SerializeMaterialStateObservation(
					FindMaterialStateObservation(a_snapshot, a_payload.words[0]), a_payload, a_generation);
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
					{ "schema", "draw-call-v3" },
					{ "operation", DrawOperationName(operation) },
					{ "immediateContextPointer", PointerEvidence(a_payload.words[0]) },
					{ "vertexShaderObservationId", StageShaderObservationId(
						ShaderStage::kVertex, a_payload.words[2], a_generation) },
					{ "pixelShaderObservationId", StageShaderObservationId(
						ShaderStage::kPixel, a_payload.words[3], a_generation) },
					{ "targetBindingObservationId", TargetBindingObservationId(
						a_targetBindingObservationId, a_generation) },
					{ "submissionObservationId", SubmissionObservationId(
						a_submissionObservationId, a_generation) },
					{ "preparedGeometrySetupObservationId", GeometrySetupObservationId(
						a_preparedGeometrySetupObservationId, a_generation) },
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
					{ "resourceObservationId", ResourceObservationId(
						observation ? observation->resourceObservationId : a_payload.words[4], a_generation) },
					{ "format", observation ? observation->format : 0 },
					{ "viewDimension", observation ? observation->dimension : 0 },
					{ "subresources", {
						{ "mipSliceOrFirstMip", observation ? observation->mipSlice : 0 },
						{ "firstArraySlice", observation ? observation->firstArraySlice : 0 },
						{ "arraySizeOrMipCount", observation ? observation->arraySize : 0 },
						{ "firstElement", observation ? observation->firstElement : 0 },
						{ "elementCountOrArraySize", observation ? observation->elementCount : 0 },
					} },
					{ "flags", observation ? observation->flags : 0 },
				};
			}
			case PayloadSchema::kResourceObservation: {
				const auto* observation = FindResourceObservation(a_snapshot, a_payload.words[0]);
				return {
					{ "schema", "resource-observation-v1" },
					{ "resourceObservationId", ResourceObservationId(a_payload.words[0], a_generation) },
					{ "d3dObjectPointer", PointerEvidence(observation ? observation->d3dObject : a_payload.words[1]) },
					{ "pointerGeneration", observation ? observation->pointerGeneration : a_payload.words[2] },
					{ "dimension", ResourceDimensionName(observation ? observation->dimension :
						static_cast<ResourceDimension>(a_payload.words[3])) },
					{ "widthOrBytes", observation ? observation->widthOrBytes : 0 },
					{ "height", observation ? observation->height : 0 },
					{ "depthOrArraySize", observation ? observation->depthOrArraySize : 0 },
					{ "mipLevels", observation ? observation->mipLevels : 0 },
					{ "format", observation ? observation->format : 0 },
					{ "sampleCount", observation ? observation->sampleCount : 0 },
					{ "sampleQuality", observation ? observation->sampleQuality : 0 },
					{ "usage", observation ? observation->usage : 0 },
					{ "bindFlags", observation ? observation->bindFlags : 0 },
					{ "cpuAccessFlags", observation ? observation->cpuAccessFlags : 0 },
					{ "miscFlags", observation ? observation->miscFlags : 0 },
					{ "structureByteStride", observation ? observation->structureByteStride : 0 },
				};
			}
			case PayloadSchema::kResourceViewBinding: {
				const auto bindingKind = static_cast<ResourceBindingKind>(a_payload.words[1]);
				const auto viewKind = bindingKind == ResourceBindingKind::kShaderResource ?
					TargetViewKind::kShaderResource : TargetViewKind::kUnorderedAccess;
				return {
					{ "schema", "resource-view-binding-v2" },
					{ "viewObservationId", TargetViewObservationId(viewKind, a_payload.words[0], a_generation) },
					{ "bindingKind", bindingKind == ResourceBindingKind::kShaderResource ? "shader-resource" : "unordered-access" },
					{ "stage", ResourceStageName(static_cast<ResourceStage>(a_payload.words[2])) },
					{ "slot", a_payload.words[3] },
					{ "source", ResourceBindingSourceName(
						static_cast<ResourceBindingSource>(a_payload.words[4])) },
				};
			}
			case PayloadSchema::kResourceViewStateObserved:
				return {
					{ "schema", "resource-view-state-observed-v1" },
					{ "bindingKind", static_cast<ResourceBindingKind>(a_payload.words[0]) ==
						ResourceBindingKind::kShaderResource ? "shader-resource" : "unordered-access" },
					{ "stage", ResourceStageName(static_cast<ResourceStage>(a_payload.words[1])) },
					{ "startSlot", a_payload.words[2] },
					{ "count", a_payload.words[3] },
					{ "source", ResourceBindingSourceName(
						static_cast<ResourceBindingSource>(a_payload.words[4])) },
					{ "changedSlotCount", a_payload.words[5] },
				};
			case PayloadSchema::kResourceFlow: {
				const auto operation = static_cast<ResourceFlowOperation>(a_payload.words[0]);
				const char* name = "unknown";
				switch (operation) {
				case ResourceFlowOperation::kCopyResource: name = "copy-resource"; break;
				case ResourceFlowOperation::kCopySubresourceRegion: name = "copy-subresource-region"; break;
				case ResourceFlowOperation::kResolveSubresource: name = "resolve-subresource"; break;
				case ResourceFlowOperation::kUpdateSubresource: name = "update-subresource"; break;
				case ResourceFlowOperation::kCopyStructureCount: name = "copy-structure-count"; break;
				case ResourceFlowOperation::kClearRenderTarget: name = "clear-render-target"; break;
				case ResourceFlowOperation::kClearUnorderedAccess: name = "clear-unordered-access"; break;
				case ResourceFlowOperation::kClearDepthStencil: name = "clear-depth-stencil"; break;
				case ResourceFlowOperation::kGenerateMips: name = "generate-mips"; break;
				}
				return {
					{ "schema", "resource-flow-v1" }, { "operation", name },
					{ "sourceResourceObservationId", ResourceObservationId(a_payload.words[1], a_generation) },
					{ "destinationResourceObservationId", ResourceObservationId(a_payload.words[2], a_generation) },
					{ "sourceSubresource", a_payload.words[3] },
					{ "destinationSubresource", a_payload.words[4] },
				};
			}
			case PayloadSchema::kResourceCpuAccess: {
				const auto phase = static_cast<ResourceCpuAccessPhase>(a_payload.words[0]);
				const auto mapType = static_cast<std::uint32_t>(a_payload.words[4]);
				const auto mapFlags = static_cast<std::uint32_t>(a_payload.words[4] >> 32u);
				const auto result = static_cast<std::uint32_t>(a_payload.words[6]);
				const auto succeeded = phase == ResourceCpuAccessPhase::kUnmap ||
					(result & 0x80000000u) == 0;
				const auto readable = mapType == 1 || mapType == 3;
				const auto writable = mapType >= 2 && mapType <= 5;
				return {
					{ "schema", "resource-cpu-access-v1" },
					{ "phase", phase == ResourceCpuAccessPhase::kMap ? "map" : "unmap" },
					{ "mapObservationId", CpuMapObservationId(a_payload.words[1], a_generation) },
					{ "resourceObservationId", ResourceObservationId(a_payload.words[2], a_generation) },
					{ "subresource", a_payload.words[3] },
					{ "mapType", ResourceMapTypeName(mapType) },
					{ "mapTypeValue", mapType },
					{ "mapFlags", mapFlags },
					{ "doNotWait", (mapFlags & 0x100000u) != 0 },
					{ "resultHresult", phase == ResourceCpuAccessPhase::kMap ?
						json(std::format("0x{:08X}", result)) : json(nullptr) },
					{ "succeeded", succeeded },
					{ "matchedMap", phase == ResourceCpuAccessPhase::kUnmap ?
						json(a_payload.words[1] != 0) : json(nullptr) },
					{ "readable", readable }, { "writable", writable },
					{ "durationQpcTicks", a_payload.words[5] },
					{ "rowPitch", static_cast<std::uint32_t>(a_payload.words[7]) },
					{ "depthPitch", static_cast<std::uint32_t>(a_payload.words[7] >> 32u) },
					{ "visibilityBoundary", phase == ResourceCpuAccessPhase::kMap && succeeded && readable ?
						"cpu-readable-after-map-return" : nullptr },
					{ "publicationBoundary", phase == ResourceCpuAccessPhase::kUnmap &&
						a_payload.words[1] != 0 && writable ? "gpu-visible-after-unmap-return" : nullptr },
				};
			}
			case PayloadSchema::kResourceVersion: {
				const auto readiness = static_cast<ResourceReadinessDomain>(a_payload.words[6]);
				const auto eye = static_cast<Eye>(a_payload.words[7] & 0xFFu);
				return {
					{ "schema", "resource-version-observation-v1" },
					{ "resourceVersionObservationId", ResourceVersionObservationId(a_payload.words[0], a_generation) },
					{ "resourceObservationId", ResourceObservationId(a_payload.words[1], a_generation) },
					{ "subresources", { { "first", a_payload.words[2] }, { "count", a_payload.words[3] } } },
					{ "writeEpoch", a_payload.words[4] },
					{ "producerFrame", OptionalFrame(a_payload.words[5]) },
					{ "readinessDomain", readiness == ResourceReadinessDomain::kSameImmediateContextOrder ?
						"same-immediate-context-order" : "unknown" },
					{ "eye", EyeName(eye) },
					{ "eyeMask", (a_payload.words[7] >> 8u) == 0 ? json(nullptr) : json(a_payload.words[7] >> 8u) },
				};
			}
			case PayloadSchema::kVisibilityCandidate:
				return {
					{ "schema", "visibility-candidate-v1" },
					{ "objectPointer", PointerEvidence(a_payload.words[0]) },
					{ "objectIndex", a_payload.words[1] },
					{ "producerFrame", OptionalFrame(a_payload.words[2]) },
				};
			case PayloadSchema::kVisibilityResult:
				return {
					{ "schema", "visibility-result-ready-v1" },
					{ "resourceVersionObservationId", ResourceVersionObservationId(a_payload.words[0], a_generation) },
					{ "viewObservationId", TargetViewObservationId(
						TargetViewKind::kShaderResource, a_payload.words[1], a_generation) },
					{ "objectCount", a_payload.words[2] },
					{ "producerFrame", OptionalFrame(a_payload.words[3]) },
				};
			case PayloadSchema::kVisibilitySubmission: {
				const auto flags = a_payload.words[7];
				return {
					{ "schema", "visibility-submission-v1" },
					{ "submissionObservationId", SubmissionObservationId(a_payload.words[0], a_generation) },
					{ "renderPassPointer", PointerEvidence(a_payload.words[1]) },
					{ "geometryPointer", PointerEvidence(a_payload.words[2]) },
					{ "objectIndex", a_payload.words[3] },
					{ "resourceVersionObservationId", ResourceVersionObservationId(a_payload.words[4], a_generation) },
					{ "requestedViewObservationId", TargetViewObservationId(
						TargetViewKind::kShaderResource, a_payload.words[5], a_generation) },
					{ "effectiveViewObservationId", TargetViewObservationId(
						TargetViewKind::kShaderResource, a_payload.words[6], a_generation) },
					{ "category", flags & 0xFFFFu },
					{ "slot", (flags >> 16u) & 0xFFFFu },
					{ "bindingMatches", (flags & (1ull << 32u)) != 0 },
					{ "forcedVisible", (flags & (1ull << 33u)) != 0 },
				};
			}
			case PayloadSchema::kEyeSubmission:
				return {
					{ "schema", "eye-submission-v1" },
					{ "resourceObservationId", ResourceObservationId(a_payload.words[0], a_generation) },
					{ "eye", EyeName(static_cast<Eye>(a_payload.words[1])) },
					{ "eyeMask", a_payload.words[2] == 0 ? json(nullptr) : json(a_payload.words[2]) },
					{ "bounds", {
						{ "uMin", UnpackFloat(a_payload.words[3], false) },
						{ "vMin", UnpackFloat(a_payload.words[3], true) },
						{ "uMax", UnpackFloat(a_payload.words[4], false) },
						{ "vMax", UnpackFloat(a_payload.words[4], true) },
					} },
					{ "submitFlags", a_payload.words[5] },
					{ "compositorCycle", a_payload.words[6] },
				};
			case PayloadSchema::kCullDecision:
				return {
					{ "schema", "cull-decision-v1" },
					{ "resourceVersionObservationId", ResourceVersionObservationId(a_payload.words[0], a_generation) },
					{ "objectIndex", a_payload.words[1] },
					{ "producerVisible", a_payload.words[2] != 0 },
					{ "drawCounts", {
						{ "total", a_payload.words[3] },
						{ "lighting", a_payload.words[4] },
						{ "distantTree", a_payload.words[5] },
						{ "grass", a_payload.words[6] },
					} },
					{ "producerFrame", OptionalFrame(a_payload.words[7]) },
					{ "readinessDomain", "cpu-readback-complete" },
				};
			case PayloadSchema::kTargetBinding:
				return SerializeTargetBinding(
					FindTargetBindingObservation(a_snapshot, a_payload.words[0]),
					a_payload.words[0], a_generation,
					static_cast<TargetBindingSource>(a_payload.words[2]));
			default:
				return {
					{ "schema", std::format("unknown-{}", a_payload.schema) },
					{ "words", a_payload.words },
				};
			}
		}
	}

	nlohmann::json SerializeEventKindMask(EventKindMask a_mask)
	{
		json result = json::array();
		for (std::uint16_t index = 0; index < static_cast<std::uint16_t>(EventKind::kCount); ++index) {
			const auto kind = static_cast<EventKind>(index);
			if ((a_mask & EventKindBit(kind)) != 0)
				result.push_back(EventKindName(kind));
		}
		return result;
	}

	nlohmann::json SerializeGeometryShaderTypeMask(std::uint64_t a_mask)
	{
		json result = json::array();
		for (std::uint32_t shaderType = 0; shaderType < 64; ++shaderType) {
			if ((a_mask & (std::uint64_t{ 1 } << shaderType)) != 0)
				result.push_back(shaderType);
		}
		return result;
	}

	nlohmann::json SerializeBounds(const CollectorConfig& a_config)
	{
		return {
			{ "requestedEventKinds", SerializeEventKindMask(a_config.requestedEventKindMask) },
			{ "resolvedEventKinds", SerializeEventKindMask(a_config.eventKindMask) },
			{ "maxFrames", a_config.maxFrames },
			{ "maxDurationMs", std::chrono::duration_cast<std::chrono::milliseconds>(a_config.maxDuration).count() },
			{ "maxEvents", a_config.maxEvents },
			{ "maxBytes", a_config.maxBytes },
			{ "maxScopeDepth", a_config.maxScopeDepth },
			{ "maxShaderObservations", a_config.maxShaderObservations },
			{ "maxStageShaderObservations", a_config.maxStageShaderObservations },
			{ "maxResourceObservations", a_config.maxResourceObservations },
			{ "maxTargetViewObservations", a_config.maxTargetViewObservations },
			{ "maxTargetBindingObservations", a_config.maxTargetBindingObservations },
			{ "maxSceneObjectObservations", a_config.maxSceneObjectObservations },
			{ "maxGeometryObservations", a_config.maxGeometryObservations },
			{ "maxMaterialStateObservations", a_config.maxMaterialStateObservations },
			{ "geometryShaderTypes", SerializeGeometryShaderTypeMask(a_config.geometryShaderTypeMask) },
			{ "executionWithinSelectedGeometry", a_config.executionWithinSelectedGeometry },
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
			snapshot.statistics.droppedResourceObservations != 0 ||
			snapshot.statistics.droppedTargetViewObservations != 0 ||
			snapshot.statistics.droppedTargetBindingObservations != 0 ||
			snapshot.statistics.droppedSceneObjectObservations != 0 ||
			snapshot.statistics.droppedGeometryObservations != 0 ||
			snapshot.statistics.droppedMaterialStateObservations != 0;
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
				{ "filteredEventCount", snapshot.statistics.filtered },
				{ "droppedEventCount", dropped },
				{ "boundaryRejectionCount", snapshot.statistics.droppedFrameLimit + snapshot.statistics.droppedTimeLimit },
				{ "stopRaceRejectionCount", snapshot.statistics.droppedStopped },
				{ "scopeOverflowCount", snapshot.statistics.scopeOverflow },
				{ "scopeMismatchCount", snapshot.statistics.scopeMismatch },
				{ "shaderObservationCount", snapshot.shaderObservations.size() },
				{ "droppedShaderObservationCount", snapshot.statistics.droppedShaderObservations },
				{ "stageShaderObservationCount", snapshot.stageShaderObservations.size() },
				{ "droppedStageShaderObservationCount", snapshot.statistics.droppedStageShaderObservations },
				{ "resourceObservationCount", snapshot.resourceObservations.size() },
				{ "droppedResourceObservationCount", snapshot.statistics.droppedResourceObservations },
				{ "targetViewObservationCount", snapshot.targetViewObservations.size() },
				{ "droppedTargetViewObservationCount", snapshot.statistics.droppedTargetViewObservations },
				{ "targetBindingObservationCount", snapshot.targetBindingObservations.size() },
				{ "droppedTargetBindingObservationCount", snapshot.statistics.droppedTargetBindingObservations },
				{ "sceneObjectObservationCount", snapshot.sceneObjectObservations.size() },
				{ "droppedSceneObjectObservationCount", snapshot.statistics.droppedSceneObjectObservations },
				{ "geometryObservationCount", snapshot.geometryObservations.size() },
				{ "droppedGeometryObservationCount", snapshot.statistics.droppedGeometryObservations },
				{ "materialStateObservationCount", snapshot.materialStateObservations.size() },
				{ "droppedMaterialStateObservationCount", snapshot.statistics.droppedMaterialStateObservations },
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
			{ "submissionObservationId", SubmissionObservationId(
				a_event.submissionObservationId, a_event.sessionGeneration) },
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
				a_event.targetBindingObservationId,
				a_event.submissionObservationId,
				a_event.preparedGeometrySetupObservationId) },
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
