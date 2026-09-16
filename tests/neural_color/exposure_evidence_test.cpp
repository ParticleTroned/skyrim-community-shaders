#include "Features/Upscaling/NeuralRendering/ExposureCapture.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

using namespace NeuralRendering::Color;
static unsigned checks = 0;
static void Require(bool value, const char* message)
{
	++checks;
	if (!value) {
		std::fprintf(stderr, "Exposure evidence check %u: %s\n", checks, message);
		std::abort();
	}
}
static bool Reason(const ExposureEvidenceLookup& result, const char* expected)
{
	return std::strcmp(result.reason, expected) == 0;
}
int main()
{
	auto history = std::make_unique<ExposureEvidenceHistory>();
	const auto missing = history->GetSourceFrame(41, 7);
	Require(!missing.evidence && missing.key.frame == 41 && missing.key.epoch == 7 && !missing.key.sequence,
		"missing source frame preserves epoch for a later lookup");
	Require(Reason(history->Get({}), "invalid_evidence_key"), "an empty key cannot read a default slot");
	ExposureEvidence sample;
	sample.stamp = { 41, 7, 1, false };
	sample.sourceIdentity = 100;
	sample.sourceViewIdentity = 101;
	sample.shaderIdentity = 102;
	sample.samplerIdentity = 103;
	sample.sourceFormat = sample.sourceViewFormat = 10;
	sample.sourceWidth = sample.sourceHeight = 2;
	history->Record(sample, "readback_pending");
	const auto pinned = history->GetSourceFrame(41, 7);
	Require(pinned.evidence && pinned.key.sequence == 1 && !pinned.evidence->readbackComplete &&
				Reason(pinned, "readback_pending"),
		"source lookup pins a queued exact key");
	Require(!history->Get({ 42, 7, 1, false }).evidence, "same sequence cannot match another frame");
	Require(!history->Get({ 41, 8, 1, false }).evidence, "same sequence cannot match another epoch");
	Require(!history->Get({ 41, 7, 2, false }).evidence, "same frame cannot replace an exact sequence");
	sample.readbackComplete = true;
	sample.values = { 0.5f, 2.0f, 1.0f, 1.0f };
	sample.gammaKnown = true;
	sample.frameGammaExponent = 2.2f;
	auto wrongSource = sample;
	wrongSource.sourceViewIdentity = 999;
	Require(!history->Complete(wrongSource, "readback_complete"), "completion cannot change pinned source-view identity");
	Require(history->Complete(sample, "readback_complete"), "later render polling completes the queued exact sample");
	const auto ready = history->Get(pinned.key);
	Require(ready.evidence && ready.evidence->readbackComplete && ready.evidence->values == sample.values &&
				ready.evidence->sourceViewIdentity == 101 && Reason(ready, "readback_complete"),
		"completion retains source and sampled values");
	auto anotherEpoch = sample;
	anotherEpoch.stamp = { 41, 8, 2, false };
	history->Record(anotherEpoch, "readback_pending");
	Require(history->GetSourceFrame(41, 7).key.sequence == 1 && history->GetSourceFrame(41, 8).key.sequence == 2,
		"equal world frames from separate epochs remain distinct");
	history->MarkAmbiguous(sample.stamp);
	Require(history->Complete(sample, "readback_complete"), "readback may finish after producer ambiguity");
	Require(Reason(history->Get(sample.stamp), "ambiguous_source_frame") && history->Get(sample.stamp).key.ambiguous,
		"late completion never clears a source ambiguity");
	anotherEpoch.readbackComplete = false;
	Require(history->Complete(anotherEpoch, "resources_retired_before_readback"), "retirement can mark the exact pending sample");
	Require(Reason(history->Get(anotherEpoch.stamp), "resources_retired_before_readback") &&
				!history->Get(anotherEpoch.stamp).evidence->readbackComplete,
		"retirement preserves an explicit incomplete result");
	for (std::uint64_t sequence = 3; sequence <= ExposureEvidenceHistory::Capacity + 1; ++sequence) {
		auto newer = sample;
		newer.stamp = { static_cast<std::uint32_t>(sequence + 100), 9, sequence, false };
		newer.readbackComplete = false;
		history->Record(newer, "readback_pending");
	}
	Require(Reason(history->Get(sample.stamp), "evidence_retention_expired") && !history->Get(sample.stamp).evidence,
		"bounded retention reports expiry instead of returning a replacement sample");
	Require(!history->Complete(sample, "readback_complete"), "late completion cannot overwrite an evicting capture");
	const ExposureStamp newest{ static_cast<std::uint32_t>(ExposureEvidenceHistory::Capacity + 101), 9,
		ExposureEvidenceHistory::Capacity + 1, false };
	Require(history->Get(newest).evidence && !history->Get(newest).evidence->readbackComplete,
		"the newer capture stays pending after a stale completion");
	Require(!history->GetSourceFrame(41, 7).evidence, "source lookup cannot substitute a different epoch after expiry");
	auto duplicate = sample;
	duplicate.stamp = { 41, 8, ExposureEvidenceHistory::Capacity + 3, false };
	history->Record(duplicate, "readback_pending");
	Require(Reason(history->GetSourceFrame(41, 8), "multiple_source_frame_snapshots"),
		"multiple captures with one frame and epoch fail closed");
	std::printf("Passed %u exposure evidence checks\n", checks);
}
