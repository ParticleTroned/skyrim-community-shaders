#include "Features/Upscaling/VRRelatchDrainPolicy.h"
#include "Features/Upscaling/VRRelatchReleasePolicy.h"
#include "Features/Upscaling/VRRenderScaleRetryTelemetry.h"
#include "Features/Upscaling/VRVendorRelatchPolicy.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

using ID3D11Device = int;
using ID3D11DeviceContext = int;
using ID3D12CommandQueue = int;
using ID3D12Fence = int;
namespace winrt
{
	template <class T>
	struct com_ptr
	{
		T* value = nullptr;
		T* get() const { return value; }
		explicit operator bool() const { return value != nullptr; }
		void copy_from(T* a_value) { value = a_value; }
	};
}
namespace globals
{
	namespace game
	{
		bool isVR = true;
	}
	namespace d3d
	{
		int originalDevice = 1, originalContext = 2, replacement = 3;
		int* device = &originalDevice;
		int* context = &originalContext;
	}
}

namespace logger
{
	template <class... Args>
	void debug(Args&&...)
	{}
}
namespace magic_enum
{
	template <class T>
	const char* enum_name(T)
	{
		return "test";
	}
}
const char* BoolText(bool a_value) { return a_value ? "true" : "false"; }
bool ShouldEmitUpscalingDiagLogs() { return false; }

enum class UpscaleMethod
{
	kNONE,
	kTAA,
	kFSR,
	kDLSS
};
bool IsVendorUpscalingMethod(UpscaleMethod a_method)
{
	return a_method == UpscaleMethod::kFSR || a_method == UpscaleMethod::kDLSS;
}
struct Upscaling;
bool HasPendingVRVendorRuntimeReset(const Upscaling&, UpscaleMethod);

struct Streamline
{
	VRRelatchDrainPolicy::Proof dlssRelatchDrainProof;
	enum class DLSSViewportPreparationResult
	{
		Ready,
		Pending,
		Failed
	};
	enum class DLSSViewportRole
	{
		FullEye,
		SubmitStageFoveatedCenter
	};
	std::array<DLSSViewportPreparationResult, 2> preparation{
		DLSSViewportPreparationResult::Ready, DLSSViewportPreparationResult::Ready
	};
	std::array<uint32_t, 2> calls{};
	DLSSViewportPreparationResult PrepareVRDLSSViewport(
		DLSSViewportRole a_role, uint32_t, uint32_t
#ifdef DEVBENCH_BRIDGE_ENABLED
		,
		VRRenderScaleRetryTelemetry::ViewportObservation*
#endif
	)
	{
		const auto index = static_cast<std::size_t>(a_role);
		++calls[index];
		return preparation[index];
	}
};

#include "vr_relatch_promotion_helpers_under_test.h"

