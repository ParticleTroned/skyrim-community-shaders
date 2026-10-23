#pragma once

#include "../DLSSViewportCrop.h"
#include "CaptureEvidence.h"
#include "GuidedReconstructionPolicy.h"
#include "MemoryConservationPolicy.h"
#include "MemoryRetirementPolicy.h"
#include "ModelResolutionPolicy.h"
#include "RoiDescriptor.h"
#include "StallRecoveryPolicy.h"
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "LifetimeDiagnostics.h"
#endif
#include "PipelinePolicy.h"
#include "PrewarmPolicy.h"
#include "Runtime.h"
#include "Utils/GpuMemoryBudget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d11.h>
#include <limits>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <wrl/client.h>

namespace NeuralRendering
{
	enum class RendererStage : std::uint32_t
	{
		None,
		Validation,
		FailureLatched,
		DeviceCompatibility,
		InteropInitialization,
		RuntimeProbe,
		RuntimeInitialization,
		ResourceRetirement,
		ResourceCreation,
		ColorInputCopy,
		DepthGuideCopy,
		MotionVectorCopy,
		ControlMaskCopy,
		CommandBegin,
		FeatureEvaluate,
		CommandEnd,
		OutputCommit,
		ResetWait,
		RuntimeReset,
		InteropShutdown,
		DeviceRemoved,
		Quarantined,
		Complete,
		Count
	};

	struct RendererCounters
	{
		std::uint64_t attempts = 0;
		std::uint64_t successes = 0;
		std::uint64_t failures = 0;
		std::uint64_t validationFailures = 0;
		std::uint64_t interopInitializations = 0;
		std::uint64_t runtimeInitializations = 0;
		std::uint64_t resourceRebuilds = 0;
		std::uint64_t depthGuideCopies = 0;
		std::uint64_t controlMaskCopies = 0;
		std::uint64_t featureEvaluations = 0;
		std::uint64_t outputCommits = 0;
		std::uint64_t stereoAttempts = 0;
		std::uint64_t stereoSuccesses = 0;
		std::uint64_t stereoFailures = 0;
		std::uint64_t callerHistoryResets = 0;
		std::uint64_t forcedHistoryResets = 0;
		std::uint64_t discontinuousHistoryResets = 0;
		std::uint64_t resetAttempts = 0;
		std::uint64_t resetSuccesses = 0;
		std::uint64_t resetFailures = 0;
		std::uint64_t latchedBypasses = 0;
		std::uint64_t quarantinedBypasses = 0;
		std::uint64_t deviceRemovals = 0;
		std::uint64_t quarantines = 0;
		std::array<std::uint64_t, static_cast<std::size_t>(RendererStage::Count)> failuresByStage{};
		std::array<std::uint64_t, Runtime::kFeatureSlotCount> slotSuccesses{};
		std::array<std::uint64_t, Runtime::kFeatureSlotCount> slotFailures{};
	};

