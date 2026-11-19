#include "Features/VR/RenderDocFrameCapture.h"
#include "Features/VR/VRRenderScaleFrameBoundaryPolicy.h"
#include "Utils/ImportThunk.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

static void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}

struct CaptureAPI
{
	bool capturing = false, rejectStart = false, rejectEnd = false;
	unsigned starts = 0, ends = 0, discards = 0, queries = 0;
	void* owner = nullptr;
	bool IsFrameCapturing()
	{
		++queries;
		return capturing;
	}
	void StartFrameCapture(void* device, void*)
	{
		++starts;
		capturing = !rejectStart;
		owner = device;
	}
	bool EndFrameCapture(void* device, void*)
	{
		Require(capturing && owner == device, "finish the owning device");
		++ends;
		capturing = false;
		return !rejectEnd;
	}
	bool DiscardFrameCapture(void* device, void*)
	{
		Require(capturing && owner == device, "discard the owning device");
		++discards;
		capturing = false;
		return true;
	}
};

int main()
{
	try {
		CaptureAPI api;
		RenderDocFrameCapture capture;
		int device;
		capture.Begin(&api, &device, 1, true);
		Require(api.queries == 0, "idle frames do not call the capture API");
		capture.Queue(2);
		capture.Begin(&api, &device, 1, false);
		Require(api.starts == 0 && capture.Pending() == 2, "failed poses preserve queued captures");
		api.capturing = true;
		capture.Begin(&api, &device, 2, true);
		capture.Complete(&api, 2);
		Require(api.starts == 0 && api.ends == 0 && api.discards == 0, "external capture remains owned externally");
		api.capturing = false;
		capture.Begin(&api, &device, 3, true);
		Require(capture.IsActive() && capture.Pending() == 1, "start one capture before scene rendering");
		using namespace VRRenderScaleFrameBoundaryPolicy;
		PairCompletion pair{ { 7, 3, 42, 1 } };
		RecordEyeCompletion(pair, 7, 0);
		Require(!CanServiceCompletedPair(pair, pair.identity), "left eye cannot finalize stereo capture");
		capture.Complete(&api, 2);
		Require(capture.IsActive() && api.ends == 0, "stale cycle cannot finalize current capture");
		RecordEyeCompletion(pair, 7, 1);
		Require(CanServiceCompletedPair(pair, pair.identity), "both eyes form a complete pair");
		capture.Complete(&api, pair.identity.compositorCycle);
		capture.Complete(&api, pair.identity.compositorCycle);
		Require(api.ends == 1 && capture.Completed() == 1, "one capture after both eyes, exactly once");
		capture.Begin(&api, &device, 4, true);
		capture.Begin(&api, &device, 5, true);
		Require(api.discards == 1 && capture.Failed() == 1 && !capture.IsActive(), "incomplete stereo cycle is discarded");
		capture.Queue(1);
		api.rejectStart = true;
		capture.Begin(&api, &device, 6, true);
		Require(!capture.IsActive() && capture.Failed() == 2, "failed start cannot claim capture ownership");
		api.rejectStart = false;
		capture.Queue(1);
		capture.Begin(&api, &device, 7, true);
		api.rejectEnd = true;
		capture.Complete(&api, 7);
		Require(capture.Failed() == 3 && capture.Completed() == 1, "failed finalization is not a completed capture");
		capture.Queue(1000);
		Require(capture.Pending() == 120, "capture request is bounded");
		capture.CancelPending();
		const auto idleQueries = api.queries;
		capture.Begin(&api, &device, 8, true);
		Require(capture.Pending() == 0 && !capture.IsActive() && api.queries == idleQueries, "cancelled queue stays idle");
		api.rejectEnd = false;
		capture.Queue(3);
		capture.Begin(&api, &device, 9, true);
		capture.CancelPending();
		Require(capture.IsActive() && capture.Pending() == 0, "cancellation preserves an in-flight pair");
		capture.Complete(&api, 9);
		capture.Begin(&api, &device, 10, true);
		Require(capture.Completed() == 2 && !capture.IsActive(), "active pair finishes without starting cancelled captures");

		capture.Queue(3);
		capture.Begin(&api, &device, 11, true);
		capture.Fail(&api);
		capture.Fail(&api);
		capture.Complete(&api, 11);
		Require(!capture.IsActive() && capture.Pending() == 0 && capture.Failed() == 4 && capture.Completed() == 2,
			"invalid vendor output discards its owned capture exactly once and cancels retries");
		api.capturing = true;
		const auto discards = api.discards;
		capture.Fail(&api);
		Require(api.discards == discards && api.capturing, "failure never discards an external owner's capture");
		api.capturing = false;

		std::array<std::uint8_t, 6> thunk{ 0xff, 0x25 };
		const std::int32_t displacement = -0x106;
		std::memcpy(thunk.data() + 2, &displacement, sizeof(displacement));
		Require(Util::IsImportJumpThunk(thunk, 0x1000, 0xf00), "signed import displacement resolves the expected slot");
		Require(!Util::IsImportJumpThunk(thunk, 0x1000, 0x1000), "other import slots are rejected");
		Require(!Util::IsImportJumpThunk({ thunk.data(), 5 }, 0x1000, 0xf00), "truncated import jumps are rejected");
		thunk[1] = 0x15;
		Require(!Util::IsImportJumpThunk(thunk, 0x1000, 0xf00), "call opcodes cannot masquerade as import jumps");
		std::cout << "Stereo capture ownership and import-thunk validation passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