// The production promotion methods own the guard, stereo accumulation, provider
// preparation, and final next-cycle decision; only external observations vary.
struct Upscaling
{
	enum class VRVendorRuntimeLifecyclePhase
	{
		WaitingForDrain,
		Failed,
		Ready
	};
	enum class VRRenderScaleRetryKind
	{
		Backend
	};
	enum class VRRenderScaleCPUPerformanceCounter
	{
		PromotionFastSkips,
		PromotionCandidates
	};
	enum class VRRenderScaleMemoryTrimReason
	{
		None,
		RapidRelatch
	};
	struct VRRenderScaleTransitionMetrics
	{
		bool valid = true, superseded = false;
		uint64_t transitionEpoch = 7, requestID = 4;
		uint32_t contractGeneration = 11;
		uint32_t retries = 1, readinessDeferrals = 0, backendDeferrals = 1, pressureDeferrals = 0, retirementDeferrals = 0, failures = 0;
		uint32_t memoryTrimFailures = 0, memoryPreRecreateDrainFailures = 0;
	};
	struct BootSnapshot
	{
		bool valid = true;
		uint32_t generation = 11;
		UpscaleMethod method = UpscaleMethod::kFSR;
		uint32_t qualityMode = 3;
	};
	struct
	{
		BootSnapshot snapshot;
		uint32_t trueHMDEyeWidth = 200, trueHMDEyeHeight = 200;
		BootSnapshot GetBootSnapshot() const { return snapshot; }
		bool vendorAllowed = false;
		uint32_t enableCalls = 0;
		void SetSubmitStageVendorAllowed(bool a_allowed)
		{
			vendorAllowed = a_allowed;
			if (a_allowed)
				++enableCalls;
		}
	} perfMode;
	struct
	{
		VRRelatchDrainPolicy::Proof fsrRelatchDrainProof;
		winrt::com_ptr<ID3D12Fence> runtimeD3D12Fence;
		bool quarantined = false, failureLatched = false, detached = false, contextsCompatible = true;
		bool IsHostFSRStateQuarantined() const { return quarantined; }
		bool IsRuntimeUpscalerFailureLatched() const { return failureLatched; }
		bool IsRuntimeUpscalerOwnershipDetached() const { return detached; }
		uint64_t GetFSRRelatchDrainRevision() const noexcept { return fsrRelatchDrainProof.ProviderRevision(); }
		winrt::com_ptr<ID3D12Fence> GetFSRRelatchRuntimeFence() const noexcept { return runtimeD3D12Fence; }
		bool IsFSRRelatchReleaseIdentityCurrent(uint64_t a_revision, ID3D12Fence* a_fence) const noexcept
		{
			return fsrRelatchDrainProof.ProviderRevision() == a_revision && runtimeD3D12Fence.get() == a_fence && !detached;
		}
		bool AreFSRProviderContextsCompatible(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, bool) const { return contextsCompatible; }
		bool HasFSRResources() const { return true; }
	} fidelityFX;
	Streamline streamline;
	struct
	{
		winrt::com_ptr<ID3D12CommandQueue> commandQueue;
	} dx12SwapChain;
	struct TransitionSnapshot
	{
		uint64_t targetEpoch = 7;
		struct
		{
			bool valid = true;
			uint64_t transitionEpoch = 7, requestID = 4;
			uint32_t contractGeneration = 11;
		} applied;
		struct
		{
			VRRenderScaleTransitionMetrics current;
		} metrics;
		struct
		{
			bool active = false;
		} postLoadRecovery;
		struct
		{
			uint32_t pendingSets = 0;
			uint64_t oldestEpoch = 0, newestEpoch = 0;
		} retirement;
		struct
		{
			bool pending = false;
			uint64_t oldestEpoch = 0, newestEpoch = 0;
			uint32_t pendingGenerations = 0, pendingReleaseCount = 0, fenceFailures = 0, lastReleasedPointerCount = 0;
			bool capacityBlocked = false;
		} engineTargetRetirement;
		struct
		{
			uint64_t ownerEpoch = 0;
			bool pending = false, lastSucceeded = false;
			VRRenderScaleMemoryTrimReason reason = VRRenderScaleMemoryTrimReason::None;
			uint32_t fenceFailures = 0;
		} memoryTrim;
	} transitionSnapshot;
	TransitionSnapshot GetVRRenderScaleTransitionSnapshot() const { return transitionSnapshot; }
	struct PhysicalMutation
	{
		uint64_t epoch = 0, serializationEpoch = 0;
	} physicalMutation;
	PhysicalMutation GetVRRenderScalePhysicalMutationSnapshot() const { return physicalMutation; }
	struct NativeRestore
	{
		uint64_t ownerEpoch = 0;
	} nativeRestore;
	NativeRestore GetVRLowPeakNativeRestoreProgress() const { return nativeRestore; }
	std::atomic<bool> postLoadRuntimeResetPending{ false };
	std::atomic<uint64_t> vrIntermediateRetirementCompletedSerial{ 0 }, vrIntermediateRetirementLastIssuedSerial{ 0 }, vrRenderScaleCleanupFailureSerial{ 0 };
#include "vr_relatch_release_state_under_test.h"
	uint64_t vrRenderScaleOwnedReleaseRejectedEpoch = 0;
	VRRenderScaleOwnedReleaseReceipt submitStageOwnedReleaseReceipt;
	struct RetiredIntermediate
	{
		uint64_t transitionEpoch = 7;
		uint32_t contractGeneration = 11;
		uint64_t retirementSerial = 1;
	};
	std::vector<RetiredIntermediate> retiredVRIntermediateTextures;
	void PublishSuccessfulApply(VRRenderScaleOwnedReleaseReceipt ownedRelease)
	{
		const auto attemptMetrics = transitionSnapshot.metrics.current;
		const auto relatchUpscaleMethod = selectedMethod;
		struct
		{
			uint32_t qualityMode = 3;
			bool fsr4RuntimeEnable = false;
		} relatchSettings;
		struct
		{
			uint32_t renderEyeWidth = 100, renderEyeHeight = 100;
		} targetResourceProfile;
		const uint64_t relatchEpoch = transitionSnapshot.targetEpoch;
		const bool immutableSettingsRelatch = true;
		const uint64_t postLoadRecoveryEpoch = 0;
		const bool providerNeutralNativeRecoveryRequested = false, postMutationEmergencyRecoveryAttempt = false, presentationDeadlineFallbackRequested = false;
		struct
		{
			uint32_t frameCount = 100;
		} currentState;
		const auto* state = &currentState;
#include "vr_relatch_publication_under_test.h"
	}
	static VRRelatchReleasePolicy::RetryHistory GetOwnedReleaseRetryHistory(const VRRenderScaleTransitionMetrics&);
	bool CanUseVRRenderScaleOwnedRelease(const VRRenderScaleOwnedReleaseReceipt&);
	void RevalidateVRRenderScaleOwnedRelease();
	std::recursive_mutex submitStageVendorResumeStableEyeMaskMutex;
	std::atomic<uint32_t> submitStageDLSSViewportPreparationGeneration{ 0 };
	std::atomic<bool> submitStageDLSSViewportPreparationPending{ false }, submitStageDLSSViewportPreparationFailed{ false };
	std::atomic<uint32_t> submitStageVendorResumeFrame{ 0 };
	std::atomic<bool> submitStageVendorResumeProofDrivenRelease{ false };
	uint64_t submitStageVendorResumeStabilitySerial = 0;
	std::atomic<uint32_t> submitStageVendorResumeStableFrames{ 0 }, submitStageVendorResumeLastStableFrame{ 0 };
	uint64_t submitStageVendorResumeStableEyeMaskCycle = 0;
	uint32_t submitStageVendorResumeStableEyeMaskFrame = 0, submitStageVendorResumeStableEyeMask = 0;
	std::atomic<uint64_t> submitStageVendorResumeReleaseCandidateCycle{ 0 };
	std::atomic<bool> pendingDLSSReset{ false }, pendingFSRReset{ false };
	std::atomic<bool> pendingPerfModeRenderTargetRecreate{ false }, perfModeRenderTargetRecreateInProgress{ false };
	bool lifecycleDeferred = false, fidelityAccepted = true, foveated = false;
	bool deviceLost = false, deviceRemoved = false, transitionPending = false, vendorResetPending = false;
	bool physicalContractConverged = true;
	UpscaleMethod selectedMethod = UpscaleMethod::kFSR;
	uint32_t backendRetries = 0, viewportFailures = 0, fidelityCalls = 0, watchdogClears = 0;
#ifdef DEVBENCH_BRIDGE_ENABLED
	std::vector<VRRenderScaleRetryTelemetry::EventType> events;
	struct OwnedEvent
	{
		VRRenderScaleRetryTelemetry::EventType type;
		bool eligible;
		uint64_t epoch;
		uint64_t requestID;
	};
	std::vector<OwnedEvent> ownedEvents;
	void RecordVRRenderScaleOwnedReleaseEvent(VRRenderScaleRetryTelemetry::EventType a_type, const VRRenderScaleOwnedReleaseReceipt& a_receipt, bool a_eligible, const char*)
	{
		ownedEvents.push_back({ a_type, a_eligible, a_receipt.drain.epoch, a_receipt.drain.requestID });
	}
	void RecordVRRenderScaleResumeEvent(VRRenderScaleRetryTelemetry::EventType a_type, const char*, uint32_t = 0, bool = false)
	{
		events.push_back(a_type);
	}
	void RecordVRRenderScaleViewportPreparation(const VRRenderScaleRetryTelemetry::ViewportObservation&, Streamline::DLSSViewportPreparationResult, uint32_t) {}
	void RecordVRRenderScaleCPUPerformanceCounter(VRRenderScaleCPUPerformanceCounter) {}
#endif
	bool ShouldDeferVRVendorLifecycleMutation() const { return lifecycleDeferred; }
	uint32_t GetRuntimeQualityMode() const { return 3; }
	uint32_t GetRuntimeDLSSPreset() const { return 0; }
	UpscaleMethod GetRuntimeUpscaleMethod() const { return selectedMethod; }
	bool IsFoveatedVendorDispatchEnabled(UpscaleMethod) const { return foveated; }
	void RecordVRVendorRuntimeLifecycle(UpscaleMethod, VRVendorRuntimeLifecyclePhase a_phase, uint32_t, const char*)
	{
		if (a_phase == VRVendorRuntimeLifecyclePhase::Failed)
			++viewportFailures;
	}
	void RecordVRRenderScaleTransitionRetry(VRRenderScaleRetryKind
#ifdef DEVBENCH_BRIDGE_ENABLED
		,
		const char*
#endif
	)
	{
		++backendRetries;
	}
	bool MarkSubmitStageDeviceLostIfDeviceRemoved(const char*)
	{
		deviceLost = deviceRemoved;
		return deviceLost;
	}
	bool RecordVRRenderScaleFidelityObservation(UpscaleMethod, uint32_t, bool, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, bool)
	{
		++fidelityCalls;
		return fidelityAccepted;
	}
	bool HasPendingVRUpscalingTransition() const { return transitionPending; }
	bool IsSubmitStageDeviceLost() const { return deviceLost; }
	bool IsVRRenderScalePhysicalContractConverged(UpscaleMethod, uint32_t) const { return physicalContractConverged; }
	void ClearSubmitStageBoundsFallbackWatchdog() { ++watchdogClears; }
	void ArmSubmitStageVendorResumeCooldown(uint32_t, bool, const VRRenderScaleOwnedReleaseReceipt* = nullptr);
	void ClearSubmitStageVendorResumeCooldown();
	void ClearSubmitStageVendorResumeStability();
	void TryPromoteVRRenderScaleSubmitStageContract(uint32_t, uint64_t, uint32_t, bool, UpscaleMethod, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, bool);
	void ServiceSubmitStageVendorResumePromotion(uint64_t);
};