	struct RendererPerformanceTelemetry
	{
		std::uint64_t d3d11PreparationCpuEnqueueSamples = 0;
		std::uint64_t d3d11PreparationCpuEnqueueMicroseconds = 0;
		std::uint64_t lastD3D11PreparationCpuEnqueueMicroseconds = 0;
		std::uint64_t maximumD3D11PreparationCpuEnqueueMicroseconds = 0;
		std::uint64_t outputCommitCpuEnqueueSamples = 0;
		std::uint64_t outputCommitCpuEnqueueMicroseconds = 0;
		std::uint64_t lastOutputCommitCpuEnqueueMicroseconds = 0;
		std::uint64_t maximumOutputCommitCpuEnqueueMicroseconds = 0;
		std::uint64_t commandSubmissions = 0;
		std::uint64_t mainCommandSubmissions = 0;
		std::uint64_t submitCommandSubmissions = 0;
		std::uint64_t stereoCommandSubmissions = 0;
		std::uint64_t mainStereoCommandSubmissions = 0;
		std::uint64_t submitStereoCommandSubmissions = 0;
		std::uint64_t backpressureWaits = 0;
		std::uint64_t backpressureWaitMicroseconds = 0;
		std::uint64_t maximumBackpressureWaitMicroseconds = 0;
		std::uint64_t featureGpuSamples = 0;
		std::uint64_t featureGpuReadbackFailures = 0;
		std::uint64_t featureGpuMicroseconds = 0;
		std::uint64_t mainFeatureGpuSamples = 0;
		std::uint64_t mainFeatureGpuMicroseconds = 0;
		std::uint64_t submitFeatureGpuSamples = 0;
		std::uint64_t submitFeatureGpuMicroseconds = 0;
		std::array<std::uint64_t, kInsertionPointCount>
			featureGpuSamplesByInsertionPoint{};
		std::array<std::uint64_t, kInsertionPointCount>
			featureGpuMicrosecondsByInsertionPoint{};
		std::uint64_t unexpectedFeatureSlotMaskSamples = 0;
		std::uint64_t invalidInsertionPointSamples = 0;
		std::uint64_t lastFeatureGpuMicroseconds = 0;
		std::uint64_t maximumFeatureGpuMicroseconds = 0;
		std::uint64_t lastFeaturePixelCount = 0;
		std::uint32_t lastFeatureFrameId =
			std::numeric_limits<std::uint32_t>::max();
		std::uint32_t lastFeatureEvaluationCount = 0;
		std::uint32_t lastFeatureLogicalEyeCount = 0;
		std::uint32_t lastFeatureSlotMask = 0;
		InsertionPoint lastInsertionPoint = InsertionPoint::Count;
#ifdef DEVBENCH_BRIDGE_ENABLED
		std::uint64_t parallelCommandSubmissions = 0;
		bool lastFeatureParallel = false;
		std::array<std::optional<std::uint64_t>, kMaximumExecutionRegions> lastEvaluationGpuMicroseconds{};
		std::optional<ExecutionPlanSummary> lastExecutionPlan;
#endif
	};

