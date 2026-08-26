#include "Features/Upscaling/VRRenderScaleDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "BuildProvenance.h"
#	include "Diagnostics/D3DTextureLifetimeTracker.h"
#	include "Diagnostics/VRPipelineDiagnostics.h"
#	include "Features/Upscaling.h"
#	include "Features/VR.h"
#	include "Globals.h"
#	include "ShaderCache.h"
#	include "State.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <atomic>
#	include <chrono>
#	include <cstddef>
#	include <format>
#	include <functional>
#	include <future>
#	include <memory>
#	include <optional>
#	include <stdexcept>
#	include <string>

namespace
{
	using json = nlohmann::json;

	constexpr auto kMainThreadTimeout = std::chrono::milliseconds(5000);
	constexpr std::size_t kDLSSDevBenchTraceDefaultReadLimit = 32;
	static_assert(kDLSSDevBenchTraceDefaultReadLimit <= Streamline::kDLSSDevBenchTraceCapacity);
	constexpr unsigned int kDevBenchToolExtensionRevision = 6;
	std::atomic_bool g_registered{ false };
	std::atomic_uint64_t g_nextDiagnosticTrimEpoch{ 1ull << 63 };

	const char* GetUpscaleMethodName(Upscaling::UpscaleMethod a_method)
	{
		switch (a_method) {
		case Upscaling::UpscaleMethod::kNONE:
			return "none";
		case Upscaling::UpscaleMethod::kTAA:
			return "taa";
		case Upscaling::UpscaleMethod::kFSR:
			return "fsr";
		case Upscaling::UpscaleMethod::kDLSS:
			return "dlss";
		default:
			return "unknown";
		}
	}

	const char* GetPhysicalPhaseName(
		Upscaling::VRRenderScalePhysicalPhase a_phase)
	{
		switch (a_phase) {
		case Upscaling::VRRenderScalePhysicalPhase::None:
			return "none";
		case Upscaling::VRRenderScalePhysicalPhase::Prepared:
			return "prepared";
		case Upscaling::VRRenderScalePhysicalPhase::CreatorEntered:
			return "creator_entered";
		case Upscaling::VRRenderScalePhysicalPhase::TableChanged:
			return "table_changed";
		case Upscaling::VRRenderScalePhysicalPhase::Reconciled:
			return "reconciled";
		case Upscaling::VRRenderScalePhysicalPhase::ContractPublished:
			return "contract_published";
		default:
			return "unknown";
		}
	}

	const char* GetPresentationPhaseName(
		Upscaling::VRRenderScalePresentationPhase a_phase)
	{
		switch (a_phase) {
		case Upscaling::VRRenderScalePresentationPhase::Idle:
			return "idle";
		case Upscaling::VRRenderScalePresentationPhase::Covered:
			return "covered";
		case Upscaling::VRRenderScalePresentationPhase::Repairing:
			return "repairing";
		case Upscaling::VRRenderScalePresentationPhase::AwaitingStereo:
			return "awaiting_stereo";
		case Upscaling::VRRenderScalePresentationPhase::StereoProven:
			return "stereo_proven";
		case Upscaling::VRRenderScalePresentationPhase::QuarantinedFailOpen:
			return "quarantined_fail_open";
		case Upscaling::VRRenderScalePresentationPhase::Released:
			return "released";
		default:
			return "unknown";
		}
	}

	const char* GetBackendName(Upscaling::VRRenderScaleBackendKind a_backend)
	{
		switch (a_backend) {
		case Upscaling::VRRenderScaleBackendKind::None:
			return "none";
		case Upscaling::VRRenderScaleBackendKind::DLSS:
			return "dlss";
		case Upscaling::VRRenderScaleBackendKind::FSRHost:
			return "fsr_host";
		case Upscaling::VRRenderScaleBackendKind::FSRRuntime:
			return "fsr_runtime";
		case Upscaling::VRRenderScaleBackendKind::FSR4Runtime:
			return "fsr4_runtime";
		default:
			return "unknown";
		}
	}

	const char* GetOriginName(Upscaling::VRUpscalingTransitionOrigin a_origin)
	{
		switch (a_origin) {
		case Upscaling::VRUpscalingTransitionOrigin::CSMenu:
			return "cs_menu";
		case Upscaling::VRUpscalingTransitionOrigin::VRAPI:
			return "vrapi";
		case Upscaling::VRUpscalingTransitionOrigin::RecoveryRelatch:
			return "recovery_relatch";
		case Upscaling::VRUpscalingTransitionOrigin::PostLoadSync:
			return "post_load_sync";
		default:
			return "unknown";
		}
	}

	const char* GetApplyDispositionName(Upscaling::UpscalingTransitionApplyDisposition a_disposition)
	{
		switch (a_disposition) {
		case Upscaling::UpscalingTransitionApplyDisposition::Rejected:
			return "rejected";
		case Upscaling::UpscalingTransitionApplyDisposition::NoChange:
			return "no_change";
		case Upscaling::UpscalingTransitionApplyDisposition::AppliedSynchronously:
			return "applied_synchronously";
		case Upscaling::UpscalingTransitionApplyDisposition::Queued:
			return "queued";
		case Upscaling::UpscalingTransitionApplyDisposition::Deferred:
			return "deferred";
		case Upscaling::UpscalingTransitionApplyDisposition::Coalesced:
			return "coalesced";
		default:
			return "unknown";
		}
	}

	const char* GetApplyRejectionName(Upscaling::UpscalingTransitionApplyRejection a_rejection)
	{
		switch (a_rejection) {
		case Upscaling::UpscalingTransitionApplyRejection::None:
			return "none";
		case Upscaling::UpscalingTransitionApplyRejection::OpenComposite:
			return "open_composite";
		case Upscaling::UpscalingTransitionApplyRejection::TransitionOwnership:
			return "transition_ownership";
		case Upscaling::UpscalingTransitionApplyRejection::QueueRejected:
			return "queue_rejected";
		default:
			return "unknown";
		}
	}

	json ProfileJson(const Upscaling::VRRenderScaleProfileSnapshot& a_profile)
	{
		return {
			{ "valid", a_profile.valid },
			{ "active", a_profile.active },
			{ "requestID", a_profile.requestID },
			{ "transitionEpoch", a_profile.transitionEpoch },
			{ "contractGeneration", a_profile.contractGeneration },
			{ "method", GetUpscaleMethodName(a_profile.method) },
			{ "qualityMode", a_profile.qualityMode },
			{ "dlssPreset", a_profile.dlssPreset },
			{ "renderScale", a_profile.renderScale },
			{ "renderScaleMode", a_profile.renderScaleModeEnabled },
			{ "fsr4RuntimeEnabled", a_profile.fsr4RuntimeEnabled },
			{ "displayEyeWidth", a_profile.displayEyeWidth },
			{ "displayEyeHeight", a_profile.displayEyeHeight },
			{ "renderEyeWidth", a_profile.renderEyeWidth },
			{ "renderEyeHeight", a_profile.renderEyeHeight },
			{ "queuedFrame", a_profile.queuedFrame },
			{ "origin", GetOriginName(a_profile.origin) },
			{ "backend", GetBackendName(a_profile.resources.backend) },
		};
	}

	json LifecycleJson(const Upscaling::VRVendorRuntimeLifecycleSnapshot& a_lifecycle)
	{
		return {
			{ "method", GetUpscaleMethodName(a_lifecycle.method) },
			{ "backend", GetBackendName(a_lifecycle.backend) },
			{ "phase", Upscaling::GetVRVendorRuntimeLifecyclePhaseName(a_lifecycle.phase) },
			{ "transitionEpoch", a_lifecycle.transitionEpoch },
			{ "requestedGeneration", a_lifecycle.requestedGeneration },
			{ "runtimeGeneration", a_lifecycle.runtimeGeneration },
			{ "stateFrame", a_lifecycle.stateFrame },
			{ "attempts", a_lifecycle.attempts },
			{ "deferrals", a_lifecycle.deferrals },
			{ "failures", a_lifecycle.failures },
			{ "resourcesPresent", a_lifecycle.resourcesPresent },
			{ "readyForContract", a_lifecycle.readyForContract },
		};
	}

	json VendorWorkGateJson(const Upscaling::VRVendorWorkGateSnapshot& a_gate)
	{
		const auto sourceNames = [](uint32_t a_mask) {
			json names = json::array();
			for (const auto source : VRVendorRelatchPolicy::kWorkGateSources) {
				if ((a_mask & VRVendorRelatchPolicy::ToMask(source)) != 0u)
					names.push_back(VRVendorRelatchPolicy::GetWorkGateSourceName(source));
			}
			return names;
		};

		return {
			{ "state", a_gate.state },
			{ "epoch", a_gate.stateEpoch },
			{ "active", a_gate.active },
			{ "activeMask", a_gate.activeMask },
			{ "effectiveLifecycleMask", a_gate.effectiveLifecycleMask },
			{ "gameEntryOwnerMask", a_gate.gameEntryOwnerMask },
			{ "sources", sourceNames(a_gate.activeMask) },
			{ "effectiveSources", sourceNames(a_gate.effectiveLifecycleMask) },
			{ "processStartup", a_gate.processStartup },
			{ "mainMenu", a_gate.mainMenu },
			{ "loadingMenu", a_gate.loadingMenu },
			{ "preLoadGame", a_gate.preLoadGame },
			{ "gameLoadNotification", a_gate.gameLoadNotification },
			{ "lifecycleGateRelevant", a_gate.lifecycleGateRelevant },
			{ "lifecycleMutationDeferred", a_gate.lifecycleMutationDeferred },
			{ "existingVendorDispatchReady", a_gate.existingVendorDispatchReady },
			{ "postLoadResetPending", a_gate.postLoadResetPending },
			{ "relatchQueued", a_gate.relatchQueued },
			{ "relatchInProgress", a_gate.relatchInProgress },
			{ "relatchFramePending", a_gate.relatchFramePending },
			{ "relatchPostLoadSettle", a_gate.relatchPostLoadSettle },
			{ "mainMenuActive", a_gate.mainMenuActive },
			{ "loadingPresentationActive", a_gate.loadingPresentationActive },
			{ "raceSexPresentationActive", a_gate.raceSexPresentationActive },
			{ "saveLoadProtectionActive", a_gate.saveLoadProtectionActive },
			{ "completedWorldFrame", a_gate.completedWorldFrame },
			{ "recoveryPending", a_gate.recoveryPending },
			{ "relatchPending", a_gate.relatchPending },
			{ "profileTransitionPending", a_gate.profileTransitionPending },
			{ "gameEntryReleaseReady", a_gate.gameEntryReleaseReady },
			{ "stabilizerSync", {
									{ "loadingSerial", a_gate.loadingTransitionSerial },
									{ "serialOpen", a_gate.loadingTransitionSerialOpen },
									{ "closeFrame", a_gate.loadingTransitionCloseFrame },
									{ "sourceCellFormID", a_gate.loadingTransitionSourceCellFormID },
									{ "destinationCellFormID", a_gate.loadingTransitionDestinationCellFormID },
									{ "destinationObservationWorldFrame", a_gate.loadingTransitionDestinationObservationWorldFrame },
									{ "lastResolvedCellFormID", a_gate.lastResolvedWorldCellFormID },
									{ "currentPlayerCellFormID", a_gate.currentPlayerCellFormID },
									{ "lastCompletedWorldRenderFrame", a_gate.lastCompletedWorldRenderFrame },
									{ "pendingSyncFrame", a_gate.stabilizerPendingSyncFrame },
									{ "resolvedSyncFrame", a_gate.stabilizerResolvedSyncFrame },
									{ "configuredUpscaleMethod", a_gate.configuredUpscaleMethod },
									{ "configuredQualityMode", a_gate.configuredQualityMode },
									{ "configuredRenderScaleMode", a_gate.configuredRenderScaleMode },
									{ "configuredDLSSPreset", a_gate.configuredDLSSPreset },
								} },
		};
	}

