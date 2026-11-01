#include "ScreenshotNeuralDiagnostics.h"
#include "Upscaling/NeuralRendering/ExposureCapture.h"
#include "Upscaling/NeuralRendering/MonoRenderer.h"

#include <algorithm>
#include <vector>

namespace CSX::ScreenshotPolicy
{
	namespace
	{
		using namespace NeuralRendering::Color;
		using Json = nlohmann::json;
		Json Stamp(const ExposureStamp& key)
		{
			return { { "frame", key.frame }, { "epoch", key.epoch }, { "sequence", key.sequence }, { "ambiguous", key.ambiguous } };
		}
		Json Evidence(const ExposureEvidenceLookup& value)
		{
			Json result = { { "key", Stamp(value.key) }, { "available", value.evidence.has_value() && value.evidence->readbackComplete }, { "reason", value.reason } };
			if (value.evidence) {
				const auto& sample = *value.evidence;
				result["sample"] = { { "frame", sample.stamp.frame }, { "epoch", sample.stamp.epoch }, { "sequence", sample.stamp.sequence },
					{ "producer", sample.producer }, { "readbackComplete", sample.readbackComplete }, { "values", sample.values }, { "texels", sample.texels },
					{ "gammaKnown", sample.gammaKnown }, { "gammaExponent", sample.frameGammaExponent },
					{ "sourceWidth", sample.sourceWidth }, { "sourceHeight", sample.sourceHeight },
					{ "sourceFormat", sample.sourceFormat }, { "sourceViewFormat", sample.sourceViewFormat }, { "sourceMip", sample.sourceMip } };
			}
			return result;
		}
	}

	nlohmann::json BuildMonoNeuralEvidence(std::uint32_t frame)
	{
		const auto state = NeuralRendering::MonoRenderer::Instance().Snapshot();
		const auto engine = ExposureCapture::Instance().GetSourceFrameEvidence(frame);
		const bool matched = state.presentationFrame == frame;
		return { { "schemaVersion", 1 }, { "available", matched }, { "viewCount", 1 }, { "sourceWorldFrame", frame },
			{ "reason", matched ? "" : "no_neural_result_presented_for_source_frame" }, { "status", state.status },
			{ "evaluationFrame", state.evaluationFrame }, { "presentationFrame", state.presentationFrame },
			{ "sourceWidth", state.sourceWidth }, { "sourceHeight", state.sourceHeight },
			{ "modelWidth", state.modelWidth }, { "modelHeight", state.modelHeight }, { "modelResolutionPercent", state.effectivePercent },
			{ "packedRegions", state.packedRegions }, { "packedCoverageProven", state.packedCoverageProven },
			{ "engineExposure", Stamp(engine.key) },
			{ "neuralExposure", Stamp({ state.exposureFrame, state.exposureEpoch, state.exposureSequence, false }) } };
	}

	DiagnosticSnapshot RetainNeuralDiagnostics(const nlohmann::json& evidence, DiagnosticSnapshot execution)
	{
#ifdef DEVBENCH_BRIDGE_ENABLED
		if (!execution) {
			const auto snapshot = NeuralRendering::MonoRenderer::Instance().Diagnostics(evidence.value("sourceWorldFrame", UINT32_MAX));
			execution = [snapshot] { return snapshot; };
		}
#endif
		using Lease = NeuralRendering::Color::ExposureEvidenceHistory::Lease;
		std::vector<std::pair<ExposureStamp, Lease>> retained;
		for (const auto* name : { "engineExposure", "neuralExposure" }) {
			const auto source = evidence.find(name);
			if (source == evidence.end() || !source->is_object() || !source->value("sequence", 0ull)) continue;
			const ExposureStamp key{ source->at("frame").get<uint32_t>(), source->at("epoch").get<uint64_t>(), source->at("sequence").get<uint64_t>(), source->value("ambiguous", false) };
			if (std::ranges::any_of(retained, [&](const auto& value) { return ExposureEvidenceHistory::SameKey(value.first, key); })) continue;
			retained.emplace_back(key, ExposureCapture::Instance().PinEvidence(key));
		}
		return [retained = std::move(retained), evidence, execution = std::move(execution)] {
			Json result = { { "schemaVersion", 1 }, { "finalized", true }, { "source", evidence },
				{ "measurementPolicy", "mono_execution_and_exposure" }, { "exposures", Json::array() } };
			for (const auto& [key, lease] : retained) {
				const auto sample = lease ? lease->Snapshot() : ExposureEvidenceLookup{ key, {}, "capture_retention_capacity_exhausted" };
				auto serialized = Evidence(sample);
				if (!serialized.at("available").get<bool>() && std::string_view(sample.reason) == "readback_pending")
					serialized["reason"] = "readback_pending_at_capture_finalization";
				result["exposures"].push_back(std::move(serialized));
			}
			if (execution) result["executionEvidence"] = execution();
			return result;
		};
	}
}