bool HasPendingVRVendorRuntimeReset(const Upscaling& a_upscaling, UpscaleMethod)
{
	return a_upscaling.vendorResetPending;
}

#include "vr_relatch_promotion_under_test.h"
#include "vr_relatch_release_under_test.h"

namespace
{
	void Require(bool a_condition, const char* a_message)
	{
		if (!a_condition)
			throw std::runtime_error(a_message);
	}

	void Eye(Upscaling& a_controller, uint32_t a_frame, uint64_t a_cycle, uint32_t a_eye, bool a_stable = true, uint32_t a_generation = 11)
	{
		a_controller.TryPromoteVRRenderScaleSubmitStageContract(a_frame, a_cycle, a_eye, a_stable,
			a_controller.selectedMethod, a_generation, 100, 100, 200, 200, false);
	}

	void Pair(Upscaling& a_controller, uint32_t a_frame, uint64_t a_cycle)
	{
		Eye(a_controller, a_frame, a_cycle, 0);
		Require(a_controller.submitStageVendorResumeReleaseCandidateCycle == 0,
			"A left eye alone published a promotion candidate");
		Eye(a_controller, a_frame, a_cycle, 1);
	}

	void HealthyProofQualifiesBeforeFallbackWithoutEnablingCurrentCycle()
	{
		Upscaling controller;
		controller.backendRetries = 1;
		controller.ArmSubmitStageVendorResumeCooldown(100, true);
		Require(!controller.perfMode.vendorAllowed && controller.submitStageVendorResumeFrame == 100,
			"Arming proof did not close vendor admission at the original guard frame");
		for (uint32_t frame = 100; frame < 100 + kVRSubmitStageVendorRelatchStableFrames; ++frame)
			Pair(controller, frame, frame);
		const auto candidate = controller.submitStageVendorResumeReleaseCandidateCycle.load();
		Require(candidate == 100 + kVRSubmitStageVendorRelatchStableFrames - 1 &&
					candidate < 100 + kVRUpscalingTransitionApplyDelayFrames && !controller.perfMode.vendorAllowed,
			"Exact coherent proof did not qualify before the conservative guard");
		controller.ServiceSubmitStageVendorResumePromotion(candidate);
		Require(!controller.perfMode.vendorAllowed, "Promotion enabled vendor work inside the qualifying cycle");
		controller.ServiceSubmitStageVendorResumePromotion(candidate + 1);
		Require(controller.perfMode.vendorAllowed && controller.perfMode.enableCalls == 1 &&
					controller.backendRetries == 1 && controller.watchdogClears == 1,
			"Next-cycle release changed retry history or failed to enable the proven contract");
		controller.ServiceSubmitStageVendorResumePromotion(candidate + 2);
		Require(controller.perfMode.enableCalls == 1, "Consumed promotion candidate enabled vendor work twice");
	}