	json FsrDispatchJson(
		const Upscaling::VRRenderScaleTransitionSnapshot& a_controller,
		bool a_shaderCompilationActive)
	{
		const auto* desired = [&]() -> const Upscaling::VRRenderScaleProfileSnapshot* {
			if (a_controller.requested.valid)
				return &a_controller.requested;
			if (a_controller.applying.valid)
				return &a_controller.applying;
			if (a_controller.stable.valid)
				return &a_controller.stable;
			if (a_controller.applied.valid)
				return &a_controller.applied;
			return nullptr;
		}();
		const auto* authoritative = [&]() -> const Upscaling::VRRenderScaleProfileSnapshot* {
			if (a_controller.stable.valid)
				return &a_controller.stable;
			if (a_controller.applied.valid)
				return &a_controller.applied;
			if (a_controller.applying.valid)
				return &a_controller.applying;
			return nullptr;
		}();

		const bool desiredFsr = desired && desired->method == Upscaling::UpscaleMethod::kFSR;
		const bool authoritativeFsr = authoritative && authoritative->method == Upscaling::UpscaleMethod::kFSR;
		const bool backendConverged =
			desiredFsr && authoritativeFsr &&
			desired->active == authoritative->active &&
			desired->renderScaleModeEnabled == authoritative->renderScaleModeEnabled &&
			desired->fsr4RuntimeEnabled == authoritative->fsr4RuntimeEnabled &&
			desired->resources.backend == authoritative->resources.backend &&
			desired->renderEyeWidth == authoritative->renderEyeWidth &&
			desired->renderEyeHeight == authoritative->renderEyeHeight &&
			desired->displayEyeWidth == authoritative->displayEyeWidth &&
			desired->displayEyeHeight == authoritative->displayEyeHeight;
		const auto& leftDispatch = a_controller.fidelity.eyes[0];
		const auto& rightDispatch = a_controller.fidelity.eyes[1];
		const bool leftDispatchValid =
			a_controller.fidelity.active &&
			a_controller.fidelity.method == Upscaling::UpscaleMethod::kFSR &&
			(a_controller.fidelity.evaluationEyeMask & 0x1u) != 0 &&
			leftDispatch.fsrDispatchPathValid &&
			leftDispatch.fsrDispatchSerial != 0;
		const bool rightDispatchValid =
			a_controller.fidelity.active &&
			a_controller.fidelity.method == Upscaling::UpscaleMethod::kFSR &&
			(a_controller.fidelity.evaluationEyeMask & 0x2u) != 0 &&
			rightDispatch.fsrDispatchPathValid &&
			rightDispatch.fsrDispatchSerial != 0;
		json actualDispatchEyes = json::array();
		for (std::size_t eyeIndex = 0; eyeIndex < a_controller.fidelity.eyes.size(); ++eyeIndex) {
			const auto& eye = a_controller.fidelity.eyes[eyeIndex];
			const bool valid = eyeIndex == 0 ? leftDispatchValid : rightDispatchValid;
			actualDispatchEyes.push_back({
				{ "valid", valid },
				{ "frame", valid ? eye.frame : 0u },
				{ "backend", valid ? GetBackendName(eye.fsrDispatchBackend) : "none" },
				{ "runtimeFallback", valid && eye.fsrRuntimeFallback },
				{ "serial", valid ? eye.fsrDispatchSerial : 0u },
			});
		}
		const bool actualDispatchBothEyesValid =
			a_controller.fidelity.bothEyesValid &&
			leftDispatchValid &&
			rightDispatchValid &&
			leftDispatch.frame == rightDispatch.frame &&
			leftDispatch.fsrDispatchSerial != rightDispatch.fsrDispatchSerial;
		const bool actualDispatchBackendConverged =
			actualDispatchBothEyesValid &&
			leftDispatch.fsrDispatchBackend == rightDispatch.fsrDispatchBackend;
		const bool actualRuntimeFallbackObserved =
			(leftDispatchValid && leftDispatch.fsrRuntimeFallback) ||
			(rightDispatchValid && rightDispatch.fsrRuntimeFallback);

		return {
			{ "desiredValid", desired != nullptr },
			{ "desiredMethod", desired ? GetUpscaleMethodName(desired->method) : "none" },
			{ "desiredActive", desired && desired->active },
			{ "desiredFsr4RuntimeEnabled", desiredFsr && desired->fsr4RuntimeEnabled },
			{ "desiredBackend", desired ? GetBackendName(desired->resources.backend) : "none" },
			{ "authoritativeValid", authoritative != nullptr },
			{ "authoritativeMethod", authoritative ? GetUpscaleMethodName(authoritative->method) : "none" },
			{ "authoritativeActive", authoritative && authoritative->active },
			{ "authoritativeFsr4RuntimeEnabled", authoritativeFsr && authoritative->fsr4RuntimeEnabled },
			{ "authoritativeBackend", authoritative ? GetBackendName(authoritative->resources.backend) : "none" },
			{ "fsrBackendConverged", backendConverged },
			{ "evaluationActive", a_controller.fidelity.active },
			{ "evaluationMethod", GetUpscaleMethodName(a_controller.fidelity.method) },
			{ "expectedEvaluationBackend", GetBackendName(a_controller.fidelity.backend) },
			{ "evaluationTransitionEpoch", a_controller.fidelity.transitionEpoch },
			{ "evaluationContractGeneration", a_controller.fidelity.contractGeneration },
			{ "evaluationBothEyesValid", a_controller.fidelity.bothEyesValid },
			{ "actualDispatchBothEyesValid", actualDispatchBothEyesValid },
			{ "actualDispatchBackendConverged", actualDispatchBackendConverged },
			{ "actualDispatchFrame", actualDispatchBothEyesValid ? leftDispatch.frame : 0u },
			{ "actualDispatchBackend", actualDispatchBackendConverged ? GetBackendName(leftDispatch.fsrDispatchBackend) : (actualDispatchBothEyesValid ? "mixed" : "none") },
			{ "actualRuntimeFallbackObserved", actualRuntimeFallbackObserved },
			{ "actualDispatchEyes", std::move(actualDispatchEyes) },
			{ "contractResourcesPresent", a_controller.fsrLifecycle.resourcesPresent },
			{ "contractReady", a_controller.fsrLifecycle.readyForContract },
			{ "contractLifecyclePhase", Upscaling::GetVRVendorRuntimeLifecyclePhaseName(a_controller.fsrLifecycle.phase) },
			{ "contractLifecycleFailures", a_controller.fsrLifecycle.failures },
			{ "shaderCompilationActive", a_shaderCompilationActive },
		};
	}

	json CPUPerformanceJson(Upscaling& a_upscaling)
	{
		using Counter = Upscaling::VRRenderScaleCPUPerformanceCounter;
		const auto counters =
			a_upscaling.GetVRRenderScaleCPUPerformanceSnapshot();
		const auto value = [&](Counter a_counter) {
			return counters[static_cast<std::size_t>(a_counter)];
		};
		const auto microseconds = [](uint64_t a_nanoseconds) {
			return static_cast<double>(a_nanoseconds) / 1000.0;
		};
		const auto averageMicroseconds = [&](Counter a_total, Counter a_count) {
			const uint64_t count = value(a_count);
			return count == 0 ?
				0.0 :
				microseconds(value(a_total)) / static_cast<double>(count);
		};
		const uint32_t frame =
			globals::state ? globals::state->frameCount : 0u;
		const uint64_t startFrame = value(Counter::WindowStartFrame);

		return {
			{ "schemaVersion", 1 },
			{ "devBenchOnly", true },
			{ "active", a_upscaling.IsVRRenderScaleCPUPerformanceTelemetryActive() },
			{ "window", {
							{ "initialized", startFrame != 0 },
							{ "startFrame", startFrame },
							{ "currentFrame", frame },
							{ "elapsedFrames", startFrame != 0 && frame >= startFrame ? frame - startFrame : 0u },
						} },
			{ "generationResourceValidation", {
										  { "fullValidations", value(Counter::ResourceFullValidations) },
										  { "contractPublishes", value(Counter::ResourceContractPublishes) },
										  { "contractInvalidations", value(Counter::ResourceContractInvalidations) },
										  { "stableChecks", value(Counter::ResourceContractStableChecks) },
										  { "stableHits", value(Counter::ResourceContractStableHits) },
										  { "stableMisses", value(Counter::ResourceContractStableMisses) },
									  } },
			{ "compactPresentationContract", {
										{ "publishes", value(Counter::HotContractPublishes) },
										{ "reuses", value(Counter::HotContractReuses) },
									} },
			{ "stateProportionalSafety", {
									 { "promotion", { { "fastSkips", value(Counter::PromotionFastSkips) }, { "candidates", value(Counter::PromotionCandidates) } } },
									 { "boundsGuard", { { "fastSkips", value(Counter::BoundsGuardFastSkips) }, { "candidates", value(Counter::BoundsGuardCandidates) } } },
									 { "deferredRecovery", { { "fastSkips", value(Counter::DeferredRecoveryFastSkips) }, { "candidates", value(Counter::DeferredRecoveryCandidates) } } },
									 { "nativeGuard", { { "fastSkips", value(Counter::NativeGuardFastSkips) }, { "candidates", value(Counter::NativeGuardCandidates) } } },
									 { "engineRetirement", { { "fastSkips", value(Counter::RetirementFastSkips) }, { "services", value(Counter::RetirementServices) } } },
									 { "memoryTrim", { { "fastSkips", value(Counter::TrimFastSkips) }, { "services", value(Counter::TrimServices) } } },
									 { "memoryTelemetry", { { "fastSkips", value(Counter::MemoryTelemetryFastSkips) }, { "candidates", value(Counter::MemoryTelemetryCandidates) } } },
									 { "postMutationGuard", { { "fastSkips", value(Counter::PostMutationGuardFastSkips) }, { "services", value(Counter::PostMutationGuardServices) } } },
								 } },
			{ "strongStereoPacket", {
								  { "fastSkips", value(Counter::PresentationPacketFastSkips) },
								  { "captures", value(Counter::PresentationPacketCaptures) },
								  { "cycleReuses", value(Counter::PresentationPacketCycleReuses) },
								  { "lifetimeReuses", value(Counter::PresentationLifetimeReuses) },
								  { "lifetimeRebuilds", value(Counter::PresentationLifetimeRebuilds) },
								  { "invalidations", value(Counter::PresentationPacketInvalidations) },
								  { "commitValidations", value(Counter::PresentationCommitValidations) },
								  { "commitAccepts", value(Counter::PresentationCommitAccepts) },
								  { "commitRejects", value(Counter::PresentationCommitRejects) },
								  { "queueWaitAverageMicroseconds", averageMicroseconds(Counter::PresentationQueueWaitTotalNanoseconds, Counter::PresentationPacketCaptures) },
								  { "queueWaitMaximumMicroseconds", microseconds(value(Counter::PresentationQueueWaitMaximumNanoseconds)) },
								  { "queueHoldAverageMicroseconds", averageMicroseconds(Counter::PresentationQueueHoldTotalNanoseconds, Counter::PresentationPacketCaptures) },
								  { "queueHoldMaximumMicroseconds", microseconds(value(Counter::PresentationQueueHoldMaximumNanoseconds)) },
							  } },
		};
	}

