#include "Features/Upscaling/VRPresentationStretchTelemetryPolicy.h"
#include "Features/VR/InSceneOverlaySubmitPolicy.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>

struct Upscaling
{
	enum class UpscaleMethod
	{
		kNONE,
		kTAA,
		kFSR,
		kDLSS
	};
	enum class VRRenderScaleBackendKind : uint8_t
	{
		None,
		FSRHost,
		FSRRuntime,
		FSR4Runtime,
		DLSS
	};
#include "vr_stable_menu_path_under_test.h"
#include "vr_stable_menu_snapshots_under_test.h"

	struct VRRenderScaleProfileSnapshot
	{
		bool valid = true, active = true;
		uint64_t transitionEpoch = 1;
		uint32_t contractGeneration = 7;
		UpscaleMethod method = UpscaleMethod::kFSR;
		uint32_t renderEyeWidth = 1200, renderEyeHeight = 1300;
		uint32_t displayEyeWidth = 1800, displayEyeHeight = 1950;
	};
};

namespace
{
#include "vr_stable_menu_proof_under_test.h"

	using Path = Upscaling::VRRenderScalePresentationPath;
	using Eye = Upscaling::VRRenderScalePresentationEyeSnapshot;
	using Presentation = Upscaling::VRRenderScalePresentationSnapshot;
	using Profile = Upscaling::VRRenderScaleProfileSnapshot;
	using Proof = Upscaling::VRRenderScaleStereoPresentationProof;
	static_assert(sizeof(Proof) < sizeof(std::array<Eye, 2>));
	constexpr uintptr_t kDeviceIdentity = 0x1234;
	constexpr uint64_t kResourceRevision = 9;

	Eye VendorEye(uint32_t frame, uint64_t cycle, const Profile& profile)
	{
		Eye eye{};
		eye.valid = true;
		eye.path = Path::VendorEvaluated;
		eye.frame = frame;
		eye.compositorCycleToken = cycle;
		eye.transitionEpoch = profile.transitionEpoch;
		eye.contractGeneration = profile.contractGeneration;
		eye.method = profile.method;
		eye.inputWidth = eye.expectedInputWidth = profile.renderEyeWidth;
		eye.inputHeight = eye.expectedInputHeight = profile.renderEyeHeight;
		eye.outputWidth = profile.displayEyeWidth;
		eye.outputHeight = profile.displayEyeHeight;
		eye.deviceIdentity = kDeviceIdentity;
		eye.resourceRevision = kResourceRevision;
#ifdef DEVBENCH_BRIDGE_ENABLED
		eye.vendorBackend = profile.method == Upscaling::UpscaleMethod::kFSR ?
		                        Upscaling::VRRenderScaleBackendKind::FSR4Runtime :
		                        Upscaling::VRRenderScaleBackendKind::DLSS;
#endif
		return eye;
	}

	void Observe(Presentation& presentation, unsigned index, const Eye& eye)
	{
		using VRRenderScalePresentationPath = Path;
		using VRRenderScaleBackendKind = Upscaling::VRRenderScaleBackendKind;
		using VRRenderScaleStereoPresentationProof = Proof;
		presentation.eyes[index] = eye;
#include "vr_stable_menu_observation_under_test.h"
	}

	bool MenuAdmitted(const Profile& stable, const Presentation& presentation,
		uintptr_t device = kDeviceIdentity, uint64_t revision = kResourceRevision)
	{
		using namespace VRInSceneOverlaySubmitPolicy;
		return ShouldAdmit({
			.suppressionReasons = SuppressionReason::RenderScaleTransitionPending |
		                          SuppressionReason::RenderTargetRecreatePending,
			.menuSessionOpen = true,
			.submitStageUpscalingActive = true,
			.originalSubmitCandidateSafe = true,
			.stablePresentationProven = HasExactVRRenderScaleStableStereoPresentation(stable, presentation, device, revision),
		});
	}

	bool DeferredSwitchKeepsMenu(Upscaling::UpscaleMethod method)
	{
		Profile stable{};
		stable.method = method;
		Presentation presentation{};
		const auto vendor = VendorEye(100, 50, stable);
		Observe(presentation, 0, vendor);
		Observe(presentation, 1, vendor);
		if (!MenuAdmitted(stable, presentation))
			return false;
		if (MenuAdmitted(stable, presentation, kDeviceIdentity + 1) ||
			MenuAdmitted(stable, presentation, kDeviceIdentity, kResourceRevision + 1) ||
			MenuAdmitted(stable, presentation, 0) ||
			MenuAdmitted(stable, presentation, kDeviceIdentity, 0))
			return false;

		for (const auto path : { Path::NativeOriginal, Path::ValidatedPresentationHold,
				 Path::PresentationStretch, Path::VendorFailureStretch,
				 Path::BoundsMismatchOriginalFallback }) {
			auto fallback = VendorEye(101, 51, stable);
			fallback.path = path;
			Observe(presentation, 0, fallback);
			Observe(presentation, 1, fallback);
			// Reopening the CS menu must not require another vendor dispatch while
			// memory admission keeps the replacement queued.
			if (!MenuAdmitted(stable, presentation) || presentation.eyes[0].path != path)
				return false;
		}

		auto nextVendor = VendorEye(102, 52, stable);
		Observe(presentation, 0, nextVendor);
		if (!MenuAdmitted(stable, presentation))
			return false;
		Observe(presentation, 1, nextVendor);
		if (!MenuAdmitted(stable, presentation))
			return false;
		presentation = {};
		return !MenuAdmitted(stable, presentation);
	}