	struct RendererSnapshot
	{
		std::string status = "idle";
		std::string trust = "unknown";
		std::string detail;
		std::string runtimeFailureStage = "none";
		std::string runtimePath;
		std::string runtimeHash;
		std::string runtimeVersion;
		std::string parameterCorePath;
		std::string parameterCoreHash;
		std::string parameterCoreTrust = "unknown";
		std::string parameterCoreSource = "none";
		RendererStage lastCompletedStage = RendererStage::None;
		RendererStage failureStage = RendererStage::None;
		std::int32_t lastResult = 0;
		std::uint32_t ngxResult = 0;
		std::uint32_t featureSlot = 0;
		std::uint32_t failureFeatureSlot = Runtime::kFeatureSlotCount;
		std::uint32_t frameId = std::numeric_limits<std::uint32_t>::max();
		std::uint32_t sourceWorldFrame =
			std::numeric_limits<std::uint32_t>::max();
		std::uint64_t generation = 0;
		InsertionPoint insertionPoint = kDefaultInsertionPoint;
		std::uint32_t modelResolutionPercent = 100;
		ReconstructionFilter reconstructionFilter = ReconstructionFilter::Bilinear;
		bool pressureResolutionEnabled = false;
		std::uint32_t requestedModelResolutionPercent = 100;
		std::uint32_t effectiveModelResolutionPercent = 100;
		bool modelSharedInputs = false;
		std::uint32_t modelSourceWidth = 0, modelSourceHeight = 0;
		std::uint32_t modelWidth = 0, modelHeight = 0;
		std::uint32_t colorWidth = 0;
		std::uint32_t colorHeight = 0;
		std::uint32_t guideWidth = 0;
		std::uint32_t guideHeight = 0;
		std::uint32_t outputWidth = 0;
		std::uint32_t outputHeight = 0;
		ComputeSubrect computeSubrect{};
		std::uint32_t controlMaskWidth = 0;
		std::uint32_t controlMaskHeight = 0;
		std::uint32_t colorFormat = 0;
		std::uint32_t depthSourceFormat = 0;
		std::uint32_t depthViewFormat = 0;
		std::uint32_t motionVectorFormat = 0;
		std::uint32_t outputFormat = 0;
		std::uint32_t controlMaskFormat = 0;
		std::uint64_t successes = 0;
		std::uint64_t failures = 0;
		std::uint64_t runtimeSuccessfulFrames = 0;
		std::uint32_t runtimeProxyHits = 0;
		bool featureUpscaling = false;
		bool runtimeProxyInstalled = false;
		bool failureLatched = false;
		bool quarantined = false;
		bool outputCommitted = false;
		bool parallelEyesAvailable = false, parallelEyesDisabled = false, parallelEyesLastSubmission = false;
		bool controlMaskPresent = false;
		bool providerBlending = false;
		bool useAutoMask = true;
#ifdef DEVBENCH_BRIDGE_ENABLED
		bool modelOutputSelected = false;
		GuidedReconstructionDecision guidedReconstruction{};
		bool reconstructionPrepared = false;
		GuidedReconstructionOverrideStatus reconstructionOverride{};
		PackedCropLayout packedCropLayout{};
		PrewarmPolicy prewarm{};
		Runtime::LifecycleCounts nativeLifecycle{};
		LifetimeFenceSnapshot prewarmSubmission{};
		std::uint64_t prewarmReservedBytes = 0;
		std::uint64_t shaderGenerationRequested = 0, shaderGenerationApplied = 0;
#endif
		RendererCounters counters{};
		RendererPerformanceTelemetry performance{};
		StallRecoveryPolicy stallRecovery{};
		MemoryRecoveryPolicy memoryRecovery{};
		MemoryConservationPolicy memoryConservation{};
		MemoryRetirementPolicy memoryRetirement{};
	};

	/** Private pre-DLSS reconstruction destination, published only after the complete transaction succeeds. */
	struct ModelOutputSelection
	{
		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> destination;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mask;
		ComputeSubrect maskSupport{};
		bool providerBlending = false;
	};

