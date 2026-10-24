#pragma once

#include "RenderMap/Collector.h"
#include "RenderMap/ShaderBytecodeCatalogue.h"
#include "RenderMap/TransferVersionPolicy.h"

#include <bitset>
#include <cstdint>
#include <functional>
#include <memory>
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
		kPostProcessingBoundary = 31,
		kRasterState = 32,
		kTransferResourceAccess = 33,
		kEyePublication = 34,
		kTransferOperation = 35,
		kTransferCopyRegion = 36,
		kNativePipelineSnapshot = 37,
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
		class PostProcessingScope
		{
		public:
			~PostProcessingScope();
			PostProcessingScope(const PostProcessingScope&) = delete;
			PostProcessingScope& operator=(const PostProcessingScope&) = delete;

		private:
			friend class Runtime;
			PostProcessingScope(Runtime& a_owner, const ResourceObservationInput& a_source,
				const ResourceObservationInput& a_destination, std::uint32_t a_target,
				std::uint64_t a_publicationGeneration) noexcept;
			const Runtime* previousOwner = nullptr;
			std::uint64_t previousGeneration = 0;
			std::uint64_t previousOperation = 0;
			Collector::ScopeGuard scope;
		};

		/** Bound observations to the exact original engine post-processing call. */
		PostProcessingScope EnterPostProcessing(const ResourceObservationInput& a_source,
			const ResourceObservationInput& a_destination, std::uint32_t a_target,
			std::uint64_t a_publicationGeneration) noexcept;
		bool IsInsidePostProcessing() const noexcept;
		/** Correlate one operation with its queried native shader-stage bindings. */
		void BeginTransferOperation(std::uintptr_t a_context, bool a_compute,
			std::uint32_t a_operation, const std::array<std::uintptr_t, 6>& a_shaders) noexcept;
		/** Record a queried raster slot immediately before an observed operation. */
		void RecordRasterState(std::uintptr_t a_context, std::uint32_t a_slot,
			const std::array<float, 6>& a_viewport, const std::array<std::int32_t, 4>& a_scissor,
			std::uint32_t a_viewportCount, std::uint32_t a_scissorCount, bool a_scissorEnabled) noexcept;
		/** Record bound candidates and observed command epochs, never pixel identity. */
		void RecordTransferResourceAccess(std::uintptr_t a_context, const ResourceViewInput& a_view,
			ResourceStage a_stage, std::uint32_t a_slot, bool a_write) noexcept;
		void RecordTransferCopyRegion(std::uintptr_t a_context, std::uint32_t a_sourceSubresource,
			std::uint32_t a_destinationSubresource, const std::array<std::uint32_t, 3>& a_destination,
			const std::array<std::uint32_t, 6>& a_sourceBox, bool a_hasSourceBox) noexcept;

		StartResult StartCapture(const CollectorConfig& a_config);
		std::optional<CaptureSnapshot> StopCapture(
			StopReason a_reason = StopReason::kRequested,
			std::chrono::milliseconds a_drainTimeout = std::chrono::milliseconds(100));
		bool IsCapturing() const noexcept;
		/** Start the opt-in late window before constructing its native boundary. */
		bool ActivatePostProcessingWindow(std::uint32_t a_target, std::uint64_t a_frame,
			std::uint64_t a_publicationGeneration) noexcept;
		void CompleteWindowBootstrap(bool a_success) noexcept;
		/** Record queried shader state only for the retained scope generation, without inventing execution. */
		void RecordPostProcessingBootstrap(const std::array<std::uintptr_t, 6>& a_shaders,
			std::uint64_t a_expectedCaptureGeneration) noexcept;
		/** Retained scope generation; remains unchanged when the active capture turns over. */
		std::uint64_t PostProcessingCaptureGeneration() const noexcept;
		CaptureWindowSnapshot GetCaptureWindow() const noexcept;
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
		std::uint64_t ClaimRenderTargetStateSeed(std::uintptr_t a_context,
			std::uint64_t a_expectedCaptureGeneration = 0) noexcept;
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
		std::uint64_t ClaimResourceViewStateSeed(std::uintptr_t a_context,
			std::uint64_t a_expectedCaptureGeneration = 0) noexcept;
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
			std::uint64_t a_compositorCycle, std::uint64_t a_publicationGeneration = 0) noexcept;
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
		/** @brief Produce teardown-safe cleanup for an observed native shader's metadata. */
		std::function<void()> MakeStageShaderRetirementCallback(
			ShaderStage a_stage, std::uintptr_t a_d3dObject) const;
		void RegisterEngineStageShader(
			ShaderStage a_stage,
			std::uintptr_t a_d3dObject,
			std::string_view a_loaderType,
			std::uint32_t a_descriptor,
			std::string_view a_compileSourceName = {}) noexcept;
		void RetireShaderObservation(std::uintptr_t a_shader) noexcept;