	json BuildStatus(Upscaling& a_upscaling)
	{
		const auto controller = a_upscaling.GetVRRenderScaleTransitionSnapshot();
		const auto session = a_upscaling.GetVRRenderScaleStressSessionSnapshot();
		const auto vendorWorkGate = a_upscaling.GetVRVendorWorkGateSnapshot();
		const uint32_t frame = globals::state ? globals::state->frameCount : 0u;

		json eyes = json::array();
		for (const auto& eye : controller.fidelity.eyes) {
			eyes.push_back({
				{ "frame", eye.frame },
				{ "generation", eye.generation },
				{ "inputWidth", eye.inputWidth },
				{ "inputHeight", eye.inputHeight },
				{ "outputWidth", eye.outputWidth },
				{ "outputHeight", eye.outputHeight },
				{ "evaluated", eye.evaluated },
				{ "valid", eye.valid },
			});
		}
		json presentationEyes = json::array();
		for (const auto& eye : controller.presentation.eyes) {
			presentationEyes.push_back({
				{ "valid", eye.valid },
				{ "path", Upscaling::GetVRRenderScalePresentationPathName(eye.path) },
				{ "frame", eye.frame },
				{ "transitionEpoch", eye.transitionEpoch },
				{ "contractGeneration", eye.contractGeneration },
				{ "method", GetUpscaleMethodName(eye.method) },
				{ "inputWidth", eye.inputWidth },
				{ "inputHeight", eye.inputHeight },
				{ "expectedInputWidth", eye.expectedInputWidth },
				{ "expectedInputHeight", eye.expectedInputHeight },
				{ "outputWidth", eye.outputWidth },
				{ "outputHeight", eye.outputHeight },
				{ "consecutiveFrames", eye.consecutiveFrames },
				{ "loadingOrMenuContext", eye.loadingOrMenuContext },
				{ "transitionCooldown", eye.transitionCooldown },
			});
		}
		const auto observationsSinceStart = [](uint64_t a_current, uint64_t a_baseline) {
			return a_current >= a_baseline ? a_current - a_baseline : a_current;
		};

		return {
			{ "frame", frame },
			{ "modeStatus", Upscaling::GetVRRenderScaleModeStatusName(a_upscaling.GetVRRenderScaleModeStatus()) },
			{ "cpuPerformance", CPUPerformanceJson(a_upscaling) },
			{ "pipelineDiagnostics", {
										 { "configuredForNextStartup", a_upscaling.settings.pipelineDiagnostics },
										 { "configuredStructuredForNextStartup", a_upscaling.settings.pipelineDiagnosticsStructured },
										 { "capture", VRPipelineDiagnostics::GetStatusSnapshot() },
									 } },
			{ "fsrDispatch", FsrDispatchJson(controller, globals::shaderCache && globals::shaderCache->IsCompiling()) },
			{ "vendorWorkGate", VendorWorkGateJson(vendorWorkGate) },
			{ "loadPresentationProbe", a_upscaling.BuildVRLoadPresentationProbeStatus() },
			{ "hmdMaskDiagnostics", a_upscaling.BuildVRHMDMaskDiagnosticsStatus() },
			{ "session", {
							 { "id", session.sessionID },
							 { "active", session.active },
							 { "startFrame", session.startFrame },
							 { "endFrame", session.endFrame },
							 { "retainedEvents", session.count },
							 { "overwrittenEvents", session.overwrittenEvents },
							 { "coalescedDuplicateCount", session.coalescedDuplicateCount },
						 } },
			{ "controller", {
								{ "state", Upscaling::GetVRRenderScaleTransitionStateName(controller.state) },
								{ "physicalPhase", GetPhysicalPhaseName(controller.physicalPhase) },
								{ "physicalPhaseValue", static_cast<uint32_t>(controller.physicalPhase) },
								{ "presentationPhase", GetPresentationPhaseName(controller.presentationPhase) },
								{ "presentationPhaseValue", static_cast<uint32_t>(controller.presentationPhase) },
								{ "desiredOwner", {
													  { "transitionEpoch", controller.desiredOwner.transitionEpoch },
													  { "contractGeneration", controller.desiredOwner.contractGeneration },
													  { "loadingSerial", controller.desiredOwner.loadingSerial },
												  } },
								{ "physicalOwner", {
													   { "transitionEpoch", controller.physicalOwner.transitionEpoch },
													   { "contractGeneration", controller.physicalOwner.contractGeneration },
													   { "loadingSerial", controller.physicalOwner.loadingSerial },
												   } },
								{ "presentationOwner", {
														   { "transitionEpoch", controller.presentationOwner.transitionEpoch },
														   { "contractGeneration", controller.presentationOwner.contractGeneration },
														   { "loadingSerial", controller.presentationOwner.loadingSerial },
													   } },
								{ "targetEpoch", controller.targetEpoch },
								{ "revision", controller.revision },
								{ "unresolvedPhysicalMutationEpoch", a_upscaling.vrRenderScaleUnresolvedPhysicalMutationEpoch.load(std::memory_order_acquire) },
								{ "unresolvedPhysicalMutationStartTickMs", a_upscaling.vrRenderScaleUnresolvedPhysicalMutationStartTickMs.load(std::memory_order_acquire) },
								{ "postMutationEmergencyAttemptConsumed", a_upscaling.vrRenderScalePostMutationEmergencyAttemptConsumed.load(std::memory_order_acquire) },
								{ "emergencyRecoveryRequested", a_upscaling.vrRenderScaleEmergencyRecoveryRequested.load(std::memory_order_acquire) },
								{ "terminalFailureSignaled", a_upscaling.vrRenderScaleTerminalFailureSignaled.load(std::memory_order_acquire) },
								{ "terminalDeviceLossSignaled", a_upscaling.vrRenderScaleTerminalFailureSignaled.load(std::memory_order_acquire) && a_upscaling.submitStageDeviceLost.load(std::memory_order_acquire) },
								{ "requested", ProfileJson(controller.requested) },
								{ "applying", ProfileJson(controller.applying) },
								{ "applied", ProfileJson(controller.applied) },
								{ "stable", ProfileJson(controller.stable) },
								{ "memory", {
												{ "valid", controller.memory.valid },
												{ "sampleFrame", controller.memory.sampleFrame },
												{ "usageBytes", controller.memory.currentUsageBytes },
												{ "budgetBytes", controller.memory.budgetBytes },
												{ "headroomBytes", controller.memory.headroomBytes },
												{ "usageRatio", controller.memory.usageRatio },
												{ "systemCommitValid", controller.memory.systemCommitValid },
												{ "systemCommitBytes", controller.memory.systemCommitBytes },
												{ "systemCommitLimitBytes", controller.memory.systemCommitLimitBytes },
												{ "systemCommitHeadroomBytes", controller.memory.systemCommitHeadroomBytes },
												{ "systemCommitRatio", controller.memory.systemCommitRatio },
												{ "processPrivateUsageValid", controller.memory.processPrivateUsageValid },
												{ "processPrivateUsageBytes", controller.memory.processPrivateUsageBytes },
												{ "pressure", Upscaling::GetVRRenderScaleMemoryPressureName(controller.memory.pressure) },
												{ "recoverySamples", controller.memory.recoverySamples },
											} },
								{ "resourcePlan", {
													  { "valid", controller.relatchPlan.valid },
													  { "transitionEpoch", controller.relatchPlan.transitionEpoch },
													  { "previousVendorMethod", GetUpscaleMethodName(controller.relatchPlan.previousVendorMethod) },
													  { "memoryPressure", Upscaling::GetVRRenderScaleMemoryPressureName(controller.relatchPlan.memoryPressure) },
													  { "estimatedAdditionalBytes", controller.relatchPlan.estimatedAdditionalBytes },
													  { "projectedAdditionalBytes", controller.relatchPlan.projectedAdditionalBytes },
													  { "projectedUsageBytes", controller.relatchPlan.projectedUsageBytes },
													  { "admissionUsageLimitBytes", controller.relatchPlan.admissionUsageLimitBytes },
													  { "postTrimAdmissionUsageLimitBytes", controller.relatchPlan.postTrimAdmissionUsageLimitBytes },
													  { "projectedSystemCommitAdditionalBytes", controller.relatchPlan.projectedSystemCommitAdditionalBytes },
													  { "projectedSystemCommitBytes", controller.relatchPlan.projectedSystemCommitBytes },
													  { "systemCommitAdmissionPolicy", Upscaling::GetVRRenderScaleSystemCommitAdmissionPolicyName(controller.relatchPlan.systemCommitAdmissionPolicy) },
													  { "systemCommitLimitBytes", controller.relatchPlan.systemCommitLimitBytes },
													  { "systemCommitReserveBytes", controller.relatchPlan.systemCommitReserveBytes },
													  { "systemCommitAdmissionLimitBytes", controller.relatchPlan.systemCommitAdmissionLimitBytes },
													  { "pressureCleanupRequired", controller.relatchPlan.pressureCleanupRequired },
													  { "projectedResidencyGuardActive", controller.relatchPlan.projectedResidencyGuardActive },
													  { "projectedResidencyPostTrimRelaxed", controller.relatchPlan.projectedResidencyPostTrimRelaxed },
													  { "projectedResidencyDeferred", controller.relatchPlan.projectedResidencyDeferred },
													  { "systemCommitGuardActive", controller.relatchPlan.systemCommitGuardActive },
													  { "doorHandoffHardReserveOnly", controller.relatchPlan.doorHandoffHardReserveOnly },
													  { "systemCommitDeferred", controller.relatchPlan.systemCommitDeferred },
													  { "pressureDeferred", controller.relatchPlan.pressureDeferred },
													  { "emergencySystemCommitGuardActive", controller.relatchPlan.emergencySystemCommitGuardActive },
													  { "emergencySystemCommitProjectionMultiplier", controller.relatchPlan.emergencySystemCommitProjectionMultiplier },
													  { "emergencySystemCommitMinimumProjectionBytes", controller.relatchPlan.emergencySystemCommitMinimumProjectionBytes },
													  { "emergencySystemCommitProjectionValid", controller.relatchPlan.emergencySystemCommitProjectionValid },
													  { "emergencyProjectedSystemCommitAdditionalBytes", controller.relatchPlan.emergencyProjectedSystemCommitAdditionalBytes },
													  { "emergencyProjectedSystemCommitBytes", controller.relatchPlan.emergencyProjectedSystemCommitBytes },
													  { "emergencySystemCommitReserveBytes", controller.relatchPlan.emergencySystemCommitReserveBytes },
													  { "emergencySystemCommitAdmissionLimitBytes", controller.relatchPlan.emergencySystemCommitAdmissionLimitBytes },
													  { "emergencySystemCommitSafe", controller.relatchPlan.emergencySystemCommitSafe },
												  } },
								{ "memoryTrim", {
													{ "pending", controller.memoryTrim.pending },
													{ "reason", Upscaling::GetVRRenderScaleMemoryTrimReasonName(controller.memoryTrim.reason) },
													{ "ownerEpoch", controller.memoryTrim.ownerEpoch },
													{ "requestedFrame", controller.memoryTrim.requestedFrame },
													{ "completedFrame", controller.memoryTrim.completedFrame },
													{ "fenceFailures", controller.memoryTrim.fenceFailures },
													{ "completedCount", controller.memoryTrim.completedCount },
													{ "failures", controller.memoryTrim.failures },
													{ "lastSucceeded", controller.memoryTrim.lastSucceeded },
													{ "preRecreateDrainCount", controller.memoryTrim.preRecreateDrainCount },
													{ "preRecreateDrainFailures", controller.memoryTrim.preRecreateDrainFailures },
													{ "lastOfferedResourceCount", controller.memoryTrim.lastOfferedResourceCount },
													{ "lastOfferUsedDecommit", controller.memoryTrim.lastOfferUsedDecommit },
												} },
								{ "retirement", {
													{ "pendingSets", controller.retirement.pendingSets },
													{ "fencePending", controller.retirement.fencePending },
													{ "capacityBlocked", controller.retirement.capacityBlocked },
													{ "nextCleanupFrame", controller.retirement.nextCleanupFrame },
												} },
								{ "engineTargetRetirement", {
																{ "supported", controller.engineTargetRetirement.supported },
																{ "pending", controller.engineTargetRetirement.pending },
																{ "oldestEpoch", controller.engineTargetRetirement.oldestEpoch },
																{ "newestEpoch", controller.engineTargetRetirement.newestEpoch },
																{ "pendingGenerations", controller.engineTargetRetirement.pendingGenerations },
																{ "capturedPointerCount", controller.engineTargetRetirement.capturedPointerCount },
																{ "aliasedPointerCount", controller.engineTargetRetirement.aliasedPointerCount },
																{ "provenPointerCount", controller.engineTargetRetirement.provenPointerCount },
																{ "retainedUnprovenPointerCount", controller.engineTargetRetirement.retainedUnprovenPointerCount },
																{ "replacedPointerCount", controller.engineTargetRetirement.replacedPointerCount },
																{ "restoredPointerCount", controller.engineTargetRetirement.restoredPointerCount },
																{ "pendingReleaseCount", controller.engineTargetRetirement.pendingReleaseCount },
																{ "lastReleasedPointerCount", controller.engineTargetRetirement.lastReleasedPointerCount },
																{ "totalReleasedPointerCount", controller.engineTargetRetirement.totalReleasedPointerCount },
																{ "completedCount", controller.engineTargetRetirement.completedCount },
																{ "fenceFailures", controller.engineTargetRetirement.fenceFailures },
																{ "fencePending", controller.engineTargetRetirement.fencePending },
																{ "capacityBlocked", controller.engineTargetRetirement.capacityBlocked },
															} },
								{ "postLoadRecovery", {
														  { "active", controller.postLoadRecovery.active },
														  { "recoveryEpoch", controller.postLoadRecovery.recoveryEpoch },
														  { "transitionEpoch", controller.postLoadRecovery.transitionEpoch },
														  { "loadingSerial", controller.postLoadRecovery.loadingSerial },
														  { "settledSamples", controller.postLoadRecovery.settledSamples },
														  { "admissionWaitStartFrame", controller.postLoadRecovery.admissionWaitStartFrame },
														  { "firstSettledFrame", controller.postLoadRecovery.firstSettledFrame },
														  { "lastSettledFrame", controller.postLoadRecovery.lastSettledFrame },
														  { "settleDeadlineExpired", controller.postLoadRecovery.settleDeadlineExpired },
														  { "settleTimeoutUsed", controller.postLoadRecovery.settleTimeoutUsed },
														  { "timedAttemptConsumed", controller.postLoadRecovery.timedAttemptConsumed },
														  { "timedAttemptInProgress", controller.postLoadRecovery.timedAttemptInProgress },
														  { "timedAttemptStartTickMs", controller.postLoadRecovery.timedAttemptStartTickMs },
														  { "vendorTeardownPhase", std::string{ magic_enum::enum_name(controller.postLoadRecovery.vendorTeardownPhase) } },
														  { "vendorTeardownFallbackRequested", controller.postLoadRecovery.vendorTeardownFallbackRequested },
														  { "engineTargetCreateEntered", controller.postLoadRecovery.engineTargetCreateEntered },
														  { "baselineUsageBytes", controller.postLoadRecovery.baselineUsageBytes },
														  { "peakUsageBytes", controller.postLoadRecovery.peakUsageBytes },
														  { "baselineSystemCommitBytes", controller.postLoadRecovery.baselineSystemCommitBytes },
														  { "peakSystemCommitBytes", controller.postLoadRecovery.peakSystemCommitBytes },
														  { "baselineProcessPrivateUsageBytes", controller.postLoadRecovery.baselineProcessPrivateUsageBytes },
														  { "peakProcessPrivateUsageBytes", controller.postLoadRecovery.peakProcessPrivateUsageBytes },
														  { "peakPressure", Upscaling::GetVRRenderScaleMemoryPressureName(controller.postLoadRecovery.peakPressure) },
														  { "cleanupDrained", controller.postLoadRecovery.cleanupDrained },
														  { "trimArmed", controller.postLoadRecovery.trimArmed },
														  { "trimCompleted", controller.postLoadRecovery.trimCompleted },
														  { "trimSucceeded", controller.postLoadRecovery.trimSucceeded },
														  { "relatchAdmitted", controller.postLoadRecovery.relatchAdmitted },
													  } },
								{ "fidelity", {
												  { "active", controller.fidelity.active },
												  { "transitionEpoch", controller.fidelity.transitionEpoch },
												  { "contractGeneration", controller.fidelity.contractGeneration },
												  { "method", GetUpscaleMethodName(controller.fidelity.method) },
												  { "backend", GetBackendName(controller.fidelity.backend) },
												  { "bothEyesValid", controller.fidelity.bothEyesValid },
												  { "evaluationEyeMask", controller.fidelity.evaluationEyeMask },
												  { "invariantEyeMask", controller.fidelity.invariantEyeMask },
												  { "lastMismatchMask", controller.fidelity.lastMismatchMask },
												  { "mismatchCount", controller.fidelity.mismatchCount },
												  { "eyes", std::move(eyes) },
											  } },
								{ "presentation", {
													  { "lastBothEyesVendorFrame", controller.presentation.lastBothEyesVendorFrame },
													  { "consecutiveBothEyesVendorFrames", controller.presentation.consecutiveBothEyesVendorFrames },
													  { "lastFallbackFrame", controller.presentation.lastFallbackFrame },
													  { "maximumConsecutivePresentationStretchFrames", controller.presentation.maximumConsecutivePresentationStretchFrames },
													  { "vendorEvaluatedEyeObservations", controller.presentation.vendorEvaluatedEyeObservations },
													  { "presentationStretchEyeObservations", controller.presentation.presentationStretchEyeObservations },
													  { "vendorFailureStretchEyeObservations", controller.presentation.vendorFailureStretchEyeObservations },
													  { "boundsMismatchOriginalFallbackEyeObservations", controller.presentation.boundsMismatchOriginalFallbackEyeObservations },
													  { "sessionVendorEvaluatedEyeObservations", observationsSinceStart(controller.presentation.vendorEvaluatedEyeObservations, session.baselineVendorEvaluatedEyeObservations) },
													  { "sessionPresentationStretchEyeObservations", observationsSinceStart(controller.presentation.presentationStretchEyeObservations, session.baselinePresentationStretchEyeObservations) },
													  { "sessionVendorFailureStretchEyeObservations", observationsSinceStart(controller.presentation.vendorFailureStretchEyeObservations, session.baselineVendorFailureStretchEyeObservations) },
													  { "sessionBoundsMismatchOriginalFallbackEyeObservations", observationsSinceStart(controller.presentation.boundsMismatchOriginalFallbackEyeObservations, session.baselineBoundsMismatchOriginalFallbackEyeObservations) },
													  { "eyes", std::move(presentationEyes) },
												  } },
								{ "currentMetrics", {
														{ "valid", controller.metrics.current.valid },
														{ "transitionEpoch", controller.metrics.current.transitionEpoch },
														{ "retries", controller.metrics.current.retries },
														{ "pressureDeferrals", controller.metrics.current.pressureDeferrals },
														{ "retirementDeferrals", controller.metrics.current.retirementDeferrals },
														{ "backendDeferrals", controller.metrics.current.backendDeferrals },
														{ "failures", controller.metrics.current.failures },
														{ "fidelityMismatches", controller.metrics.current.fidelityMismatches },
														{ "memoryTrimCount", controller.metrics.current.memoryTrimCount },
														{ "memoryTrimFailures", controller.metrics.current.memoryTrimFailures },
														{ "memoryPreRecreateDrainCount", controller.metrics.current.memoryPreRecreateDrainCount },
														{ "memoryPreRecreateDrainFailures", controller.metrics.current.memoryPreRecreateDrainFailures },
													} },
								{ "dlssLifecycle", LifecycleJson(controller.dlssLifecycle) },
								{ "fsrLifecycle", LifecycleJson(controller.fsrLifecycle) },
							} },
		};
	}

