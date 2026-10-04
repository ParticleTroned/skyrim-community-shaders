#pragma once

#include <cstdint>

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "VRDepthCullingTelemetry.h"
#	include "VRHybridCullingDiagnostics.h"
#	include "VRHybridCullingPolicy.h"
#	include "VRHybridCullingSnapshot.h"
#endif

namespace VRHybridCulling
{
#ifdef DEVBENCH_BRIDGE_ENABLED
	using StageTiming = VRDepthCullingTelemetry::StageTiming;
	inline constexpr std::array FallbackReasons{
		"unsupported_frame", "resource_publication_unavailable", "pipeline_unavailable", "resource_setup_failed",
		"not_prepared", "empty_or_invalid_batch", "preparation_expired", "native_buffers_invalid", "renderer_unavailable",
		"resource_publication_changed", "pipeline_owner_changed", "prepared_source_changed", "replacement_not_prepared"
	};
	inline constexpr std::array HistoryRejectionReasons{
		"unreadable_batch", "batch_mismatch", "pipeline_changed", "bounds_changed", "resource_publication_changed",
		"frame_unavailable", "depth_changed", "view_changed", "configuration_changed"
	};
	inline constexpr std::array MatchedDropReasons{
		"unreadable_batch", "batch_mismatch", "pipeline_changed", "bounds_changed", "resource_publication_changed",
		"frame_unavailable", "depth_changed", "view_changed", "configuration_changed", "native_submission_changed",
		"diagnostics_unavailable", "queue_full", "readback_expired", "window_changed", "submission_abandoned",
		"renderer_context_changed", "visibility_map_failed", "diagnostic_map_failed", "invalid_record", "native_recovery_changed", "dispatch_failed"
	};

	struct SourceSnapshot
	{
		bool available = false;
		bool valid = false;
		bool current = false;
		bool busy = false;
		const char* validity = "not_captured";
		const char* stage = "none";
		std::uint64_t cullingEpoch = 0;
		VRHybridCullingSnapshot::Source source{};
		std::array<VRHybridCullingPolicy::EyeRect, VRHybridCullingPolicy::kEyeCount> eyes{};
		VRHybridCullingPolicy::PyramidLayout pyramid{};
		std::uint64_t logicalPyramidBytes = 0;
		std::array<std::array<std::array<float, 4>, 4>, VRHybridCullingPolicy::kEyeCount> viewProjection{};
		std::array<std::array<std::array<float, 4>, 4>, VRHybridCullingPolicy::kEyeCount> unjitteredProjection{};
		std::array<std::array<float, 4>, VRHybridCullingPolicy::kEyeCount> cameraAdjust{};
	};

	struct Status
	{
		const char* state = "idle";
		const char* effectiveBackend = "pending";
		const char* fallbackReason = "none";
		const char* historyRejectionReason = "none";
		std::uint64_t submittedBatches = 0;
		std::uint64_t acceptedBatches = 0;
		std::uint64_t invalidatedBatches = 0;
		std::uint64_t fallbackBatches = 0;
		std::uint64_t promotedObjects = 0;
		std::uint64_t unreadableBatches = 0;
		std::uint64_t submittedObjects = 0;
		std::uint64_t testedObjects = 0;
		std::uint64_t acceptedOccludedObjects = 0;
		std::uint64_t acceptedVisibleObjects = 0;
		std::uint64_t pipelineBuildAttempts = 0;
		std::uint64_t pipelineBuilds = 0;
		std::uint64_t pipelineRecreations = 0;
		std::uint64_t pyramidAllocations = 0;
		std::uint64_t pyramidBuilds = 0;
		std::uint64_t pyramidDispatches = 0;
		std::uint64_t boundsDispatches = 0;
		std::uint64_t logicalPyramidBytesHighWater = 0;
		std::uint64_t droppedSourceSnapshots = 0;
		std::array<std::uint64_t, FallbackReasons.size()> fallbackReasonCounts{};
		std::array<std::uint64_t, HistoryRejectionReasons.size()> historyRejectionReasonCounts{};
		SourceSnapshot snapshot{};
		std::uint32_t lastObjectCount = 0;
		StageTiming prepare, dispatch, readback;
		std::uint32_t sourceReductionActive = 0;
		bool sourceRefinementEnabled = true;
		bool directIntersectionEnabled = true;
		bool farClipEnabled = true;
		bool traversalDiagnosticsEnabled = false;
		bool traversalDiagnosticsAvailable = false;
		const char* traversalDiagnosticsAvailability = "not_created";
		std::uint64_t traversalSubmittedBatches = 0, traversalUnavailableBatches = 0;
		VRHybridCullingDiagnostics::Totals traversal{};
		std::uint64_t traversalBatches = 0, traversalNotReadyBatches = 0, traversalFailedBatches = 0;
		std::uint64_t traversalDiscardedBatches = 0;
		bool matchedDiagnosticsEnabled = false, matchedSnapshotAvailable = false, matchedSnapshotBusy = false;
		std::uint64_t matchedSubmittedBatches = 0, matchedBatches = 0, matchedDroppedBatches = 0, matchedFailedBatches = 0;
		std::uint64_t matchedPendingBatches = 0, matchedNotReadyPolls = 0;
		std::uint64_t matchedSnapshotPublicationMisses = 0;
		std::array<std::uint64_t, MatchedDropReasons.size()> matchedDropReasonCounts{};
		VRHybridCullingDiagnostics::MatchedTotals matched{};
	};
#endif

