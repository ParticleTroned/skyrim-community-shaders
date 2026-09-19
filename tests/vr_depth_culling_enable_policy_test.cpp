#include "Features/VRDepthCullingEnablePolicy.h"

#include <limits>

namespace
{
	using namespace VRDepthCullingEnablePolicy;

	constexpr bool CoversIndependentLocationSwitches()
	{
		return IsEnabled(false, true, false) &&
		       IsEnabled(false, true, true) &&
		       !IsEnabled(true, true, false) &&
		       IsEnabled(true, true, true) &&
		       !IsEnabled(false, false, false) &&
		       !IsEnabled(false, false, true) &&
		       !IsEnabled(true, false, false) &&
		       IsEnabled(true, false, true);
	}

	constexpr bool CoversIndependentLocationThresholds()
	{
		return SelectMinimumExtent(false, 7.0f, 23.0f) == 7.0f &&
		       SelectMinimumExtent(true, 7.0f, 23.0f) == 23.0f &&
		       SelectMinimumExtent(false, kMinimumExtent, kMaximumExtent) == kMinimumExtent &&
		       SelectMinimumExtent(true, kMinimumExtent, kMaximumExtent) == kMaximumExtent;
	}

	bool CoversSettingsBoundaryValidation()
	{
		return SanitizeMinimumExtent(0.0) == kMinimumExtent &&
		       SanitizeMinimumExtent(1000.0) == kMaximumExtent &&
		       SanitizeMinimumExtent(23.5) == 23.5f &&
		       SanitizeMinimumExtent(-1.0) == kMinimumExtent &&
		       SanitizeMinimumExtent(1001.0) == kMaximumExtent &&
		       SanitizeMinimumExtent(std::numeric_limits<double>::max()) == kMaximumExtent &&
		       SanitizeMinimumExtent(std::numeric_limits<double>::lowest()) == kMinimumExtent &&
		       SanitizeMinimumExtent(std::numeric_limits<double>::infinity()) == kDefaultMinimumExtent &&
		       SanitizeMinimumExtent(-std::numeric_limits<double>::infinity()) == kDefaultMinimumExtent &&
		       SanitizeMinimumExtent(std::numeric_limits<double>::quiet_NaN()) == kDefaultMinimumExtent;
	}

	static_assert(CoversIndependentLocationSwitches());
	static_assert(CoversIndependentLocationThresholds());
}

int main()
{
	return CoversIndependentLocationSwitches() &&
	               CoversIndependentLocationThresholds() &&
	               CoversSettingsBoundaryValidation() ?
	           0 :
	           1;
}
