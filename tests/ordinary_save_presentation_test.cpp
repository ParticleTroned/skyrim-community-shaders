#include "Features/Upscaling/VROrdinarySaveRecovery.h"
#include "Features/Upscaling/VRSubmitColorContract.h"
#include "Features/Upscaling/VRVendorRelatchPolicy.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <optional>

struct State
{
	uint64_t token = 9;
	uint32_t lastWorldRenderFrame = 10;
	uint32_t lastCompletedWorldRenderFrame = 10;
	bool pendingPostLoadRuntimeReset = false;
	bool safeMode = true;
	uint64_t GetOrdinarySaveRenderRecoveryToken() const { return token; }
	bool IsSaveLoadSafeModeActive() const { return safeMode; }
};

namespace globals
{
	State* state = nullptr;
	namespace game
	{
		bool isVR = true;
	}
}

namespace logger
{
	template <class... Args>
	void debug(const char*, Args&&...)
	{}
}

template <class Callback>
struct ScopeExit
{
	Callback callback;
	~ScopeExit() { callback(); }
};

class Upscaling
{
public:
	enum class UpscaleMethod : uint32_t
	{
		kFSR = 3
	};
	bool latched = true;
	bool lifecycleDeferred = false;
	bool transitionPending = false;
	bool vendorReset = false;
	bool deviceLost = false;
	bool vendorReady = true;
	bool commonReady = true;
	bool converged = true;
	bool menu = false;
	bool loading = false;
	bool protectedLoading = false;
	uint64_t resourceKey = 13;
	uint32_t activeGeneration = 4;
	uint64_t commonVendorResourceGeneration = 1;
	uint32_t vrIntermediateTextureGeneration = 4;
	std::atomic_bool postLoadRuntimeResetPending{ false };
	std::atomic_uint64_t submitTemporalCompositorCycle{ 10 };
	mutable std::mutex ordinarySaveRecoveryMutex;
	VROrdinarySaveRecovery::Proof ordinarySaveRecovery;
	uint64_t submitStageVendorAdmissionCycle = 0;
	uint32_t submitStageVendorAdmissionGeneration = 0;
	uint32_t submitStageVendorAdmissionMethod = 0;
	VRSubmitColorContract::Contract submitStageVendorAdmissionColorContract{};
	bool submitStageVendorAdmissionPresentationOnly = false;
	bool submitStageVendorAdmissionExactProviderReady = false;
	bool submitStageVendorAdmissionAuthoritativeDLSSProfile = false;
	uint32_t submitStageVendorAdmissionDLSSQualityMode = 0;
	uint32_t submitStageVendorAdmissionDLSSPreset = 0;
	uint32_t submitStageVendorAdmissionFrame = 0;
	uint32_t submitStageVendorAdmissionEyeMask = 0;

	bool IsVRRenderScaleModeLatched() const { return latched; }
	bool ShouldDeferVRVendorLifecycleMutation() const { return lifecycleDeferred; }
	bool HasPendingVRUpscalingTransition() const { return transitionPending; }
	bool IsSubmitStageDeviceLost() const { return deviceLost; }
	bool IsVendorRuntimeReadyForActiveContract(UpscaleMethod) const { return vendorReady; }
	bool IsCommonVendorResourceContractCurrent(UpscaleMethod) const { return commonReady; }
	bool IsVRRenderScalePhysicalContractConverged(UpscaleMethod, uint32_t) const { return converged; }
	UpscaleMethod GetRuntimeUpscaleMethod() const { return UpscaleMethod::kFSR; }
	uint32_t GetRuntimeQualityMode() const { return 2; }
	uint32_t GetActiveVRRenderScaleContractGeneration() const { return activeGeneration; }
	bool IsNonLoadingVRMenuPresentationContextActive() const { return menu; }
	bool IsLoadingMenuContextActive() const { return loading; }
	void RequestHistoryReset() {}
	bool ShouldReuseOrdinarySaveResources() const;
	std::optional<VROrdinarySaveRecovery::Identity> GetOrdinarySavePresentationIdentity() const;
	bool CanResumeOrdinarySavePresentation() const;
	void ObserveOrdinarySavePresentation(uint32_t, uint64_t, uint32_t,
		const VRSubmitInputFreshnessPolicy::SubmitBoundaryIdentity&,
		const VROrdinarySaveRecovery::Identity&, bool);

