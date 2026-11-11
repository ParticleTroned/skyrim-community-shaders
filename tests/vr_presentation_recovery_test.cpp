#include "Features/Upscaling/VRVendorRelatchPolicy.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <latch>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

using Stage = VRVendorRelatchPolicy::PresentationRecoveryStage;
using Phase = VRVendorRelatchPolicy::PostMutationProgressPhase;
constexpr int FALSE = 0;
uint64_t testTick = 70000;
unsigned clockReads = 0;
uint64_t GetTickCount64()
{
	++clockReads;
	return testTick;
}
int IsDebuggerPresent() { return FALSE; }
const char* BoolText(bool value) { return value ? "yes" : "no"; }
const char* GetVRRenderScalePostMutationProgressPhaseName(Phase) { return "presentation"; }
uint64_t GetVRRenderScalePostMutationTerminalDeadlineMilliseconds(Phase) { return 60000; }
namespace logger
{
	template <class... T>
	void warn(const char*, T...)
	{}
	template <class... T>
	void error(const char*, T...)
	{}
}
namespace SKSE::stl
{
	template <class F>
	struct scope_exit
	{
		F fn;
		~scope_exit() { fn(); }
	};
}
struct State
{
	unsigned frameCount = 1000, lastWorldRenderFrame = 1000, lastCompletedWorldRenderFrame = 1000;
	bool pendingPostLoadRuntimeReset = false, engineBusy = false, safeMode = false;
	std::atomic<unsigned> saveLoadSafeModeEndFrame{ 0 };
	static constexpr unsigned kSaveLoadSafeModeGraceFrames = 120;
	bool IsEngineSaveLoadActivityActive() const { return engineBusy; }
	bool IsSaveLoadSafeModeActive() const { return safeMode; }
	void ExtendSaveLoadSafeMode(unsigned frame, unsigned grace) { saveLoadSafeModeEndFrame = frame + grace; }
};
namespace globals
{
	State storage;
	State* state = &storage;
}
#include "vr_presentation_mutexes_under_test.h"
std::atomic<bool> g_vrLoadingTransitionSerialOpen{ false };
bool loadMenu = false, knownMenu = false, worldReady = true, exactContract = true, vendorReset = false;
bool IsLoadingMenuContextActive() { return loadMenu; }
bool IsKnownGameMenuContextActive() { return knownMenu; }
bool HasCompletedVRWorldFrameAfterLatestLoad(const State*) { return worldReady; }
bool HasPendingVRVendorRuntimeReset(const auto&, unsigned) { return vendorReset; }
bool IsVRRenderScalePhysicalVendorContractExact(const auto&, const auto&, unsigned) { return exactContract; }
enum class VRRenderScaleTransitionState
{
	Idle,
	Active,
	Stabilizing,
	WaitingForSafePoint
};
enum class VRUpscalingTransitionOrigin
{
	RecoveryRelatch
};
enum class UpscaleMethod
{
	kNONE
};
bool IsVRRenderScaleRecoveryOrigin(VRUpscalingTransitionOrigin) { return true; }
VRUpscalingTransitionOrigin LoadVRUpscalingTransitionOrigin(const std::atomic<VRUpscalingTransitionOrigin>& value) { return value.load(); }
unsigned ClampQualityModeUInt(unsigned value) { return value; }
unsigned ClampDLSSPresetUInt(unsigned value) { return value; }
struct Profile
{
	bool valid = true;
	uint64_t transitionEpoch = 7;
	unsigned contractGeneration = 13, method = 3, qualityMode = 3;
};
struct Transition
{
	VRRenderScaleTransitionState state = VRRenderScaleTransitionState::Stabilizing;
	Profile applied, stable;
	uint64_t targetEpoch = 7;
};
struct Mutation
{
	uint64_t serializationEpoch = 7, epoch = 0, chainSerial = 7, startTickMs = 1, lastProgressTickMs = 1000;
	bool emergencyRecoveryRequested = true;
	Phase progressPhase = Phase::PresentationStabilizing;
};
struct Upscaling : PresentationRecoveryMutexes
{
	std::atomic<uint64_t> vrRenderScalePostMutationSerializationEpoch{ 7 }, vrRenderScaleUnresolvedPhysicalMutationEpoch{ 0 };
	std::atomic<bool> vrRenderScaleExtendedRecoveryDeadlineLogged{ false };
	std::atomic<unsigned> vrRenderScalePostMutationEmergencyAttemptFrame{ 10 };
	std::atomic<uint64_t> vrRenderScalePostMutationEmergencyAttemptGraceDeadlineTickMs{ 1000 };
	std::atomic<bool> vrRenderScaleTerminalFailureSignaled{ false }, pendingPerfModeRenderTargetRecreate{ false };
	std::atomic<bool> vrRenderScalePostMutationEmergencyAttemptConsumed{ true }, vrRenderScaleEmergencyRecoveryRequested{ true };
	std::atomic<uint64_t> pendingPostLoadRuntimeResetEpoch{ 12 }, pendingPerfModeRenderTargetRecreateRecoveryEpoch{ 12 };
	std::atomic<Phase> vrRenderScalePostMutationProgressPhase{ Phase::PresentationStabilizing };
	std::atomic<uint64_t> vrRenderScalePostMutationLastProgressTickMs{ 1000 };
	std::atomic<bool> vrRenderScaleTerminalDeadlineFailOpenLogged{ false };
	std::atomic<Stage> vrRenderScalePresentationRecoveryStage{ Stage::None };
	std::atomic<bool> vrRenderScaleNativeFallbackAttemptConsumed{ false }, vrRenderScalePresentationFallbackAdmissionActive{ false };
	std::atomic<bool> deferredVRRenderScaleRequestPending{ false };
	std::atomic<uint64_t> vrRenderScaleProviderNeutralNativeRecoveryEpoch{ 0 };
	std::atomic<bool> postLoadRuntimeResetPending{ false }, perfModeRenderTargetRecreateInProgress{ false };
	bool deferredVRRenderScaleRelatchAfterPresentation = false, deferredVRRenderScalePostLoadResetAfterPresentation = false;
	uint64_t vrRenderScalePresentationRecoveryStartTickMs = 0;
	uint64_t vrRenderScalePresentationRecoveryMenuEpoch = 0;
	uint64_t vrRenderScalePresentationRecoveryLastMenuTickMs = 0;
	bool vrRenderScalePresentationResumeAttempted = false;
	Transition controller;
	Mutation mutationState;
	bool hold = false, removed = false, removeOnProbe = false, resourcesReady = true, physicalExact = true, lifecycleBlocked = false;
	bool pendingSettings = false, nativeAdmission = true;
	unsigned vrPostLoadCompositorInFlightSubmitCount = 0, quarantinedCycles = 0;
	uint64_t vrPostLoadCompositorInFlightCycleToken = 2;
	bool probeConcurrentRetirement = false, concurrentRetirementEntered = false;
	unsigned resumes = 0, recreations = 0, nativeFallbacks = 0, snapshots = 0, diagnostics = 0;
	std::atomic<uint64_t> pendingPerfModeRenderTargetRecreateEpoch{ 0 };
	std::atomic<VRUpscalingTransitionOrigin> pendingPerfModeRenderTargetRecreateOrigin{ VRUpscalingTransitionOrigin::RecoveryRelatch };
	std::atomic<bool> pendingPerfModeRenderTargetRecreateForcePhysical{ false };
	struct Request
	{
		VRUpscalingTransitionOrigin origin = VRUpscalingTransitionOrigin::RecoveryRelatch;
		UpscaleMethod method = UpscaleMethod::kNONE;
		bool renderScaleModeEnabled = false, perfModeEnabled = false;
		uint64_t transitionEpoch = 8;
	};
	std::optional<Request> pendingVRRenderScaleRequest;
	struct Settings
	{
		unsigned qualityMode = 3, dlssPreset = 0;
	} settings;
	struct Boot
	{
		bool valid = true, active = true;
	};
	Boot pendingVRRenderScaleRecoverySnapshot;
	struct Perf
	{
		Boot GetBootSnapshot() const { return {}; }
	} perfMode;
	Mutation GetVRRenderScalePhysicalMutationSnapshot() const
	{
		auto result = mutationState;
		result.serializationEpoch = vrRenderScalePostMutationSerializationEpoch;
		result.epoch = vrRenderScaleUnresolvedPhysicalMutationEpoch;
		return result;
	}
	Transition GetVRRenderScaleTransitionSnapshot()
	{
		++snapshots;
		return controller;
	}
	bool IsVRInitialLoadPresentationProtectionActive() const { return false; }
	bool IsVRPostLoadCompositorHoldActive() const { return hold; }
	bool IsSubmitStageDeviceLost() const { return removed; }
	bool MarkSubmitStageDeviceLostIfDeviceRemoved(const char*)
	{
		if (removeOnProbe) {
			const std::scoped_lock lock(vrPostLoadCompositorHoldMutex);
			removed = true;
		}
		return removed;
	}
	bool HasPendingVRUpscalingTransition() const { return pendingSettings; }
	bool HasTruthfulStableVRVendorResources(const Profile&, bool) const { return resourcesReady; }
	bool IsVRRenderScalePhysicalContractConverged(unsigned, unsigned) const { return physicalExact; }
	bool ShouldDeferVRVendorLifecycleMutation() const { return lifecycleBlocked; }
	void ArmSubmitStageVendorResumeCooldown(unsigned) { ++resumes; }
	void ResetVRPostLoadCompositorHold();
	void ResetVRPostLoadPresentationProtectionLocked();
	void QuarantineVRPostLoadCompositorCycleLocked(uint64_t) { ++quarantinedCycles; }
	void FinishVRInitialLoadPresentationProtectionLocked(bool)
	{
		const std::scoped_lock lock(vrRenderScalePhysicalMutationMutex);
		hold = false;
		TryRetireVRRenderScalePostMutationSerializationLocked(vrRenderScalePostMutationSerializationEpoch);
	}
	void SetVRRenderScaleTransitionState(VRRenderScaleTransitionState value, const char*) { controller.state = value; }
	bool TryRetireVRRenderScalePostMutationSerialization(uint64_t epoch)
	{
		const std::scoped_lock lock(vrPostLoadCompositorHoldMutex, vrRenderScalePhysicalMutationMutex);
		return TryRetireVRRenderScalePostMutationSerializationLocked(epoch);
	}
	bool TryRetireVRRenderScalePostMutationSerializationLocked(uint64_t epoch)
	{
		if (epoch == vrRenderScalePostMutationSerializationEpoch && !hold && controller.state == VRRenderScaleTransitionState::Active) {
			vrRenderScalePostMutationSerializationEpoch = 0;
			return true;
		}
		return false;
	}
	bool IsOpenCompositeUpscalingBlocked() const { return false; }
	struct QueueResult
	{
		bool accepted;
		uint64_t transitionEpoch = 8;
		bool Accepted() const { return accepted; }
		bool Deferred() const { return false; }
	};
	QueueResult QueueVRRenderScaleRequest(UpscaleMethod, bool, unsigned, unsigned, bool, VRUpscalingTransitionOrigin, uint64_t)
	{
		++nativeFallbacks;
		if (nativeAdmission)
			pendingVRRenderScaleRequest = Request{};
		return { nativeAdmission };
	}
	bool EnsureVRRenderScaleProviderNeutralNativeRecovery(const char*, bool);
	bool EnsureVRRenderScaleProviderNeutralNativeRecoveryLocked(const char*, bool);
	uint64_t BeginVRRenderScalePostLoadRecovery() { return 12; }
	void RequestPerfModeRenderTargetRecreate(const char*, VRUpscalingTransitionOrigin, const Boot*, unsigned = 0, bool native = false)
	{
		++recreations;
		pendingPerfModeRenderTargetRecreate = true;
		if (native) {
			pendingPerfModeRenderTargetRecreateEpoch = pendingVRRenderScaleRequest->transitionEpoch;
			pendingPerfModeRenderTargetRecreateForcePhysical = true;
			pendingVRRenderScaleRecoverySnapshot.valid = false;
		}
	}
	void LogVRRenderScalePresentationStall(uint64_t, const char*, const char*)
	{
		++diagnostics;
		if (probeConcurrentRetirement) {
			std::thread retire([&] {
				if (vrPostLoadCompositorHoldMutex.try_lock()) {
					concurrentRetirementEntered = true;
					vrRenderScalePostMutationSerializationEpoch = 0;
					vrRenderScalePresentationRecoveryStage = Stage::None;
					vrPostLoadCompositorHoldMutex.unlock();
				}
			});
			retire.join();
		}
	}
	void ServiceVRRenderScalePostMutationWatchdog(const char*);
	bool ShouldDeferVRRenderScalePresentationSuccessor() const;
	bool IsVRRenderScalePostMutationAttemptConsumed(uint64_t) const;
	void DeferVRRenderScalePresentationRelatch(bool);
	bool TryResumeVRRenderScalePresentationLocked(uint64_t);
	void ServiceVRRenderScalePresentationRecovery(uint64_t, const char*);
};
#include "vr_presentation_hold_reset_under_test.h"
#include "vr_presentation_native_recovery_under_test.h"
#include "vr_presentation_owner_under_test.h"
#include "vr_presentation_recovery_under_test.h"

