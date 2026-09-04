#include "RenderMap/Runtime.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <functional>

namespace CSX::RenderMap
{
	namespace
	{
		struct PendingVisibilitySubmission
		{
			const Runtime* owner{ nullptr };
			std::uint64_t generation{ 0 };
			std::uint64_t observationId{ 0 };
			std::uintptr_t context{ 0 };
		};

		struct PendingGeometrySubmission
		{
			const Runtime* owner{ nullptr };
			std::uint64_t generation{ 0 };
			std::uint64_t observationId{ 0 };
			std::uintptr_t context{ 0 };
		};

		thread_local PendingVisibilitySubmission pendingVisibilitySubmission;
		thread_local PendingGeometrySubmission pendingGeometrySubmission;

		std::uint64_t PackFloats(float a_low, float a_high) noexcept
		{
			return static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(a_low)) |
			       (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(a_high)) << 32u);
		}

		EventPayload RenderPassPayload(const RenderPassBoundary& a_boundary) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kRenderPassBoundary),
				.words = {
					a_boundary.renderPass,
					a_boundary.geometry,
					a_boundary.technique,
					a_boundary.passEnum,
					a_boundary.renderFlags,
					a_boundary.alphaTest ? 1u : 0u,
				},
			};
		}

		EventPayload TechniquePayload(
			const TechniqueBoundary& a_boundary,
			std::uint64_t a_shaderObservationId) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kTechniqueBoundary),
				.words = {
					a_shaderObservationId,
					a_boundary.shader,
					a_boundary.shaderType,
					a_boundary.vertexDescriptor,
					a_boundary.pixelDescriptor,
					a_boundary.callerRva,
					a_boundary.skipPixelShader ? 1u : 0u,
				},
			};
		}

		EventPayload ShaderObservationPayload(
			const TechniqueBoundary& a_boundary,
			const ShaderObservationResult& a_observation) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kShaderObservation),
				.words = {
					a_observation.observationId,
					a_boundary.shader,
					a_observation.pointerGeneration,
					a_boundary.shaderType,
				},
			};
		}

		EventPayload StageShaderObservationPayload(
			const StageShaderObservationInput& a_shader,
			const StageShaderObservationResult& a_observation) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kStageShaderObservation),
				.words = {
					a_observation.observationId,
					a_shader.d3dObject,
					a_shader.wrapper,
					a_observation.pointerGeneration,
					static_cast<std::uint64_t>(a_shader.stage),
					a_shader.wrapperDescriptor,
					a_shader.bytecodeSize,
					a_shader.bytecodeSha256.empty() ? 0u : 1u,
				},
			};
		}

		EventPayload TechniqueResolutionPayload(
			const TechniqueResolution& a_resolution,
			std::uint64_t a_vertexObservationId,
			std::uint64_t a_pixelObservationId) noexcept
		{
			const auto flags = static_cast<std::uint64_t>(a_resolution.vertex.route) |
			                   (static_cast<std::uint64_t>(a_resolution.pixel.route) << 8u) |
			                   (a_resolution.shaderFound ? (1ull << 16u) : 0u) |
			                   (a_resolution.skipPixelShader ? (1ull << 17u) : 0u);
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kTechniqueResolution),
				.words = {
					a_resolution.inputVertexDescriptor,
					a_resolution.inputPixelDescriptor,
					a_resolution.resolvedVertexDescriptor,
					a_resolution.resolvedPixelDescriptor,
					a_vertexObservationId,
					a_pixelObservationId,
					flags,
				},
			};
		}

		EventPayload DrawCallPayload(
			std::uintptr_t a_context,
			DrawOperation a_operation,
			std::uint64_t a_vertexObservationId,
			std::uint64_t a_pixelObservationId,
			std::uint64_t a_argument0,
			std::uint64_t a_argument1,
			std::uint64_t a_argument2,
			std::uint64_t a_argument3) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kDrawCall),
				.words = {
					a_context,
					static_cast<std::uint64_t>(a_operation),
					a_vertexObservationId,
					a_pixelObservationId,
					a_argument0,
					a_argument1,
					a_argument2,
					a_argument3,
				},
			};
		}

		EventPayload DispatchCallPayload(
			std::uintptr_t a_context,
			DispatchOperation a_operation,
			std::uint64_t a_computeObservationId,
			std::uint64_t a_argument0,
			std::uint64_t a_argument1,
			std::uint64_t a_argument2,
			std::uint64_t a_argument3) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kDispatchCall),
				.words = {
					a_context,
					static_cast<std::uint64_t>(a_operation),
					a_computeObservationId,
					a_argument0,
					a_argument1,
					a_argument2,
					a_argument3,
				},
			};
		}

		EventPayload GeometryPayload(
			const GeometryBoundary& a_boundary,
			std::uint64_t a_geometryObservationId,
			std::uint64_t a_materialStateObservationId) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kGeometryBoundaryV2),
				.words = {
					a_boundary.shader,
					a_boundary.renderPass,
					a_boundary.geometry,
					a_boundary.shaderType,
					a_boundary.passEnum,
					a_boundary.renderFlags,
					a_geometryObservationId,
					a_materialStateObservationId,
				},
			};
		}

		EventPayload SceneObjectObservationPayload(
			const SceneObjectObservationInput& a_object,
			const SceneObjectObservationResult& a_observation) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kSceneObjectObservation),
				.words = {
					a_observation.observationId,
					a_object.reference,
					a_observation.pointerGeneration,
					a_object.referenceFormId,
					a_object.baseFormId,
					a_object.referenceFormDynamic ? 1u : 0u,
					a_object.baseFormDynamic ? 1u : 0u,
				},
			};
		}

		EventPayload GeometryObservationPayload(
			const GeometryObservationInput& a_geometry,
			const GeometryObservationResult& a_observation) noexcept
		{
			const auto availability = (a_geometry.worldTransformAvailable ? 1ull : 0ull) |
			                          (a_geometry.worldBoundAvailable ? 2ull : 0ull);
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kGeometryObservation),
				.words = {
					a_observation.observationId,
					a_geometry.geometry,
					a_observation.pointerGeneration,
					a_geometry.geometryType,
					a_geometry.vertexDescriptor,
					a_geometry.sceneObjectObservationId,
					availability,
				},
			};
		}

		EventPayload MaterialStateObservationPayload(
			const MaterialStateObservationInput& a_material,
			const MaterialStateObservationResult& a_observation) noexcept
		{
			const auto availability = (a_material.shaderPropertyAvailable ? 1ull : 0ull) |
			                          (a_material.materialAvailable ? 2ull : 0ull);
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kMaterialStateObservation),
				.words = {
					a_observation.observationId,
					a_material.shaderProperty,
					a_material.material,
					a_observation.stateRevision,
					a_observation.fingerprint,
					a_material.shaderPropertyFlags,
					availability,
				},
			};
		}

		EventPayload DeviceContextObservationPayload(
			std::uint64_t a_observationId,
			std::uintptr_t a_context,
			std::uint64_t a_pointerGeneration,
			DeviceContextKind a_kind,
			ContextCreationEvidence a_creationEvidence,
			std::uint32_t a_contextFlags = 0) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kDeviceContextObservation),
				.words = {
					a_observationId,
					a_context,
					a_pointerGeneration,
					static_cast<std::uint64_t>(a_kind),
					static_cast<std::uint64_t>(a_creationEvidence),
					a_contextFlags,
				},
			};
		}

		EventPayload CommandRecordingObservationPayload(
			std::uint64_t a_observationId, std::uint64_t a_contextObservationId,
			std::uint64_t a_epoch, bool a_partialAtCaptureStart) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kCommandRecordingObservation),
				.words = { a_observationId, a_contextObservationId, a_epoch,
					a_partialAtCaptureStart ? 1u : 0u },
			};
		}

		EventPayload CommandListObservationPayload(
			std::uint64_t a_observationId, std::uintptr_t a_commandList,
			std::uint64_t a_pointerGeneration, std::uint64_t a_sourceContextObservationId,
			std::uint64_t a_sourceRecordingObservationId, bool a_sourceRecordingComplete,
			std::uint64_t a_sourceRecordingIncompleteReasons) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kCommandListObservation),
				.words = { a_observationId, a_commandList, a_pointerGeneration,
					a_sourceContextObservationId, a_sourceRecordingObservationId,
					a_sourceRecordingComplete ? 1u : 0u,
					a_sourceRecordingIncompleteReasons },
			};
		}

		EventPayload FinishCommandListPayload(
			std::uint64_t a_recordingObservationId, std::uint64_t a_commandListObservationId,
			std::uintptr_t a_commandList, bool a_restoreDeferredContextState,
			std::int32_t a_result, bool a_sourceRecordingComplete,
			std::uint64_t a_sourceRecordingIncompleteReasons) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kFinishCommandList),
				.words = { a_recordingObservationId, a_commandListObservationId, a_commandList,
					a_restoreDeferredContextState ? 1u : 0u,
					static_cast<std::uint32_t>(a_result),
					a_sourceRecordingComplete ? 1u : 0u,
					a_sourceRecordingIncompleteReasons },
			};
		}

		EventPayload ExecuteCommandListPayload(
			std::uint64_t a_commandListObservationId, std::uintptr_t a_commandList,
			std::uint64_t a_sourceRecordingObservationId, bool a_restoreContextState) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kExecuteCommandList),
				.words = { a_commandListObservationId, a_commandList,
					a_sourceRecordingObservationId, a_restoreContextState ? 1u : 0u },
			};
		}

		EventPayload TargetViewObservationPayload(
			const TargetViewObservationInput& a_target,
			const TargetViewObservationResult& a_observation) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kTargetViewObservation),
				.words = {
					a_observation.observationId,
					a_target.d3dObject,
					a_observation.pointerGeneration,
					static_cast<std::uint64_t>(a_target.kind),
					a_target.resourceObservationId,
				},
			};
		}

		EventPayload TargetBindingPayload(
			const TargetBindingObservationResult& a_observation,
			std::uintptr_t a_context,
			TargetBindingSource a_source) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kTargetBinding),
				.words = { a_observation.observationId, a_context, static_cast<std::uint64_t>(a_source) },
			};
		}

		EventPayload ResourceObservationPayload(
			const ResourceObservationInput& a_resource,
			const ResourceObservationResult& a_observation) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kResourceObservation),
				.words = {
					a_observation.observationId,
					a_resource.d3dObject,
					a_observation.pointerGeneration,
					static_cast<std::uint64_t>(a_resource.dimension),
				},
			};
		}

		EventPayload ResourceViewBindingPayload(
			std::uint64_t a_viewObservationId,
			ResourceBindingKind a_bindingKind,
			ResourceStage a_stage,
			std::uint32_t a_slot,
			ResourceBindingSource a_source) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kResourceViewBinding),
				.words = {
					a_viewObservationId,
					static_cast<std::uint64_t>(a_bindingKind),
					static_cast<std::uint64_t>(a_stage),
					a_slot,
					static_cast<std::uint64_t>(a_source),
				},
			};
		}

		EventPayload ResourceViewStateObservedPayload(
			ResourceBindingKind a_bindingKind,
			ResourceStage a_stage,
			std::uint32_t a_startSlot,
			std::uint32_t a_count,
			ResourceBindingSource a_source,
			std::uint32_t a_changedSlotCount) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kResourceViewStateObserved),
				.words = {
					static_cast<std::uint64_t>(a_bindingKind),
					static_cast<std::uint64_t>(a_stage),
					a_startSlot,
					a_count,
					static_cast<std::uint64_t>(a_source),
					a_changedSlotCount,
				},
			};
		}

		EventPayload ResourceFlowPayload(
			ResourceFlowOperation a_operation,
			std::uint64_t a_sourceObservationId,
			std::uint64_t a_destinationObservationId,
			std::uint32_t a_sourceSubresource,
			std::uint32_t a_destinationSubresource) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kResourceFlow),
				.words = {
					static_cast<std::uint64_t>(a_operation),
					a_sourceObservationId,
					a_destinationObservationId,
					a_sourceSubresource,
					a_destinationSubresource,
				},
			};
		}

		EventPayload ResourceCpuAccessPayload(
			ResourceCpuAccessPhase a_phase,
			std::uint64_t a_mapObservationId,
			std::uint64_t a_resourceObservationId,
			std::uint32_t a_subresource,
			std::uint32_t a_mapType,
			std::uint32_t a_mapFlags,
			std::uint64_t a_durationQpcTicks,
			std::int32_t a_result,
			std::uint32_t a_rowPitch,
			std::uint32_t a_depthPitch) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kResourceCpuAccess),
				.words = {
					static_cast<std::uint64_t>(a_phase),
					a_mapObservationId,
					a_resourceObservationId,
					a_subresource,
					static_cast<std::uint64_t>(a_mapType) |
						(static_cast<std::uint64_t>(a_mapFlags) << 32u),
					a_durationQpcTicks,
					static_cast<std::uint32_t>(a_result),
					static_cast<std::uint64_t>(a_rowPitch) |
						(static_cast<std::uint64_t>(a_depthPitch) << 32u),
				},
			};
		}

		EventPayload ResourceVersionPayload(
			std::uint64_t a_versionObservationId,
			std::uint64_t a_resourceObservationId,
			const ResourceVersionInput& a_version) noexcept
		{
			const auto eye = static_cast<std::uint64_t>(a_version.eye) |
			                 (static_cast<std::uint64_t>(a_version.eyeMask) << 8u);
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kResourceVersion),
				.words = {
					a_versionObservationId,
					a_resourceObservationId,
					a_version.firstSubresource,
					a_version.subresourceCount,
					a_version.writeEpoch,
					a_version.producerFrame,
					static_cast<std::uint64_t>(a_version.readinessDomain),
					eye,
				},
			};
		}

		EventPayload VisibilityCandidatePayload(
			std::uintptr_t a_object,
			std::uint32_t a_objectIndex,
			std::uint64_t a_producerFrame) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kVisibilityCandidate),
				.words = { a_object, a_objectIndex, a_producerFrame },
			};
		}

		EventPayload VisibilityResultPayload(
			std::uint64_t a_versionObservationId,
			std::uint64_t a_viewObservationId,
			std::uint32_t a_objectCount,
			std::uint64_t a_producerFrame) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kVisibilityResult),
				.words = { a_versionObservationId, a_viewObservationId, a_objectCount, a_producerFrame },
			};
		}

		EventPayload VisibilitySubmissionPayload(
			std::uint64_t a_submissionObservationId,
			const VisibilitySubmissionInput& a_submission,
			std::uint64_t a_requestedViewObservationId,
			std::uint64_t a_effectiveViewObservationId) noexcept
		{
			const auto flags = static_cast<std::uint64_t>(a_submission.category) |
			                   (static_cast<std::uint64_t>(a_submission.slot) << 16u) |
			                   (a_submission.bindingMatches ? (1ull << 32u) : 0u) |
			                   (a_submission.forcedVisible ? (1ull << 33u) : 0u);
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kVisibilitySubmission),
				.words = {
					a_submissionObservationId,
					a_submission.renderPass,
					a_submission.geometry,
					a_submission.objectIndex,
					a_submission.resourceVersionObservationId,
					a_requestedViewObservationId,
					a_effectiveViewObservationId,
					flags,
				},
			};
		}

		EventPayload EyeSubmissionPayload(
			std::uint64_t a_resourceObservationId,
			Eye a_eye,
			std::uint8_t a_eyeMask,
			float a_uMin,
			float a_vMin,
			float a_uMax,
			float a_vMax,
			std::uint32_t a_submitFlags,
			std::uint64_t a_compositorCycle) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kEyeSubmission),
				.words = {
					a_resourceObservationId,
					static_cast<std::uint64_t>(a_eye),
					a_eyeMask,
					PackFloats(a_uMin, a_vMin),
					PackFloats(a_uMax, a_vMax),
					a_submitFlags,
					a_compositorCycle,
				},
			};
		}

		EventPayload CullDecisionPayload(
			std::uint64_t a_resourceVersionObservationId,
			std::uint32_t a_objectIndex,
			bool a_producerVisible,
			std::uint32_t a_totalDraws,
			std::uint32_t a_lightingDraws,
			std::uint32_t a_distantTreeDraws,
			std::uint32_t a_grassDraws,
			std::uint64_t a_producerFrame) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kCullDecision),
				.words = {
					a_resourceVersionObservationId,
					a_objectIndex,
					a_producerVisible ? 1u : 0u,
					a_totalDraws,
					a_lightingDraws,
					a_distantTreeDraws,
					a_grassDraws,
					a_producerFrame,
				},
			};
		}
	}

	std::size_t Runtime::PersistentStageShaderKeyHash::operator()(
		const PersistentStageShaderKey& a_key) const noexcept
	{
		const auto pointerHash = std::hash<std::uintptr_t>{}(a_key.d3dObject);
		const auto stageHash = std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(a_key.stage));
		return pointerHash ^ (stageHash + 0x9e3779b9u + (pointerHash << 6u) + (pointerHash >> 2u));
	}

	std::size_t Runtime::ActiveCpuMapKeyHash::operator()(const ActiveCpuMapKey& a_key) const noexcept
	{
		auto value = std::hash<std::uintptr_t>{}(a_key.context);
		const auto combine = [&](std::size_t a_part) {
			value ^= a_part + 0x9e3779b9u + (value << 6u) + (value >> 2u);
		};
		combine(std::hash<std::uintptr_t>{}(a_key.resource));
		combine(std::hash<std::uint32_t>{}(a_key.subresource));
		return value;
	}

	StartResult Runtime::StartCapture(const CollectorConfig& a_config)
	{
		const auto result = collector.Start(a_config);
		if (result == StartResult::kStarted) {
			{
				std::scoped_lock lock(activeCpuMapMutex);
				activeCpuMaps.clear();
			}
			immediateContextObservationId.store(0, std::memory_order_release);
			immediateContextObservationGeneration.store(0, std::memory_order_release);
			immediateContextCommandSequence.store(0, std::memory_order_release);
			boundTargetBindingObservationId.store(0, std::memory_order_release);
			targetStateObservationGeneration.store(0, std::memory_order_release);
			boundVertexShaderObservationId.store(0, std::memory_order_release);
			boundPixelShaderObservationId.store(0, std::memory_order_release);
			boundComputeShaderObservationId.store(0, std::memory_order_release);
			{
				std::scoped_lock lock(deferredContextMutex);
				for (auto& [_, state] : deferredContexts) {
					state.observationGeneration = 0;
					state.observationId = 0;
					state.commandSequence = 0;
					state.recordingObservationId = 0;
					state.creationCaptureGeneration = 0;
					state.boundVertexShaderObservationId = 0;
					state.boundPixelShaderObservationId = 0;
					state.boundComputeShaderObservationId = 0;
				}
			}
			{
				std::scoped_lock lock(commandListMutex);
				// Command-list identities are capture-local. Lists created before this
				// capture are deliberately re-admitted as first-seen execution evidence.
				commandLists.clear();
			}
		}
		return result;
	}

	std::optional<CaptureSnapshot> Runtime::StopCapture(
		StopReason a_reason,
		std::chrono::milliseconds a_drainTimeout)
	{
		return collector.Stop(a_reason, a_drainTimeout);
	}

	bool Runtime::IsCapturing() const noexcept
	{
		return collector.IsCapturing();
	}

	bool Runtime::IsCaptureDraining() const noexcept
	{
		return collector.IsDraining();
	}

	std::uint64_t Runtime::ActiveCaptureGeneration() const noexcept
	{
		return collector.ActiveGeneration();
	}

	void Runtime::SetCpuFrame(std::uint64_t a_cpuFrame) noexcept
	{
		if (!collector.IsCapturing())
			return;
		auto frame = collector.GetThreadFrameContext();
		frame.cpuFrame = a_cpuFrame;
		collector.SetThreadFrameContext(frame);
	}

	void Runtime::SetFrameContext(const FrameContext& a_context) noexcept
	{
		collector.SetThreadFrameContext(a_context);
	}

	Collector::ScopeGuard Runtime::EnterRenderPass(const RenderPassBoundary& a_boundary) noexcept
	{
		if (!collector.IsCapturing())
			return {};
		const auto observationId = collector.AllocateObservationId();
		if (observationId == 0)
			return {};
		const auto payload = RenderPassPayload(a_boundary);
		return collector.EnterScope(
			ScopeKind::kRenderPass,
			observationId,
			EventKind::kRenderPassEnter,
			EventKind::kRenderPassExit,
			payload,
			payload);
	}

	Collector::ScopeGuard Runtime::EnterTechnique(const TechniqueBoundary& a_boundary) noexcept
	{
		if (!collector.IsCapturing())
			return {};
		const auto shaderObservation = collector.ObserveShader({
			.shader = a_boundary.shader,
			.shaderType = a_boundary.shaderType,
			.fxpFilename = a_boundary.fxpFilename,
			.imageSpaceName = a_boundary.imageSpaceName,
			.compileSourceName = a_boundary.compileSourceName,
			.definesSuffix = a_boundary.definesSuffix,
		});
		const auto captureGeneration = shaderObservation.sessionGeneration != 0 ?
		                                   shaderObservation.sessionGeneration :
		                                   collector.ActiveGeneration();
		if (shaderObservation.firstSeen) {
			collector.RecordForGeneration(
				EventKind::kShaderObserved, ShaderObservationPayload(a_boundary, shaderObservation), 0, captureGeneration);
		}
		const auto techniqueObservationId = collector.AllocateObservationId(captureGeneration);
		if (techniqueObservationId == 0)
			return {};
		const auto payload = TechniquePayload(a_boundary, shaderObservation.observationId);
		return collector.EnterScope(
			ScopeKind::kTechnique,
			techniqueObservationId,
			EventKind::kTechniqueBegin,
			EventKind::kTechniqueEnd,
			payload,
			payload,
			captureGeneration);
	}

	void Runtime::RetireShaderObservation(std::uintptr_t a_shader) noexcept
	{
		collector.RetireShaderObservation(a_shader);
	}

	void Runtime::RegisterCreatedStageShader(
		ShaderStage a_stage,
		std::uintptr_t a_d3dObject,
		std::uint64_t a_bytecodeSize,
		std::string_view a_bytecodeSha256) noexcept
	{
		if (a_d3dObject == 0)
			return;

		PersistentStageShaderIdentity identity{ .bytecodeSize = a_bytecodeSize };
		const auto copyLength = std::min(a_bytecodeSha256.size(), identity.bytecodeSha256.size() - 1);
		if (copyLength != 0)
			std::memcpy(identity.bytecodeSha256.data(), a_bytecodeSha256.data(), copyLength);
		identity.bytecodeSha256[copyLength] = '\0';

		try {
			std::unique_lock lock(persistentStageShaderMutex);
			persistentStageShaders.insert_or_assign(
				PersistentStageShaderKey{ a_stage, a_d3dObject }, identity);
		} catch (...) {
			// Provenance is diagnostic. Shader creation must never fail because the
			// process-lifetime identity catalogue could not grow.
		}
	}

	void Runtime::RegisterEngineStageShader(
		ShaderStage a_stage,
		std::uintptr_t a_d3dObject,
		std::string_view a_loaderType,
		std::uint32_t a_descriptor,
		std::string_view a_compileSourceName) noexcept
	{
		if (a_d3dObject == 0 || a_loaderType.empty())
			return;

		try {
			PersistentStageShaderIdentity::EngineAlias alias{
				.loaderType = std::string(a_loaderType),
				.compileSourceName = std::string(a_compileSourceName),
				.descriptor = a_descriptor,
			};
			std::unique_lock lock(persistentStageShaderMutex);
			auto& identity = persistentStageShaders[PersistentStageShaderKey{ a_stage, a_d3dObject }];
			if (std::find(identity.engineAliases.begin(), identity.engineAliases.end(), alias) ==
				identity.engineAliases.end()) {
				identity.engineAliases.push_back(std::move(alias));
			}
		} catch (...) {
			// Engine aliases are diagnostic provenance and must never affect loading.
		}
	}

	StageShaderObservationResult Runtime::ObserveStageShaderWithPersistent(
		const StageShaderObservationInput& a_input) noexcept
	{
		auto enriched = a_input;
		const auto persistent = FindCreatedStageShader(a_input.stage, a_input.d3dObject);
		std::array<StageShaderObservationInput::EngineAlias, kMaximumEngineShaderAliasesPerStage> aliases{};
		if (persistent) {
			if (enriched.bytecodeSize == 0)
				enriched.bytecodeSize = persistent->bytecodeSize;
			if (enriched.bytecodeSha256.empty())
				enriched.bytecodeSha256 = persistent->bytecodeSha256.data();
			if (enriched.engineAliasCount == 0 && !persistent->engineAliases.empty()) {
				const auto count = std::min(persistent->engineAliases.size(), aliases.size());
				for (std::size_t index = 0; index < count; ++index) {
					aliases[index] = {
						.loaderType = persistent->engineAliases[index].loaderType,
						.compileSourceName = persistent->engineAliases[index].compileSourceName,
						.descriptor = persistent->engineAliases[index].descriptor,
					};
				}
				enriched.engineAliases = aliases.data();
				enriched.engineAliasCount = static_cast<std::uint32_t>(count);
				enriched.engineAliasTotalCount = static_cast<std::uint32_t>(persistent->engineAliases.size());
			}
		}

		auto observation = collector.ObserveStageShader(enriched);
		if (observation.firstSeen) {
			collector.RecordForGeneration(
				EventKind::kStageShaderObserved,
				StageShaderObservationPayload(enriched, observation), 0, observation.sessionGeneration);
		}
		return observation;
	}

	void Runtime::RecordTechniqueResolution(const TechniqueResolution& a_resolution) noexcept
	{
		if (!collector.IsCapturing())
			return;

		const auto vertex = ObserveStageShaderWithPersistent(a_resolution.vertex.shader);
		const auto pixel = ObserveStageShaderWithPersistent(a_resolution.pixel.shader);
		const auto captureGeneration = vertex.sessionGeneration != 0 ? vertex.sessionGeneration :
		                                                               (pixel.sessionGeneration != 0 ? pixel.sessionGeneration : collector.ActiveGeneration());
		if (captureGeneration == 0)
			return;
		PublishBoundStageObservation(a_resolution.vertex.shader.stage, a_resolution.vertex.shader.d3dObject, vertex);
		PublishBoundStageObservation(a_resolution.pixel.shader.stage, a_resolution.pixel.shader.d3dObject, pixel);
		collector.RecordForGeneration(
			EventKind::kTechniqueResolved,
			TechniqueResolutionPayload(a_resolution, vertex.observationId, pixel.observationId), 0, captureGeneration);
	}

	void Runtime::SetImmediateContext(std::uintptr_t a_context) noexcept
	{
		if (immediateContext.exchange(a_context, std::memory_order_acq_rel) == a_context)
			return;
		if (pendingGeometrySubmission.owner == this)
			pendingGeometrySubmission = {};
		immediateContextPointerGeneration.fetch_add(1, std::memory_order_acq_rel);
		immediateContextObservationId.store(0, std::memory_order_release);
		immediateContextObservationGeneration.store(0, std::memory_order_release);
		immediateContextCommandSequence.store(0, std::memory_order_release);
		boundVertexShader.store(0, std::memory_order_release);
		boundPixelShader.store(0, std::memory_order_release);
		boundComputeShader.store(0, std::memory_order_release);
		boundVertexShaderObservationId.store(0, std::memory_order_release);
		boundPixelShaderObservationId.store(0, std::memory_order_release);
		boundComputeShaderObservationId.store(0, std::memory_order_release);
		boundTargetBindingObservationId.store(0, std::memory_order_release);
		targetStateObservationGeneration.store(0, std::memory_order_release);
	}

	void Runtime::RegisterDeferredContext(
		std::uintptr_t a_context,
		std::uint32_t a_contextFlags,
		bool a_creationObserved) noexcept
	{
		if (a_context == 0)
			return;
		const auto captureGeneration = collector.ActiveGeneration();
		{
			std::scoped_lock lock(deferredContextMutex);
			if (!deferredContexts.contains(a_context) &&
				deferredContexts.size() >= kMaximumTrackedDeferredContexts) {
				return;
			}
			auto [it, inserted] = deferredContexts.try_emplace(a_context);
			auto& state = it->second;
			if (!inserted && a_creationObserved) {
				const auto nextPointerGeneration = state.pointerGeneration + 1;
				state = DeferredContextState{
					.pointerGeneration = nextPointerGeneration,
					.contextFlags = a_contextFlags,
				};
			} else {
				state.contextFlags = a_contextFlags;
			}
			if (a_creationObserved)
				state.creationCaptureGeneration = captureGeneration;
		}
		if (captureGeneration != 0)
			EnsureContextObservation(a_context);
	}

	void Runtime::ResetImmediatePipelineState() noexcept
	{
		boundVertexShader.store(0, std::memory_order_release);
		boundPixelShader.store(0, std::memory_order_release);
		boundComputeShader.store(0, std::memory_order_release);
		boundVertexShaderObservationId.store(0, std::memory_order_release);
		boundPixelShaderObservationId.store(0, std::memory_order_release);
		boundComputeShaderObservationId.store(0, std::memory_order_release);
		boundTargetBindingObservationId.store(0, std::memory_order_release);
		targetStateObservationGeneration.store(0, std::memory_order_release);
		resourceViewStateObservationGeneration.store(0, std::memory_order_release);
	}

	std::uint64_t Runtime::StartDeferredRecording(
		DeferredContextState& a_state,
		std::uint64_t a_captureGeneration, bool a_partialAtCaptureStart) noexcept
	{
		a_state.recordingObservationId = 0;
		a_state.recordingIncompleteReasons =
			static_cast<std::uint64_t>(CommandRecordingIncompleteReason::kHookCoverageUnqualified);
		if (a_partialAtCaptureStart) {
			a_state.recordingIncompleteReasons |=
				static_cast<std::uint64_t>(CommandRecordingIncompleteReason::kPartialAtCaptureStart);
		}
		if (a_state.observationId == 0 || a_captureGeneration == 0) {
			a_state.recordingIncompleteReasons |=
				static_cast<std::uint64_t>(CommandRecordingIncompleteReason::kDeclarationUnavailable);
			return 0;
		}
		const auto recordingObservationId = collector.AllocateObservationId(a_captureGeneration);
		if (recordingObservationId == 0) {
			a_state.recordingIncompleteReasons |=
				static_cast<std::uint64_t>(CommandRecordingIncompleteReason::kDeclarationUnavailable);
			return 0;
		}
		++a_state.recordingEpoch;
		if (collector.RecordForGeneration(
				EventKind::kCommandRecordingObserved,
				CommandRecordingObservationPayload(
					recordingObservationId, a_state.observationId,
					a_state.recordingEpoch, a_partialAtCaptureStart),
				a_state.observationId, a_captureGeneration, a_state.commandSequence,
				0, 0, 0, recordingObservationId, true) != RecordResult::kRecorded) {
			a_state.recordingIncompleteReasons |=
				static_cast<std::uint64_t>(CommandRecordingIncompleteReason::kDeclarationUnavailable);
			return 0;
		}
		a_state.recordingObservationId = recordingObservationId;
		return recordingObservationId;
	}

	void Runtime::MarkDeferredRecordingIncomplete(
		std::uintptr_t a_context, std::uint64_t a_contextObservationId,
		std::uint64_t a_recordingObservationId,
		CommandRecordingIncompleteReason a_reason) noexcept
	{
		std::scoped_lock lock(deferredContextMutex);
		const auto found = deferredContexts.find(a_context);
		if (found == deferredContexts.end() ||
			found->second.observationId != a_contextObservationId ||
			found->second.recordingObservationId != a_recordingObservationId) {
			return;
		}
		found->second.recordingIncompleteReasons |= static_cast<std::uint64_t>(a_reason);
	}

	Runtime::ContextObservation Runtime::EnsureContextObservation(
		std::uintptr_t a_context) noexcept
	{
		if (a_context == 0)
			return {};
		if (a_context == immediateContext.load(std::memory_order_acquire)) {
			return {
				.kind = DeviceContextKind::kImmediate,
				.observationId = EnsureImmediateContextObservation(),
				.commandSequence = immediateContextCommandSequence.load(std::memory_order_acquire),
			};
		}

		const auto captureGeneration = collector.ActiveGeneration();
		if (captureGeneration == 0)
			return {};
		std::scoped_lock lock(deferredContextMutex);
		const auto found = deferredContexts.find(a_context);
		if (found == deferredContexts.end())
			return {};
		auto& state = found->second;
		if (state.observationGeneration != captureGeneration || state.observationId == 0) {
			const auto observationId = collector.AllocateObservationId(captureGeneration);
			if (observationId == 0)
				return {};
			const auto creationObserved = state.creationCaptureGeneration == captureGeneration;
			if (collector.RecordForGeneration(
					EventKind::kDeviceContextObserved,
					DeviceContextObservationPayload(
						observationId, a_context, state.pointerGeneration,
						DeviceContextKind::kDeferred,
						creationObserved ? ContextCreationEvidence::kCreateDeferredContext :
										   ContextCreationEvidence::kFirstSeen,
						state.contextFlags),
					observationId, captureGeneration) != RecordResult::kRecorded) {
				return {};
			}
			state.observationGeneration = captureGeneration;
			state.observationId = observationId;
			state.commandSequence = 0;
			state.recordingObservationId = 0;
			state.recordingIncompleteReasons = 0;
			StartDeferredRecording(state, captureGeneration, !creationObserved);
		}
		return {
			.kind = DeviceContextKind::kDeferred,
			.observationId = state.observationId,
			.commandSequence = state.commandSequence,
			.recordingObservationId = state.recordingObservationId,
		};
	}

	void Runtime::BindStage(
		std::uintptr_t a_context,
		ShaderStage a_stage,
		std::uintptr_t a_d3dObject) noexcept
	{
		if (a_context == 0)
			return;
		const auto isImmediate = a_context == immediateContext.load(std::memory_order_acquire);
		if (!isImmediate) {
			if (!collector.IsCapturing())
				return;
			const auto context = EnsureContextObservation(a_context);
			if (context.kind != DeviceContextKind::kDeferred || context.observationId == 0)
				return;
			const auto observed = ObserveBoundStage(a_stage, a_d3dObject);
			std::scoped_lock lock(deferredContextMutex);
			const auto found = deferredContexts.find(a_context);
			if (found == deferredContexts.end() || found->second.observationId != context.observationId)
				return;
			auto& state = found->second;
			++state.commandSequence;
			switch (a_stage) {
			case ShaderStage::kVertex:
				state.boundVertexShader = a_d3dObject;
				state.boundVertexShaderObservationId = observed.observationId;
				break;
			case ShaderStage::kPixel:
				state.boundPixelShader = a_d3dObject;
				state.boundPixelShaderObservationId = observed.observationId;
				break;
			case ShaderStage::kCompute:
				state.boundComputeShader = a_d3dObject;
				state.boundComputeShaderObservationId = observed.observationId;
				break;
			}
			return;
		}
		if (collector.IsCapturing()) {
			if (EnsureImmediateContextObservation() == 0)
				return;
			NextCommandStreamSequence();
		}
		switch (a_stage) {
		case ShaderStage::kVertex:
			boundVertexShader.store(a_d3dObject, std::memory_order_release);
			boundVertexShaderObservationId.store(
				ObserveBoundStage(a_stage, a_d3dObject).observationId, std::memory_order_release);
			break;
		case ShaderStage::kPixel:
			boundPixelShader.store(a_d3dObject, std::memory_order_release);
			boundPixelShaderObservationId.store(
				ObserveBoundStage(a_stage, a_d3dObject).observationId, std::memory_order_release);
			break;
		case ShaderStage::kCompute:
			boundComputeShader.store(a_d3dObject, std::memory_order_release);
			boundComputeShaderObservationId.store(
				ObserveBoundStage(a_stage, a_d3dObject).observationId, std::memory_order_release);
			break;
		}
	}

	void Runtime::RecordFinishCommandList(
		std::uintptr_t a_context,
		std::uintptr_t a_commandList,
		bool a_restoreDeferredContextState,
		std::int32_t a_result) noexcept
	{
		if (!collector.IsCapturing() || a_context == 0)
			return;
		const auto context = EnsureContextObservation(a_context);
		if (context.kind != DeviceContextKind::kDeferred || context.observationId == 0)
			return;
		const auto captureGeneration = collector.ActiveGeneration();
		std::uint64_t recordingObservationId = 0;
		std::uint64_t recordingIncompleteReasons = 0;
		std::uint64_t commandSequence = 0;
		{
			std::scoped_lock lock(deferredContextMutex);
			const auto found = deferredContexts.find(a_context);
			if (found == deferredContexts.end() || found->second.observationId != context.observationId)
				return;
			auto& state = found->second;
			recordingObservationId = state.recordingObservationId;
			recordingIncompleteReasons = state.recordingIncompleteReasons;
			commandSequence = ++state.commandSequence;
		}
		auto sourceRecordingComplete =
			recordingObservationId != 0 && recordingIncompleteReasons == 0;
		if (recordingObservationId == 0) {
			recordingIncompleteReasons |= static_cast<std::uint64_t>(
				CommandRecordingIncompleteReason::kDeclarationUnavailable);
			sourceRecordingComplete = false;
		}

		std::uint64_t commandListObservationId = 0;
		if (a_result >= 0 && a_commandList != 0) {
			std::scoped_lock lock(commandListMutex);
			if (commandLists.contains(a_commandList) ||
				commandLists.size() < kMaximumTrackedCommandLists) {
				auto [it, inserted] = commandLists.try_emplace(a_commandList);
				auto& state = it->second;
				if (!inserted) {
					const auto nextPointerGeneration = state.pointerGeneration + 1;
					state = CommandListState{ .pointerGeneration = nextPointerGeneration };
				}
				commandListObservationId = collector.AllocateObservationId(captureGeneration);
				if (commandListObservationId != 0) {
					const auto recordResult = collector.RecordForGeneration(
						EventKind::kCommandListObserved,
						CommandListObservationPayload(
							commandListObservationId, a_commandList, state.pointerGeneration,
							context.observationId, recordingObservationId,
							sourceRecordingComplete, recordingIncompleteReasons),
						context.observationId, captureGeneration, commandSequence,
						0, 0, 0, recordingObservationId, true);
					if (recordResult == RecordResult::kRecorded) {
						state.observationGeneration = captureGeneration;
						state.observationId = commandListObservationId;
						state.sourceContextObservationId = context.observationId;
						state.sourceRecordingObservationId = recordingObservationId;
						state.sourceRecordingComplete = sourceRecordingComplete;
						state.sourceRecordingIncompleteReasons = recordingIncompleteReasons;
					} else {
						commandListObservationId = 0;
						recordingIncompleteReasons |= static_cast<std::uint64_t>(
							CommandRecordingIncompleteReason::kEventNotRecorded);
						sourceRecordingComplete = false;
					}
				} else {
					recordingIncompleteReasons |= static_cast<std::uint64_t>(
						CommandRecordingIncompleteReason::kEventNotRecorded);
					sourceRecordingComplete = false;
				}
			} else {
				recordingIncompleteReasons |= static_cast<std::uint64_t>(
					CommandRecordingIncompleteReason::kEventNotRecorded);
				sourceRecordingComplete = false;
			}
		} else if (a_result >= 0) {
			recordingIncompleteReasons |= static_cast<std::uint64_t>(
				CommandRecordingIncompleteReason::kDeclarationUnavailable);
			sourceRecordingComplete = false;
		}

		collector.RecordForGeneration(
			EventKind::kFinishCommandList,
			FinishCommandListPayload(
				recordingObservationId, commandListObservationId,
				a_result >= 0 ? a_commandList : 0,
				a_restoreDeferredContextState, a_result,
				sourceRecordingComplete, recordingIncompleteReasons),
			context.observationId, captureGeneration, commandSequence,
			0, 0, 0, recordingObservationId, true);

		{
			std::scoped_lock lock(deferredContextMutex);
			const auto found = deferredContexts.find(a_context);
			if (found == deferredContexts.end() || found->second.observationId != context.observationId)
				return;
			auto& state = found->second;
			state.recordingObservationId = 0;
			state.recordingIncompleteReasons = 0;
			if (!a_restoreDeferredContextState) {
				state.boundVertexShader = 0;
				state.boundPixelShader = 0;
				state.boundComputeShader = 0;
				state.boundVertexShaderObservationId = 0;
				state.boundPixelShaderObservationId = 0;
				state.boundComputeShaderObservationId = 0;
			}
			StartDeferredRecording(state, captureGeneration, false);
		}
	}

	void Runtime::RecordExecuteCommandList(
		std::uintptr_t a_context,
		std::uintptr_t a_commandList,
		bool a_restoreContextState) noexcept
	{
		const auto isImmediateContext =
			a_context != 0 && a_context == immediateContext.load(std::memory_order_acquire);
		if (isImmediateContext && !a_restoreContextState)
			ResetImmediatePipelineState();
		if (!collector.IsCapturing() || !isImmediateContext || a_commandList == 0) {
			return;
		}
		const auto captureGeneration = collector.ActiveGeneration();
		const auto contextObservationId = EnsureImmediateContextObservation();
		if (captureGeneration == 0 || contextObservationId == 0)
			return;
		const auto commandSequence = NextCommandStreamSequence();
		std::uint64_t commandListObservationId = 0;
		std::uint64_t sourceRecordingObservationId = 0;
		{
			std::scoped_lock lock(commandListMutex);
			if (!commandLists.contains(a_commandList) &&
				commandLists.size() >= kMaximumTrackedCommandLists) {
				return;
			}
			auto [it, inserted] = commandLists.try_emplace(a_commandList);
			auto& state = it->second;
			if (state.observationGeneration != captureGeneration || state.observationId == 0) {
				commandListObservationId = collector.AllocateObservationId(captureGeneration);
				if (commandListObservationId == 0)
					return;
				const auto incompleteReasons = static_cast<std::uint64_t>(
					CommandRecordingIncompleteReason::kDeclarationUnavailable);
				if (collector.RecordForGeneration(
						EventKind::kCommandListObserved,
						CommandListObservationPayload(
							commandListObservationId, a_commandList, state.pointerGeneration,
							0, 0, false, incompleteReasons),
						contextObservationId, captureGeneration, commandSequence) !=
					RecordResult::kRecorded) {
					return;
				}
				state.observationGeneration = captureGeneration;
				state.observationId = commandListObservationId;
				state.sourceContextObservationId = 0;
				state.sourceRecordingObservationId = 0;
				state.sourceRecordingComplete = false;
				state.sourceRecordingIncompleteReasons = incompleteReasons;
			} else {
				commandListObservationId = state.observationId;
			}
			sourceRecordingObservationId = state.sourceRecordingObservationId;
		}
		collector.RecordForGeneration(
			EventKind::kExecuteCommandList,
			ExecuteCommandListPayload(
				commandListObservationId, a_commandList,
				sourceRecordingObservationId, a_restoreContextState),
			contextObservationId, captureGeneration, commandSequence,
			0, 0, 0, 0, false);
	}

	void Runtime::BindRenderTargets(
		std::uintptr_t a_context,
		std::uint32_t a_renderTargetCount,
		const std::uintptr_t* a_renderTargets,
		std::uintptr_t a_depthTarget,
		bool a_keepTargets) noexcept
	{
		std::array<ResourceViewInput, kMaximumRenderTargets> targets{};
		const auto count = std::min<std::uint32_t>(a_renderTargetCount, kMaximumRenderTargets);
		for (std::uint32_t index = 0; index < count; ++index) {
			targets[index].view.kind = TargetViewKind::kRenderTarget;
			targets[index].view.d3dObject = a_renderTargets ? a_renderTargets[index] : 0;
		}
		ResourceViewInput depth{};
		depth.view.kind = TargetViewKind::kDepthTarget;
		depth.view.d3dObject = a_depthTarget;
		BindRenderTargetViews(
			a_context, a_renderTargetCount, targets.data(), a_depthTarget != 0 ? &depth : nullptr, a_keepTargets);
	}

	void Runtime::BindRenderTargetViews(
		std::uintptr_t a_context,
		std::uint32_t a_renderTargetCount,
		const ResourceViewInput* a_renderTargets,
		const ResourceViewInput* a_depthTarget,
		bool a_keepTargets,
		TargetBindingSource a_source,
		std::uint64_t a_expectedCaptureGeneration) noexcept
	{
		if (!collector.IsCapturing() || a_context == 0 ||
			a_context != immediateContext.load(std::memory_order_acquire)) {
			return;
		}
		const auto captureGeneration = collector.ActiveGeneration();
		if (captureGeneration == 0 ||
			(a_expectedCaptureGeneration != 0 && a_expectedCaptureGeneration != captureGeneration)) {
			return;
		}
		const auto contextObservationId = EnsureImmediateContextObservation();
		if (contextObservationId == 0)
			return;
		const auto commandStreamSequence = NextCommandStreamSequence();
		if (a_keepTargets)
			return;
		targetStateObservationGeneration.store(captureGeneration, std::memory_order_release);

		TargetBindingObservationInput binding;
		bool identityComplete = true;
		binding.renderTargetCount = static_cast<std::uint8_t>(
			std::min<std::uint32_t>(a_renderTargetCount, kMaximumRenderTargets));
		for (std::size_t index = 0; index < binding.renderTargetCount; ++index) {
			if (!a_renderTargets || a_renderTargets[index].view.d3dObject == 0)
				continue;
			auto input = a_renderTargets[index];
			input.view.kind = TargetViewKind::kRenderTarget;
			const auto observation = ObserveResourceView(input, contextObservationId, commandStreamSequence);
			if (observation.observationId == 0)
				identityComplete = false;
			binding.renderTargetObservationIds[index] = observation.observationId;
		}

		if (a_depthTarget && a_depthTarget->view.d3dObject != 0) {
			auto input = *a_depthTarget;
			input.view.kind = TargetViewKind::kDepthTarget;
			const auto observation = ObserveResourceView(input, contextObservationId, commandStreamSequence);
			if (observation.observationId == 0)
				identityComplete = false;
			binding.depthTargetObservationId = observation.observationId;
		}
		if (!identityComplete) {
			boundTargetBindingObservationId.store(0, std::memory_order_release);
			return;
		}

		const auto bindingObservation = collector.ObserveTargetBinding(binding);
		boundTargetBindingObservationId.store(bindingObservation.observationId, std::memory_order_release);
		if (bindingObservation.observationId == 0)
			return;
		collector.RecordForGeneration(
			EventKind::kRenderTargetBind,
			TargetBindingPayload(bindingObservation, a_context, a_source),
			contextObservationId,
			bindingObservation.sessionGeneration,
			commandStreamSequence,
			bindingObservation.observationId);
	}

	std::uint64_t Runtime::ClaimRenderTargetStateSeed(std::uintptr_t a_context) noexcept
	{
		if (!collector.IsCapturing() || a_context == 0 ||
			a_context != immediateContext.load(std::memory_order_acquire)) {
			return 0;
		}
		const auto generation = collector.ActiveGeneration();
		if (generation == 0)
			return 0;
		return targetStateObservationGeneration.exchange(generation, std::memory_order_acq_rel) == generation ?
		           0 :
		           generation;
	}

	ResourceObservationResult Runtime::ObserveResource(
		const ResourceObservationInput& a_input,
		std::uint64_t a_contextObservationId,
		std::uint64_t a_commandStreamSequence) noexcept
	{
		if (a_input.d3dObject == 0)
			return {};
		const auto observation = collector.ObserveResource(a_input);
		if (observation.firstSeen) {
			collector.RecordForGeneration(
				EventKind::kResourceObserved,
				ResourceObservationPayload(a_input, observation),
				a_contextObservationId,
				observation.sessionGeneration,
				a_commandStreamSequence);
		}
		return observation;
	}

	TargetViewObservationResult Runtime::ObserveResourceView(
		const ResourceViewInput& a_input,
		std::uint64_t a_contextObservationId,
		std::uint64_t a_commandStreamSequence) noexcept
	{
		if (a_input.view.d3dObject == 0)
			return {};
		auto view = a_input.view;
		const auto resource = ObserveResource(a_input.resource, a_contextObservationId, a_commandStreamSequence);
		view.resourceObservationId = resource.observationId;
		const auto observation = collector.ObserveTargetView(view);
		if (observation.firstSeen) {
			collector.RecordForGeneration(
				EventKind::kTargetViewObserved,
				TargetViewObservationPayload(view, observation),
				a_contextObservationId,
				observation.sessionGeneration,
				a_commandStreamSequence);
		}
		return observation;
	}

	void Runtime::BindResourceViews(
		std::uintptr_t a_context,
		ResourceBindingKind a_bindingKind,
		ResourceStage a_stage,
		std::uint32_t a_startSlot,
		std::uint32_t a_viewCount,
		const ResourceViewInput* a_views,
		bool a_keepViews,
		ResourceBindingSource a_source,
		std::uint64_t a_expectedCaptureGeneration) noexcept
	{
		if (!collector.IsCapturing() || a_context == 0 ||
			a_context != immediateContext.load(std::memory_order_acquire)) {
			return;
		}
		const auto captureGeneration = collector.ActiveGeneration();
		if (captureGeneration == 0 ||
			(a_expectedCaptureGeneration != 0 && a_expectedCaptureGeneration != captureGeneration)) {
			return;
		}
		const auto contextObservationId = EnsureImmediateContextObservation();
		if (contextObservationId == 0)
			return;
		const auto commandStreamSequence = NextCommandStreamSequence();
		if (a_keepViews)
			return;

		const auto maximumSlots = a_bindingKind == ResourceBindingKind::kShaderResource ?
		                              static_cast<std::uint32_t>(kMaximumShaderResourceSlots) :
		                              static_cast<std::uint32_t>(kMaximumUnorderedAccessSlots);
		const auto count = a_startSlot >= maximumSlots ? 0u :
		                                                 std::min(a_viewCount, maximumSlots - a_startSlot);
		std::array<bool, kMaximumShaderResourceSlots> recordSlot{};
		std::uint32_t changedSlotCount = 0;
		if (a_source == ResourceBindingSource::kRequestedCall) {
			std::fill_n(recordSlot.begin(), count, true);
			changedSlotCount = count;
		} else {
			const auto kindIndex = a_bindingKind == ResourceBindingKind::kShaderResource ? 0u : 1u;
			const auto stageValue = static_cast<std::uint32_t>(a_stage);
			if (stageValue == 0 || stageValue > effectiveResourceViews[kindIndex].size())
				return;
			std::scoped_lock lock(resourceViewStateMutex);
			if (resourceViewStateGeneration != captureGeneration) {
				for (auto& kind : effectiveResourceViews)
					for (auto& stage : kind)
						stage.fill(0);
				resourceViewStateGeneration = captureGeneration;
			}
			auto& state = effectiveResourceViews[kindIndex][stageValue - 1];
			for (std::uint32_t index = 0; index < count; ++index) {
				const auto pointer = a_views ? a_views[index].view.d3dObject : 0;
				if (state[a_startSlot + index] != pointer) {
					state[a_startSlot + index] = pointer;
					recordSlot[index] = true;
					++changedSlotCount;
				}
			}
		}
		for (std::uint32_t index = 0; index < count; ++index) {
			if (!recordSlot[index])
				continue;
			std::uint64_t viewObservationId = 0;
			std::uint64_t generation = captureGeneration;
			if (a_views && a_views[index].view.d3dObject != 0) {
				auto input = a_views[index];
				input.view.kind = a_bindingKind == ResourceBindingKind::kShaderResource ?
				                      TargetViewKind::kShaderResource :
				                      TargetViewKind::kUnorderedAccess;
				const auto observation = ObserveResourceView(input, contextObservationId, commandStreamSequence);
				viewObservationId = observation.observationId;
				if (observation.sessionGeneration != 0)
					generation = observation.sessionGeneration;
			}
			collector.RecordForGeneration(
				EventKind::kResourceViewBind,
				ResourceViewBindingPayload(
					viewObservationId, a_bindingKind, a_stage, a_startSlot + index, a_source),
				contextObservationId,
				generation,
				commandStreamSequence);
		}
		if (a_source != ResourceBindingSource::kRequestedCall) {
			collector.RecordForGeneration(
				EventKind::kResourceViewStateObserved,
				ResourceViewStateObservedPayload(
					a_bindingKind, a_stage, a_startSlot, count, a_source, changedSlotCount),
				contextObservationId,
				captureGeneration,
				commandStreamSequence);
		}
	}

	std::uint64_t Runtime::ClaimResourceViewStateSeed(std::uintptr_t a_context) noexcept
	{
		if (!collector.IsCapturing() || a_context == 0 ||
			a_context != immediateContext.load(std::memory_order_acquire)) {
			return 0;
		}
		const auto generation = collector.ActiveGeneration();
		if (generation == 0)
			return 0;
		return resourceViewStateObservationGeneration.exchange(generation, std::memory_order_acq_rel) == generation ?
		           0 :
		           generation;
	}

	void Runtime::RecordResourceFlow(
		std::uintptr_t a_context,
		ResourceFlowOperation a_operation,
		const ResourceObservationInput& a_source,
		const ResourceObservationInput& a_destination,
		std::uint32_t a_sourceSubresource,
		std::uint32_t a_destinationSubresource) noexcept
	{
		if (!collector.IsCapturing() || a_context == 0 ||
			a_context != immediateContext.load(std::memory_order_acquire)) {
			return;
		}
		const auto contextObservationId = EnsureImmediateContextObservation();
		if (contextObservationId == 0)
			return;
		const auto commandStreamSequence = NextCommandStreamSequence();
		const auto source = ObserveResource(a_source, contextObservationId, commandStreamSequence);
		const auto destination = ObserveResource(a_destination, contextObservationId, commandStreamSequence);
		const auto generation = source.sessionGeneration != 0 ? source.sessionGeneration :
		                                                        (destination.sessionGeneration != 0 ? destination.sessionGeneration : collector.ActiveGeneration());
		collector.RecordForGeneration(
			EventKind::kResourceFlow,
			ResourceFlowPayload(
				a_operation, source.observationId, destination.observationId,
				a_sourceSubresource, a_destinationSubresource),
			contextObservationId,
			generation,
			commandStreamSequence);
	}

	void Runtime::RecordCpuMap(
		std::uintptr_t a_context,
		const ResourceObservationInput& a_resource,
		std::uint32_t a_subresource,
		std::uint32_t a_mapType,
		std::uint32_t a_mapFlags,
		std::int32_t a_result,
		std::uint64_t a_callDurationQpcTicks,
		std::uint64_t a_completedQpcTick,
		std::uint32_t a_rowPitch,
		std::uint32_t a_depthPitch,
		std::uint64_t a_expectedCaptureGeneration) noexcept
	{
		if (a_expectedCaptureGeneration == 0 || collector.ActiveGeneration() != a_expectedCaptureGeneration ||
			a_context == 0 || a_context != immediateContext.load(std::memory_order_acquire)) {
			return;
		}
		const auto contextObservationId = EnsureImmediateContextObservation();
		if (contextObservationId == 0)
			return;
		const auto commandStreamSequence = NextCommandStreamSequence();
		const auto resource = ObserveResource(a_resource, contextObservationId, commandStreamSequence);
		if (resource.observationId == 0 || resource.sessionGeneration != a_expectedCaptureGeneration)
			return;
		const auto mapObservationId = collector.AllocateObservationId(a_expectedCaptureGeneration);
		if (mapObservationId == 0)
			return;
		const auto recorded = collector.RecordForGeneration(
			EventKind::kResourceCpuAccess,
			ResourceCpuAccessPayload(
				ResourceCpuAccessPhase::kMap, mapObservationId, resource.observationId,
				a_subresource, a_mapType, a_mapFlags, a_callDurationQpcTicks,
				a_result, a_rowPitch, a_depthPitch),
			contextObservationId,
			a_expectedCaptureGeneration,
			commandStreamSequence);
		if (a_result < 0 || recorded != RecordResult::kRecorded)
			return;
		try {
			std::scoped_lock lock(activeCpuMapMutex);
			activeCpuMaps.insert_or_assign(
				ActiveCpuMapKey{ a_context, a_resource.d3dObject, a_subresource },
				ActiveCpuMap{
					.captureGeneration = a_expectedCaptureGeneration,
					.observationId = mapObservationId,
					.completedQpcTick = a_completedQpcTick,
					.mapType = a_mapType,
					.mapFlags = a_mapFlags,
					.rowPitch = a_rowPitch,
					.depthPitch = a_depthPitch,
				});
		} catch (...) {
			// CPU-access correlation is diagnostic and must never affect the game.
		}
	}

	void Runtime::RecordCpuUnmap(
		std::uintptr_t a_context,
		const ResourceObservationInput& a_resource,
		std::uint32_t a_subresource,
		std::uint64_t a_completedQpcTick,
		std::uint64_t a_expectedCaptureGeneration) noexcept
	{
		if (a_expectedCaptureGeneration == 0 || collector.ActiveGeneration() != a_expectedCaptureGeneration ||
			a_context == 0 || a_context != immediateContext.load(std::memory_order_acquire)) {
			return;
		}
		ActiveCpuMap map;
		bool matched = false;
		{
			std::scoped_lock lock(activeCpuMapMutex);
			const auto found = activeCpuMaps.find(
				ActiveCpuMapKey{ a_context, a_resource.d3dObject, a_subresource });
			if (found != activeCpuMaps.end() && found->second.captureGeneration == a_expectedCaptureGeneration) {
				map = found->second;
				activeCpuMaps.erase(found);
				matched = true;
			}
		}
		const auto contextObservationId = EnsureImmediateContextObservation();
		if (contextObservationId == 0)
			return;
		const auto commandStreamSequence = NextCommandStreamSequence();
		const auto resource = ObserveResource(a_resource, contextObservationId, commandStreamSequence);
		if (resource.observationId == 0 || resource.sessionGeneration != a_expectedCaptureGeneration)
			return;
		const auto mappedDuration = matched && a_completedQpcTick >= map.completedQpcTick ?
		                                a_completedQpcTick - map.completedQpcTick :
		                                0;
		collector.RecordForGeneration(
			EventKind::kResourceCpuAccess,
			ResourceCpuAccessPayload(
				ResourceCpuAccessPhase::kUnmap, matched ? map.observationId : 0,
				resource.observationId, a_subresource, matched ? map.mapType : 0,
				matched ? map.mapFlags : 0, mappedDuration, 0,
				matched ? map.rowPitch : 0, matched ? map.depthPitch : 0),
			contextObservationId,
			a_expectedCaptureGeneration,
			commandStreamSequence);
	}

	void Runtime::RecordVisibilityCandidate(
		std::uintptr_t a_object,
		std::uint32_t a_objectIndex,
		std::uint64_t a_producerFrame) noexcept
	{
		if (!collector.IsCapturing() || a_object == 0)
			return;
		collector.Record(
			EventKind::kVisibilityCandidate,
			VisibilityCandidatePayload(a_object, a_objectIndex, a_producerFrame));
	}

	std::uint64_t Runtime::RecordVisibilityResultReady(
		std::uintptr_t a_context,
		const ResourceVersionInput& a_version,
		const ResourceViewInput& a_view,
		std::uint32_t a_objectCount) noexcept
	{
		if (!collector.IsCapturing() || a_context == 0 ||
			a_context != immediateContext.load(std::memory_order_acquire)) {
			return 0;
		}
		const auto contextObservationId = EnsureImmediateContextObservation();
		if (contextObservationId == 0)
			return 0;
		const auto commandStreamSequence = NextCommandStreamSequence();
		const auto resource = ObserveResource(
			a_version.resource, contextObservationId, commandStreamSequence);
		if (resource.observationId == 0)
			return 0;
		const auto versionObservationId = collector.AllocateObservationId(resource.sessionGeneration);
		if (versionObservationId == 0)
			return 0;
		if (collector.RecordForGeneration(
				EventKind::kResourceVersionObserved,
				ResourceVersionPayload(versionObservationId, resource.observationId, a_version),
				contextObservationId,
				resource.sessionGeneration,
				commandStreamSequence) != RecordResult::kRecorded) {
			return 0;
		}

		auto view = a_view;
		view.resource = a_version.resource;
		view.view.kind = TargetViewKind::kShaderResource;
		const auto viewObservation = ObserveResourceView(
			view, contextObservationId, commandStreamSequence);
		collector.RecordForGeneration(
			EventKind::kVisibilityResultReady,
			VisibilityResultPayload(
				versionObservationId, viewObservation.observationId,
				a_objectCount, a_version.producerFrame),
			contextObservationId,
			resource.sessionGeneration,
			commandStreamSequence);
		return versionObservationId;
	}

	std::uint64_t Runtime::DeclareVisibilitySubmission(
		std::uintptr_t a_context,
		const VisibilitySubmissionInput& a_submission) noexcept
	{
		if (!collector.IsCapturing() || a_context == 0 ||
			a_context != immediateContext.load(std::memory_order_acquire)) {
			return 0;
		}
		const auto generation = collector.ActiveGeneration();
		const auto contextObservationId = EnsureImmediateContextObservation();
		if (generation == 0 || contextObservationId == 0)
			return 0;
		const auto commandStreamSequence = NextCommandStreamSequence();
		const auto requested = ObserveResourceView(
			a_submission.requestedView, contextObservationId, commandStreamSequence);
		const auto effective = ObserveResourceView(
			a_submission.effectiveView, contextObservationId, commandStreamSequence);
		const auto submissionObservationId = collector.AllocateObservationId(generation);
		if (submissionObservationId == 0)
			return 0;
		if (collector.RecordForGeneration(
				EventKind::kVisibilityConsumed,
				VisibilitySubmissionPayload(
					submissionObservationId, a_submission,
					requested.observationId, effective.observationId),
				contextObservationId,
				generation,
				commandStreamSequence,
				boundTargetBindingObservationId.load(std::memory_order_acquire),
				submissionObservationId) != RecordResult::kRecorded) {
			return 0;
		}
		pendingVisibilitySubmission = {
			.owner = this,
			.generation = generation,
			.observationId = submissionObservationId,
			.context = a_context,
		};
		return submissionObservationId;
	}

	void Runtime::ClearPendingVisibilitySubmission(std::uintptr_t a_context) noexcept
	{
		if (pendingVisibilitySubmission.owner == this &&
			(a_context == 0 || pendingVisibilitySubmission.context == a_context)) {
			pendingVisibilitySubmission = {};
		}
	}

	void Runtime::RecordCullDecision(
		std::uint64_t a_resourceVersionObservationId,
		std::uint64_t a_captureGeneration,
		std::uint32_t a_objectIndex,
		bool a_producerVisible,
		std::uint32_t a_totalDraws,
		std::uint32_t a_lightingDraws,
		std::uint32_t a_distantTreeDraws,
		std::uint32_t a_grassDraws,
		std::uint64_t a_producerFrame) noexcept
	{
		if (!collector.IsCapturing() || a_resourceVersionObservationId == 0 ||
			a_captureGeneration == 0 || collector.ActiveGeneration() != a_captureGeneration) {
			return;
		}
		collector.Record(
			EventKind::kCullDecision,
			CullDecisionPayload(
				a_resourceVersionObservationId, a_objectIndex, a_producerVisible,
				a_totalDraws, a_lightingDraws, a_distantTreeDraws, a_grassDraws,
				a_producerFrame));
	}

	void Runtime::RecordEyeSubmission(
		const ResourceObservationInput& a_resource,
		Eye a_eye,
		std::uint8_t a_eyeMask,
		float a_uMin,
		float a_vMin,
		float a_uMax,
		float a_vMax,
		std::uint32_t a_submitFlags,
		std::uint64_t a_compositorCycle) noexcept
	{
		if (!collector.IsCapturing() || a_resource.d3dObject == 0)
			return;
		const auto resource = ObserveResource(a_resource, 0, 0);
		if (resource.observationId == 0)
			return;
		const auto previousFrame = collector.GetThreadFrameContext();
		auto frame = previousFrame;
		frame.eye = a_eye;
		frame.eyeMask = a_eyeMask;
		collector.SetThreadFrameContext(frame);
		collector.RecordForGeneration(
			EventKind::kEyeSubmitted,
			EyeSubmissionPayload(
				resource.observationId, a_eye, a_eyeMask,
				a_uMin, a_vMin, a_uMax, a_vMax, a_submitFlags, a_compositorCycle),
			0,
			resource.sessionGeneration);
		collector.SetThreadFrameContext(previousFrame);
	}

	std::uint64_t Runtime::EnsureImmediateContextObservation() noexcept
	{
		const auto captureGeneration = collector.ActiveGeneration();
		const auto context = immediateContext.load(std::memory_order_acquire);
		if (captureGeneration == 0 || context == 0)
			return 0;

		if (immediateContextObservationGeneration.load(std::memory_order_acquire) == captureGeneration) {
			return immediateContextObservationId.load(std::memory_order_acquire);
		}

		std::lock_guard lock(immediateContextObservationMutex);
		if (immediateContextObservationGeneration.load(std::memory_order_acquire) == captureGeneration) {
			return immediateContextObservationId.load(std::memory_order_acquire);
		}

		const auto observationId = collector.AllocateObservationId(captureGeneration);
		if (observationId == 0)
			return 0;
		const auto recorded = collector.RecordForGeneration(
			EventKind::kDeviceContextObserved,
			DeviceContextObservationPayload(
				observationId, context,
				immediateContextPointerGeneration.load(std::memory_order_acquire),
				DeviceContextKind::kImmediate,
				ContextCreationEvidence::kInitialImmediate),
			observationId,
			captureGeneration);
		if (recorded != RecordResult::kRecorded)
			return 0;

		immediateContextObservationId.store(observationId, std::memory_order_release);
		immediateContextObservationGeneration.store(captureGeneration, std::memory_order_release);
		return observationId;
	}

	std::uint64_t Runtime::NextCommandStreamSequence() noexcept
	{
		return immediateContextCommandSequence.fetch_add(1, std::memory_order_acq_rel) + 1;
	}

	std::uint64_t Runtime::EnsureBoundStageObservation(ShaderStage a_stage) noexcept
	{
		std::atomic_uintptr_t* boundShader = nullptr;
		std::atomic_uint64_t* boundObservation = nullptr;
		switch (a_stage) {
		case ShaderStage::kVertex:
			boundShader = &boundVertexShader;
			boundObservation = &boundVertexShaderObservationId;
			break;
		case ShaderStage::kPixel:
			boundShader = &boundPixelShader;
			boundObservation = &boundPixelShaderObservationId;
			break;
		case ShaderStage::kCompute:
			boundShader = &boundComputeShader;
			boundObservation = &boundComputeShaderObservationId;
			break;
		}
		if (!boundShader || !boundObservation)
			return 0;

		const auto existing = boundObservation->load(std::memory_order_acquire);
		if (existing != 0)
			return existing;
		const auto observed = ObserveBoundStage(
			a_stage, boundShader->load(std::memory_order_acquire));
		if (observed.observationId != 0)
			boundObservation->store(observed.observationId, std::memory_order_release);
		return observed.observationId;
	}

	std::optional<Runtime::PersistentStageShaderIdentity> Runtime::FindCreatedStageShader(
		ShaderStage a_stage,
		std::uintptr_t a_d3dObject) const noexcept
	{
		try {
			std::shared_lock lock(persistentStageShaderMutex);
			const auto found = persistentStageShaders.find({ a_stage, a_d3dObject });
			if (found != persistentStageShaders.end())
				return found->second;
		} catch (...) {
		}
		return std::nullopt;
	}

	StageShaderObservationResult Runtime::ObserveBoundStage(
		ShaderStage a_stage,
		std::uintptr_t a_d3dObject) noexcept
	{
		if (a_d3dObject == 0 || !collector.IsCapturing())
			return {};

		const StageShaderObservationInput input{
			.stage = a_stage,
			.d3dObject = a_d3dObject,
		};
		return ObserveStageShaderWithPersistent(input);
	}

	void Runtime::PublishBoundStageObservation(
		ShaderStage a_stage,
		std::uintptr_t a_d3dObject,
		const StageShaderObservationResult& a_observation) noexcept
	{
		if (a_d3dObject == 0 || a_observation.observationId == 0)
			return;
		switch (a_stage) {
		case ShaderStage::kVertex:
			if (boundVertexShader.load(std::memory_order_acquire) == a_d3dObject)
				boundVertexShaderObservationId.store(a_observation.observationId, std::memory_order_release);
			break;
		case ShaderStage::kPixel:
			if (boundPixelShader.load(std::memory_order_acquire) == a_d3dObject)
				boundPixelShaderObservationId.store(a_observation.observationId, std::memory_order_release);
			break;
		case ShaderStage::kCompute:
			if (boundComputeShader.load(std::memory_order_acquire) == a_d3dObject)
				boundComputeShaderObservationId.store(a_observation.observationId, std::memory_order_release);
			break;
		}
	}

	void Runtime::RecordDraw(
		std::uintptr_t a_context,
		DrawOperation a_operation,
		std::uint64_t a_argument0,
		std::uint64_t a_argument1,
		std::uint64_t a_argument2,
		std::uint64_t a_argument3) noexcept
	{
		if (!collector.IsCapturing() || a_context == 0) {
			return;
		}
		if (a_context != immediateContext.load(std::memory_order_acquire)) {
			const auto context = EnsureContextObservation(a_context);
			if (context.kind != DeviceContextKind::kDeferred || context.observationId == 0)
				return;
			std::uint64_t commandSequence = 0;
			std::uint64_t recordingObservationId = 0;
			std::uint64_t vertexObservationId = 0;
			std::uint64_t pixelObservationId = 0;
			{
				std::scoped_lock lock(deferredContextMutex);
				const auto found = deferredContexts.find(a_context);
				if (found == deferredContexts.end() || found->second.observationId != context.observationId)
					return;
				auto& state = found->second;
				commandSequence = ++state.commandSequence;
				recordingObservationId = state.recordingObservationId;
				vertexObservationId = state.boundVertexShaderObservationId;
				pixelObservationId = state.boundPixelShaderObservationId;
			}
			if (recordingObservationId == 0) {
				collector.CountFiltered();
				return;
			}
			const auto recordResult = collector.RecordForGeneration(
				EventKind::kDraw,
				DrawCallPayload(
					a_context, a_operation, vertexObservationId, pixelObservationId,
					a_argument0, a_argument1, a_argument2, a_argument3),
				context.observationId, collector.ActiveGeneration(), commandSequence,
				0, 0, 0, recordingObservationId, true);
			if (recordResult != RecordResult::kRecorded) {
				MarkDeferredRecordingIncomplete(
					a_context, context.observationId, recordingObservationId,
					CommandRecordingIncompleteReason::kEventNotRecorded);
			}
			return;
		}
		const auto commandStreamSequence = NextCommandStreamSequence();
		const auto captureGeneration = collector.ActiveGeneration();
		std::uint64_t preparedGeometrySetupObservationId = 0;
		if (pendingGeometrySubmission.owner == this) {
			if (pendingGeometrySubmission.generation == captureGeneration &&
				pendingGeometrySubmission.context == a_context) {
				preparedGeometrySetupObservationId = pendingGeometrySubmission.observationId;
			} else {
				pendingGeometrySubmission = {};
			}
		}
		if (!collector.IsExecutionAllowedByGeometryScope(preparedGeometrySetupObservationId)) {
			if (pendingVisibilitySubmission.owner == this &&
				pendingVisibilitySubmission.generation == captureGeneration &&
				pendingVisibilitySubmission.context == a_context) {
				pendingVisibilitySubmission = {};
			}
			collector.CountFiltered();
			return;
		}
		if (preparedGeometrySetupObservationId != 0)
			pendingGeometrySubmission = {};
		const auto contextObservationId = EnsureImmediateContextObservation();
		if (contextObservationId == 0)
			return;
		const auto vertexObservationId = EnsureBoundStageObservation(ShaderStage::kVertex);
		const auto pixelObservationId = EnsureBoundStageObservation(ShaderStage::kPixel);
		std::uint64_t submissionObservationId = 0;
		if (pendingVisibilitySubmission.owner == this &&
			pendingVisibilitySubmission.generation == captureGeneration &&
			pendingVisibilitySubmission.context == a_context) {
			submissionObservationId = pendingVisibilitySubmission.observationId;
			pendingVisibilitySubmission = {};
		}
		collector.RecordForGeneration(
			EventKind::kDraw,
			DrawCallPayload(
				a_context, a_operation, vertexObservationId, pixelObservationId,
				a_argument0, a_argument1, a_argument2, a_argument3),
			contextObservationId, captureGeneration, commandStreamSequence,
			boundTargetBindingObservationId.load(std::memory_order_acquire),
			submissionObservationId,
			preparedGeometrySetupObservationId);
	}

	void Runtime::RecordDispatch(
		std::uintptr_t a_context,
		DispatchOperation a_operation,
		std::uint64_t a_argument0,
		std::uint64_t a_argument1,
		std::uint64_t a_argument2,
		std::uint64_t a_argument3) noexcept
	{
		if (!collector.IsCapturing() || a_context == 0) {
			return;
		}
		if (a_context != immediateContext.load(std::memory_order_acquire)) {
			const auto context = EnsureContextObservation(a_context);
			if (context.kind != DeviceContextKind::kDeferred || context.observationId == 0)
				return;
			std::uint64_t commandSequence = 0;
			std::uint64_t recordingObservationId = 0;
			std::uint64_t computeObservationId = 0;
			{
				std::scoped_lock lock(deferredContextMutex);
				const auto found = deferredContexts.find(a_context);
				if (found == deferredContexts.end() || found->second.observationId != context.observationId)
					return;
				auto& state = found->second;
				commandSequence = ++state.commandSequence;
				recordingObservationId = state.recordingObservationId;
				computeObservationId = state.boundComputeShaderObservationId;
			}
			if (recordingObservationId == 0) {
				collector.CountFiltered();
				return;
			}
			const auto recordResult = collector.RecordForGeneration(
				EventKind::kDispatch,
				DispatchCallPayload(
					a_context, a_operation, computeObservationId,
					a_argument0, a_argument1, a_argument2, a_argument3),
				context.observationId, collector.ActiveGeneration(), commandSequence,
				0, 0, 0, recordingObservationId, true);
			if (recordResult != RecordResult::kRecorded) {
				MarkDeferredRecordingIncomplete(
					a_context, context.observationId, recordingObservationId,
					CommandRecordingIncompleteReason::kEventNotRecorded);
			}
			return;
		}
		const auto contextObservationId = EnsureImmediateContextObservation();
		if (contextObservationId == 0)
			return;
		const auto computeObservationId = EnsureBoundStageObservation(ShaderStage::kCompute);
		const auto commandStreamSequence = NextCommandStreamSequence();
		const auto captureGeneration = collector.ActiveGeneration();
		collector.RecordForGeneration(
			EventKind::kDispatch,
			DispatchCallPayload(
				a_context, a_operation, computeObservationId,
				a_argument0, a_argument1, a_argument2, a_argument3),
			contextObservationId, captureGeneration, commandStreamSequence);
	}

	Collector::ScopeGuard Runtime::EnterGeometry(const GeometryBoundary& a_boundary) noexcept
	{
		if (pendingGeometrySubmission.owner == this)
			pendingGeometrySubmission = {};
		if (!collector.IsCapturing())
			return {};
		if (!collector.IsGeometryShaderTypeSelected(a_boundary.shaderType)) {
			collector.CountFiltered(2);
			return {};
		}

		const auto sceneObject = collector.ObserveSceneObject(a_boundary.sceneObject);
		const auto captureGeneration = sceneObject.sessionGeneration != 0 ?
		                                   sceneObject.sessionGeneration :
		                                   collector.ActiveGeneration();
		if (sceneObject.firstSeen) {
			collector.RecordForGeneration(
				EventKind::kObjectObserved,
				SceneObjectObservationPayload(a_boundary.sceneObject, sceneObject),
				0,
				captureGeneration);
		}

		auto geometryInput = a_boundary.geometryObservation;
		if (geometryInput.geometry == 0)
			geometryInput.geometry = a_boundary.geometry;
		if (geometryInput.sceneObjectObservationId == 0)
			geometryInput.sceneObjectObservationId = sceneObject.observationId;
		const auto geometry = collector.ObserveGeometry(geometryInput);
		if (geometry.firstSeen) {
			collector.RecordForGeneration(
				EventKind::kGeometryObserved,
				GeometryObservationPayload(geometryInput, geometry),
				0,
				geometry.sessionGeneration);
		}

		auto materialInput = a_boundary.materialState;
		for (std::size_t index = 0; index < materialInput.textureBindingCount; ++index) {
			auto& binding = materialInput.textureBindings[index];
			if (binding.resource.d3dObject != 0) {
				binding.resourceObservationId = ObserveResource(binding.resource, 0, 0).observationId;
			}
		}
		const auto material = collector.ObserveMaterialState(materialInput);
		if (material.firstSeen) {
			collector.RecordForGeneration(
				EventKind::kMaterialObserved,
				MaterialStateObservationPayload(materialInput, material),
				0,
				material.sessionGeneration);
		}

		const auto observationId = collector.AllocateObservationId(captureGeneration);
		if (observationId == 0)
			return {};
		const auto payload = GeometryPayload(
			a_boundary, geometry.observationId, material.observationId);
		auto scope = collector.EnterScope(
			ScopeKind::kGeometry,
			observationId,
			EventKind::kGeometrySetupBegin,
			EventKind::kGeometrySetupEnd,
			payload,
			payload,
			captureGeneration);
		if (scope.IsActive()) {
			pendingGeometrySubmission = {
				.owner = this,
				.generation = captureGeneration,
				.observationId = observationId,
				.context = immediateContext.load(std::memory_order_acquire),
			};
		}
		return scope;
	}

	Runtime& GetRuntime() noexcept
	{
		static Runtime runtime;
		return runtime;
	}
}
