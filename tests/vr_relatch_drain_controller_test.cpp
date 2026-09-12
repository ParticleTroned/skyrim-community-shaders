#include "Features/Upscaling/VRRelatchDrainPolicy.h"
#include "Features/Upscaling/VRRelatchReleasePolicy.h"
#include "Features/Upscaling/VRRenderScaleRetryTelemetry.h"
#include "Features/Upscaling/VRVendorRelatchPolicy.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

// Production controller and proof predicates run with scripted provider polls;
// graphics creation and the downstream full relatch are observable dependencies.
struct Upscaling;
namespace globals
{
	struct State
	{
		uint32_t frameCount = 100;
	} testState;
	State* state = &testState;
	namespace game
	{
		bool isVR = true;
	}
	namespace d3d
	{
		int originalDevice = 0;
		int otherDevice = 0;
		int originalContext = 0;
		int* device = &originalDevice;
		int* context = &originalContext;
	}
	namespace features
	{
		extern Upscaling upscaling;
	}
}

template <class Callback>
struct ScopeExit
{
	Callback callback;
	~ScopeExit() { callback(); }
};

struct Identity
{
	int* value = nullptr;
	int* get() const { return value; }
	void copy_from(int* a_value) { value = a_value; }
	explicit operator bool() const { return value != nullptr; }
	Identity& operator=(std::nullptr_t)
	{
		value = nullptr;
		return *this;
	}
};

struct FenceDependency
{
	uint32_t resets = 0;
	void Reset() { ++resets; }
};

using ID3D11Device = int;
using ID3D11DeviceContext = int;
using ID3D12CommandQueue = int;
using ID3D12Fence = int;
namespace winrt
{
	template <class T>
	using com_ptr = Identity;
}

struct FidelityFX
{
	enum class LifecycleResult
	{
		Ready,
		Pending,
		Failed,
		DeviceLost
	};
	LifecycleResult pollResult = LifecycleResult::Pending;
	uint32_t polls = 0;
	VRRelatchDrainPolicy::Proof fsrRelatchDrainProof;
	FenceDependency fsrRelatchDrainHostFence, fsrRelatchDrainInteropFence;
	Identity fsrRelatchDrainDevice, fsrRelatchDrainRuntimeFence, fsrRelatchDrainRuntimeQueue, runtimeD3D12Fence;
	uint64_t fsrRelatchDrainRuntimeFenceValue = 0;
	VRRenderScaleRetryTelemetry::DrainFenceObservation fsrRelatchDrainRuntimeObservation{};
	mutable uint32_t telemetryCaptures = 0;
	void CaptureFSRRelatchDrainTelemetry(VRRenderScaleRetryTelemetry::Event&) const noexcept { ++telemetryCaptures; }
	uint32_t fsrContextCount = 0;
	std::array<int, 2> fsrContext{};
	std::array<bool, 2> fsrContextValid{}, fsrContextIndeterminate{}, runtimeUpscalerContextIndeterminate{};
	int* fsrScratchBuffer = nullptr;
	bool fsrHostStateQuarantined = false;
	bool runtimeUpscalerSessionQuarantined = false;
	bool runtimeUpscalerFailureLatched = false;
	bool ownershipDetached = false;
	bool IsRuntimeUpscalerOwnershipDetached() const { return ownershipDetached; }
	uint64_t GetFSRRelatchDrainRevision() const noexcept { return fsrRelatchDrainProof.ProviderRevision(); }
	uint64_t GetFSRRelatchDrainTicket(uint64_t a_epoch) const noexcept { return fsrRelatchDrainProof.TicketSerial(a_epoch); }
	Identity GetFSRRelatchRuntimeFence() const noexcept { return runtimeD3D12Fence; }
	bool IsHostFSRStateQuarantined() const { return fsrHostStateQuarantined; }
	bool IsRuntimeUpscalerFailureLatched() const { return runtimeUpscalerFailureLatched; }
	LifecycleResult PollFSRRelatchDrain(uint64_t a_epoch)
	{
		++polls;
		(void)fsrRelatchDrainProof.Begin(a_epoch);
		if (pollResult == LifecycleResult::Ready)
			fsrRelatchDrainProof.MarkReady(a_epoch);
		fsrRelatchDrainDevice.value = globals::d3d::device;
		return pollResult;
	}
	void CancelFSRRelatchDrain() noexcept;
	void InvalidateFSRRelatchDrain() noexcept;
	bool IsFSRRelatchDrainReady(uint64_t) const noexcept;
};

