#pragma once

#include "RenderMap/Collector.h"

#include <cstdint>
#include <mutex>
#include <optional>

namespace CSX::RenderMap
{
	enum class PayloadSchema : std::uint16_t
	{
		kRenderPassBoundary = 1,
		kTechniqueBoundary = 2,
		kGeometryBoundary = 3,
		kShaderObservation = 4,
		kStageShaderObservation = 5,
		kTechniqueResolution = 6,
		kDrawCall = 7,
		kDispatchCall = 8,
		kDeviceContextObservation = 9,
		kTargetViewObservation = 10,
		kTargetBinding = 11,
		kResourceObservation = 12,
		kResourceViewBinding = 13,
		kResourceFlow = 14,
	};

	enum class ResourceFlowOperation : std::uint8_t
	{
		kCopyResource = 1,
		kCopySubresourceRegion = 2,
		kResolveSubresource = 3,
		kUpdateSubresource = 4,
		kCopyStructureCount = 5,
		kClearRenderTarget = 6,
		kClearUnorderedAccess = 7,
		kClearDepthStencil = 8,
		kGenerateMips = 9,
	};

	struct ResourceViewInput
	{
		ResourceObservationInput resource;
		TargetViewObservationInput view;
	};

	enum class DrawOperation : std::uint8_t
	{
		kDraw,
		kDrawIndexed,
		kDrawInstanced,
		kDrawIndexedInstanced,
		kDrawAuto,
		kDrawInstancedIndirect,
		kDrawIndexedInstancedIndirect,
	};

	enum class DispatchOperation : std::uint8_t
	{
		kDispatch,
		kDispatchIndirect,
	};

	enum class ShaderSelectionRoute : std::uint8_t
	{
		kUnknown,
		kEngine,
		kCSXCache,
		kCSXFallback,
		kSkipped,
		kMissing,
	};

	struct RenderPassBoundary
	{
		std::uintptr_t renderPass{ 0 };
		std::uintptr_t geometry{ 0 };
		std::uint32_t technique{ 0 };
		std::uint32_t passEnum{ 0 };
		std::uint32_t renderFlags{ 0 };
		bool alphaTest{ false };
	};

	struct TechniqueBoundary
	{
		std::uintptr_t shader{ 0 };
		std::uint32_t shaderType{ 0 };
		std::uint32_t vertexDescriptor{ 0 };
		std::uint32_t pixelDescriptor{ 0 };
		std::uint32_t callerRva{ 0 };
		bool skipPixelShader{ false };
		std::string_view fxpFilename;
		std::string_view imageSpaceName;
		std::string_view definesSuffix;
	};

	struct GeometryBoundary
	{
		std::uintptr_t shader{ 0 };
		std::uintptr_t renderPass{ 0 };
		std::uintptr_t geometry{ 0 };
		std::uint32_t shaderType{ 0 };
		std::uint32_t passEnum{ 0 };
		std::uint32_t renderFlags{ 0 };
	};

	struct TechniqueStageSelection
	{
		ShaderSelectionRoute route{ ShaderSelectionRoute::kUnknown };
		StageShaderObservationInput shader;
	};

	struct TechniqueResolution
	{
		std::uint32_t inputVertexDescriptor{ 0 };
		std::uint32_t inputPixelDescriptor{ 0 };
		std::uint32_t resolvedVertexDescriptor{ 0 };
		std::uint32_t resolvedPixelDescriptor{ 0 };
		bool shaderFound{ false };
		bool skipPixelShader{ false };
		TechniqueStageSelection vertex;
		TechniqueStageSelection pixel;
	};

	class Runtime
	{
	public:
		StartResult StartCapture(const CollectorConfig& a_config);
		std::optional<CaptureSnapshot> StopCapture(StopReason a_reason = StopReason::kRequested);
		bool IsCapturing() const noexcept;

		void SetCpuFrame(std::uint64_t a_cpuFrame) noexcept;
		void SetFrameContext(const FrameContext& a_context) noexcept;