	void ConservativeReleaseRetainsOriginalSixFrameGuard()
	{
		Upscaling controller;
		controller.ArmSubmitStageVendorResumeCooldown(100, false);
		for (uint32_t frame = 100; frame < 100 + kVRUpscalingTransitionApplyDelayFrames; ++frame) {
			Pair(controller, frame, frame);
			Require(controller.submitStageVendorResumeReleaseCandidateCycle == 0 && !controller.perfMode.vendorAllowed,
				"A conservative attempt bypassed the original settling window");
		}
		Pair(controller, 106, 106);
		Require(controller.submitStageVendorResumeReleaseCandidateCycle == 106,
			"The fallback guard restarted or changed its original six-frame deadline");
	}

	void MissingDuplicateAndMismatchedEyesCannotQualify()
	{
		for (uint32_t scenario = 0; scenario < 4; ++scenario) {
			Upscaling controller;
			controller.ArmSubmitStageVendorResumeCooldown(100, true);
			for (uint32_t frame = 100; frame < 108; ++frame) {
				Eye(controller, frame, frame, 0);
				if (scenario == 0)
					Eye(controller, frame, frame, 0);
				else if (scenario == 1)
					Eye(controller, frame, frame + 20, 1);
				else if (scenario == 2)
					Eye(controller, frame + 20, frame, 1);
				else
					Eye(controller, frame, frame, 1, false);
			}
			Require(controller.submitStageVendorResumeReleaseCandidateCycle == 0 && !controller.perfMode.vendorAllowed,
				"Incomplete, duplicate-only, mismatched, or unstable stereo qualified");
		}
		Upscaling controller;
		controller.ArmSubmitStageVendorResumeCooldown(100, true);
		Pair(controller, 100, 100);
		Pair(controller, 102, 102);
		Pair(controller, 104, 104);
		Require(controller.submitStageVendorResumeStableFrames == 1 && controller.submitStageVendorResumeReleaseCandidateCycle == 0,
			"Nonconsecutive stereo cycles accumulated toward release");
	}

