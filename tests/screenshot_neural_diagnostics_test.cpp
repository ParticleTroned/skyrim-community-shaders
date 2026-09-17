#include "Features/ScreenshotNeuralDiagnostics.h"
#include "Features/Upscaling/NeuralRendering/CaptureEvidence.h"
#include <stdexcept>
#include <thread>
#include <vector>

using namespace NeuralRendering::Color;
using Json = nlohmann::json;
namespace
{
	MeasurementBatchHistory<Measurement> batches;
	ExposureEvidenceHistory exposures;
}
namespace NeuralRendering::Color
{
	// Substitute CPU publishers; retention and capture assembly are production code.
	Registry& Registry::Instance()
	{
		static Registry value;
		return value;
	}
	MeasurementBatchHistory<Measurement>::Lease Registry::PinMeasurementBatch(const MeasurementBatchKey& key) { return batches.Pin(key); }
	ExposureCapture::ExposureCapture() : state_(nullptr) {}
	ExposureCapture& ExposureCapture::Instance()
	{
		static ExposureCapture value;
		return value;
	}
	ExposureEvidenceHistory::Lease ExposureCapture::PinEvidence(const ExposureStamp& key) { return exposures.Pin(key); }
	Json MeasurementBatchEvidenceJson(const MeasurementBatch<Measurement>& batch)
	{
		return { { "measurementBatchId", batch.key.id }, { "revision", batch.key.revision },
			{ "left", batch.samples[0].data[0] }, { "right", batch.samples[1].data[0] } };
	}
	Json ExposureLookupEvidenceJson(const ExposureEvidenceLookup& lookup)
	{
		const auto& k = lookup.key;
		return { { "key", { { "frame", k.frame }, { "epoch", k.epoch }, { "sequence", k.sequence } } },
			{ "available", lookup.evidence && lookup.evidence->readbackComplete }, { "reason", lookup.reason } };
	}
}
int main()
{
	const auto require = [](bool value) { if (!value) throw std::runtime_error("capture companion invariant"); };
	const MeasurementBatchKey key{ 1, 7, 4, 100, 100, 0, 3, true };
	ExposureEvidence pending;
	pending.stamp = { 100, 3, 12, false };
	exposures.Record(pending, "readback_pending");
	const Json stamp{ { "frame", 100 }, { "epoch", 3 }, { "sequence", 12 } };
	const Json region{ { "measurementBatchId", 1 }, { "revision", 7 }, { "generation", 4 }, { "frame", 100 },
		{ "sourceWorldFrame", 100 }, { "insertionPoint", 0 }, { "expectedMeasurementSlotMask", 3 },
		{ "atomicColourBatch", true }, { "exposure", stamp } };
	const Json eye{ { "exposure", stamp }, { "physicalRegions", Json::array({ region }) } };
	const Json evidence{ { "available", true }, { "transactionId", "capture-tx" },
		{ "configuration", { { "color", { { "experiments", { { "diagnostics", true } } } } } } },
		{ "engineExposure", stamp }, { "left", eye }, { "right", eye } };
	auto snapshot = CSX::ScreenshotPolicy::RetainNeuralDiagnostics(evidence);
	require(static_cast<bool>(snapshot));
	Json actual{ { "acquisition", { { "nrEvidence", evidence } } } };
	const auto frozen = actual["acquisition"];
	Measurement left, right;
	left.data[0] = 12;
	right.data[0] = 13;
	batches.Record(key, 0, left);
	// Simulate delayed encoding while unrelated diagnostics replace both histories.
	for (std::uint64_t id = 2; id < 40; ++id) {
		auto newer = key;
		newer.id = id;
		batches.Record(newer, 0, left);
		batches.Record(newer, 1, right);
	}
	auto newerExposure = pending;
	newerExposure.stamp.sequence += ExposureEvidenceHistory::Capacity;
	exposures.Record(newerExposure, "readback_pending");
	pending.readbackComplete = true;
	exposures.Complete(pending, "readback_complete");
	batches.Record(key, 1, right);
	std::thread consumer([&] { CSX::ScreenshotPolicy::FinalizeNeuralDiagnostics(snapshot, actual); });
	consumer.join();
	require(!snapshot && actual["acquisition"] == frozen);
	const auto sealed = actual["captureDiagnostics"];
	require(sealed["transactionId"] == "capture-tx" && sealed["measurementBatches"].size() == 1);
	require(sealed["measurementBatches"][0]["left"] == 12 && sealed["measurementBatches"][0]["right"] == 13);
	require(sealed["exposures"].size() == 1 && sealed["exposures"][0]["available"] == true);
	require(sealed["measurementRequests"][0]["available"] == true);
	CSX::ScreenshotPolicy::FinalizeNeuralDiagnostics(snapshot, actual);
	require(actual["captureDiagnostics"] == sealed);
	exposures.MarkAmbiguous(pending.stamp);
	require(actual["captureDiagnostics"] == sealed);

	auto malformed = evidence;
	malformed["right"]["physicalRegions"][0]["generation"] = 999;
	auto failed = CSX::ScreenshotPolicy::RetainNeuralDiagnostics(malformed);
	Json failedActual;
	CSX::ScreenshotPolicy::FinalizeNeuralDiagnostics(failed, failedActual);
	require(failedActual["captureDiagnostics"]["reason"] == "companion_retention_failed");
	require(!CSX::ScreenshotPolicy::RetainNeuralDiagnostics(Json{ { "available", false } }));
	auto malformedAvailability = CSX::ScreenshotPolicy::RetainNeuralDiagnostics(Json{ { "available", "invalid" } });
	CSX::ScreenshotPolicy::FinalizeNeuralDiagnostics(malformedAvailability, failedActual);
	require(failedActual["captureDiagnostics"]["reason"] == "companion_retention_failed");

	auto baseline = evidence;
	for (const auto* eyeName : { "left", "right" }) {
		baseline[eyeName]["physicalRegions"] = Json::array();
		baseline[eyeName]["exposure"] = Json{ { "frame", nullptr }, { "valid", false } };
	}
	auto baselineSnapshot = CSX::ScreenshotPolicy::RetainNeuralDiagnostics(baseline);
	Json baselineActual;
	CSX::ScreenshotPolicy::FinalizeNeuralDiagnostics(baselineSnapshot, baselineActual);
	require(baselineActual["captureDiagnostics"]["measurementRequests"].empty());
	require(baselineActual["captureDiagnostics"]["exposures"].size() == 1);
	auto noMeasurements = evidence;
	noMeasurements["configuration"]["color"]["experiments"]["diagnostics"] = false;
	auto noMeasurementsSnapshot = CSX::ScreenshotPolicy::RetainNeuralDiagnostics(noMeasurements);
	CSX::ScreenshotPolicy::FinalizeNeuralDiagnostics(noMeasurementsSnapshot, baselineActual);
	require(baselineActual["captureDiagnostics"]["measurementPolicy"] == "diagnostics_disabled");
	require(baselineActual["captureDiagnostics"]["measurementRequests"].empty());

	// Finalization with missing readbacks is explicit, bounded, and releases leases.
	auto absent = evidence;
	absent["engineExposure"]["sequence"] = 9999;
	for (const auto* name : { "left", "right" }) {
		absent[name]["physicalRegions"][0]["measurementBatchId"] = 1000;
		absent[name]["physicalRegions"][0]["exposure"] = Json::object();
		absent[name]["exposure"] = Json::object();
	}
	auto unavailable = CSX::ScreenshotPolicy::RetainNeuralDiagnostics(absent);
	Json terminal;
	CSX::ScreenshotPolicy::FinalizeNeuralDiagnostics(unavailable, terminal);
	require(!unavailable && terminal["captureDiagnostics"]["measurementBatches"].empty());
	require(terminal["captureDiagnostics"]["measurementRequests"][0]["available"] == false);
	require(terminal["captureDiagnostics"]["exposures"][0]["available"] == false);
	std::vector<MeasurementBatchHistory<Measurement>::Lease> occupied;
	for (std::size_t i = 0; i < Util::kCaptureRetentionCapacity; ++i) {
		auto reserved = key;
		reserved.id = 2000 + i;
		occupied.push_back(batches.Pin(reserved));
		require(static_cast<bool>(occupied.back()));
	}
	auto exhausted = CSX::ScreenshotPolicy::RetainNeuralDiagnostics(absent);
	CSX::ScreenshotPolicy::FinalizeNeuralDiagnostics(exhausted, terminal);
	require(terminal["captureDiagnostics"]["measurementRequests"][0]["reason"] == "capture_retention_capacity_exhausted");
	occupied.clear();
	auto released = CSX::ScreenshotPolicy::RetainNeuralDiagnostics(absent);
	CSX::ScreenshotPolicy::FinalizeNeuralDiagnostics(released, terminal);
	require(terminal["captureDiagnostics"]["measurementRequests"][0]["reason"] == "measurement_incomplete_at_capture_finalization");
	CSX::ScreenshotPolicy::DiagnosticSnapshot throwing = []() -> Json { throw std::runtime_error("test serialization"); };
	CSX::ScreenshotPolicy::FinalizeNeuralDiagnostics(throwing, terminal);
	require(!throwing && terminal["captureDiagnostics"]["reason"] == "companion_finalization_failed");
	return 0;
}