struct Streamline
{
	enum class DLSSResourceTeardownResult
	{
		Ready,
		Pending,
		Failed
	};
	DLSSResourceTeardownResult pollResult = DLSSResourceTeardownResult::Ready;
	uint32_t polls = 0;
	mutable uint32_t telemetryCaptures = 0;
	void CaptureDLSSRelatchDrainTelemetry(VRRenderScaleRetryTelemetry::Event&) const noexcept { ++telemetryCaptures; }
	VRRelatchDrainPolicy::Proof dlssRelatchDrainProof;
	FenceDependency dlssRelatchDrainFence;
	Identity dlssRelatchDrainDevice;
	int* boundDeviceIdentity = &globals::d3d::originalDevice;
	bool initialized = true, featureDLSS = true, slDLSSSetOptions = true, slFreeResources = true;
	DLSSResourceTeardownResult PollDLSSRelatchDrain(uint64_t a_epoch)
	{
		++polls;
		(void)dlssRelatchDrainProof.Begin(a_epoch);
		if (pollResult == DLSSResourceTeardownResult::Ready)
			dlssRelatchDrainProof.MarkReady(a_epoch);
		dlssRelatchDrainDevice.value = globals::d3d::device;
		return pollResult;
	}
	void CancelDLSSRelatchDrain() noexcept;
	void InvalidateDLSSRelatchDrain() noexcept;
	bool IsDLSSRelatchDrainReady(uint64_t) const noexcept;
};

enum class VRUpscalingTransitionOrigin
{
	Settings,
	Recovery
};
enum class VRRenderScaleRetryKind
{
	Retirement
};
struct VRRenderScaleProfileSnapshot
{
	bool valid = true;
	uint64_t requestID = 4, transitionEpoch = 7;
	VRUpscalingTransitionOrigin origin = VRUpscalingTransitionOrigin::Settings;
};
VRUpscalingTransitionOrigin LoadVRUpscalingTransitionOrigin(const std::atomic<VRUpscalingTransitionOrigin>& a_origin)
{
	return a_origin.load();
}
bool IsExplicitSettingsVRRenderScaleTransitionOrigin(VRUpscalingTransitionOrigin a_origin)
{
	return a_origin == VRUpscalingTransitionOrigin::Settings;
}

#include "vr_relatch_drain_helpers_under_test.h"

