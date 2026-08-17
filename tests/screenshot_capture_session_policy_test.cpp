#include "Features/ScreenshotCaptureSessionPolicy.h"

#include <cassert>
#include <cstdint>

namespace
{
	using namespace ScreenshotCaptureSessionPolicy;

	constexpr bool CoversValidation()
	{
		const auto low = Validate(0, 0, 0);
		const auto high = Validate(UINT32_MAX, UINT32_MAX, UINT32_MAX);
		return low.frameCount == 1 &&
		       low.frameInterval == 1 &&
		       low.previewFramesPerSecond == 1 &&
		       high.frameCount == kMaxFrameCount &&
		       high.frameInterval == kMaxFrameInterval &&
		       high.previewFramesPerSecond == kMaxPreviewFramesPerSecond;
	}

	constexpr bool CoversCycleSelection()
	{
		CycleGate gate;
		if (!gate.IsEligible(100))
			return false;
		gate.RecordAccepted(100, 3);
		return !gate.IsEligible(100) &&
		       !gate.IsEligible(102) &&
		       gate.IsEligible(103);
	}

	constexpr bool CoversCycleOverflow()
	{
		CycleGate gate;
		gate.RecordAccepted(UINT64_MAX - 1, 3);
		return gate.nextEligibleCycle == UINT64_MAX &&
		       !gate.IsEligible(UINT64_MAX - 1) &&
		       gate.IsEligible(UINT64_MAX);
	}

	constexpr bool CoversCompletionStates()
	{
		const auto draining = ResolveCompletion(2, 1, 1, 0, false, false);
		const auto cancelled = ResolveCompletion(1, 1, 1, 0, true, true);
		const auto empty = ResolveCompletion(0, 0, 0, 0, false, false);
		const auto failed = ResolveCompletion(2, 2, 1, 1, false, true);
		const auto complete = ResolveCompletion(2, 2, 2, 0, false, false);
		return draining.state == CompletionState::Draining &&
		       cancelled.state == CompletionState::Cancelled &&
		       empty.state == CompletionState::Failed && empty.needsDefaultError &&
		       failed.state == CompletionState::Failed && !failed.needsDefaultError &&
		       complete.state == CompletionState::Complete;
	}

	static_assert(CoversValidation());
	static_assert(CoversCycleSelection());
	static_assert(CoversCycleOverflow());
	static_assert(CoversCompletionStates());
}

int main()
{
	const auto request = Validate(30, 6, 15);
	assert(request.frameCount == 30);
	assert(request.frameInterval == 6);
	assert(request.previewFramesPerSecond == 15);
}
