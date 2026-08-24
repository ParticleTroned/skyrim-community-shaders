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
	}

	StartResult Runtime::StartCapture(const CollectorConfig& a_config)
	{
		return collector.Start(a_config);
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
