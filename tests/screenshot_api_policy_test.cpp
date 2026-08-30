#include "Features/ScreenshotApiPolicy.h"

#include <stdexcept>

int main()
{
	using namespace CSX::ScreenshotPolicy;
	for (const auto* unsafe : { "", ".", "..", "CON", "con.txt", "NUL.png", "COM1", "LPT9.log",
			 "trailing.", "trailing ", "stream:name", "star*", "slash/", "back\\slash", "caf\xC3\xA9" }) {
		if (IsSafeWindowsFilenameSegment(unsafe))
			throw std::runtime_error("unsafe filename segment was accepted");
	}
	if (!IsSafeWindowsFilenameSegment("frame_000001") ||
		FilenameCollisionKey("LEFT") != FilenameCollisionKey("left"))
		throw std::runtime_error("safe filename policy is invalid");
	if (!CanAdmitPendingOperations(MaximumPendingOperations - 1) ||
		CanAdmitPendingOperations(MaximumPendingOperations))
		throw std::runtime_error("pending-operation admission boundary is invalid");
	if (!CanAugmentOutputs(2, 2) || !CanAugmentOutputs(3, 1) ||
		CanAugmentOutputs(3, 2) || CanAugmentOutputs(4, 1) ||
		CanAugmentOutputs(0, 1))
		throw std::runtime_error("post-augmentation output boundary is invalid");
	if (ExpectedSequenceArtifacts(true) != 1 ||
		ExpectedSequenceArtifacts(false) != 0)
		throw std::runtime_error("sequence packaging artifact accounting is invalid");
	if (!CanStartWorker(true, false) || CanStartWorker(false, false) ||
		CanStartWorker(true, true) || CanStartWorker(false, true))
		throw std::runtime_error("worker admission is not terminal after close");
	if (ResolveActualOutputView("source_native", true, false, false) != "source_native" ||
		!ResolveActualOutputView("left_eye", true, false, false).empty() ||
		ResolveActualOutputView("framed_combined", false, true, false) != "framed_left" ||
		ResolveActualOutputView("framed_combined", false, true, true) != "framed_right" ||
		ResolveActualOutputView("side_by_side", false, false, false) != "side_by_side")
		throw std::runtime_error("actual output provenance policy is invalid");
	const std::filesystem::path captureRoot = "C:/Users/test/Pictures/Community Shaders";
	if (!IsContainedPath(captureRoot, captureRoot) ||
		!IsContainedPath(captureRoot, captureRoot / "Screenshots") ||
		IsContainedPath(captureRoot, captureRoot.parent_path() / "Other") ||
		IsContainedPath(captureRoot, captureRoot / ".." / "Other"))
		throw std::runtime_error("settings-default containment policy is invalid");
	if (!IsWallClockScheduleWithinLimit(0, 1000, 3601) ||
		IsWallClockScheduleWithinLimit(1, 1000, 3601))
		throw std::runtime_error("wall-clock sequence limit is invalid");
	if (!IsGameFrameScheduleWithinLimit(0, 60, 3601) ||
		IsGameFrameScheduleWithinLimit(1, 60, 3601))
		throw std::runtime_error("game-frame sequence limit is invalid");
	return 0;
}
