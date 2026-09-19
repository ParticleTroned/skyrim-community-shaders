#pragma once

#include "ExecutionEvidence.h"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace NeuralRendering::Evidence
{
	using Json = nlohmann::json;
	inline Json PassTimingJson(const Util::PassTimingHandle& handle);
	template <class T>
	inline Json OptionalJson(const std::optional<T>& value)
	{
		return value ? Json(*value) : Json(nullptr);
	}
	inline Json ExtentJson(const UpscalingDLSS::Extent& value)
	{
		return { { "width", value.width }, { "height", value.height } };
	}
	inline Json RectJson(const UpscalingDLSS::Rect& value)
	{
		return { { "left", value.left }, { "top", value.top }, { "right", value.right }, { "bottom", value.bottom } };
	}
	inline Json SubrectJson(const ComputeSubrect& value)
	{
		return { { "x", value.baseX }, { "y", value.baseY }, { "width", value.width }, { "height", value.height } };
	}
	inline Json ViewportJson(const UpscalingDLSS::ViewportCrop& value)
	{
		return { { "fullInput", ExtentJson(value.fullInput) }, { "input", RectJson(value.input) },
			{ "fullOutput", ExtentJson(value.fullOutput) }, { "output", RectJson(value.output) } };
	}
	inline Json ContextJson(const ExecutionContext& value)
	{
		return { { "sourceTransactionId", value.sourceTransactionId },
			{ "mode", value.renderingMode ? Json(GetRenderingModeName(*value.renderingMode)) : Json(nullptr) },
			{ "fovOnly", OptionalJson(value.fovOnly) }, { "renderscaleFov", OptionalJson(value.renderscaleFov) }, { "captureEpoch", OptionalJson(value.captureEpoch) },
			{ "configurationEpoch", OptionalJson(value.configurationEpoch) }, { "sourceContext", value.sourceContext },
			{ "dlssRouteGrid", value.dlssViewportCrop ? ViewportJson(*value.dlssViewportCrop) : Json(nullptr) },
			{ "dlssRouteGridRole", "caller_contract_actual_dispatches_recorded_separately" },
			{ "sourceColorOrigin", OptionalJson(value.sourceColorOrigin) }, { "sourceGuideOrigin", OptionalJson(value.sourceGuideOrigin) },
			{ "jitterPixels", OptionalJson(value.jitterPixels) }, { "jitterConvention", "engine_input_pixel_offset_dlss_negates_xy" },
			{ "motionVectorConvention", "engine_normalized_per_eye_scaled_by_nr_viewport_no_jitter_parameter_in_feature18" },
			{ "pixelCoordinates", "top_left_origin_integer_texels_half_open_rectangles" } };
	}
	/** Missing samples are null; clocks and inclusive/self semantics stay explicit. */
	inline Json PassTimingJson(const Util::PassTimingHandle& handle)
	{
		const auto value = Util::ReadPassTiming(handle);
		const auto sample = [](Util::PassTimingState state, const char* reason, float inclusive, float self, const char* clock) {
			const bool ready = state == Util::PassTimingState::Ready;
			return Json{ { "state", Util::PassTimingStateName(state) }, { "reason", reason }, { "clock", clock },
				{ "inclusiveMs", ready ? Json(inclusive) : Json(nullptr) }, { "selfMs", ready ? Json(self) : Json(nullptr) } };
		};
		return { { "cpu", sample(value.cpuState, value.cpuReason, value.cpuInclusiveMs, value.cpuSelfMs, "cpu_qpc") },
			{ "captureId", value.captureId },
			{ "gpu", sample(value.gpuState, value.gpuReason, value.gpuInclusiveMs, value.gpuSelfMs, "d3d11_context") },
			{ "profilerFrame", value.capturedFrame == std::numeric_limits<uint32_t>::max() ? Json(nullptr) : Json(value.capturedFrame) },
			{ "detailOnly", value.detailOnly } };
	}
	inline Json CpuTimingJson(const std::optional<uint64_t>& value)
	{
		return { { "state", value ? "complete" : "unavailable" }, { "microseconds", OptionalJson(value) }, { "clock", "cpu_steady_clock" } };
	}
	inline Json GpuTimingJson(const ExecutionTiming& value)
	{
		const char* state = value.state == ExecutionTimingState::Complete ? "complete" :
		                    value.state == ExecutionTimingState::Pending  ? "pending" :
		                    value.state == ExecutionTimingState::Failed   ? "failed" :
		                                                                    "unavailable";
		return { { "state", state }, { "microseconds", value.state == ExecutionTimingState::Complete ? OptionalJson(value.microseconds) : Json(nullptr) },
			{ "clock", "d3d12_nr_queue" }, { "reason", value.state == ExecutionTimingState::NotRequested ? "not_requested" : state } };
	}
	inline Json TextureJson(const ExecutionTexture& value)
	{
		return { { "capacityGrid", ExtentJson(value.extent) }, { "format", value.format }, { "work", SubrectJson(value.work) },
			{ "capacityPixels", static_cast<uint64_t>(value.extent.width) * value.extent.height },
			{ "workPixels", value.work.Area() }, { "capacityLogicalBytes", OptionalJson(value.allocationLogicalBytes) },
			{ "workLogicalBytes", OptionalJson(value.workLogicalBytes) } };
	}
	inline Json ExecutionJson(const ExecutionEvidence& evidence)
	{
		const auto& d = evidence.Descriptor();
		const auto s = evidence.Snapshot();
		Json regions = Json::array();
		Json waits = Json::array();
		for (uint32_t i = 0; i < std::min<uint32_t>(s.cpuWaitSampleCount, static_cast<uint32_t>(s.cpuWaitSamples.size())); ++i) {
			const auto& wait = s.cpuWaitSamples[i];
			waits.push_back({ { "operation", wait.operation ? Json(wait.operation) : Json(nullptr) },
				{ "microseconds", wait.microseconds }, { "result", wait.result }, { "error", wait.error },
				{ "timeoutMilliseconds", wait.timeoutMilliseconds }, { "clock", "cpu_steady_clock" } });
		}
		uint32_t actualEvaluations = 0;
		uint64_t activePixels = 0;
		uint64_t aggregateGpuMicroseconds = 0;
		bool aggregateComplete = true;
		for (uint32_t i = 0; i < std::min<uint32_t>(d.regionCount, static_cast<uint32_t>(d.regions.size())); ++i) {
			const auto& r = d.regions[i];
			const auto& o = s.regions[i];
			if (o.runtime.evaluateAttempted) {
				++actualEvaluations;
				activePixels += r.output.work.Area();
				aggregateComplete &= o.evaluationGpu.state == ExecutionTimingState::Complete && o.evaluationGpu.microseconds.has_value();
				if (o.evaluationGpu.state == ExecutionTimingState::Complete && o.evaluationGpu.microseconds)
					aggregateGpuMicroseconds += *o.evaluationGpu.microseconds;
			}
			regions.push_back({ { "physicalSlot", r.physicalSlot }, { "logicalSlot", r.logicalSlot }, { "eye", r.eye },
				{ "region", r.region }, { "regionIdentity", r.regionIdentity }, { "clusterIdentity", r.clusterIdentity },
				{ "source", ContextJson(r.context) }, { "nrInput", TextureJson(r.color) }, { "nrOutput", TextureJson(r.output) },
				{ "nrDepthGuide", TextureJson(r.depth) }, { "nrMotionGuide", TextureJson(r.motion) }, { "controlMask", TextureJson(r.controlMask) },
				{ "depthSourceFormat", r.depthSourceFormat }, { "depthViewFormat", r.depthViewFormat },
				{ "nrViewport", ViewportJson(r.viewportCrop) }, { "motionVectorScale", { r.motionVectorScaleX, r.motionVectorScaleY } },
				{ "characterSelection", r.characterVisualIsolation }, { "featureUpscaling", r.featureUpscaling },
				{ "createAttempted", o.runtime.createAttempted }, { "createSucceeded", o.runtime.createSucceeded },
				{ "createReason", o.runtime.createReason ? Json(o.runtime.createReason) : Json(nullptr) },
				{ "createResult", OptionalJson(o.runtime.createResult) }, { "evaluateResult", OptionalJson(o.runtime.evaluateResult) },
				{ "evaluationAttempted", o.runtime.evaluateAttempted }, { "evaluationSucceeded", o.runtime.evaluateSucceeded },
				{ "resourcesReady", o.resourcesReady }, { "privateOutputCommitted", o.privateOutputCommitted },
				{ "outputCopyEnqueued", o.outputCopyEnqueued }, { "colorRetainedLogicalBytes", OptionalJson(o.colorRetainedLogicalBytes) },
				{ "effectiveReset", o.effectiveReset }, { "resetReasonFlags", o.resetReasons }, { "rebuildReasonFlags", o.rebuildReasons },
				{ "newlyAllocatedLogicalBytes", o.allocationBytesKnown ? Json(o.newlyAllocatedLogicalBytes) : Json(nullptr) },
				{ "copiedLogicalBytes", o.copyBytesKnown ? Json(o.copiedLogicalBytes) : Json(nullptr) },
				{ "timing", { { "createCpu", CpuTimingJson(o.runtime.createCpuMicroseconds) },
								{ "sourceInputPreparation", PassTimingJson(r.context.inputPreparation) },
								{ "evaluationCpu", CpuTimingJson(o.runtime.evaluateCpuMicroseconds) }, { "evaluationGpu", GpuTimingJson(o.evaluationGpu) },
								{ "colorPreparation", PassTimingJson(o.colorPreparePass) }, { "reconstruction", PassTimingJson(o.colorReconstructPass) },
								{ "depthGuide", PassTimingJson(o.depthGuidePass) }, { "colorCopy", PassTimingJson(o.colorCopyPass) },
								{ "motionCopy", PassTimingJson(o.motionCopyPass) }, { "controlCopy", PassTimingJson(o.controlCopyPass) },
								{ "outputCopy", PassTimingJson(o.outputCopyPass) } } } });
		}
		return { { "submissionId", d.submissionId }, { "source", ContextJson(d.context) }, { "frame", d.frame },
			{ "sourceWorldFrame", d.sourceWorldFrame }, { "generation", d.generation }, { "colorRevision", d.colorRevision }, { "inputEpoch", d.inputEpoch },
			{ "route", d.route == FeatureSlotRoute::Main ? "main" : d.route == FeatureSlotRoute::Submit ? "submit" :
																										  "unexpected" },
			{ "legacyInsertionPoint", GetInsertionPointName(d.insertion) }, { "logicalEyeCount", d.logicalEyeCount },
			{ "plannedRegionCount", d.regionCount }, { "actualEvaluationCount", actualEvaluations }, { "activeEvaluationPixels", activePixels },
			{ "plannedPhysicalSlotMask", d.plannedPhysicalSlotMask }, { "attemptedPhysicalSlotMask", s.attemptedPhysicalSlotMask },
			{ "succeededPhysicalSlotMask", s.succeededPhysicalSlotMask }, { "privateCommittedPhysicalSlotMask", s.committedPhysicalSlotMask },
			{ "finished", s.finished }, { "succeeded", s.succeeded }, { "failureStage", s.failureStage }, { "evidenceFailed", s.evidenceFailed },
			{ "transportBypass", d.transportBypass }, { "regions", std::move(regions) },
			{ "timing", { { "wholeNrLegacy", PassTimingJson(s.wholePass) }, { "wholeFeatureBatchGpuLegacy", GpuTimingJson(s.batchGpu) },
							{ "aggregateEvaluationGpu", { { "state", actualEvaluations && aggregateComplete ? "complete" : "unavailable" },
															{ "microseconds", actualEvaluations && aggregateComplete ? Json(aggregateGpuMicroseconds) : Json(nullptr) },
															{ "clock", "d3d12_nr_queue" }, { "semantics", "sum_nonoverlapping_actual_evaluate_feature_intervals" } } },
							{ "preparationCpuInclusive", CpuTimingJson(s.preparationCpuMicroseconds) }, { "commitCpuInclusive", CpuTimingJson(s.commitCpuMicroseconds) },
							{ "resourceRetirementCpuInclusive", CpuTimingJson(s.resourceRetirementCpuMicroseconds) }, { "commandBeginCpuInclusive", CpuTimingJson(s.commandBeginCpuMicroseconds) },
							{ "explicitCpuWait", CpuTimingJson(s.cpuWaitMicroseconds) }, { "explicitCpuWaitCalls", s.cpuWaitCalls },
							{ "waitSamples", std::move(waits) }, { "droppedWaitSampleCount", s.cpuWaitSamplesDropped } } } };
	}
}