	void ProviderPreparationAndFidelityRevokeEarlyRelease()
	{
		using Result = Streamline::DLSSViewportPreparationResult;
		for (bool center : { false, true }) {
			Upscaling controller;
			controller.selectedMethod = UpscaleMethod::kDLSS;
			controller.foveated = center;
			controller.streamline.preparation[center ? 1 : 0] = Result::Pending;
			controller.ArmSubmitStageVendorResumeCooldown(100, true);
			Pair(controller, 100, 100);
			Pair(controller, 101, 101);
			Require(!controller.submitStageVendorResumeProofDrivenRelease && controller.backendRetries == 1 &&
						controller.submitStageVendorResumeReleaseCandidateCycle == 0,
				"Pending exact viewport preparation retained early release or repeated its retry");
			controller.streamline.preparation = { Result::Ready, Result::Ready };
			for (uint32_t frame = 102; frame < 106; ++frame) {
				Pair(controller, frame, frame);
				Require(controller.submitStageVendorResumeReleaseCandidateCycle == 0,
					"Provider recovery restored revoked proof before the original guard");
			}
			Pair(controller, 106, 106);
			Require(controller.submitStageVendorResumeReleaseCandidateCycle == 106 && controller.backendRetries == 1,
				"Ready preparation failed to honor the original conservative deadline");
		}
		for (bool removed : { false, true }) {
			Upscaling controller;
			controller.selectedMethod = UpscaleMethod::kDLSS;
			controller.streamline.preparation[0] = Result::Failed;
			controller.deviceRemoved = removed;
			controller.ArmSubmitStageVendorResumeCooldown(100, true);
			Eye(controller, 100, 100, 0);
			Require(!controller.submitStageVendorResumeProofDrivenRelease && !controller.perfMode.vendorAllowed &&
						controller.deviceLost == removed && controller.submitStageVendorResumeReleaseCandidateCycle == 0,
				"Failed preparation or device loss retained early vendor admission");
		}
		Upscaling controller;
		controller.ArmSubmitStageVendorResumeCooldown(100, true);
		controller.fidelityAccepted = false;
		Pair(controller, 100, 100);
		Require(!controller.submitStageVendorResumeProofDrivenRelease && controller.submitStageVendorResumeStableFrames == 0,
			"A rejected fidelity observation retained promotion evidence");
	}

