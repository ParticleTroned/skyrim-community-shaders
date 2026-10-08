#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>

// Compile the production snapshot producer and consumer around the real queue
// operation; only unrelated renderer and controller telemetry are omitted.
struct Upscaling
{
	enum class UpscaleMethod
	{
		kNONE,
		kTAA,
		kFSR,
		kDLSS
	};
	enum class VRUpscalingTransitionOrigin
	{
		CSMenu,
		RecoveryRelatch,
		PostLoadSync
	};
	struct PerfModeState
	{
		struct BootSnapshot
		{
			bool valid = false;
			bool active = false;
			UpscaleMethod method = UpscaleMethod::kNONE;
		} boot;
		const BootSnapshot& GetBootSnapshot() const { return boot; }
	} perfMode;
	struct VRRenderScaleProfileSnapshot
	{
		bool valid = false;
		uint64_t requestID = 0;
		uint64_t transitionEpoch = 0;
		UpscaleMethod method = UpscaleMethod::kNONE;
		bool renderScaleModeEnabled = false;
		bool perfModeEnabled = false;
		VRUpscalingTransitionOrigin origin = VRUpscalingTransitionOrigin::CSMenu;
	};
	struct VRRenderScaleDesiredProfile : VRRenderScaleProfileSnapshot
	{
		bool pending = false;
		bool HasPendingSettings() const { return pending; }
	};
	struct VRRenderScaleTransitionSnapshot
	{
		uint64_t targetEpoch = 0;
		VRRenderScaleProfileSnapshot requested{}, applying{};
	} controller;
	PerfModeState::BootSnapshot pendingVRRenderScaleRecoverySnapshot;
	std::atomic<bool> pendingPerfModeRenderTargetRecreateForcePhysical = false;
	std::optional<VRRenderScaleDesiredProfile> pendingVRRenderScaleRequest;
	mutable std::mutex pendingVRRenderScaleRequestMutex;

	VRRenderScaleDesiredProfile GetPendingVRRenderScaleDesiredProfile() const
	{
		std::scoped_lock lock(pendingVRRenderScaleRequestMutex);
		return pendingVRRenderScaleRequest.value_or(VRRenderScaleDesiredProfile{});
	}
	VRRenderScaleTransitionSnapshot GetVRRenderScaleTransitionSnapshot() const { return controller; }
	std::optional<VRRenderScaleDesiredProfile> TakePendingVRRenderScaleRequest();
	bool HasPendingVRUpscalingTransition() const;
	void CaptureRecoverySnapshot(VRUpscalingTransitionOrigin, const PerfModeState::BootSnapshot*, bool, uint64_t);
};

using Method = Upscaling::UpscaleMethod;
using Origin = Upscaling::VRUpscalingTransitionOrigin;
using Boot = Upscaling::PerfModeState::BootSnapshot;
using Profile = Upscaling::VRRenderScaleProfileSnapshot;

bool IsVRRenderScaleRecoveryOrigin(Origin origin)
{
	return origin == Origin::RecoveryRelatch;
}

#include "vr_recovery_contract_under_test.h"

void Require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

void CheckNativeFallback(Method failedMethod, Method nativeMethod)
{
	Upscaling state;
	state.perfMode.boot = { true, true, failedMethod };
	const auto failedBoot = state.perfMode.boot;
	Upscaling::VRRenderScaleDesiredProfile fallback;
	fallback.valid = true;
	fallback.requestID = 12;
	fallback.transitionEpoch = 23;
	fallback.method = nativeMethod;
	fallback.origin = Origin::RecoveryRelatch;
	fallback.pending = true;
	state.controller.targetEpoch = fallback.transitionEpoch;
	state.controller.requested = fallback;
	state.pendingVRRenderScaleRequest = fallback;
	Require(!ShouldPreserveActiveVRRenderScaleContractForRecovery(state, Origin::RecoveryRelatch, failedBoot),
		"Queued native recovery must override the failed vendor snapshot");
	const auto consumed = state.TakePendingVRRenderScaleRequest();
	Require(consumed.has_value() && !state.HasPendingVRUpscalingTransition(), "Recovery request must leave the pending queue");

	for (const auto origin : { Origin::RecoveryRelatch, Origin::PostLoadSync }) {
		for (const bool suppliedSnapshot : { false, true }) {
			state.CaptureRecoverySnapshot(origin, suppliedSnapshot ? &failedBoot : nullptr, false, fallback.transitionEpoch);
			Require(!state.pendingVRRenderScaleRecoverySnapshot.valid && !state.pendingVRRenderScaleRecoverySnapshot.active,
				"Native recovery must discard the failed boot when publishing the physical tuple");
			Require(!state.pendingPerfModeRenderTargetRecreateForcePhysical.load(),
				"Ordinary native fallback must retain normal physical relatch policy");
			Require(!ShouldPreserveActiveVRRenderScaleContractForRecovery(state, origin, state.pendingVRRenderScaleRecoverySnapshot),
				"Consumed native recovery must not preserve the failed vendor snapshot");
		}
	}
	state.controller.applying = *consumed;
	state.controller.requested = {};
	state.CaptureRecoverySnapshot(Origin::RecoveryRelatch, &failedBoot, false, fallback.transitionEpoch);
	const auto captured = state.pendingVRRenderScaleRecoverySnapshot;
	state.controller = {};
	Require(!ShouldPreserveActiveVRRenderScaleContractForRecovery(state, Origin::RecoveryRelatch, captured),
		"Retiring the controller owner must not revive the failed contract in a published tuple");

	state.controller.targetEpoch = fallback.transitionEpoch;
	state.controller.requested = fallback;
	state.controller.applying = {};
	state.controller.requested.transitionEpoch--;
	state.controller.applying = fallback;
	state.CaptureRecoverySnapshot(Origin::RecoveryRelatch, &failedBoot, false, fallback.transitionEpoch);
	Require(!state.pendingVRRenderScaleRecoverySnapshot.valid,
		"A stale requested profile must not hide the applying native recovery");
}

