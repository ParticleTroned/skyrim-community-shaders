#include "Features/VR/MenuPositioningPolicy.h"

namespace
{
	using VRMenuPositioningPolicy::UseFixedWorld;

	constexpr bool CoversFixedWorldSelection()
	{
		return UseFixedWorld(1) &&
		       !UseFixedWorld(0);
	}

	static_assert(CoversFixedWorldSelection());
}

int main()
{
	return CoversFixedWorldSelection() ? 0 : 1;
}