		Collector::ScopeGuard EnterRenderPass(const RenderPassBoundary& a_boundary) noexcept;
		Collector::ScopeGuard EnterTechnique(const TechniqueBoundary& a_boundary) noexcept;
		Collector::ScopeGuard EnterGeometry(const GeometryBoundary& a_boundary) noexcept;
		void RecordTechniqueResolution(const TechniqueResolution& a_resolution) noexcept;
		void SetImmediateContext(std::uintptr_t a_context) noexcept;
		void BindStage(
			std::uintptr_t a_context,
			ShaderStage a_stage,
			std::uintptr_t a_d3dObject) noexcept;
		void BindRenderTargets(
			std::uintptr_t a_context,
			std::uint32_t a_renderTargetCount,
			const std::uintptr_t* a_renderTargets,
			std::uintptr_t a_depthTarget,
			bool a_keepTargets = false) noexcept;
		void BindRenderTargetViews(
			std::uintptr_t a_context,
			std::uint32_t a_renderTargetCount,
			const ResourceViewInput* a_renderTargets,
			const ResourceViewInput* a_depthTarget,
			bool a_keepTargets = false) noexcept;
		void BindResourceViews(
			std::uintptr_t a_context,
			ResourceBindingKind a_bindingKind,
			ResourceStage a_stage,
			std::uint32_t a_startSlot,
			std::uint32_t a_viewCount,
			const ResourceViewInput* a_views,
			bool a_keepViews = false) noexcept;
		void RecordResourceFlow(
			std::uintptr_t a_context,
			ResourceFlowOperation a_operation,
			const ResourceObservationInput& a_source,
			const ResourceObservationInput& a_destination,
			std::uint32_t a_sourceSubresource = 0,
			std::uint32_t a_destinationSubresource = 0) noexcept;
		void RecordDraw(
			std::uintptr_t a_context,
			DrawOperation a_operation,
			std::uint64_t a_argument0 = 0,
			std::uint64_t a_argument1 = 0,
			std::uint64_t a_argument2 = 0,
			std::uint64_t a_argument3 = 0) noexcept;
		void RecordDispatch(
			std::uintptr_t a_context,
			DispatchOperation a_operation,
			std::uint64_t a_argument0 = 0,
			std::uint64_t a_argument1 = 0,
			std::uint64_t a_argument2 = 0,
			std::uint64_t a_argument3 = 0) noexcept;
		void RetireShaderObservation(std::uintptr_t a_shader) noexcept;

	private:
		std::uint64_t EnsureImmediateContextObservation() noexcept;
		std::uint64_t NextCommandStreamSequence() noexcept;
		StageShaderObservationResult ResolveBoundStage(
			ShaderStage a_stage,
			std::uintptr_t a_d3dObject) noexcept;
		TargetViewObservationResult ObserveResourceView(
			const ResourceViewInput& a_input,
			std::uint64_t a_contextObservationId,
			std::uint64_t a_commandStreamSequence) noexcept;
		ResourceObservationResult ObserveResource(
			const ResourceObservationInput& a_input,
			std::uint64_t a_contextObservationId,
			std::uint64_t a_commandStreamSequence) noexcept;

		Collector collector;
		std::atomic_uintptr_t immediateContext{ 0 };
		std::atomic_uintptr_t boundVertexShader{ 0 };
		std::atomic_uintptr_t boundPixelShader{ 0 };
		std::atomic_uintptr_t boundComputeShader{ 0 };
		std::atomic_uint64_t boundTargetBindingObservationId{ 0 };
		std::atomic_uint64_t immediateContextPointerGeneration{ 0 };
		std::atomic_uint64_t immediateContextObservationId{ 0 };
		std::atomic_uint64_t immediateContextObservationGeneration{ 0 };
		std::atomic_uint64_t immediateContextCommandSequence{ 0 };
		std::mutex immediateContextObservationMutex;
	};

	Runtime& GetRuntime() noexcept;
}
