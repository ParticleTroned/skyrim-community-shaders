#pragma once

#include "CharacterPreparationEvidence.h"
#include "ExecutionEvidenceJson.h"

namespace NeuralRendering::Evidence
{
	inline Json CharacterPreparationKeyJson(const CharacterPreparationKey& key)
	{
		return { { "frame", key.frame }, { "sourceWorldFrame", key.sourceWorldFrame }, { "eye", key.eye },
			{ "logicalSlot", key.featureSlot }, { "generation", key.generation }, { "contentSerial", key.contentSerial },
			{ "settingsKey", key.settingsKey }, { "captureEpoch", key.captureEpoch }, { "viewport", ViewportJson(key.crop) },
			{ "capturedJitterPixels", { key.jitterX, key.jitterY } }, { "outputIsJittered", key.outputIsJittered } };
	}

	inline Json CharacterCpuMilliseconds(bool available, double milliseconds, const char* absentReason)
	{
		return { { "state", available ? "complete" : "unavailable" }, { "reason", available ? "" : absentReason },
			{ "milliseconds", available ? Json(milliseconds) : Json(nullptr) }, { "clock", "cpu_steady_clock" } };
	}

	/** Capture-owned character facts, with optional readbacks identified by their original producer. */
	inline Json CharacterPreparationJson(const std::shared_ptr<const CharacterPreparationEvidence>& evidence)
	{
		if (!evidence)
			return { { "available", false }, { "reason", "preparation_evidence_unavailable" } };
		const auto& e = *evidence;
		Json source{ { "available", false }, { "reason", "source_not_captured_in_epoch" } };
		if (e.source) {
			const auto& s = *e.source;
			source = { { "available", true }, { "producerBoundary", "post_terrain_pre_decal_category_depth_capture" },
				{ "sourceWorldFrame", s.sourceWorldFrame }, { "captureEpoch", s.captureEpoch }, { "boundsCaptureSerial", s.captureSerial },
				{ "eyeGrid", { { "width", s.eyeWidth }, { "height", s.height } } }, { "eyeCount", s.eyeCount },
				{ "authoredGrid", { { "width", static_cast<std::uint64_t>(s.eyeWidth) * s.eyeCount }, { "height", s.height } } },
				{ "authoredEyeOrigin", { static_cast<std::uint64_t>(s.eyeWidth) * e.key.eye, 0 } },
				{ "copiedRects", Json::array({ SubrectJson(s.copiedRects[0]), SubrectJson(s.copiedRects[1]) }) },
				{ "jitterPixels", { s.jitterX, s.jitterY } }, { "empty", s.empty },
				{ "dispatchedPixels", s.dispatchedPixels }, { "dispatchedThreads", s.dispatchedThreads },
				{ "logicalTextureBytes", s.logicalTextureBytes }, { "copiedLogicalBytes", s.copiedLogicalBytes },
				{ "logicalBoundsBytes", s.logicalBoundsBytes }, { "boundsReadbackBytes", s.boundsReadbackBytes },
				{ "boundsDispatchedThreads", s.boundsDispatchedThreads },
				{ "detectionCalls", s.detectionCpuAvailable ? Json(s.detectionCalls) : Json(nullptr) },
				{ "detectionCpuScope", s.detectionCpuScope },
				{ "detectionCpu", CharacterCpuMilliseconds(s.detectionCpuAvailable, s.detectionCpuMs, "source_detection_not_observed") },
				{ "captureCpuInclusive", CharacterCpuMilliseconds(true, s.captureCpuMs, "") },
				{ "categoryCapture", PassTimingJson(s.captureTiming) }, { "earlyBounds", PassTimingJson(s.boundsTiming) } };
		}
		Json support{ { "state", "unavailable" }, { "reason", "coverage_evidence_unavailable" }, { "pixels", nullptr } };
		if (e.support) {
			const auto s = e.support->Snapshot();
			const char* state = s.state == CharacterSupportState::Ready ? "ready" : s.state == CharacterSupportState::Pending ? "pending" :
			                                                                    s.state == CharacterSupportState::Failed      ? "failed" :
			                                                                                                                    "unavailable";
			support = { { "state", state }, { "reason", s.reason }, { "producer", CharacterPreparationKeyJson(e.support->Key()) },
				{ "pixels", s.state == CharacterSupportState::Ready ? Json(s.pixels) : Json(nullptr) },
				{ "source", "actual_mask_counter_gpu_readback" } };
		}
		Json regions = Json::array();
		if (e.prepared && e.requiresEvaluation) {
			if (e.computeRegions.count == 0)
				regions.push_back(SubrectJson(e.computeSubrect));
			else
				for (std::uint32_t i = 0; i < std::min<std::uint32_t>(e.computeRegions.count, static_cast<uint32_t>(e.computeRegions.regions.size())); ++i)
					regions.push_back(SubrectJson(e.computeRegions.regions[i]));
		}
		return { { "available", true }, { "key", CharacterPreparationKeyJson(e.key) }, { "outcome", e.outcome },
			{ "prepared", e.prepared }, { "requiresEvaluation", e.requiresEvaluation }, { "reused", e.reused },
			{ "sourceCapture", std::move(source) }, { "computeSubrect", SubrectJson(e.computeSubrect) }, { "regions", std::move(regions) },
			{ "maskSupport", std::move(support) }, { "dirtyDispatchRect", SubrectJson(e.dirtyDispatchRect) },
			{ "dispatchedPixels", e.dispatchedPixels }, { "dispatchedThreads", e.dispatchedThreads }, { "clearedPixels", e.clearedPixels },
			{ "logicalMaskBytes", e.logicalMaskBytes }, { "logicalDiagnosticBytes", e.logicalDiagnosticBytes }, { "copiedReadbackBytes", e.copiedReadbackBytes },
			{ "byteAccounting", "logical_texels_and_buffer_payload_excludes_driver_padding_and_queries" },
			{ "bounds", { { "state", e.boundsStatus }, { "ready", e.boundsReady }, { "used", e.boundsUsed },
							{ "pollCpu", CharacterCpuMilliseconds(e.boundsPollCpuAvailable, e.boundsPollCpuMs, "not_polled_this_preparation") } } },
			{ "timing", { { "preparationCpuInclusive", CharacterCpuMilliseconds(true, e.preparationCpuMs, "") },
							{ "roiPlanningCpuInclusive", CharacterCpuMilliseconds(e.roiPlanningCpuAvailable, e.roiPlanningCpuMs, "preparation_reused_or_planning_not_reached") },
							{ "explicitWait", CharacterCpuMilliseconds(e.waitAvailable, 0.0, e.waitReason) }, { "mask", PassTimingJson(e.maskTiming) } } } };
	}
}
