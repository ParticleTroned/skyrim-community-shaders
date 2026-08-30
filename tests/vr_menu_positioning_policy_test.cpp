#include "Features/VR/MenuPositioningPolicy.h"

namespace
{
	using VRMenuPositioningPolicy::SelectEffectiveValue;
	using VRMenuPositioningPolicy::ShouldAllowOverlayDrag;
	using VRMenuPositioningPolicy::ShouldLockDesktopCanvas;
	using VRMenuPositioningPolicy::ShouldReanchorOnOpen;
	using VRMenuPositioningPolicy::ShouldTrackHMDYaw;
	using VRMenuPositioningPolicy::UseFixedWorld;

	constexpr bool CoversMenuLayoutSelection()
	{
		return UseFixedWorld(1) &&
		       !UseFixedWorld(0) &&
		       SelectEffectiveValue(false, 7, 1) == 1 &&
		       SelectEffectiveValue(true, 7, 1) == 7 &&
		       ShouldLockDesktopCanvas(true, false) &&
		       !ShouldLockDesktopCanvas(true, true) &&
		       !ShouldLockDesktopCanvas(false, false) &&
		       ShouldAllowOverlayDrag(true, true) &&
		       !ShouldAllowOverlayDrag(false, true) &&
		       !ShouldAllowOverlayDrag(true, false) &&
		       ShouldReanchorOnOpen(false) &&
		       !ShouldReanchorOnOpen(true) &&
		       ShouldTrackHMDYaw(false) &&
		       !ShouldTrackHMDYaw(true);
	}

	static_assert(CoversMenuLayoutSelection());
}

int main()
{
	return CoversMenuLayoutSelection() ? 0 : 1;
}