struct Upscaling
{
	enum class VRVendorResourceResetResult
	{
		Ready,
		Pending,
		Failed
	};
	FidelityFX fidelityFX;
	Streamline streamline;
	struct
	{
		Identity commandQueue;
	} dx12SwapChain;
	struct Snapshot
	{
		uint32_t generation = 10;
	};
	struct
	{
		Snapshot snapshot;
		Snapshot GetBootSnapshot() const { return snapshot; }
	} perfMode;
	struct Transition
	{
		uint64_t targetEpoch = 7;
		VRRenderScaleProfileSnapshot requested, applying;
		struct
		{
			struct Metrics
			{
				bool valid = true, superseded = false;
				uint64_t transitionEpoch = 7, requestID = 4;
				uint32_t retries = 1, readinessDeferrals = 0, backendDeferrals = 1, pressureDeferrals = 0, retirementDeferrals = 0, failures = 0;
			} current;
		} metrics;
	} transition;
	using VRRenderScaleTransitionMetrics = decltype(transition.metrics.current);
	Transition GetVRRenderScaleTransitionSnapshot() const { return transition; }
	mutable std::recursive_mutex perfModeRenderTargetRecreateQueueMutex;
	std::atomic<bool> pendingPerfModeRenderTargetRecreate{ true };
	std::atomic<uint64_t> pendingPerfModeRenderTargetRecreateRecoveryEpoch{ 0 };
	std::atomic<bool> pendingPerfModeRenderTargetRecreateForcePhysical{ false };
	std::atomic<bool> postLoadRuntimeResetPending{ false };
	std::atomic<VRUpscalingTransitionOrigin> pendingPerfModeRenderTargetRecreateOrigin{ VRUpscalingTransitionOrigin::Settings };
	std::atomic<uint64_t> pendingPerfModeRenderTargetRecreateEpoch{ 7 };
	std::atomic<uint32_t> pendingVRRenderScaleContractGeneration{ 11 };
	std::atomic<uint32_t> pendingPerfModeRenderTargetRecreateFrame{ 100 };
	std::atomic<uint32_t> pendingPerfModeRenderTargetRecreateDelayFrames{ kVRUpscalingTransitionApplyDelayFrames };
#include "vr_relatch_drain_state_under_test.h"
	VRRenderScaleRelatchDrainState vrRenderScaleRelatchDrain;
	std::atomic<uint64_t> vrRenderScaleRelatchDrainEpoch{ 0 };
	uint64_t vrRenderScaleRelatchDrainDisabledEpoch = 0;
	uint64_t vrRenderScaleOwnedReleaseRejectedEpoch = 0;
	uint32_t vrRenderScaleRelatchBoundaryFrame = 0;
	bool deviceLost = false, deviceRemoved = false;
	uint32_t fsrFailureHandlers = 0, deviceRemovalProbes = 0, applyCalls = 0;
	uint64_t applyCommitEpoch = 0;
	bool applyBoundaryActive = false;
	bool throwDuringApply = false;
	uint32_t cleanupCalls = 0, completedCleanups = 0, retirementRetries = 0;
	bool cleanupReady = true;
	std::vector<VRRenderScaleRetryTelemetry::Event> events;
	bool IsSubmitStageDeviceLost() const { return deviceLost; }
	void HandleFSRLifecycleDeviceLoss(FidelityFX::LifecycleResult a_result, const char*)
	{
		++fsrFailureHandlers;
		deviceLost = a_result == FidelityFX::LifecycleResult::DeviceLost;
	}
	bool MarkSubmitStageDeviceLostIfDeviceRemoved(const char*)
	{
		++deviceRemovalProbes;
		deviceLost = deviceRemoved;
		return deviceLost;
	}
	void RecordVRRenderScaleRetryEvent(VRRenderScaleRetryTelemetry::Event a_event) { events.push_back(a_event); }
	void RecordVRRenderScaleRelatchDrainEvent(VRRenderScaleRetryTelemetry::EventType, const char*);
	bool RequiresVRRenderScaleRelatchFrameBoundary() const;
	void ClearVRRenderScaleRelatchDrain();
	VRVendorResourceResetResult PollVRRenderScaleRelatchDrain();
	void ServiceVRRenderScaleRelatchAtFrameBoundary();
	static VRRelatchReleasePolicy::RetryHistory GetOwnedReleaseRetryHistory(const VRRenderScaleTransitionMetrics&);
	bool IsVRRenderScaleOwnedDrainConsumable() const;
	bool ApplyPendingPerfModeRenderTargetRecreate(const char*)
	{
		++applyCalls;
		applyCommitEpoch = g_vrRelatchDrainCommitEpoch;
		applyBoundaryActive = g_vrRelatchFrameBoundaryActive;
		if (throwDuringApply)
			throw std::runtime_error("injected downstream relatch failure");
		return false;
	}
	bool ApplyVRRenderScaleMemoryReliefTransitionCleanup(const char*, bool)
	{
		++cleanupCalls;
		if (cleanupReady)
			++completedCleanups;
		return cleanupReady;
	}
	bool RunCleanupPhase()
	{
		const auto relatchEpoch = pendingPerfModeRenderTargetRecreateEpoch.load();
		const bool memoryReliefActiveForRelatch = true;
		const bool epochOwnedNativeRestore = false, lowPeakNativeRestoreRelatch = false, previousVendorWasFSR = true;
		struct
		{
			bool preserveCompatibleFSRIntermediates = false;
		} relatchPlan;
		const auto requeueRelatch = [&](uint32_t a_delay, bool, VRRenderScaleRetryKind a_kind) {
			if (a_delay != kVRUpscalingTransitionApplyDelayFrames || a_kind != VRRenderScaleRetryKind::Retirement)
				throw std::runtime_error("cleanup changed conservative retry classification");
			++retirementRetries;
		};
#include "vr_relatch_drain_cleanup_under_test.h"
		return true;
	}
};

