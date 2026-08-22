#include "Features/VR/DepthCullingDiagnostics.h"

int main()
{
	CSX::VRDepthCullingDiagnostics::Counters counters;
	if (counters.Capture().collecting)
		return 1;

	counters.Start();
	counters.BeginFrame(10, true);
	counters.RecordAccumulation(false);
	counters.RecordAccumulation(true);
	counters.RecordReady(true, 10, 44, 321);
	counters.RecordBindAttempt();
	counters.RecordBoundDraw();
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
		snapshot.currentFrameBoundDraws != 0 || snapshot.lastCompletedFrameBoundDraws != 1 ||
		snapshot.resultNotReady != 1) {
		return 2;
	}

	counters.Stop();
	counters.RecordBindAttempt();
	if (counters.Capture().bindAttempts != 2)
		return 3;

	counters.Reset();
	snapshot = counters.Capture();
	return !snapshot.collecting && snapshot.framesObserved == 0 && snapshot.bindAttempts == 0 ? 0 : 4;
}
