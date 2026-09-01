#pragma once

#include "RenderMap/Collector.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace CSX::RenderMap
{
	inline constexpr std::size_t kMaximumTrackedDeferredContexts = 256;
	inline constexpr std::size_t kMaximumTrackedCommandLists = 8192;

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
		kResourceVersion = 15,
		kVisibilityCandidate = 16,
		kVisibilityResult = 17,
		kVisibilitySubmission = 18,
		kEyeSubmission = 19,
		kCullDecision = 20,
		kSceneObjectObservation = 21,
		kGeometryObservation = 22,
		kMaterialStateObservation = 23,
		kGeometryBoundaryV2 = 24,
		kResourceViewStateObserved = 25,
		kResourceCpuAccess = 26,
		kCommandRecordingObservation = 27,
		kCommandListObservation = 28,
		kFinishCommandList = 29,
		kExecuteCommandList = 30,
	};

	enum class DeviceContextKind : std::uint8_t
	{
		kUnknown = 0,
		kImmediate = 1,
		kDeferred = 2,
	};

	enum class ContextCreationEvidence : std::uint8_t
	{
		kUnknown = 0,
		kInitialImmediate = 1,
		kCreateDeferredContext = 2,
		kFirstSeen = 3,
	};

	enum class CommandRecordingIncompleteReason : std::uint64_t
	{
		kPartialAtCaptureStart = 1ull << 0,
		kDeclarationUnavailable = 1ull << 1,
		kEventNotRecorded = 1ull << 2,
		kHookCoverageUnqualified = 1ull << 3,
	};

	enum class ResourceCpuAccessPhase : std::uint8_t
	{
		kMap = 1,
		kUnmap = 2,
	};

	enum class ResourceReadinessDomain : std::uint8_t
	{
		kUnknown = 0,
		kSameImmediateContextOrder = 1,
	};

	enum class TargetBindingSource : std::uint8_t
	{
		kObservedCall = 1,
		kCaptureStateSnapshot = 2,
		kPostCallQuery = 3,
	};

	enum class ResourceBindingSource : std::uint8_t
	{
		kRequestedCall = 1,
		kPostCallQuery = 2,
		kCaptureStateSnapshot = 3,
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

	struct ResourceVersionInput
	{
		ResourceObservationInput resource;
		std::uint32_t firstSubresource{ 0 };
		std::uint32_t subresourceCount{ 1 };
		std::uint64_t writeEpoch{ 0 };
		std::uint64_t producerFrame{ kUnknownFrame };
		ResourceReadinessDomain readinessDomain{ ResourceReadinessDomain::kUnknown };
		Eye eye{ Eye::kUnknown };
		std::uint8_t eyeMask{ 0 };
	};

	struct VisibilitySubmissionInput
	{
		std::uintptr_t renderPass{ 0 };
		std::uintptr_t geometry{ 0 };
		std::uint32_t objectIndex{ 0 };
		std::uint32_t category{ 0 };
		std::uint64_t resourceVersionObservationId{ 0 };
		ResourceViewInput requestedView;
		ResourceViewInput effectiveView;
		std::uint32_t slot{ 0 };
		bool bindingMatches{ false };
		bool forcedVisible{ false };
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
		std::string_view compileSourceName;
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
		SceneObjectObservationInput sceneObject;
		GeometryObservationInput geometryObservation;
		MaterialStateObservationInput materialState;
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
		std::optional<CaptureSnapshot> StopCapture(
			StopReason a_reason = StopReason::kRequested,
			std::chrono::milliseconds a_drainTimeout = std::chrono::milliseconds(100));
		bool IsCapturing() const noexcept;
		bool IsCaptureDraining() const noexcept;
		std::uint64_t ActiveCaptureGeneration() const noexcept;

		void SetCpuFrame(std::uint64_t a_cpuFrame) noexcept;
		void SetFrameContext(const FrameContext& a_context) noexcept;

		Collector::ScopeGuard EnterRenderPass(const RenderPassBoundary& a_boundary) noexcept;
		Collector::ScopeGuard EnterTechnique(const TechniqueBoundary& a_boundary) noexcept;
		Collector::ScopeGuard EnterGeometry(const GeometryBoundary& a_boundary) noexcept;
		void RecordTechniqueResolution(const TechniqueResolution& a_resolution) noexcept;
		void SetImmediateContext(std::uintptr_t a_context) noexcept;
		void RegisterDeferredContext(
			std::uintptr_t a_context,
			std::uint32_t a_contextFlags,
			bool a_creationObserved = true) noexcept;
		void RecordFinishCommandList(
			std::uintptr_t a_context,
			std::uintptr_t a_commandList,
			bool a_restoreDeferredContextState,
			std::int32_t a_result) noexcept;
		void RecordExecuteCommandList(
			std::uintptr_t a_context,
			std::uintptr_t a_commandList,
			bool a_restoreContextState) noexcept;
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
			bool a_keepTargets = false,
			TargetBindingSource a_source = TargetBindingSource::kObservedCall,
			std::uint64_t a_expectedCaptureGeneration = 0) noexcept;
		std::uint64_t ClaimRenderTargetStateSeed(std::uintptr_t a_context) noexcept;
		void BindResourceViews(
			std::uintptr_t a_context,
			ResourceBindingKind a_bindingKind,
			ResourceStage a_stage,
			std::uint32_t a_startSlot,
			std::uint32_t a_viewCount,
			const ResourceViewInput* a_views,
			bool a_keepViews = false,
			ResourceBindingSource a_source = ResourceBindingSource::kRequestedCall,
			std::uint64_t a_expectedCaptureGeneration = 0) noexcept;
		std::uint64_t ClaimResourceViewStateSeed(std::uintptr_t a_context) noexcept;
		void RecordResourceFlow(
			std::uintptr_t a_context,
			ResourceFlowOperation a_operation,
			const ResourceObservationInput& a_source,
			const ResourceObservationInput& a_destination,
			std::uint32_t a_sourceSubresource = 0,
			std::uint32_t a_destinationSubresource = 0) noexcept;
		void RecordCpuMap(
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
			std::uint64_t a_expectedCaptureGeneration) noexcept;
		void RecordCpuUnmap(
			std::uintptr_t a_context,
			const ResourceObservationInput& a_resource,
			std::uint32_t a_subresource,
			std::uint64_t a_completedQpcTick,
			std::uint64_t a_expectedCaptureGeneration) noexcept;
		void RecordVisibilityCandidate(
			std::uintptr_t a_object,
			std::uint32_t a_objectIndex,
			std::uint64_t a_producerFrame) noexcept;
		std::uint64_t RecordVisibilityResultReady(
			std::uintptr_t a_context,
			const ResourceVersionInput& a_version,
			const ResourceViewInput& a_view,
			std::uint32_t a_objectCount) noexcept;
		std::uint64_t DeclareVisibilitySubmission(
			std::uintptr_t a_context,
			const VisibilitySubmissionInput& a_submission) noexcept;
		void ClearPendingVisibilitySubmission(std::uintptr_t a_context = 0) noexcept;
		void RecordCullDecision(
			std::uint64_t a_resourceVersionObservationId,
			std::uint64_t a_captureGeneration,
			std::uint32_t a_objectIndex,
			bool a_producerVisible,
			std::uint32_t a_totalDraws,
			std::uint32_t a_lightingDraws,
			std::uint32_t a_distantTreeDraws,
			std::uint32_t a_grassDraws,
			std::uint64_t a_producerFrame) noexcept;
		void RecordEyeSubmission(
			const ResourceObservationInput& a_resource,
			Eye a_eye,
			std::uint8_t a_eyeMask,
			float a_uMin,
			float a_vMin,
			float a_uMax,
			float a_vMax,
			std::uint32_t a_submitFlags,
			std::uint64_t a_compositorCycle) noexcept;
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
		void RegisterCreatedStageShader(
			ShaderStage a_stage,
			std::uintptr_t a_d3dObject,
			std::uint64_t a_bytecodeSize,
			std::string_view a_bytecodeSha256) noexcept;
		void RegisterEngineStageShader(
			ShaderStage a_stage,
			std::uintptr_t a_d3dObject,
			std::string_view a_loaderType,
			std::uint32_t a_descriptor,
			std::string_view a_compileSourceName = {}) noexcept;
		void RetireShaderObservation(std::uintptr_t a_shader) noexcept;

	private:
		struct PersistentStageShaderKey
		{
			ShaderStage stage{ ShaderStage::kVertex };
			std::uintptr_t d3dObject{ 0 };

			bool operator==(const PersistentStageShaderKey&) const noexcept = default;
		};

		struct PersistentStageShaderKeyHash
		{
			std::size_t operator()(const PersistentStageShaderKey& a_key) const noexcept;
		};

		struct PersistentStageShaderIdentity
		{
			struct EngineAlias
			{
				std::string loaderType;
				std::string compileSourceName;
				std::uint32_t descriptor{ 0 };

				bool operator==(const EngineAlias&) const noexcept = default;
			};

			std::uint64_t bytecodeSize{ 0 };
			std::array<char, kSha256HexLength + 1> bytecodeSha256{};
			std::vector<EngineAlias> engineAliases;
		};

		struct ActiveCpuMapKey
		{
			std::uintptr_t context{ 0 };
			std::uintptr_t resource{ 0 };
			std::uint32_t subresource{ 0 };

			bool operator==(const ActiveCpuMapKey&) const noexcept = default;
		};

		struct ActiveCpuMapKeyHash
		{
			std::size_t operator()(const ActiveCpuMapKey& a_key) const noexcept;
		};

		struct ActiveCpuMap
		{
			std::uint64_t captureGeneration{ 0 };
			std::uint64_t observationId{ 0 };
			std::uint64_t completedQpcTick{ 0 };
			std::uint32_t mapType{ 0 };
			std::uint32_t mapFlags{ 0 };
			std::uint32_t rowPitch{ 0 };
			std::uint32_t depthPitch{ 0 };
		};

		struct DeferredContextState
		{
			std::uint64_t pointerGeneration{ 1 };
			std::uint64_t observationGeneration{ 0 };
			std::uint64_t observationId{ 0 };
			std::uint64_t commandSequence{ 0 };
			std::uint64_t recordingEpoch{ 0 };
			std::uint64_t recordingObservationId{ 0 };
			std::uint64_t recordingIncompleteReasons{ 0 };
			std::uint32_t contextFlags{ 0 };
			std::uint64_t creationCaptureGeneration{ 0 };
			std::uintptr_t boundVertexShader{ 0 };
			std::uintptr_t boundPixelShader{ 0 };
			std::uintptr_t boundComputeShader{ 0 };
			std::uint64_t boundVertexShaderObservationId{ 0 };
			std::uint64_t boundPixelShaderObservationId{ 0 };
			std::uint64_t boundComputeShaderObservationId{ 0 };
		};

		struct CommandListState
		{
			std::uint64_t pointerGeneration{ 1 };
			std::uint64_t observationGeneration{ 0 };
			std::uint64_t observationId{ 0 };
			std::uint64_t sourceContextObservationId{ 0 };
			std::uint64_t sourceRecordingObservationId{ 0 };
			bool sourceRecordingComplete{ false };
			std::uint64_t sourceRecordingIncompleteReasons{ 0 };
		};

		struct ContextObservation
		{
			DeviceContextKind kind{ DeviceContextKind::kUnknown };
			std::uint64_t observationId{ 0 };
			std::uint64_t commandSequence{ 0 };
			std::uint64_t recordingObservationId{ 0 };
		};

		std::uint64_t EnsureImmediateContextObservation() noexcept;
		ContextObservation EnsureContextObservation(std::uintptr_t a_context) noexcept;
		std::uint64_t StartDeferredRecording(
			DeferredContextState& a_state,
			std::uint64_t a_captureGeneration, bool a_partialAtCaptureStart) noexcept;
		void MarkDeferredRecordingIncomplete(
			std::uintptr_t a_context, std::uint64_t a_contextObservationId,
			std::uint64_t a_recordingObservationId,
			CommandRecordingIncompleteReason a_reason) noexcept;
		void ResetImmediatePipelineState() noexcept;
		std::uint64_t NextCommandStreamSequence() noexcept;
		std::uint64_t EnsureBoundStageObservation(ShaderStage a_stage) noexcept;
		StageShaderObservationResult ObserveBoundStage(
			ShaderStage a_stage,
			std::uintptr_t a_d3dObject) noexcept;
		StageShaderObservationResult ObserveStageShaderWithPersistent(
			const StageShaderObservationInput& a_input) noexcept;
		std::optional<PersistentStageShaderIdentity> FindCreatedStageShader(
			ShaderStage a_stage,
			std::uintptr_t a_d3dObject) const noexcept;
		void PublishBoundStageObservation(
			ShaderStage a_stage,
			std::uintptr_t a_d3dObject,
			const StageShaderObservationResult& a_observation) noexcept;
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
		std::atomic_uint64_t boundVertexShaderObservationId{ 0 };
		std::atomic_uint64_t boundPixelShaderObservationId{ 0 };
		std::atomic_uint64_t boundComputeShaderObservationId{ 0 };
		std::atomic_uint64_t boundTargetBindingObservationId{ 0 };
		std::atomic_uint64_t targetStateObservationGeneration{ 0 };
		std::atomic_uint64_t resourceViewStateObservationGeneration{ 0 };
		std::atomic_uint64_t immediateContextPointerGeneration{ 0 };
		std::atomic_uint64_t immediateContextObservationId{ 0 };
		std::atomic_uint64_t immediateContextObservationGeneration{ 0 };
		std::atomic_uint64_t immediateContextCommandSequence{ 0 };
		std::mutex immediateContextObservationMutex;
		std::mutex resourceViewStateMutex;
		std::mutex activeCpuMapMutex;
		std::mutex deferredContextMutex;
		std::mutex commandListMutex;
		std::unordered_map<std::uintptr_t, DeferredContextState> deferredContexts;
		std::unordered_map<std::uintptr_t, CommandListState> commandLists;
		std::unordered_map<ActiveCpuMapKey, ActiveCpuMap, ActiveCpuMapKeyHash> activeCpuMaps;
		std::uint64_t resourceViewStateGeneration{ 0 };
		std::array<std::array<std::array<std::uintptr_t, kMaximumShaderResourceSlots>, 7>, 2>
			effectiveResourceViews{};
		mutable std::shared_mutex persistentStageShaderMutex;
		std::unordered_map<PersistentStageShaderKey, PersistentStageShaderIdentity,
			PersistentStageShaderKeyHash>
			persistentStageShaders;
	};

	Runtime& GetRuntime() noexcept;
}