	bool RejectsUnprovenOrChangedContract()
	{
		Profile stable{};
		Presentation presentation{};
		if (MenuAdmitted(stable, presentation))
			return false;
		const auto vendor = VendorEye(100, 50, stable);
		Observe(presentation, 0, vendor);
		if (MenuAdmitted(stable, presentation))
			return false;
		Observe(presentation, 1, vendor);
		if (!MenuAdmitted(stable, presentation))
			return false;
		++stable.contractGeneration;
		if (MenuAdmitted(stable, presentation))
			return false;
		--stable.contractGeneration;
		++stable.transitionEpoch;
		if (MenuAdmitted(stable, presentation))
			return false;
		--stable.transitionEpoch;
		++stable.renderEyeWidth;
		return !MenuAdmitted(stable, presentation);
	}

	bool RejectsMixedStereoContract()
	{
		const Profile stable{};
		const auto vendor = VendorEye(100, 50, stable);
		for (unsigned field = 0; field < 11; ++field) {
			Presentation presentation{};
			auto otherEye = vendor;
			switch (field) {
			case 0:
				++otherEye.frame;
				break;
			case 1:
				++otherEye.compositorCycleToken;
				break;
			case 2:
				++otherEye.transitionEpoch;
				break;
			case 3:
				++otherEye.contractGeneration;
				break;
			case 4:
				otherEye.method = Upscaling::UpscaleMethod::kDLSS;
				break;
			case 5:
				++otherEye.inputWidth;
				break;
			case 6:
				++otherEye.expectedInputHeight;
				break;
			case 7:
				++otherEye.outputWidth;
				break;
			case 8:
				++otherEye.deviceIdentity;
				break;
			case 9:
				++otherEye.resourceRevision;
				break;
			case 10:
				otherEye.vendorBackend = Upscaling::VRRenderScaleBackendKind::FSRRuntime;
				break;
			}
			Observe(presentation, 0, vendor);
			Observe(presentation, 1, otherEye);
			if (MenuAdmitted(stable, presentation))
				return false;
		}
		return true;
	}

	bool RetainsProofAcrossRejectedPair()
	{
		const Profile stable{};
		Presentation presentation{};
		const auto vendor = VendorEye(100, 50, stable);
		Observe(presentation, 0, vendor);
		Observe(presentation, 1, vendor);
		auto nextLeft = VendorEye(101, 51, stable);
		auto nextRight = nextLeft;
		++nextRight.contractGeneration;
		Observe(presentation, 0, nextLeft);
		Observe(presentation, 1, nextRight);
		return MenuAdmitted(stable, presentation);
	}

	bool AcceptsPresentationWithoutDiagnosticBackend()
	{
		const Profile stable{};
		Presentation presentation{};
		auto vendor = VendorEye(100, 50, stable);
		vendor.vendorBackend = Upscaling::VRRenderScaleBackendKind::None;
		Observe(presentation, 1, vendor);
		if (MenuAdmitted(stable, presentation))
			return false;
		Observe(presentation, 0, vendor);
		if (!MenuAdmitted(stable, presentation))
			return false;
#ifdef DEVBENCH_BRIDGE_ENABLED
		const auto proof = presentation.lastVendorStereoProof;
		if (presentation.lastCoherentVendorEyes[0].valid ||
			presentation.lastCoherentVendorEyes[1].valid)
			return false;

		const auto attributed = VendorEye(101, 51, stable);
		Observe(presentation, 0, attributed);
		Observe(presentation, 1, attributed);
		for (const auto& eye : presentation.lastCoherentVendorEyes) {
			if (!eye.valid || eye.frame != attributed.frame ||
				eye.vendorBackend != attributed.vendorBackend)
				return false;
		}

		vendor.frame = 102;
		vendor.compositorCycleToken = 52;
		Observe(presentation, 1, vendor);
		Observe(presentation, 0, vendor);
		if (presentation.lastVendorStereoProof != proof)
			return false;
		for (const auto& eye : presentation.lastCoherentVendorEyes) {
			if (eye.frame != attributed.frame || eye.vendorBackend != attributed.vendorBackend)
				return false;
		}
#endif
		return MenuAdmitted(stable, presentation);
	}