	json BuildGPUPerformanceStatus(Upscaling& a_upscaling)
	{
		using Counter = Upscaling::VRRenderScaleGPUPerformanceCounter;
		const auto counters = a_upscaling.GetVRRenderScaleGPUPerformanceSnapshot();
		const auto value = [&](Counter a_counter) {
			return counters[static_cast<std::size_t>(a_counter)];
		};
		const uint64_t activeInputPixels = value(Counter::FSRActiveInputPixels);
		const uint64_t avoidedInputPixels = value(Counter::FSRAvoidedInputPixels);
		const uint64_t potentialInputPixels = activeInputPixels + avoidedInputPixels;
		const uint64_t croppedHistoryPixels = value(Counter::PeripheryTAAHistoryPixels);
		const uint64_t fullHistoryPixels = value(Counter::PeripheryTAAFullEyePixels);
		const uint32_t frame = globals::state ? globals::state->frameCount : 0u;
		const uint64_t startFrame = value(Counter::WindowStartFrame);

		return {
			{ "active", a_upscaling.IsVRRenderScaleGPUPerformanceTelemetryActive() },
			{ "startFrame", startFrame },
			{ "currentFrame", frame },
			{ "observedFrames", frame >= startFrame ? frame - startFrame : 0u },
			{ "item5ActiveFSRCopies", {
				{ "copyCalls", value(Counter::FSRActiveInputCopyCalls) },
				{ "activePixels", activeInputPixels },
				{ "avoidedPixels", avoidedInputPixels },
				{ "activePixelRatio", potentialInputPixels ? static_cast<double>(activeInputPixels) / potentialInputPixels : 0.0 },
			} },
			{ "item6RuntimeFSRStereo", {
				{ "batchAttempts", value(Counter::RuntimeFSRStereoBatchAttempts) },
				{ "batchReuses", value(Counter::RuntimeFSRStereoBatchReuses) },
				{ "batchSuccesses", value(Counter::RuntimeFSRStereoBatchSuccesses) },
				{ "batchNotHandled", value(Counter::RuntimeFSRStereoBatchNotHandled) },
				{ "batchFailures", value(Counter::RuntimeFSRStereoBatchFailures) },
				{ "interopTransactionsAvoided", value(Counter::RuntimeFSRStereoBatchSuccesses) },
			} },
			{ "item7EarlyHAM", {
				{ "protectedPostProcessInputs", value(Counter::EarlyHAMProtectedInputs) },
				{ "directOutputSkips", value(Counter::EarlyHAMDirectOutputSkips) },
				{ "executedClears", value(Counter::EarlyHAMClearExecutions) },
			} },
			{ "item8MirrorWriteback", {
				{ "consumerEyeObservations", value(Counter::MirrorConsumerRequests) },
				{ "skippedEyeObservations", value(Counter::MirrorConsumerSkips) },
				{ "copyPairs", value(Counter::MirrorCopyPairs) },
				{ "blitPairs", value(Counter::MirrorBlitPairs) },
			} },
			{ "item9SpatialComposite", {
				{ "dispatches", value(Counter::SpatialCompositeDispatches) },
				{ "fullEyePixels", value(Counter::SpatialCompositePixels) },
				{ "centerRectPixels", value(Counter::SpatialCenterPixels) },
			} },
			{ "item10PeripheryTAAHistory", {
				{ "dispatches", value(Counter::PeripheryTAAHistoryDispatches) },
				{ "croppedPixels", croppedHistoryPixels },
				{ "fullEyePixels", fullHistoryPixels },
				{ "pixelRatio", fullHistoryPixels ? static_cast<double>(croppedHistoryPixels) / fullHistoryPixels : 0.0 },
				{ "avoidedPixelRatio", fullHistoryPixels ? 1.0 - static_cast<double>(croppedHistoryPixels) / fullHistoryPixels : 0.0 },
			} },
		};
	}