namespace globals::features
{
	Upscaling upscaling;
}

#include "vr_relatch_drain_consumable_under_test.h"
#include "vr_relatch_drain_controller_under_test.h"
#include "vr_relatch_drain_providers_under_test.h"

namespace
{
	using FSR = FidelityFX::LifecycleResult;
	using DLSS = Streamline::DLSSResourceTeardownResult;
	using EventType = VRRenderScaleRetryTelemetry::EventType;

	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	Upscaling& Reset()
	{
		auto& controller = globals::features::upscaling;
		std::destroy_at(&controller);
		std::construct_at(&controller);
		globals::state = &globals::testState;
		globals::state->frameCount = 100;
		globals::game::isVR = true;
		globals::d3d::device = &globals::d3d::originalDevice;
		globals::d3d::context = &globals::d3d::originalContext;
		g_vrRelatchFrameBoundaryActive = false;
		g_vrRelatchDrainCommitEpoch = 0;
		controller.vrRenderScaleRelatchDrain = { .epoch = 7, .requestID = 4, .sourceGeneration = 10, .targetGeneration = 11, .fsrRevision = controller.fidelityFX.fsrRelatchDrainProof.ProviderRevision(), .dlssRevision = controller.streamline.dlssRelatchDrainProof.ProviderRevision(), .needsFSR = true, .needsDLSS = true, .waitingForProviderDrain = true, .retryQueuedFrame = 100, .beginFrame = 100 };
		controller.vrRenderScaleRelatchDrainEpoch = 7;
		return controller;
	}

	bool HasReason(const Upscaling& a_controller, std::string_view a_reason)
	{
		return std::ranges::any_of(a_controller.events, [&](const auto& a_event) { return a_event.reason == a_reason; });
	}

	void RequiresExactOrdinaryOwner()
	{
		for (uint32_t scenario = 0; scenario < 9; ++scenario) {
			auto& controller = Reset();
			if (scenario == 0)
				globals::game::isVR = false;
			if (scenario == 1)
				controller.pendingPerfModeRenderTargetRecreate = false;
			if (scenario == 2)
				controller.pendingPerfModeRenderTargetRecreateRecoveryEpoch = 8;
			if (scenario == 3)
				controller.pendingPerfModeRenderTargetRecreateForcePhysical = true;
			if (scenario == 4)
				controller.postLoadRuntimeResetPending = true;
			if (scenario == 5)
				controller.deviceLost = true;
			if (scenario == 6)
				controller.pendingPerfModeRenderTargetRecreateOrigin = VRUpscalingTransitionOrigin::Recovery;
			if (scenario == 7)
				controller.transition.targetEpoch = 9;
			if (scenario == 8)
				controller.transition.requested.valid = controller.transition.applying.valid = false;
			Require(!controller.RequiresVRRenderScaleRelatchFrameBoundary(), "Unowned or recovery relatch gained ordinary boundary admission");
			controller.ServiceVRRenderScaleRelatchAtFrameBoundary();
			Require(controller.applyCalls == 0 && controller.fidelityFX.polls == 0, "Rejected boundary polled or applied resources");
		}
	}

