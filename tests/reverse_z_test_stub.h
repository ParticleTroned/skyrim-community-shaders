#pragma once

#include "Utils/DepthConvention.h"

namespace globals::features
{
	struct ReverseZTestState
	{
		bool active = false;
		bool IsActive() const { return active; }
		Depth::Convention GetConvention() const { return active ? Depth::Convention::Reversed : Depth::Convention::Standard; }
	};
	inline ReverseZTestState reverseZ;
}
