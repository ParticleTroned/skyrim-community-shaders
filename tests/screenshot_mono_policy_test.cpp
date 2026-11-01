#include "Features/ScreenshotApiPolicy.h"
#include "Features/ScreenshotBurstPolicy.h"
#include "Features/ScreenshotCapturePolicy.h"
#include <cassert>
#include <limits>

int main()
{
	using namespace CSX::Screenshot;
	using namespace CSX::ScreenshotPolicy;
	auto full = ResolveCrop(1920, 1080, 0, 0, 1, 1);
	assert(full.x == 0 && full.y == 0 && full.width == 1920 && full.height == 1080);
	auto pixel = ResolveCrop(1920, 1080, .5f, .5f, .00001f, .00001f);
	assert(pixel.x == 960 && pixel.y == 540 && pixel.width == 1 && pixel.height == 1);
	for (const auto value : { std::numeric_limits<float>::infinity(), -1.0f, std::numeric_limits<float>::quiet_NaN() }) {
		bool rejected = false;
		try { (void)ResolveCrop(1920, 1080, value, 0, .1f, .1f); } catch (...) { rejected = true; }
		assert(rejected);
	}
	assert(CanReserve(MaximumStagingBytes - MaximumJobBytes, MaximumJobBytes));
	assert(!CanReserve(MaximumStagingBytes, 1));
	assert(!CanReserve(0, MaximumJobBytes + 1));
	DispatchArbitration fairness;
	assert(fairness.Select(true, true) == DispatchClass::Manual);
	fairness.FinishAttempt(DispatchClass::Manual, true);
	assert(fairness.Select(true, true) == DispatchClass::Manual);
	fairness.FinishAttempt(DispatchClass::Manual, false);
	assert(fairness.Select(true, true) == DispatchClass::Sequence);
	assert(ResolveBusyDispatch(false, false, false) == BusyDispatchDisposition::Retry);
	assert(ResolveBusyDispatch(true, false, false) == BusyDispatchDisposition::Drop);
	assert(ResolveBusyDispatch(false, false, true) == BusyDispatchDisposition::Fail);
	assert(ResolveBusyDispatch(false, true, false) == BusyDispatchDisposition::Cancel);
	for (const auto text : { "../escape", "con", "AUX.txt", "x.", "LPT9", "space " }) assert(!IsSafeWindowsFilenameSegment(text));
	assert(IsSafeWindowsFilenameSegment("frame_000001"));
	assert(FilenameCollisionKey("Scene_A") == FilenameCollisionKey("SCENE_A"));
	assert(IsContainedPath("/captures", "/captures/sequence/a.png"));
	assert(!IsContainedPath("/captures", "/outside/a.png"));
	assert(IsGameFrameScheduleWithinLimit(0, 1, 10000));
	assert(!IsGameFrameScheduleWithinLimit(216000, 1, 2));
	assert(!IsWallClockScheduleWithinLimit(3600000, 100, 2));
	const nlohmann::json request{ { "regions", { { { "x", 5 }, { "y", 8 }, { "width", 32 }, { "height", 16 } },
		{ { "x", 64 }, { "y", 80 }, { "width", 32 }, { "height", 24 } } } } };
	const auto atlas = ScreenshotBurst::Parse(request, 120, 1);
	assert(atlas.width == 32 && atlas.height == 40 && atlas.payloadBytes == 32 * 40 * 4 * 120);
	const auto copies = atlas.Copies(1920, 1080, false, false);
	assert(copies.size() == 2 && copies[0].destinationY == 0 && copies[1].destinationY == 16);
	for (const auto views : { 0u, 2u }) {
		bool rejected = false;
		try { (void)ScreenshotBurst::Parse(request, 1, views); } catch (...) { rejected = true; }
		assert(rejected);
	}
	ScreenshotBurst::Continuity continuity;
	assert(continuity.Observe(10) && continuity.Observe(11));
	assert(!continuity.Observe(13));
	assert(!continuity.Receipt(3, true).at("complete").get<bool>());
	ScreenshotBurst::Continuity complete;
	assert(complete.Observe(50) && complete.Observe(51));
	assert(complete.Receipt(2, true).at("complete").get<bool>());
	assert(!complete.Receipt(2, false).at("complete").get<bool>());
}
