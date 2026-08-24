#include "Features/VR/InSceneOverlaySubmitPolicy.h"
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
		using VRInSceneOverlaySubmitPolicy::SuppressionReason;

		for (std::uint32_t reasonBits = 0; reasonBits < (1u << 4); ++reasonBits) {
			for (std::uint32_t stateBits = 0; stateBits < (1u << 4); ++stateBits) {
				const auto reasons = static_cast<SuppressionReason>(reasonBits);
				const VRInSceneOverlaySubmitPolicy::Admission admission{
					.suppressionReasons = reasons,
					.mainMenuOpen = (stateBits & (1u << 0)) != 0,
					.submitStageUpscalingActive = (stateBits & (1u << 1)) != 0,
					.renderTargetRecreateInProgress = (stateBits & (1u << 2)) != 0,
					.originalSubmitCandidateSafe = (stateBits & (1u << 3)) != 0,
				};
				const bool expected =
					reasons == SuppressionReason::None ||
					(admission.mainMenuOpen &&
						!admission.submitStageUpscalingActive &&
						admission.originalSubmitCandidateSafe &&
						(reasons == SuppressionReason::RenderScaleTransitionPending ||
							(reasons == SuppressionReason::RenderTargetRecreatePending &&
								!admission.renderTargetRecreateInProgress)));
				if (VRInSceneOverlaySubmitPolicy::ShouldAdmit(admission) != expected)
					return false;
			}
		}
		return true;
	}

	static_assert(CoversShaderCompilationStatusAdmission());
	static_assert(CoversShaderCompilationHMDRouting());
	static_assert(CoversVRInSceneOverlaySubmitAdmission());
}

int main() {}