	const char* GetDLSSDevBenchTraceStageName(Streamline::DLSSDevBenchTraceStage a_stage)
	{
		switch (a_stage) {
		case Streamline::DLSSDevBenchTraceStage::ConstantsCacheReuse:
			return "constants_cache_reuse";
		case Streamline::DLSSDevBenchTraceStage::SetConstants:
			return "set_constants";
		case Streamline::DLSSDevBenchTraceStage::Evaluate:
			return "evaluate";
		default:
			return "unknown";
		}
	}

	json DLSSDevBenchChangedFieldsJson(uint64_t a_mask)
	{
		json fields = json::array();
		using Field = Streamline::DLSSDevBenchTraceSignatureField;
		for (uint8_t index = 0; index < static_cast<uint8_t>(Field::Count); ++index) {
			if ((a_mask & (uint64_t{ 1 } << index)) != 0)
				fields.push_back(std::string(magic_enum::enum_name(static_cast<Field>(index))));
		}
		return fields;
	}

	std::string DLSSDevBenchHex64(uint64_t a_value)
	{
		return std::format("0x{:016X}", a_value);
	}

	json DLSSDevBenchTraceSignatureJson(const Streamline::DLSSDevBenchTraceSignature& a_signature)
	{
		const auto quantized = [](int32_t a_value) {
			return json{
				{ "value", static_cast<double>(a_value) / 1000000.0 },
				{ "quantized", a_value },
			};
		};
		return {
			{ "traceSessionID", a_signature.traceSessionID },
			{ "frame", a_signature.frame },
			{ "frameToken", a_signature.frameToken },
			{ "frameTokenAddress", DLSSDevBenchHex64(a_signature.frameTokenAddress) },
			{ "requestedViewport", a_signature.requestedViewport },
			{ "resolvedViewport", a_signature.resolvedViewport },
			{ "eye", a_signature.eyeIndex },
			{ "viewportRole", std::string(magic_enum::enum_name(static_cast<Streamline::DLSSViewportRole>(a_signature.viewportRole))) },
			{ "viewportRoleValue", a_signature.viewportRole },
			{ "output", { { "width", a_signature.outputWidth }, { "height", a_signature.outputHeight } } },
			{ "qualityMode", a_signature.qualityMode },
			{ "dlssPreset", a_signature.dlssPreset },
			{ "extentIn", {
							  { "left", a_signature.extentInLeft },
							  { "top", a_signature.extentInTop },
							  { "width", a_signature.extentInWidth },
							  { "height", a_signature.extentInHeight },
						  } },
			{ "extentOut", {
							   { "left", a_signature.extentOutLeft },
							   { "top", a_signature.extentOutTop },
							   { "width", a_signature.extentOutWidth },
							   { "height", a_signature.extentOutHeight },
						   } },
			{ "viewportScale", { { "x", quantized(a_signature.viewportScaleXQ) }, { "y", quantized(a_signature.viewportScaleYQ) } } },
			{ "pinholeOffset", { { "x", quantized(a_signature.pinholeOffsetXQ) }, { "y", quantized(a_signature.pinholeOffsetYQ) } } },
			{ "jitter", { { "x", quantized(a_signature.jitterXQ) }, { "y", quantized(a_signature.jitterYQ) } } },
			{ "historyReset", a_signature.historyResetRequested },
			{ "colorBuffersHDR", a_signature.colorBuffersHDR },
			{ "submitStageVR", a_signature.submitStageVRDLSS },
			{ "streamlineConstants", {
										 { "encoding", "IEEE-754 binary32 bit patterns; Boolean fields use sl::Boolean values" },
										 { "cameraViewToClip", a_signature.constants.cameraViewToClip },
										 { "clipToCameraView", a_signature.constants.clipToCameraView },
										 { "clipToLensClip", a_signature.constants.clipToLensClip },
										 { "clipToPrevClip", a_signature.constants.clipToPrevClip },
										 { "prevClipToClip", a_signature.constants.prevClipToClip },
										 { "jitterOffset", a_signature.constants.jitterOffset },
										 { "motionVectorScale", a_signature.constants.motionVectorScale },
										 { "cameraPinholeOffset", a_signature.constants.cameraPinholeOffset },
										 { "cameraPosition", a_signature.constants.cameraPosition },
										 { "cameraUp", a_signature.constants.cameraUp },
										 { "cameraRight", a_signature.constants.cameraRight },
										 { "cameraForward", a_signature.constants.cameraForward },
										 { "cameraNear", a_signature.constants.cameraNear },
										 { "cameraFar", a_signature.constants.cameraFar },
										 { "cameraFOV", a_signature.constants.cameraFOV },
										 { "cameraAspectRatio", a_signature.constants.cameraAspectRatio },
										 { "motionVectorsInvalidValue", a_signature.constants.motionVectorsInvalidValue },
										 { "minRelativeLinearDepthObjectSeparation", a_signature.constants.minRelativeLinearDepthObjectSeparation },
										 { "depthInverted", a_signature.constants.depthInverted },
										 { "cameraMotionIncluded", a_signature.constants.cameraMotionIncluded },
										 { "motionVectors3D", a_signature.constants.motionVectors3D },
										 { "reset", a_signature.constants.reset },
										 { "orthographicProjection", a_signature.constants.orthographicProjection },
										 { "motionVectorsDilated", a_signature.constants.motionVectorsDilated },
										 { "motionVectorsJittered", a_signature.constants.motionVectorsJittered },
									 } },
			{ "resources", {
							   { "colorIn", DLSSDevBenchHex64(a_signature.colorIn) },
							   { "colorOut", DLSSDevBenchHex64(a_signature.colorOut) },
							   { "depth", DLSSDevBenchHex64(a_signature.depth) },
							   { "motionVectors", DLSSDevBenchHex64(a_signature.motionVectors) },
							   { "reactiveMask", DLSSDevBenchHex64(a_signature.reactiveMask) },
							   { "transparencyMask", DLSSDevBenchHex64(a_signature.transparencyMask) },
						   } },
		};
	}

	json DLSSDevBenchTraceSummaryJson(const Streamline::DLSSDevBenchTraceSnapshot& a_snapshot)
	{
		return {
			{ "active", a_snapshot.active },
			{ "sessionID", a_snapshot.sessionID },
			{ "capacity", Streamline::kDLSSDevBenchTraceCapacity },
			{ "timestampQPCFrequency", a_snapshot.timestampQPCFrequency },
			{ "retainedRecords", a_snapshot.retainedRecords },
			{ "totalRecords", a_snapshot.totalRecords },
			{ "overwrittenRecords", a_snapshot.overwrittenRecords },
			{ "droppedRecords", a_snapshot.droppedRecords },
			{ "constantsCacheReuses", a_snapshot.constantsCacheReuses },
			{ "setConstantsCalls", a_snapshot.setConstantsCalls },
			{ "evaluateCalls", a_snapshot.evaluateCalls },
			{ "duplicatedConstantsFailures", a_snapshot.duplicatedConstantsFailures },
			{ "evaluateFailures", a_snapshot.evaluateFailures },
			{ "lastDuplicatedConstantsFailureFound", a_snapshot.lastDuplicatedConstantsFailureFound },
			{ "lastDuplicatedConstantsFailureSequence", a_snapshot.lastDuplicatedConstantsFailureSequence },
			{ "lastEvaluateFailureFound", a_snapshot.lastEvaluateFailureFound },
			{ "lastEvaluateFailureSequence", a_snapshot.lastEvaluateFailureSequence },
		};
	}

