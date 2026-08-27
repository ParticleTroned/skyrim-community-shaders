#include "Features/VR/DepthCullingDiagnostics.h"

int main()
{
	CSX::VRDepthCullingDiagnostics::Counters counters;
	if (counters.Capture().collecting)
		return 1;

	counters.Start();
	counters.SetControlMode(CSX::VRDepthCullingDiagnostics::ControlMode::ForcedVisible);
	counters.BeginFrame(10, true);
	counters.RecordAccumulation(false);
	counters.RecordAccumulation(true);
	counters.RecordReady(true, 10, 44, 321);
	counters.RecordBindAttempt();
	counters.RecordBoundDraw(CSX::VRDepthCullingDiagnostics::DrawCategory::Grass);
	counters.RecordReadbackQueued();
	CSX::VRDepthCullingDiagnostics::ClassifiedDraws classified{
		.occluded = 7,
		.visible = 11,
		.occludedLighting = 3,
		.visibleLighting = 5,
		.occludedDistantTree = 1,
		.visibleDistantTree = 2,
		.occludedGrass = 3,
		.visibleGrass = 4,
	};
	counters.RecordReadbackCompleted();
	counters.RecordVisibilitySample(counters.CollectionEpoch(), 10, 100, 40, 60, 8, 12, classified);
	counters.RecordPipelineQueryBegun();
	counters.RecordPipelineQueryEnded();
	counters.RecordPipelineQueryCompleted();
	counters.RecordPipelineTiming(counters.CollectionEpoch(), 2500000);
	counters.RecordCoverageSpanTiming(counters.CollectionEpoch(), 1750000);
	counters.RecordPipelineStatistics(counters.CollectionEpoch(), 42, {
		.iaVertices = 1000,
		.iaPrimitives = 500,
		.vsInvocations = 900,
		.clipperInvocations = 800,
		.clipperPrimitives = 400,
		.psInvocations = 700,
		.csInvocations = 600,
	});
	counters.BeginFrame(11, true);
	counters.RecordBindAttempt();
	counters.RecordResultNotReady();

	auto snapshot = counters.Capture();
	if (!snapshot.collecting ||
		snapshot.firstFrame != 10 || snapshot.lastFrame != 11 || snapshot.lastReadyFrame != 10 ||
		snapshot.lastDepthCullingFrame != 44 || snapshot.lastObjectCount != 321 ||
		snapshot.framesObserved != 2 || snapshot.enabledFrames != 2 ||
		snapshot.accumulationCalls != 2 || snapshot.previousResultsNeutralized != 1 ||
		snapshot.readyPassCalls != 1 || snapshot.readyFrames != 1 ||
		snapshot.bindAttempts != 2 || snapshot.boundDraws != 1 ||
		snapshot.controlMode != CSX::VRDepthCullingDiagnostics::ControlMode::ForcedVisible ||
		snapshot.boundGrassDraws != 1 ||
		snapshot.currentFrameBoundDraws != 0 || snapshot.lastCompletedFrameBoundDraws != 1 ||
		snapshot.readbackCopiesQueued != 1 || snapshot.readbackCopiesCompleted != 1 ||
		snapshot.visibilityFramesSampled != 1 || snapshot.visibilityObjectsSampled != 100 ||
		snapshot.occludedObjects != 40 || snapshot.visibleObjects != 60 ||
		snapshot.occludedObjectsWithCoveredDraws != 8 || snapshot.visibleObjectsWithCoveredDraws != 12 ||
		snapshot.occludedDraws != 7 || snapshot.visibleDraws != 11 ||
		snapshot.occludedLightingDraws != 3 || snapshot.visibleGrassDraws != 4 ||
		snapshot.pipelineQueriesBegun != 1 || snapshot.pipelineQueriesEnded != 1 ||
		snapshot.pipelineQueriesCompleted != 1 || snapshot.pipelineStatsSamples != 1 ||
		snapshot.pipelineCoveredLightingDraws != 42 ||
		snapshot.pipelineTimingSamples != 1 || snapshot.pipelineRegionNanoseconds != 2500000 ||
		snapshot.coverageSpanTimingSamples != 1 || snapshot.coverageSpanRegionNanoseconds != 1750000 ||
		snapshot.pipelineIAVertices != 1000 || snapshot.pipelineIAPrimitives != 500 ||
		snapshot.pipelineVSInvocations != 900 || snapshot.pipelineClipperInvocations != 800 ||
		snapshot.pipelineClipperPrimitives != 400 || snapshot.pipelinePSInvocations != 700 ||
		snapshot.pipelineCSInvocations != 600 ||
		snapshot.resultNotReady != 1) {
		return 2;
	}

	counters.Stop();
	counters.RecordBindAttempt();
	if (counters.Capture().bindAttempts != 2)
		return 3;

	counters.Reset();
	snapshot = counters.Capture();
	return !snapshot.collecting && snapshot.framesObserved == 0 && snapshot.bindAttempts == 0 &&
		snapshot.visibilityFramesSampled == 0 &&
		snapshot.pipelineStatsSamples == 0 &&
		snapshot.pipelineCoveredLightingDraws == 0 &&
		snapshot.pipelineTimingSamples == 0 && snapshot.pipelineRegionNanoseconds == 0 &&
		snapshot.coverageSpanTimingSamples == 0 && snapshot.coverageSpanRegionNanoseconds == 0 &&
		snapshot.controlMode == CSX::VRDepthCullingDiagnostics::ControlMode::ForcedVisible ?
		0 :
		4;
}
