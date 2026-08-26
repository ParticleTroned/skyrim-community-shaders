#include "RenderMap/Runtime.h"

namespace CSX::RenderMap
{
	namespace
	{
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
	}

	StartResult Runtime::StartCapture(const CollectorConfig& a_config)
	{
		const auto result = collector.Start(a_config);
		if (result == StartResult::kStarted) {
			immediateContextObservationId.store(0, std::memory_order_release);
			immediateContextObservationGeneration.store(0, std::memory_order_release);
			immediateContextCommandSequence.store(0, std::memory_order_release);
		}
		return result;
	}

	std::optional<CaptureSnapshot> Runtime::StopCapture(StopReason a_reason)
	{
		return collector.Stop(a_reason);
	}

	bool Runtime::IsCapturing() const noexcept
	{
		return collector.IsCapturing();
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
			break;
		case ShaderStage::kPixel:
			boundPixelShader.store(a_d3dObject, std::memory_order_release);
			break;
		case ShaderStage::kCompute:
			boundComputeShader.store(a_d3dObject, std::memory_order_release);
			break;
		}
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

	StageShaderObservationResult Runtime::ResolveBoundStage(
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
		const auto vertex = ResolveBoundStage(
			ShaderStage::kVertex, boundVertexShader.load(std::memory_order_acquire));
		const auto pixel = ResolveBoundStage(
			ShaderStage::kPixel, boundPixelShader.load(std::memory_order_acquire));
		const auto captureGeneration = vertex.sessionGeneration != 0 ? vertex.sessionGeneration :
			(pixel.sessionGeneration != 0 ? pixel.sessionGeneration : collector.ActiveGeneration());
		collector.RecordForGeneration(
			EventKind::kDraw,
			DrawCallPayload(
				a_context, a_operation, vertex.observationId, pixel.observationId,
				a_argument0, a_argument1, a_argument2, a_argument3),
			contextObservationId, captureGeneration, commandStreamSequence);
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
		const auto compute = ResolveBoundStage(
			ShaderStage::kCompute, boundComputeShader.load(std::memory_order_acquire));
		const auto captureGeneration = compute.sessionGeneration != 0 ?
			compute.sessionGeneration : collector.ActiveGeneration();
		collector.RecordForGeneration(
			EventKind::kDispatch,
			DispatchCallPayload(
				a_context, a_operation, compute.observationId,
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
