#include "Features/VR/InSceneOverlaySubmitPolicy.h"
#include "Features/VR/OpenVRSubmitLeasePolicy.h"
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
		constexpr auto queuedReplacementMask =
			static_cast<std::uint8_t>(
				SuppressionReason::RenderScaleTransitionPending) |
			static_cast<std::uint8_t>(
				SuppressionReason::RenderTargetRecreatePending);

		// Include one unknown bit so future/invalid reasons remain fail-closed.
		for (std::uint32_t reasonBits = 0; reasonBits < (1u << 5); ++reasonBits) {
			for (std::uint32_t stateBits = 0; stateBits < (1u << 8); ++stateBits) {
				const auto reasons = static_cast<SuppressionReason>(reasonBits);
				const VRInSceneOverlaySubmitPolicy::Admission admission{
					.suppressionReasons = reasons,
					.mainMenuOpen = (stateBits & (1u << 0)) != 0,
					.menuSessionOpen = (stateBits & (1u << 1)) != 0,
					.submitStageUpscalingActive = (stateBits & (1u << 2)) != 0,
					.renderTargetRecreateInProgress = (stateBits & (1u << 3)) != 0,
					.originalSubmitCandidateSafe = (stateBits & (1u << 4)) != 0,
					.stablePresentationProven = (stateBits & (1u << 5)) != 0,
					.deviceLost = (stateBits & (1u << 6)) != 0,
					.nativeRestoreGuardActive = (stateBits & (1u << 7)) != 0,
				};
				const bool hardFailure =
					admission.deviceLost ||
					admission.renderTargetRecreateInProgress ||
					admission.nativeRestoreGuardActive ||
					!admission.originalSubmitCandidateSafe;
				const bool mainMenuException =
					admission.mainMenuOpen &&
					!admission.submitStageUpscalingActive &&
					(reasons == SuppressionReason::RenderScaleTransitionPending ||
						reasons == SuppressionReason::RenderTargetRecreatePending);
				const auto reasonBitsValue = static_cast<std::uint8_t>(reasons);
				const bool queuedReplacementOnly =
					reasonBitsValue != 0 &&
					(reasonBitsValue & queuedReplacementMask) == reasonBitsValue;
				if (VRInSceneOverlaySubmitPolicy::IsQueuedReplacementOnly(reasons) !=
					queuedReplacementOnly) {
					return false;
				}
				const bool csMenuStableProvider =
					admission.menuSessionOpen &&
					admission.stablePresentationProven &&
					queuedReplacementOnly;
				const bool expected =
					!hardFailure &&
					(reasons == SuppressionReason::None ||
						mainMenuException ||
						csMenuStableProvider);
				if (VRInSceneOverlaySubmitPolicy::ShouldAdmit(admission) != expected)
					return false;
			}
		}
		return true;
	}

	constexpr bool CoversOpenVRPayloadSelection()
	{
		using OpenVRSubmitLeasePolicy::PayloadKind;
		using OpenVRSubmitLeasePolicy::SelectPayloadKind;

		return SelectPayloadKind(false, false) == PayloadKind::Texture &&
		       SelectPayloadKind(true, false) == PayloadKind::TextureWithPose &&
		       SelectPayloadKind(false, true) == PayloadKind::TextureWithDepth &&
		       SelectPayloadKind(true, true) ==
				       PayloadKind::TextureWithPoseAndDepth;
	}

	constexpr bool CoversOpenVRPublicationLease()
	{
		using OpenVRSubmitLeasePolicy::CanPublish;
		using OpenVRSubmitLeasePolicy::PublicationLease;

		PublicationLease lease{
			.generation = 17,
			.deviceIdentity = 0x1234,
			.colorTextureRetained = true,
		};
		if (!CanPublish(lease, 17, 0x1234) ||
			CanPublish(lease, 18, 0x1234) ||
			CanPublish(lease, 17, 0x5678)) {
			return false;
		}
		lease.generation = 0;
		if (CanPublish(lease, 0, 0x1234))
			return false;
		lease.generation = 17;

		lease.depthTextureRequired = true;
		if (CanPublish(lease, 17, 0x1234))
			return false;
		lease.depthTextureRetained = true;
		if (!CanPublish(lease, 17, 0x1234))
			return false;

		lease.colorTextureRetained = false;
		return !CanPublish(lease, 17, 0x1234);
	}

	static_assert(CoversShaderCompilationStatusAdmission());
	static_assert(CoversShaderCompilationHMDRouting());
	static_assert(CoversVRInSceneOverlaySubmitAdmission());
	static_assert(CoversOpenVRPayloadSelection());
	static_assert(CoversOpenVRPublicationLease());
}

int main() {}
