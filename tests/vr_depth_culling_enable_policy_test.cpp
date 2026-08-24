#include "Features/VRDepthCullingEnablePolicy.h"

namespace
{
	using namespace VRDepthCullingEnablePolicy;

	constexpr bool CoversExteriorMasterAndInteriorOptIn()
	{
		return IsEnabled(false, true, false) &&
		       IsEnabled(false, true, true) &&
		       !IsEnabled(true, true, false) &&
		       IsEnabled(true, true, true) &&
		       !IsEnabled(false, false, false) &&
		       !IsEnabled(false, false, true) &&
		       !IsEnabled(true, false, false) &&
		       !IsEnabled(true, false, true);
	}

	static_assert(CoversExteriorMasterAndInteriorOptIn());
}

int main()
{
	return CoversExteriorMasterAndInteriorOptIn() ? 0 : 1;
}
