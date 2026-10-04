#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Features/VRDepthCullingTemporal.h"
#	include "Features/VRHybridCulling.h"

#	include <array>
#	include <cstddef>
#	include <cstdint>
#	include <nlohmann/json.hpp>
#	include <utility>

namespace MenuDepthCullingDiagnostics
{
	/** Missing observations remain null; retained metadata never proves depth-content freshness. */
	inline nlohmann::json SourceStatus(const VRHybridCulling::SourceSnapshot& a_snapshot)
	{
		using nlohmann::json;
		json result{
			{ "available", a_snapshot.available }, { "current", a_snapshot.current },
			{ "valid", a_snapshot.valid }, { "busy", a_snapshot.busy },
			{ "validity", a_snapshot.validity }, { "stage", a_snapshot.stage },
			{ "cullingEpoch", a_snapshot.cullingEpoch }, { "source", nullptr },
			{ "eyes", nullptr }, { "pyramid", nullptr }, { "observedCamera", nullptr }
		};
		if (!a_snapshot.available)
			return result;
		const auto& source = a_snapshot.source;
		const char* phase = "unknown";
		if (source.phase == VRHybridCullingSnapshot::Phase::NativeDownscale)
			phase = "native_downscale_observation";
		else if (source.phase == VRHybridCullingSnapshot::Phase::NativeReadback)
			phase = "native_readback_observation";
		result["source"] = {
			{ "viewIdentity", source.view }, { "resourceGeneration", source.resourceGeneration },
			{ "frame", source.frame }, { "phase", phase }, { "contentFreshnessProven", false },
			{ "width", source.width }, { "height", source.height },
			{ "textureFormatDXGI", source.textureFormat }, { "viewFormatDXGI", source.viewFormat },
			{ "sampleCount", source.sampleCount }, { "convention", "standard" },
			{ "nearDepth", source.nearDepth }, { "farDepth", source.farDepth },
			{ "maskPolicy", "zero_is_untrusted" }
		};
		result["eyes"] = json::array();
		for (const auto& eye : a_snapshot.eyes)
			result["eyes"].push_back({ { "x", eye.x }, { "y", eye.y }, { "width", eye.width }, { "height", eye.height } });
		result["pyramid"] = {
			{ "width", a_snapshot.pyramid.width }, { "height", a_snapshot.pyramid.height },
			{ "mipCount", a_snapshot.pyramid.mipCount }, { "sourceReduction", a_snapshot.pyramid.sourceReduction },
			{ "eyeLayers", VRHybridCullingPolicy::kEyeCount }, { "logicalBytes", a_snapshot.logicalPyramidBytes },
			{ "memoryAccounting", "uncompressed_r32_float_texels_not_driver_vram" }
		};
		result["observedCamera"] = {
			{ "viewProjection", a_snapshot.viewProjection },
			{ "unjitteredProjection", a_snapshot.unjitteredProjection },
			{ "cameraAdjust", a_snapshot.cameraAdjust },
			{ "matrixConvention", "viewProjection_transposed_for_shader_unjitteredProjection_native" }
		};
		return result;
	}

	/** Preserve every reason, including zero counts, for comparable measurement windows. */
	template <std::size_t Size>
	inline nlohmann::json ReasonCounts(const std::array<const char*, Size>& a_names, const std::array<std::uint64_t, Size>& a_counts)
	{
		nlohmann::json result = nlohmann::json::object();
		for (std::size_t index = 0; index < Size; ++index)
			result[a_names[index]] = a_counts[index];
		return result;
	}