#if defined(CSX_RENDER_MAP_TESTING)
		void FailNextDeferredContextCatalogueAdmissionForTesting() noexcept;
		void FailNextCommandListCatalogueAdmissionForTesting() noexcept;
		void PauseCommandListAdmissionsForTesting(std::uint32_t a_count) noexcept;
		std::uint32_t PausedCommandListAdmissionsForTesting() const noexcept;
		void ResumeCommandListAdmissionsForTesting() noexcept;
		std::size_t CommandListCatalogueSizeForTesting() noexcept;
		void PauseNextProducerPublicationForTesting(std::uint32_t a_skipPublications = 0) noexcept;
		void PauseNextDeferredPublicationForTesting() noexcept;
		void PauseNextImmediateStagePublicationForTesting() noexcept;
		void PauseNextImmediateDispatchAdmissionForTesting() noexcept;
		void PauseNextDeferredFinishCleanupForTesting() noexcept;
		bool IsDeferredPublicationPausedForTesting() const noexcept;
		void ResumeDeferredPublicationForTesting() noexcept;
#endif

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
			bool engineAliasesOverflowed{ false };
		};

		struct PersistentStageShaderCatalogue
		{
			mutable std::shared_mutex mutex;
			std::unordered_map<PersistentStageShaderKey, PersistentStageShaderIdentity,
				PersistentStageShaderKeyHash>
				records;
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
			std::uint64_t captureGeneration{ 0 };
			std::uint64_t observationId{ 0 };
			std::uint64_t commandSequence{ 0 };
			std::uint64_t recordingObservationId{ 0 };
		};

		struct ImmediateStageObservation
		{
			std::uintptr_t d3dObject{ 0 };
			std::uint64_t observationId{ 0 };
			std::uint64_t captureGeneration{ 0 };
		};

		std::uint64_t EnsureImmediateContextObservation(std::uint64_t a_expectedCaptureGeneration) noexcept;
		ContextObservation EnsureContextObservation(std::uintptr_t a_context) noexcept;
		std::uint64_t StartDeferredRecording(
			DeferredContextState& a_state,
			std::uint64_t a_captureGeneration, bool a_partialAtCaptureStart) noexcept;
		void MarkDeferredRecordingIncomplete(
			std::uintptr_t a_context, std::uint64_t a_captureGeneration,
			std::uint64_t a_contextObservationId,
			std::uint64_t a_recordingObservationId,
			CommandRecordingIncompleteReason a_reason) noexcept;
#if defined(CSX_RENDER_MAP_TESTING)
		void PauseProducerPublicationForTesting() noexcept;
		void PauseCommandListAdmissionForTesting() noexcept;
		void PauseDeferredPublicationBeforeAppendForTesting() noexcept;
		void PauseImmediateStagePublicationForTesting() noexcept;
		void PauseImmediateDispatchBeforeAppendForTesting() noexcept;
		void PauseDeferredFinishCleanupForTesting() noexcept;