	bool maskPreview = false;
	bool Submit(uint32_t a_eye, bool a_inputsReady, bool a_outputReady,
		const std::function<void()>& a_duringOutput = {})
	{
		const auto a_compositorCycleToken = submitTemporalCompositorCycle.load();
		const VRSubmitInputFreshnessPolicy::SubmitBoundaryIdentity a_submitBoundaryIdentity{
			0, a_compositorCycleToken + 1000, 0
		};
#include "ordinary_save_submit_finish_under_test.h"
		bool presentationOnly = false;
		const auto upscaleMethod = GetRuntimeUpscaleMethod();
		const uint32_t activeContractGeneration = activeGeneration;
		const uint32_t currentFrame = globals::state->lastCompletedWorldRenderFrame;
		const auto sourceColorContract = VRSubmitColorContract::Resolve(true,
			VRSubmitColorContract::SourceColorSpace::Gamma);
		const bool foveatedMaskVisualizationPreview = maskPreview;
		bool vendorLifecycleMutationDeferred = lifecycleDeferred;
		bool exactExistingProviderReady = false;
		bool useAuthoritativeDLSSProfile = false;
		uint32_t authoritativeDLSSQualityMode = 0;
		uint32_t authoritativeDLSSPreset = 0;
#include "ordinary_save_submit_admission_under_test.h"
		ordinarySaveProducerFrame = globals::state->lastCompletedWorldRenderFrame;
		ordinarySaveIdentity = GetOrdinarySavePresentationIdentity().value_or(VROrdinarySaveRecovery::Identity{});
		ordinarySaveInputsReady = a_inputsReady;
		if (a_duringOutput)
			a_duringOutput();
		if (!a_outputReady)
			return presentationOnly;
#include "ordinary_save_preview_output_under_test.h"
		return presentationOnly;
	}
};

bool IsVRLoadingSubmitProtectionContextActive(const Upscaling& a_upscaling, State*)
{
	return a_upscaling.protectedLoading;
}

bool HasPendingVRVendorRuntimeReset(const Upscaling& a_upscaling, Upscaling::UpscaleMethod)
{
	return a_upscaling.vendorReset;
}

uint64_t BuildResourceCheckStableKey(const Upscaling& a_upscaling, Upscaling::UpscaleMethod)
{
	return a_upscaling.resourceKey;
}

#include "ordinary_save_presentation_under_test.h"

