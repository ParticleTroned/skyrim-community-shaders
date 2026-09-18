#pragma once

#include "ColorPipeline.h"
#include "ExecutionEvidence.h"
#include <array>
#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>

namespace NeuralRendering
{
	/** CPU evidence from the latched input transaction, independent of GPU diagnostics. */
	struct CaptureInputs
	{
		bool valid = false;
		std::uint32_t frame = 0, sourceWorldFrame = 0, insertion = 0, route = 0;
		std::uint64_t generation = 0;
		std::uint64_t sourceTransactionId = 0;
		std::optional<std::uint64_t> captureEpoch;
		Color::Configuration configuration{};
		std::uint32_t slotMask = 0, attemptedMask = 0, succeededMask = 0;
		std::array<Color::Observation, 8> slots{};
		std::array<std::shared_ptr<ExecutionEvidence>, 8> executions{};
		std::uint32_t executionCount = 0;
		std::uint32_t executionEvidenceFailures = 0;

		[[nodiscard]] bool Matches(std::uint32_t requestedRoute, std::uint32_t requestedFrame,
			std::uint32_t worldFrame, std::uint64_t requestedGeneration, std::uint32_t requestedInsertion) const noexcept
		{
			return valid && route == requestedRoute && frame == requestedFrame &&
			       sourceWorldFrame == worldFrame && generation == requestedGeneration && insertion == requestedInsertion;
		}
	};
}

namespace NeuralRendering::Color
{
	/** Serialize frozen values using the same colour contract as the DevBench tool. */
	nlohmann::json ConfigurationEvidenceJson(const Configuration& configuration);
	nlohmann::json ObservationEvidenceJson(const Observation& observation);
	nlohmann::json ExposureEvidenceJson(const ExposureEvidence& evidence);
	nlohmann::json MeasurementBatchEvidenceJson(const MeasurementBatch<Measurement>& batch);
	nlohmann::json ExposureLookupEvidenceJson(const ExposureEvidenceLookup& lookup);
}