#endif
		void ResetImmediatePipelineState() noexcept;
		void ResetImmediateStageObservations(bool a_clearBindings) noexcept;
		void SetImmediateBoundStage(ShaderStage a_stage, std::uintptr_t a_d3dObject) noexcept;
		void BindBootstrapStage(ShaderStage a_stage, std::uintptr_t a_d3dObject,
			std::uint64_t a_expectedCaptureGeneration) noexcept;
		std::uint64_t ClaimImmediateStateSeed(std::atomic_uint64_t& a_seed,
			std::uintptr_t a_context, std::uint64_t a_expectedCaptureGeneration) noexcept;
		ImmediateStageObservation ReadImmediateStageObservation(ShaderStage a_stage) const noexcept;
		void ApplyEffectiveResourceViewResetLocked() noexcept;
		std::uint64_t NextCommandStreamSequence() noexcept;
		ImmediateStageObservation EnsureBoundStageObservation(ShaderStage a_stage, std::uint64_t a_expectedCaptureGeneration) noexcept;
		StageShaderObservationResult ObserveBoundStage(
			ShaderStage a_stage,
			std::uintptr_t a_d3dObject, std::uint64_t a_expectedCaptureGeneration) noexcept;
		StageShaderObservationResult ObserveStageShaderWithPersistent(
			const StageShaderObservationInput& a_input, std::uint64_t a_expectedCaptureGeneration) noexcept;
		std::optional<PersistentStageShaderIdentity> FindCreatedStageShader(
			ShaderStage a_stage,
			std::uintptr_t a_d3dObject) const noexcept;
		std::shared_ptr<PersistentStageShaderCatalogue> EnsurePersistentStageShaderCatalogue() const;
		void PublishBoundStageObservation(
			ShaderStage a_stage,
			std::uintptr_t a_d3dObject,
			const StageShaderObservationResult& a_observation) noexcept;
		TargetViewObservationResult ObserveResourceView(
			const ResourceViewInput& a_input,
			std::uint64_t a_contextObservationId,
			std::uint64_t a_commandStreamSequence, std::uint64_t a_expectedCaptureGeneration) noexcept;
		ResourceObservationResult ObserveResource(
			const ResourceObservationInput& a_input,
			std::uint64_t a_contextObservationId,
			std::uint64_t a_commandStreamSequence, std::uint64_t a_expectedCaptureGeneration) noexcept;

		Collector collector;
		std::mutex transferVersionMutex;
		TransferVersions transferVersions;
		// Serializes reset-before-publish capture transitions with shutdown.
		std::mutex captureLifecycleMutex;
		std::atomic_uintptr_t immediateContext{ 0 };
		std::atomic_uintptr_t boundVertexShader{ 0 };
		std::atomic_uintptr_t boundPixelShader{ 0 };
		std::atomic_uintptr_t boundComputeShader{ 0 };
		std::atomic_uint64_t boundVertexShaderObservationId{ 0 };
		std::atomic_uint64_t boundPixelShaderObservationId{ 0 };
		std::atomic_uint64_t boundComputeShaderObservationId{ 0 };
		std::atomic_uint64_t boundVertexShaderObservationGeneration{ 0 };
		std::atomic_uint64_t boundPixelShaderObservationGeneration{ 0 };
		std::atomic_uint64_t boundComputeShaderObservationGeneration{ 0 };
		std::atomic_uint64_t immediateStageObservationRevision{ 0 };
		std::atomic_uint64_t boundTargetBindingObservationId{ 0 };
		std::atomic_uint64_t targetStateObservationGeneration{ 0 };
		std::atomic_uint64_t resourceViewStateObservationGeneration{ 0 };
		std::atomic_bool resourceViewStateResetPending{ false };
		std::atomic_uint64_t immediateContextPointerGeneration{ 0 };
		std::atomic_uint64_t immediateContextObservationId{ 0 };
		std::atomic_uint64_t immediateContextObservationGeneration{ 0 };
		std::atomic_uint64_t immediateContextCommandSequence{ 0 };
		std::mutex immediateContextObservationMutex;
		std::mutex immediateStageObservationMutex;
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
		std::array<std::array<std::bitset<kMaximumShaderResourceSlots>, 7>, 2>
			resourceViewSlotsNeedingObservation{};
#if defined(CSX_RENDER_MAP_TESTING)
		std::atomic_bool failNextDeferredContextCatalogueAdmission{ false };
		std::atomic_bool failNextCommandListCatalogueAdmission{ false };
		std::atomic_bool pauseNextProducerPublication{ false };
		std::atomic_uint32_t producerPublicationsToSkip{ 0 };
		std::atomic_uint32_t commandListAdmissionsToPause{ 0 };
		std::atomic_uint32_t pausedCommandListAdmissions{ 0 };
		std::atomic_bool resumeCommandListAdmissions{ false };
		std::atomic_bool pauseNextDeferredPublication{ false };
		std::atomic_bool pauseNextImmediateStagePublication{ false };
		std::atomic_bool pauseNextImmediateDispatchAdmission{ false };
		std::atomic_bool pauseNextDeferredFinishCleanup{ false };
		std::atomic_bool deferredPublicationPaused{ false };
		std::atomic_bool resumeDeferredPublication{ false };
#endif
		mutable std::atomic<std::shared_ptr<PersistentStageShaderCatalogue>> persistentStageShaderCatalogue;
	};

	Runtime& GetRuntime() noexcept;
}