	void FinalNextCycleChecksRevokeInvalidatedCandidates()
	{
		for (uint32_t blocker = 0; blocker < 7; ++blocker) {
			Upscaling controller;
			controller.ArmSubmitStageVendorResumeCooldown(100, true);
			for (uint32_t frame = 100; frame < 103; ++frame)
				Pair(controller, frame, frame);
			Require(controller.submitStageVendorResumeReleaseCandidateCycle == 102, "Test did not qualify a real candidate");
			switch (blocker) {
			case 0:
				controller.transitionPending = true;
				break;
			case 1:
				controller.pendingPerfModeRenderTargetRecreate = true;
				break;
			case 2:
				controller.perfModeRenderTargetRecreateInProgress = true;
				break;
			case 3:
				controller.deviceLost = true;
				break;
			case 4:
				controller.selectedMethod = UpscaleMethod::kTAA;
				break;
			case 5:
				controller.vendorResetPending = true;
				break;
			case 6:
				controller.physicalContractConverged = false;
				break;
			}
			controller.ServiceSubmitStageVendorResumePromotion(103);
			Require(!controller.perfMode.vendorAllowed && controller.perfMode.enableCalls == 0 &&
						controller.submitStageVendorResumeReleaseCandidateCycle == 0,
				"The final release reused an invalidated or superseded candidate");
		}
		Upscaling controller;
		controller.ArmSubmitStageVendorResumeCooldown(100, true);
		Pair(controller, 100, 100);
		controller.lifecycleDeferred = true;
		Pair(controller, 101, 101);
		Require(controller.submitStageVendorResumeStableFrames == 0 && controller.fidelityCalls == 2,
			"Lifecycle deferral performed provider/fidelity work or retained stereo evidence");
	}

	Upscaling::VRRenderScaleOwnedReleaseReceipt HealthyOwnedReceipt(Upscaling& a_controller)
	{
		Upscaling::VRRenderScaleOwnedReleaseReceipt receipt;
		receipt.drain.epoch = 7;
		receipt.drain.requestID = 4;
		receipt.drain.sourceGeneration = 10;
		receipt.drain.targetGeneration = 11;
		receipt.drain.priorCleanupDebt = false;
		receipt.drain.needsFSR = true;
		receipt.drain.device.copy_from(globals::d3d::device);
		receipt.drain.contextOwner.copy_from(globals::d3d::context);
		receipt.drain.beforeWait = { .valid = true, .epoch = 7, .requestID = 4 };
		receipt.drain.afterOwnedWait = { .valid = true, .epoch = 7, .requestID = 4, .retries = 1, .backendDeferrals = 1 };
		receipt.consumed = receipt.sharedCleanupSatisfied = receipt.targetPrepared = receipt.memoryAdmissionSatisfied = true;
		receipt.method = UpscaleMethod::kFSR;
		receipt.qualityMode = 3;
		receipt.targetFSRRevision = a_controller.fidelityFX.fsrRelatchDrainProof.ProviderRevision();
		receipt.targetDLSSRevision = a_controller.streamline.dlssRelatchDrainProof.ProviderRevision();
		receipt.engine.observed = true;
		receipt.engine.pendingAtBegin = false;
		receipt.publishedHistory = Upscaling::GetOwnedReleaseRetryHistory(a_controller.transitionSnapshot.metrics.current);
		return receipt;
	}

	void SuccessfulPublicationSealsReceiptAndKeepsBackendHistory()
	{
		Upscaling controller;
		auto receipt = HealthyOwnedReceipt(controller);
		receipt.targetPrepared = false;
		controller.PublishSuccessfulApply(receipt);
		Require(controller.submitStageVendorResumeProofDrivenRelease && controller.submitStageOwnedReleaseReceipt.consumed &&
					controller.submitStageOwnedReleaseReceipt.targetPrepared &&
					controller.submitStageOwnedReleaseReceipt.inputWidth == 100 && controller.submitStageOwnedReleaseReceipt.outputWidth == 200,
			"The successful production publication did not seal exact target preparation");
		for (uint32_t frame = 100; frame < 103; ++frame)
			Pair(controller, frame, frame);
		controller.ServiceSubmitStageVendorResumePromotion(103);
		Require(controller.perfMode.vendorAllowed && controller.transitionSnapshot.metrics.current.retries == 1 &&
					controller.transitionSnapshot.metrics.current.backendDeferrals == 1 && controller.transitionSnapshot.metrics.current.readinessDeferrals == 0,
			"Owned release changed historical Backend classification or failed to present");
	}