	void PendingBudgetPreservesConservativeRetry()
	{
		auto& controller = Reset();
		for (uint32_t age = 1; age < kVRUpscalingTransitionApplyDelayFrames; ++age) {
			globals::state->frameCount = 100 + age;
			controller.ServiceVRRenderScaleRelatchAtFrameBoundary();
			Require(controller.applyCalls == 0 && controller.cleanupCalls == 0 && controller.vrRenderScaleRelatchDrain.epoch == 7,
				"Pending observation entered full apply or cleared its owned proof before the deadline");
			controller.ServiceVRRenderScaleRelatchAtFrameBoundary();
			Require(controller.fidelityFX.polls == age, "A frame boundary polled twice in one frame");
			Require(!g_vrRelatchFrameBoundaryActive && g_vrRelatchDrainCommitEpoch == 0, "Pending observation leaked boundary scope");
		}
		globals::state->frameCount = 100 + kVRUpscalingTransitionApplyDelayFrames;
		controller.ServiceVRRenderScaleRelatchAtFrameBoundary();
		Require(controller.applyCalls == 1 && controller.applyCommitEpoch == 0 && controller.vrRenderScaleRelatchDrainDisabledEpoch == 7 &&
					controller.vrRenderScaleRelatchDrain.epoch == 0 && controller.vrRenderScaleRelatchDrainEpoch == 0 &&
					controller.pendingPerfModeRenderTargetRecreateFrame == 100 && controller.pendingPerfModeRenderTargetRecreateDelayFrames == 6 &&
					HasReason(controller, "relatch_drain_budget_expired"),
			"Deadline did not revoke the proof and preserve the original conservative retry");
	}

	void ReadyRequiresMatchingQueuedRetry()
	{
		for (uint32_t scenario = 0; scenario < 4; ++scenario) {
			auto& controller = Reset();
			controller.fidelityFX.pollResult = FSR::Ready;
			globals::state->frameCount = scenario == 1 ? 106u : 101u;
			if (scenario == 2)
				controller.pendingPerfModeRenderTargetRecreateFrame = 101;
			if (scenario == 3)
				controller.pendingPerfModeRenderTargetRecreateDelayFrames = 12;
			controller.ServiceVRRenderScaleRelatchAtFrameBoundary();
			Require(controller.applyCalls == 1 && controller.applyBoundaryActive && controller.applyCommitEpoch == (scenario < 2 ? 7u : 0u),
				"Ready proof bypassed a different queued retry, or failed to admit its own retry");
			Require(!g_vrRelatchFrameBoundaryActive && g_vrRelatchDrainCommitEpoch == 0, "Ready admission leaked boundary scope");
			Require(!controller.vrRenderScaleRelatchDrain.waitingForProviderDrain, "Ready observation did not consume the queued bypass");
			++globals::state->frameCount;
			controller.ServiceVRRenderScaleRelatchAtFrameBoundary();
			Require(controller.applyCalls == 2 && controller.applyCommitEpoch == 0 && controller.fidelityFX.polls == 1 && controller.streamline.polls == 1,
				"A subsequent callback repolled the ready operation or replayed its consumed bypass");
		}
	}

	void FailuresStayConservativeAndDeviceLossStopsApply()
	{
		for (uint32_t scenario = 0; scenario < 4; ++scenario) {
			auto& controller = Reset();
			controller.fidelityFX.pollResult = scenario < 2 ? (scenario == 0 ? FSR::Failed : FSR::DeviceLost) : FSR::Ready;
			if (scenario >= 2)
				controller.streamline.pollResult = DLSS::Failed;
			controller.deviceRemoved = scenario == 3;
			globals::state->frameCount = 101;
			controller.ServiceVRRenderScaleRelatchAtFrameBoundary();
			const bool lost = scenario == 1 || scenario == 3;
			Require(controller.deviceLost == lost && controller.applyCalls == (lost ? 0u : 1u) && controller.applyCommitEpoch == 0 &&
						controller.vrRenderScaleRelatchDrainDisabledEpoch == 7 && controller.vrRenderScaleRelatchDrain.epoch == 0 &&
						HasReason(controller, "relatch_drain_failed_conservative_retry"),
				"Failed provider drain entered owned commit or device loss continued into full apply");
		}
	}