void CheckRecoveryOwnership()
{
	const Profile native{ true, 12, 23, Method::kTAA, false, false, Origin::RecoveryRelatch };
	for (const auto method : { Method::kDLSS, Method::kFSR }) {
		Upscaling state;
		state.perfMode.boot = { true, true, method };
		state.controller.targetEpoch = 23;
		for (unsigned condition = 0; condition != 9; ++condition) {
			state.controller.requested = native;
			uint64_t relatchEpoch = 23;
			switch (condition) {
			case 0:
				state.controller.requested.valid = false;
				break;
			case 1:
				state.controller.requested.requestID = 0;
				break;
			case 2:
				state.controller.requested.transitionEpoch = 0;
				break;
			case 3:
				state.controller.requested.transitionEpoch = 22;
				break;
			case 4:
				relatchEpoch = 24;
				break;
			case 5:
				state.controller.requested.origin = Origin::CSMenu;
				break;
			case 6:
				state.controller.requested.renderScaleModeEnabled = true;
				break;
			case 7:
				state.controller.requested.perfModeEnabled = true;
				break;
			case 8:
				state.controller.requested.method = method;
				break;
			}
			state.CaptureRecoverySnapshot(Origin::RecoveryRelatch, nullptr, false, relatchEpoch);
			Require(state.pendingVRRenderScaleRecoverySnapshot.valid && state.pendingVRRenderScaleRecoverySnapshot.method == method,
				"Stale, synthetic, menu or non-native profiles must not suppress ordinary recovery");
		}
		state.controller.requested = native;
		state.controller.applying = native;
		state.controller.requested.origin = Origin::CSMenu;
		state.CaptureRecoverySnapshot(Origin::RecoveryRelatch, nullptr, false, 23);
		Require(state.pendingVRRenderScaleRecoverySnapshot.active,
			"The current requested owner must take precedence over an older applying fallback");
	}
}

void CheckOrdinaryRecovery()
{
	Upscaling state;
	const Boot boot{ true, true, Method::kDLSS };
	state.perfMode.boot = boot;
	for (const auto origin : { Origin::RecoveryRelatch, Origin::PostLoadSync }) {
		state.CaptureRecoverySnapshot(origin, &boot, false, 23);
		Require(ShouldPreserveActiveVRRenderScaleContractForRecovery(state, origin, state.pendingVRRenderScaleRecoverySnapshot),
			"Ordinary recovery must preserve its captured active boot");
	}
	state.CaptureRecoverySnapshot(Origin::RecoveryRelatch, nullptr, false, 23);
	Require(state.pendingVRRenderScaleRecoverySnapshot.active, "Recovery without an explicit snapshot must capture the active boot");
	state.CaptureRecoverySnapshot(Origin::CSMenu, &boot, false, 23);
	Require(!state.pendingVRRenderScaleRecoverySnapshot.valid, "Menu changes must not capture the old boot");
	state.CaptureRecoverySnapshot(Origin::PostLoadSync, nullptr, false, 23);
	Require(!state.pendingVRRenderScaleRecoverySnapshot.valid, "Post-load capture requires an explicit recovery snapshot");
	for (const auto invalid : { Boot{}, Boot{ true, false, Method::kDLSS } }) {
		state.perfMode.boot = invalid;
		state.CaptureRecoverySnapshot(Origin::RecoveryRelatch, &invalid, false, 23);
		Require(!state.pendingVRRenderScaleRecoverySnapshot.valid,
			"Invalid or inactive recovery snapshots must not become active contracts");
	}
	state.perfMode.boot = boot;
	state.CaptureRecoverySnapshot(Origin::RecoveryRelatch, &boot, true, 23);
	Require(!state.pendingVRRenderScaleRecoverySnapshot.valid && state.pendingPerfModeRenderTargetRecreateForcePhysical.load(),
		"Provider-neutral recovery must discard the vendor snapshot and force physical recovery");
}

int main()
{
	try {
		for (const auto failed : { Method::kDLSS, Method::kFSR })
			for (const auto native : { Method::kTAA, Method::kNONE })
				CheckNativeFallback(failed, native);
		CheckRecoveryOwnership();
		CheckOrdinaryRecovery();
		std::cout << "VR recovery producer, queue and consumer tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
