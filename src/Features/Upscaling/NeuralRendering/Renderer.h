#pragma once

#include "PipelinePolicy.h"
#include "Runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Resource;
struct ID3D11ShaderResourceView;

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
		std::uint32_t lastFeatureEvaluationCount = 0;
		std::uint32_t lastFeatureSlotMask = 0;
		InsertionPoint lastInsertionPoint = InsertionPoint::Count;
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
		std::uint64_t generation = 0;
		InsertionPoint insertionPoint = kDefaultInsertionPoint;
		std::uint32_t colorWidth = 0;
		std::uint32_t colorHeight = 0;
		std::uint32_t guideWidth = 0;
		std::uint32_t guideHeight = 0;
		std::uint32_t outputWidth = 0;
		std::uint32_t outputHeight = 0;
		std::uint32_t colorFormat = 0;
		std::uint32_t depthSourceFormat = 0;
		std::uint32_t depthViewFormat = 0;
		std::uint32_t motionVectorFormat = 0;
		std::uint32_t outputFormat = 0;
		std::uint64_t successes = 0;
		std::uint64_t failures = 0;
		std::uint64_t runtimeSuccessfulFrames = 0;
		std::uint32_t runtimeProxyHits = 0;
		bool featureUpscaling = false;
		bool runtimeProxyInstalled = false;
		bool failureLatched = false;
		bool quarantined = false;
		bool outputCommitted = false;
		RendererCounters counters{};
		RendererPerformanceTelemetry performance{};
	};

	struct RendererApplyArgs
	{
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		std::uint32_t featureSlot = 0;
		std::uint32_t frameId = std::numeric_limits<std::uint32_t>::max();
		std::uint64_t generation = 0;
		InsertionPoint insertionPoint = kDefaultInsertionPoint;
		ID3D11Resource* colorInput = nullptr;
		ID3D11Resource* depthGuide = nullptr;
		ID3D11ShaderResourceView* depthGuideSRV = nullptr;
		ID3D11Resource* motionVectors = nullptr;
		ID3D11Resource* colorOutput = nullptr;
		std::uint32_t colorWidth = 0;
		std::uint32_t colorHeight = 0;
		std::uint32_t guideWidth = 0;
		std::uint32_t guideHeight = 0;
		std::uint32_t outputWidth = 0;
		std::uint32_t outputHeight = 0;
		float motionVectorScaleX = 1.0f;
		float motionVectorScaleY = 1.0f;
		std::uint32_t inputOffsetX = 0;
		std::uint32_t inputOffsetY = 0;
		std::uint32_t outputOffsetX = 0;
		std::uint32_t outputOffsetY = 0;
		float pinholeOffsetX = 0.0f;
		float pinholeOffsetY = 0.0f;
		bool featureUpscaling = false;
		Tuning tuning{};
		bool reset = false;
		// Pair orchestration keeps separate eye submissions on one reset decision.
		bool synchronizedHistoryReset = false;
		bool synchronizedHistoryDiscontinuity = false;
	};

	/** Per-call evidence captured at the exact NVIDIA evaluation boundary. */
	struct RendererApplyOutcome
	{
		std::uint32_t evaluationAttemptedFeatureSlotMask = 0;
		std::uint32_t evaluationSucceededFeatureSlotMask = 0;

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

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		/** Runs one Feature 18 slot and commits the external output only on success. */
		bool Apply(
			const RendererApplyArgs& a_args,
			RendererApplyOutcome* a_outcome = nullptr);
		/** Runs both eye slots in one command list and commits neither output on failure. */
		bool ApplyStereo(
			const std::array<RendererApplyArgs, 2>& a_args,
			RendererApplyOutcome* a_outcome = nullptr);
		/** Runs two validated eye transactions separately with one reset policy. */
		bool ApplySequentialStereo(
			const std::array<RendererApplyArgs, 2>& a_args,
			RendererApplyOutcome* a_outcome = nullptr);

		/** Performs a bounded idle wait before releasing runtime and interop ownership. */
		bool Reset();
		void ResetShaderCache();

		[[nodiscard]] RendererSnapshot GetSnapshot() const;
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