void Require(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << '\n';
		throw std::runtime_error(message);
	}
}
int main()
{
	for (const auto [covered, retryAdmitted] : {
			 std::pair{ false, false }, std::pair{ false, true },
			 std::pair{ true, false }, std::pair{ true, true } }) {
		Upscaling u;
		u.hold = covered;
		u.resourcesReady = false;
		if (retryAdmitted)
			u.ServiceVRRenderScalePostMutationWatchdog("gameplay retry before menu");
		knownMenu = true;
		for (unsigned minute = 0; minute < 3; ++minute) {
			testTick += 60000;
			u.ServiceVRRenderScalePostMutationWatchdog("character creation with current preview world frames");
			Require(u.nativeFallbacks == 0 && u.resumes == 0 && u.hold == covered &&
						u.vrRenderScalePresentationRecoveryStage == (retryAdmitted ? Stage::ResumePending : Stage::None),
				"An intentional menu cannot consume presentation recovery or native fallback quota");
		}
		knownMenu = false;
		u.ServiceVRRenderScalePostMutationWatchdog("menu closed");
		Require(u.nativeFallbacks == 0, "Closing a long-lived menu cannot immediately trigger fallback");
		if (!retryAdmitted) {
			testTick += kVRRenderScalePostMutationStalledDeadlineMilliseconds - 1;
			u.ServiceVRRenderScalePostMutationWatchdog("gameplay within fresh stall interval");
			Require(u.vrRenderScalePresentationRecoveryStage == Stage::None,
				"A menu must leave the full gameplay stall interval for stereo promotion");
			++testTick;
			u.ServiceVRRenderScalePostMutationWatchdog("gameplay stall reached");
			Require(u.vrRenderScalePresentationRecoveryStage == Stage::ResumePending,
				"A real gameplay stall must still admit bounded presentation recovery");
		}
		testTick += kVRRenderScalePresentationRetryMilliseconds - 1;
		u.ServiceVRRenderScalePostMutationWatchdog("gameplay within fresh retry interval");
		Require(u.nativeFallbacks == 0, "Menu time cannot exhaust the presentation retry interval");
		++testTick;
		u.ServiceVRRenderScalePostMutationWatchdog("gameplay retry exhausted");
		Require(u.nativeFallbacks == 1 && u.vrRenderScalePresentationRecoveryStage == Stage::NativeFallback,
			"A failed gameplay retry must retain the existing one-shot native fallback");
	}
	{
		Upscaling u;
		knownMenu = true;
		u.ServiceVRRenderScalePostMutationWatchdog("menu under previous owner");
		knownMenu = false;
		u.vrRenderScalePostMutationSerializationEpoch = 8;
		u.controller.applied.transitionEpoch = 8;
		u.controller.stable.transitionEpoch = 8;
		u.ServiceVRRenderScalePostMutationWatchdog("new owner with an actual gameplay stall");
		Require(u.resumes == 1, "An old menu clock cannot defer a different serialization owner");
	}
	{
		Upscaling u;
		u.hold = true;
		u.removeOnProbe = true;
		knownMenu = true;
		u.ServiceVRRenderScalePostMutationWatchdog("device removal during character creation");
		knownMenu = false;
		Require(u.vrRenderScalePresentationRecoveryStage == Stage::Quarantined &&
					u.nativeFallbacks == 0 && u.resumes == 0 && u.hold,
			"An intentional menu cannot hide typed device-loss evidence");
	}
	for (bool covered : { false, true }) {
		Upscaling u;
		u.hold = covered;
		u.ServiceVRRenderScalePostMutationWatchdog("stalled");
		Require(u.resumes == 1 && !u.hold && u.controller.state == VRRenderScaleTransitionState::Stabilizing,
			"Covered and uncovered stalls must retry stereo without inventing stability");
		for (unsigned i = 0; i < 100; ++i) u.ServiceVRRenderScalePostMutationWatchdog("repeat");
		Require(u.resumes == 1 && u.nativeFallbacks == 0, "Resume must be bounded without resource churn");
		const auto owner = u.vrRenderScalePostMutationSerializationEpoch.load();
		Require(u.ShouldDeferVRRenderScalePresentationSuccessor(), "Finishing owner must defer notifications");
		u.DeferVRRenderScalePresentationRelatch(true);
		u.DeferVRRenderScalePresentationRelatch(false);
		Require(u.deferredVRRenderScaleRequestPending && u.pendingPostLoadRuntimeResetEpoch == 12 &&
					u.vrRenderScalePostMutationSerializationEpoch == owner && !u.pendingPerfModeRenderTargetRecreate,
			"Deferral must retain the recovery tuple without queuing physical work");
		u.controller.state = VRRenderScaleTransitionState::Active;
		u.ServiceVRRenderScalePostMutationWatchdog("stereo completed");
		Require(u.vrRenderScalePostMutationSerializationEpoch == 0, "Actual completion retires ownership");
		const unsigned sampled = clockReads, snapshots = u.snapshots;
		for (unsigned i = 0; i < 1000; ++i) u.ServiceVRRenderScalePostMutationWatchdog("healthy");
		Require(clockReads == sampled && snapshots == u.snapshots && u.resumes == 1,
			"Normal gameplay must not sample clocks, resources or controller state");
	}
	for (unsigned blocker = 0; blocker < 6; ++blocker) {
		Upscaling u;
		u.hold = true;
		if (blocker == 0)
			u.resourcesReady = false;
		if (blocker == 1)
			u.physicalExact = false;
		if (blocker == 2)
			u.lifecycleBlocked = true;
		if (blocker == 3)
			u.pendingSettings = true;
		if (blocker == 4)
			vendorReset = true;
		if (blocker == 5)
			exactContract = false;
		u.ServiceVRRenderScalePostMutationWatchdog("blocked");
		Require(u.resumes == 0 && u.hold, "Unsafe resume must retain protection");
		testTick += kVRRenderScalePresentationRetryMilliseconds;
		u.ServiceVRRenderScalePostMutationWatchdog("expired");
		Require(u.nativeFallbacks == 1 && u.vrRenderScalePresentationRecoveryStage == Stage::NativeFallback,
			"Failed presentation must explicitly queue native recovery");
		Require(!u.IsVRRenderScalePostMutationAttemptConsumed(8) && u.IsVRRenderScalePostMutationAttemptConsumed(7),
			"Native quota must be tied to the exact native successor");
		u.vrRenderScaleNativeFallbackAttemptConsumed = true;
		Require(u.IsVRRenderScalePostMutationAttemptConsumed(8), "Native creator is one-shot");
		testTick += kVRRenderScalePostMutationStalledDeadlineMilliseconds;
		u.ServiceVRRenderScalePostMutationWatchdog("unsafe fallback");
		Require(u.vrRenderScalePresentationRecoveryStage == Stage::Quarantined && u.hold,
			"Unsafe native recovery retains quarantine and cover");
		for (unsigned i = 0; i < 100; ++i) u.ServiceVRRenderScalePostMutationWatchdog("quarantined");
		Require(u.nativeFallbacks == 1 && u.resumes == 0, "Quarantine must not repeatedly retry");
		vendorReset = false;
		exactContract = true;
	}
	{
		Upscaling u;
		u.removed = true;
		u.hold = true;
		u.ServiceVRRenderScalePostMutationWatchdog("removed");
		Require(u.resumes == 0 && u.nativeFallbacks == 0 && u.hold &&
					u.vrRenderScalePresentationRecoveryStage == Stage::Quarantined,
			"Device loss must remain quarantined");
	}
	{
		Upscaling u;
		g_vrLoadingTransitionSerialOpen = true;
		u.ServiceVRRenderScalePostMutationWatchdog("new load");
		Require(u.resumes == 0 && u.nativeFallbacks == 0, "An open physical load cannot resume");
		g_vrLoadingTransitionSerialOpen = false;
		u.ServiceVRRenderScalePresentationRecovery(6, "stale owner");
		Require(u.resumes == 0 && u.nativeFallbacks == 0, "Stale service cannot affect the current owner");
	}
	{
		Upscaling u;
		u.controller.state = VRRenderScaleTransitionState::Active;
		u.hold = true;
		u.ServiceVRRenderScalePostMutationWatchdog("stable contract with stalled cover");
		Require(u.resumes == 1 && u.controller.state == VRRenderScaleTransitionState::Stabilizing &&
					u.vrRenderScalePostMutationSerializationEpoch == 7,
			"Cover release must not retire a newly armed stereo proof");
	}
	{
		Upscaling u;
		u.hold = true;
		u.removeOnProbe = true;
		u.ServiceVRRenderScalePostMutationWatchdog("fresh device removal");
		Require(u.vrRenderScalePresentationRecoveryStage == Stage::Quarantined &&
					u.nativeFallbacks == 0 && u.resumes == 0 && u.hold,
			"Fresh removal evidence must prevent fallback admission immediately");
	}
	{
		Upscaling u;
		u.hold = true;
		globals::state->safeMode = true;
		u.ServiceVRRenderScalePostMutationWatchdog("missing completion event");
		Require(u.resumes == 0 && u.hold && globals::state->saveLoadSafeModeEndFrame != 0,
			"Recovery may arm normal load grace but cannot bypass it");
		globals::state->safeMode = false;
		testTick += kVRRenderScalePresentationRetryMilliseconds;
		u.ServiceVRRenderScalePostMutationWatchdog("grace completed");
		Require(u.resumes == 1 && u.nativeFallbacks == 0,
			"A newly admitted resume gets its own bounded proof interval");
		testTick += kVRRenderScalePresentationRetryMilliseconds;
		u.ServiceVRRenderScalePostMutationWatchdog("proof did not arrive");
		Require(u.nativeFallbacks == 1, "Unproven presentation must fall back even without a cover");
		u.vrRenderScalePostMutationSerializationEpoch = 8;
		u.vrRenderScaleUnresolvedPhysicalMutationEpoch = 8;
		Require(u.ShouldDeferVRRenderScalePresentationSuccessor(), "Native recovery cannot be superseded during physical mutation");
		testTick += kVRRenderScalePostMutationStalledDeadlineMilliseconds;
		u.ServiceVRRenderScalePostMutationWatchdog("transferred native timeout");
		Require(u.vrRenderScalePresentationRecoveryStage == Stage::Quarantined && u.nativeFallbacks == 1,
			"Physical owner transfer cannot renew fallback quota or timeout");
	}
	{
		Upscaling u;
		u.resourcesReady = false;
		u.nativeAdmission = false;
		u.ServiceVRRenderScalePostMutationWatchdog("unavailable resources");
		testTick += kVRRenderScalePresentationRetryMilliseconds;
		u.ServiceVRRenderScalePostMutationWatchdog("native rejected");
		Require(u.vrRenderScalePresentationRecoveryStage == Stage::Quarantined &&
					!u.vrRenderScalePresentationFallbackAdmissionActive,
			"Rejected native admission closes its exception and retains quarantine");
	}
	{
		Upscaling u;
		globals::state->lastCompletedWorldRenderFrame = 999;
		u.ServiceVRRenderScalePostMutationWatchdog("stale world while menu is open");
		Require(u.resumes == 0 && u.nativeFallbacks == 0, "A stale world frame cannot admit presentation recovery");
		globals::state->lastCompletedWorldRenderFrame = 1000;
	}
	{
		Upscaling u;
		u.resourcesReady = false;
		u.ServiceVRRenderScalePostMutationWatchdog("failed presentation");
		testTick += kVRRenderScalePresentationRetryMilliseconds;
		u.ServiceVRRenderScalePostMutationWatchdog("native fallback queued");
		worldReady = false;
		testTick += kVRRenderScalePostMutationStalledDeadlineMilliseconds;
		u.ServiceVRRenderScalePostMutationWatchdog("native fallback cannot produce a world frame");
		Require(u.vrRenderScalePresentationRecoveryStage == Stage::Quarantined && u.nativeFallbacks == 1,
			"Missing world frames must not disable an admitted fallback's deadline");
		worldReady = true;
	}
	{
		Upscaling u;
		worldReady = false;
		for (unsigned frame = 0; frame < 100; ++frame)
			u.ServiceVRRenderScalePostMutationWatchdog("blocked world diagnostics");
		Require(u.diagnostics == 1 && u.resumes == 0 && u.nativeFallbacks == 0,
			"A stalled world gate needs one diagnostic without repeated sampling or unsafe work");
		worldReady = true;
	}
	{
		Upscaling u;
		u.probeConcurrentRetirement = true;
		u.ServiceVRRenderScalePostMutationWatchdog("stereo retirement races recovery admission");
		Require(!u.concurrentRetirementEntered && u.vrRenderScalePostMutationSerializationEpoch == 7 &&
					u.resumes == 1 && u.nativeFallbacks == 0,
			"Stereo retirement cannot invalidate ownership during recovery admission");
	}
	{
		Upscaling u;
		u.resourcesReady = false;
		u.ServiceVRRenderScalePostMutationWatchdog("native fallback needed");
		testTick += kVRRenderScalePresentationRetryMilliseconds;
		u.ServiceVRRenderScalePostMutationWatchdog("admit exact native worker");
		Require(u.EnsureVRRenderScaleProviderNeutralNativeRecovery("reuse pending worker", true) && u.nativeFallbacks == 1,
			"Reusing the exact queued native worker cannot create a successor");
		u.vrRenderScalePostMutationSerializationEpoch = 8;
		u.pendingPerfModeRenderTargetRecreate = false;
		u.perfModeRenderTargetRecreateInProgress = true;
		Require(u.EnsureVRRenderScaleProviderNeutralNativeRecovery("reuse active worker", true) && u.nativeFallbacks == 1,
			"The active native creator must recognize its own ownership");
		u.perfModeRenderTargetRecreateInProgress = false;
		Require(!u.EnsureVRRenderScaleProviderNeutralNativeRecovery("reject another successor", true) && u.nativeFallbacks == 1,
			"An exhausted native worker cannot admit another successor");
	}
	{
		Upscaling u;
		u.hold = true;
		u.vrPostLoadCompositorInFlightSubmitCount = 1;
		u.ServiceVRRenderScalePostMutationWatchdog("in-flight stereo during resume");
		Require(u.quarantinedCycles == 1 && u.resumes == 1 && !u.hold &&
					u.vrRenderScalePostMutationSerializationEpoch == 7,
			"Owned reset must quarantine an in-flight submit without recursive locking or early retirement");
	}
	{
		Upscaling u;
		u.hold = true;
		u.controller.state = VRRenderScaleTransitionState::Active;
		u.ResetVRPostLoadCompositorHold();
		Require(!u.hold && u.vrRenderScalePostMutationSerializationEpoch == 0,
			"Unowned reset must acquire the hold lock and retire completed presentation");
	}
	{
		Upscaling u;
		u.vrRenderScalePostMutationSerializationEpoch = 0;
		std::latch held{ 1 }, release{ 1 };
		std::thread recoveryOwner([&] {
			const std::scoped_lock lock(u.perfModeRenderTargetRecreateQueueMutex, u.vrPostLoadCompositorHoldMutex);
			held.count_down();
			release.wait();
		});
		held.wait();
		const auto sampled = clockReads;
		for (unsigned frame = 0; frame < 1000; ++frame)
			u.ServiceVRRenderScalePostMutationWatchdog("settled gameplay with contended recovery locks");
		release.count_down();
		recoveryOwner.join();
		Require(clockReads == sampled && u.snapshots == 0 && u.resumes == 0 && u.recreations == 0 && u.diagnostics == 0,
			"Settled gameplay must neither wait for recovery locks nor sample, log, retry or rebuild");
	}
	std::cout << "Presentation recovery regression cases passed\n";
}
