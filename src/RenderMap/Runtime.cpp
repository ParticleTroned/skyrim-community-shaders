#include "RenderMap/Runtime.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <functional>
#include <new>
#include <thread>

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
		thread_local const Runtime* postProcessingOwner = nullptr;
		thread_local std::uint64_t postProcessingGeneration = 0;
		thread_local std::uint64_t transferOperation = 0;

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
		std::scoped_lock lifecycleLock(captureLifecycleMutex);
		if (collector.ActiveGeneration() != 0 || collector.IsDraining())
			return collector.Start(a_config);

		// Reset while hooks still observe an inactive collector; publishing first
		// could admit new-generation evidence and then erase it here.
		{
			{
				std::scoped_lock lock(activeCpuMapMutex);
				activeCpuMaps.clear();
			}
			immediateContextObservationId.store(0, std::memory_order_release);
			immediateContextObservationGeneration.store(0, std::memory_order_release);
			immediateContextCommandSequence.store(0, std::memory_order_release);
			boundTargetBindingObservationId.store(0, std::memory_order_release);
			targetStateObservationGeneration.store(0, std::memory_order_release);
			ResetImmediateStageObservations(false);
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
			std::scoped_lock lock(commandListMutex);
			// Command-list identities are capture-local. Lists created before this
			// capture are deliberately re-admitted as first-seen execution evidence.
			commandLists.clear();
		}
		return collector.Start(a_config);
	}

	std::optional<CaptureSnapshot> Runtime::StopCapture(
		StopReason a_reason,
		std::chrono::milliseconds a_drainTimeout)
	{
		std::scoped_lock lifecycleLock(captureLifecycleMutex);
		return collector.Stop(a_reason, a_drainTimeout);
	}

	bool Runtime::IsCapturing() const noexcept
	{
		return collector.IsCapturing();
	}

	bool Runtime::ActivatePostProcessingWindow(std::uint32_t a_target, std::uint64_t a_frame,
		std::uint64_t a_publicationGeneration) noexcept
	{
		return collector.ActivatePostProcessingWindow(a_target, a_frame, a_publicationGeneration);
	}

	void Runtime::CompleteWindowBootstrap(bool a_success) noexcept
	{
		std::scoped_lock lifecycleLock(captureLifecycleMutex);
		const auto generation = postProcessingOwner == this ?
		                            postProcessingGeneration :
		                            collector.ActiveGeneration();
		if (generation != 0 && collector.ActiveGeneration() == generation)
			collector.CompleteWindowBootstrap(a_success, generation);
	}

	std::uint64_t Runtime::PostProcessingCaptureGeneration() const noexcept
	{
		return postProcessingOwner == this ? postProcessingGeneration : 0;
	}

	void Runtime::BindBootstrapStage(ShaderStage a_stage, std::uintptr_t a_d3dObject,
		std::uint64_t a_expectedCaptureGeneration) noexcept
	{
#if defined(CSX_RENDER_MAP_TESTING)
		PauseProducerPublicationForTesting();
#endif
		// Serialize diagnostic publication with capture reset, not native D3D queries.
		std::scoped_lock lifecycleLock(captureLifecycleMutex);
		if (collector.ActiveGeneration() != a_expectedCaptureGeneration)
			return;
		SetImmediateBoundStage(a_stage, a_d3dObject);
		if (EnsureImmediateContextObservation(a_expectedCaptureGeneration) == 0)
			return;
		NextCommandStreamSequence();
		PublishBoundStageObservation(a_stage, a_d3dObject,
			ObserveBoundStage(a_stage, a_d3dObject, a_expectedCaptureGeneration));
	}

	void Runtime::RecordPostProcessingBootstrap(const std::array<std::uintptr_t, 6>& a_shaders,
		std::uint64_t a_expectedCaptureGeneration) noexcept
	{
		if (a_expectedCaptureGeneration == 0 ||
			PostProcessingCaptureGeneration() != a_expectedCaptureGeneration || !IsInsidePostProcessing())
			return;
		BindBootstrapStage(ShaderStage::kVertex, a_shaders[0], a_expectedCaptureGeneration);
		BindBootstrapStage(ShaderStage::kPixel, a_shaders[4], a_expectedCaptureGeneration);
		BindBootstrapStage(ShaderStage::kCompute, a_shaders[5], a_expectedCaptureGeneration);
		std::scoped_lock lifecycleLock(captureLifecycleMutex);
		if (collector.ActiveGeneration() != a_expectedCaptureGeneration)
			return;
		transferOperation = 0;
		collector.RecordForGeneration(EventKind::kRasterStateObserved,
			{ .schema = static_cast<std::uint16_t>(PayloadSchema::kNativePipelineSnapshot),
				.words = { a_shaders[0], a_shaders[1], a_shaders[2], a_shaders[3], a_shaders[4], a_shaders[5] } },
			EnsureImmediateContextObservation(a_expectedCaptureGeneration),
			a_expectedCaptureGeneration, NextCommandStreamSequence());
	}

	CaptureWindowSnapshot Runtime::GetCaptureWindow() const noexcept
	{
		return collector.GetCaptureWindow();
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
		collector.PollCaptureWindow(a_cpuFrame);
		auto frame = collector.GetThreadFrameContext();
		frame.cpuFrame = a_cpuFrame;
		collector.SetThreadFrameContext(frame);
	}

	void Runtime::SetFrameContext(const FrameContext& a_context) noexcept
	{
		collector.SetThreadFrameContext(a_context);
	}

	Runtime::PostProcessingScope::PostProcessingScope(Runtime& a_owner,
		const ResourceObservationInput& a_source, const ResourceObservationInput& a_destination,
		std::uint32_t a_target, std::uint64_t a_publicationGeneration) noexcept :
		previousOwner(postProcessingOwner), previousGeneration(postProcessingGeneration), previousOperation(transferOperation)
	{
		const auto generation = a_owner.collector.ActiveGeneration();
		if (!generation)
			return;
		const auto context = a_owner.EnsureImmediateContextObservation(generation);
		const auto sequence = a_owner.NextCommandStreamSequence();
		const auto source = a_owner.ObserveResource(a_source, context, sequence, generation);
		const auto destination = a_owner.ObserveResource(a_destination, context, sequence, generation);
		if (!context || (a_source.d3dObject && (!source.observationId || source.sessionGeneration != generation)) ||
			(a_destination.d3dObject && (!destination.observationId || destination.sessionGeneration != generation)))
			return;
		const auto observation = a_owner.collector.AllocateObservationId(generation);
		if (!observation)
			return;
		const EventPayload payload{
			.schema = static_cast<std::uint16_t>(PayloadSchema::kPostProcessingBoundary),
			.words = { observation, source.observationId, destination.observationId,
				a_target, a_publicationGeneration },
		};
		scope = a_owner.collector.EnterScope(ScopeKind::kRenderPass, observation,
			EventKind::kRenderPassEnter, EventKind::kRenderPassExit, payload, payload, generation);
		if (scope.IsActive()) {
			transferOperation = 0;
			postProcessingOwner = &a_owner;
			postProcessingGeneration = generation;
		}
	}

	Runtime::PostProcessingScope::~PostProcessingScope()
	{
		postProcessingOwner = previousOwner;
		postProcessingGeneration = previousGeneration;
		transferOperation = previousOperation;
	}

	Runtime::PostProcessingScope Runtime::EnterPostProcessing(
		const ResourceObservationInput& a_source, const ResourceObservationInput& a_destination,
		std::uint32_t a_target, std::uint64_t a_publicationGeneration) noexcept
	{
		return PostProcessingScope(*this, a_source, a_destination, a_target, a_publicationGeneration);
	}

	bool Runtime::IsInsidePostProcessing() const noexcept
	{
		return postProcessingOwner == this && postProcessingGeneration != 0 &&
		       postProcessingGeneration == collector.ActiveGeneration();
	}

	void Runtime::BeginTransferOperation(std::uintptr_t a_context, bool a_compute,
		std::uint32_t a_operation, const std::array<std::uintptr_t, 6>& a_shaders) noexcept
	{
		transferOperation = 0;
		if (!IsInsidePostProcessing() || a_context != immediateContext.load(std::memory_order_acquire))
			return;
		const auto observation = collector.AllocateObservationId(postProcessingGeneration);
		const auto recorded = collector.RecordForGeneration(EventKind::kRasterStateObserved,
			{ .schema = static_cast<std::uint16_t>(PayloadSchema::kTransferOperation), .words = {
																						   observation,
																						   a_operation | (static_cast<std::uint64_t>(a_compute) << 32u),
																						   a_shaders[0],
																						   a_shaders[1],
																						   a_shaders[2],
																						   a_shaders[3],
																						   a_shaders[4],
																						   a_shaders[5],
																					   } },
			EnsureImmediateContextObservation(postProcessingGeneration), postProcessingGeneration, NextCommandStreamSequence());
		if (recorded == RecordResult::kRecorded)
			transferOperation = observation;
	}

	void Runtime::RecordRasterState(std::uintptr_t a_context, std::uint32_t a_slot,
		const std::array<float, 6>& a_viewport, const std::array<std::int32_t, 4>& a_scissor,
		std::uint32_t a_viewportCount, std::uint32_t a_scissorCount, bool a_scissorEnabled) noexcept
	{
		if (!IsInsidePostProcessing() || a_context != immediateContext.load(std::memory_order_acquire))
			return;
		collector.RecordForGeneration(EventKind::kRasterStateObserved,
			{ .schema = static_cast<std::uint16_t>(PayloadSchema::kRasterState), .words = {
																					 a_slot,
																					 PackFloats(a_viewport[0], a_viewport[1]),
																					 PackFloats(a_viewport[2], a_viewport[3]),
																					 PackFloats(a_viewport[4], a_viewport[5]),
																					 static_cast<std::uint32_t>(a_scissor[0]) | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(a_scissor[1])) << 32u),
																					 static_cast<std::uint32_t>(a_scissor[2]) | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(a_scissor[3])) << 32u),
																					 a_viewportCount | (static_cast<std::uint64_t>(a_scissorCount) << 32u),
																					 (transferOperation << 1u) | static_cast<std::uint64_t>(a_scissorEnabled),
																				 } },
			EnsureImmediateContextObservation(postProcessingGeneration), postProcessingGeneration, NextCommandStreamSequence());
	}

	void Runtime::RecordTransferResourceAccess(std::uintptr_t a_context,
		const ResourceViewInput& a_view, ResourceStage a_stage, std::uint32_t a_slot, bool a_write) noexcept
	{
		if (!IsInsidePostProcessing() ||
			a_context != immediateContext.load(std::memory_order_acquire))
			return;
		const auto generation = postProcessingGeneration;
		const auto context = EnsureImmediateContextObservation(generation);
		const auto sequence = NextCommandStreamSequence();
		const auto resource = ObserveResource(a_view.resource, context, sequence, generation);
		const auto view = a_view.view.d3dObject ? ObserveResourceView(a_view, context, sequence, generation) : TargetViewObservationResult{};
		if (!context || !resource.observationId || resource.sessionGeneration != generation ||
			(a_view.view.d3dObject && (!view.observationId || view.sessionGeneration != generation))) {
			if (a_write) {
				const std::lock_guard lock(transferVersionMutex);
				// A completed write cannot retain an older positive command attribution.
				if (collector.ActiveGeneration() == generation)
					transferVersions.Invalidate();
			}
			return;
		}
		const auto frame = collector.GetThreadFrameContext().cpuFrame;
		TransferVersions::Version version;
		bool capacityAvailable = true;
		{
			const std::lock_guard lock(transferVersionMutex);
			if (collector.ActiveGeneration() != generation)
				return;
			if (a_write) {
				version = { resource.observationId, collector.AllocateObservationId(generation), sequence, frame };
				capacityAvailable = version.observation && frame != kUnknownFrame &&
				                    transferVersions.CanWrite(generation, resource.observationId);
				if (!capacityAvailable)
					version = {};
			} else {
				version = transferVersions.Read(generation, resource.observationId, frame);
			}
			const auto recorded = collector.RecordForGeneration(EventKind::kTransferResourceAccess,
				{ .schema = static_cast<std::uint16_t>(PayloadSchema::kTransferResourceAccess), .words = {
																									resource.observationId,
																									view.observationId,
																									version.observation,
																									version.command,
																									static_cast<std::uint64_t>(a_stage),
																									a_slot,
																									static_cast<std::uint64_t>(a_write) | (static_cast<std::uint64_t>(capacityAvailable) << 1u) | (static_cast<std::uint64_t>(a_view.view.kind) << 8u),
																									transferOperation,
																								} },
				context, generation, sequence);
			if (a_write && capacityAvailable && recorded == RecordResult::kRecorded)
				(void)transferVersions.Write(generation, version);
			else if (a_write)
				transferVersions.Invalidate();
		}
	}

	void Runtime::RecordTransferCopyRegion(std::uintptr_t a_context, std::uint32_t a_sourceSubresource,
		std::uint32_t a_destinationSubresource, const std::array<std::uint32_t, 3>& a_destination,
		const std::array<std::uint32_t, 6>& a_sourceBox, bool a_hasSourceBox) noexcept
	{
		if (!IsInsidePostProcessing() || a_context != immediateContext.load(std::memory_order_acquire))
			return;
		const auto pack = [](std::uint32_t a_low, std::uint32_t a_high) {
			return a_low | (static_cast<std::uint64_t>(a_high) << 32u);
		};
		collector.RecordForGeneration(EventKind::kResourceFlow,
			{ .schema = static_cast<std::uint16_t>(PayloadSchema::kTransferCopyRegion), .words = {
																							transferOperation,
																							pack(a_sourceSubresource, a_destinationSubresource),
																							pack(a_destination[0], a_destination[1]),
																							a_destination[2],
																							pack(a_sourceBox[0], a_sourceBox[1]),
																							pack(a_sourceBox[2], a_sourceBox[3]),
																							pack(a_sourceBox[4], a_sourceBox[5]),
																							a_hasSourceBox,
																						} },
			EnsureImmediateContextObservation(postProcessingGeneration), postProcessingGeneration, NextCommandStreamSequence());
	}

	Collector::ScopeGuard Runtime::EnterRenderPass(const RenderPassBoundary& a_boundary) noexcept
	{
		if (!collector.IsCapturing())
			return {};
		const auto captureGeneration = collector.ActiveGeneration();
		if (!captureGeneration)
			return {};
		const auto observationId = collector.AllocateObservationId(captureGeneration);
		if (observationId == 0)
			return {};

#if defined(CSX_RENDER_MAP_TESTING)
		PauseProducerPublicationForTesting();
#endif
		const auto payload = RenderPassPayload(a_boundary);
		return collector.EnterScope(
			ScopeKind::kRenderPass,
			observationId,
			EventKind::kRenderPassEnter,
			EventKind::kRenderPassExit,
			payload,
			payload,
			captureGeneration);
	}

	Collector::ScopeGuard Runtime::EnterTechnique(const TechniqueBoundary& a_boundary) noexcept
	{
		if (!collector.IsCapturing())
			return {};
		const auto captureGeneration = collector.ActiveGeneration();
		if (!captureGeneration)
			return {};
		const auto shaderObservation = collector.ObserveShader({
																   .shader = a_boundary.shader,
																   .shaderType = a_boundary.shaderType,
																   .fxpFilename = a_boundary.fxpFilename,
																   .imageSpaceName = a_boundary.imageSpaceName,
																   .compileSourceName = a_boundary.compileSourceName,
																   .definesSuffix = a_boundary.definesSuffix,
															   },
			captureGeneration);
#if defined(CSX_RENDER_MAP_TESTING)
		PauseProducerPublicationForTesting();
#endif
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
			const auto catalogue = EnsurePersistentStageShaderCatalogue();
			std::unique_lock lock(catalogue->mutex);
			const PersistentStageShaderKey key{ a_stage, a_d3dObject };
			if (!catalogue->records.contains(key) &&
				catalogue->records.size() >= kMaximumPersistentStageShaders) {
				RecordShaderMetadataFailure();
				return;
			}
			catalogue->records.insert_or_assign(
				key, identity);
		} catch (...) {
			RecordShaderMetadataFailure();
		}
	}

	std::function<void()> Runtime::MakeStageShaderRetirementCallback(
		ShaderStage a_stage, std::uintptr_t a_d3dObject) const
	{
		const std::weak_ptr<PersistentStageShaderCatalogue> weakCatalogue = EnsurePersistentStageShaderCatalogue();
		return [weakCatalogue, key = PersistentStageShaderKey{ a_stage, a_d3dObject }]() noexcept {
			try {
				if (const auto catalogue = weakCatalogue.lock()) {
					std::unique_lock lock(catalogue->mutex);
					catalogue->records.erase(key);
				}
			} catch (...) {
				RecordShaderMetadataFailure();
			}
		};
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
				.loaderType = std::string(a_loaderType.substr(0, kMaximumShaderNameLength + 1)),
				.compileSourceName = std::string(a_compileSourceName.substr(0, kMaximumShaderNameLength + 1)),
				.descriptor = a_descriptor,
			};
			const auto catalogue = persistentStageShaderCatalogue.load(std::memory_order_acquire);
			if (!catalogue)
				return;
			std::unique_lock lock(catalogue->mutex);
			const auto found = catalogue->records.find({ a_stage, a_d3dObject });
			if (found == catalogue->records.end())
				return;
			auto& identity = found->second;
			if (std::find(identity.engineAliases.begin(), identity.engineAliases.end(), alias) ==
				identity.engineAliases.end()) {
				if (identity.engineAliases.size() < kMaximumEngineShaderAliasesPerStage) {
					identity.engineAliases.push_back(std::move(alias));
				} else {
					identity.engineAliasesOverflowed = true;
				}
			}
		} catch (...) {
			RecordShaderMetadataFailure();
		}
	}

	StageShaderObservationResult Runtime::ObserveStageShaderWithPersistent(
		const StageShaderObservationInput& a_input, std::uint64_t a_expectedCaptureGeneration) noexcept
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
				enriched.engineAliasTotalCount = static_cast<std::uint32_t>(persistent->engineAliases.size()) +
				                                 (persistent->engineAliasesOverflowed ? 1u : 0u);
			}
		}

		auto observation = collector.ObserveStageShader(enriched, a_expectedCaptureGeneration);
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

		const auto captureGeneration = collector.ActiveGeneration();
		if (!captureGeneration)
			return;
		const auto vertex = ObserveStageShaderWithPersistent(a_resolution.vertex.shader, captureGeneration);
		const auto pixel = ObserveStageShaderWithPersistent(a_resolution.pixel.shader, captureGeneration);
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
		ResetImmediateStageObservations(true);
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
		try {
			std::scoped_lock lock(deferredContextMutex);
			if (!deferredContexts.contains(a_context) &&
				deferredContexts.size() >= kMaximumTrackedDeferredContexts) {
				return;
			}
#if defined(CSX_RENDER_MAP_TESTING)
			if (!deferredContexts.contains(a_context) &&
				failNextDeferredContextCatalogueAdmission.exchange(false, std::memory_order_acq_rel)) {
				throw std::bad_alloc();
			}
#endif
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
		} catch (...) {
			// Context provenance is diagnostic. Failure to grow its bounded
			// catalogue must not escape the graphics API callback.
			return;
		}
		if (captureGeneration != 0)
			EnsureContextObservation(a_context);
	}

	void Runtime::ResetImmediatePipelineState() noexcept
	{
		ResetImmediateStageObservations(true);
		boundTargetBindingObservationId.store(0, std::memory_order_release);
		targetStateObservationGeneration.store(0, std::memory_order_release);
		resourceViewStateObservationGeneration.store(0, std::memory_order_release);
		resourceViewStateResetPending.store(true, std::memory_order_release);
		try {
			std::scoped_lock lock(resourceViewStateMutex);
			ApplyEffectiveResourceViewResetLocked();
			resourceViewStateResetPending.store(false, std::memory_order_release);
		} catch (...) {
			// The pending marker lets the next successful state observation
			// complete the diagnostic reset without affecting D3D execution.
		}
	}

	void Runtime::ApplyEffectiveResourceViewResetLocked() noexcept
	{
		for (std::size_t kindIndex = 0; kindIndex < effectiveResourceViews.size(); ++kindIndex) {
			for (std::size_t stageIndex = 0;
				stageIndex < effectiveResourceViews[kindIndex].size(); ++stageIndex) {
				auto& state = effectiveResourceViews[kindIndex][stageIndex];
				auto& needsObservation = resourceViewSlotsNeedingObservation[kindIndex][stageIndex];
				for (std::size_t slot = 0; slot < state.size(); ++slot) {
					if (state[slot] != 0)
						needsObservation.set(slot);
				}
				state.fill(0);
			}
		}
	}