	json DLSSDevBenchTraceCallJson(const Streamline::DLSSDevBenchTraceCall& a_call)
	{
		return {
			{ "sequence", a_call.sequence },
			{ "timestampQPC", a_call.timestampQPC },
			{ "stage", GetDLSSDevBenchTraceStageName(a_call.stage) },
			{ "resultCode", a_call.resultCode },
			{ "result", std::string(magic_enum::enum_name(static_cast<sl::Result>(a_call.resultCode))) },
			{ "label", std::string(a_call.label.data()) },
			{ "threadID", a_call.threadID },
			{ "compositorCycle", a_call.compositorCycleToken },
			{ "compositorCycleExact", std::to_string(a_call.compositorCycleToken) },
			{ "signature", DLSSDevBenchTraceSignatureJson(a_call.signature) },
		};
	}

	json DLSSDevBenchTraceRecordJson(const Streamline::DLSSDevBenchTraceRecord& a_record)
	{
		json output{
			{ "current", DLSSDevBenchTraceCallJson(a_record.current) },
			{ "previousConstantsFound", a_record.previousConstantsFound },
			{ "constantsChangedFieldMask", a_record.constantsChangedFieldMask },
			{ "constantsChangedFieldMaskExact", DLSSDevBenchHex64(a_record.constantsChangedFieldMask) },
			{ "constantsChangedFields", DLSSDevBenchChangedFieldsJson(a_record.constantsChangedFieldMask) },
			{ "previousEvaluationFound", a_record.previousEvaluationFound },
			{ "evaluationChangedFieldMask", a_record.evaluationChangedFieldMask },
			{ "evaluationChangedFieldMaskExact", DLSSDevBenchHex64(a_record.evaluationChangedFieldMask) },
			{ "evaluationChangedFields", DLSSDevBenchChangedFieldsJson(a_record.evaluationChangedFieldMask) },
		};
		if (a_record.previousConstantsFound)
			output["previousConstants"] = DLSSDevBenchTraceCallJson(a_record.previousConstants);
		if (a_record.previousEvaluationFound)
			output["previousEvaluation"] = DLSSDevBenchTraceCallJson(a_record.previousEvaluation);
		return output;
	}

	bool TryGetNonNegativeInteger(const json& a_value, uint64_t& a_result)
	{
		if (a_value.is_number_unsigned()) {
			a_result = a_value.get<uint64_t>();
			return true;
		}
		if (!a_value.is_number_integer())
			return false;
		const int64_t value = a_value.get<int64_t>();
		if (value < 0)
			return false;
		a_result = static_cast<uint64_t>(value);
		return true;
	}

	json DLSSDevBenchTraceReadJson(
		const Streamline::DLSSDevBenchTraceSnapshot& a_snapshot,
		uint64_t a_afterSequence,
		std::size_t a_limit)
	{
		json records = json::array();
		uint64_t lastReturnedSequence = a_afterSequence;
		for (const auto& record : a_snapshot.records) {
			if (record.current.sequence <= a_afterSequence)
				continue;
			if (records.size() >= a_limit)
				break;
			records.push_back(DLSSDevBenchTraceRecordJson(record));
			lastReturnedSequence = record.current.sequence;
		}
		const uint64_t availableFromSequence = a_snapshot.records.empty() ? 0u : a_snapshot.records.front().current.sequence;
		const uint64_t latestSequence = a_snapshot.records.empty() ? 0u : a_snapshot.records.back().current.sequence;
		const bool requestedSequenceOverwritten =
			availableFromSequence > 0 && a_afterSequence < availableFromSequence - 1u;
		json output{
			{ "summary", DLSSDevBenchTraceSummaryJson(a_snapshot) },
			{ "afterSequence", a_afterSequence },
			{ "limit", a_limit },
			{ "availableFromSequence", availableFromSequence },
			{ "requestedSequenceOverwritten", requestedSequenceOverwritten },
			{ "latestSequence", latestSequence },
			{ "lastReturnedSequence", lastReturnedSequence },
			{ "moreAvailable", lastReturnedSequence < latestSequence },
			{ "records", std::move(records) },
		};
		if (a_snapshot.lastDuplicatedConstantsFailureFound) {
			output["lastDuplicatedConstantsFailure"] =
				DLSSDevBenchTraceRecordJson(a_snapshot.lastDuplicatedConstantsFailure);
		}
		if (a_snapshot.lastEvaluateFailureFound)
			output["lastEvaluateFailure"] = DLSSDevBenchTraceRecordJson(a_snapshot.lastEvaluateFailure);
		return output;
	}

	json RenderScaleActions()
	{
		return json::array({
			"status",
			"cpu_performance_status",
			"cpu_performance_start",
			"cpu_performance_stop",
			"cpu_performance_reset",
			"gpu_performance_status",
			"gpu_performance_start",
			"gpu_performance_stop",
			"gpu_performance_reset",
			"dlss_trace_status",
			"dlss_trace_start",
			"dlss_trace_read",
			"dlss_trace_stop",
			"dlss_trace_reset",
			"record",
			"start",
			"apply",
			"stop",
			"reset",
			"probe_start",
			"probe_stop",
			"probe_record",
			"probe_reset",
			"ham_status",
			"ham_reset",
			"trim",
			"texture_lifetime_start",
			"texture_lifetime_status",
			"texture_lifetime_checkpoint",
			"texture_lifetime_stop",
			"texture_lifetime_reset"
		});
	}

	void RunHandler(
		json (*a_build)(const json&),
		const char* a_argsJson,
		void* a_sink,
		DevBenchAPI::WriteFn a_write) noexcept
	{
		json output;
		try {
			json args = json::object();
			if (a_argsJson && *a_argsJson)
				args = json::parse(a_argsJson);
			if (!args.is_object())
				throw std::runtime_error("arguments must be a JSON object");
			if (auto mismatch = BuildProvenance::ValidateExpectedBuild(args))
				output = std::move(*mismatch);
			else
				output = a_build(args);
		} catch (const std::exception& e) {
			output = { { "error", "invalid request" }, { "detail", e.what() } };
		} catch (...) {
			output = { { "error", "unknown handler error" } };
		}

		BuildProvenance::AttachProducer(output);
		try {
			const std::string serialized = output.dump();
			a_write(a_sink, serialized.c_str());
		} catch (...) {
			const char* fallback = R"({"error":"response serialization failed"})";
			a_write(a_sink, fallback);
		}
	}

	json RunOnMainThread(std::function<json()> a_run)
	{
		auto* taskInterface = SKSE::GetTaskInterface();
		if (!taskInterface)
			return { { "error", "SKSE task interface unavailable" } };

		auto promise = std::make_shared<std::promise<json>>();
		auto cancelled = std::make_shared<std::atomic_bool>(false);
		auto future = promise->get_future();
		taskInterface->AddTask([promise, cancelled, run = std::move(a_run)]() mutable {
			if (cancelled->load(std::memory_order_acquire))
				return;
			try {
				promise->set_value(run());
			} catch (const std::exception& e) {
				promise->set_value(json{ { "error", "main-thread task failed" }, { "detail", e.what() } });
			} catch (...) {
				promise->set_value(json{ { "error", "main-thread task failed" } });
			}
		});

		if (future.wait_for(kMainThreadTimeout) != std::future_status::ready) {
			cancelled->store(true, std::memory_order_release);
			return { { "error", "main thread did not run within 5000ms" } };
		}
		return future.get();
	}