	void ChangedOwnerAndRecoveryCancelBothProviders()
	{
		for (uint32_t scenario = 0; scenario < 5; ++scenario) {
			auto& controller = Reset();
			if (scenario == 0)
				controller.vrRenderScaleRelatchDrain.epoch = 6;
			if (scenario == 1)
				controller.perfMode.snapshot.generation = 12;
			if (scenario == 2)
				controller.pendingVRRenderScaleContractGeneration = 12;
			if (scenario == 3)
				controller.pendingPerfModeRenderTargetRecreateRecoveryEpoch = 9;
			if (scenario == 4)
				controller.postLoadRuntimeResetPending = true;
			controller.ServiceVRRenderScaleRelatchAtFrameBoundary();
			Require(controller.vrRenderScaleRelatchDrain.epoch == 0 && controller.fidelityFX.fsrRelatchDrainHostFence.resets > 0 &&
						controller.streamline.dlssRelatchDrainFence.resets > 0 && controller.fidelityFX.polls == 0 &&
						HasReason(controller, "relatch_drain_owner_changed"),
				"Changed ownership reused an old provider fence");
		}
	}

	void ReadyProofRejectsDeviceAndProviderChanges()
	{
		for (uint32_t scenario = 0; scenario < 5; ++scenario) {
			auto& controller = Reset();
			auto& fsr = controller.fidelityFX;
			auto& dlss = controller.streamline;
			(void)fsr.fsrRelatchDrainProof.Begin(7);
			(void)dlss.dlssRelatchDrainProof.Begin(7);
			fsr.fsrRelatchDrainProof.MarkReady(7);
			dlss.dlssRelatchDrainProof.MarkReady(7);
			fsr.fsrRelatchDrainDevice.value = dlss.dlssRelatchDrainDevice.value = globals::d3d::device;
			Require(fsr.IsFSRRelatchDrainReady(7) && dlss.IsDLSSRelatchDrainReady(7), "Healthy held proofs were not ready");
			if (scenario == 0)
				globals::d3d::device = &globals::d3d::otherDevice;
			if (scenario == 1)
				dlss.boundDeviceIdentity = &globals::d3d::otherDevice;
			if (scenario == 2) {
				fsr.InvalidateFSRRelatchDrain();
				dlss.InvalidateDLSSRelatchDrain();
			}
			if (scenario == 3) {
				fsr.fsrRelatchDrainRuntimeFence.value = &globals::d3d::originalDevice;
			}
			if (scenario == 4) {
				fsr.fsrRelatchDrainRuntimeFence.value = fsr.runtimeD3D12Fence.value = &globals::d3d::originalDevice;
				fsr.fsrRelatchDrainRuntimeQueue.value = &globals::d3d::originalDevice;
			}
			Require((scenario == 1 || !fsr.IsFSRRelatchDrainReady(7)) && (scenario >= 3 || !dlss.IsDLSSRelatchDrainReady(7)),
				"Changed device, runtime fence/queue or provider revision retained a consumable proof");
			Require(!fsr.IsFSRRelatchDrainReady(8) && !dlss.IsDLSSRelatchDrainReady(8), "A new epoch consumed the previous epoch's proof");
		}
	}