	struct RendererApplyArgs
	{
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		std::uint32_t featureSlot = 0;
		/** Current presentation/evaluation frame used by Feature 18 timing. */
		std::uint32_t frameId = std::numeric_limits<std::uint32_t>::max();
		/** Immutable world frame represented by the supplied temporal resources. */
		std::uint32_t sourceWorldFrame =
			std::numeric_limits<std::uint32_t>::max();
		std::uint64_t generation = 0;
		InsertionPoint insertionPoint = kDefaultInsertionPoint;
		ID3D11Resource* colorInput = nullptr;
		ExecutionContext executionContext{};
		std::shared_ptr<const CharacterPreparationEvidence> characterEvidence;
		ID3D11Resource* depthGuide = nullptr;
		ID3D11ShaderResourceView* depthGuideSRV = nullptr;
		ID3D11Resource* motionVectors = nullptr;
		ID3D11Resource* colorOutput = nullptr;
		/** Optional private UAV of colorOutput for fused reduced-resolution reconstruction and selection. */
		std::optional<ModelOutputSelection> modelOutputSelection;
		// R8_UNORM provider-mask transport. Actor blending converts selection to protection.
		Microsoft::WRL::ComPtr<ID3D11Resource> controlMask;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> actorSelection;
		bool providerBlending = false;
		ComputeSubrect actorSelectionSupport{};
		std::uint32_t modelResolutionPercent = 100;
		ReconstructionFilter reconstructionFilter = ReconstructionFilter::Bilinear;
		bool guidedReconstructionEnabled = false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		GuidedReconstructionOverride reconstructionOverride = GuidedReconstructionOverride::Auto;
#endif
		bool pressureResolutionEnabled = false;
		bool parallelEyes = false;
		/** Keep model preparation resident while moving actors alternate atlas and single ROI. */
		bool packedCropsEnabled = false;
		PackedCropSources packedCrops{};
		/** Frozen before admission: empty capacity selects the ordinary single-region path. */
		std::optional<UpscalingDLSS::Extent> packedCropCapacity;
		std::optional<PackedCropPlan> packedCropPlan;
#ifdef DEVBENCH_BRIDGE_ENABLED
		PackedCropPlanningDiagnostics packedCropPlanning{};
		PackedCropCapacityDiagnostics packedCropCapacityDiagnostics{};
		std::optional<PackedCropAdmissionDiagnostics> packedCropPlanDiagnostics;
#endif
		CentralArea centralArea{};
		/** Original sampling basis retained by independently scaled temporal inputs. */
		std::optional<ModelResolutionHistory> modelResolutionHistory;
		std::uint32_t colorWidth = 0;
		std::uint32_t colorHeight = 0;
		std::uint32_t guideWidth = 0;
		std::uint32_t guideHeight = 0;
		std::uint32_t outputWidth = 0;
		std::uint32_t outputHeight = 0;
		std::uint32_t controlMaskWidth = 0;
		std::uint32_t controlMaskHeight = 0;
		// Optional output-local Feature 18 compute rectangle. An invalid rectangle
		// retains the configured centered/full-resource behavior.
		ComputeSubrect computeSubrect{};
		UpscalingDLSS::ViewportCrop viewportCrop{};
		/** Prepared single-region roles, bound to this call's source, crop and execution context. */
		std::optional<RoiDescriptor> roi;
		// Caller guarantees exact outer CSX mask compositing; not the private NGX mask ABI.
		bool characterVisualIsolation = false;
		bool featureUpscaling = false;
		Tuning tuning{};
		bool reset = false;
		// Pair orchestration keeps separate eye submissions on one reset decision.
		bool synchronizedHistoryReset = false;
		bool synchronizedHistoryDiscontinuity = false;
		/** Runtime policy input independent of optional capture evidence. */
		std::optional<RenderingMode> renderingMode;
	};

	/** Per-call evidence captured at the exact NVIDIA evaluation boundary. */
	struct RendererApplyOutcome
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		bool reconstructionHistoryReset = false;
#endif
		std::uint32_t evaluationAttemptedFeatureSlotMask = 0;
		std::uint32_t evaluationSucceededFeatureSlotMask = 0;
		std::uint32_t selectedOutputFeatureSlotMask = 0;
		bool memoryPressureBypass = false;

		/** True only after every requested private output in the transaction is ready for publication. */
		[[nodiscard]] bool WasOutputSelected(std::uint32_t featureSlot) const noexcept
		{
			return featureSlot < Runtime::kFeatureSlotCount &&
			       (selectedOutputFeatureSlotMask & (1u << featureSlot)) != 0;
		}

		[[nodiscard]] bool WasEvaluationAttempted(
			std::uint32_t a_featureSlot) const noexcept
		{
			return a_featureSlot < Runtime::kFeatureSlotCount &&
			       (evaluationAttemptedFeatureSlotMask & (1u << a_featureSlot)) != 0;
		}