	json BuildRenderScaleResult(const json& a_args)
	{
		const std::string action = a_args.value("action", std::string("status"));
		if (action.starts_with("texture_lifetime_") && !globals::game::isVR) {
			return json{ { "error", "D3D11 texture-lifetime capture requires Skyrim VR" } };
		}
		if (action.starts_with("dlss_trace_") && !globals::game::isVR) {
			return json{ { "error", "DLSS dispatch tracing requires Skyrim VR" } };
		}
		if (action == "status") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale iteration control requires Skyrim VR" } };
				return json{ { "action", "status" }, { "status", BuildStatus(globals::features::upscaling) } };
			});
		}

		if (action == "dlss_trace_status") {
			return {
				{ "action", "dlss_trace_status" },
				{ "capture", DLSSDevBenchTraceSummaryJson(globals::features::upscaling.streamline.GetDLSSDevBenchTraceSnapshot(false)) },
			};
		}

		if (action == "dlss_trace_start") {
			auto& streamline = globals::features::upscaling.streamline;
			if (!streamline.StartDLSSDevBenchTrace()) {
				return {
					{ "error", "a DLSS dispatch trace is already active" },
					{ "capture", DLSSDevBenchTraceSummaryJson(streamline.GetDLSSDevBenchTraceSnapshot(false)) },
				};
			}
			return {
				{ "action", "dlss_trace_start" },
				{ "capture", DLSSDevBenchTraceSummaryJson(streamline.GetDLSSDevBenchTraceSnapshot(false)) },
			};
		}

		if (action == "dlss_trace_read") {
			uint64_t afterSequence = 0;
			if (a_args.contains("afterSequence")) {
				if (!TryGetNonNegativeInteger(a_args["afterSequence"], afterSequence))
					return { { "error", "afterSequence must be a non-negative integer" } };
			}
			uint64_t limit = kDLSSDevBenchTraceDefaultReadLimit;
			if (a_args.contains("limit")) {
				if (!TryGetNonNegativeInteger(a_args["limit"], limit))
					return { { "error", "limit must be a non-negative integer" } };
				if (limit < 1 || limit > Streamline::kDLSSDevBenchTraceCapacity) {
					return {
						{ "error", "limit is outside the supported range" },
						{ "minimum", 1 },
						{ "maximum", Streamline::kDLSSDevBenchTraceCapacity },
					};
				}
			}
			return {
				{ "action", "dlss_trace_read" },
				{ "capture", DLSSDevBenchTraceReadJson(
								 globals::features::upscaling.streamline.GetDLSSDevBenchTraceSnapshot(),
								 afterSequence,
								 static_cast<std::size_t>(limit)) },
			};
		}

		if (action == "dlss_trace_stop") {
			auto& streamline = globals::features::upscaling.streamline;
			if (!streamline.StopDLSSDevBenchTrace()) {
				return {
					{ "error", "no DLSS dispatch trace is active" },
					{ "capture", DLSSDevBenchTraceSummaryJson(streamline.GetDLSSDevBenchTraceSnapshot(false)) },
				};
			}
			return {
				{ "action", "dlss_trace_stop" },
				{ "capture", DLSSDevBenchTraceSummaryJson(streamline.GetDLSSDevBenchTraceSnapshot(false)) },
			};
		}

		if (action == "dlss_trace_reset") {
			auto& streamline = globals::features::upscaling.streamline;
			if (!streamline.ResetDLSSDevBenchTrace()) {
				return {
					{ "error", "stop the active DLSS dispatch trace before resetting it" },
					{ "capture", DLSSDevBenchTraceSummaryJson(streamline.GetDLSSDevBenchTraceSnapshot(false)) },
				};
			}
			return {
				{ "action", "dlss_trace_reset" },
				{ "capture", DLSSDevBenchTraceSummaryJson(streamline.GetDLSSDevBenchTraceSnapshot(false)) },
			};
		}

		if (action == "cpu_performance_status") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale CPU telemetry requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				return json{
					{ "action", "cpu_performance_status" },
					{ "cpuPerformance", CPUPerformanceJson(upscaling) },
				};
			});
		}

		if (action == "cpu_performance_start") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale CPU telemetry requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				if (upscaling.IsVRRenderScaleCPUPerformanceTelemetryActive()) {
					return json{
						{ "error", "a render-scale CPU telemetry capture is already active" },
						{ "cpuPerformance", CPUPerformanceJson(upscaling) },
					};
				}
				upscaling.StartVRRenderScaleCPUPerformanceTelemetry();
				return json{
					{ "action", "cpu_performance_start" },
					{ "cpuPerformance", CPUPerformanceJson(upscaling) },
				};
			});
		}

		if (action == "cpu_performance_stop") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale CPU telemetry requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				if (!upscaling.IsVRRenderScaleCPUPerformanceTelemetryActive()) {
					return json{
						{ "error", "no render-scale CPU telemetry capture is active" },
						{ "cpuPerformance", CPUPerformanceJson(upscaling) },
					};
				}
				upscaling.StopVRRenderScaleCPUPerformanceTelemetry();
				return json{
					{ "action", "cpu_performance_stop" },
					{ "cpuPerformance", CPUPerformanceJson(upscaling) },
				};
			});
		}

		if (action == "cpu_performance_reset") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale CPU telemetry requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				if (upscaling.IsVRRenderScaleCPUPerformanceTelemetryActive()) {
					return json{
						{ "error", "stop the render-scale CPU telemetry capture before resetting it" },
						{ "cpuPerformance", CPUPerformanceJson(upscaling) },
					};
				}
				upscaling.ResetVRRenderScaleCPUPerformanceTelemetry();
				return json{
					{ "action", "cpu_performance_reset" },
					{ "cpuPerformance", CPUPerformanceJson(upscaling) },
				};
			});
		}

		if (action == "gpu_performance_status") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "GPU performance capture requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				return json{ { "action", "gpu_performance_status" }, { "capture", BuildGPUPerformanceStatus(upscaling) } };
			});
		}

		if (action == "gpu_performance_start") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "GPU performance capture requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				if (upscaling.IsVRRenderScaleGPUPerformanceTelemetryActive())
					return json{ { "error", "a GPU performance capture is already active" }, { "capture", BuildGPUPerformanceStatus(upscaling) } };
				upscaling.StartVRRenderScaleGPUPerformanceTelemetry();
				return json{ { "action", "gpu_performance_start" }, { "capture", BuildGPUPerformanceStatus(upscaling) } };
			});
		}

		if (action == "gpu_performance_stop") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "GPU performance capture requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				if (!upscaling.IsVRRenderScaleGPUPerformanceTelemetryActive())
					return json{ { "error", "no GPU performance capture is active" }, { "capture", BuildGPUPerformanceStatus(upscaling) } };
				upscaling.StopVRRenderScaleGPUPerformanceTelemetry();
				return json{ { "action", "gpu_performance_stop" }, { "capture", BuildGPUPerformanceStatus(upscaling) } };
			});
		}

		if (action == "gpu_performance_reset") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "GPU performance capture requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				if (upscaling.IsVRRenderScaleGPUPerformanceTelemetryActive())
					return json{ { "error", "stop the GPU performance capture before resetting it" }, { "capture", BuildGPUPerformanceStatus(upscaling) } };
				upscaling.ResetVRRenderScaleGPUPerformanceTelemetry();
				return json{ { "action", "gpu_performance_reset" }, { "capture", BuildGPUPerformanceStatus(upscaling) } };
			});
		}

		if (action == "record") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale iteration control requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				return json{
					{ "action", "record" },
					{ "record", upscaling.BuildVRRenderScaleIterationRecord() },
					{ "status", BuildStatus(upscaling) },
					{ "statusRelation", "captured_after_record" },
				};
			});
		}

		if (action == "start") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale iteration control requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				if (upscaling.GetVRRenderScaleStressSessionSnapshot().active)
					return json{ { "error", "a stress capture is already active" }, { "status", BuildStatus(upscaling) } };
				upscaling.StartVRRenderScaleStressSession();
				return json{ { "action", "start" }, { "status", BuildStatus(upscaling) } };
			});
		}

		if (action == "stop") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale iteration control requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				if (!upscaling.GetVRRenderScaleStressSessionSnapshot().active)
					return json{ { "error", "no stress capture is active" }, { "status", BuildStatus(upscaling) } };
				upscaling.StopVRRenderScaleStressSession();
				return json{
					{ "action", "stop" },
					{ "record", upscaling.BuildVRRenderScaleIterationRecord() },
					{ "status", BuildStatus(upscaling) },
					{ "statusRelation", "captured_after_record" },
				};
			});
		}

		if (action == "reset") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale iteration control requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				if (upscaling.GetVRRenderScaleStressSessionSnapshot().active)
					return json{ { "error", "stop the active capture before resetting it" }, { "status", BuildStatus(upscaling) } };
				upscaling.ResetVRRenderScaleStressSession();
				return json{ { "action", "reset" }, { "status", BuildStatus(upscaling) } };
			});
		}

		if (action == "probe_start") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "load presentation probing requires Skyrim VR" } };
				if (!globals::state || !globals::state->IsDeveloperMode())
					return json{ { "error", "developer mode is required to start a load presentation probe" } };
				auto& upscaling = globals::features::upscaling;
				const auto status = upscaling.BuildVRLoadPresentationProbeStatus();
				if (status.value("active", false))
					return json{ { "error", "a load presentation probe is already active" }, { "status", status } };
				if (!globals::features::vr.InstallSubmitHook(false)) {
					return json{
						{ "error", "OpenVR submit interception is not available; load presentation probe was not started" },
						{ "status", status }
					};
				}
				upscaling.StartVRLoadPresentationProbe();
				return json{ { "action", "probe_start" }, { "status", upscaling.BuildVRLoadPresentationProbeStatus() } };
			});
		}

		if (action == "probe_stop") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "load presentation probing requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				const auto status = upscaling.BuildVRLoadPresentationProbeStatus();
				if (!status.value("active", false))
					return json{ { "error", "no load presentation probe is active" }, { "status", status } };
				upscaling.StopVRLoadPresentationProbe();
				return json{ { "action", "probe_stop" }, { "status", upscaling.BuildVRLoadPresentationProbeStatus() } };
			});
		}

		if (action == "probe_record") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "load presentation probing requires Skyrim VR" } };
				return json{
					{ "action", "probe_record" },
					{ "record", globals::features::upscaling.BuildVRLoadPresentationProbeRecord() },
				};
			});
		}

		if (action == "probe_reset") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "load presentation probing requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				const auto status = upscaling.BuildVRLoadPresentationProbeStatus();
				if (status.value("active", false))
					return json{ { "error", "stop the load presentation probe before resetting it" }, { "status", status } };
				upscaling.ResetVRLoadPresentationProbe();
				return json{ { "action", "probe_reset" }, { "status", upscaling.BuildVRLoadPresentationProbeStatus() } };
			});
		}

		if (action == "ham_status") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "HMD-mask diagnostics require Skyrim VR" } };
				return json{
					{ "action", "ham_status" },
					{ "hamApiVersion", 3 },
					{ "status", globals::features::upscaling.BuildVRHMDMaskDiagnosticsStatus() },
				};
			});
		}

		if (action == "ham_reset") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "HMD-mask diagnostics require Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				upscaling.ResetVRHMDMaskDiagnostics();
				return json{
					{ "action", "ham_reset" },
					{ "hamApiVersion", 3 },
					{ "status", upscaling.BuildVRHMDMaskDiagnosticsStatus() },
				};
			});
		}

		if (action == "trim") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "DXGI memory trimming requires Skyrim VR" } };
				if (!globals::state || !globals::state->IsDeveloperMode())
					return json{ { "error", "developer mode is required to request a diagnostic DXGI trim" } };

				auto& upscaling = globals::features::upscaling;
				const auto before = upscaling.GetVRRenderScaleTransitionSnapshot();
				if (before.memoryTrim.pending)
					return json{ { "error", "a GPU-fenced DXGI trim is already pending" }, { "status", BuildStatus(upscaling) } };

				uint64_t ownerEpoch = g_nextDiagnosticTrimEpoch.fetch_add(1, std::memory_order_acq_rel);
				if (ownerEpoch == 0)
					ownerEpoch = g_nextDiagnosticTrimEpoch.fetch_add(1, std::memory_order_acq_rel);
				const bool armed = upscaling.ArmVRRenderScaleMemoryTrim(
					ownerEpoch,
					Upscaling::VRRenderScaleMemoryTrimReason::Pressure);
				return json{
					{ "action", "trim" },
					{ "armed", armed },
					{ "ownerEpoch", ownerEpoch },
					{ "status", BuildStatus(upscaling) },
				};
			});
		}

		if (action == "texture_lifetime_start") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "D3D11 texture-lifetime capture requires Skyrim VR" } };
				if (!Diagnostics::D3DTextureLifetimeTracker::Start())
					return json{
						{ "error", "a D3D11 texture-lifetime capture is already active" },
						{ "capture", Diagnostics::D3DTextureLifetimeTracker::BuildStatus() }
					};
				return json{
					{ "action", "texture_lifetime_start" },
					{ "capture", Diagnostics::D3DTextureLifetimeTracker::BuildStatus() }
				};
			});
		}

		if (action == "texture_lifetime_status") {
			return json{
				{ "action", "texture_lifetime_status" },
				{ "capture", Diagnostics::D3DTextureLifetimeTracker::BuildStatus() }
			};
		}

		if (action == "texture_lifetime_checkpoint") {
			if (!Diagnostics::D3DTextureLifetimeTracker::Checkpoint())
				return {
					{ "error", "no D3D11 texture-lifetime capture is active" },
					{ "capture", Diagnostics::D3DTextureLifetimeTracker::BuildStatus() }
				};
			return {
				{ "action", "texture_lifetime_checkpoint" },
				{ "capture", Diagnostics::D3DTextureLifetimeTracker::BuildStatus() }
			};
		}

		if (action == "texture_lifetime_stop") {
			return RunOnMainThread([]() {
				if (!Diagnostics::D3DTextureLifetimeTracker::Stop())
					return json{
						{ "error", "no D3D11 texture-lifetime capture is active" },
						{ "capture", Diagnostics::D3DTextureLifetimeTracker::BuildStatus() }
					};
				return json{
					{ "action", "texture_lifetime_stop" },
					{ "capture", Diagnostics::D3DTextureLifetimeTracker::BuildStatus() }
				};
			});
		}

		if (action == "texture_lifetime_reset") {
			if (!Diagnostics::D3DTextureLifetimeTracker::Reset())
				return {
					{ "error", "stop the active D3D11 texture-lifetime capture before resetting it" },
					{ "capture", Diagnostics::D3DTextureLifetimeTracker::BuildStatus() }
				};
			return {
				{ "action", "texture_lifetime_reset" },
				{ "capture", Diagnostics::D3DTextureLifetimeTracker::BuildStatus() }
			};
		}

		if (action == "apply") {
			if (!a_args.contains("method") || !a_args["method"].is_string())
				return { { "error", "apply requires string parameter 'method'" } };
			if (!a_args.contains("enabled") || !a_args["enabled"].is_boolean())
				return { { "error", "apply requires boolean parameter 'enabled'" } };
			if (!a_args.contains("qualityMode") || !a_args["qualityMode"].is_number_integer())
				return { { "error", "apply requires integer parameter 'qualityMode'" } };

			const std::string methodName = a_args["method"].get<std::string>();
			Upscaling::UpscaleMethod method = Upscaling::UpscaleMethod::kNONE;
			if (methodName == "dlss")
				method = Upscaling::UpscaleMethod::kDLSS;
			else if (methodName == "fsr")
				method = Upscaling::UpscaleMethod::kFSR;
			else
				return { { "error", "method must be 'dlss' or 'fsr'" }, { "method", methodName } };

			const bool enabled = a_args["enabled"].get<bool>();
			const int64_t qualityValue = a_args["qualityMode"].get<int64_t>();
			if (qualityValue < 0 || qualityValue > static_cast<int64_t>(Upscaling::kQualityModeMaxIndex))
				return { { "error", "qualityMode is outside 0..6" }, { "qualityMode", qualityValue } };
			if (enabled && qualityValue == 0)
				return { { "error", "enabled render scale requires qualityMode 1..6" } };

			std::optional<uint32_t> requestedPreset;
			if (a_args.contains("dlssPreset")) {
				if (!a_args["dlssPreset"].is_number_integer())
					return { { "error", "dlssPreset must be an integer" } };
				const int64_t presetValue = a_args["dlssPreset"].get<int64_t>();
				if (presetValue < 0 || presetValue > static_cast<int64_t>(Upscaling::kDLSSPresetMaxIndex))
					return { { "error", "dlssPreset is outside 0..5" }, { "dlssPreset", presetValue } };
				requestedPreset = static_cast<uint32_t>(presetValue);
			}

			return RunOnMainThread([method,
									   methodName,
									   enabled,
									   qualityMode = static_cast<uint32_t>(qualityValue),
									   requestedPreset]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale iteration control requires Skyrim VR" } };

				auto& upscaling = globals::features::upscaling;
				const auto session = upscaling.GetVRRenderScaleStressSessionSnapshot();
				if (!session.active)
					return json{ { "error", "start a stress capture before applying an iteration profile" }, { "status", BuildStatus(upscaling) } };

				const auto before = upscaling.GetPendingVRRenderScaleDesiredProfile();
				const uint32_t dlssPreset = requestedPreset.value_or(before.dlssPreset);
				const auto applied = upscaling.ApplyCSMenuUpscalingTransition(
					method,
					enabled,
					qualityMode,
					dlssPreset,
					"devbench render-scale iteration",
					Upscaling::VRUpscalingTransitionOrigin::CSMenu);

				const bool accepted = applied.disposition != Upscaling::UpscalingTransitionApplyDisposition::Rejected;
				const bool asynchronous =
					applied.disposition == Upscaling::UpscalingTransitionApplyDisposition::Queued ||
					applied.disposition == Upscaling::UpscalingTransitionApplyDisposition::Deferred ||
					applied.disposition == Upscaling::UpscalingTransitionApplyDisposition::Coalesced;
				json response{
					{ "action", "apply" },
					{ "method", methodName },
					{ "enabled", enabled },
					{ "qualityMode", qualityMode },
					{ "dlssPreset", dlssPreset },
					{ "accepted", accepted },
					{ "asynchronous", asynchronous },
					{ "disposition", GetApplyDispositionName(applied.disposition) },
					{ "rejection", GetApplyRejectionName(applied.rejection) },
					{ "requestID", applied.requestID },
					{ "transitionEpoch", applied.transitionEpoch },
					{ "status", BuildStatus(upscaling) },
				};
				return response;
			});
		}

		return {
			{ "error", "unknown action" },
			{ "action", action },
			{ "supported", RenderScaleActions() },
		};
	}

	void RenderScaleToolHandler(
		void*,
		const char* a_argsJson,
		void* a_sink,
		DevBenchAPI::WriteFn a_write)
	{
		RunHandler(&BuildRenderScaleResult, a_argsJson, a_sink, a_write);
	}

	void RenderScaleInspectExtensionHandler(
		void*,
		const char*,
		void* a_sink,
		DevBenchAPI::WriteFn a_write)
	{
		json result{
			{ "registered", g_registered.load(std::memory_order_acquire) },
			{ "tool", "communityshaders.renderscale" },
			{ "usage", R"(Invoke the top-level devbench tool with {"action":"status"} when exposed. If the client has not exposed dynamic tools, dispatch it through devbench scenario with a tool step: {"tool":"communityshaders.renderscale","args":{"action":"status"}}.)" },
			{ "actions", RenderScaleActions() },
		};
		BuildProvenance::AttachProducer(result);
		const auto serialized = result.dump();
		a_write(a_sink, serialized.c_str());
	}

	DevBenchAPI::IDevBenchInterface001* GetDevBenchToolExtensionInterface()
	{
		const auto messaging = SKSE::GetMessagingInterface();
		if (!messaging)
			return nullptr;

		DevBenchAPI::DevBenchMessage message;
		messaging->Dispatch(
			DevBenchAPI::DevBenchMessage::kMessage_GetInterface,
			&message,
			sizeof(DevBenchAPI::DevBenchMessage*),
			DevBenchAPI::DevBenchPluginName);
		if (!message.GetApiFunction)
			return nullptr;

		return static_cast<DevBenchAPI::IDevBenchInterface001*>(
			message.GetApiFunction(kDevBenchToolExtensionRevision));
	}
}