	void CleanupBackpressureDoesNotRepeatCompletedCleanup()
	{
		auto& controller = Reset();
		controller.cleanupReady = false;
		Require(!controller.RunCleanupPhase() && controller.cleanupCalls == 1 && controller.retirementRetries == 1 &&
					!controller.vrRenderScaleRelatchDrain.sharedCleanupCompleted,
			"Cleanup Pending lost retirement backpressure");
		controller.cleanupReady = true;
		Require(controller.RunCleanupPhase() && controller.RunCleanupPhase() && controller.cleanupCalls == 2 &&
					controller.completedCleanups == 1 && controller.vrRenderScaleRelatchDrain.sharedCleanupCompleted,
			"Repeated commit repeated completed shared cleanup");
		Require(std::ranges::count_if(controller.events, [](const auto& a_event) { return a_event.type == EventType::RelatchSharedCleanup; }) == 1,
			"Shared cleanup completion was recorded more than once");
		controller.ClearVRRenderScaleRelatchDrain();
		controller.vrRenderScaleRelatchDrain.epoch = 8;
		controller.pendingPerfModeRenderTargetRecreateEpoch = 8;
		Require(controller.RunCleanupPhase() && controller.completedCleanups == 2, "A fresh epoch inherited the previous completed cleanup");
	}

	void CancelledTicketsCannotAliasReissuedProofs()
	{
		auto& controller = Reset();
		auto& fsr = controller.fidelityFX;
		auto& dlss = controller.streamline;
		(void)fsr.fsrRelatchDrainProof.Begin(7);
		(void)dlss.dlssRelatchDrainProof.Begin(7);
		fsr.fsrRelatchDrainProof.MarkReady(7);
		dlss.dlssRelatchDrainProof.MarkReady(7);
		const auto fsrRevision = fsr.fsrRelatchDrainProof.ProviderRevision();
		const auto dlssRevision = dlss.dlssRelatchDrainProof.ProviderRevision();
		const auto fsrTicket = fsr.fsrRelatchDrainProof.TicketSerial(7);
		const auto dlssTicket = dlss.dlssRelatchDrainProof.TicketSerial(7);
		Require(fsrTicket != 0 && dlssTicket != 0, "A started drain did not publish an exact ticket");
		fsr.CancelFSRRelatchDrain();
		dlss.CancelDLSSRelatchDrain();
		Require(fsr.fsrRelatchDrainProof.TicketSerial(7) == 0 && dlss.dlssRelatchDrainProof.TicketSerial(7) == 0 &&
					!fsr.IsFSRRelatchDrainReady(7) && !dlss.IsDLSSRelatchDrainReady(7),
			"Provider cancellation retained the old ticket or readiness");
		Require(fsr.fsrRelatchDrainProof.Begin(7) && dlss.dlssRelatchDrainProof.Begin(7),
			"A cancelled same-epoch drain did not create fresh tickets");
		Require(fsr.fsrRelatchDrainProof.ProviderRevision() == fsrRevision && dlss.dlssRelatchDrainProof.ProviderRevision() == dlssRevision &&
					fsr.fsrRelatchDrainProof.TicketSerial(7) != fsrTicket && dlss.dlssRelatchDrainProof.TicketSerial(7) != dlssTicket,
			"Same-epoch reissue aliased a cancelled ticket or invented provider use");
		fsr.InvalidateFSRRelatchDrain();
		dlss.InvalidateDLSSRelatchDrain();
		Require(fsr.fsrRelatchDrainProof.ProviderRevision() != fsrRevision && dlss.dlssRelatchDrainProof.ProviderRevision() != dlssRevision &&
					fsr.fsrRelatchDrainProof.TicketSerial(7) == 0 && dlss.dlssRelatchDrainProof.TicketSerial(7) == 0,
			"Provider use failed to revoke both the revision and ticket identity");
	}

