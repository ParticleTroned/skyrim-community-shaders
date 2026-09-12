#include "Features/Upscaling/VRRelatchDrainPolicy.h"

namespace
{
	constexpr bool RetainsCompletionUntilProviderUse()
	{
		VRRelatchDrainPolicy::Proof proof;
		if (proof.Begin(0) || proof.IsReady(0) || !proof.Begin(7))
			return false;
		if (proof.Begin(7) || proof.IsReady(7))
			return false;
		proof.MarkReady(7);
		if (!proof.IsReady(7) || proof.Begin(7) || !proof.IsReady(7))
			return false;
		proof.Invalidate();
		if (proof.IsReady(7) || proof.Matches(7) || !proof.Begin(7))
			return false;
		return !proof.IsReady(7);
	}

	constexpr bool RejectsSupersededAndCancelledProofs()
	{
		VRRelatchDrainPolicy::Proof proof;
		if (!proof.Begin(7))
			return false;
		proof.MarkReady(7);
		if (!proof.Begin(8) || proof.IsReady(7) || proof.IsReady(8))
			return false;
		proof.MarkReady(7);
		if (proof.IsReady(8))
			return false;
		proof.MarkReady(8);
		proof.Cancel();
		return !proof.IsReady(8) && !proof.Matches(8) && proof.Begin(8) && !proof.IsReady(8);
	}

	static_assert(RetainsCompletionUntilProviderUse());
	static_assert(RejectsSupersededAndCancelledProofs());
}

int main()
{
	return RetainsCompletionUntilProviderUse() && RejectsSupersededAndCancelledProofs() ? 0 : 1;
}
