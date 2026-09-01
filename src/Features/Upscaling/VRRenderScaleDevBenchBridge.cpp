#include "Features/Upscaling/VRRenderScaleDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Features/Upscaling.h"
#	include "Features/Upscaling/NeuralRendering/PipelinePolicy.h"
#	include "Features/Upscaling/NeuralRendering/Renderer.h"
#	include "Features/VR.h"
#	include "Globals.h"
#	include "State.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <algorithm>
#	include <atomic>
#	include <cmath>
#	include <chrono>
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
	std::atomic_bool g_registered{ false };

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

	json NeuralImplementationJson(bool a_batchedStereo, bool a_directCommit)
	{
		return {
			{ "id", NeuralRendering::GetImplementationName(a_batchedStereo, a_directCommit) },
			{ "displayName", NeuralRendering::GetImplementationDisplayName(a_batchedStereo, a_directCommit) },
			{ "purpose", NeuralRendering::GetImplementationPurposeName(a_batchedStereo, a_directCommit) },
			{ "purposeDescription", NeuralRendering::GetImplementationPurpose(a_batchedStereo, a_directCommit) },
			{ "stereoSubmission", NeuralRendering::GetStereoSubmissionName(a_batchedStereo) },
			{ "outputCommit", NeuralRendering::GetOutputCommitName(a_directCommit) },
			{ "batchedStereo", a_batchedStereo },
			{ "directCommit", a_directCommit },
		};
	}

	const char* NeuralMotionVectorScaleModeName(uint32_t a_mode) noexcept
	{
		switch (a_mode) {
		case 0:
			return "full_eye";
		case 1:
			return "center";
		default:
			return "unknown";
		}
	}

	const char* NeuralSubmitPairBoundaryModeName(uint32_t a_mode) noexcept
	{
		switch (a_mode) {
		case 0:
			return "current";
		case 1:
			return "observe";
		case 2:
			return "require";
		default:
			return "unknown";
		}
	}

	json NeuralImplementationMatrixJson()
	{
		json matrix = json::array();
		for (uint32_t index = 0;
			index < NeuralRendering::kPipelineImplementations.size(); ++index) {
			const auto implementation =
				NeuralRendering::kPipelineImplementations[index];
			auto lane = NeuralImplementationJson(
				implementation.batchedStereo, implementation.directCommit);
			lane["index"] = index;
			matrix.push_back(std::move(lane));
		}
		return matrix;
	}

	json NeuralRenderingStatusJson(const Upscaling& a_upscaling)
	{
		const auto snapshot = NeuralRendering::Renderer::Instance().GetSnapshot();
		json failuresByStage = json::array();
		for (std::size_t index = 0;
			index < static_cast<std::size_t>(NeuralRendering::RendererStage::Count);
			++index) {
			const auto stage = static_cast<NeuralRendering::RendererStage>(index);
			failuresByStage.push_back({
				{ "stage", NeuralRendering::ToString(stage) },
				{ "stageValue", index },
				{ "failures", snapshot.counters.failuresByStage[index] },
			});
		}

		json slots = json::array();
		for (std::size_t index = 0; index < NeuralRendering::Runtime::kFeatureSlotCount; ++index) {
			slots.push_back({
				{ "slot", index },
				{ "successes", snapshot.counters.slotSuccesses[index] },
				{ "failures", snapshot.counters.slotFailures[index] },
			});
		}

		const auto routeSnapshots = a_upscaling.GetNeuralStereoRouteSnapshot();
		const auto outerRouteCounters =
			a_upscaling.GetOuterDLSSRouteCountersSnapshot();
		const auto submitCycleSnapshot =
			a_upscaling.GetLatestNeuralSubmitCycleSnapshot();
		const auto submitPairBoundarySnapshot =
			a_upscaling.GetNeuralSubmitPairBoundarySnapshot();
		const auto state = globals::state;
		const uint32_t observedFrame = state ? state->frameCount : 0u;
		const bool developerMode = state && state->IsDeveloperMode();
		const bool submitCycleCurrentFrame =
			submitCycleSnapshot.entryObserved &&
			submitCycleSnapshot.frame == observedFrame;
		const uint64_t observedSubmitCycle = submitCycleCurrentFrame ?
		                                         submitCycleSnapshot.compositorCycle :
		                                         0u;
		uint64_t latestRouteSequence = 0;
		json routes = json::array();
		for (const auto& route : routeSnapshots) {
			latestRouteSequence = std::max(latestRouteSequence, route.sequence);
			const bool submitRole = route.role == Upscaling::NeuralStereoRouteRole::Submit;
			const bool freshForCurrentFrame = route.valid && route.frame == observedFrame;
			const bool freshForCurrentCycle =
				!submitRole ||
				(submitCycleCurrentFrame &&
					submitCycleSnapshot.compositorCycle != 0 &&
					route.valid &&
					route.compositorCycle == submitCycleSnapshot.compositorCycle);
			json eyes = json::array();
			for (uint32_t eye = 0; eye < 2; ++eye) {
				const uint32_t eyeBit = 1u << eye;
				eyes.push_back({
					{ "eye", eye },
					{ "prepared", (route.preparedEyeMask & eyeBit) != 0 },
					{ "attempted", (route.attemptedEyeMask & eyeBit) != 0 },
					{ "evaluated", (route.evaluatedEyeMask & eyeBit) != 0 },
					{ "applied", (route.appliedEyeMask & eyeBit) != 0 },
					{ "neuralCommitted", (route.committedEyeMask & eyeBit) != 0 },
					{ "presentationAccepted", (route.presentationAcceptedEyeMask & eyeBit) != 0 },
					{ "dlssEvaluated", (route.dlssEyeMask & eyeBit) != 0 },
					{ "callerReset", (route.callerResetEyeMask & eyeBit) != 0 },
					{ "featureUpscaling", (route.featureUpscalingEyeMask & eyeBit) != 0 },
					{ "motionVectorScale", {
											   { "x", route.motionVectorScaleX[eye] },
											   { "y", route.motionVectorScaleY[eye] },
										   } },
					{ "inputOffset", {
										 { "x", route.inputOffsetX[eye] },
										 { "y", route.inputOffsetY[eye] },
									 } },
					{ "pinholeOffset", {
										   { "x", route.pinholeOffsetX[eye] },
										   { "y", route.pinholeOffsetY[eye] },
									   } },
					{ "outerDLSSPrepared", (route.outerDLSSPreparedEyeMask & eyeBit) != 0 },
					{ "outerDLSSAttempted", (route.outerDLSSAttemptedEyeMask & eyeBit) != 0 },
					{ "outerDLSSEvaluated", (route.outerDLSSEvaluatedEyeMask & eyeBit) != 0 },
					{ "outerDLSSCommitted", (route.outerDLSSCommittedEyeMask & eyeBit) != 0 },
				});
			}
			const auto routeIndex = static_cast<std::size_t>(route.role);
			const auto counters = routeIndex < outerRouteCounters.size() ?
			                          outerRouteCounters[routeIndex] :
			                          Upscaling::OuterDLSSRouteCountersSnapshot{};
			const json effectiveImplementation = route.effectiveImplementationKnown ?
			                                         NeuralImplementationJson(
														 route.effectiveBatchedStereo,
														 route.effectiveDirectCommit) :
			                                         json(nullptr);
			const json effectiveStereoSubmission =
				route.effectiveImplementationKnown ?
					json(NeuralRendering::GetStereoSubmissionName(
						route.effectiveBatchedStereo)) :
					json(nullptr);
			const json effectiveOutputCommit =
				route.effectiveImplementationKnown ?
					json(NeuralRendering::GetOutputCommitName(
						route.effectiveDirectCommit)) :
					json(nullptr);
			const uint64_t routeCommandSubmissions = submitRole ?
			                                             snapshot.performance.submitCommandSubmissions :
			                                             snapshot.performance.mainCommandSubmissions;
			const uint64_t routeStereoCommandSubmissions = submitRole ?
			                                                   snapshot.performance.submitStereoCommandSubmissions :
			                                                   snapshot.performance.mainStereoCommandSubmissions;
			const uint64_t routeFeatureGpuSamples = submitRole ?
			                                            snapshot.performance.submitFeatureGpuSamples :
			                                            snapshot.performance.mainFeatureGpuSamples;
			const uint64_t routeFeatureGpuMicroseconds = submitRole ?
			                                                 snapshot.performance.submitFeatureGpuMicroseconds :
			                                                 snapshot.performance.mainFeatureGpuMicroseconds;
			routes.push_back({
				{ "valid", route.valid },
				{ "role", Upscaling::GetNeuralStereoRouteRoleName(route.role) },
				{ "roleValue", static_cast<uint32_t>(route.role) },
				{ "sequence", route.sequence },
				{ "compositorCycle", route.compositorCycle },
				{ "frame", route.frame },
				{ "fresh", freshForCurrentFrame && freshForCurrentCycle },
				{ "freshForCurrentFrame", freshForCurrentFrame },
				{ "freshForCurrentCycle", freshForCurrentCycle },
				{ "generation", route.generation },
				{ "arrangement", NeuralRendering::GetPipelineArrangementName(
									 static_cast<NeuralRendering::PipelineArrangement>(route.arrangement)) },
				{ "arrangementValue", route.arrangement },
				{ "requested", route.requested },
				{ "eligible", route.eligible },
				{ "sourceBatchEligible", route.sourceBatchEligible },
				{ "sourceSignatureProven", route.sourceSignatureProven },
				{ "submitPairBoundary", {
											{ "mode", NeuralSubmitPairBoundaryModeName(route.submitPairBoundaryMode) },
											{ "modeValue", route.submitPairBoundaryMode },
											{ "proven", route.submitPairBoundaryProven },
											{ "token", route.submitPairBoundaryToken },
										} },
				{ "submitEyeMaskAtDecision", route.submitEyeMaskAtDecision },
				{ "pairComplete", route.pairComplete },
				{ "gates", {
							   { "colorBuffersHDRKnown", route.colorBuffersHDRKnown },
							   { "colorBuffersHDR", route.colorBuffersHDRKnown ? json(route.colorBuffersHDR) : json(nullptr) },
							   { "hdrRequired", route.hdrRequired },
							   { "hdrClassification", route.colorBuffersHDRKnown ? (route.colorBuffersHDR ? "hdr" : "ldr") : "unknown" },
							   { "frameGenerationActive", route.frameGenerationActive },
							   { "frameGenerationPassed", route.frameGenerationGatePassed },
						   } },
				{ "requestedImplementation", NeuralImplementationJson(route.requestedBatchedStereo, route.requestedDirectCommit) },
				{ "requestedStereoSubmission", NeuralRendering::GetStereoSubmissionName(route.requestedBatchedStereo) },
				{ "requestedOutputCommit", NeuralRendering::GetOutputCommitName(route.requestedDirectCommit) },
				{ "effectiveImplementationKnown", route.effectiveImplementationKnown },
				{ "effectiveImplementation", effectiveImplementation },
				{ "effectiveStereoSubmission", effectiveStereoSubmission },
				{ "effectiveOutputCommit", effectiveOutputCommit },
				{ "implementationModeExecuted", route.implementationModeExecuted },
				{ "pairDisposition", Upscaling::GetNeuralStereoPairDispositionName(route.disposition) },
				{ "fallbackReason", Upscaling::GetNeuralStereoFallbackReasonName(route.fallbackReason) },
				{ "eyeMasks", {
								  { "prepared", route.preparedEyeMask },
								  { "attempted", route.attemptedEyeMask },
								  { "evaluated", route.evaluatedEyeMask },
								  { "applied", route.appliedEyeMask },
								  { "neuralCommitted", route.committedEyeMask },
								  { "presentationAccepted", route.presentationAcceptedEyeMask },
								  { "dlss", route.dlssEyeMask },
								  { "callerReset", route.callerResetEyeMask },
								  { "featureUpscaling", route.featureUpscalingEyeMask },
							  } },
				{ "outerDLSS", {
								   { "requested", route.outerDLSSRequested },
								   { "eligible", route.outerDLSSEligible },
								   { "fallbackReason", Upscaling::GetOuterDLSSFallbackReasonName(route.outerDLSSFallbackReason) },
								   { "eyeMasks", {
													 { "prepared", route.outerDLSSPreparedEyeMask },
													 { "attempted", route.outerDLSSAttemptedEyeMask },
													 { "evaluated", route.outerDLSSEvaluatedEyeMask },
													 { "committed", route.outerDLSSCommittedEyeMask },
												 } },
								   { "counters", {
													 { "requestedTransactions", counters.requestedTransactions },
													 { "eligibleTransactions", counters.eligibleTransactions },
													 { "preparedEyes", counters.preparedEyes },
													 { "attemptedEyes", counters.attemptedEyes },
													 { "evaluatedEyes", counters.evaluatedEyes },
													 { "fallbackTransactions", counters.fallbackTransactions },
													 { "committedTransactions", counters.committedTransactions },
												 } },
							   } },
				{ "performance", {
									 { "commandSubmissions", routeCommandSubmissions },
									 { "stereoCommandSubmissions", routeStereoCommandSubmissions },
									 { "featureGpuSamples", routeFeatureGpuSamples },
									 { "featureGpuMicroseconds", routeFeatureGpuMicroseconds },
									 { "averageFeatureGpuMicroseconds", routeFeatureGpuSamples ? static_cast<double>(routeFeatureGpuMicroseconds) / static_cast<double>(routeFeatureGpuSamples) : 0.0 },
									 { "unexpectedFeatureSlotMaskSamplesGlobal", snapshot.performance.unexpectedFeatureSlotMaskSamples },
								 } },
				{ "eyes", std::move(eyes) },
			});
		}

		return {
			{ "apiVersion", 5 },
			{ "arrangement", NeuralRendering::GetPipelineArrangementName() },
			{ "implementationMatrix", NeuralImplementationMatrixJson() },
			{ "settings", {
							  { "enabled", a_upscaling.settings.neuralRenderingEnabled },
							  { "outerDLSS", a_upscaling.settings.foveatedOuterDLSS },
							  { "mainRoute", a_upscaling.settings.neuralRenderingMainRoute },
							  { "submitRoute", a_upscaling.settings.neuralRenderingSubmitRoute },
							  { "resetEveryFrame", a_upscaling.settings.neuralRenderingResetEveryFrame },
							  { "forceFeatureUpscaling", a_upscaling.settings.neuralRenderingForceFeatureUpscaling },
							  { "motionVectorScaleMode", NeuralMotionVectorScaleModeName(
															 a_upscaling.settings.neuralRenderingMotionVectorScaleMode) },
							  { "motionVectorScaleModeValue", a_upscaling.settings.neuralRenderingMotionVectorScaleMode },
							  { "submitPairBoundaryMode", NeuralSubmitPairBoundaryModeName(
															  a_upscaling.settings.neuralRenderingSubmitPairBoundaryMode) },
							  { "submitPairBoundaryModeValue", a_upscaling.settings.neuralRenderingSubmitPairBoundaryMode },
							  { "batchedStereo", a_upscaling.settings.neuralRenderingBatchedStereo },
							  { "directCommit", a_upscaling.settings.neuralRenderingDirectCommit },
							  { "stereoSubmission", NeuralRendering::GetStereoSubmissionName(
														a_upscaling.settings.neuralRenderingBatchedStereo) },
							  { "outputCommit", NeuralRendering::GetOutputCommitName(
													a_upscaling.settings.neuralRenderingDirectCommit) },
							  { "implementationMode", NeuralRendering::GetImplementationName(
														  a_upscaling.settings.neuralRenderingBatchedStereo,
														  a_upscaling.settings.neuralRenderingDirectCommit) },
							  { "selectedImplementation", NeuralImplementationJson(
															  a_upscaling.settings.neuralRenderingBatchedStereo,
															  a_upscaling.settings.neuralRenderingDirectCommit) },
							  { "preset", a_upscaling.settings.neuralRenderingPreset },
							  { "intensity", a_upscaling.settings.neuralRenderingIntensity },
							  { "localToneStrength", a_upscaling.settings.neuralRenderingLocalTone },
							  { "localStructureStrength", a_upscaling.settings.neuralRenderingLocalStructure },
							  { "skinStructureStrength", a_upscaling.settings.neuralRenderingSkinStructure },
							  { "style", a_upscaling.settings.neuralRenderingStyle },
							  { "useAutoMask", a_upscaling.settings.neuralRenderingAutoMask },
							  { "uiCorrection", a_upscaling.settings.neuralRenderingUICorrection },
						  } },
			{ "safeControlContract", {
										 { "useAutoMask", { { "fixed", true }, { "requiredValue", true } } },
										 { "uiCorrection", { { "fixed", true }, { "requiredValue", false } } },
									 } },
			{ "routeObservation", {
									  { "currentFrame", observedFrame },
									  { "currentSubmitCycle", observedSubmitCycle },
									  { "submitCycleSource", "submit_entry" },
									  { "submitEntryObserved", submitCycleSnapshot.entryObserved },
									  { "submitCycleCurrentFrame", submitCycleCurrentFrame },
									  { "latestSubmitFrame", submitCycleSnapshot.frame },
									  { "latestSubmitCycle", submitCycleSnapshot.compositorCycle },
									  { "latestSubmitObservedEyeMask", submitCycleSnapshot.observedEyeMask },
									  { "latestSequence", latestRouteSequence },
								  } },
			{ "submitPairBoundary", {
										{ "mode", NeuralSubmitPairBoundaryModeName(submitPairBoundarySnapshot.mode) },
										{ "modeValue", submitPairBoundarySnapshot.mode },
										{ "active", submitPairBoundarySnapshot.active },
										{ "sourceValid", submitPairBoundarySnapshot.sourceValid },
										{ "sequence", submitPairBoundarySnapshot.sequence },
										{ "compositorCycle", submitPairBoundarySnapshot.compositorCycle },
										{ "frame", submitPairBoundarySnapshot.frame },
										{ "threadId", submitPairBoundarySnapshot.threadId },
										{ "observedEyeMask", submitPairBoundarySnapshot.observedEyeMask },
										{ "provenEyeMask", submitPairBoundarySnapshot.provenEyeMask },
										{ "outerCalls", submitPairBoundarySnapshot.outerCalls },
										{ "innerCalls", submitPairBoundarySnapshot.innerCalls },
										{ "provenInnerCalls", submitPairBoundarySnapshot.provenInnerCalls },
										{ "orphanInnerCalls", submitPairBoundarySnapshot.orphanInnerCalls },
										{ "mismatchedInnerCalls", submitPairBoundarySnapshot.mismatchedInnerCalls },
										{ "incompleteOuterCalls", submitPairBoundarySnapshot.incompleteOuterCalls },
										{ "exceptions", submitPairBoundarySnapshot.exceptions },
									} },
			{ "stereoRoutes", std::move(routes) },
			{ "runtime", {
							 { "status", snapshot.status },
							 { "trust", snapshot.trust },
							 { "developerMode", developerMode },
							 { "patchedRuntimeAllowed", true },
							 { "failureStage", snapshot.runtimeFailureStage },
							 { "detail", snapshot.detail },
							 { "path", snapshot.runtimePath },
							 { "sha256", snapshot.runtimeHash },
							 { "version", snapshot.runtimeVersion },
							 { "proxyHits", snapshot.runtimeProxyHits },
							 { "proxyInstalled", snapshot.runtimeProxyInstalled },
							 { "successfulFrames", snapshot.runtimeSuccessfulFrames },
							 { "ngxResult", snapshot.ngxResult },
							 { "parameterCore", {
													{ "path", snapshot.parameterCorePath },
													{ "sha256", snapshot.parameterCoreHash },
													{ "trust", snapshot.parameterCoreTrust },
													{ "source", snapshot.parameterCoreSource },
												} },
						 } },
			{ "renderer", {
							  { "lastCompletedStage", NeuralRendering::ToString(snapshot.lastCompletedStage) },
							  { "failureStage", NeuralRendering::ToString(snapshot.failureStage) },
							  { "lastResult", snapshot.lastResult },
							  { "featureSlot", snapshot.featureSlot },
							  { "failureFeatureSlot", snapshot.failureFeatureSlot },
							  { "frame", snapshot.frameId },
							  { "generation", snapshot.generation },
							  { "successes", snapshot.successes },
							  { "failures", snapshot.failures },
							  { "featureUpscaling", snapshot.featureUpscaling },
							  { "failureLatched", snapshot.failureLatched },
							  { "quarantined", snapshot.quarantined },
							  { "outputCommitted", snapshot.outputCommitted },
						  } },
			{ "dimensions", {
								{ "color", { { "width", snapshot.colorWidth }, { "height", snapshot.colorHeight } } },
								{ "guide", { { "width", snapshot.guideWidth }, { "height", snapshot.guideHeight } } },
								{ "output", { { "width", snapshot.outputWidth }, { "height", snapshot.outputHeight } } },
							} },
			{ "resources", {
							   { "formats", {
												{ "color", snapshot.colorFormat },
												{ "depthSource", snapshot.depthSourceFormat },
												{ "depthView", snapshot.depthViewFormat },
												{ "motionVectors", snapshot.motionVectorFormat },
												{ "output", snapshot.outputFormat },
											} },
						   } },
			{ "counters", {
							  { "attempts", snapshot.counters.attempts },
							  { "successes", snapshot.counters.successes },
							  { "failures", snapshot.counters.failures },
							  { "validationFailures", snapshot.counters.validationFailures },
							  { "interopInitializations", snapshot.counters.interopInitializations },
							  { "runtimeInitializations", snapshot.counters.runtimeInitializations },
							  { "resourceRebuilds", snapshot.counters.resourceRebuilds },
							  { "depthGuideCopies", snapshot.counters.depthGuideCopies },
							  { "featureEvaluations", snapshot.counters.featureEvaluations },
							  { "outputCommits", snapshot.counters.outputCommits },
							  { "stereoAttempts", snapshot.counters.stereoAttempts },
							  { "stereoSuccesses", snapshot.counters.stereoSuccesses },
							  { "stereoFailures", snapshot.counters.stereoFailures },
							  { "callerHistoryResets", snapshot.counters.callerHistoryResets },
							  { "forcedHistoryResets", snapshot.counters.forcedHistoryResets },
							  { "discontinuousHistoryResets", snapshot.counters.discontinuousHistoryResets },
							  { "resetAttempts", snapshot.counters.resetAttempts },
							  { "resetSuccesses", snapshot.counters.resetSuccesses },
							  { "resetFailures", snapshot.counters.resetFailures },
							  { "deviceRemovals", snapshot.counters.deviceRemovals },
							  { "quarantines", snapshot.counters.quarantines },
							  { "latchedBypasses", snapshot.counters.latchedBypasses },
							  { "quarantinedBypasses", snapshot.counters.quarantinedBypasses },
							  { "failuresByStage", std::move(failuresByStage) },
						  } },
			{ "performance", {
								 { "d3d11PreparationCpuEnqueueSamples", snapshot.performance.d3d11PreparationCpuEnqueueSamples },
								 { "d3d11PreparationCpuEnqueueMicroseconds", snapshot.performance.d3d11PreparationCpuEnqueueMicroseconds },
								 { "lastD3D11PreparationCpuEnqueueMicroseconds", snapshot.performance.lastD3D11PreparationCpuEnqueueMicroseconds },
								 { "maximumD3D11PreparationCpuEnqueueMicroseconds", snapshot.performance.maximumD3D11PreparationCpuEnqueueMicroseconds },
								 { "outputCommitCpuEnqueueSamples", snapshot.performance.outputCommitCpuEnqueueSamples },
								 { "outputCommitCpuEnqueueMicroseconds", snapshot.performance.outputCommitCpuEnqueueMicroseconds },
								 { "lastOutputCommitCpuEnqueueMicroseconds", snapshot.performance.lastOutputCommitCpuEnqueueMicroseconds },
								 { "maximumOutputCommitCpuEnqueueMicroseconds", snapshot.performance.maximumOutputCommitCpuEnqueueMicroseconds },
								 { "commandSubmissions", snapshot.performance.commandSubmissions },
								 { "mainCommandSubmissions", snapshot.performance.mainCommandSubmissions },
								 { "submitCommandSubmissions", snapshot.performance.submitCommandSubmissions },
								 { "stereoCommandSubmissions", snapshot.performance.stereoCommandSubmissions },
								 { "mainStereoCommandSubmissions", snapshot.performance.mainStereoCommandSubmissions },
								 { "submitStereoCommandSubmissions", snapshot.performance.submitStereoCommandSubmissions },
								 { "backpressureWaits", snapshot.performance.backpressureWaits },
								 { "backpressureWaitMicroseconds", snapshot.performance.backpressureWaitMicroseconds },
								 { "maximumBackpressureWaitMicroseconds", snapshot.performance.maximumBackpressureWaitMicroseconds },
								 { "featureGpuSamples", snapshot.performance.featureGpuSamples },
								 { "featureGpuReadbackFailures", snapshot.performance.featureGpuReadbackFailures },
								 { "featureGpuMicroseconds", snapshot.performance.featureGpuMicroseconds },
								 { "mainFeatureGpuSamples", snapshot.performance.mainFeatureGpuSamples },
								 { "mainFeatureGpuMicroseconds", snapshot.performance.mainFeatureGpuMicroseconds },
								 { "submitFeatureGpuSamples", snapshot.performance.submitFeatureGpuSamples },
								 { "submitFeatureGpuMicroseconds", snapshot.performance.submitFeatureGpuMicroseconds },
								 { "unexpectedFeatureSlotMaskSamples", snapshot.performance.unexpectedFeatureSlotMaskSamples },
								 { "lastFeatureGpuMicroseconds", snapshot.performance.lastFeatureGpuMicroseconds },
								 { "maximumFeatureGpuMicroseconds", snapshot.performance.maximumFeatureGpuMicroseconds },
								 { "lastFeaturePixelCount", snapshot.performance.lastFeaturePixelCount },
								 { "lastFeatureEvaluationCount", snapshot.performance.lastFeatureEvaluationCount },
								 { "lastFeatureSlotMask", snapshot.performance.lastFeatureSlotMask },
							 } },
			{ "slots", std::move(slots) },
		};
	}

	json BuildStatus(Upscaling& a_upscaling)
	{
		const auto controller = a_upscaling.GetVRRenderScaleTransitionSnapshot();
		const auto session = a_upscaling.GetVRRenderScaleStressSessionSnapshot();
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
			{ "neuralRendering", NeuralRenderingStatusJson(a_upscaling) },
			{ "loadPresentationProbe", a_upscaling.BuildVRLoadPresentationProbeStatus() },
			{ "session", {
							 { "id", session.sessionID },
							 { "active", session.active },
							 { "startFrame", session.startFrame },
							 { "endFrame", session.endFrame },
							 { "retainedEvents", session.count },
							 { "overwrittenEvents", session.overwrittenEvents },
						 } },
			{ "controller", {
								{ "state", Upscaling::GetVRRenderScaleTransitionStateName(controller.state) },
								{ "targetEpoch", controller.targetEpoch },
								{ "revision", controller.revision },
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
													  { "systemCommitAdmissionLimitBytes", controller.relatchPlan.systemCommitAdmissionLimitBytes },
													  { "pressureCleanupRequired", controller.relatchPlan.pressureCleanupRequired },
													  { "projectedResidencyGuardActive", controller.relatchPlan.projectedResidencyGuardActive },
													  { "projectedResidencyPostTrimRelaxed", controller.relatchPlan.projectedResidencyPostTrimRelaxed },
													  { "projectedResidencyDeferred", controller.relatchPlan.projectedResidencyDeferred },
													  { "systemCommitGuardActive", controller.relatchPlan.systemCommitGuardActive },
													  { "doorHandoffHardReserveOnly", controller.relatchPlan.doorHandoffHardReserveOnly },
													  { "systemCommitDeferred", controller.relatchPlan.systemCommitDeferred },
													  { "pressureDeferred", controller.relatchPlan.pressureDeferred },
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
														  { "settledSamples", controller.postLoadRecovery.settledSamples },
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
			output = a_build(args);
		} catch (const std::exception& e) {
			output = { { "error", "invalid request" }, { "detail", e.what() } };
		} catch (...) {
			output = { { "error", "unknown handler error" } };
		}

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

	struct NeuralRenderingConfigurationRequest
	{
		std::optional<bool> enabled;
		std::optional<bool> outerDLSS;
		std::optional<bool> mainRoute;
		std::optional<bool> submitRoute;
		std::optional<bool> resetEveryFrame;
		std::optional<bool> forceFeatureUpscaling;
		std::optional<uint32_t> motionVectorScaleMode;
		std::optional<uint32_t> submitPairBoundaryMode;
		std::optional<bool> batchedStereo;
		std::optional<bool> directCommit;
		std::optional<uint32_t> preset;
		std::optional<float> intensity;
		std::optional<float> localToneStrength;
		std::optional<float> localStructureStrength;
		std::optional<float> skinStructureStrength;
		std::optional<uint32_t> style;
		std::optional<bool> useAutoMask;
		std::optional<bool> uiCorrection;

		[[nodiscard]] bool HasTuningOverrides() const noexcept
		{
			return intensity || localToneStrength || localStructureStrength ||
			       skinStructureStrength || style;
		}

		[[nodiscard]] bool HasMutation() const noexcept
		{
			return enabled || outerDLSS || mainRoute || submitRoute ||
			       resetEveryFrame || forceFeatureUpscaling ||
			       motionVectorScaleMode || submitPairBoundaryMode ||
			       batchedStereo || directCommit || preset || HasTuningOverrides() ||
			       useAutoMask || uiCorrection;
		}
	};

	bool TryParseNeuralRenderingConfiguration(
		const json& a_args,
		NeuralRenderingConfigurationRequest& a_request,
		json& a_error)
	{
		const auto parseBoolean = [&](const char* a_name, std::optional<bool>& a_output) {
			const auto value = a_args.find(a_name);
			if (value == a_args.end())
				return true;
			if (!value->is_boolean()) {
				a_error = { { "error", std::format("{} must be a boolean", a_name) } };
				return false;
			}
			a_output = value->get<bool>();
			return true;
		};
		if (!parseBoolean("enabled", a_request.enabled) ||
			!parseBoolean("outerDLSS", a_request.outerDLSS) ||
			!parseBoolean("mainRoute", a_request.mainRoute) ||
			!parseBoolean("submitRoute", a_request.submitRoute) ||
			!parseBoolean("resetEveryFrame", a_request.resetEveryFrame) ||
			!parseBoolean("forceFeatureUpscaling", a_request.forceFeatureUpscaling) ||
			!parseBoolean("batchedStereo", a_request.batchedStereo) ||
			!parseBoolean("directCommit", a_request.directCommit) ||
			!parseBoolean("useAutoMask", a_request.useAutoMask) ||
			!parseBoolean("uiCorrection", a_request.uiCorrection)) {
			return false;
		}

		if (const auto scaleMode = a_args.find("motionVectorScaleMode");
			scaleMode != a_args.end()) {
			if (!scaleMode->is_string()) {
				a_error = { { "error", "motionVectorScaleMode must be a string" } };
				return false;
			}
			const auto name = scaleMode->get<std::string>();
			if (name == "full_eye")
				a_request.motionVectorScaleMode = 0u;
			else if (name == "center")
				a_request.motionVectorScaleMode = 1u;
			else {
				a_error = {
					{ "error", "motionVectorScaleMode must be 'full_eye' or 'center'" },
					{ "errorCode", "nr_motion_vector_scale_mode_invalid" },
				};
				return false;
			}
		}

		if (const auto boundaryMode = a_args.find("submitPairBoundaryMode");
			boundaryMode != a_args.end()) {
			if (!boundaryMode->is_string()) {
				a_error = { { "error", "submitPairBoundaryMode must be a string" } };
				return false;
			}
			const auto name = boundaryMode->get<std::string>();
			if (name == "current")
				a_request.submitPairBoundaryMode = 0u;
			else if (name == "observe")
				a_request.submitPairBoundaryMode = 1u;
			else if (name == "require")
				a_request.submitPairBoundaryMode = 2u;
			else {
				a_error = {
					{ "error", "submitPairBoundaryMode must be 'current', 'observe', or 'require'" },
					{ "errorCode", "nr_submit_pair_boundary_mode_invalid" },
				};
				return false;
			}
		}

		const auto mergeModeAxis = [&](std::optional<bool>& a_axis, bool a_value, const char* a_name) {
			if (a_axis && *a_axis != a_value) {
				a_error = {
					{ "error", std::format("{} contradicts another neural implementation selector", a_name) },
					{ "errorCode", "nr_implementation_conflict" },
				};
				return false;
			}
			a_axis = a_value;
			return true;
		};
		if (const auto implementation = a_args.find("implementationMode");
			implementation != a_args.end()) {
			if (!implementation->is_string()) {
				a_error = { { "error", "implementationMode must be a string" } };
				return false;
			}
			const auto parsedImplementation = NeuralRendering::ParseImplementationName(
				implementation->get<std::string>());
			if (!parsedImplementation) {
				a_error = {
					{ "error", "implementationMode is not a canonical neural implementation lane" },
					{ "errorCode", "nr_implementation_invalid" },
				};
				return false;
			}
			if (!mergeModeAxis(
					a_request.batchedStereo,
					parsedImplementation->batchedStereo,
					"implementationMode") ||
				!mergeModeAxis(
					a_request.directCommit,
					parsedImplementation->directCommit,
					"implementationMode")) {
				return false;
			}
		}
		if (const auto submission = a_args.find("stereoSubmission");
			submission != a_args.end()) {
			if (!submission->is_string()) {
				a_error = { { "error", "stereoSubmission must be a string" } };
				return false;
			}
			const auto name = submission->get<std::string>();
			if (name != "per_eye" && name != "batched") {
				a_error = { { "error", "stereoSubmission must be 'per_eye' or 'batched'" } };
				return false;
			}
			if (!mergeModeAxis(a_request.batchedStereo, name == "batched", "stereoSubmission"))
				return false;
		}
		if (const auto commit = a_args.find("outputCommit"); commit != a_args.end()) {
			if (!commit->is_string()) {
				a_error = { { "error", "outputCommit must be a string" } };
				return false;
			}
			const auto name = commit->get<std::string>();
			if (name != "staged" && name != "direct") {
				a_error = { { "error", "outputCommit must be 'staged' or 'direct'" } };
				return false;
			}
			if (!mergeModeAxis(a_request.directCommit, name == "direct", "outputCommit"))
				return false;
		}

		if (const auto preset = a_args.find("preset"); preset != a_args.end()) {
			if (preset->is_number_unsigned()) {
				const auto requested = preset->get<uint64_t>();
				if (requested > Upscaling::kNeuralRenderingPresetMaxIndex) {
					a_error = {
						{ "error", "preset is outside 0..5" },
						{ "errorCode", "nr_preset_out_of_range" },
						{ "field", "preset" },
						{ "requested", requested },
					};
					return false;
				}
				a_request.preset = static_cast<uint32_t>(requested);
			} else if (preset->is_number_integer()) {
				const auto requested = preset->get<int64_t>();
				if (requested < 0 ||
					requested > static_cast<int64_t>(Upscaling::kNeuralRenderingPresetMaxIndex)) {
					a_error = {
						{ "error", "preset is outside 0..5" },
						{ "errorCode", "nr_preset_out_of_range" },
						{ "field", "preset" },
						{ "requested", requested },
					};
					return false;
				}
				a_request.preset = static_cast<uint32_t>(requested);
			} else {
				a_error = {
					{ "error", "preset must be an integer" },
					{ "errorCode", "nr_preset_type_invalid" },
					{ "field", "preset" },
				};
				return false;
			}
		}

		const auto parseStrength = [&](const char* a_name, std::optional<float>& a_output) {
			const auto value = a_args.find(a_name);
			if (value == a_args.end())
				return true;
			if (!value->is_number()) {
				a_error = {
					{ "error", std::format("{} must be a number", a_name) },
					{ "errorCode", "nr_tuning_type_invalid" },
					{ "field", a_name },
				};
				return false;
			}
			const double requested = value->get<double>();
			if (!std::isfinite(requested)) {
				a_error = {
					{ "error", std::format("{} must be finite", a_name) },
					{ "errorCode", "nr_tuning_non_finite" },
					{ "field", a_name },
				};
				return false;
			}
			if (requested < 0.0 || requested > 2.0) {
				a_error = {
					{ "error", std::format("{} is outside 0..2", a_name) },
					{ "errorCode", "nr_tuning_out_of_range" },
					{ "field", a_name },
					{ "requested", requested },
				};
				return false;
			}
			a_output = static_cast<float>(requested);
			return true;
		};
		if (!parseStrength("intensity", a_request.intensity) ||
			!parseStrength("localToneStrength", a_request.localToneStrength) ||
			!parseStrength("localStructureStrength", a_request.localStructureStrength) ||
			!parseStrength("skinStructureStrength", a_request.skinStructureStrength)) {
			return false;
		}

		if (const auto style = a_args.find("style"); style != a_args.end()) {
			if (style->is_number_unsigned()) {
				const auto requested = style->get<uint64_t>();
				if (requested > 3u) {
					a_error = {
						{ "error", "style is outside 0..3" },
						{ "errorCode", "nr_style_out_of_range" },
						{ "field", "style" },
						{ "requested", requested },
					};
					return false;
				}
				a_request.style = static_cast<uint32_t>(requested);
			} else if (style->is_number_integer()) {
				const auto requested = style->get<int64_t>();
				if (requested < 0 || requested > 3) {
					a_error = {
						{ "error", "style is outside 0..3" },
						{ "errorCode", "nr_style_out_of_range" },
						{ "field", "style" },
						{ "requested", requested },
					};
					return false;
				}
				a_request.style = static_cast<uint32_t>(requested);
			} else {
				a_error = {
					{ "error", "style must be an integer" },
					{ "errorCode", "nr_style_type_invalid" },
					{ "field", "style" },
				};
				return false;
			}
		}
		if (a_request.useAutoMask && !*a_request.useAutoMask) {
			a_error = {
				{ "error", "manual neural masks require an unimplemented ControlMask input" },
				{ "errorCode", "nr_manual_mask_unsupported" },
			};
			return false;
		}
		if (a_request.uiCorrection && *a_request.uiCorrection) {
			a_error = {
				{ "error", "neural UI correction requires unimplemented UI resources" },
				{ "errorCode", "nr_ui_correction_unsupported" },
			};
			return false;
		}
		if (!a_request.HasMutation()) {
			a_error = {
				{ "error", "nr_configure requires at least one neural-rendering mutation" },
				{ "errorCode", "nr_empty_configuration" },
			};
			return false;
		}
		return true;
	}

	json BuildRenderScaleResult(const json& a_args)
	{
		const std::string action = a_args.value("action", std::string("status"));
		if (action == "nr_status") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "neural-rendering diagnostics require Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				return json{
					{ "action", "nr_status" },
					{ "neuralRendering", NeuralRenderingStatusJson(upscaling) },
				};
			});
		}

		if (action == "nr_configure") {
			NeuralRenderingConfigurationRequest request;
			json error;
			if (!TryParseNeuralRenderingConfiguration(a_args, request, error))
				return error;

			return RunOnMainThread([request]() {
				if (!globals::game::isVR)
					return json{ { "error", "neural-rendering configuration requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				auto& settings = upscaling.settings;
				const auto previousSettings = settings;
				auto requestedSettings = previousSettings;
				if (request.enabled)
					requestedSettings.neuralRenderingEnabled = *request.enabled;
				if (request.outerDLSS)
					requestedSettings.foveatedOuterDLSS = *request.outerDLSS;
				if (request.mainRoute)
					requestedSettings.neuralRenderingMainRoute = *request.mainRoute;
				if (request.submitRoute)
					requestedSettings.neuralRenderingSubmitRoute = *request.submitRoute;
				if (request.resetEveryFrame)
					requestedSettings.neuralRenderingResetEveryFrame = *request.resetEveryFrame;
				if (request.forceFeatureUpscaling)
					requestedSettings.neuralRenderingForceFeatureUpscaling = *request.forceFeatureUpscaling;
				if (request.motionVectorScaleMode)
					requestedSettings.neuralRenderingMotionVectorScaleMode = *request.motionVectorScaleMode;
				if (request.submitPairBoundaryMode)
					requestedSettings.neuralRenderingSubmitPairBoundaryMode = *request.submitPairBoundaryMode;
				if (request.batchedStereo)
					requestedSettings.neuralRenderingBatchedStereo = *request.batchedStereo;
				if (request.directCommit)
					requestedSettings.neuralRenderingDirectCommit = *request.directCommit;
				if (request.preset)
					(void)Upscaling::ApplyNeuralRenderingPreset(requestedSettings, *request.preset);
				if (request.intensity)
					requestedSettings.neuralRenderingIntensity = *request.intensity;
				if (request.localToneStrength)
					requestedSettings.neuralRenderingLocalTone = *request.localToneStrength;
				if (request.localStructureStrength)
					requestedSettings.neuralRenderingLocalStructure = *request.localStructureStrength;
				if (request.skinStructureStrength)
					requestedSettings.neuralRenderingSkinStructure = *request.skinStructureStrength;
				if (request.style)
					requestedSettings.neuralRenderingStyle = *request.style;
				if (request.useAutoMask)
					requestedSettings.neuralRenderingAutoMask = *request.useAutoMask;
				if (request.uiCorrection)
					requestedSettings.neuralRenderingUICorrection = *request.uiCorrection;
				if (request.HasTuningOverrides())
					requestedSettings.neuralRenderingPreset = 0;

				const bool settingsChanged =
					Upscaling::GetNeuralRenderingSettingsKey(previousSettings) !=
					Upscaling::GetNeuralRenderingSettingsKey(requestedSettings);
				const bool enableStateChanged =
					previousSettings.neuralRenderingEnabled !=
					requestedSettings.neuralRenderingEnabled;
				const bool implementationChanged =
					previousSettings.neuralRenderingBatchedStereo != requestedSettings.neuralRenderingBatchedStereo ||
					previousSettings.neuralRenderingDirectCommit != requestedSettings.neuralRenderingDirectCommit;
				const bool outerDLSSChanged =
					previousSettings.foveatedOuterDLSS != requestedSettings.foveatedOuterDLSS;
				const bool routeChanged =
					previousSettings.neuralRenderingMainRoute != requestedSettings.neuralRenderingMainRoute ||
					previousSettings.neuralRenderingSubmitRoute != requestedSettings.neuralRenderingSubmitRoute;
				const bool resetPolicyChanged =
					previousSettings.neuralRenderingResetEveryFrame != requestedSettings.neuralRenderingResetEveryFrame;
				const bool featureUpscalingChanged =
					previousSettings.neuralRenderingForceFeatureUpscaling !=
					requestedSettings.neuralRenderingForceFeatureUpscaling;
				const bool motionVectorScaleChanged =
					previousSettings.neuralRenderingMotionVectorScaleMode !=
					requestedSettings.neuralRenderingMotionVectorScaleMode;
				const bool submitPairBoundaryChanged =
					previousSettings.neuralRenderingSubmitPairBoundaryMode !=
					requestedSettings.neuralRenderingSubmitPairBoundaryMode;
				const char* transition = !settingsChanged ? "none" :
				                         enableStateChanged ?
				                                            (requestedSettings.neuralRenderingEnabled ? "enable" : "disable") :
				                         implementationChanged     ? "implementation" :
				                         outerDLSSChanged          ? "outer_dlss" :
				                         routeChanged              ? "route" :
				                         featureUpscalingChanged   ? "feature_upscaling" :
				                         motionVectorScaleChanged  ? "motion_vector_scale" :
				                         submitPairBoundaryChanged ? "submit_pair_boundary" :
				                         resetPolicyChanged        ? "history_reset_policy" :
				                                                     "tuning";
				if (settingsChanged)
					settings = requestedSettings;
				const bool transitionSucceeded = !settingsChanged ||
				                                 upscaling.HandleNeuralRenderingSettingsTransition(
													 previousSettings,
													 "DevBench neural-rendering configuration");
				const bool resetAttempted =
					settingsChanged && (enableStateChanged || featureUpscalingChanged);
				json response{
					{ "action", "nr_configure" },
					{ "settingsChanged", settingsChanged },
					{ "noOp", !settingsChanged },
					{ "transition", transition },
					{ "transitionSucceeded", transitionSucceeded },
					{ "historyResetRequested", settingsChanged },
					{ "resetAttempted", resetAttempted },
					{ "resetSucceeded", resetAttempted ? json(transitionSucceeded) : json(nullptr) },
					{ "selectedImplementation", NeuralImplementationJson(
													requestedSettings.neuralRenderingBatchedStereo,
													requestedSettings.neuralRenderingDirectCommit) },
					{ "neuralRendering", NeuralRenderingStatusJson(upscaling) },
				};
				if (!transitionSucceeded) {
					if (enableStateChanged) {
						const bool enabling = requestedSettings.neuralRenderingEnabled;
						response["error"] = enabling ?
						                        "neural-rendering backend reset did not complete while enabling" :
						                        "neural-rendering backend reset did not complete while disabling";
						response["errorCode"] = enabling ?
						                            "nr_enable_reset_failed" :
						                            "nr_disable_reset_failed";
					} else {
						response["error"] =
							"neural-rendering backend reset did not complete during the requested settings transition";
						response["errorCode"] = "nr_settings_transition_failed";
					}
				}
				return response;
			});
		}

		if (action == "nr_cycle_modes") {
			std::optional<uint32_t> requestedIndex;
			if (const auto matrixIndex = a_args.find("matrixIndex");
				matrixIndex != a_args.end()) {
				uint64_t parsedIndex = 0;
				if (matrixIndex->is_number_unsigned()) {
					parsedIndex = matrixIndex->get<uint64_t>();
				} else if (matrixIndex->is_number_integer()) {
					const auto signedIndex = matrixIndex->get<int64_t>();
					if (signedIndex < 0) {
						return {
							{ "error", "matrixIndex must be an integer in range 0..3" },
							{ "errorCode", "nr_matrix_index_invalid" },
						};
					}
					parsedIndex = static_cast<uint64_t>(signedIndex);
				} else {
					return {
						{ "error", "matrixIndex must be an integer in range 0..3" },
						{ "errorCode", "nr_matrix_index_invalid" },
					};
				}
				if (parsedIndex >= NeuralRendering::kPipelineImplementations.size()) {
					return {
						{ "error", "matrixIndex must be an integer in range 0..3" },
						{ "errorCode", "nr_matrix_index_invalid" },
					};
				}
				requestedIndex = static_cast<uint32_t>(parsedIndex);
			}

			return RunOnMainThread([requestedIndex]() {
				if (!globals::game::isVR)
					return json{ { "error", "neural-rendering mode cycling requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				auto& settings = upscaling.settings;
				const auto previousSettings = settings;
				const uint32_t previousIndex =
					(previousSettings.neuralRenderingBatchedStereo ? 1u : 0u) |
					(previousSettings.neuralRenderingDirectCommit ? 2u : 0u);
				const uint32_t currentIndex = requestedIndex.value_or(
					(previousIndex + 1u) % NeuralRendering::kPipelineImplementations.size());
				auto requestedSettings = previousSettings;
				requestedSettings.neuralRenderingBatchedStereo = (currentIndex & 1u) != 0;
				requestedSettings.neuralRenderingDirectCommit = (currentIndex & 2u) != 0;
				const bool settingsChanged = previousIndex != currentIndex;
				if (settingsChanged)
					settings = requestedSettings;
				const bool transitionSucceeded = !settingsChanged ||
				                                 upscaling.HandleNeuralRenderingSettingsTransition(
													 previousSettings,
													 "DevBench neural-rendering matrix cycle");
				return json{
					{ "action", "nr_cycle_modes" },
					{ "previousIndex", previousIndex },
					{ "currentIndex", currentIndex },
					{ "nextIndex", (currentIndex + 1u) % NeuralRendering::kPipelineImplementations.size() },
					{ "settingsChanged", settingsChanged },
					{ "noOp", !settingsChanged },
					{ "transitionSucceeded", transitionSucceeded },
					{ "historyResetRequested", settingsChanged },
					{ "selectedLane", NeuralImplementationJson(
										  requestedSettings.neuralRenderingBatchedStereo,
										  requestedSettings.neuralRenderingDirectCommit) },
					{ "executionClaimed", false },
					{ "neuralRendering", NeuralRenderingStatusJson(upscaling) },
				};
			});
		}

		if (action == "nr_reset") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "neural-rendering reset requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				const bool resetSucceeded = NeuralRendering::Renderer::Instance().Reset();
				upscaling.RequestHistoryReset();
				json response{
					{ "action", "nr_reset" },
					{ "resetSucceeded", resetSucceeded },
					{ "historyResetRequested", true },
					{ "neuralRendering", NeuralRenderingStatusJson(upscaling) },
				};
				if (!resetSucceeded) {
					response["error"] = "neural-rendering backend reset did not complete";
					response["errorCode"] = "nr_reset_failed";
				}
				return response;
			});
		}

		if (action == "status") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale iteration control requires Skyrim VR" } };
				return json{ { "action", "status" }, { "status", BuildStatus(globals::features::upscaling) } };
			});
		}

		if (action == "record") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale iteration control requires Skyrim VR" } };
				return json{ { "action", "record" }, { "record", globals::features::upscaling.BuildVRRenderScaleIterationRecord() } };
			});
		}

		if (action == "start") {
			return RunOnMainThread([]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale iteration control requires Skyrim VR" } };
				if (!globals::state || !globals::state->IsDeveloperMode())
					return json{ { "error", "developer mode is required to start a stress capture" } };
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
				if (!globals::state || !globals::state->IsDeveloperMode())
					return json{ { "error", "developer mode is required to apply an iteration profile" } };

				auto& upscaling = globals::features::upscaling;
				const auto session = upscaling.GetVRRenderScaleStressSessionSnapshot();
				if (!session.active)
					return json{ { "error", "start a stress capture before applying an iteration profile" }, { "status", BuildStatus(upscaling) } };

				const auto before = upscaling.GetPendingVRRenderScaleDesiredProfile();
				const uint32_t dlssPreset = requestedPreset.value_or(before.dlssPreset);
				upscaling.ApplyCSMenuUpscalingTransition(
					method,
					enabled,
					qualityMode,
					dlssPreset,
					"devbench render-scale iteration",
					Upscaling::VRUpscalingTransitionOrigin::CSMenu);

				const auto after = upscaling.GetPendingVRRenderScaleDesiredProfile();
				const bool queued =
					after.pending &&
					after.requestID != 0 &&
					after.requestID != before.requestID &&
					after.origin == Upscaling::VRUpscalingTransitionOrigin::CSMenu;
				json response{
					{ "action", "apply" },
					{ "method", methodName },
					{ "enabled", enabled },
					{ "qualityMode", qualityMode },
					{ "dlssPreset", dlssPreset },
					{ "queued", queued },
					{ "requestID", queued ? after.requestID : 0 },
					{ "transitionEpoch", queued ? after.transitionEpoch : 0 },
					{ "status", BuildStatus(upscaling) },
				};
				if (!queued)
					response["note"] = "request was unchanged, applied synchronously, or rejected by transition ownership";
				return response;
			});
		}

		return {
			{ "error", "unknown action" },
			{ "action", action },
			{ "supported", json::array({ "status", "nr_status", "nr_configure", "nr_cycle_modes", "nr_reset", "record", "start", "apply", "stop", "reset", "probe_start", "probe_stop", "probe_record", "probe_reset" }) },
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

		static constexpr const char* descriptor =
			R"json({
				"description": "Control and inspect Community Shaders VR render-scale stress iterations and experimental DLSS Neural Rendering. nr_status reports runtime trust, renderer stages, resources, counters, stereo-route decisions, and per-route performance. nr_configure applies one validated main-thread settings patch. nr_cycle_modes selects or advances through the four stereo implementation lanes. nr_reset retires the NR backend and invalidates temporal history. Render-scale stress mutations require developer mode; apply additionally requires an active stress capture.",
				"inputSchema": {
					"type": "object",
					"properties": {
						"action": {
							"type": "string",
							"enum": [
								"status", "nr_status", "nr_configure", "nr_cycle_modes", "nr_reset",
								"record", "start", "apply", "stop", "reset",
								"probe_start", "probe_stop", "probe_record", "probe_reset"
							]
						},
						"method": { "type": "string", "enum": ["dlss", "fsr"] },
						"enabled": {
							"type": "boolean",
							"description": "apply or nr_configure: requested enable state."
						},
						"qualityMode": { "type": "integer", "minimum": 0, "maximum": 6 },
						"dlssPreset": { "type": "integer", "minimum": 0, "maximum": 5 },
						"outerDLSS": {
							"type": "boolean",
							"description": "nr_configure: use isolated full-eye DLSS as the foveated outer-composition base."
						},
						"mainRoute": {
							"type": "boolean",
							"description": "nr_configure: enable Feature 18 on the main render-stage route."
						},
						"submitRoute": {
							"type": "boolean",
							"description": "nr_configure: enable Feature 18 on the OpenVR submit-stage route."
						},
						"resetEveryFrame": {
							"type": "boolean",
							"description": "nr_configure: reset both-eye Feature 18 history every frame for temporal-artifact isolation."
						},
						"forceFeatureUpscaling": {
							"type": "boolean",
							"description": "nr_configure: advertise equal-size Feature 18 evaluation as upscaling for controlled comparison."
						},
						"motionVectorScaleMode": {
							"type": "string",
							"enum": ["full_eye", "center"]
						},
						"submitPairBoundaryMode": {
							"type": "string",
							"enum": ["current", "observe", "require"]
						},
						"batchedStereo": { "type": "boolean" },
						"directCommit": { "type": "boolean" },
						"implementationMode": {
							"type": "string",
							"enum": ["per_eye_staged", "batched_staged", "per_eye_direct", "batched_direct"]
						},
						"stereoSubmission": {
							"type": "string",
							"enum": ["per_eye", "batched"]
						},
						"outputCommit": {
							"type": "string",
							"enum": ["staged", "direct"]
						},
						"preset": { "type": "integer", "minimum": 0, "maximum": 5 },
						"matrixIndex": { "type": "integer", "minimum": 0, "maximum": 3 },
						"intensity": { "type": "number", "minimum": 0, "maximum": 2 },
						"localToneStrength": { "type": "number", "minimum": 0, "maximum": 2 },
						"localStructureStrength": { "type": "number", "minimum": 0, "maximum": 2 },
						"skinStructureStrength": { "type": "number", "minimum": 0, "maximum": 2 },
						"style": { "type": "integer", "minimum": 0, "maximum": 3 },
						"useAutoMask": {
							"type": "boolean",
							"description": "nr_configure: must remain true until manual ControlMask input exists."
						},
						"uiCorrection": {
							"type": "boolean",
							"description": "nr_configure: must remain false until UI resources exist."
						}
					},
					"required": ["action"]
				}
			})json";
		devBench->RegisterTool(
			"communityshaders.renderscale",
			descriptor,
			&RenderScaleToolHandler,
			nullptr);
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