	void ConsumptionRequiresExactLiveProviderTicketsAndOwner()
	{
		for (uint32_t blocker = 0; blocker < 19; ++blocker) {
			auto& controller = Reset();
			auto& drain = controller.vrRenderScaleRelatchDrain;
			drain.priorCleanupDebt = false;
			drain.device.copy_from(globals::d3d::device);
			drain.contextOwner.copy_from(globals::d3d::context);
			drain.beforeWait = { .valid = true, .epoch = 7, .requestID = 4 };
			drain.afterOwnedWait = { .valid = true, .epoch = 7, .requestID = 4, .retries = 1, .backendDeferrals = 1 };
			controller.fidelityFX.pollResult = FSR::Ready;
			Require(controller.PollVRRenderScaleRelatchDrain() == Upscaling::VRVendorResourceResetResult::Ready &&
						controller.IsVRRenderScaleOwnedDrainConsumable(),
				"The exact production poll did not create consumable owner-bound tickets");
			switch (blocker) {
			case 0:
				controller.fidelityFX.CancelFSRRelatchDrain();
				(void)controller.fidelityFX.fsrRelatchDrainProof.Begin(7);
				controller.fidelityFX.fsrRelatchDrainProof.MarkReady(7);
				break;
			case 1:
				controller.streamline.InvalidateDLSSRelatchDrain();
				(void)controller.streamline.dlssRelatchDrainProof.Begin(7);
				controller.streamline.dlssRelatchDrainProof.MarkReady(7);
				break;
			case 2:
				++controller.perfMode.snapshot.generation;
				break;
			case 3:
				++controller.pendingVRRenderScaleContractGeneration;
				break;
			case 4:
				++controller.transition.targetEpoch;
				break;
			case 5:
				++controller.transition.metrics.current.requestID;
				break;
			case 6:
				controller.dx12SwapChain.commandQueue.value = &globals::d3d::otherDevice;
				break;
			case 7:
				globals::d3d::device = &globals::d3d::otherDevice;
				break;
			case 8:
				globals::d3d::context = &globals::d3d::otherDevice;
				break;
			case 9:
				controller.fidelityFX.runtimeD3D12Fence.value = &globals::d3d::otherDevice;
				break;
			case 10:
				++controller.transition.metrics.current.retries;
				++controller.transition.metrics.current.backendDeferrals;
				break;
			case 11:
				controller.transition.metrics.current.retries = controller.transition.metrics.current.backendDeferrals = UINT32_MAX;
				break;
			case 12:
				drain.priorCleanupDebt = true;
				break;
			case 13:
				controller.vrRenderScaleOwnedReleaseRejectedEpoch = 7;
				break;
			case 14:
				drain.needsFSR = drain.needsDLSS = false;
				break;
			case 15:
				controller.transition.metrics.current.superseded = true;
				break;
			case 16:
				drain.fsrRevision = UINT64_MAX;
				break;
			case 17:
				drain.dlssTicket = UINT64_MAX;
				break;
			case 18:
				controller.transition.metrics.current.failures = 1;
				break;
			}
			Require(!controller.IsVRRenderScaleOwnedDrainConsumable(),
				"Changed live ownership, ticket, provider identity, or retry history retained consumption authority");
		}
	}

	void DownstreamFailureRestoresBoundaryScope()
	{
		auto& controller = Reset();
		controller.fidelityFX.pollResult = FSR::Ready;
		controller.throwDuringApply = true;
		bool caught = false;
		try {
			controller.ServiceVRRenderScaleRelatchAtFrameBoundary();
		} catch (const std::runtime_error&) {
			caught = true;
		}
		Require(caught && controller.applyCalls == 1 && controller.applyBoundaryActive &&
					!g_vrRelatchFrameBoundaryActive && g_vrRelatchDrainCommitEpoch == 0,
			"Downstream failure leaked native boundary admission");
	}
}

int main()
{
	RequiresExactOrdinaryOwner();
	PendingBudgetPreservesConservativeRetry();
	ReadyRequiresMatchingQueuedRetry();
	FailuresStayConservativeAndDeviceLossStopsApply();
	ChangedOwnerAndRecoveryCancelBothProviders();
	ReadyProofRejectsDeviceAndProviderChanges();
	CleanupBackpressureDoesNotRepeatCompletedCleanup();
	CancelledTicketsCannotAliasReissuedProofs();
	ConsumptionRequiresExactLiveProviderTicketsAndOwner();
	DownstreamFailureRestoresBoundaryScope();
}