		[[nodiscard]] bool WasEvaluationSuccessful(
			std::uint32_t a_featureSlot) const noexcept
		{
			return a_featureSlot < Runtime::kFeatureSlotCount &&
			       (evaluationSucceededFeatureSlotMask & (1u << a_featureSlot)) != 0;
		}
	};

	class Renderer
	{
	public:
		using ApplyArgs = RendererApplyArgs;

		static Renderer& Instance();
		/** Snapshot both route transactions without querying current configuration. */
		std::array<CaptureInputs, 2> GetCaptureInputs() const;

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		/** Runs one Feature 18 slot and commits the external output only on success. */
		bool Apply(
			const RendererApplyArgs& a_args,
			RendererApplyOutcome* a_outcome = nullptr);
		/** Submits a validated eye pair together and commits neither output on failure. */
		bool ApplyStereo(
			const std::array<RendererApplyArgs, 2>& a_args,
			RendererApplyOutcome* a_outcome = nullptr);
		/** Runs two validated eye transactions separately with one reset policy.
		 *  Colour reconstruction uses one atomic batch for both eyes. */
		bool ApplySequentialStereo(
			const std::array<RendererApplyArgs, 2>& a_args,
			RendererApplyOutcome* a_outcome = nullptr);

		/** Drains both APIs; optional retention applies only to a completely healthy backend. */
		bool Reset(bool a_clearTransportRejections = false,
			BackendRetirementPolicy a_policy = BackendRetirementPolicy::ReleaseBackend);
		/** Services retained resources or a latched transient stall; otherwise performs no sampling. */
		void ServiceBackendMaintenance(bool a_requested);
		/** Service one optional stage at the completed boundary; false leaves the request unconsumed. */
		[[nodiscard]] bool ServicePrewarm(const PrewarmKey* request, const char* deferReason,
			std::uint64_t opportunity);
		/** Cheap disabled-path gate; pending unused ownership still needs maintenance. */
		[[nodiscard]] bool HasPrewarmWork() const noexcept;

		/** Request coalesced shader-only invalidation at the completed-frame maintenance boundary. */
		void ResetShaderCache();

		[[nodiscard]] RendererSnapshot GetSnapshot() const;
		/** Lock-free projection eligibility hint; frame-latched policy remains the authority for model quality. */
		[[nodiscard]] std::uint32_t ModelResolutionCeiling() const noexcept;
		/** Admit full private-output replacement overlap; keep the ticket through creation and a fresh shared sample. */
		[[nodiscard]] Util::GpuMemoryBudget::Reservation ReserveOutputAllocation(ID3D11Device* device, std::uint64_t bytes);
		/** Queues recovery without GPU work; the caller must gate this on requested NR. */
		bool NotifyDlssMemoryPressure();
#ifdef DEVBENCH_BRIDGE_ENABLED
		/** Start/cancel a bounded session-only comparison, rejecting stale restoration tokens. */
		bool ConfigureReconstructionOverride(GuidedReconstructionOverride, std::uint32_t frames, std::uint64_t expectedRevision);
		/** Resolve before source admission; the caller resets downstream history once when changed. */
		[[nodiscard]] std::pair<GuidedReconstructionOverride, bool> ResolveReconstructionOverride(std::uint32_t frame, std::uint64_t generation);
		/** Injects a logical backpressure timeout; recovery still requires real fence completion. */
		bool SimulateTransientStall();
		static constexpr std::uint32_t kMaximumMemorySimulationMilliseconds = 30000;
		/** Elevates pressure for at most 30 seconds; conservationOnly targets the early reclamation stage. */
		bool SimulateMemoryPressure(std::uint32_t a_durationMilliseconds, bool a_conservationOnly = false);
		/** Retains the bounded opt-in native lifetime history, including its first failure. */
		[[nodiscard]] LifetimeSnapshot GetLifetimeDiagnostics() const;
		/** Session-only CPU transition capture, independent of profiling and full frame evidence. */
		void ConfigurePackedCropDiagnostics(std::optional<bool> enabled, bool clear);
		/** Retain the first rejected atlas and same-frame native rebuild costs until explicitly cleared. */
		[[nodiscard]] nlohmann::json GetPackedCropDiagnostics() const;
		/** Read-only allocation/lease accounting; native provider byte sizes remain unknown. */
		[[nodiscard]] nlohmann::json GetSourceTransportDiagnostics() const;
#endif
		[[nodiscard]] bool IsFailureLatched() const;
		[[nodiscard]] bool IsQuarantined() const;

	private:
		class State;

		Renderer();
		~Renderer();

		State* state_ = nullptr;
	};

	[[nodiscard]] const char* ToString(RendererStage a_stage);
}