#if defined(CSX_RENDER_MAP_TESTING)
	void Runtime::FailNextDeferredContextCatalogueAdmissionForTesting() noexcept
	{
		failNextDeferredContextCatalogueAdmission.store(true, std::memory_order_release);
	}

	void Runtime::FailNextCommandListCatalogueAdmissionForTesting() noexcept
	{
		failNextCommandListCatalogueAdmission.store(true, std::memory_order_release);
	}

	void Runtime::PauseCommandListAdmissionsForTesting(std::uint32_t a_count) noexcept
	{
		resumeCommandListAdmissions.store(false, std::memory_order_release);
		commandListAdmissionsToPause.store(a_count, std::memory_order_release);
	}

	std::uint32_t Runtime::PausedCommandListAdmissionsForTesting() const noexcept
	{
		return pausedCommandListAdmissions.load(std::memory_order_acquire);
	}

	void Runtime::ResumeCommandListAdmissionsForTesting() noexcept
	{
		resumeCommandListAdmissions.store(true, std::memory_order_release);
	}

	std::size_t Runtime::CommandListCatalogueSizeForTesting() noexcept
	{
		std::scoped_lock lock(commandListMutex);
		return commandLists.size();
	}

	void Runtime::PauseCommandListAdmissionForTesting() noexcept
	{
		auto remaining = commandListAdmissionsToPause.load(std::memory_order_acquire);
		while (remaining != 0) {
			if (!commandListAdmissionsToPause.compare_exchange_weak(remaining, remaining - 1,
					std::memory_order_acq_rel))
				continue;
			pausedCommandListAdmissions.fetch_add(1, std::memory_order_acq_rel);
			while (!resumeCommandListAdmissions.load(std::memory_order_acquire))
				std::this_thread::yield();
			pausedCommandListAdmissions.fetch_sub(1, std::memory_order_acq_rel);
			return;
		}
	}

	void Runtime::PauseNextProducerPublicationForTesting(std::uint32_t a_skipPublications) noexcept
	{
		resumeDeferredPublication.store(false, std::memory_order_release);
		deferredPublicationPaused.store(false, std::memory_order_release);
		producerPublicationsToSkip.store(a_skipPublications, std::memory_order_release);
		pauseNextProducerPublication.store(true, std::memory_order_release);
	}

	void Runtime::PauseProducerPublicationForTesting() noexcept
	{
		if (!pauseNextProducerPublication.load(std::memory_order_acquire))
			return;
		auto remaining = producerPublicationsToSkip.load(std::memory_order_acquire);
		while (remaining != 0) {
			if (producerPublicationsToSkip.compare_exchange_weak(remaining, remaining - 1,
					std::memory_order_acq_rel))
				return;
		}
		if (!pauseNextProducerPublication.exchange(false, std::memory_order_acq_rel))
			return;
		deferredPublicationPaused.store(true, std::memory_order_release);
		while (!resumeDeferredPublication.load(std::memory_order_acquire))
			std::this_thread::yield();
		deferredPublicationPaused.store(false, std::memory_order_release);
		resumeDeferredPublication.store(false, std::memory_order_release);
	}

	void Runtime::PauseNextDeferredPublicationForTesting() noexcept
	{
		resumeDeferredPublication.store(false, std::memory_order_release);
		deferredPublicationPaused.store(false, std::memory_order_release);
		pauseNextDeferredPublication.store(true, std::memory_order_release);
	}

	void Runtime::PauseNextImmediateStagePublicationForTesting() noexcept
	{
		resumeDeferredPublication.store(false, std::memory_order_release);
		deferredPublicationPaused.store(false, std::memory_order_release);
		pauseNextImmediateStagePublication.store(true, std::memory_order_release);
	}

	void Runtime::PauseNextImmediateDispatchAdmissionForTesting() noexcept
	{
		resumeDeferredPublication.store(false, std::memory_order_release);
		deferredPublicationPaused.store(false, std::memory_order_release);
		pauseNextImmediateDispatchAdmission.store(true, std::memory_order_release);
	}

	void Runtime::PauseNextDeferredFinishCleanupForTesting() noexcept
	{
		resumeDeferredPublication.store(false, std::memory_order_release);
		deferredPublicationPaused.store(false, std::memory_order_release);
		pauseNextDeferredFinishCleanup.store(true, std::memory_order_release);
	}

	bool Runtime::IsDeferredPublicationPausedForTesting() const noexcept
	{
		return deferredPublicationPaused.load(std::memory_order_acquire);
	}

	void Runtime::ResumeDeferredPublicationForTesting() noexcept
	{
		resumeDeferredPublication.store(true, std::memory_order_release);
	}

	void Runtime::PauseDeferredPublicationBeforeAppendForTesting() noexcept
	{
		if (!pauseNextDeferredPublication.exchange(false, std::memory_order_acq_rel))
			return;
		deferredPublicationPaused.store(true, std::memory_order_release);
		while (!resumeDeferredPublication.load(std::memory_order_acquire))
			std::this_thread::yield();
		deferredPublicationPaused.store(false, std::memory_order_release);
		resumeDeferredPublication.store(false, std::memory_order_release);
	}

	void Runtime::PauseImmediateStagePublicationForTesting() noexcept
	{
		if (!pauseNextImmediateStagePublication.exchange(false, std::memory_order_acq_rel))
			return;
		deferredPublicationPaused.store(true, std::memory_order_release);
		while (!resumeDeferredPublication.load(std::memory_order_acquire))
			std::this_thread::yield();
		deferredPublicationPaused.store(false, std::memory_order_release);
		resumeDeferredPublication.store(false, std::memory_order_release);
	}

	void Runtime::PauseImmediateDispatchBeforeAppendForTesting() noexcept
	{
		if (!pauseNextImmediateDispatchAdmission.exchange(false, std::memory_order_acq_rel))
			return;
		deferredPublicationPaused.store(true, std::memory_order_release);
		while (!resumeDeferredPublication.load(std::memory_order_acquire))
			std::this_thread::yield();
		deferredPublicationPaused.store(false, std::memory_order_release);
		resumeDeferredPublication.store(false, std::memory_order_release);
	}

	void Runtime::PauseDeferredFinishCleanupForTesting() noexcept
	{
		if (!pauseNextDeferredFinishCleanup.exchange(false, std::memory_order_acq_rel))
			return;
		deferredPublicationPaused.store(true, std::memory_order_release);
		while (!resumeDeferredPublication.load(std::memory_order_acquire))
			std::this_thread::yield();
		deferredPublicationPaused.store(false, std::memory_order_release);
		resumeDeferredPublication.store(false, std::memory_order_release);
	}
