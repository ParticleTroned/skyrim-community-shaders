#include "ScreenshotNeuralDiagnostics.h"
#include "Upscaling/NeuralRendering/CaptureEvidence.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace CSX::ScreenshotPolicy
{
	namespace
	{
		using namespace NeuralRendering::Color;
		using Json = nlohmann::json;

		struct Companions
		{
			std::string transaction;
			struct Exposure
			{
				ExposureStamp key;
				ExposureEvidenceHistory::Lease lease;
			};
			struct Batch
			{
				MeasurementBatchKey key;
				MeasurementBatchHistory<Measurement>::Lease lease;
			};
			std::vector<Exposure> exposures;
			std::vector<Batch> batches;
			bool diagnostics = false;
			DiagnosticSnapshot execution;

			void PinExposure(const Json& source)
			{
				if (!source.contains("sequence") || source.at("sequence") == 0)
					return;
				const ExposureStamp key{ source.at("frame").get<std::uint32_t>(),
					source.at("epoch").get<std::uint64_t>(), source.at("sequence").get<std::uint64_t>(), false };
				if (std::any_of(exposures.begin(), exposures.end(), [&](const auto& e) { return ExposureEvidenceHistory::SameKey(e.key, key); }))
					return;
				if (exposures.size() == 9)
					throw std::invalid_argument("too_many_exposure_companions");
				exposures.push_back({ key, ExposureCapture::Instance().PinEvidence(key) });
			}

			void PinBatch(const Json& region)
			{
				const MeasurementBatchKey key{ region.at("measurementBatchId").get<std::uint64_t>(),
					region.at("revision").get<std::uint64_t>(), region.at("generation").get<std::uint64_t>(),
					region.at("frame").get<std::uint32_t>(), region.at("sourceWorldFrame").get<std::uint32_t>(),
					region.at("insertionPoint").get<std::uint32_t>(), region.at("expectedMeasurementSlotMask").get<std::uint32_t>(),
					region.at("atomicColourBatch").get<bool>() };
				if (const auto found = std::find_if(batches.begin(), batches.end(), [&](const auto& b) { return b.key.id == key.id; });
					found != batches.end()) {
					if (found->key != key)
						throw std::invalid_argument("conflicting_measurement_batch_identity");
					return;
				}
				if (batches.size() == 8)
					throw std::invalid_argument("too_many_measurement_companions");
				batches.push_back({ key, Registry::Instance().PinMeasurementBatch(key) });
			}

			Json Snapshot() const
			{
				Json result{ { "schemaVersion", 1 }, { "transactionId", transaction }, { "finalized", true },
					{ "exposures", Json::array() }, { "measurementBatches", Json::array() },
					{ "measurementRequests", Json::array() },
					{ "measurementPolicy", diagnostics ? "exact_private_batches" : "diagnostics_disabled" } };
				for (const auto& item : exposures) {
					const auto value = item.lease ? item.lease->Snapshot() :
					                                ExposureEvidenceLookup{ item.key, {}, "capture_retention_capacity_exhausted" };
					auto serialized = ExposureLookupEvidenceJson(value);
					if (!serialized.at("available").get<bool>() && value.reason == std::string_view("readback_pending"))
						serialized["reason"] = "readback_pending_at_capture_finalization";
					result["exposures"].push_back(std::move(serialized));
				}
				for (const auto& item : batches) {
					const auto batch = item.lease ? item.lease->Snapshot() : MeasurementBatch<Measurement>{};
					const char* reason = !item.key.Valid() ? "invalid_measurement_batch_key" :
					                     !item.lease       ? "capture_retention_capacity_exhausted" :
					                     batch.invalid     ? "measurement_batch_invalid_or_expired" :
					                     batch.Complete()  ? "" :
					                                         "measurement_incomplete_at_capture_finalization";
					const auto& k = item.key;
					result["measurementRequests"].push_back({ { "key", { { "measurementBatchId", k.id }, { "revision", k.revision }, { "generation", k.generation },
																		   { "frame", k.frame }, { "sourceWorldFrame", k.sourceWorldFrame }, { "insertionPoint", k.insertion },
																		   { "expectedMeasurementSlotMask", k.expectedSlotMask }, { "atomicColourBatch", k.atomicStereo } } },
						{ "available", batch.Complete() }, { "reason", reason }, { "receivedMeasurementSlotMask", batch.receivedSlotMask } });
					if (batch.Complete())
						result["measurementBatches"].push_back(MeasurementBatchEvidenceJson(batch));
				}
				if (execution)
					result["executionEvidence"] = execution();
				return result;
			}
		};
	}

	DiagnosticSnapshot RetainNeuralDiagnostics(const nlohmann::json& evidence, DiagnosticSnapshot execution)
	{
		try {
			if (!evidence.is_object() || !evidence.value("available", false))
				return {};
			Companions companions;
			companions.execution = std::move(execution);
			companions.transaction = evidence.at("transactionId").get<std::string>();
			companions.diagnostics = evidence.at("configuration").at("color").at("experiments").at("diagnostics").get<bool>();
			companions.PinExposure(evidence.value("engineExposure", Json::object()));
			for (const auto* eye : { "left", "right" }) {
				const auto& outcome = evidence.at(eye);
				companions.PinExposure(outcome.at("exposure"));
				const auto& regions = outcome.at("physicalRegions");
				if (!regions.is_array() || regions.size() > 4)
					throw std::invalid_argument("invalid_physical_region_count");
				for (const auto& region : regions) {
					companions.PinExposure(region.at("exposure"));
					if (companions.diagnostics)
						companions.PinBatch(region);
				}
			}
			return [retained = std::move(companions)] { return retained.Snapshot(); };
		} catch (const std::exception& error) {
			return [reason = std::string(error.what())] {
				return Json{ { "schemaVersion", 1 }, { "finalized", true }, { "available", false },
					{ "reason", "companion_retention_failed" }, { "detail", reason } };
			};
		}
	}
}
