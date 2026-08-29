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
	if (!IsWallClockScheduleWithinLimit(0, 1000, 3601) ||
		IsWallClockScheduleWithinLimit(1, 1000, 3601))
		throw std::runtime_error("wall-clock sequence limit is invalid");
	if (!IsGameFrameScheduleWithinLimit(0, 60, 3601) ||
		IsGameFrameScheduleWithinLimit(1, 60, 3601))
		throw std::runtime_error("game-frame sequence limit is invalid");
	return 0;
}