int main()
{
	int failures = 0;
	auto check = [&](bool value, const char* message) {
		if (!value) {
			std::fprintf(stderr, "%s\n", message);
			++failures;
		}
	};
	State state;
	globals::state = &state;
	Upscaling upscaling;
	auto frame = [&](uint32_t a_frame) {
		state.lastWorldRenderFrame = state.lastCompletedWorldRenderFrame = a_frame;
		upscaling.submitTemporalCompositorCycle = a_frame;
	};
	auto qualify = [&](uint32_t a_firstFrame) {
		upscaling.ordinarySaveRecovery.Reset();
		for (uint32_t current = a_firstFrame; current < a_firstFrame + 6; ++current) {
			frame(current);
			check(upscaling.Submit(0, true, true), "First qualifying eye must use stretch while mask repair is guarded");
			check(upscaling.Submit(1, true, true), "Second qualifying eye must share the stretch decision");
			check(!upscaling.CanResumeOrdinarySavePresentation(), "A successful qualifying pair cannot release its current cycle");
		}
		frame(a_firstFrame + 6);
		check(upscaling.CanResumeOrdinarySavePresentation(), "Successful stretch pairs bootstrap the following vendor cycle");
	};
	qualify(10);
	check(!upscaling.Submit(0, true, true) && !upscaling.Submit(1, true, true),
		"Both eyes resume normal presentation in the next compositor cycle");
	check(state.safeMode, "Presentation release preserves independent save protection");

	frame(17);
	upscaling.Submit(0, true, false);
	check(!upscaling.CanResumeOrdinarySavePresentation() && !upscaling.ordinarySaveRecovery.qualifiedCycle,
		"A submit failing after input observation revokes all earlier stereo proof");
	check(upscaling.Submit(1, true, true), "The peer of a failed submit returns to presentation stretch");
	check(upscaling.ordinarySaveRecovery.stableFrames == 0, "A failed eye cannot count as a completed pair");
	state.token = 0;
	state.safeMode = false;
	check(upscaling.Submit(0, true, true),
		"A revoked stereo admission remains stretch if the save grace expires within the same cycle");
	frame(18);
	check(!upscaling.Submit(0, true, true) && !upscaling.Submit(1, true, true),
		"Expired save protection releases the cached downgrade in the next compositor cycle");
	state.token = 9;
	state.safeMode = true;

	qualify(30);
	upscaling.Submit(0, true, true, [&] { ++upscaling.commonVendorResourceGeneration; });
	check(!upscaling.ordinarySaveRecovery.qualifiedCycle,
		"Resource replacement between candidate capture and output completion revokes proof");

	qualify(40);
	check(!upscaling.Submit(0, true, true), "A qualified first eye retains vendor admission");
	++state.token;
	check(upscaling.Submit(1, true, true),
		"A new save between successful eye submissions overrides the cached vendor admission");
	check(!upscaling.ordinarySaveRecovery.qualifiedCycle,
		"The new save cannot inherit qualification from the completed first eye");
	state.token = 9;

	qualify(50);
	upscaling.Submit(0, false, true);
	check(!upscaling.ordinarySaveRecovery.qualifiedCycle,
		"Successful fallback output cannot substitute for missing temporal inputs");

	qualify(70);
	upscaling.maskPreview = true;
	check(upscaling.Submit(0, true, true) && upscaling.Submit(1, true, true),
		"Mask preview must use presentation without a vendor evaluation");
	check(!upscaling.ordinarySaveRecovery.qualifiedCycle,
		"A successful mask preview must revoke earlier save recovery proof");
	for (uint32_t current = 77; current < 84; ++current) {
		frame(current);
		upscaling.Submit(0, true, true);
		upscaling.Submit(1, true, true);
	}
	check(!upscaling.CanResumeOrdinarySavePresentation() && upscaling.ordinarySaveRecovery.stableFrames == 0,
		"Preview eye pairs must never qualify ordinary-save recovery");
	upscaling.maskPreview = false;
	qualify(90);
	for (bool* gate : { &upscaling.lifecycleDeferred, &upscaling.transitionPending, &upscaling.vendorReset,
			 &upscaling.deviceLost, &upscaling.menu, &upscaling.loading, &upscaling.protectedLoading,
			 &state.pendingPostLoadRuntimeReset }) {
		*gate = true;
		check(!upscaling.CanResumeOrdinarySavePresentation(), "Each concrete unsafe gate blocks retained proof");
		*gate = false;
	}
	for (bool* contract : { &upscaling.latched, &upscaling.vendorReady, &upscaling.commonReady, &upscaling.converged }) {
		*contract = false;
		check(!upscaling.CanResumeOrdinarySavePresentation(), "Every resource contract predicate is required at release");
		*contract = true;
	}
	state.token = 0;
	check(!upscaling.Submit(0, true, true), "Ineligible load provenance does not select ordinary-save stretch");
	state.token = 9;
	globals::game::isVR = false;
	check(!upscaling.Submit(0, true, true), "SE and AE do not select ordinary-save stretch");
	globals::game::isVR = true;
	std::printf("Ordinary-save presentation integration: %d failures\n", failures);
	return failures ? 1 : 0;
}