	bool RejectsUnprovenPairsWithoutDiagnosticBackend()
	{
		const Profile stable{};
		auto vendor = VendorEye(100, 50, stable);
		vendor.vendorBackend = Upscaling::VRRenderScaleBackendKind::None;
		const auto rejected = [&](const Eye& eye) {
			Presentation presentation{};
			Observe(presentation, 0, eye);
			Observe(presentation, 1, eye);
			return !MenuAdmitted(stable, presentation);
		};
		for (const auto path : { Path::Unknown, Path::NativeOriginal,
				 Path::ValidatedPresentationHold, Path::PresentationStretch,
				 Path::VendorFailureStretch, Path::BoundsMismatchOriginalFallback }) {
			auto fallback = vendor;
			fallback.path = path;
			if (!rejected(fallback))
				return false;
		}
		auto invalid = vendor;
		invalid.valid = false;
		if (!rejected(invalid))
			return false;
		invalid = vendor;
		invalid.deviceIdentity = 0;
		if (!rejected(invalid))
			return false;
		invalid = vendor;
		invalid.resourceRevision = 0;
		if (!rejected(invalid))
			return false;
		invalid = vendor;
		invalid.compositorCycleToken = 0;
		return rejected(invalid);
	}

	bool RefreshesChangedStereoIdentity()
	{
		for (unsigned firstEye : { 0u, 1u }) {
			for (unsigned field = 0; field < 9; ++field) {
				const Profile stable{};
				Presentation presentation{};
				const auto vendor = VendorEye(100, 50, stable);
				Observe(presentation, firstEye, vendor);
				Observe(presentation, 1u - firstEye, vendor);
				auto next = stable;
				auto device = kDeviceIdentity;
				auto revision = kResourceRevision;
				switch (field) {
				case 0:
					++next.transitionEpoch;
					break;
				case 1:
					++next.contractGeneration;
					break;
				case 2:
					next.method = Upscaling::UpscaleMethod::kDLSS;
					break;
				case 3:
					++next.renderEyeWidth;
					break;
				case 4:
					++next.renderEyeHeight;
					break;
				case 5:
					++next.displayEyeWidth;
					break;
				case 6:
					++next.displayEyeHeight;
					break;
				case 7:
					++device;
					break;
				case 8:
					++revision;
					break;
				}
				auto nextVendor = VendorEye(101, 51, next);
				nextVendor.deviceIdentity = device;
				nextVendor.resourceRevision = revision;
				Observe(presentation, firstEye, nextVendor);
				if (!MenuAdmitted(stable, presentation) || MenuAdmitted(next, presentation, device, revision))
					return false;
				Observe(presentation, 1u - firstEye, nextVendor);
				if (MenuAdmitted(stable, presentation) || !MenuAdmitted(next, presentation, device, revision))
					return false;

				const auto proof = presentation.lastVendorStereoProof;
				++nextVendor.frame;
				++nextVendor.compositorCycleToken;
				++nextVendor.vendorDispatchFrame;
				++nextVendor.vendorDispatchSerial;
				Observe(presentation, firstEye, nextVendor);
				Observe(presentation, 1u - firstEye, nextVendor);
				if (presentation.lastVendorStereoProof != proof || !MenuAdmitted(next, presentation, device, revision))
					return false;
			}
		}
		return true;
	}

	bool RejectsMalformedPairAfterValidProof()
	{
		const Profile stable{};
		for (unsigned field = 0; field < 3; ++field) {
			Presentation presentation{};
			const auto vendor = VendorEye(100, 50, stable);
			Observe(presentation, 0, vendor);
			Observe(presentation, 1, vendor);
			auto invalid = VendorEye(101, 51, stable);
			if (field == 0)
				invalid.frame = 0;
			else if (field == 1)
				++invalid.expectedInputWidth;
			else
				++invalid.expectedInputHeight;
			Observe(presentation, 0, invalid);
			Observe(presentation, 1, invalid);
			if (MenuAdmitted(stable, presentation))
				return false;
			const auto recovered = VendorEye(102, 52, stable);
			Observe(presentation, 0, recovered);
			Observe(presentation, 1, recovered);
			if (!MenuAdmitted(stable, presentation))
				return false;
		}
		return true;
	}
}

int main()
{
	if (!DeferredSwitchKeepsMenu(Upscaling::UpscaleMethod::kFSR) ||
		!DeferredSwitchKeepsMenu(Upscaling::UpscaleMethod::kDLSS)) {
		std::puts("Deferred vendor switch lost the VR menu after a fallback eye publication");
		return 1;
	}
	if (!RejectsUnprovenOrChangedContract() || !RejectsMixedStereoContract() ||
		!RetainsProofAcrossRejectedPair()) {
		std::puts("VR menu accepted missing or mismatched stereo evidence");
		return 1;
	}
	if (!AcceptsPresentationWithoutDiagnosticBackend() ||
		!RejectsUnprovenPairsWithoutDiagnosticBackend()) {
		std::puts("VR menu continuity depends on optional vendor diagnostics");
		return 1;
	}
	if (!RefreshesChangedStereoIdentity() || !RejectsMalformedPairAfterValidProof()) {
		std::puts("VR menu retained stale or malformed stereo proof");
		return 1;
	}
	std::printf("VR stereo proof: %zu bytes instead of %zu; continuity and identity checks passed\n",
		sizeof(Proof), sizeof(std::array<Eye, 2>));
	return 0;
}