	/** Serialize one diagnostic observation without changing culling or capture state. */
	inline nlohmann::json BuildStatus(const VRDepthCullingTemporal::Status& depthCullingTemporal,
		const VRHybridCulling::Status& hybridCulling)
	{
		using nlohmann::json;
		const auto stageTimingStatus = [](const auto& a_timing) {
			json bounds = VRDepthCullingTelemetry::DurationUpperBoundsNanoseconds;
			bounds.push_back(nullptr);
			return json{
				{ "samples", a_timing.samples },
				{ "totalNanoseconds", a_timing.totalNanoseconds },
				{ "maximumNanoseconds", a_timing.maximumNanoseconds },
				{ "meanNanoseconds", a_timing.samples ? json(static_cast<double>(a_timing.totalNanoseconds) / a_timing.samples) : json(nullptr) },
				{ "durationHistogramNanoseconds", { { "upperBounds", std::move(bounds) }, { "counts", a_timing.durationHistogram } } }
			};
		};
		return {
			{ "installed", depthCullingTemporal.installed },
			{ "hybridInstalled", depthCullingTemporal.hybridInstalled },
			{ "cullingEnabled", depthCullingTemporal.cullingEnabled },
			{ "telemetryEnabled", depthCullingTemporal.telemetryEnabled },
			{ "telemetryFrozen", depthCullingTemporal.telemetryFrozen },
			{ "policy", VRDepthCullingTemporal::GetModeName(depthCullingTemporal.mode) },
			{ "cullingEpoch", depthCullingTemporal.cullingEpoch },
			{ "measurementWindow", { { "id", depthCullingTemporal.measurementWindowId },
									   { "startEpoch", depthCullingTemporal.measurementStartEpoch },
									   { "startFrame", depthCullingTemporal.measurementStartFrame },
									   { "current", depthCullingTemporal.measurementWindowCurrent },
									   { "requiresExplicitReset", true } } },
			{ "nativeVisibility", { { "batches", depthCullingTemporal.nativeVisibility.batches },
									  { "emptyBatches", depthCullingTemporal.nativeVisibility.emptyBatches },
									  { "unreadableBatches", depthCullingTemporal.nativeVisibility.unreadableBatches },
									  { "testedObjects", depthCullingTemporal.nativeVisibility.testedObjects },
									  { "occludedBeforeRecovery", depthCullingTemporal.nativeVisibility.occludedBeforeRecovery },
									  { "visibleBeforeRecovery", depthCullingTemporal.nativeVisibility.visibleBeforeRecovery },
									  { "occludedAfterRecovery", depthCullingTemporal.nativeVisibility.occludedAfterRecovery },
									  { "visibleAfterRecovery", depthCullingTemporal.nativeVisibility.visibleAfterRecovery },
									  { "semantics", "native_cpu_results_before_and_after_recovery_excludes_hybrid_readbacks" } } },
			{ "cpuTimings", { { "nativeReadback", stageTimingStatus(depthCullingTemporal.nativeReadback) },
								{ "outerDownscale", stageTimingStatus(depthCullingTemporal.outerDownscale) },
								{ "replayDownscale", stageTimingStatus(depthCullingTemporal.replayDownscale) },
								{ "nativeProducer", stageTimingStatus(depthCullingTemporal.nativeProducer) },
								{ "scopeSemantics", "inclusive_cpu_wall_time_do_not_sum_nested_stages" } } },
			{ "hybrid", {
							{ "state", hybridCulling.state },
							{ "effectiveBackend", hybridCulling.effectiveBackend },
							{ "proofBiasComparison", { { "requested", hybridCulling.proofVariantRequested },
														 { "active", hybridCulling.proofVariantActive },
														 { "baselineAvailable", hybridCulling.guardedBaselineAvailable },
														 { "baselineAvailability", hybridCulling.guardedBaselineAvailability },
														 { "requestedRevision", hybridCulling.proofSelectionRevision },
														 { "activeRevision", hybridCulling.proofActiveRevision } } },
							{ "depthReductionComparison", { { "requestedSourceReduction", hybridCulling.sourceReductionRequested },
															  { "activeSourceReduction", hybridCulling.sourceReductionActive },
															  { "state", hybridCulling.depthComparisonState },
															  { "largeSourceFallback", hybridCulling.sourceReductionRequested == 2 && hybridCulling.sourceReductionActive == 4 },
															  { "requestedRevision", hybridCulling.proofSelectionRevision },
															  { "activeRevision", hybridCulling.proofActiveRevision } } },
							{ "fallbackReason", hybridCulling.fallbackReason },
							{ "historyRejectionReason", hybridCulling.historyRejectionReason },
							{ "submittedBatches", hybridCulling.submittedBatches },
							{ "acceptedBatches", hybridCulling.acceptedBatches },
							{ "invalidatedBatches", hybridCulling.invalidatedBatches },
							{ "unreadableBatches", hybridCulling.unreadableBatches },
							{ "fallbackBatches", hybridCulling.fallbackBatches },
							{ "promotedObjects", hybridCulling.promotedObjects },
							{ "lastObjectCount", hybridCulling.lastObjectCount },
							{ "submittedObjects", hybridCulling.submittedObjects },
							{ "testedObjects", hybridCulling.testedObjects },
							{ "objectCountSemantics", "submitted_gpu_candidates_and_examined_cpu_readback_records" },
							{ "acceptedOccludedObjects", hybridCulling.acceptedOccludedObjects },
							{ "acceptedVisibleObjects", hybridCulling.acceptedVisibleObjects },
							{ "fallbackReasonCounts", ReasonCounts(VRHybridCulling::FallbackReasons, hybridCulling.fallbackReasonCounts) },
							{ "historyRejectionReasonCounts", ReasonCounts(VRHybridCulling::HistoryRejectionReasons, hybridCulling.historyRejectionReasonCounts) },
							{ "traversalDiagnostics", { { "enabled", hybridCulling.traversalDiagnosticsEnabled },
														  { "available", hybridCulling.traversalDiagnosticsAvailable },
														  { "availability", hybridCulling.traversalDiagnosticsAvailability },
														  { "submittedBatches", hybridCulling.traversalSubmittedBatches },
														  { "unavailableBatches", hybridCulling.traversalUnavailableBatches },
														  { "batches", hybridCulling.traversalBatches },
														  { "objects", hybridCulling.traversal.objects },
														  { "notReadyBatches", hybridCulling.traversalNotReadyBatches },
														  { "failedBatches", hybridCulling.traversalFailedBatches },
														  { "discardedBatches", hybridCulling.traversalDiscardedBatches },
														  { "depthLoads", hybridCulling.traversal.depthLoads },
														  { "faceRegions", hybridCulling.traversal.faceRegions },
														  { "faceTriangles", hybridCulling.traversal.faceTriangles },
														  { "planeProofs", hybridCulling.traversal.planeProofs },
														  { "polygonClips", hybridCulling.traversal.polygonClips },
														  { "faceBiasOnlyProofs", hybridCulling.traversal.faceBiasOnlyProofs },
														  { "triangleBiasOnlyProofs", hybridCulling.traversal.triangleBiasOnlyProofs },
														  { "eyeReasons", ReasonCounts(VRHybridCullingDiagnostics::Reasons, hybridCulling.traversal.eyeReasons) },
														  { "semantics", "accepted_history_only_first_decisive_reason_per_eye_second_eye_may_be_not_tested_work_sums_both_eyes" } } },
							{ "sourceSnapshot", SourceStatus(hybridCulling.snapshot) },
							{ "droppedSourceSnapshots", hybridCulling.droppedSourceSnapshots },
							{ "resources", { { "pipelineBuildAttempts", hybridCulling.pipelineBuildAttempts },
											   { "pipelineBuilds", hybridCulling.pipelineBuilds },
											   { "pipelineRecreations", hybridCulling.pipelineRecreations },
											   { "pyramidAllocations", hybridCulling.pyramidAllocations },
											   { "pyramidBuilds", hybridCulling.pyramidBuilds },
											   { "pyramidDispatches", hybridCulling.pyramidDispatches },
											   { "boundsDispatches", hybridCulling.boundsDispatches },
											   { "logicalPyramidBytesHighWater", hybridCulling.logicalPyramidBytesHighWater } } },
							{ "cpuTimings", { { "prepare", stageTimingStatus(hybridCulling.prepare) },
												{ "dispatch", stageTimingStatus(hybridCulling.dispatch) },
												{ "readback", stageTimingStatus(hybridCulling.readback) },
												{ "readbackSemantics", "hybrid_validation_after_native_readback" } } },
						} },
			{ "envelopeMisses", depthCullingTemporal.envelopeMisses },
			{ "recoveryAttempts", depthCullingTemporal.recoveryAttempts },
			{ "objectsInspected", depthCullingTemporal.objectsInspected },
			{ "invalidTransforms", depthCullingTemporal.invalidTransforms },
			{ "invalidMotionEnvelopes", depthCullingTemporal.invalidMotionEnvelopes },
			{ "frustumTests", depthCullingTemporal.frustumTests },
			{ "totalEligible", depthCullingTemporal.totalEligible },
			{ "totalPromoted", depthCullingTemporal.totalPromoted },
			{ "totalDurationNanoseconds", depthCullingTemporal.totalDurationNanoseconds },
			{ "maximumDurationNanoseconds", depthCullingTemporal.maximumDurationNanoseconds },
			{ "durationHistogramNanoseconds", {
												  { "upperBounds", [] {
													   json bounds = VRDepthCullingTelemetryPolicy::DurationUpperBoundsNanoseconds;
													   bounds.push_back(nullptr);
													   return bounds;
												   }() },
												  { "counts", depthCullingTemporal.durationHistogram },
											  } },
			{ "lastObjectCount", depthCullingTemporal.lastObjectCount },
			{ "lastEligibleCount", depthCullingTemporal.lastEligibleCount },
			{ "lastPromotedCount", depthCullingTemporal.lastPromotedCount },
		};
	}
}

#endif
