#include "Features/Upscaling/ReflexPolicy.h"

namespace
{
	using namespace ReflexPolicy;

	constexpr bool CoversMarkerOptimizationAdmission()
	{
		for (unsigned bits = 0; bits < 16; ++bits) {
			const bool reflexAvailable = (bits & (1u << 0)) != 0;
			const bool pclAvailable = (bits & (1u << 1)) != 0;
			const bool requested = (bits & (1u << 2)) != 0;
			const bool authoritativeCoverage = (bits & (1u << 3)) != 0;
			const auto decision = ResolveMarkerOptimization(
				reflexAvailable,
				pclAvailable,
				requested,
				authoritativeCoverage);
			const bool expectedAvailable =
				reflexAvailable && pclAvailable && authoritativeCoverage;
			if (decision.available != expectedAvailable ||
				decision.enabled != (expectedAvailable && requested)) {
				return false;
			}
		}
		return true;
	}

	constexpr bool CoversCurrentCSXMarkerPolicy()
	{
		const auto decision = ResolveCSXMarkerOptimization(true, true, true);
		return !kHasAuthoritativeFullFrameMarkerCoverage &&
		       !decision.available &&
		       !decision.enabled;
	}

	static_assert(CoversMarkerOptimizationAdmission());
	static_assert(CoversCurrentCSXMarkerPolicy());
}

int main() {}
