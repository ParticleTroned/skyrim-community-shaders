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

	constexpr bool CoversShaderCompilationHMDRouting()
	{
		for (std::uint32_t bits = 0; bits < (1u << 2); ++bits) {
			const bool statusAdmitted = (bits & (1u << 0)) != 0;
			const bool showInHMD = (bits & (1u << 1)) != 0;
			if (OverlayPolicy::ShouldRouteShaderCompilationStatusToHMD(statusAdmitted, showInHMD) !=
				(statusAdmitted && showInHMD))
				return false;
		}
		return true;
	}

	constexpr bool CoversVRInSceneOverlaySubmitAdmission()
	{
		for (std::uint32_t bits = 0; bits < (1u << 3); ++bits) {
			const OverlayPolicy::VRInSceneOverlaySubmitAdmission admission{
				.suppressInSceneOverlaySubmit = (bits & (1u << 0)) != 0,
				.mainMenuOpen = (bits & (1u << 1)) != 0,
				.submitStageUpscalingActive = (bits & (1u << 2)) != 0,
			};
			const bool expected =
				!admission.suppressInSceneOverlaySubmit ||
				(admission.mainMenuOpen && !admission.submitStageUpscalingActive);
			if (OverlayPolicy::ShouldAdmitVRInSceneOverlaySubmit(admission) != expected)
				return false;
		}
		return true;
	}

	static_assert(CoversShaderCompilationStatusAdmission());
	static_assert(CoversShaderCompilationHMDRouting());
	static_assert(CoversVRInSceneOverlaySubmitAdmission());
}

int main() {}