	/** Compile the pipeline during VR renderer setup; depth textures remain frame-dependent. */
	void PrewarmShaders();
	/** Prepare stereo resources before suppressing the native depth downsample. */
	[[nodiscard]] bool Prepare(std::uint64_t a_epoch);
	/** Write native-indexed visibility and enqueue its existing staging copy. */
	[[nodiscard]] bool Dispatch(void* a_culler, std::uint64_t a_epoch);
	/** Validate the exact submitted batch after native readback, before collection resets it. */
	[[nodiscard]] bool CompleteReadback(void* a_culler, std::uint64_t a_epoch, bool a_selected);
	/** Clear render-thread preparation when the native producer must be used. */
	void CancelPreparation(bool a_hybridSelected, std::uint64_t a_epoch);
	/** Request pipeline recreation on the next render-thread preparation. */
	void ClearShaderCache();
#ifdef DEVBENCH_BRIDGE_ENABLED
	/** Select the extra shader/readback instrumentation independently of GPU profiling. */
	void SetTraversalDiagnosticsEnabled(bool a_enabled) noexcept;
	/** Enable diagnostic shadow testing while native culling continues to own displayed visibility. */
	void SetMatchedDiagnosticsEnabled(bool a_enabled) noexcept;
	/** Return whether the telemetry gate admits native/Hi-Z shadow comparisons. */
	[[nodiscard]] bool IsMatchedDiagnosticsActive() noexcept;
	/** Test the prepared native batch into private diagnostic buffers. */
	void DispatchMatched(void* a_culler, std::uint64_t a_epoch);
	/** Bind the native output after its producer, retaining the exact submitted input. */
	void FinalizeMatchedSubmission(void* a_culler, std::uint64_t a_epoch);
	/** Toggle original-depth refinement for DevBench A/B; production always retains refinement. */
	void SetSourceRefinementEnabled(bool a_enabled) noexcept;
	/** Toggle direct triangle intersection proofs against the retained polygon clipper for DevBench A/B. */
	void SetDirectIntersectionEnabled(bool a_enabled) noexcept;
	/** Toggle conservative far-depth vertex clamping independently of intersection proofs for DevBench A/B. */
	void SetFarClipEnabled(bool a_enabled) noexcept;
	/** Publish matched outcomes after Advanced recovery, without changing native results. */
	void CompleteMatchedRecovery(void* a_culler, std::uint64_t a_epoch);
	/** Read current backend state and gated measurements without owning the render context. */
	[[nodiscard]] Status GetStatus(std::uint64_t a_epoch, bool a_selected, bool a_enabled, bool a_installed);
	/** Reset measurements while the shared depth-culling telemetry gate is exclusively held. */
	void ResetTelemetryUnderLock() noexcept;
#endif
}
