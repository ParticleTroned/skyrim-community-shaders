#include "RenderMap/Runtime.h"

#include <algorithm>
#include <bit>

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

		thread_local PendingVisibilitySubmission pendingVisibilitySubmission;

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

		EventPayload GeometryPayload(const GeometryBoundary& a_boundary) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kGeometryBoundary),
				.words = {
					a_boundary.shader,
					a_boundary.renderPass,
					a_boundary.geometry,
					a_boundary.shaderType,
					a_boundary.passEnum,
					a_boundary.renderFlags,
				},
			};
		}

		EventPayload DeviceContextObservationPayload(
			std::uint64_t a_observationId,
			std::uintptr_t a_context,
			std::uint64_t a_pointerGeneration) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kDeviceContextObservation),
				.words = {
					a_observationId,
					a_context,
					a_pointerGeneration,
					1u,
				},
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
			std::uintptr_t a_context) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kTargetBinding),
				.words = { a_observation.observationId, a_context },
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
			std::uint32_t a_slot) noexcept
		{
			return {
				.schema = static_cast<std::uint16_t>(PayloadSchema::kResourceViewBinding),
				.words = {
					a_viewObservationId,
					static_cast<std::uint64_t>(a_bindingKind),
					static_cast<std::uint64_t>(a_stage),
					a_slot,
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

	StartResult Runtime::StartCapture(const CollectorConfig& a_config)
	{
		const auto result = collector.Start(a_config);
		if (result == StartResult::kStarted) {
			immediateContextObservationId.store(0, std::memory_order_release);
			immediateContextObservationGeneration.store(0, std::memory_order_release);
			immediateContextCommandSequence.store(0, std::memory_order_release);
			boundTargetBindingObservationId.store(0, std::memory_order_release);
			boundVertexShaderObservationId.store(0, std::memory_order_release);
			boundPixelShaderObservationId.store(0, std::memory_order_release);
			boundComputeShaderObservationId.store(0, std::memory_order_release);
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
			.definesSuffix = a_boundary.definesSuffix,
		});
		const auto captureGeneration = shaderObservation.sessionGeneration != 0 ?
			shaderObservation.sessionGeneration : collector.ActiveGeneration();
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

	void Runtime::RecordTechniqueResolution(const TechniqueResolution& a_resolution) noexcept
	{
		if (!collector.IsCapturing())
			return;

		const auto vertex = collector.ObserveStageShader(a_resolution.vertex.shader);
		const auto pixel = collector.ObserveStageShader(a_resolution.pixel.shader);
		const auto captureGeneration = vertex.sessionGeneration != 0 ? vertex.sessionGeneration :
			(pixel.sessionGeneration != 0 ? pixel.sessionGeneration : collector.ActiveGeneration());
		if (captureGeneration == 0)
			return;
		if (vertex.firstSeen) {
			collector.RecordForGeneration(
				EventKind::kStageShaderObserved,
				StageShaderObservationPayload(a_resolution.vertex.shader, vertex), 0, captureGeneration);
		}
		if (pixel.firstSeen) {
			collector.RecordForGeneration(
				EventKind::kStageShaderObserved,
				StageShaderObservationPayload(a_resolution.pixel.shader, pixel), 0, captureGeneration);
		}
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
	}

	void Runtime::BindStage(
		std::uintptr_t a_context,
		ShaderStage a_stage,
		std::uintptr_t a_d3dObject) noexcept
	{
		if (a_context == 0 || a_context != immediateContext.load(std::memory_order_acquire))
			return;
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
		bool a_keepTargets) noexcept
	{
		if (!collector.IsCapturing() || a_context == 0 ||
			a_context != immediateContext.load(std::memory_order_acquire)) {
			return;
		}
		const auto contextObservationId = EnsureImmediateContextObservation();
		if (contextObservationId == 0)
			return;
		const auto commandStreamSequence = NextCommandStreamSequence();
		if (a_keepTargets)
			return;

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
			TargetBindingPayload(bindingObservation, a_context),
			contextObservationId,
			bindingObservation.sessionGeneration,
			commandStreamSequence,
			bindingObservation.observationId);
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
		bool a_keepViews) noexcept
	{
		if (!collector.IsCapturing() || a_context == 0 ||
			a_context != immediateContext.load(std::memory_order_acquire)) {
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
		for (std::uint32_t index = 0; index < count; ++index) {
			std::uint64_t viewObservationId = 0;
			std::uint64_t generation = collector.ActiveGeneration();
			if (a_views && a_views[index].view.d3dObject != 0) {
				auto input = a_views[index];
				input.view.kind = a_bindingKind == ResourceBindingKind::kShaderResource ?
					TargetViewKind::kShaderResource : TargetViewKind::kUnorderedAccess;
				const auto observation = ObserveResourceView(input, contextObservationId, commandStreamSequence);
				viewObservationId = observation.observationId;
				if (observation.sessionGeneration != 0)
					generation = observation.sessionGeneration;
			}
			collector.RecordForGeneration(
				EventKind::kResourceViewBind,
				ResourceViewBindingPayload(viewObservationId, a_bindingKind, a_stage, a_startSlot + index),
				contextObservationId,
				generation,
				commandStreamSequence);
		}
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
				immediateContextPointerGeneration.load(std::memory_order_acquire)),
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

	StageShaderObservationResult Runtime::ObserveBoundStage(
		ShaderStage a_stage,
		std::uintptr_t a_d3dObject) noexcept
	{
		if (a_d3dObject == 0)
			return {};
		auto observation = collector.FindStageShader(a_stage, a_d3dObject);
		if (observation.observationId != 0)
			return observation;

		const StageShaderObservationInput input{
			.stage = a_stage,
			.d3dObject = a_d3dObject,
		};
		observation = collector.ObserveStageShader(input);
		if (observation.firstSeen) {
			collector.RecordForGeneration(
				EventKind::kStageShaderObserved,
				StageShaderObservationPayload(input, observation), 0, observation.sessionGeneration);
		}
		return observation;
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
		if (!collector.IsCapturing() || a_context == 0 ||
			a_context != immediateContext.load(std::memory_order_acquire)) {
			return;
		}
		const auto contextObservationId = EnsureImmediateContextObservation();
		if (contextObservationId == 0)
			return;
		const auto commandStreamSequence = NextCommandStreamSequence();
		const auto vertexObservationId = boundVertexShaderObservationId.load(std::memory_order_acquire);
		const auto pixelObservationId = boundPixelShaderObservationId.load(std::memory_order_acquire);
		const auto captureGeneration = collector.ActiveGeneration();
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
			submissionObservationId);
	}

	void Runtime::RecordDispatch(
		std::uintptr_t a_context,
		DispatchOperation a_operation,
		std::uint64_t a_argument0,
		std::uint64_t a_argument1,
		std::uint64_t a_argument2,
		std::uint64_t a_argument3) noexcept
	{
		if (!collector.IsCapturing() || a_context == 0 ||
			a_context != immediateContext.load(std::memory_order_acquire)) {
			return;
		}
		const auto contextObservationId = EnsureImmediateContextObservation();
		if (contextObservationId == 0)
			return;
		const auto commandStreamSequence = NextCommandStreamSequence();
		const auto computeObservationId = boundComputeShaderObservationId.load(std::memory_order_acquire);
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
		if (!collector.IsCapturing())
			return {};
		const auto observationId = collector.AllocateObservationId();
		if (observationId == 0)
			return {};
		const auto payload = GeometryPayload(a_boundary);
		return collector.EnterScope(
			ScopeKind::kGeometry,
			observationId,
			EventKind::kGeometrySetupBegin,
			EventKind::kGeometrySetupEnd,
			payload,
			payload);
	}

	Runtime& GetRuntime() noexcept
	{
		static Runtime runtime;
		return runtime;
	}
}
