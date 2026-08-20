#pragma once

namespace VRMenuPositioningPolicy
{
	constexpr bool UseFixedWorld(int a_positioningMethod)
	{
		return a_positioningMethod == 1;
	}
}
