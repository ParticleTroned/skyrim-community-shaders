#include "Features/Upscaling/FSRHostLifecyclePolicy.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace globals::game
{
	bool isVR = true;
}
namespace FidelityFX
{
	enum class LifecycleResult
	{
		Ready,
		Pending,
		Failed
	};
}

// Exercise the production admission and lifecycle helpers without vendor teardown.
struct Upscaling
{
	enum class UpscaleMethod
	{
		kDLSS,
		kFSR,
		kTAA
	};
	enum class VRVendorRuntimeLifecyclePhase
	{
		Ready,
		Failed
	};
	enum class VRUpscalingTransitionOrigin
	{
		Other,
		RecoveryRelatch
	};
	struct Profile
	{
		bool valid = false, active = false;
		UpscaleMethod method = UpscaleMethod::kTAA;
		uint32_t contractGeneration = 0;
		VRUpscalingTransitionOrigin origin = VRUpscalingTransitionOrigin::Other;
	};
	struct VRRenderScaleTransitionSnapshot
	{
		struct Lifecycle
		{
			VRVendorRuntimeLifecyclePhase phase = VRVendorRuntimeLifecyclePhase::Ready;
			uint32_t transitionEpoch = 0, requestedGeneration = 0;
		} fsrLifecycle;
		uint32_t targetEpoch = 1;
		Profile stable, requested;
	} controller;
	struct Host
	{
		bool quarantined = false;
		bool IsHostFSRStateQuarantined() const { return quarantined; }
	} fidelityFX;
	std::atomic<bool> pendingDLSSReset = false, pendingFSRReset = false;
	std::atomic<bool> pendingPerfModeRenderTargetRecreate = false, perfModeRenderTargetRecreateInProgress = false;
	std::optional<uint64_t> fsrResourceFailureRequestKey;
	uint32_t activeGeneration = 2, vrDLSSRuntimeResourceGeneration = 2, vrFSRRuntimeResourceGeneration = 2;
	uint64_t requestKey = 7;
	bool includeInactive = true, loading = false;
	mutable unsigned keyReads = 0, snapshotReads = 0, generationReads = 0;
	unsigned resetPathCalls = 0;
	uint32_t GetVRVendorEvaluationContractGeneration(UpscaleMethod) const
	{
		++generationReads;
		return activeGeneration;
	}
	VRRenderScaleTransitionSnapshot GetVRRenderScaleTransitionSnapshot() const
	{
		++snapshotReads;
		return controller;
	}
	bool HandleFSRLifecycleDeviceLoss(FidelityFX::LifecycleResult, const char*) { return false; }
	bool ApplyPendingVendorRuntimeReset(UpscaleMethod, const char*);
};

uint64_t BuildFSRResourceLifecycleRequestKey(const Upscaling& upscaling, Upscaling::UpscaleMethod)
{
	++upscaling.keyReads;
	return upscaling.requestKey;
}
bool ShouldIncludeInactiveVRVendorReset(const Upscaling& upscaling, Upscaling::UpscaleMethod) { return upscaling.includeInactive; }
bool IsUpscalingLoadTransitionContextActive(const Upscaling& upscaling) { return upscaling.loading; }
bool ShouldRetainInactiveFSROwnership(const Upscaling&, Upscaling::UpscaleMethod, std::optional<bool> = std::nullopt);

#include "vendor_lifecycle_under_test.h"
#include "vendor_reset_under_test.h"

void Require(bool value)
{
	if (!value)
		throw std::runtime_error("Vendor reset admission regression");
}

int main()
{
	using Method = Upscaling::UpscaleMethod;
	for (bool quarantine : { false, true }) {
		for (auto active : { 0u, 2u }) {
			for (auto runtime : { 0u, 2u }) {
				Upscaling up;
				up.fidelityFX.quarantined = quarantine;
				up.activeGeneration = active;
				up.vrDLSSRuntimeResourceGeneration = runtime;
				Require(up.ApplyPendingVendorRuntimeReset(Method::kDLSS, ""));
				Require(up.keyReads == 0 && up.snapshotReads == 0 && up.generationReads == 1);
			}
		}
	}
	for (unsigned trigger = 0; trigger < 3; ++trigger) {
		Upscaling up;
		up.activeGeneration = trigger == 0 ? 3 : 2;
		up.pendingDLSSReset = trigger == 1;
		up.pendingFSRReset = trigger == 2;
		Require(!up.ApplyPendingVendorRuntimeReset(Method::kDLSS, ""));
		Require(up.resetPathCalls == 1 && up.keyReads == 1 && up.snapshotReads == 1);
	}
	for (bool matches : { false, true }) {
		Upscaling up;
		up.fsrResourceFailureRequestKey = matches ? up.requestKey : up.requestKey + 1;
		up.pendingFSRReset = true;
		Require(up.ApplyPendingVendorRuntimeReset(Method::kDLSS, "") == matches);
		Require(up.fsrResourceFailureRequestKey.has_value() == matches);
		Require(up.keyReads == 1 && up.snapshotReads == (matches ? 0u : 1u));
	}
	for (bool quarantine : { false, true }) {
		Upscaling up;
		up.fidelityFX.quarantined = quarantine;
		up.pendingFSRReset = true;
		Require(up.ApplyPendingVendorRuntimeReset(Method::kDLSS, "") == quarantine);
		Require(up.keyReads == 1 && up.snapshotReads == 1);
	}
	for (bool nativeRecovery : { false, true }) {
		Upscaling up;
		up.controller.fsrLifecycle.phase = Upscaling::VRVendorRuntimeLifecyclePhase::Failed;
		up.controller.fsrLifecycle.transitionEpoch = nativeRecovery ? 0 : up.controller.targetEpoch;
		up.controller.fsrLifecycle.requestedGeneration = 4;
		up.controller.stable = { true, false, Method::kFSR, 4 };
		up.controller.requested = { true, false, Method::kTAA, 0, Upscaling::VRUpscalingTransitionOrigin::RecoveryRelatch };
		Require(!up.ApplyPendingVendorRuntimeReset(Method::kFSR, ""));
		Require(up.resetPathCalls == 0 && up.keyReads == 1 && up.snapshotReads == 1);
	}
	{
		Upscaling up;
		up.fsrResourceFailureRequestKey = up.requestKey;
		Require(!up.ApplyPendingVendorRuntimeReset(Method::kFSR, ""));
		Require(up.resetPathCalls == 0 && up.keyReads == 1 && up.snapshotReads == 0);
	}
	{
		Upscaling up;
		up.includeInactive = false;
		up.pendingFSRReset = true;
		Require(up.ApplyPendingVendorRuntimeReset(Method::kDLSS, ""));
		Require(up.resetPathCalls == 0 && up.keyReads == 1 && up.snapshotReads == 1);
	}
	for (unsigned defer = 0; defer < 4; ++defer) {
		Upscaling up;
		up.pendingDLSSReset = true;
		up.loading = defer == 0;
		up.pendingPerfModeRenderTargetRecreate = defer == 1;
		up.perfModeRenderTargetRecreateInProgress = defer == 2;
		globals::game::isVR = defer != 3;
		Require(up.ApplyPendingVendorRuntimeReset(Method::kDLSS, "") && up.resetPathCalls == 0);
		Require(up.keyReads == (defer == 3 ? 0u : 1u));
	}
}
