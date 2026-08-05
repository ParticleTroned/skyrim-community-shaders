#include "Menu/OverlayPolicy.h"

#include <cstdint>

namespace
{
	constexpr bool CoversShaderCompilationStatusAdmission()
	{
		for (std::uint32_t bits = 0; bits < (1u << 3); ++bits) {
			const OverlayPolicy::ShaderCompilationStatusAdmission admission{
				.hasRenderedWorldFrame = (bits & (1u << 0)) != 0,
				.menuSessionOpen = (bits & (1u << 1)) != 0,
				.performanceOverlayOpen = (bits & (1u << 2)) != 0,
			};
			const bool expected =
				!admission.hasRenderedWorldFrame ||
				admission.menuSessionOpen ||
				admission.performanceOverlayOpen;
			if (OverlayPolicy::ShouldShowShaderCompilationStatus(admission) != expected)
				return false;
		}
		return true;
	}

	static_assert(CoversShaderCompilationStatusAdmission());
}

int main() {}