	void MixedRetriesAndIncompletePublicationKeepFallbackGuard()
	{
		for (uint32_t blocker = 0; blocker < 7; ++blocker) {
			Upscaling controller;
			auto receipt = HealthyOwnedReceipt(controller);
			switch (blocker) {
			case 0:
				++controller.transitionSnapshot.metrics.current.retries;
				++controller.transitionSnapshot.metrics.current.backendDeferrals;
				break;
			case 1:
				controller.transitionSnapshot.metrics.current.retries = UINT32_MAX;
				controller.transitionSnapshot.metrics.current.backendDeferrals = UINT32_MAX;
				break;
			case 2:
				receipt.consumed = false;
				break;
			case 3:
				receipt.sharedCleanupSatisfied = false;
				break;
			case 4:
				receipt.memoryAdmissionSatisfied = false;
				break;
			case 5:
				controller.physicalMutation.epoch = 7;
				break;
			case 6:
				controller.transitionSnapshot.metrics.current.failures = 1;
				break;
			}
			const auto originalRetries = controller.transitionSnapshot.metrics.current.retries;
			controller.PublishSuccessfulApply(receipt);
			Require(!controller.submitStageVendorResumeProofDrivenRelease && !controller.submitStageOwnedReleaseReceipt.consumed &&
						controller.submitStageVendorResumeFrame == 100 && controller.transitionSnapshot.metrics.current.retries == originalRetries,
				"Mixed history or partial mutation/cleanup acquired an early-release receipt");
		}
	}

	void ActualReceiptAssemblyRejectsStaleAndBlockingEvidence()
	{
		for (uint32_t blocker = 0; blocker < 37; ++blocker) {
			Upscaling controller;
			auto receipt = HealthyOwnedReceipt(controller);
			Require(controller.CanUseVRRenderScaleOwnedRelease(receipt), "Healthy receipt failed its production admission baseline");
			switch (blocker) {
			case 0:
				receipt.targetPrepared = false;
				break;
			case 1:
				receipt.drain.device.value = &globals::d3d::replacement;
				break;
			case 2:
				receipt.drain.contextOwner.value = &globals::d3d::replacement;
				break;
			case 3:
				receipt.targetRuntimeQueue.value = &globals::d3d::replacement;
				break;
			case 4:
				controller.fidelityFX.quarantined = true;
				break;
			case 5:
				controller.fidelityFX.failureLatched = true;
				break;
			case 6:
				controller.fidelityFX.detached = true;
				break;
			case 7:
				controller.fidelityFX.fsrRelatchDrainProof.Invalidate();
				break;
			case 8:
				controller.streamline.dlssRelatchDrainProof.Invalidate();
				break;
			case 9:
				controller.perfMode.snapshot.generation = 12;
				break;
			case 10:
				controller.perfMode.snapshot.method = UpscaleMethod::kTAA;
				break;
			case 11:
				controller.perfMode.snapshot.qualityMode = 4;
				break;
			case 12:
				controller.transitionSnapshot.targetEpoch = 8;
				break;
			case 13:
				++controller.transitionSnapshot.metrics.current.requestID;
				break;
			case 14:
				++controller.transitionSnapshot.metrics.current.contractGeneration;
				break;
			case 15:
				controller.transitionSnapshot.metrics.current.memoryTrimFailures = 1;
				break;
			case 16:
				controller.transitionSnapshot.metrics.current.memoryPreRecreateDrainFailures = 1;
				break;
			case 17:
				controller.transitionSnapshot.postLoadRecovery.active = true;
				break;
			case 18:
				controller.postLoadRuntimeResetPending = true;
				break;
			case 19:
				controller.nativeRestore.ownerEpoch = 7;
				break;
			case 20:
				controller.physicalContractConverged = false;
				break;
			case 21:
				controller.fidelityFX.contextsCompatible = false;
				break;
			case 22:
				controller.physicalMutation.serializationEpoch = 8;
				break;
			case 23:
				controller.transitionSnapshot.engineTargetRetirement.capacityBlocked = true;
				break;
			case 24:
				controller.vrRenderScaleCleanupFailureSerial = 1;
				break;
			case 25:
				controller.vrIntermediateRetirementLastIssuedSerial = 1;
				break;
			case 26:
				controller.transitionSnapshot.memoryTrim.pending = true;
				break;
			case 27:
				controller.transitionSnapshot.memoryTrim.fenceFailures = 1;
				break;
			case 28:
				receipt.drain.priorCleanupDebt = true;
				break;
			case 29:
				controller.transitionSnapshot.metrics.current.superseded = true;
				break;
			case 30:
				controller.vrRenderScaleOwnedReleaseRejectedEpoch = 7;
				break;
			case 31:
				receipt.targetRuntimeFence.value = &globals::d3d::replacement;
				break;
			case 32:
				controller.transitionSnapshot.applied.valid = false;
				break;
			case 33:
				++controller.transitionSnapshot.applied.requestID;
				break;
			case 34:
				++controller.transitionSnapshot.applied.transitionEpoch;
				break;
			case 35:
				++controller.transitionSnapshot.applied.contractGeneration;
				break;
			case 36:
				controller.deviceLost = true;
				break;
			}
			const bool eligible = controller.CanUseVRRenderScaleOwnedRelease(receipt);
			if (eligible)
				std::fprintf(stderr, "Unexpected owned-release admission for blocker %u\n", blocker);
			Require(!eligible,
				"Production receipt assembly accepted stale identity, failed preparation, recovery, or blocking debt");
		}
	}