#endif

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
		std::uintptr_t a_context, std::uint64_t a_captureGeneration,
		std::uint64_t a_contextObservationId,
		std::uint64_t a_recordingObservationId,
		CommandRecordingIncompleteReason a_reason) noexcept
	{
		std::scoped_lock lock(deferredContextMutex);
		const auto found = deferredContexts.find(a_context);
		if (found == deferredContexts.end() ||
			found->second.observationGeneration != a_captureGeneration ||
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
			const auto captureGeneration = collector.ActiveGeneration();
			if (!captureGeneration)
				return {};
			return {
				.kind = DeviceContextKind::kImmediate,
				.captureGeneration = captureGeneration,
				.observationId = EnsureImmediateContextObservation(captureGeneration),
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
			.captureGeneration = state.observationGeneration,
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
			const auto observed = ObserveBoundStage(a_stage, a_d3dObject, context.captureGeneration);
#if defined(CSX_RENDER_MAP_TESTING)
			PauseDeferredPublicationBeforeAppendForTesting();
#endif
			std::scoped_lock lock(deferredContextMutex);
			const auto found = deferredContexts.find(a_context);
			if (found == deferredContexts.end() ||
				collector.ActiveGeneration() != context.captureGeneration ||
				found->second.observationGeneration != context.captureGeneration ||
				found->second.observationId != context.observationId ||
				found->second.recordingObservationId != context.recordingObservationId ||
				(a_d3dObject != 0 && observed.sessionGeneration != context.captureGeneration))
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
		switch (a_stage) {
		case ShaderStage::kVertex:
		case ShaderStage::kPixel:
		case ShaderStage::kCompute:
			SetImmediateBoundStage(a_stage, a_d3dObject);
			break;
		default:
			return;
		}
		const auto captureGeneration = collector.ActiveGeneration();
		if (!collector.IsCapturing() || !captureGeneration || EnsureImmediateContextObservation(captureGeneration) == 0)
			return;
		NextCommandStreamSequence();
		PublishBoundStageObservation(a_stage, a_d3dObject, ObserveBoundStage(a_stage, a_d3dObject, captureGeneration));
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
		const auto captureGeneration = context.captureGeneration;
		std::uint64_t recordingObservationId = 0;
		std::uint64_t recordingIncompleteReasons = 0;
		std::uint64_t commandSequence = 0;
		{
			std::scoped_lock lock(deferredContextMutex);
			const auto found = deferredContexts.find(a_context);
			if (found == deferredContexts.end() ||
				collector.ActiveGeneration() != captureGeneration ||
				found->second.observationGeneration != captureGeneration ||
				found->second.observationId != context.observationId ||
				found->second.recordingObservationId != context.recordingObservationId)
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
#if defined(CSX_RENDER_MAP_TESTING)
			PauseCommandListAdmissionForTesting();
#endif
			try {
				std::scoped_lock lock(commandListMutex);
				if (collector.ActiveGeneration() != captureGeneration)
					return;
				if (commandLists.contains(a_commandList) ||
					commandLists.size() < kMaximumTrackedCommandLists) {
#if defined(CSX_RENDER_MAP_TESTING)
					if (!commandLists.contains(a_commandList) &&
						failNextCommandListCatalogueAdmission.exchange(false, std::memory_order_acq_rel)) {
						throw std::bad_alloc();
					}
#endif
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
			} catch (...) {
				// The real FinishCommandList call already succeeded. Preserve its
				// result and advance the recording epoch with incomplete evidence.
				commandListObservationId = 0;
				recordingIncompleteReasons |= static_cast<std::uint64_t>(
					CommandRecordingIncompleteReason::kEventNotRecorded);
				sourceRecordingComplete = false;
			}
		} else if (a_result >= 0) {
			recordingIncompleteReasons |= static_cast<std::uint64_t>(
				CommandRecordingIncompleteReason::kDeclarationUnavailable);
			sourceRecordingComplete = false;
		}

#if defined(CSX_RENDER_MAP_TESTING)
		PauseDeferredPublicationBeforeAppendForTesting();
#endif
		collector.RecordForGeneration(
			EventKind::kFinishCommandList,
			FinishCommandListPayload(
				recordingObservationId, commandListObservationId,
				a_result >= 0 ? a_commandList : 0,
				a_restoreDeferredContextState, a_result,
				sourceRecordingComplete, recordingIncompleteReasons),
			context.observationId, captureGeneration, commandSequence,
			0, 0, 0, recordingObservationId, true);

#if defined(CSX_RENDER_MAP_TESTING)
		PauseDeferredFinishCleanupForTesting();
#endif
		{
			std::scoped_lock lock(deferredContextMutex);
			const auto found = deferredContexts.find(a_context);
			if (found == deferredContexts.end() ||
				collector.ActiveGeneration() != captureGeneration ||
				found->second.observationGeneration != captureGeneration ||
				found->second.observationId != context.observationId ||
				found->second.recordingObservationId != recordingObservationId)
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
		if (collector.IsCapturing()) {
			const std::lock_guard lock(transferVersionMutex);
			transferVersions.Invalidate();
		}
		const auto isImmediateContext =
			a_context != 0 && a_context == immediateContext.load(std::memory_order_acquire);
		if (isImmediateContext && !a_restoreContextState)
			ResetImmediatePipelineState();
		if (!collector.IsCapturing() || !isImmediateContext || a_commandList == 0) {
			return;
		}
		const auto captureGeneration = collector.ActiveGeneration();
		const auto contextObservationId = EnsureImmediateContextObservation(captureGeneration);
		if (captureGeneration == 0 || contextObservationId == 0)
			return;
		const auto commandSequence = NextCommandStreamSequence();
		std::uint64_t commandListObservationId = 0;
		std::uint64_t sourceRecordingObservationId = 0;
#if defined(CSX_RENDER_MAP_TESTING)
		PauseCommandListAdmissionForTesting();
#endif
		try {
			std::scoped_lock lock(commandListMutex);
			if (collector.ActiveGeneration() != captureGeneration)
				return;
			if (!commandLists.contains(a_commandList) &&
				commandLists.size() >= kMaximumTrackedCommandLists) {
				return;
			}
#if defined(CSX_RENDER_MAP_TESTING)
			if (!commandLists.contains(a_commandList) &&
				failNextCommandListCatalogueAdmission.exchange(false, std::memory_order_acq_rel)) {
				throw std::bad_alloc();
			}
#endif
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
		} catch (...) {
			// ExecuteCommandList has already run. Missing diagnostic catalogue
			// storage must not escape this post-call hook boundary.
			return;
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
		const auto contextObservationId = EnsureImmediateContextObservation(captureGeneration);
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
			const auto observation = ObserveResourceView(input, contextObservationId, commandStreamSequence, captureGeneration);
			if (observation.observationId == 0)
				identityComplete = false;
			binding.renderTargetObservationIds[index] = observation.observationId;
		}

		if (a_depthTarget && a_depthTarget->view.d3dObject != 0) {
			auto input = *a_depthTarget;
			input.view.kind = TargetViewKind::kDepthTarget;
			const auto observation = ObserveResourceView(input, contextObservationId, commandStreamSequence, captureGeneration);
			if (observation.observationId == 0)
				identityComplete = false;
			binding.depthTargetObservationId = observation.observationId;
		}
		if (!identityComplete) {
			boundTargetBindingObservationId.store(0, std::memory_order_release);
			return;
		}

		const auto bindingObservation = collector.ObserveTargetBinding(binding, captureGeneration);
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

	std::uint64_t Runtime::ClaimImmediateStateSeed(std::atomic_uint64_t& a_seed,
		std::uintptr_t a_context, std::uint64_t a_expectedCaptureGeneration) noexcept
	{
		const auto generation = a_expectedCaptureGeneration != 0 ?
		                            a_expectedCaptureGeneration :
		                            collector.ActiveGeneration();
		if (generation == 0 || collector.ActiveGeneration() != generation || !collector.IsCapturing() ||
			a_context == 0 || a_context != immediateContext.load(std::memory_order_acquire) ||
			a_seed.load(std::memory_order_acquire) == generation)
			return 0;
#if defined(CSX_RENDER_MAP_TESTING)
		if (a_expectedCaptureGeneration != 0)
			PauseProducerPublicationForTesting();
#endif
		std::scoped_lock lifecycleLock(captureLifecycleMutex);
		if (collector.ActiveGeneration() != generation || !collector.IsCapturing() ||
			a_context != immediateContext.load(std::memory_order_acquire))
			return 0;
		return a_seed.exchange(generation, std::memory_order_acq_rel) == generation ? 0 : generation;
	}

	std::uint64_t Runtime::ClaimRenderTargetStateSeed(std::uintptr_t a_context,
		std::uint64_t a_expectedCaptureGeneration) noexcept
	{
		return ClaimImmediateStateSeed(targetStateObservationGeneration, a_context, a_expectedCaptureGeneration);
	}

	ResourceObservationResult Runtime::ObserveResource(
		const ResourceObservationInput& a_input,
		std::uint64_t a_contextObservationId,
		std::uint64_t a_commandStreamSequence, std::uint64_t a_expectedCaptureGeneration) noexcept
	{
		if (a_input.d3dObject == 0)
			return {};
		const auto observation = collector.ObserveResource(a_input, a_expectedCaptureGeneration);
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
		std::uint64_t a_commandStreamSequence, std::uint64_t a_expectedCaptureGeneration) noexcept
	{
		if (a_input.view.d3dObject == 0)
			return {};
		const auto generation = a_expectedCaptureGeneration != 0 ? a_expectedCaptureGeneration : collector.ActiveGeneration();
		if (!generation)
			return {};
		auto view = a_input.view;
		const auto resource = ObserveResource(a_input.resource, a_contextObservationId, a_commandStreamSequence, generation);
		view.resourceObservationId = resource.observationId;
		const auto observation = collector.ObserveTargetView(view, generation);
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
		const auto contextObservationId = EnsureImmediateContextObservation(captureGeneration);
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
				for (std::size_t resetKind = 0; resetKind < effectiveResourceViews.size(); ++resetKind) {
					for (std::size_t resetStage = 0;
						resetStage < effectiveResourceViews[resetKind].size(); ++resetStage) {
						effectiveResourceViews[resetKind][resetStage].fill(0);
						resourceViewSlotsNeedingObservation[resetKind][resetStage].reset();
					}
				}
				resourceViewStateGeneration = captureGeneration;
				resourceViewStateResetPending.store(false, std::memory_order_release);
			} else if (resourceViewStateResetPending.exchange(false, std::memory_order_acq_rel)) {
				ApplyEffectiveResourceViewResetLocked();
			}
			auto& state = effectiveResourceViews[kindIndex][stageValue - 1];
			auto& needsObservation = resourceViewSlotsNeedingObservation[kindIndex][stageValue - 1];
			for (std::uint32_t index = 0; index < count; ++index) {
				const auto slot = a_startSlot + index;
				const auto pointer = a_views ? a_views[index].view.d3dObject : 0;
				if (needsObservation.test(slot) || state[slot] != pointer) {
					state[slot] = pointer;
					needsObservation.reset(slot);
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
				const auto observation = ObserveResourceView(input, contextObservationId, commandStreamSequence, captureGeneration);
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

	std::uint64_t Runtime::ClaimResourceViewStateSeed(std::uintptr_t a_context,
		std::uint64_t a_expectedCaptureGeneration) noexcept
	{
		return ClaimImmediateStateSeed(resourceViewStateObservationGeneration, a_context, a_expectedCaptureGeneration);
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
		const auto generation = collector.ActiveGeneration();
		if (!generation)
			return;
		if (IsInsidePostProcessing()) {
			BeginTransferOperation(a_context, false, 0x100u + static_cast<std::uint32_t>(a_operation), {});
			RecordTransferResourceAccess(a_context, { .resource = a_source }, ResourceStage::kPixel, a_sourceSubresource, false);
			RecordTransferResourceAccess(a_context, { .resource = a_destination }, ResourceStage::kOutputMerger, a_destinationSubresource, true);
		} else {
			const std::lock_guard lock(transferVersionMutex);
			transferVersions.Invalidate();
		}

		const auto contextObservationId = EnsureImmediateContextObservation(generation);
		if (contextObservationId == 0)
			return;
		const auto commandStreamSequence = NextCommandStreamSequence();
		const auto source = ObserveResource(a_source, contextObservationId, commandStreamSequence, generation);
#if defined(CSX_RENDER_MAP_TESTING)
		PauseProducerPublicationForTesting();
#endif
		const auto destination = ObserveResource(a_destination, contextObservationId, commandStreamSequence, generation);
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
		const auto contextObservationId = EnsureImmediateContextObservation(a_expectedCaptureGeneration);
		if (contextObservationId == 0)
			return;
		const auto commandStreamSequence = NextCommandStreamSequence();
		const auto resource = ObserveResource(a_resource, contextObservationId, commandStreamSequence, a_expectedCaptureGeneration);
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
		if (collector.IsCapturing() && !IsInsidePostProcessing()) {
			const std::lock_guard lock(transferVersionMutex);
			transferVersions.Invalidate();
		}
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
		const auto contextObservationId = EnsureImmediateContextObservation(a_expectedCaptureGeneration);
		if (contextObservationId == 0)
			return;
		const auto commandStreamSequence = NextCommandStreamSequence();
		const auto resource = ObserveResource(a_resource, contextObservationId, commandStreamSequence, a_expectedCaptureGeneration);
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
		const auto captureGeneration = collector.ActiveGeneration();
		if (!captureGeneration)
			return;
#if defined(CSX_RENDER_MAP_TESTING)
		PauseProducerPublicationForTesting();
#endif
		collector.RecordForGeneration(
			EventKind::kVisibilityCandidate,
			VisibilityCandidatePayload(a_object, a_objectIndex, a_producerFrame), 0, captureGeneration);
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
		const auto captureGeneration = collector.ActiveGeneration();
		if (!captureGeneration)
			return 0;
		const auto contextObservationId = EnsureImmediateContextObservation(captureGeneration);
		if (contextObservationId == 0)
			return 0;
		const auto commandStreamSequence = NextCommandStreamSequence();
		const auto resource = ObserveResource(
			a_version.resource, contextObservationId, commandStreamSequence, captureGeneration);
#if defined(CSX_RENDER_MAP_TESTING)
		PauseProducerPublicationForTesting();
#endif
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
			view, contextObservationId, commandStreamSequence, captureGeneration);
		const auto recorded = collector.RecordForGeneration(
			EventKind::kVisibilityResultReady,
			VisibilityResultPayload(
				versionObservationId, viewObservation.observationId,
				a_objectCount, a_version.producerFrame),
			contextObservationId,
			resource.sessionGeneration,
			commandStreamSequence);
		return recorded == RecordResult::kRecorded ? versionObservationId : 0;
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
		const auto contextObservationId = EnsureImmediateContextObservation(generation);
		if (generation == 0 || contextObservationId == 0)
			return 0;
		const auto commandStreamSequence = NextCommandStreamSequence();
		const auto requested = ObserveResourceView(
			a_submission.requestedView, contextObservationId, commandStreamSequence, generation);
		const auto effective = ObserveResourceView(
			a_submission.effectiveView, contextObservationId, commandStreamSequence, generation);
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

#if defined(CSX_RENDER_MAP_TESTING)
		PauseProducerPublicationForTesting();
#endif
		collector.RecordForGeneration(
			EventKind::kCullDecision,
			CullDecisionPayload(
				a_resourceVersionObservationId, a_objectIndex, a_producerVisible,
				a_totalDraws, a_lightingDraws, a_distantTreeDraws, a_grassDraws,
				a_producerFrame),
			0, a_captureGeneration);
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
		std::uint64_t a_compositorCycle, std::uint64_t a_publicationGeneration) noexcept
	{
		if (!collector.IsCapturing() || a_resource.d3dObject == 0)
			return;
		const auto captureGeneration = collector.ActiveGeneration();
		if (!captureGeneration)
			return;
		const auto resource = ObserveResource(a_resource, 0, 0, captureGeneration);
#if defined(CSX_RENDER_MAP_TESTING)
		PauseProducerPublicationForTesting();
#endif
		if (resource.observationId == 0)
			return;
		const auto previousFrame = collector.GetThreadFrameContext();
		auto frame = previousFrame;
		frame.eye = a_eye;
		frame.eyeMask = a_eyeMask;
		collector.SetThreadFrameContext(frame);
		TransferVersions::Version version;
		{
			const std::lock_guard lock(transferVersionMutex);
			if (collector.ActiveGeneration() == captureGeneration)
				version = transferVersions.Read(captureGeneration, resource.observationId, frame.cpuFrame);
		}
		const auto eyeRecorded = collector.RecordForGeneration(
			EventKind::kEyeSubmitted,
			EyeSubmissionPayload(
				resource.observationId, a_eye, a_eyeMask,
				a_uMin, a_vMin, a_uMax, a_vMax, a_submitFlags, a_compositorCycle),
			0,
			resource.sessionGeneration);
		auto publicationRecorded = RecordResult::kFiltered;
		if (a_publicationGeneration)
			publicationRecorded = collector.RecordForGeneration(EventKind::kEyeSubmitted,
				{ .schema = static_cast<std::uint16_t>(PayloadSchema::kEyePublication), .words = {
																							resource.observationId,
																							version.observation,
																							version.command,
																							a_publicationGeneration,
																							a_compositorCycle,
																							static_cast<std::uint64_t>(a_eye),
																							a_eyeMask,
																						} },
				0, resource.sessionGeneration);
		if (eyeRecorded == RecordResult::kRecorded && publicationRecorded == RecordResult::kRecorded)
			collector.AcceptWindowEye(a_eye, frame.cpuFrame, a_compositorCycle, a_publicationGeneration, captureGeneration);
		collector.SetThreadFrameContext(previousFrame);
	}

	std::uint64_t Runtime::EnsureImmediateContextObservation(std::uint64_t a_expectedCaptureGeneration) noexcept
	{
		const auto captureGeneration = collector.ActiveGeneration();
		const auto context = immediateContext.load(std::memory_order_acquire);
		if (captureGeneration == 0 || context == 0 ||
			(a_expectedCaptureGeneration != 0 && captureGeneration != a_expectedCaptureGeneration))
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

	void Runtime::ResetImmediateStageObservations(bool a_clearBindings) noexcept
	{
		std::scoped_lock lock(immediateStageObservationMutex);
		immediateStageObservationRevision.fetch_add(1, std::memory_order_acq_rel);
		if (a_clearBindings) {
			boundVertexShader.store(0, std::memory_order_release);
			boundPixelShader.store(0, std::memory_order_release);
			boundComputeShader.store(0, std::memory_order_release);
		}
		boundVertexShaderObservationId.store(0, std::memory_order_release);
		boundPixelShaderObservationId.store(0, std::memory_order_release);
		boundComputeShaderObservationId.store(0, std::memory_order_release);
		boundVertexShaderObservationGeneration.store(0, std::memory_order_release);
		boundPixelShaderObservationGeneration.store(0, std::memory_order_release);
		boundComputeShaderObservationGeneration.store(0, std::memory_order_release);
		immediateStageObservationRevision.fetch_add(1, std::memory_order_release);
	}

	void Runtime::SetImmediateBoundStage(
		ShaderStage a_stage,
		std::uintptr_t a_d3dObject) noexcept
	{
		std::atomic_uintptr_t* boundShader = nullptr;
		std::atomic_uint64_t* boundObservation = nullptr;
		std::atomic_uint64_t* boundGeneration = nullptr;
		switch (a_stage) {
		case ShaderStage::kVertex:
			boundShader = &boundVertexShader;
			boundObservation = &boundVertexShaderObservationId;
			boundGeneration = &boundVertexShaderObservationGeneration;
			break;
		case ShaderStage::kPixel:
			boundShader = &boundPixelShader;
			boundObservation = &boundPixelShaderObservationId;
			boundGeneration = &boundPixelShaderObservationGeneration;
			break;
		case ShaderStage::kCompute:
			boundShader = &boundComputeShader;
			boundObservation = &boundComputeShaderObservationId;
			boundGeneration = &boundComputeShaderObservationGeneration;
			break;
		}
		if (!boundShader || !boundObservation || !boundGeneration)
			return;

		std::scoped_lock lock(immediateStageObservationMutex);
		immediateStageObservationRevision.fetch_add(1, std::memory_order_acq_rel);
		boundShader->store(a_d3dObject, std::memory_order_release);
		boundObservation->store(0, std::memory_order_release);
		boundGeneration->store(0, std::memory_order_release);
		immediateStageObservationRevision.fetch_add(1, std::memory_order_release);
	}

	Runtime::ImmediateStageObservation Runtime::ReadImmediateStageObservation(
		ShaderStage a_stage) const noexcept
	{
		const std::atomic_uintptr_t* boundShader = nullptr;
		const std::atomic_uint64_t* boundObservation = nullptr;
		const std::atomic_uint64_t* boundGeneration = nullptr;
		switch (a_stage) {
		case ShaderStage::kVertex:
			boundShader = &boundVertexShader;
			boundObservation = &boundVertexShaderObservationId;
			boundGeneration = &boundVertexShaderObservationGeneration;
			break;
		case ShaderStage::kPixel:
			boundShader = &boundPixelShader;
			boundObservation = &boundPixelShaderObservationId;
			boundGeneration = &boundPixelShaderObservationGeneration;
			break;
		case ShaderStage::kCompute:
			boundShader = &boundComputeShader;
			boundObservation = &boundComputeShaderObservationId;
			boundGeneration = &boundComputeShaderObservationGeneration;
			break;
		}
		if (!boundShader || !boundObservation || !boundGeneration)
			return {};

		for (std::uint32_t attempt = 0; attempt < 8; ++attempt) {
			const auto before = immediateStageObservationRevision.load(std::memory_order_acquire);
			if ((before & 1) != 0)
				continue;
			const ImmediateStageObservation observation{
				.d3dObject = boundShader->load(std::memory_order_acquire),
				.observationId = boundObservation->load(std::memory_order_acquire),
				.captureGeneration = boundGeneration->load(std::memory_order_acquire),
			};
			const auto after = immediateStageObservationRevision.load(std::memory_order_acquire);
			if (before == after && (after & 1) == 0)
				return observation;
		}
		return {};
	}

	Runtime::ImmediateStageObservation Runtime::EnsureBoundStageObservation(ShaderStage a_stage, std::uint64_t a_expectedCaptureGeneration) noexcept
	{
		const auto existing = ReadImmediateStageObservation(a_stage);
		const auto activeGeneration = a_expectedCaptureGeneration;
		if (!activeGeneration || collector.ActiveGeneration() != activeGeneration)
			return {};
		if (existing.observationId != 0 && existing.captureGeneration == activeGeneration)
			return existing;
		if (existing.d3dObject == 0)
			return existing;

		PublishBoundStageObservation(
			a_stage, existing.d3dObject,
			ObserveBoundStage(a_stage, existing.d3dObject, activeGeneration));
		const auto published = ReadImmediateStageObservation(a_stage);
		return published.observationId != 0 &&
		               published.captureGeneration == activeGeneration && collector.ActiveGeneration() == activeGeneration ?
		           published :
		           ImmediateStageObservation{ .d3dObject = published.d3dObject };
	}

	std::optional<Runtime::PersistentStageShaderIdentity> Runtime::FindCreatedStageShader(
		ShaderStage a_stage,
		std::uintptr_t a_d3dObject) const noexcept
	{
		try {
			const auto catalogue = persistentStageShaderCatalogue.load(std::memory_order_acquire);
			if (!catalogue)
				return std::nullopt;
			std::shared_lock lock(catalogue->mutex);
			const auto found = catalogue->records.find({ a_stage, a_d3dObject });
			if (found != catalogue->records.end())
				return found->second;
		} catch (...) {
			RecordShaderMetadataFailure();
		}
		return std::nullopt;
	}

	std::shared_ptr<Runtime::PersistentStageShaderCatalogue> Runtime::EnsurePersistentStageShaderCatalogue() const
	{
		auto catalogue = persistentStageShaderCatalogue.load(std::memory_order_acquire);
		if (!catalogue) {
			auto created = std::make_shared<PersistentStageShaderCatalogue>();
			if (persistentStageShaderCatalogue.compare_exchange_strong(catalogue, created,
					std::memory_order_acq_rel, std::memory_order_acquire))
				catalogue = std::move(created);
		}
		return catalogue;
	}

	StageShaderObservationResult Runtime::ObserveBoundStage(
		ShaderStage a_stage,
		std::uintptr_t a_d3dObject, std::uint64_t a_expectedCaptureGeneration) noexcept
	{
		if (a_d3dObject == 0 || !collector.IsCapturing())
			return {};

		const StageShaderObservationInput input{
			.stage = a_stage,
			.d3dObject = a_d3dObject,
		};
		return ObserveStageShaderWithPersistent(input, a_expectedCaptureGeneration);
	}

	void Runtime::PublishBoundStageObservation(
		ShaderStage a_stage,
		std::uintptr_t a_d3dObject,
		const StageShaderObservationResult& a_observation) noexcept
	{
		if (a_d3dObject == 0 || a_observation.observationId == 0)
			return;

#if defined(CSX_RENDER_MAP_TESTING)
		PauseImmediateStagePublicationForTesting();
#endif

		std::atomic_uintptr_t* boundShader = nullptr;
		std::atomic_uint64_t* boundObservation = nullptr;
		std::atomic_uint64_t* boundGeneration = nullptr;
		switch (a_stage) {
		case ShaderStage::kVertex:
			boundShader = &boundVertexShader;
			boundObservation = &boundVertexShaderObservationId;
			boundGeneration = &boundVertexShaderObservationGeneration;
			break;
		case ShaderStage::kPixel:
			boundShader = &boundPixelShader;
			boundObservation = &boundPixelShaderObservationId;
			boundGeneration = &boundPixelShaderObservationGeneration;
			break;
		case ShaderStage::kCompute:
			boundShader = &boundComputeShader;
			boundObservation = &boundComputeShaderObservationId;
			boundGeneration = &boundComputeShaderObservationGeneration;
			break;
		}
		if (!boundShader || !boundObservation || !boundGeneration)
			return;

		std::scoped_lock lock(immediateStageObservationMutex);
		const auto activeGeneration = collector.ActiveGeneration();
		if (activeGeneration == 0 ||
			a_observation.sessionGeneration != activeGeneration ||
			boundShader->load(std::memory_order_acquire) != a_d3dObject)
			return;
		immediateStageObservationRevision.fetch_add(1, std::memory_order_acq_rel);
		boundObservation->store(a_observation.observationId, std::memory_order_release);
		boundGeneration->store(a_observation.sessionGeneration, std::memory_order_release);
		immediateStageObservationRevision.fetch_add(1, std::memory_order_release);
	}

	void Runtime::RecordDraw(
		std::uintptr_t a_context,
		DrawOperation a_operation,
		std::uint64_t a_argument0,
		std::uint64_t a_argument1,
		std::uint64_t a_argument2,
		std::uint64_t a_argument3) noexcept
	{
		if (collector.IsCapturing() && !IsInsidePostProcessing()) {
			const std::lock_guard lock(transferVersionMutex);
			transferVersions.Invalidate();
		}
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
				if (found == deferredContexts.end() ||
					found->second.observationGeneration != context.captureGeneration ||
					found->second.observationId != context.observationId)
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
			if (!collector.IsExecutionAllowedByGeometryScope()) {
				collector.CountFiltered();
				MarkDeferredRecordingIncomplete(
					a_context, context.captureGeneration, context.observationId,
					recordingObservationId,
					CommandRecordingIncompleteReason::kEventNotRecorded);
				return;
			}
#if defined(CSX_RENDER_MAP_TESTING)
			PauseDeferredPublicationBeforeAppendForTesting();
#endif
			const auto recordResult = collector.RecordForGeneration(
				EventKind::kDraw,
				DrawCallPayload(
					a_context, a_operation, vertexObservationId, pixelObservationId,
					a_argument0, a_argument1, a_argument2, a_argument3),
				context.observationId, context.captureGeneration, commandSequence,
				0, 0, 0, recordingObservationId, true);
			if (recordResult != RecordResult::kRecorded) {
				MarkDeferredRecordingIncomplete(
					a_context, context.captureGeneration, context.observationId,
					recordingObservationId,
					CommandRecordingIncompleteReason::kEventNotRecorded);
			}
			return;
		}
		const auto captureGeneration = collector.ActiveGeneration();
		if (captureGeneration == 0)
			return;
		const auto commandStreamSequence = NextCommandStreamSequence();
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
		const auto contextObservationId = EnsureImmediateContextObservation(captureGeneration);
		if (contextObservationId == 0 ||
			immediateContextObservationGeneration.load(std::memory_order_acquire) != captureGeneration)
			return;
		const auto vertexObservation = EnsureBoundStageObservation(ShaderStage::kVertex, captureGeneration);
		const auto pixelObservation = EnsureBoundStageObservation(ShaderStage::kPixel, captureGeneration);
		const auto vertexObservationId = vertexObservation.captureGeneration == captureGeneration ?
		                                     vertexObservation.observationId :
		                                     0;
		const auto pixelObservationId = pixelObservation.captureGeneration == captureGeneration ?
		                                    pixelObservation.observationId :
		                                    0;
		if (collector.ActiveGeneration() != captureGeneration)
			return;
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
		if (collector.IsCapturing() && !IsInsidePostProcessing()) {
			const std::lock_guard lock(transferVersionMutex);
			transferVersions.Invalidate();
		}
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
				if (found == deferredContexts.end() ||
					found->second.observationGeneration != context.captureGeneration ||
					found->second.observationId != context.observationId)
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
			if (!collector.IsExecutionAllowedByGeometryScope()) {
				collector.CountFiltered();
				MarkDeferredRecordingIncomplete(
					a_context, context.captureGeneration, context.observationId,
					recordingObservationId,
					CommandRecordingIncompleteReason::kEventNotRecorded);
				return;
			}
#if defined(CSX_RENDER_MAP_TESTING)
			PauseDeferredPublicationBeforeAppendForTesting();
#endif
			const auto recordResult = collector.RecordForGeneration(
				EventKind::kDispatch,
				DispatchCallPayload(
					a_context, a_operation, computeObservationId,
					a_argument0, a_argument1, a_argument2, a_argument3),
				context.observationId, context.captureGeneration, commandSequence,
				0, 0, 0, recordingObservationId, true);
			if (recordResult != RecordResult::kRecorded) {
				MarkDeferredRecordingIncomplete(
					a_context, context.captureGeneration, context.observationId,
					recordingObservationId,
					CommandRecordingIncompleteReason::kEventNotRecorded);
			}
			return;
		}
		const auto captureGeneration = collector.ActiveGeneration();
		if (captureGeneration == 0)
			return;
		const auto commandStreamSequence = NextCommandStreamSequence();
		if (!collector.IsExecutionAllowedByGeometryScope()) {
			collector.CountFiltered();
			return;
		}
		const auto contextObservationId = EnsureImmediateContextObservation(captureGeneration);
		if (contextObservationId == 0 ||
			immediateContextObservationGeneration.load(std::memory_order_acquire) != captureGeneration)
			return;
		const auto computeObservation = EnsureBoundStageObservation(ShaderStage::kCompute, captureGeneration);
#if defined(CSX_RENDER_MAP_TESTING)
		PauseImmediateDispatchBeforeAppendForTesting();
#endif
		if (collector.ActiveGeneration() != captureGeneration)
			return;
		const auto computeObservationId = computeObservation.captureGeneration == captureGeneration ?
		                                      computeObservation.observationId :
		                                      0;
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
		const auto captureGeneration = collector.ActiveGeneration();
		if (!captureGeneration)
			return {};
		if (!collector.IsGeometryShaderTypeSelected(a_boundary.shaderType, captureGeneration)) {
			collector.CountFiltered(2, captureGeneration);
			return {};
		}

		const auto sceneObject = collector.ObserveSceneObject(a_boundary.sceneObject, captureGeneration);
#if defined(CSX_RENDER_MAP_TESTING)
		PauseProducerPublicationForTesting();
#endif
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
		const auto geometry = collector.ObserveGeometry(geometryInput, captureGeneration);
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
				binding.resourceObservationId = ObserveResource(binding.resource, 0, 0, captureGeneration).observationId;
			}
		}
		const auto material = collector.ObserveMaterialState(materialInput, captureGeneration);
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
