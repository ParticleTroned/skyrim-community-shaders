#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED
#	include "MonoExperiments.h"
#	include "MonoSettingsSerialization.h"
#	include "NativeEvaluationLayout.h"
#	include "RuntimeExecutionEvidence.h"

namespace NeuralRendering
{
	/** Observation and admission used by one execution, retained independently of live scene state. */
	struct MonoFaceDetailEvidence
	{
		std::uint32_t sourceFrame = UINT32_MAX, observedRegions = 0;
		bool current = false, active = false;
	};

	struct MonoExecutionRecord
	{
		std::uint32_t frame = UINT32_MAX;
		bool prewarm = false, submitted = false, committed = false, historyReset = false;
		NativeEvaluationLayout layout{};
		MonoSettings settings{};
		MonoExperiments experiments{};
		MonoFaceDetailEvidence faceDetail{};
		RuntimeExecutionEvidence native{};
	};

	[[nodiscard]] inline nlohmann::json DescribeMonoFaceDetail(const MonoFaceDetailEvidence& value)
	{
		using Json = nlohmann::json;
		return { { "sourceFrame", value.sourceFrame == UINT32_MAX ? Json(nullptr) : Json(value.sourceFrame) },
			{ "observedRegions", value.observedRegions }, { "current", value.current }, { "active", value.active }, { "fixedModelGrid", true } };
	}

	/** Export actual SDK inputs beside the configuration that requested them. */
	[[nodiscard]] inline nlohmann::json DescribeMonoExecution(const MonoExecutionRecord& record)
	{
		using Json = nlohmann::json;
		const auto& native = record.native;
		const auto optional = [](const auto& value) -> Json { return value ? Json(*value) : Json(nullptr); };
		return { { "frame", record.frame }, { "prewarm", record.prewarm }, { "submitted", record.submitted },
			{ "committed", record.committed }, { "historyReset", record.historyReset }, { "settings", Json(record.settings) },
			{ "experiments", DescribeMonoExperiments(record.experiments) }, { "faceDetail", DescribeMonoFaceDetail(record.faceDetail) },
			{ "inputWidth", record.layout.creation.input.width }, { "inputHeight", record.layout.creation.input.height },
			{ "outputWidth", record.layout.creation.output.width }, { "outputHeight", record.layout.creation.output.height },
			{ "jitterPixels", record.layout.jitterPixels }, { "scalingRatio", record.layout.ScalingRatio() },
			{ "nativeControlMask", record.layout.nativeControlMask }, { "motionVectorScale", record.layout.motionVectorScale },
			{ "featureUpscaling", record.layout.featureUpscaling },
			{ "createAttempted", native.createAttempted }, { "createSucceeded", native.createSucceeded },
			{ "evaluateAttempted", native.evaluateAttempted }, { "evaluateSucceeded", native.evaluateSucceeded },
			{ "createReason", native.createReason ? Json(native.createReason) : Json(nullptr) },
			{ "createResult", optional(native.createResult) }, { "evaluateResult", optional(native.evaluateResult) },
			{ "createCpuMicroseconds", optional(native.createCpuMicroseconds) }, { "evaluateCpuMicroseconds", optional(native.evaluateCpuMicroseconds) } };
	}
}
#endif