	void ConsumedOldDrainIdentityCannotStandInForTargetReadiness()
	{
		Upscaling controller;
		auto receipt = HealthyOwnedReceipt(controller);
		receipt.drain.runtimeQueue.value = &globals::d3d::replacement;
		receipt.drain.runtimeFence.value = &globals::d3d::replacement;
		receipt.drain.fsrTicket = receipt.drain.dlssTicket = 23;
		Require(controller.CanUseVRRenderScaleOwnedRelease(receipt),
			"Consumed old drain identities incorrectly replaced separately published target readiness");
		controller.fidelityFX.contextsCompatible = false;
		Require(!controller.CanUseVRRenderScaleOwnedRelease(receipt),
			"An old consumed drain stood in for missing target provider preparation");
	}

	void OwnedDetachedDebtCanRemainWithoutReleasingResources()
	{
		Upscaling controller;
		auto receipt = HealthyOwnedReceipt(controller);
		controller.retiredVRIntermediateTextures.push_back({});
		controller.vrIntermediateRetirementLastIssuedSerial = 1;
		controller.transitionSnapshot.retirement = { .pendingSets = 1, .oldestEpoch = 7, .newestEpoch = 7 };
		controller.PublishSuccessfulApply(receipt);
		Require(controller.submitStageVendorResumeProofDrivenRelease && controller.submitStageOwnedReleaseReceipt.intermediateCount == 1,
			"Exact detached intermediate ownership was treated as unresolved presentation work");
		for (uint32_t frame = 100; frame < 103; ++frame)
			Pair(controller, frame, frame);
		controller.ServiceSubmitStageVendorResumePromotion(103);
		Require(controller.perfMode.vendorAllowed && controller.vrIntermediateRetirementCompletedSerial == 0 &&
					controller.retiredVRIntermediateTextures.size() == 1 && controller.transitionSnapshot.retirement.pendingSets == 1,
			"Presentation release prematurely retired resources or erased outstanding cleanup evidence");
	}

	void ReceiptMustRemainValidUntilFinalNextCycleRelease()
	{
		for (uint32_t blocker = 0; blocker < 4; ++blocker) {
			Upscaling controller;
			controller.PublishSuccessfulApply(HealthyOwnedReceipt(controller));
			for (uint32_t frame = 100; frame < 103; ++frame)
				Pair(controller, frame, frame);
			Require(controller.submitStageVendorResumeReleaseCandidateCycle == 102, "Owned test did not form a candidate");
			switch (blocker) {
			case 0:
				controller.fidelityFX.fsrRelatchDrainProof.Invalidate();
				break;
			case 1:
				++controller.transitionSnapshot.metrics.current.retries;
				++controller.transitionSnapshot.metrics.current.backendDeferrals;
				break;
			case 2:
				controller.transitionSnapshot.memoryTrim.fenceFailures = 1;
				break;
			case 3:
				controller.physicalMutation.epoch = 7;
				break;
			}
			controller.ServiceSubmitStageVendorResumePromotion(103);
			Require(!controller.perfMode.vendorAllowed && !controller.submitStageVendorResumeProofDrivenRelease &&
						controller.submitStageVendorResumeReleaseCandidateCycle == 0 && controller.submitStageVendorResumeFrame == 100,
				"Final next-cycle service consumed changed evidence or restarted the guard");
		}
	}
}

int main()
{
	try {
		HealthyProofQualifiesBeforeFallbackWithoutEnablingCurrentCycle();
		ConservativeReleaseRetainsOriginalSixFrameGuard();
		MissingDuplicateAndMismatchedEyesCannotQualify();
		ProviderPreparationAndFidelityRevokeEarlyRelease();
		FinalNextCycleChecksRevokeInvalidatedCandidates();
		SuccessfulPublicationSealsReceiptAndKeepsBackendHistory();
		MixedRetriesAndIncompletePublicationKeepFallbackGuard();
		ActualReceiptAssemblyRejectsStaleAndBlockingEvidence();
		ConsumedOldDrainIdentityCannotStandInForTargetReadiness();
		OwnedDetachedDebtCanRemainWithoutReleasingResources();
		ReceiptMustRemainValidUntilFinalNextCycleRelease();
	} catch (const std::exception& error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
