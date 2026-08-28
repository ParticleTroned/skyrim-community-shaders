#include "Features/Upscaling/VRRenderScaleMemoryTracePolicy.h"

#include <cassert>
#include <cstdint>
#include <limits>

int main()
{
	using namespace VRRenderScaleMemoryTracePolicy;

	static_assert(SaturatingDelta(9, 4) == 5);
	static_assert(SaturatingDelta(4, 9) == 0);
	static_assert(SafeRatio(5, 0) == 0.0);

	Summary summary;
	Sample baseline{
		.tick = 10,
		.frame = 100,
		.transitionEpoch = 7,
		.contractGeneration = 3,
		.dxgiValid = true,
		.dxgiUsageBytes = 100,
		.systemCommitValid = true,
		.systemCommitBytes = 200,
		.processPrivateValid = true,
		.processPrivateBytes = 300,
		.estimatedAdditionalBytes = 40,
		.projectedAdditionalBytes = 80,
		.projectedSystemCommitAdditionalBytes = 160,
		.residencyAdmissionLimitBytes = 200,
		.systemCommitAdmissionLimitBytes = 400,
		.planValid = true,
	};
	const auto firstPeaks = summary.Observe(baseline);
	assert(summary.baselineCaptured);
	assert(summary.baseline.frame == 100);
	assert(summary.samplesObserved == 1);
	assert((firstPeaks & kPeakDXGIUsage) != 0);
	assert((firstPeaks & kPeakSystemCommit) != 0);
	assert((firstPeaks & kPeakProcessPrivate) != 0);
	assert(summary.peakEstimatedAdditional.value == 40);

	Sample deferred = baseline;
	deferred.tick = 20;
	deferred.frame = 104;
	deferred.dxgiUsageBytes = 160;
	deferred.systemCommitBytes = 260;
	deferred.processPrivateBytes = 290;
	deferred.estimatedAdditionalBytes = 60;
	deferred.projectedAdditionalBytes = 100;
	deferred.projectedSystemCommitAdditionalBytes = 240;
	deferred.admissionDeferred = true;
	deferred.previousPresentationRetained = true;
	const auto secondPeaks = summary.Observe(deferred);
	assert((secondPeaks & kPeakDXGIUsage) != 0);
	assert((secondPeaks & kPeakSystemCommit) != 0);
	assert((secondPeaks & kPeakProcessPrivate) == 0);
	assert(summary.admissionDeferredObserved);
	assert(summary.preMutationAdmissionDeferredObserved);
	assert(summary.presentationRetainedWhileDeferredObserved);
	assert(summary.maximumDeferredResidencyAdmissionRatio == 0.8);
	assert(summary.maximumDeferredSystemCommitAdmissionRatio == 0.65);
	assert(summary.deferredResidencyAdmissionRatioObserved);
	assert(summary.deferredSystemCommitAdmissionRatioObserved);
	assert(SaturatingDelta(summary.peakDXGIUsage.value,
		baseline.dxgiUsageBytes) == 60);
	assert(SaturatingDelta(summary.peakSystemCommit.value,
		baseline.systemCommitBytes) == 60);

	Sample mutation = deferred;
	mutation.tick = 30;
	mutation.frame = 108;
	mutation.dxgiValid = false;
	mutation.systemCommitValid = false;
	mutation.processPrivateValid = false;
	mutation.physicalMutationActive = true;
	mutation.previousPresentationRetained = false;
	summary.Observe(mutation);
	assert(summary.samplesObserved == 3);
	assert(summary.invalidDXGISamples == 1);
	assert(summary.invalidSystemCommitSamples == 1);
	assert(summary.invalidProcessPrivateSamples == 1);
	assert(summary.physicalMutationObserved);

	ScalarPeak tiePeak = summary.peakDXGIUsage;
	assert(!UpdatePeak(tiePeak, true, tiePeak.value, mutation));
	assert(UpdatePeak(tiePeak, true, tiePeak.value + 1, mutation));
	assert(tiePeak.frame == mutation.frame);

	summary.samplesObserved = std::numeric_limits<std::uint64_t>::max();
	summary.invalidDXGISamples = std::numeric_limits<std::uint64_t>::max();
	summary.Observe(mutation);
	assert(summary.samplesObserved == std::numeric_limits<std::uint64_t>::max());
	assert(summary.invalidDXGISamples == std::numeric_limits<std::uint64_t>::max());

	Summary invalidPlanSummary;
	Sample invalidPlan = baseline;
	invalidPlan.planValid = false;
	invalidPlan.admissionDeferred = true;
	invalidPlan.estimatedAdditionalBytes = 1000;
	invalidPlan.projectedAdditionalBytes = 2000;
	invalidPlan.projectedSystemCommitAdditionalBytes = 3000;
	invalidPlan.residencyAdmissionLimitBytes = 1;
	invalidPlan.systemCommitAdmissionLimitBytes = 1;
	const auto invalidPlanPeaks = invalidPlanSummary.Observe(invalidPlan);
	assert((invalidPlanPeaks & kPeakEstimatedAdditional) == 0);
	assert((invalidPlanPeaks & kPeakProjectedAdditional) == 0);
	assert((invalidPlanPeaks & kPeakProjectedSystemCommitAdditional) == 0);
	assert(!invalidPlanSummary.admissionDeferredObserved);
	assert(!invalidPlanSummary.deferredResidencyAdmissionRatioObserved);
	assert(!invalidPlanSummary.deferredSystemCommitAdmissionRatioObserved);

	Summary invalidMemorySummary;
	Sample invalidMemory = baseline;
	invalidMemory.admissionDeferred = true;
	invalidMemory.dxgiValid = false;
	invalidMemory.systemCommitValid = false;
	invalidMemorySummary.Observe(invalidMemory);
	assert(!invalidMemorySummary.deferredResidencyAdmissionRatioObserved);
	assert(!invalidMemorySummary.deferredSystemCommitAdmissionRatioObserved);

	return 0;
}