namespace VRRenderScaleDevBenchBridge
{
	void Install()
	{
		g_registered.store(false, std::memory_order_release);
		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench) {
			logger::info("VRRenderScaleDevBenchBridge: devbench host not present; iteration tool not registered");
			return;
		}

		static constexpr const char* diagnosticDescriptor =
			R"({"description":"Control and inspect CSX VR render-scale, fixed tiled-exact HMD-mask, transition, DevBench-only DLSS dispatch tracing, and CPU/GPU performance diagnostics. DLSS tracing is opt-in and non-blocking; droppedRecords reports capture contention, and the latest failures remain pinned if the ring wraps. Every response identifies the exact producing DLL; expectedBuildId makes operations fail closed on a stale or unintended build.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["status","cpu_performance_status","cpu_performance_start","cpu_performance_stop","cpu_performance_reset","gpu_performance_status","gpu_performance_start","gpu_performance_stop","gpu_performance_reset","dlss_trace_status","dlss_trace_start","dlss_trace_read","dlss_trace_stop","dlss_trace_reset","record","start","apply","stop","reset","probe_start","probe_stop","probe_record","probe_reset","ham_status","ham_reset","trim","texture_lifetime_start","texture_lifetime_status","texture_lifetime_checkpoint","texture_lifetime_stop","texture_lifetime_reset"]},"method":{"type":"string","enum":["dlss","fsr"]},"enabled":{"type":"boolean"},"qualityMode":{"type":"integer","minimum":0,"maximum":6},"dlssPreset":{"type":"integer","minimum":0,"maximum":5},"afterSequence":{"type":"integer","minimum":0,"description":"For dlss_trace_read, return records after this sequence."},"limit":{"type":"integer","minimum":1,"maximum":256,"description":"Maximum ring records returned by dlss_trace_read; defaults to 32 and pinned failures are returned separately."},"expectedBuildId":{"type":"string","description":"Exact 64-character CSX Build ID required for this operation."}},"required":["action"]}})";
		devBench->RegisterTool(
			"communityshaders.renderscale",
			diagnosticDescriptor,
			&RenderScaleToolHandler,
			nullptr);
		if (devBench->GetBuildNumber() >= 10500) {
			static constexpr const char* inspectDescriptor =
				R"({"description":"Reports the Community Shaders render-scale diagnostic tool registration and points callers to the top-level communityshaders.renderscale tool."})";
			if (auto* extensionDevBench = GetDevBenchToolExtensionInterface()) {
				const bool inserted = extensionDevBench->RegisterToolExtension(
					"inspect",
					"communityshaders.renderscale",
					inspectDescriptor,
					&RenderScaleInspectExtensionHandler,
					nullptr);
				logger::info(
					"VRRenderScaleDevBenchBridge: registered inspect extension communityshaders.renderscale with devbench build {}{}",
					extensionDevBench->GetBuildNumber(),
					inserted ? "" : " (replaced existing handler)");
			} else {
				logger::warn("VRRenderScaleDevBenchBridge: devbench revision-5 interface unavailable; inspect extension not registered");
			}
		}
		g_registered.store(true, std::memory_order_release);
		logger::info(
			"VRRenderScaleDevBenchBridge: registered communityshaders.renderscale with devbench build {}",
			devBench->GetBuildNumber());
	}

	bool IsBuilt()
	{
		return true;
	}

	bool IsRegistered()
	{
		return g_registered.load(std::memory_order_acquire);
	}
}

#else

namespace VRRenderScaleDevBenchBridge
{
	void Install() {}

	bool IsBuilt()
	{
		return false;
	}

	bool IsRegistered()
	{
		return false;
	}
}

#endif
