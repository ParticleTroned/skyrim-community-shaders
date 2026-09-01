#include "Features/Upscaling/VRRenderScaleDevBenchBridge.h"

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "Api/MainThreadDispatchPolicy.h"
#	include "Api/RuntimeThreadAffinity.h"
#	include "Api/ServiceRegistry.h"
#	include "BuildProvenance.h"
#	include "Diagnostics/D3DTextureLifetimeTracker.h"
#	include "Diagnostics/VRPipelineDiagnostics.h"
#	include "Features/Upscaling.h"
#	include "Features/Upscaling/VRRenderScaleQualificationPolicy.h"
#	include "Features/Upscaling/VRRenderScaleReplacementTelemetryPolicy.h"
#	include "Features/VR.h"
#	include "Globals.h"
#	include "ShaderCache.h"
#	include "State.h"
#	include "Utils/Form.h"
#	include "VRAPI/CSserviceapi.h"
#	include "VRAPI/CSupscalingapi.h"

#	include <DevBenchAPI.h>
#	include <nlohmann/json.hpp>

#	include <algorithm>
#	include <array>
#	include <atomic>
#	include <chrono>
#	include <cctype>
#	include <cmath>
#	include <cstddef>
#	include <format>
#	include <functional>
#	include <future>
#	include <limits>
#	include <memory>
#	include <mutex>
#	include <optional>
#	include <stdexcept>
#	include <string>
#	include <string_view>
#	include <thread>
#	include <vector>

namespace
{
	using json = nlohmann::json;

	constexpr auto kMainThreadTimeout = std::chrono::milliseconds(5000);
	constexpr std::size_t kDLSSDevBenchTraceDefaultReadLimit = 32;
	static_assert(kDLSSDevBenchTraceDefaultReadLimit <= Streamline::kDLSSDevBenchTraceCapacity);
	constexpr uint64_t kQualificationMaximumTimeoutMs = 120'000;
	constexpr double kQualificationFoveationFloatTolerance = 0.0001;
	constexpr unsigned int kDevBenchToolExtensionRevision = 10;
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

	bool IsFSRBackend(Upscaling::VRRenderScaleBackendKind a_backend)
	{
		return a_backend == Upscaling::VRRenderScaleBackendKind::FSRHost ||
		       a_backend == Upscaling::VRRenderScaleBackendKind::FSRRuntime ||
		       a_backend == Upscaling::VRRenderScaleBackendKind::FSR4Runtime;
	}

	bool HasCurrentVendorDispatch(
		Upscaling::UpscaleMethod a_method,
		Upscaling::VRRenderScaleBackendKind a_backend,
		uint32_t a_presentationFrame,
		uint32_t a_dispatchFrame,
		uint64_t a_dispatchSerial,
		bool a_runtimeFallback)
	{
		if (a_presentationFrame == 0 || a_dispatchFrame != a_presentationFrame)
			return false;
		if (a_method == Upscaling::UpscaleMethod::kDLSS) {
			return a_backend == Upscaling::VRRenderScaleBackendKind::DLSS &&
			       !a_runtimeFallback;
		}
		return a_method == Upscaling::UpscaleMethod::kFSR &&
		       IsFSRBackend(a_backend) && a_dispatchSerial != 0;
	}

	const char* GetPreparationEventTypeName(
		Upscaling::VRRenderScalePreparationEventType a_type)
	{
		using Type = Upscaling::VRRenderScalePreparationEventType;
		switch (a_type) {
		case Type::RequestQueued:
			return "request_queued";
		case Type::AdmissionCheck:
			return "admission_check";
		case Type::EarlyExit:
			return "early_exit";
		case Type::ShaderCacheBusyWait:
			return "shader_cache_busy_wait";
		case Type::SSSRaymarchPrewarm:
			return "sss_raymarch_prewarm";
		case Type::SSGIPrewarm:
			return "ssgi_prewarm";
		case Type::DLSSPreparation:
			return "dlss_preparation";
		case Type::FSRPreparation:
			return "fsr_preparation";
		case Type::FSR4Preparation:
			return "fsr4_preparation";
		case Type::D3DObjectCreation:
			return "d3d_object_creation";
		case Type::TotalPreparation:
			return "total_preparation";
		case Type::RequestToPrepared:
			return "request_to_prepared";
		case Type::PreparedToCreator:
			return "prepared_to_creator";
		default:
			return "unknown";
		}
	}

	const char* GetPreparationOutcomeName(
		Upscaling::VRRenderScalePreparationOutcome a_outcome)
	{
		using Outcome = Upscaling::VRRenderScalePreparationOutcome;
		switch (a_outcome) {
		case Outcome::Observed:
			return "observed";
		case Outcome::Eligible:
			return "eligible";
		case Outcome::Busy:
			return "busy";
		case Outcome::Ready:
			return "ready";
		case Outcome::NotNeeded:
			return "not_needed";
		case Outcome::Failed:
			return "failed";
		case Outcome::Superseded:
			return "superseded";
		case Outcome::DeviceChanged:
			return "device_changed";
		case Outcome::Cancelled:
			return "cancelled";
		case Outcome::ProtectedFallback:
			return "protected_fallback";
		default:
			return "unknown";
		}
	}

	json PreparationReasonNames(uint64_t a_mask)
	{
		using Reason = Upscaling::VRRenderScalePreparationReason;
		constexpr std::array reasons{
			std::pair{ Reason::InactiveRequest, "inactive_request" },
			std::pair{ Reason::WrongOrigin, "wrong_origin" },
			std::pair{ Reason::MethodIneligible, "method_ineligible" },
			std::pair{ Reason::RenderScaleDisabled, "render_scale_disabled" },
			std::pair{ Reason::QualityIneligible, "quality_ineligible" },
			std::pair{ Reason::MissingDevice, "missing_device" },
			std::pair{ Reason::InvalidDimensions, "invalid_dimensions" },
			std::pair{ Reason::DeviceLost, "device_lost" },
			std::pair{ Reason::PostLoadReset, "post_load_reset" },
			std::pair{ Reason::ProviderReset, "provider_reset" },
			std::pair{ Reason::ShaderCacheBusy, "shader_cache_busy" },
			std::pair{ Reason::AlreadyAttempted, "already_attempted" },
			std::pair{ Reason::MemoryUnavailable, "memory_unavailable" },
			std::pair{ Reason::Superseded, "superseded" },
			std::pair{ Reason::DeviceChanged, "device_changed" },
			std::pair{ Reason::ShaderFailure, "shader_failure" },
			std::pair{ Reason::ProviderFailure, "provider_failure" },
			std::pair{ Reason::NonDirectEdit, "non_direct_edit" },
		};
		json output = json::array();
		for (const auto& [reason, name] : reasons) {
			if ((a_mask & static_cast<uint64_t>(reason)) != 0)
				output.push_back(name);
		}
		return output;
	}

	json PreparationTelemetryJson(Upscaling& a_upscaling)
	{
		const auto telemetry =
			a_upscaling.GetVRRenderScalePreparationTelemetrySnapshot();
		std::vector<const Upscaling::VRRenderScalePreparationEvent*> ordered;
		ordered.reserve(telemetry.count);
		for (const auto& event : telemetry.events) {
			if (event.sequence != 0 &&
				event.sessionID == telemetry.sessionID) {
				ordered.push_back(std::addressof(event));
			}
		}
		std::sort(
			ordered.begin(),
			ordered.end(),
			[](const auto* a_left, const auto* a_right) {
				return a_left->sequence < a_right->sequence;
			});

		const auto milliseconds = [&](uint64_t a_ticks) {
			return telemetry.qpcFrequency == 0 ?
			           0.0 :
			           static_cast<double>(
						   (static_cast<long double>(a_ticks) * 1000.0L) /
						   static_cast<long double>(telemetry.qpcFrequency));
		};
		json events = json::array();
		for (const auto* event : ordered) {
			events.push_back({
				{ "sequence", event->sequence },
				{ "sessionId", event->sessionID },
				{ "requestId", event->requestID },
				{ "transitionEpoch", event->transitionEpoch },
				{ "optionsGeneration", event->optionsGeneration },
				{ "shaderDefinesGeneration", event->shaderDefinesGeneration },
				{ "deviceIdentity", static_cast<uint64_t>(event->deviceIdentity) },
				{ "frame", event->frame },
				{ "lastFrame", event->lastFrame },
				{ "occurrences", event->occurrences },
				{ "method", GetUpscaleMethodName(event->method) },
				{ "qualityMode", event->qualityMode },
				{ "dlssPreset", event->dlssPreset },
				{ "fsr4RuntimeEnabled", event->fsr4RuntimeEnabled },
				{ "renderEyeWidth", event->renderEyeWidth },
				{ "renderEyeHeight", event->renderEyeHeight },
				{ "displayEyeWidth", event->displayEyeWidth },
				{ "displayEyeHeight", event->displayEyeHeight },
				{ "event", GetPreparationEventTypeName(event->type) },
				{ "outcome", GetPreparationOutcomeName(event->outcome) },
				{ "reasonMask", event->reasonMask },
				{ "reasons", PreparationReasonNames(event->reasonMask) },
				{ "beginQpc", event->beginQpc },
				{ "endQpc", event->endQpc },
				{ "durationQpcTicks", event->durationQpcTicks },
				{ "durationMs", milliseconds(event->durationQpcTicks) },
				{ "bytecodeCompilationQpcTicks", event->bytecodeCompilationQpcTicks },
				{ "bytecodeCompilationMs", milliseconds(event->bytecodeCompilationQpcTicks) },
				{ "d3dObjectCreationQpcTicks", event->d3dObjectCreationQpcTicks },
				{ "d3dObjectCreationMs", milliseconds(event->d3dObjectCreationQpcTicks) },
			});
		}

		return {
			{ "schemaVersion", 1 },
			{ "devBenchOnly", true },
			{ "active", telemetry.active },
			{ "sessionId", telemetry.sessionID },
			{ "qpcFrequency", telemetry.qpcFrequency },
			{ "retainedEvents", telemetry.count },
			{ "capacity", telemetry.events.size() },
			{ "overwrittenEvents", telemetry.overwrittenEvents },
			{ "coalescedEvents", telemetry.coalescedEvents },
			{ "events", std::move(events) },
		};
	}

	namespace ReplacementTelemetry = VRRenderScaleReplacementTelemetryPolicy;

	ReplacementTelemetry::PreparationAdmission ClassifyPreparationAdmission(
		Upscaling::VRRenderScalePreparationOutcome a_outcome,
		uint64_t a_reasonMask)
	{
		using Outcome = Upscaling::VRRenderScalePreparationOutcome;
		if (ReplacementTelemetry::IsPreparationNotApplicable(a_reasonMask))
			return ReplacementTelemetry::PreparationAdmission::NotApplicable;
		switch (a_outcome) {
		case Outcome::Observed:
		case Outcome::Eligible:
			return ReplacementTelemetry::PreparationAdmission::Eligible;
		case Outcome::Busy:
			return ReplacementTelemetry::PreparationAdmission::Busy;
		case Outcome::Ready:
		case Outcome::NotNeeded:
			return ReplacementTelemetry::PreparationAdmission::Ready;
		case Outcome::ProtectedFallback:
			return ReplacementTelemetry::PreparationAdmission::ProtectedFallback;
		case Outcome::Failed:
		case Outcome::Superseded:
		case Outcome::DeviceChanged:
		case Outcome::Cancelled:
			return ReplacementTelemetry::PreparationAdmission::Failed;
		default:
			return ReplacementTelemetry::PreparationAdmission::NotApplicable;
		}
	}

	struct OwnedMutationBoundary
	{
		bool valid = false;
		uint64_t stressSessionID = 0;
		uint64_t transitionID = 0;
		uint64_t ownershipToken = 0;
		uint64_t requestID = 0;
		uint64_t transitionEpoch = 0;
		uint32_t contractGeneration = 0;
		uintptr_t deviceIdentity = 0;
		uint32_t frame = 0;
		uint64_t tick = 0;
		std::string source;
	};

	ReplacementTelemetry::PresentationDisposition ToAuditDisposition(
		Upscaling::VRRenderScalePresentationPath a_path);

	json ReplacementPresentationEvidenceJson(
		Upscaling& a_upscaling,
		const Upscaling::VRRenderScaleTransitionSnapshot& a_controller,
		const OwnedMutationBoundary& a_boundary,
		uint64_t a_physicalMutationEpoch,
		uint64_t a_physicalSerializationEpoch,
		uint64_t a_tick,
		uint32_t a_frame)
	{
		const auto replacement = [&]() -> const Upscaling::VRRenderScaleProfileSnapshot* {
			for (const auto* profile : {
					 std::addressof(a_controller.requested),
					 std::addressof(a_controller.applying) }) {
				if (profile->valid &&
					profile->transitionEpoch == a_controller.targetEpoch) {
					return profile;
				}
			}
			return nullptr;
		}();
		const uint64_t replacementRequestID = replacement ? replacement->requestID : 0;
		const uint64_t replacementTransitionEpoch =
			replacement ? replacement->transitionEpoch : a_controller.targetEpoch;
		const uint32_t replacementContractGeneration =
			replacement ? replacement->contractGeneration : 0;
		const uint64_t replacementDeviceIdentity = replacement ?
		                                               static_cast<uint64_t>(
														   reinterpret_cast<uintptr_t>(globals::d3d::device)) :
		                                               0;
		const auto admission =
			a_upscaling.GetVRRenderScalePreparationAdmissionSnapshot(
				replacementRequestID,
				replacementTransitionEpoch);
		const auto preparationAdmission = admission.observed ?
		                                      ClassifyPreparationAdmission(admission.outcome, admission.reasonMask) :
		                                      ReplacementTelemetry::PreparationAdmission::NotApplicable;

		const auto& left = a_controller.presentation.eyes[0];
		const auto& right = a_controller.presentation.eyes[1];
		const auto& currentProfile = a_controller.stable.valid ?
		                                 a_controller.stable :
		                                 a_controller.applied;
		const bool commonStereoIdentity =
			left.valid && right.valid && left.path == right.path &&
			left.frame == right.frame &&
			left.compositorCycleToken != 0 &&
			left.compositorCycleToken == right.compositorCycleToken &&
			left.transitionEpoch == right.transitionEpoch &&
			left.contractGeneration == right.contractGeneration &&
			left.method == right.method &&
			left.deviceIdentity != 0 &&
			left.deviceIdentity == right.deviceIdentity &&
			left.resourceRevision != 0 &&
			left.resourceRevision == right.resourceRevision &&
			!left.loadingOrMenuContext && !right.loadingOrMenuContext &&
			!left.transitionCooldown && !right.transitionCooldown;
		State::RenderTargetResourcePublicationDiagnostics resourcePublication{};
		if (globals::state) {
			resourcePublication =
				globals::state->GetCurrentMainRenderTargetResourcePublicationDiagnostics();
		}
		const bool currentProfileMatches = currentProfile.valid &&
		                                   left.transitionEpoch == currentProfile.transitionEpoch &&
		                                   left.contractGeneration == currentProfile.contractGeneration &&
		                                   left.method == currentProfile.method;
		const bool observedDimensions =
			left.inputWidth != 0 && left.inputHeight != 0 &&
			left.outputWidth != 0 && left.outputHeight != 0 &&
			left.inputWidth == right.inputWidth &&
			left.inputHeight == right.inputHeight &&
			left.outputWidth == right.outputWidth &&
			left.outputHeight == right.outputHeight;
		const bool profileDimensionsMatch = currentProfileMatches &&
		                                    left.inputWidth == currentProfile.renderEyeWidth &&
		                                    left.inputHeight == currentProfile.renderEyeHeight &&
		                                    left.outputWidth == currentProfile.displayEyeWidth &&
		                                    left.outputHeight == currentProfile.displayEyeHeight;
		const bool exactNativeWithoutProfile = !currentProfile.valid &&
		                                       left.path == Upscaling::VRRenderScalePresentationPath::NativeOriginal &&
		                                       right.path == Upscaling::VRRenderScalePresentationPath::NativeOriginal &&
		                                       left.inputWidth == left.outputWidth &&
		                                       left.inputHeight == left.outputHeight;
		const bool currentContractMatches =
			currentProfileMatches || exactNativeWithoutProfile;
		const bool exactDimensions = observedDimensions &&
		                             (profileDimensionsMatch || exactNativeWithoutProfile);
		const bool nativeDimensions = exactDimensions &&
		                              left.inputWidth == left.outputWidth &&
		                              left.inputHeight == left.outputHeight;
		const bool vendorBackendCoherent =
			left.vendorBackend != Upscaling::VRRenderScaleBackendKind::None &&
			left.vendorBackend == right.vendorBackend;
		const bool dlssBackend =
			left.vendorBackend == Upscaling::VRRenderScaleBackendKind::DLSS;
		const bool fsrBackend = IsFSRBackend(left.vendorBackend);
		const bool vendorDispatchProven =
			ReplacementTelemetry::HasCoherentVendorDispatch({
				.backendCoherent = vendorBackendCoherent,
				.dispatchFramesCurrent =
					HasCurrentVendorDispatch(
						left.method, left.vendorBackend, left.frame,
						left.vendorDispatchFrame, left.vendorDispatchSerial,
						left.vendorRuntimeFallback) &&
					HasCurrentVendorDispatch(
						right.method, right.vendorBackend, right.frame,
						right.vendorDispatchFrame, right.vendorDispatchSerial,
						right.vendorRuntimeFallback),
				.runtimeFallbackCoherent =
					left.vendorRuntimeFallback == right.vendorRuntimeFallback,
				.dlssBackend = dlssBackend,
				.fsrBackend = fsrBackend,
				.runtimeFallback = left.vendorRuntimeFallback,
				.leftDispatchSerial = left.vendorDispatchSerial,
				.rightDispatchSerial = right.vendorDispatchSerial,
				.sharedFSRDispatchRequired =
					left.path == Upscaling::VRRenderScalePresentationPath::NativeOriginal,
			});
		const auto proofKind = ReplacementTelemetry::ClassifyPresentationProof({
			.coherentStereoCycle = commonStereoIdentity,
			.currentProfileMatches = currentContractMatches,
			.publicationCurrent = resourcePublication.current,
			.exactDimensions = exactDimensions &&
		                       left.vendorBackend == right.vendorBackend,
			.nativeDimensions = nativeDimensions,
			.vendorDispatchProven = vendorDispatchProven,
			.renderScaleDisabled = exactNativeWithoutProfile ||
		                           (currentProfile.valid &&
									   !currentProfile.renderScaleModeEnabled),
			.foveatedVendorDisabled =
				!a_upscaling.UseActiveFoveatedPeripheryTAAProfile(),
			.staleVendorGenerationAbsent =
				left.vendorBackend == Upscaling::VRRenderScaleBackendKind::None &&
				right.vendorBackend == Upscaling::VRRenderScaleBackendKind::None &&
				left.vendorDispatchFrame == 0 && right.vendorDispatchFrame == 0 &&
				left.vendorDispatchSerial == 0 && right.vendorDispatchSerial == 0 &&
				!left.vendorRuntimeFallback && !right.vendorRuntimeFallback,
			.completedOutputStronglyOwned = left.resourceRevision != 0 &&
		                                    left.resourceRevision == right.resourceRevision &&
		                                    left.deviceIdentity != 0 && left.deviceIdentity == right.deviceIdentity,
			.disposition = ToAuditDisposition(left.path),
		});
		const bool currentPresentationProven =
			proofKind != ReplacementTelemetry::PresentationProofKind::None;
		const uint32_t providerResourceGeneration =
			left.method == Upscaling::UpscaleMethod::kDLSS ?
				a_controller.dlssLifecycle.runtimeGeneration :
			left.method == Upscaling::UpscaleMethod::kFSR ?
				a_controller.fsrLifecycle.runtimeGeneration :
				0;
		const bool creatorEntered =
			a_physicalMutationEpoch != 0 ||
			a_physicalSerializationEpoch != 0 ||
			a_controller.physicalPhase ==
				Upscaling::VRRenderScalePhysicalPhase::CreatorEntered ||
			a_controller.physicalPhase ==
				Upscaling::VRRenderScalePhysicalPhase::TableChanged ||
			a_controller.physicalPhase ==
				Upscaling::VRRenderScalePhysicalPhase::Reconciled ||
			a_controller.physicalPhase ==
				Upscaling::VRRenderScalePhysicalPhase::ContractPublished;
		const uint32_t presentationFrame = left.valid ? left.frame : a_frame;
		const bool physicalMutationStarted =
			ReplacementTelemetry::IsAtOrAfterMutationBoundary(
				presentationFrame,
				a_tick,
				{
					.valid = a_boundary.valid,
					.frame = a_boundary.frame,
					.qpcTick = a_boundary.tick,
				});
		ReplacementTelemetry::MutationAdmissionFacts mutationFacts{
			.hasReplacement = replacement != nullptr,
			.superseded = a_controller.metrics.current.valid &&
			              a_controller.metrics.current.transitionEpoch == replacementTransitionEpoch &&
			              a_controller.metrics.current.superseded,
			.failed = (a_controller.dlssLifecycle.transitionEpoch == replacementTransitionEpoch &&
						  a_controller.dlssLifecycle.phase ==
							  Upscaling::VRVendorRuntimeLifecyclePhase::Failed) ||
			          (a_controller.fsrLifecycle.transitionEpoch == replacementTransitionEpoch &&
						  a_controller.fsrLifecycle.phase ==
							  Upscaling::VRVendorRuntimeLifecyclePhase::Failed),
			.physicalMutationStarted = physicalMutationStarted,
			.recoveryActive = a_controller.postLoadRecovery.active,
			.memoryDeferred = a_controller.relatchPlan.valid &&
			                  (a_controller.relatchPlan.projectedResidencyDeferred ||
								  a_controller.relatchPlan.systemCommitDeferred ||
								  a_controller.relatchPlan.pressureDeferred),
			.shaderDeferred = globals::shaderCache &&
			                  globals::shaderCache->IsCompiling(),
			.providerDeferred =
				(a_controller.dlssLifecycle.transitionEpoch == replacementTransitionEpoch &&
					a_controller.dlssLifecycle.phase ==
						Upscaling::VRVendorRuntimeLifecyclePhase::WaitingForDrain) ||
				(a_controller.fsrLifecycle.transitionEpoch == replacementTransitionEpoch &&
					a_controller.fsrLifecycle.phase ==
						Upscaling::VRVendorRuntimeLifecyclePhase::WaitingForDrain),
			.workGateDeferred = a_upscaling.GetVRVendorWorkGateSnapshot().lifecycleMutationDeferred,
			.cleanupDebt = a_controller.memoryTrim.pending ||
			               a_controller.retirement.pendingSets != 0 ||
			               a_controller.engineTargetRetirement.pending,
			.preparing = a_controller.state ==
			             Upscaling::VRRenderScaleTransitionState::Preparing,
			.queued = a_controller.state ==
			              Upscaling::VRRenderScaleTransitionState::Requested ||
			          a_controller.state ==
			              Upscaling::VRRenderScaleTransitionState::WaitingForSafePoint,
		};
		const auto mutationAdmission =
			ReplacementTelemetry::ClassifyMutationAdmission(mutationFacts);
		const bool mutationAdmissionBlocked =
			mutationAdmission == ReplacementTelemetry::MutationAdmission::MemoryDeferred ||
			mutationAdmission == ReplacementTelemetry::MutationAdmission::ShaderDeferred ||
			mutationAdmission == ReplacementTelemetry::MutationAdmission::ProviderDeferred ||
			mutationAdmission == ReplacementTelemetry::MutationAdmission::WorkGateDeferred ||
			mutationAdmission == ReplacementTelemetry::MutationAdmission::PreMutationRecovery ||
			mutationAdmission == ReplacementTelemetry::MutationAdmission::Failed ||
			mutationAdmission == ReplacementTelemetry::MutationAdmission::Superseded;
		const uint64_t mutationAdmissionReasonMask =
			(mutationFacts.memoryDeferred ? 1ull << 0 : 0) |
			(mutationFacts.shaderDeferred ? 1ull << 1 : 0) |
			(mutationFacts.providerDeferred ? 1ull << 2 : 0) |
			(mutationFacts.workGateDeferred ? 1ull << 3 : 0) |
			(mutationFacts.recoveryActive ? 1ull << 4 : 0) |
			(mutationFacts.failed ? 1ull << 5 : 0) |
			(mutationFacts.superseded ? 1ull << 6 : 0) |
			(mutationFacts.cleanupDebt ? 1ull << 7 : 0);
		json mutationAdmissionReasons = json::array();
		for (const auto& [mask, name] : std::array{
				 std::pair{ 1ull << 0, "memory_deferred" },
				 std::pair{ 1ull << 1, "shader_deferred" },
				 std::pair{ 1ull << 2, "provider_deferred" },
				 std::pair{ 1ull << 3, "work_gate_deferred" },
				 std::pair{ 1ull << 4, "recovery" },
				 std::pair{ 1ull << 5, "failed" },
				 std::pair{ 1ull << 6, "superseded" },
				 std::pair{ 1ull << 7, "cleanup_debt" } }) {
			if ((mutationAdmissionReasonMask & mask) != 0)
				mutationAdmissionReasons.push_back(name);
		}
		ReplacementTelemetry::MutationExpectation mutationExpectation =
			ReplacementTelemetry::MutationExpectation::Unknown;
		std::string_view mutationExpectationReason = "replacement_not_observed";
		if (replacement && a_controller.relatchPlan.valid &&
			a_controller.relatchPlan.transitionEpoch == replacementTransitionEpoch) {
			using Action = Upscaling::VRRenderScaleRelatchAction;
			constexpr uint32_t mutationActions =
				static_cast<uint32_t>(Action::RecreateRenderTargets) |
				static_cast<uint32_t>(Action::ResetDLSS) |
				static_cast<uint32_t>(Action::ResetFSR) |
				static_cast<uint32_t>(Action::RecreateFSR) |
				static_cast<uint32_t>(Action::RefreshPresentation);
			mutationExpectation =
				(a_controller.relatchPlan.actionMask & mutationActions) != 0 ?
					ReplacementTelemetry::MutationExpectation::Required :
					ReplacementTelemetry::MutationExpectation::NotRequired;
			mutationExpectationReason =
				mutationExpectation == ReplacementTelemetry::MutationExpectation::Required ?
					"physical_relatch_plan" :
					"compatible_contract_reuse";
		}
		const bool commonPath = left.valid && right.valid && left.path == right.path;
		const char* selectedDisposition =
			commonPath ?
				Upscaling::GetVRRenderScalePresentationPathName(left.path) :
			left.valid || right.valid ?
				"mixed" :
				"none";
		const auto eyeJson = [a_tick](const Upscaling::VRRenderScalePresentationEyeSnapshot& a_eye) {
			return json{
				{ "valid", a_eye.valid },
				{ "path", Upscaling::GetVRRenderScalePresentationPathName(a_eye.path) },
				{ "frame", a_eye.frame },
				{ "qpcTick", a_tick },
				{ "generation", a_eye.contractGeneration },
				{ "deviceIdentity", static_cast<uint64_t>(a_eye.deviceIdentity) },
				{ "resourceRevision", a_eye.resourceRevision },
				{ "renderWidth", a_eye.inputWidth },
				{ "renderHeight", a_eye.inputHeight },
				{ "displayWidth", a_eye.outputWidth },
				{ "displayHeight", a_eye.outputHeight },
				{ "compositorCycleToken", a_eye.compositorCycleToken },
				{ "transitionEpoch", a_eye.transitionEpoch },
				{ "method", GetUpscaleMethodName(a_eye.method) },
				{ "backend", GetBackendName(a_eye.vendorBackend) },
				{ "vendorDispatchFrame", a_eye.vendorDispatchFrame },
				{ "vendorDispatchSerial", a_eye.vendorDispatchSerial },
				{ "vendorRuntimeFallback", a_eye.vendorRuntimeFallback },
				{ "loadingOrMenuContext", a_eye.loadingOrMenuContext },
				{ "transitionCooldown", a_eye.transitionCooldown },
			};
		};
		const json presentationProof{
			{ "proven", currentPresentationProven },
			{ "kind", ReplacementTelemetry::GetProofKindName(proofKind) },
			{ "frame", currentPresentationProven ? json(left.frame) : json(nullptr) },
			{ "qpcTick", currentPresentationProven ? json(a_tick) : json(nullptr) },
			{ "method", currentPresentationProven ?
							json(GetUpscaleMethodName(left.method)) :
							json(nullptr) },
			{ "methodValue", currentPresentationProven ?
								 json(static_cast<uint32_t>(left.method)) :
								 json(nullptr) },
			{ "qualityMode", currentPresentationProven && currentProfile.valid ?
								 json(currentProfile.qualityMode) :
								 json(nullptr) },
			{ "renderScaleMode", currentPresentationProven ?
									 json(currentProfile.valid &&
										  currentProfile.renderScaleModeEnabled) :
									 json(nullptr) },
			{ "backend", currentPresentationProven ?
							 json(GetBackendName(left.vendorBackend)) :
							 json(nullptr) },
			{ "backendValue", currentPresentationProven ?
								  json(static_cast<uint32_t>(left.vendorBackend)) :
								  json(nullptr) },
			{ "vendorDispatchProven", currentPresentationProven ?
										  json(vendorDispatchProven) :
										  json(nullptr) },
			{ "requestId", currentPresentationProven ?
							   json(currentProfile.requestID) :
							   json(nullptr) },
			{ "transitionEpoch", currentPresentationProven ?
									 json(left.transitionEpoch) :
									 json(nullptr) },
			{ "contractGeneration", currentPresentationProven ?
										json(left.contractGeneration) :
										json(nullptr) },
			{ "providerRuntimeGeneration", currentPresentationProven ?
											   json(providerResourceGeneration) :
											   json(nullptr) },
			{ "resourcePublicationGeneration", currentPresentationProven ?
												   json(resourcePublication.publishedGeneration) :
												   json(nullptr) },
			{ "resourceRevision", currentPresentationProven ?
									  json(left.resourceRevision) :
									  json(nullptr) },
			{ "renderWidth", currentPresentationProven ?
								 json(left.inputWidth) :
								 json(nullptr) },
			{ "renderHeight", currentPresentationProven ?
								  json(left.inputHeight) :
								  json(nullptr) },
			{ "displayWidth", currentPresentationProven ?
								  json(left.outputWidth) :
								  json(nullptr) },
			{ "displayHeight", currentPresentationProven ?
								   json(left.outputHeight) :
								   json(nullptr) },
			{ "deviceIdentity", currentPresentationProven ?
									json(static_cast<uint64_t>(left.deviceIdentity)) :
									json(nullptr) },
			{ "compositorCycleToken", currentPresentationProven ?
										  json(left.compositorCycleToken) :
										  json(nullptr) },
			{ "leftEye", eyeJson(left) },
			{ "rightEye", eyeJson(right) },
		};

		return {
			{ "schemaRevision", 14 },
			{ "observationFrame", a_frame },
			{ "presentationProof", presentationProof },
			{ "currentPresentationProven", currentPresentationProven },
			{ "currentPresentationGeneration",
				currentPresentationProven ? json(left.contractGeneration) : json(nullptr) },
			{ "currentPresentationDeviceIdentity",
				currentPresentationProven ?
					json(static_cast<uint64_t>(left.deviceIdentity)) :
					json(nullptr) },
			{ "currentPresentationResourceRevision",
				currentPresentationProven ? json(left.resourceRevision) : json(nullptr) },
			{ "currentPresentationProviderGeneration",
				currentPresentationProven ? json(providerResourceGeneration) : json(nullptr) },
			{ "replacementRequestId", replacementRequestID },
			{ "replacementTransitionEpoch", replacementTransitionEpoch },
			{ "replacementContractGeneration", replacementContractGeneration },
			{ "replacementDeviceIdentity", replacementDeviceIdentity },
			{ "replacementAdmissionObserved", admission.observed },
			{ "preparationAdmission", {
										  { "status", ReplacementTelemetry::GetPreparationAdmissionName(
														  preparationAdmission) },
										  { "observed", admission.observed },
										  { "sequence", admission.observed ? json(admission.sequence) : json(nullptr) },
										  { "frame", admission.observed ? json(admission.frame) : json(nullptr) },
										  { "outcome", admission.observed ?
														   json(GetPreparationOutcomeName(admission.outcome)) :
														   json("not_observed") },
										  { "reasonMask", admission.reasonMask },
										  { "reasons", PreparationReasonNames(admission.reasonMask) },
									  } },
			{ "replacementMutationAdmission", {
												  { "status", ReplacementTelemetry::GetMutationAdmissionName(mutationAdmission) },
												  { "blocked", mutationAdmissionBlocked },
												  { "reasonMask", mutationAdmissionReasonMask },
												  { "reasons", mutationAdmissionReasons },
											  } },
			{ "replacementAdmissionBlocked", mutationAdmissionBlocked },
			{ "replacementAdmissionSequence", admission.observed ? json(admission.sequence) : json(nullptr) },
			{ "replacementAdmissionFrame", admission.observed ? json(admission.frame) : json(nullptr) },
			{ "replacementAdmissionOutcome", admission.observed ? json(GetPreparationOutcomeName(admission.outcome)) : json("not_observed") },
			{ "replacementAdmissionBlockReasonMask", mutationAdmissionReasonMask },
			{ "replacementAdmissionBlockReasons", mutationAdmissionReasons },
			{ "mutationExpectation", ReplacementTelemetry::GetMutationExpectationName(mutationExpectation) },
			{ "mutationExpectationReason", mutationExpectationReason },
			{ "physicalMutationStarted", physicalMutationStarted },
			{ "physicalMutationSource", physicalMutationStarted ? json(a_boundary.source) : json("none") },
			{ "physicalMutationEpoch", a_physicalMutationEpoch },
			{ "physicalSerializationEpoch", a_physicalSerializationEpoch },
			{ "engineTargetCreatorEntered", creatorEntered },
			{ "selectedPresentationDisposition", selectedDisposition },
			{ "completedOutputReuse", commonPath && left.path == Upscaling::VRRenderScalePresentationPath::ValidatedPresentationHold },
			{ "completedOutputOwnershipProven", currentPresentationProven && left.path == Upscaling::VRRenderScalePresentationPath::ValidatedPresentationHold },
			{ "leftEye", eyeJson(left) },
			{ "rightEye", eyeJson(right) },
		};
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
			{ "preparationOptionsGeneration", a_profile.preparationOptionsGeneration },
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
			{ "foveatedVendorDispatch", a_profile.resources.foveatedVendorDispatch },
			{ "peripheryTAA", a_profile.resources.peripheryTAA },
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
		const bool active =
			a_upscaling.IsVRRenderScaleCPUPerformanceTelemetryActive();
		const uint64_t sessionID =
			a_upscaling.GetVRRenderScaleCPUPerformanceSessionID();
		const uint64_t startFrame = value(Counter::WindowStartFrame);

		return {
			{ "schemaVersion", 1 },
			{ "devBenchOnly", true },
			{ "active", active },
			{ "sessionId", sessionID },
			{ "state", active ? "active" : (sessionID != 0 ? "stopped" : "reset") },
			{ "window", {
							{ "initialized", sessionID != 0 },
							{ "startFrame", startFrame },
							{ "currentFrame", frame },
							{ "elapsedFrames", sessionID != 0 && frame >= startFrame ? frame - startFrame : 0u },
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

	json BuildAdapterIdentity()
	{
		if (!globals::d3d::device) {
			return {
				{ "available", false },
				{ "reason", "d3d11_device_unavailable" },
			};
		}

		winrt::com_ptr<IDXGIDevice> dxgiDevice;
		if (FAILED(globals::d3d::device->QueryInterface(IID_PPV_ARGS(dxgiDevice.put())))) {
			return {
				{ "available", false },
				{ "reason", "dxgi_device_query_failed" },
			};
		}
		winrt::com_ptr<IDXGIAdapter> adapter;
		if (FAILED(dxgiDevice->GetAdapter(adapter.put()))) {
			return {
				{ "available", false },
				{ "reason", "dxgi_adapter_query_failed" },
			};
		}
		DXGI_ADAPTER_DESC description{};
		if (FAILED(adapter->GetDesc(&description))) {
			return {
				{ "available", false },
				{ "reason", "dxgi_adapter_description_failed" },
			};
		}

		LARGE_INTEGER driverVersion{};
		const bool driverVersionAvailable =
			SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(ID3D11Device), &driverVersion));
		const auto high = static_cast<uint32_t>(driverVersion.HighPart);
		const auto low = static_cast<uint32_t>(driverVersion.LowPart);
		const std::string driverVersionText = driverVersionAvailable ?
		                                          std::format("{}.{}.{}.{}", high >> 16, high & 0xffffu, low >> 16, low & 0xffffu) :
		                                          std::string{};
		return {
			{ "available", true },
			{ "description", globals::state ? globals::state->adapterDescription : std::string{} },
			{ "vendorId", description.VendorId },
			{ "deviceId", description.DeviceId },
			{ "subsystemId", description.SubSysId },
			{ "revision", description.Revision },
			{ "luidHigh", description.AdapterLuid.HighPart },
			{ "luidLow", description.AdapterLuid.LowPart },
			{ "driverVersionAvailable", driverVersionAvailable },
			{ "driverVersion", driverVersionText },
			{ "driverVersionRaw", static_cast<uint64_t>(driverVersion.QuadPart) },
		};
	}

	json BuildStatus(Upscaling& a_upscaling)
	{
		const auto controller = a_upscaling.GetVRRenderScaleTransitionSnapshot();
		const auto session = a_upscaling.GetVRRenderScaleStressSessionSnapshot();
		const auto vendorWorkGate = a_upscaling.GetVRVendorWorkGateSnapshot();
		const auto& resolutionPlan = a_upscaling.GetRuntimeResolutionPlan();
		const auto mainPassDiagnostics =
			a_upscaling.GetVRMainPassDispatchDiagnosticSnapshot();
		const auto nativeRestorePreparation =
			a_upscaling.GetVRNativeRestorePreparationDiagnosticSnapshot();
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
			{ "adapter", BuildAdapterIdentity() },
			{ "modeStatus", Upscaling::GetVRRenderScaleModeStatusName(a_upscaling.GetVRRenderScaleModeStatus()) },
			{ "runtimeRouting", {
									{ "configuredMethod", GetUpscaleMethodName(a_upscaling.GetConfiguredUpscaleMethodForTransition()) },
									{ "runtimeMethod", GetUpscaleMethodName(a_upscaling.GetRuntimeUpscaleMethod()) },
									{ "runtimeQualityMode", a_upscaling.GetRuntimeQualityMode() },
									{ "renderScaleRequested", a_upscaling.IsRenderScaleModeRequested() },
									{ "renderScaleLatched", a_upscaling.IsVRRenderScaleModeLatched() },
									{ "perfModeActive", a_upscaling.IsPerfModeActive() },
									{ "presentationUpscalingActive", a_upscaling.IsPresentationUpscalingActive() },
								} },
			{ "runtimeResolutionPlan", {
										   { "method", GetUpscaleMethodName(resolutionPlan.upscaleMethod) },
										   { "owner", std::string(magic_enum::enum_name(resolutionPlan.owner)) },
										   { "outputTarget", std::string(magic_enum::enum_name(resolutionPlan.outputTarget)) },
										   { "qualityMode", resolutionPlan.qualityMode },
										   { "engineRenderWidth", resolutionPlan.engineRenderSize.x },
										   { "engineRenderHeight", resolutionPlan.engineRenderSize.y },
										   { "finalOutputWidth", resolutionPlan.finalOutputSize.x },
										   { "finalOutputHeight", resolutionPlan.finalOutputSize.y },
										   { "vendorMethod", resolutionPlan.vendorMethod },
										   { "menuContextActive", resolutionPlan.menuContextActive },
									   } },
			{ "mainPassDispatch", {
									  { "lastStage", std::string(magic_enum::enum_name(mainPassDiagnostics.lastStage)) },
									  { "lastFrame", mainPassDiagnostics.lastFrame },
									  { "callCount", mainPassDiagnostics.callCount },
									  { "encodeAttemptCount", mainPassDiagnostics.encodeAttemptCount },
									  { "encodeSuccessCount", mainPassDiagnostics.encodeSuccessCount },
									  { "vendorResetBlockedCount", mainPassDiagnostics.vendorResetBlockedCount },
									  { "fidelityAttemptCount", mainPassDiagnostics.fidelityAttemptCount },
									  { "fidelitySuccessCount", mainPassDiagnostics.fidelitySuccessCount },
								  } },
			{ "nativeRestorePreparation", {
											  { "rejectMask", nativeRestorePreparation.rejectMask },
											  { "frame", nativeRestorePreparation.frame },
											  { "compositorCycleToken", nativeRestorePreparation.compositorCycleToken },
											  { "commitOutcome", nativeRestorePreparation.commitOutcome },
											  { "commitFrame", nativeRestorePreparation.commitFrame },
											  { "commitCompositorCycleToken", nativeRestorePreparation.commitCompositorCycleToken },
											  { "commitAttemptCount", nativeRestorePreparation.commitAttemptCount },
											  { "commitSuccessCount", nativeRestorePreparation.commitSuccessCount },
										  } },
			{ "cpuPerformance", CPUPerformanceJson(a_upscaling) },
			{ "preparation", PreparationTelemetryJson(a_upscaling) },
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

	uint64_t OptionalNonNegativeIntegerOrZero(
		const json& a_object,
		const char* a_name) noexcept
	{
		try {
			const auto value = a_object.find(a_name);
			uint64_t result = 0;
			return value != a_object.end() &&
			               TryGetNonNegativeInteger(*value, result) ?
			           result :
			           0;
		} catch (...) {
			return 0;
		}
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

	namespace QualificationPolicy = VRRenderScaleQualificationPolicy;
	using QualificationTarget = QualificationPolicy::TargetProfile;
	using QualificationMilestone = QualificationPolicy::Milestone;

	struct QualificationExpectedCell
	{
		std::optional<uint32_t> formID;
		std::optional<std::string> editorID;
	};

	struct QualificationFoveationTarget
	{
		bool foveatedVendorDispatch = false;
		double foveatedCenterArea = 0.0;
		bool peripheryTAAEnable = false;
		double peripheryTAACenterArea = 0.0;
		double peripheryTAAOuterScale = 0.0;
	};

	struct QualificationDiagnostics
	{
		uint64_t stressNextSequence = 0;
		uint64_t stressRetryEvents = 0;
		uint64_t stressFailureEvents = 0;
		uint64_t stressOverwrittenEvents = 0;
		uint64_t stressCoalescedEvents = 0;
		uint64_t vendorEvaluatedEyeObservations = 0;
		uint64_t presentationStretchEyeObservations = 0;
		uint64_t vendorFailureStretchEyeObservations = 0;
		uint64_t boundsMismatchFallbackEyeObservations = 0;
		uint64_t presentationStretchEpisodes = 0;
		uint64_t presentationStretchCompletedEpisodes = 0;
		uint64_t presentationStretchTimedCompletedEpisodes = 0;
		uint64_t presentationStretchCompletedFrames = 0;
		uint64_t presentationStretchCompletedQpcTicks = 0;
		bool presentationStretchEpisodeActive = false;
		uint64_t presentationStretchActiveFrames = 0;
		uint64_t presentationStretchActiveQpcTicks = 0;
		bool presentationStretchActiveQpcTimingAvailable = false;
		uint64_t maximumPresentationStretchFrames = 0;
		uint64_t maximumPresentationStretchQpcTicks = 0;
		uint64_t presentationStretchQpcFrequency = 0;
		uint64_t fidelityTransitionEpoch = 0;
		uint32_t fidelityContractGeneration = 0;
		uint64_t fidelityMismatches = 0;
		uint64_t transitionFailures = 0;
		uint64_t outOfMemoryFailures = 0;
		uint64_t deviceLostFailures = 0;
		uint64_t dlssLifecycleFailures = 0;
		uint64_t fsrLifecycleFailures = 0;
		uint64_t memoryTrimFailures = 0;
		uint64_t retirementFenceFailures = 0;
		uint64_t dlssTraceRecords = 0;
		uint64_t dlssTraceDroppedRecords = 0;
		uint64_t dlssTraceConstantsFailures = 0;
		uint64_t dlssTraceEvaluateFailures = 0;
		bool dlssTraceActive = false;
		uint64_t dlssTraceSessionID = 0;
	};

	struct QualificationBaseline
	{
		uint64_t beginTick = 0;
		uint64_t tickFrequency = 0;
		uint32_t beginFrame = 0;
		uint32_t sourceCellFormID = 0;
		std::string sourceCellEditorID;
		uint64_t stressSessionID = 0;
		uint64_t apiStateRevision = 0;
		uint64_t controllerRevision = 0;
		CSX::UpscalingAPI::Snapshot001 apiSnapshot{};
		Upscaling::VRRenderScaleProfileSnapshot applied{};
		Upscaling::VRRenderScaleProfileSnapshot stable{};
		QualificationFoveationTarget foveationSettings{};
		QualificationDiagnostics diagnostics{};
	};

	struct QualificationTransition
	{
		uint64_t transitionID = 0;
		uint64_t ownershipToken = 0;
		std::string ownerID;
		bool ready = false;
		uint64_t dispatchTick = 0;
		uint32_t dispatchFrame = 0;
		std::optional<std::string> cocCellEditorID;
		QualificationMilestone milestone = QualificationMilestone::Strict;
		QualificationPolicy::FirstObservation presentationStable{};
		QualificationPolicy::FirstObservation cleanupDrained{};
		QualificationPolicy::FirstObservation strictSatisfied{};
		json dispatchPresentationEvidence = nullptr;
		json lastPreMutationEvidence = nullptr;
		json blockedPreMutationEvidence = nullptr;
		json firstPhysicalMutationEvidence = nullptr;
		json firstPostMutationEvidence = nullptr;
		json firstNewGenerationProvenEvidence = nullptr;
		json mutationNotRequiredTerminalProofEvidence = nullptr;
		std::optional<QualificationTarget> expectedTarget;
		uint64_t expectedReplacementRequestID = 0;
		uint64_t expectedReplacementTransitionEpoch = 0;
		uint32_t expectedReplacementContractGeneration = 0;
		uintptr_t expectedReplacementDeviceIdentity = 0;
		ReplacementTelemetry::MutationExpectation mutationExpectation =
			ReplacementTelemetry::MutationExpectation::Unknown;
		std::string mutationExpectationReason = "replacement_not_observed";
		ReplacementTelemetry::AuditState presentationAudit{};
		bool waitInProgress = false;
		QualificationBaseline baseline{};
		std::shared_ptr<std::atomic_bool> cancelled =
			std::make_shared<std::atomic_bool>(false);
	};

	QualificationPolicy::Profile QualificationProfile(
		const Upscaling::VRRenderScaleProfileSnapshot& a_profile);

	void MergeQualificationMutationExpectation(
		QualificationTransition& a_transition,
		ReplacementTelemetry::MutationExpectation a_expectation,
		std::string_view a_reason)
	{
		const auto merged = ReplacementTelemetry::MergeMutationExpectation(
			a_transition.mutationExpectation, a_expectation);
		if (merged == a_transition.mutationExpectation)
			return;
		a_transition.mutationExpectation = merged;
		a_transition.mutationExpectationReason = a_reason;
	}

	void SeedNativeTargetMutationExpectation(
		QualificationTransition& a_transition,
		const QualificationTarget& a_target,
		const Upscaling::VRRenderScaleTransitionSnapshot& a_controller)
	{
		const auto& baseline = a_transition.baseline;
		const auto& current = baseline.stable.valid ?
		                          baseline.stable :
		                          baseline.applied;
		const json proof =
			a_transition.dispatchPresentationEvidence.is_object() &&
					a_transition.dispatchPresentationEvidence.contains("presentationProof") ?
				a_transition.dispatchPresentationEvidence["presentationProof"] :
				json(nullptr);
		const bool exactNativePresentation = proof.is_object() &&
		                                     proof.value("proven", false) &&
		                                     proof.value("kind", std::string{}) == "exact_native_presentation";
		const bool currentPhysicalContractActive =
			current.active || current.renderScaleModeEnabled ||
			current.resources.active;

		const auto* replacement = [&]() -> const Upscaling::VRRenderScaleProfileSnapshot* {
			for (const auto* profile : {
					 std::addressof(a_controller.requested),
					 std::addressof(a_controller.applying) }) {
				if (profile->valid && profile->transitionEpoch != 0 &&
					profile->transitionEpoch == a_controller.targetEpoch &&
					QualificationPolicy::MatchesTarget(
						QualificationProfile(*profile), a_target)) {
					return profile;
				}
			}
			return nullptr;
		}();
		const bool relatchDecisionKnown = replacement &&
		                                  a_controller.relatchPlan.valid &&
		                                  a_controller.relatchPlan.transitionEpoch ==
		                                      replacement->transitionEpoch;
		using Action = Upscaling::VRRenderScaleRelatchAction;
		constexpr uint32_t engineMutationActions =
			static_cast<uint32_t>(Action::RecreateRenderTargets);
		constexpr uint32_t providerInvalidationActions =
			static_cast<uint32_t>(Action::ResetDLSS) |
			static_cast<uint32_t>(Action::ResetFSR) |
			static_cast<uint32_t>(Action::RecreateFSR) |
			static_cast<uint32_t>(Action::RefreshPresentation);
		const bool currentVendorPresentation = proof.is_object() &&
		                                       proof.value("proven", false) &&
		                                       proof.value("kind", std::string{}) == "exact_vendor_evaluation";
		const bool relatchRequiresMutation = relatchDecisionKnown &&
		                                     ((a_controller.relatchPlan.actionMask & engineMutationActions) != 0 ||
												 (currentVendorPresentation &&
													 (a_controller.relatchPlan.actionMask &
														 providerInvalidationActions) != 0));
		const bool physicalMutationRecorded =
			!a_transition.firstPhysicalMutationEvidence.is_null();
		const auto expectation =
			ReplacementTelemetry::DetermineNativeTargetMutationExpectation({
				.targetRenderScaleMode = a_target.renderScaleMode,
				.currentContractKnown = current.valid || exactNativePresentation,
				.currentPhysicalContractActive = currentPhysicalContractActive,
				.relatchDecisionKnown = relatchDecisionKnown,
				.relatchRequiresMutation = relatchRequiresMutation,
				.physicalMutationRecorded = physicalMutationRecorded,
			});
		const std::string reason =
			expectation == ReplacementTelemetry::MutationExpectation::Required ?
				physicalMutationRecorded      ? a_transition.mutationExpectationReason :
				currentPhysicalContractActive ? "scaled_contract_retirement" :
												"physical_relatch_plan" :
			expectation == ReplacementTelemetry::MutationExpectation::NotRequired ?
				relatchDecisionKnown ? "compatible_contract_reuse" :
									   "native_contract_reuse" :
				"replacement_not_observed";
		MergeQualificationMutationExpectation(
			a_transition, expectation, reason);
	}

	struct QualificationStore
	{
		std::mutex mutex;
		std::optional<QualificationTransition> active;
		json lastEvidence = nullptr;
		uint64_t nextOwnershipToken = 1;
	};

	QualificationStore& GetQualificationStore()
	{
		static QualificationStore store;
		return store;
	}

	OwnedMutationBoundary ReadOwnedMutationBoundary(
		uint64_t a_transitionID,
		uint64_t a_ownershipToken)
	{
		OwnedMutationBoundary boundary{};
		auto& store = GetQualificationStore();
		std::lock_guard lock(store.mutex);
		if (!store.active ||
			!QualificationPolicy::OwnsTransitionInstance(
				store.active->transitionID,
				store.active->ownershipToken,
				a_transitionID,
				a_ownershipToken) ||
			!store.active->firstPhysicalMutationEvidence.is_object()) {
			return boundary;
		}

		const auto& evidence = store.active->firstPhysicalMutationEvidence;
		boundary.stressSessionID =
			OptionalNonNegativeIntegerOrZero(evidence, "stressSessionId");
		boundary.transitionID =
			OptionalNonNegativeIntegerOrZero(evidence, "qualificationTransitionId");
		boundary.ownershipToken =
			OptionalNonNegativeIntegerOrZero(evidence, "ownershipToken");
		boundary.requestID =
			OptionalNonNegativeIntegerOrZero(evidence, "replacementRequestId");
		boundary.transitionEpoch =
			OptionalNonNegativeIntegerOrZero(evidence, "replacementTransitionEpoch");
		boundary.contractGeneration = static_cast<uint32_t>(
			OptionalNonNegativeIntegerOrZero(
				evidence, "replacementContractGeneration"));
		boundary.deviceIdentity = static_cast<uintptr_t>(
			OptionalNonNegativeIntegerOrZero(evidence, "replacementDeviceIdentity"));
		boundary.frame = static_cast<uint32_t>(
			OptionalNonNegativeIntegerOrZero(evidence, "frame"));
		boundary.tick = OptionalNonNegativeIntegerOrZero(evidence, "tick");
		boundary.source = evidence.value("physicalMutationSource", std::string{});
		boundary.valid = boundary.stressSessionID != 0 &&
		                 boundary.stressSessionID ==
		                     store.active->baseline.stressSessionID &&
		                 boundary.transitionID == a_transitionID &&
		                 boundary.ownershipToken == a_ownershipToken &&
		                 boundary.requestID != 0 &&
		                 boundary.transitionEpoch != 0 &&
		                 boundary.deviceIdentity != 0 &&
		                 boundary.frame != 0 && boundary.tick != 0 &&
		                 !boundary.source.empty();
		return boundary;
	}

	uint64_t AllocateQualificationOwnershipTokenLocked(
		QualificationStore& a_store) noexcept
	{
		const uint64_t token = a_store.nextOwnershipToken;
		a_store.nextOwnershipToken =
			token == std::numeric_limits<uint64_t>::max() ? 1 : token + 1;
		return token;
	}

	uint64_t QueryQualificationTick()
	{
		LARGE_INTEGER value{};
		return QueryPerformanceCounter(&value) ?
		           static_cast<uint64_t>(value.QuadPart) :
		           0;
	}

	uint64_t GetQualificationTickFrequency()
	{
		static const uint64_t frequency = [] {
			LARGE_INTEGER value{};
			return QueryPerformanceFrequency(&value) ?
			           static_cast<uint64_t>(value.QuadPart) :
			           0;
		}();
		return frequency;
	}

	ReplacementTelemetry::PresentationDisposition ToAuditDisposition(
		Upscaling::VRRenderScalePresentationPath a_path)
	{
		using Disposition = ReplacementTelemetry::PresentationDisposition;
		switch (a_path) {
		case Upscaling::VRRenderScalePresentationPath::VendorEvaluated:
			return Disposition::ExactVendor;
		case Upscaling::VRRenderScalePresentationPath::NativeOriginal:
			return Disposition::ExactNative;
		case Upscaling::VRRenderScalePresentationPath::ValidatedPresentationHold:
			return Disposition::CompletedOutputHold;
		case Upscaling::VRRenderScalePresentationPath::PresentationStretch:
			return Disposition::PresentationStretch;
		case Upscaling::VRRenderScalePresentationPath::VendorFailureStretch:
			return Disposition::VendorFailureStretch;
		case Upscaling::VRRenderScalePresentationPath::BoundsMismatchOriginalFallback:
			return Disposition::BoundsMismatchOriginal;
		default:
			return Disposition::None;
		}
	}

	json AuditCycleJson(const ReplacementTelemetry::CompleteCycle& a_cycle)
	{
		if (!a_cycle.valid)
			return nullptr;
		return {
			{ "frame", a_cycle.frame },
			{ "qpcTick", a_cycle.qpcTick },
			{ "leftFrame", a_cycle.leftFrame },
			{ "leftQpcTick", a_cycle.leftQpcTick },
			{ "rightFrame", a_cycle.rightFrame },
			{ "rightQpcTick", a_cycle.rightQpcTick },
			{ "compositorCycleToken", a_cycle.compositorCycleToken },
			{ "requestId", a_cycle.requestID },
			{ "transitionEpoch", a_cycle.transitionEpoch },
			{ "contractGeneration", a_cycle.contractGeneration },
			{ "providerRuntimeGeneration", a_cycle.providerGeneration },
			{ "resourcePublicationGeneration", a_cycle.publicationGeneration },
			{ "resourceRevision", a_cycle.resourceRevision },
			{ "deviceIdentity", static_cast<uint64_t>(a_cycle.deviceIdentity) },
			{ "renderWidth", a_cycle.renderWidth },
			{ "renderHeight", a_cycle.renderHeight },
			{ "displayWidth", a_cycle.displayWidth },
			{ "displayHeight", a_cycle.displayHeight },
			{ "methodValue", a_cycle.method },
			{ "qualityMode", a_cycle.qualityMode },
			{ "renderScaleMode", a_cycle.renderScaleMode },
			{ "backendValue", a_cycle.backend },
			{ "leftVendorDispatchFrame", a_cycle.leftVendorDispatchFrame },
			{ "leftVendorDispatchSerial", a_cycle.leftVendorDispatchSerial },
			{ "rightVendorDispatchFrame", a_cycle.rightVendorDispatchFrame },
			{ "rightVendorDispatchSerial", a_cycle.rightVendorDispatchSerial },
			{ "vendorRuntimeFallback", a_cycle.vendorRuntimeFallback },
			{ "vendorDispatchProven", a_cycle.vendorDispatchProven },
			{ "sharedVendorDispatchRequired",
				a_cycle.sharedVendorDispatchRequired },
			{ "disposition", ReplacementTelemetry::GetDispositionName(
								 a_cycle.disposition) },
			{ "submitted", a_cycle.submitted },
			{ "coherent", a_cycle.coherent },
			{ "beforeMutation", a_cycle.beforeMutation },
			{ "afterMutation", a_cycle.afterMutation },
			{ "boundarySpanning", a_cycle.boundarySpanning },
			{ "exactCurrent", a_cycle.exactCurrent },
			{ "exactReplacement", a_cycle.exactReplacement },
			{ "blockedPreMutation", a_cycle.blockedPreMutation },
			{ "loadingOrMenuContext", a_cycle.loadingOrMenuContext },
			{ "transitionCooldown", a_cycle.transitionCooldown },
		};
	}

	json PresentationAuditJson(const ReplacementTelemetry::AuditState& a_state)
	{
		const auto& counters = a_state.counters;
		const std::uint64_t incompleteStereoCycles =
			ReplacementTelemetry::CountIncompleteStereoCycles(a_state);
		json beforeDispositions = json::object();
		json afterDispositions = json::object();
		for (std::size_t index = 0;
			index < counters.dispositionsBeforeMutation.size(); ++index) {
			const auto disposition = static_cast<
				ReplacementTelemetry::PresentationDisposition>(index);
			beforeDispositions[std::string(
				ReplacementTelemetry::GetDispositionName(disposition))] =
				counters.dispositionsBeforeMutation[index];
			afterDispositions[std::string(
				ReplacementTelemetry::GetDispositionName(disposition))] =
				counters.dispositionsAfterMutation[index];
		}
		const auto offender = [](const ReplacementTelemetry::FirstOffender& a_value) {
			return a_value.valid ? AuditCycleJson(a_value.cycle) : json(nullptr);
		};
		return {
			{ "active", a_state.active },
			{ "evidenceComplete", a_state.evidenceComplete },
			{ "retentionOverflow", a_state.retentionOverflow },
			{ "physicalMutationObserved", a_state.physicalMutationObserved },
			{ "ownerTransitionId", a_state.ownerTransitionID },
			{ "ownerToken", a_state.ownerToken },
			{ "eyeObservations", counters.eyeObservations },
			{ "partialEyeObservations", incompleteStereoCycles },
			{ "incompleteStereoCycles", incompleteStereoCycles },
			{ "completeStereoCyclesBeforeMutation", counters.completeStereoCyclesBeforeMutation },
			{ "blockedPreMutationCycles", counters.blockedPreMutationCycles },
			{ "exactPreviousGenerationCycles", counters.exactPreviousGenerationCycles },
			{ "suppressedExactPreviousGenerationCycles", counters.suppressedExactPreviousGenerationCycles },
			{ "presentationStretchCyclesBeforeMutation", counters.presentationStretchCyclesBeforeMutation },
			{ "blackKeepaliveCyclesBeforeMutation", counters.blackKeepaliveCyclesBeforeMutation },
			{ "quarantineCyclesBeforeMutation", counters.quarantineCyclesBeforeMutation },
			{ "completeStereoCyclesAfterMutation", counters.completeStereoCyclesAfterMutation },
			{ "boundarySpanningStereoCycles", counters.boundarySpanningStereoCycles },
			{ "oldGenerationEvaluationsAfterMutation", counters.oldGenerationEvaluationsAfterMutation },
			{ "oldGenerationCompletedOutputReuseAfterMutation", counters.oldGenerationCompletedOutputReuseAfterMutation },
			{ "mixedOrUnprovenStereoPairsSubmitted", counters.mixedOrUnprovenStereoPairsSubmitted },
			{ "firstExactNewGenerationCycles", counters.firstExactNewGenerationCycles },
			{ "dispositionCounts", {
									   { "beforeMutation", std::move(beforeDispositions) },
									   { "afterMutation", std::move(afterDispositions) },
								   } },
			{ "violations", {
								{ "preMutationExactPresentationSuppressed", counters.preMutationExactPresentationSuppressed },
								{ "preMutationStretchWithoutMutation", counters.preMutationStretchWithoutMutation },
								{ "postMutationOldGenerationPresented", counters.postMutationOldGenerationPresented },
								{ "postMutationUnprovenStereoSubmitted", counters.postMutationUnprovenStereoSubmitted },
								{ "firstPreMutationExactPresentationSuppressed", offender(counters.firstPreMutationExactPresentationSuppressed) },
								{ "firstPreMutationStretchWithoutMutation", offender(counters.firstPreMutationStretchWithoutMutation) },
								{ "firstPostMutationOldGenerationPresented", offender(counters.firstPostMutationOldGenerationPresented) },
								{ "firstPostMutationUnprovenStereoSubmitted", offender(counters.firstPostMutationUnprovenStereoSubmitted) },
							} },
		};
	}

	std::string NormalizeEditorID(std::string_view a_editorID)
	{
		std::string normalized;
		normalized.reserve(a_editorID.size());
		for (const unsigned char character : a_editorID)
			normalized.push_back(static_cast<char>(std::tolower(character)));
		return normalized;
	}

	bool EditorIDsEqual(std::string_view a_left, std::string_view a_right)
	{
		return NormalizeEditorID(a_left) == NormalizeEditorID(a_right);
	}

	QualificationPolicy::Method ToQualificationMethod(
		CSX::UpscalingAPI::Method a_method)
	{
		switch (a_method) {
		case CSX::UpscalingAPI::Method::kNone:
			return QualificationPolicy::Method::None;
		case CSX::UpscalingAPI::Method::kTAA:
			return QualificationPolicy::Method::TAA;
		case CSX::UpscalingAPI::Method::kDLSS:
			return QualificationPolicy::Method::DLSS;
		case CSX::UpscalingAPI::Method::kFSR:
			return QualificationPolicy::Method::FSR;
		default:
			return QualificationPolicy::Method::Unknown;
		}
	}

	QualificationPolicy::Method ToQualificationMethod(
		Upscaling::UpscaleMethod a_method)
	{
		switch (a_method) {
		case Upscaling::UpscaleMethod::kNONE:
			return QualificationPolicy::Method::None;
		case Upscaling::UpscaleMethod::kTAA:
			return QualificationPolicy::Method::TAA;
		case Upscaling::UpscaleMethod::kDLSS:
			return QualificationPolicy::Method::DLSS;
		case Upscaling::UpscaleMethod::kFSR:
			return QualificationPolicy::Method::FSR;
		default:
			return QualificationPolicy::Method::Unknown;
		}
	}

	const char* GetQualificationMethodName(QualificationPolicy::Method a_method)
	{
		switch (a_method) {
		case QualificationPolicy::Method::None:
			return "none";
		case QualificationPolicy::Method::TAA:
			return "taa";
		case QualificationPolicy::Method::DLSS:
			return "dlss";
		case QualificationPolicy::Method::FSR:
			return "fsr";
		default:
			return "unknown";
		}
	}

	const char* GetDLSSProfileName(uint32_t a_profile)
	{
		static constexpr std::array<const char*, 6> names{ "J", "K", "L", "M", "F", "E" };
		return a_profile < names.size() ? names[a_profile] : "unknown";
	}

	json QualificationTargetJson(const QualificationTarget& a_target)
	{
		json output{
			{ "method", GetQualificationMethodName(a_target.method) },
			{ "qualityMode", a_target.qualityMode },
			{ "renderScaleMode", a_target.renderScaleMode },
		};
		if (a_target.matchDLSSProfile)
			output["dlssProfile"] = GetDLSSProfileName(a_target.dlssProfile);
		if (a_target.matchFSRRuntime)
			output["fsrRuntime"] = a_target.fsr4Runtime ? "fsr4" : "fsr3";
		return output;
	}

	bool TryParseQualificationMilestone(
		const json& a_args,
		QualificationMilestone& a_milestone,
		json& a_error)
	{
		a_milestone = QualificationMilestone::Strict;
		if (!a_args.contains("milestone"))
			return true;
		if (!a_args["milestone"].is_string() ||
			!QualificationPolicy::TryParseMilestone(
				a_args["milestone"].get<std::string>(), a_milestone)) {
			a_error = {
				{ "error", "milestone must be strict, presentation, or cleanup" },
				{ "errorCode", "invalid_milestone" },
			};
			return false;
		}
		return true;
	}

	bool TryParseQualificationTarget(
		const json& a_args,
		std::optional<QualificationTarget>& a_target,
		json& a_error)
	{
		if (!a_args.contains("target")) {
			a_target.reset();
			return true;
		}
		if (!a_args["target"].is_object()) {
			a_error = { { "error", "qualification_wait target must be an object when supplied" },
				{ "errorCode", "invalid_target" } };
			return false;
		}
		a_target.emplace();
		const auto& target = a_args["target"];
		for (const auto& [name, value] : target.items()) {
			(void)value;
			if (!QualificationPolicy::IsTargetPropertyAllowed(name)) {
				a_error = { { "error", std::format("unknown target property '{}'", name) },
					{ "errorCode", "unknown_target_property" } };
				return false;
			}
		}
		if (!target.contains("method") || !target["method"].is_string() ||
			!target.contains("qualityMode") || !target["qualityMode"].is_number_integer() ||
			!target.contains("renderScaleMode") || !target["renderScaleMode"].is_boolean()) {
			a_error = { { "error", "target requires method, qualityMode, and renderScaleMode" },
				{ "errorCode", "invalid_target" } };
			return false;
		}

		const std::string method = target["method"].get<std::string>();
		if (method == "none")
			a_target->method = QualificationPolicy::Method::None;
		else if (method == "taa")
			a_target->method = QualificationPolicy::Method::TAA;
		else if (method == "dlss")
			a_target->method = QualificationPolicy::Method::DLSS;
		else if (method == "fsr")
			a_target->method = QualificationPolicy::Method::FSR;
		else {
			a_error = { { "error", "target.method must be 'none', 'taa', 'dlss', or 'fsr'" },
				{ "errorCode", "invalid_target_method" } };
			return false;
		}

		uint64_t qualityMode = 0;
		if (!TryGetNonNegativeInteger(target["qualityMode"], qualityMode) ||
			qualityMode > QualificationPolicy::kMaximumQualityMode) {
			a_error = { { "error", "target.qualityMode is outside 0..6" },
				{ "errorCode", "invalid_target_quality" } };
			return false;
		}
		a_target->qualityMode = static_cast<uint32_t>(qualityMode);
		a_target->renderScaleMode = target["renderScaleMode"].get<bool>();

		if (target.contains("dlssProfile")) {
			if (a_target->method != QualificationPolicy::Method::DLSS ||
				!target["dlssProfile"].is_string()) {
				a_error = { { "error", "target.dlssProfile is valid only for DLSS" },
					{ "errorCode", "invalid_dlss_profile" } };
				return false;
			}
			const std::string profile = target["dlssProfile"].get<std::string>();
			static constexpr std::array<std::string_view, 6> names{ "J", "K", "L", "M", "F", "E" };
			const auto found = std::find(names.begin(), names.end(), profile);
			if (found == names.end()) {
				a_error = { { "error", "target.dlssProfile must be J, K, L, M, F, or E" },
					{ "errorCode", "invalid_dlss_profile" } };
				return false;
			}
			a_target->matchDLSSProfile = true;
			a_target->dlssProfile = static_cast<uint32_t>(std::distance(names.begin(), found));
		}

		if (target.contains("fsrRuntime")) {
			if (a_target->method != QualificationPolicy::Method::FSR ||
				!target["fsrRuntime"].is_string()) {
				a_error = { { "error", "target.fsrRuntime is valid only for FSR" },
					{ "errorCode", "invalid_fsr_runtime" } };
				return false;
			}
			const std::string runtime = target["fsrRuntime"].get<std::string>();
			if (runtime != "fsr3" && runtime != "fsr4") {
				a_error = { { "error", "target.fsrRuntime must be 'fsr3' or 'fsr4'" },
					{ "errorCode", "invalid_fsr_runtime" } };
				return false;
			}
			a_target->matchFSRRuntime = true;
			a_target->fsr4Runtime = runtime == "fsr4";
		}

		if (!QualificationPolicy::IsValidTarget(*a_target)) {
			a_error = { { "error", "target.renderScaleMode must be true exactly when target.qualityMode is 1..6" },
				{ "errorCode", "invalid_target" } };
			return false;
		}
		return true;
	}

	json QualificationFoveationTargetJson(
		const QualificationFoveationTarget& a_target)
	{
		return {
			{ "foveatedVendorDispatch", a_target.foveatedVendorDispatch },
			{ "foveatedCenterArea", a_target.foveatedCenterArea },
			{ "peripheryTAAEnable", a_target.peripheryTAAEnable },
			{ "peripheryTAACenterArea", a_target.peripheryTAACenterArea },
			{ "peripheryTAAOuterScale", a_target.peripheryTAAOuterScale },
			{ "floatTolerance", kQualificationFoveationFloatTolerance },
		};
	}

	QualificationFoveationTarget CaptureQualificationFoveationSettings(
		const Upscaling::Settings& a_settings)
	{
		return {
			.foveatedVendorDispatch = a_settings.foveatedVendorDispatch,
			.foveatedCenterArea = a_settings.foveatedCenterArea,
			.peripheryTAAEnable = a_settings.periphery_taa_enable,
			.peripheryTAACenterArea = a_settings.periphery_taa_center_area,
			.peripheryTAAOuterScale = a_settings.periphery_taa_outer_scale,
		};
	}

	bool QualificationFoveationValuesMatch(
		const QualificationFoveationTarget& a_observed,
		const QualificationFoveationTarget& a_target)
	{
		const auto close = [](double a_left, double a_right) {
			return std::abs(a_left - a_right) <=
			       kQualificationFoveationFloatTolerance;
		};
		return a_observed.foveatedVendorDispatch == a_target.foveatedVendorDispatch &&
		       close(a_observed.foveatedCenterArea, a_target.foveatedCenterArea) &&
		       a_observed.peripheryTAAEnable == a_target.peripheryTAAEnable &&
		       close(a_observed.peripheryTAACenterArea, a_target.peripheryTAACenterArea) &&
		       close(a_observed.peripheryTAAOuterScale, a_target.peripheryTAAOuterScale);
	}

	bool TryParseQualificationFoveationTarget(
		const json& a_args,
		std::optional<QualificationFoveationTarget>& a_target,
		json& a_error)
	{
		if (!a_args.contains("foveation"))
			return true;
		if (!a_args["foveation"].is_object()) {
			a_error = { { "error", "foveation must be an object" },
				{ "errorCode", "invalid_foveation_target" } };
			return false;
		}
		const auto& value = a_args["foveation"];
		for (const auto& [name, property] : value.items()) {
			(void)property;
			if (!QualificationPolicy::IsFoveationPropertyAllowed(name)) {
				a_error = { { "error", std::format("unknown foveation property '{}'", name) },
					{ "errorCode", "unknown_foveation_property" } };
				return false;
			}
		}
		static constexpr std::array<const char*, 5> required{
			"foveatedVendorDispatch",
			"foveatedCenterArea",
			"peripheryTAAEnable",
			"peripheryTAACenterArea",
			"peripheryTAAOuterScale",
		};
		for (const char* name : required) {
			if (!value.contains(name)) {
				a_error = { { "error", std::format("foveation requires '{}'", name) },
					{ "errorCode", "invalid_foveation_target" } };
				return false;
			}
		}
		if (!value["foveatedVendorDispatch"].is_boolean() ||
			!value["peripheryTAAEnable"].is_boolean() ||
			!value["foveatedCenterArea"].is_number() ||
			!value["peripheryTAACenterArea"].is_number() ||
			!value["peripheryTAAOuterScale"].is_number()) {
			a_error = { { "error", "foveation boolean and numeric field types are invalid" },
				{ "errorCode", "invalid_foveation_target" } };
			return false;
		}
		QualificationFoveationTarget parsed{
			.foveatedVendorDispatch = value["foveatedVendorDispatch"].get<bool>(),
			.foveatedCenterArea = value["foveatedCenterArea"].get<double>(),
			.peripheryTAAEnable = value["peripheryTAAEnable"].get<bool>(),
			.peripheryTAACenterArea = value["peripheryTAACenterArea"].get<double>(),
			.peripheryTAAOuterScale = value["peripheryTAAOuterScale"].get<double>(),
		};
		const auto validScale = [](double a_scale) {
			return std::isfinite(a_scale) && a_scale >= 0.0 && a_scale <= 1.0;
		};
		if (!validScale(parsed.foveatedCenterArea) ||
			!validScale(parsed.peripheryTAACenterArea) ||
			!validScale(parsed.peripheryTAAOuterScale)) {
			a_error = { { "error", "foveation numeric fields must be finite values in 0..1" },
				{ "errorCode", "invalid_foveation_target" } };
			return false;
		}
		a_target = parsed;
		return true;
	}

	bool TryParsePositiveInteger(
		const json& a_args,
		const char* a_name,
		uint64_t a_maximum,
		uint64_t& a_output,
		json& a_error)
	{
		if (!a_args.contains(a_name) || !TryGetNonNegativeInteger(a_args[a_name], a_output) ||
			a_output == 0 || a_output > a_maximum) {
			a_error = {
				{ "error", std::format("{} must be an integer in 1..{}", a_name, a_maximum) },
				{ "errorCode", std::format("invalid_{}", a_name) },
			};
			return false;
		}
		return true;
	}

	bool TryParseOptionalIntegerExpectation(
		const json& a_args,
		const char* a_name,
		uint64_t a_minimum,
		const char* a_errorCode,
		std::optional<uint64_t>& a_output,
		json& a_error)
	{
		if (!a_args.contains(a_name))
			return true;

		uint64_t value = 0;
		if (!TryGetNonNegativeInteger(a_args[a_name], value) || value < a_minimum) {
			a_error = {
				{ "error", std::format("{} must be an integer greater than or equal to {}", a_name, a_minimum) },
				{ "errorCode", a_errorCode },
			};
			return false;
		}
		a_output = value;
		return true;
	}

	bool TryParseQualificationOwnerID(
		const json& a_args,
		std::string& a_output,
		json& a_error)
	{
		if (!a_args.contains("ownerId") || !a_args["ownerId"].is_string()) {
			a_error = {
				{ "error", "ownerId must be a string containing 1..128 bytes" },
				{ "errorCode", "invalid_owner_id" },
			};
			return false;
		}
		a_output = a_args["ownerId"].get<std::string>();
		if (a_output.empty() || a_output.size() > 128) {
			a_error = {
				{ "error", "ownerId must be a string containing 1..128 bytes" },
				{ "errorCode", "invalid_owner_id" },
			};
			return false;
		}
		return true;
	}

	bool QualificationEvidenceOwnedBy(
		const json& a_evidence,
		uint64_t a_transitionID,
		std::string_view a_ownerID)
	{
		if (!a_evidence.is_object())
			return false;
		const auto transition = a_evidence.find("transitionId");
		const auto owner = a_evidence.find("ownerId");
		uint64_t evidenceTransitionID = 0;
		return transition != a_evidence.end() &&
		       TryGetNonNegativeInteger(*transition, evidenceTransitionID) &&
		       evidenceTransitionID == a_transitionID &&
		       owner != a_evidence.end() && owner->is_string() &&
		       std::string_view(owner->get_ref<const std::string&>()) == a_ownerID;
	}

	bool TryParseExpectedCell(
		const json& a_args,
		QualificationExpectedCell& a_expected,
		json& a_error)
	{
		if (a_args.contains("expectedCell")) {
			uint64_t value = 0;
			if (!TryGetNonNegativeInteger(a_args["expectedCell"], value) ||
				value == 0 || value > std::numeric_limits<uint32_t>::max()) {
				a_error = { { "error", "expectedCell must be a nonzero uint32 form ID" },
					{ "errorCode", "invalid_expected_cell" } };
				return false;
			}
			a_expected.formID = static_cast<uint32_t>(value);
		}
		if (a_args.contains("expectedCellEditorId")) {
			if (!a_args["expectedCellEditorId"].is_string()) {
				a_error = { { "error", "expectedCellEditorId must be a string" },
					{ "errorCode", "invalid_expected_cell_editor_id" } };
				return false;
			}
			a_expected.editorID = a_args["expectedCellEditorId"].get<std::string>();
			if (a_expected.editorID->empty() || a_expected.editorID->size() > 128) {
				a_error = { { "error", "expectedCellEditorId must contain 1..128 bytes" },
					{ "errorCode", "invalid_expected_cell_editor_id" } };
				return false;
			}
		}
		if (!a_expected.formID && !a_expected.editorID) {
			a_error = { { "error", "qualification_wait requires expectedCellEditorId or expectedCell" },
				{ "errorCode", "missing_expected_cell" } };
			return false;
		}
		return true;
	}

	bool TryParseQualificationCocCellEditorID(
		const json& a_args,
		std::optional<std::string>& a_output,
		json& a_error)
	{
		if (!a_args.contains("cocCellEditorId"))
			return true;
		if (!a_args["cocCellEditorId"].is_string()) {
			a_error = {
				{ "error", "cocCellEditorId must be a string" },
				{ "errorCode", "invalid_coc_cell_editor_id" },
			};
			return false;
		}
		const auto value = a_args["cocCellEditorId"].get<std::string>();
		if (value.empty() || value.size() > 128 ||
			!std::all_of(value.begin(), value.end(), [](unsigned char a_character) {
				return std::isalnum(a_character) || a_character == '_';
			})) {
			a_error = {
				{ "error", "cocCellEditorId must contain 1..128 ASCII letters, digits, or underscores" },
				{ "errorCode", "invalid_coc_cell_editor_id" },
			};
			return false;
		}
		a_output = value;
		return true;
	}

	QualificationPolicy::Profile QualificationProfile(
		const CSX::UpscalingAPI::Profile001& a_profile,
		bool a_valid)
	{
		return {
			.valid = a_valid,
			.method = ToQualificationMethod(a_profile.method),
			.qualityMode = static_cast<uint32_t>(a_profile.qualityMode),
			.renderScaleMode = a_profile.renderScaleMode != 0,
			.dlssProfile = static_cast<uint32_t>(a_profile.dlssProfile),
			.fsr4Runtime = a_profile.fsrRuntime == CSX::UpscalingAPI::FSRRuntime::kFSR4,
		};
	}

	QualificationPolicy::Profile QualificationProfile(
		const Upscaling::VRRenderScaleProfileSnapshot& a_profile)
	{
		return {
			.valid = a_profile.valid,
			.method = ToQualificationMethod(a_profile.method),
			.qualityMode = a_profile.qualityMode,
			.renderScaleMode = a_profile.renderScaleModeEnabled,
			.dlssProfile = a_profile.dlssPreset,
			.fsr4Runtime = a_profile.fsr4RuntimeEnabled,
		};
	}

	json APIProfileJson(const CSX::UpscalingAPI::Profile001& a_profile)
	{
		return {
			{ "method", GetQualificationMethodName(ToQualificationMethod(a_profile.method)) },
			{ "methodValue", static_cast<uint32_t>(a_profile.method) },
			{ "qualityMode", static_cast<uint32_t>(a_profile.qualityMode) },
			{ "renderScaleMode", a_profile.renderScaleMode != 0 },
			{ "dlssProfile", GetDLSSProfileName(static_cast<uint32_t>(a_profile.dlssProfile)) },
			{ "fsrRuntime", a_profile.fsrRuntime == CSX::UpscalingAPI::FSRRuntime::kFSR4 ? "fsr4" : "fsr3" },
		};
	}

	json APISnapshotJson(const CSX::UpscalingAPI::Snapshot001& a_snapshot)
	{
		return {
			{ "stateRevision", a_snapshot.stateRevision },
			{ "capabilityRevision", a_snapshot.capabilityRevision },
			{ "profilePresence", a_snapshot.profilePresence },
			{ "flags", a_snapshot.flags },
			{ "observedConditions", a_snapshot.observedConditions },
			{ "transitionState", static_cast<uint32_t>(a_snapshot.transitionState) },
			{ "renderScaleStatus", static_cast<uint32_t>(a_snapshot.renderScaleStatus) },
			{ "activeOperationId", a_snapshot.activeOperationId },
			{ "requested", APIProfileJson(a_snapshot.requested) },
			{ "effective", APIProfileJson(a_snapshot.effective) },
			{ "stable", APIProfileJson(a_snapshot.stable) },
			{ "dimensions", {
								{ "displayEyeWidth", a_snapshot.displayEyeWidth },
								{ "displayEyeHeight", a_snapshot.displayEyeHeight },
								{ "renderEyeWidth", a_snapshot.renderEyeWidth },
								{ "renderEyeHeight", a_snapshot.renderEyeHeight },
							} },
		};
	}

	const CSX::UpscalingAPI::Interface001* QueryUpscalingService()
	{
		const auto* registry = CSX::Api::GetNativeServiceRegistry001();
		if (!registry || !registry->QueryService)
			return nullptr;
		CSX::ServiceAPI::ServiceQuery001 query;
		query.name = CSX::UpscalingAPI::ServiceName;
		query.major = CSX::UpscalingAPI::ServiceMajor;
		query.minimumMinor = CSX::UpscalingAPI::ServiceMinor;
		query.maximumMinor = CSX::UpscalingAPI::ServiceMinor;
		query.requiredCapabilities = CSX::ServiceAPI::kCapabilityInspection;
		const void* service = nullptr;
		if (registry->QueryService(registry->context, &query, &service, nullptr) !=
			CSX::ServiceAPI::Status::kSuccess) {
			return nullptr;
		}
		return static_cast<const CSX::UpscalingAPI::Interface001*>(service);
	}

	uint64_t CounterDelta(uint64_t a_current, uint64_t a_baseline)
	{
		return QualificationPolicy::MonotonicCounterDelta(a_current, a_baseline);
	}

	json QualificationMonotonicRegressionsJson(
		const QualificationDiagnostics& a_current,
		const QualificationDiagnostics& a_baseline,
		bool a_dlssLifecycleRelevant,
		bool a_traceRelevant)
	{
		json output = json::array();
		const auto add = [&output](
							 std::string_view a_counter, uint64_t a_current, uint64_t a_baseline) {
			if (QualificationPolicy::CounterRegressed(a_current, a_baseline)) {
				output.push_back({
					{ "counter", a_counter },
					{ "baseline", a_baseline },
					{ "current", a_current },
				});
			}
		};
		const auto addGenerationCounter = [&output](
											  std::string_view a_counter,
											  uint64_t a_current,
											  uint64_t a_baseline,
											  uint64_t a_currentEpoch,
											  uint32_t a_currentGeneration,
											  uint64_t a_baselineEpoch,
											  uint32_t a_baselineGeneration) {
			if (QualificationPolicy::GenerationCounterRegressed(
					a_current,
					a_baseline,
					a_currentEpoch,
					a_currentGeneration,
					a_baselineEpoch,
					a_baselineGeneration)) {
				output.push_back({
					{ "counter", a_counter },
					{ "baseline", a_baseline },
					{ "current", a_current },
					{ "baselineEpoch", a_baselineEpoch },
					{ "baselineGeneration", a_baselineGeneration },
					{ "currentEpoch", a_currentEpoch },
					{ "currentGeneration", a_currentGeneration },
				});
			}
		};
		add("stress.nextSequence", a_current.stressNextSequence, a_baseline.stressNextSequence);
		add("stress.retryEvents", a_current.stressRetryEvents, a_baseline.stressRetryEvents);
		add("stress.failureEvents", a_current.stressFailureEvents, a_baseline.stressFailureEvents);
		add("stress.overwrittenEvents", a_current.stressOverwrittenEvents, a_baseline.stressOverwrittenEvents);
		add("stress.coalescedEvents", a_current.stressCoalescedEvents, a_baseline.stressCoalescedEvents);
		add("presentation.vendorEvaluatedEyeObservations", a_current.vendorEvaluatedEyeObservations, a_baseline.vendorEvaluatedEyeObservations);
		add("presentation.stretchEyeObservations", a_current.presentationStretchEyeObservations, a_baseline.presentationStretchEyeObservations);
		add("presentation.vendorFailureStretchEyeObservations", a_current.vendorFailureStretchEyeObservations, a_baseline.vendorFailureStretchEyeObservations);
		add("presentation.boundsMismatchFallbackEyeObservations", a_current.boundsMismatchFallbackEyeObservations, a_baseline.boundsMismatchFallbackEyeObservations);
		add("presentation.stretchEpisodes", a_current.presentationStretchEpisodes, a_baseline.presentationStretchEpisodes);
		add("presentation.stretchCompletedEpisodes", a_current.presentationStretchCompletedEpisodes, a_baseline.presentationStretchCompletedEpisodes);
		add("presentation.stretchTimedCompletedEpisodes", a_current.presentationStretchTimedCompletedEpisodes, a_baseline.presentationStretchTimedCompletedEpisodes);
		add("presentation.stretchCompletedFrames", a_current.presentationStretchCompletedFrames, a_baseline.presentationStretchCompletedFrames);
		add("presentation.stretchCompletedQpcTicks", a_current.presentationStretchCompletedQpcTicks, a_baseline.presentationStretchCompletedQpcTicks);
		add("presentation.maximumStretchFrames", a_current.maximumPresentationStretchFrames, a_baseline.maximumPresentationStretchFrames);
		add("presentation.maximumStretchQpcTicks", a_current.maximumPresentationStretchQpcTicks, a_baseline.maximumPresentationStretchQpcTicks);
		addGenerationCounter(
			"failures.fidelityMismatches",
			a_current.fidelityMismatches,
			a_baseline.fidelityMismatches,
			a_current.fidelityTransitionEpoch,
			a_current.fidelityContractGeneration,
			a_baseline.fidelityTransitionEpoch,
			a_baseline.fidelityContractGeneration);
		add("failures.transition", a_current.transitionFailures, a_baseline.transitionFailures);
		add("failures.outOfMemory", a_current.outOfMemoryFailures, a_baseline.outOfMemoryFailures);
		add("failures.deviceLost", a_current.deviceLostFailures, a_baseline.deviceLostFailures);
		if (a_dlssLifecycleRelevant) {
			add("failures.dlssLifecycle", a_current.dlssLifecycleFailures, a_baseline.dlssLifecycleFailures);
		} else {
			add("failures.fsrLifecycle", a_current.fsrLifecycleFailures, a_baseline.fsrLifecycleFailures);
		}
		add("failures.memoryTrim", a_current.memoryTrimFailures, a_baseline.memoryTrimFailures);
		add("failures.retirementFence", a_current.retirementFenceFailures, a_baseline.retirementFenceFailures);
		if (a_traceRelevant) {
			add("dlssTrace.records", a_current.dlssTraceRecords, a_baseline.dlssTraceRecords);
			add("dlssTrace.droppedRecords", a_current.dlssTraceDroppedRecords, a_baseline.dlssTraceDroppedRecords);
			add("dlssTrace.duplicatedConstantsFailures", a_current.dlssTraceConstantsFailures, a_baseline.dlssTraceConstantsFailures);
			add("dlssTrace.evaluateFailures", a_current.dlssTraceEvaluateFailures, a_baseline.dlssTraceEvaluateFailures);
		}
		return output;
	}

	QualificationDiagnostics CaptureQualificationDiagnostics(
		Upscaling& a_upscaling,
		const Upscaling::VRRenderScaleTransitionSnapshot& a_controller,
		const Upscaling::VRRenderScaleStressSessionSnapshot& a_session)
	{
		QualificationDiagnostics output;
		output.stressNextSequence = a_session.nextSequence;
		output.stressOverwrittenEvents = a_session.overwrittenEvents;
		output.stressCoalescedEvents = a_session.coalescedDuplicateCount;
		for (const auto& event : a_session.events) {
			if (event.sequence == 0 || event.sessionID != a_session.sessionID)
				continue;
			if (event.type == Upscaling::VRRenderScaleStressEventType::Retry)
				output.stressRetryEvents += event.occurrences;
			else if (event.type == Upscaling::VRRenderScaleStressEventType::Failure)
				output.stressFailureEvents += event.occurrences;
		}
		output.vendorEvaluatedEyeObservations =
			a_controller.presentation.vendorEvaluatedEyeObservations;
		output.presentationStretchEyeObservations =
			a_controller.presentation.presentationStretchEyeObservations;
		output.vendorFailureStretchEyeObservations =
			a_controller.presentation.vendorFailureStretchEyeObservations;
		output.boundsMismatchFallbackEyeObservations =
			a_controller.presentation.boundsMismatchOriginalFallbackEyeObservations;
		output.presentationStretchEpisodes = a_session.presentationStretchEpisodes;
		output.presentationStretchCompletedEpisodes =
			a_session.presentationStretchCompletedEpisodes;
		output.presentationStretchTimedCompletedEpisodes =
			a_session.presentationStretchTimedCompletedEpisodes;
		output.presentationStretchCompletedFrames =
			a_session.presentationStretchCompletedFrames;
		output.presentationStretchCompletedQpcTicks =
			a_session.presentationStretchCompletedQpcTicks;
		output.presentationStretchEpisodeActive =
			a_session.presentationStretchEpisodeActive;
		output.presentationStretchActiveFrames =
			a_session.presentationStretchActiveFrames;
		output.presentationStretchActiveQpcTicks =
			a_session.presentationStretchActiveQpcTicks;
		output.presentationStretchActiveQpcTimingAvailable =
			a_session.presentationStretchActiveQpcTimingAvailable;
		output.maximumPresentationStretchFrames =
			a_session.maximumPresentationStretchFrames;
		output.maximumPresentationStretchQpcTicks =
			a_session.maximumPresentationStretchQpcTicks;
		output.presentationStretchQpcFrequency =
			a_session.presentationStretchQpcFrequency;
		output.fidelityTransitionEpoch = a_controller.fidelity.transitionEpoch;
		output.fidelityContractGeneration = a_controller.fidelity.contractGeneration;
		output.fidelityMismatches = a_controller.fidelity.mismatchCount;
		output.transitionFailures = a_controller.metrics.current.failures;
		output.outOfMemoryFailures = a_controller.metrics.current.outOfMemoryFailures;
		output.deviceLostFailures = a_controller.metrics.current.deviceLostFailures;
		output.dlssLifecycleFailures = a_controller.dlssLifecycle.failures;
		output.fsrLifecycleFailures = a_controller.fsrLifecycle.failures;
		output.memoryTrimFailures = a_controller.memoryTrim.failures +
		                            a_controller.memoryTrim.preRecreateDrainFailures;
		output.retirementFenceFailures =
			a_controller.engineTargetRetirement.fenceFailures;
		const auto trace = a_upscaling.streamline.GetDLSSDevBenchTraceSnapshot(false);
		output.dlssTraceActive = trace.active;
		output.dlssTraceSessionID = trace.sessionID;
		output.dlssTraceRecords = trace.totalRecords;
		output.dlssTraceDroppedRecords = trace.droppedRecords;
		output.dlssTraceConstantsFailures = trace.duplicatedConstantsFailures;
		output.dlssTraceEvaluateFailures = trace.evaluateFailures;
		return output;
	}

	json QualificationDiagnosticsJson(const QualificationDiagnostics& a_value)
	{
		return {
			{ "stress", {
							{ "nextSequence", a_value.stressNextSequence },
							{ "retryEvents", a_value.stressRetryEvents },
							{ "failureEvents", a_value.stressFailureEvents },
							{ "overwrittenEvents", a_value.stressOverwrittenEvents },
							{ "coalescedEvents", a_value.stressCoalescedEvents },
						} },
			{ "presentation", {
								  { "vendorEvaluatedEyeObservations", a_value.vendorEvaluatedEyeObservations },
								  { "stretchEyeObservations", a_value.presentationStretchEyeObservations },
								  { "vendorFailureStretchEyeObservations", a_value.vendorFailureStretchEyeObservations },
								  { "boundsMismatchFallbackEyeObservations", a_value.boundsMismatchFallbackEyeObservations },
								  { "stretchEpisodes", a_value.presentationStretchEpisodes },
								  { "stretchCompletedEpisodes", a_value.presentationStretchCompletedEpisodes },
								  { "stretchTimedCompletedEpisodes", a_value.presentationStretchTimedCompletedEpisodes },
								  { "stretchCompletedFrames", a_value.presentationStretchCompletedFrames },
								  { "stretchCompletedQpcTicks", a_value.presentationStretchCompletedQpcTicks },
								  { "stretchEpisodeActive", a_value.presentationStretchEpisodeActive },
								  { "stretchActiveFrames", a_value.presentationStretchActiveFrames },
								  { "stretchActiveQpcTicks", a_value.presentationStretchActiveQpcTicks },
								  { "stretchActiveQpcTimingAvailable", a_value.presentationStretchActiveQpcTimingAvailable },
								  { "maximumStretchFrames", a_value.maximumPresentationStretchFrames },
								  { "maximumStretchQpcTicks", a_value.maximumPresentationStretchQpcTicks },
								  { "stretchQpcFrequency", a_value.presentationStretchQpcFrequency },
							  } },
			{ "failures", {
							  { "fidelityMismatches", a_value.fidelityMismatches },
							  { "transition", a_value.transitionFailures },
							  { "outOfMemory", a_value.outOfMemoryFailures },
							  { "deviceLost", a_value.deviceLostFailures },
							  { "dlssLifecycle", a_value.dlssLifecycleFailures },
							  { "fsrLifecycle", a_value.fsrLifecycleFailures },
							  { "memoryTrim", a_value.memoryTrimFailures },
							  { "retirementFence", a_value.retirementFenceFailures },
						  } },
			{ "counterScopes", {
								   { "fidelity", {
													 { "transitionEpoch", a_value.fidelityTransitionEpoch },
													 { "contractGeneration", a_value.fidelityContractGeneration },
												 } },
							   } },
			{ "dlssTrace", {
							   { "active", a_value.dlssTraceActive },
							   { "sessionID", a_value.dlssTraceSessionID },
							   { "records", a_value.dlssTraceRecords },
							   { "droppedRecords", a_value.dlssTraceDroppedRecords },
							   { "duplicatedConstantsFailures", a_value.dlssTraceConstantsFailures },
							   { "evaluateFailures", a_value.dlssTraceEvaluateFailures },
						   } },
		};
	}

	QualificationDiagnostics QualificationDiagnosticsDelta(
		const QualificationDiagnostics& a_current,
		const QualificationDiagnostics& a_baseline)
	{
		QualificationDiagnostics output;
#	define QUALIFICATION_DELTA(field) output.field = CounterDelta(a_current.field, a_baseline.field)
		QUALIFICATION_DELTA(stressNextSequence);
		QUALIFICATION_DELTA(stressRetryEvents);
		QUALIFICATION_DELTA(stressFailureEvents);
		QUALIFICATION_DELTA(stressOverwrittenEvents);
		QUALIFICATION_DELTA(stressCoalescedEvents);
		QUALIFICATION_DELTA(vendorEvaluatedEyeObservations);
		QUALIFICATION_DELTA(presentationStretchEyeObservations);
		QUALIFICATION_DELTA(vendorFailureStretchEyeObservations);
		QUALIFICATION_DELTA(boundsMismatchFallbackEyeObservations);
		QUALIFICATION_DELTA(presentationStretchEpisodes);
		QUALIFICATION_DELTA(presentationStretchCompletedEpisodes);
		QUALIFICATION_DELTA(presentationStretchTimedCompletedEpisodes);
		QUALIFICATION_DELTA(presentationStretchCompletedFrames);
		QUALIFICATION_DELTA(presentationStretchCompletedQpcTicks);
		output.fidelityMismatches = QualificationPolicy::GenerationCounterDelta(
			a_current.fidelityMismatches,
			a_baseline.fidelityMismatches,
			a_current.fidelityTransitionEpoch,
			a_current.fidelityContractGeneration,
			a_baseline.fidelityTransitionEpoch,
			a_baseline.fidelityContractGeneration);
		QUALIFICATION_DELTA(transitionFailures);
		QUALIFICATION_DELTA(outOfMemoryFailures);
		QUALIFICATION_DELTA(deviceLostFailures);
		QUALIFICATION_DELTA(dlssLifecycleFailures);
		QUALIFICATION_DELTA(fsrLifecycleFailures);
		QUALIFICATION_DELTA(memoryTrimFailures);
		QUALIFICATION_DELTA(retirementFenceFailures);
		QUALIFICATION_DELTA(dlssTraceRecords);
		QUALIFICATION_DELTA(dlssTraceDroppedRecords);
		QUALIFICATION_DELTA(dlssTraceConstantsFailures);
		QUALIFICATION_DELTA(dlssTraceEvaluateFailures);
#	undef QUALIFICATION_DELTA
		output.dlssTraceActive = a_current.dlssTraceActive;
		output.dlssTraceSessionID = a_current.dlssTraceSessionID;
		output.fidelityTransitionEpoch = a_current.fidelityTransitionEpoch;
		output.fidelityContractGeneration = a_current.fidelityContractGeneration;
		output.presentationStretchEpisodeActive =
			a_current.presentationStretchEpisodeActive;
		output.presentationStretchActiveFrames =
			a_current.presentationStretchActiveFrames;
		output.presentationStretchActiveQpcTicks =
			a_current.presentationStretchActiveQpcTicks;
		output.presentationStretchActiveQpcTimingAvailable =
			a_current.presentationStretchActiveQpcTimingAvailable;
		output.maximumPresentationStretchFrames =
			a_current.maximumPresentationStretchFrames;
		output.maximumPresentationStretchQpcTicks =
			a_current.maximumPresentationStretchQpcTicks;
		output.presentationStretchQpcFrequency =
			a_current.presentationStretchQpcFrequency;
		return output;
	}

	json QualificationDiagnosticsDeltaJson(
		const QualificationDiagnostics& a_current,
		const QualificationDiagnostics& a_baseline)
	{
		auto output = QualificationDiagnosticsJson(
			QualificationDiagnosticsDelta(a_current, a_baseline));
		output["presentation"].erase("maximumStretchFrames");
		output["presentation"].erase("maximumStretchQpcTicks");
		output["presentation"].erase("stretchEpisodeActive");
		output["presentation"].erase("stretchActiveFrames");
		output["presentation"].erase("stretchActiveQpcTicks");
		output["presentation"].erase("stretchActiveQpcTimingAvailable");
		output["presentation"].erase("stretchQpcFrequency");
		output["presentation"]["maximumStretchFramesBaseline"] =
			a_baseline.maximumPresentationStretchFrames;
		output["presentation"]["maximumStretchFramesCurrent"] =
			a_current.maximumPresentationStretchFrames;
		output["presentation"]["maximumStretchQpcTicksBaseline"] =
			a_baseline.maximumPresentationStretchQpcTicks;
		output["presentation"]["maximumStretchQpcTicksCurrent"] =
			a_current.maximumPresentationStretchQpcTicks;
		output["presentation"]["activeBaseline"] = {
			{ "active", a_baseline.presentationStretchEpisodeActive },
			{ "frames", a_baseline.presentationStretchActiveFrames },
			{ "qpcTicks", a_baseline.presentationStretchActiveQpcTicks },
			{ "qpcTimingAvailable", a_baseline.presentationStretchActiveQpcTimingAvailable },
		};
		output["presentation"]["activeCurrent"] = {
			{ "active", a_current.presentationStretchEpisodeActive },
			{ "frames", a_current.presentationStretchActiveFrames },
			{ "qpcTicks", a_current.presentationStretchActiveQpcTicks },
			{ "qpcTimingAvailable", a_current.presentationStretchActiveQpcTimingAvailable },
		};
		output["presentation"]["qpcFrequencyBaseline"] =
			a_baseline.presentationStretchQpcFrequency;
		output["presentation"]["qpcFrequencyCurrent"] =
			a_current.presentationStretchQpcFrequency;
		return output;
	}

	json QualificationBaselineJson(const QualificationBaseline& a_baseline)
	{
		return {
			{ "timing", {
							{ "clock", "query_performance_counter" },
							{ "beginTick", a_baseline.beginTick },
							{ "tickFrequency", a_baseline.tickFrequency },
						} },
			{ "frame", a_baseline.beginFrame },
			{ "sourceCell", a_baseline.sourceCellFormID },
			{ "sourceCellEditorId", a_baseline.sourceCellEditorID },
			{ "stressSessionId", a_baseline.stressSessionID },
			{ "apiStateRevision", a_baseline.apiStateRevision },
			{ "controllerRevision", a_baseline.controllerRevision },
			{ "profiles", {
							  { "api", APISnapshotJson(a_baseline.apiSnapshot) },
							  { "appliedPhysical", ProfileJson(a_baseline.applied) },
							  { "stablePhysical", ProfileJson(a_baseline.stable) },
						  } },
			{ "foveationSettings", QualificationFoveationTargetJson(a_baseline.foveationSettings) },
			{ "diagnostics", QualificationDiagnosticsJson(a_baseline.diagnostics) },
		};
	}

	json QualificationFirstObservationJson(
		const QualificationPolicy::FirstObservation& a_observation)
	{
		return {
			{ "observed", a_observation.Observed() },
			{ "tick", a_observation.Observed() ?
						  json(a_observation.tick) :
						  json(nullptr) },
			{ "frame", a_observation.Observed() ?
						   json(a_observation.frame) :
						   json(nullptr) },
		};
	}

	json QualificationStateJson()
	{
		auto& store = GetQualificationStore();
		std::lock_guard lock(store.mutex);
		json output{
			{ "active", store.active.has_value() },
			{ "lastEvidence", store.lastEvidence },
		};
		if (store.active) {
			output["transitionId"] = store.active->transitionID;
			output["ownerId"] = store.active->ownerID;
			output["milestone"] = QualificationPolicy::GetMilestoneName(
				store.active->milestone);
			output["phase"] = store.active->ready ?
			                      (store.active->waitInProgress ? "waiting" :
																  (store.active->dispatchTick != 0 ? "dispatched" : "armed")) :
			                      "beginning";
			if (store.active->ready) {
				output["baseline"] = QualificationBaselineJson(store.active->baseline);
				output["dispatch"] = {
					{ "marked", store.active->dispatchTick != 0 },
					{ "tick", store.active->dispatchTick != 0 ?
								  json(store.active->dispatchTick) :
								  json(nullptr) },
					{ "frame", store.active->dispatchTick != 0 ?
								   json(store.active->dispatchFrame) :
								   json(nullptr) },
				};
				output["milestones"] = {
					{ "presentationStable", QualificationFirstObservationJson(
												store.active->presentationStable) },
					{ "cleanupDrained", QualificationFirstObservationJson(
											store.active->cleanupDrained) },
					{ "strictSatisfied", QualificationFirstObservationJson(
											 store.active->strictSatisfied) },
				};
				output["replacementTimeline"] = {
					{ "dispatch", store.active->dispatchPresentationEvidence },
					{ "lastPreMutation", store.active->lastPreMutationEvidence },
					{ "blockedPreMutation", store.active->blockedPreMutationEvidence },
					{ "firstPhysicalMutation", store.active->firstPhysicalMutationEvidence },
					{ "firstPostMutation", store.active->firstPostMutationEvidence },
					{ "firstNewGenerationProven",
						store.active->firstNewGenerationProvenEvidence },
					{ "mutationNotRequiredTerminalProof",
						store.active->mutationNotRequiredTerminalProofEvidence },
					{ "mutationExpectation",
						ReplacementTelemetry::GetMutationExpectationName(
							store.active->mutationExpectation) },
					{ "mutationExpectationReason",
						store.active->mutationExpectationReason },
				};
				output["presentationCycleAudit"] =
					PresentationAuditJson(store.active->presentationAudit);
			}
		}
		return output;
	}

	void FinishQualification(
		uint64_t a_transitionID,
		uint64_t a_ownershipToken,
		const json& a_evidence)
	{
		json retainedEvidence = a_evidence;
		auto& store = GetQualificationStore();
		std::lock_guard lock(store.mutex);
		if (!store.active ||
			!QualificationPolicy::OwnsTransitionInstance(
				store.active->transitionID,
				store.active->ownershipToken,
				a_transitionID,
				a_ownershipToken)) {
			return;
		}
		store.lastEvidence = std::move(retainedEvidence);
		store.active.reset();
	}

	void ClearQualificationOwnership(
		uint64_t a_transitionID,
		uint64_t a_ownershipToken) noexcept
	{
		try {
			auto& store = GetQualificationStore();
			std::lock_guard lock(store.mutex);
			if (store.active &&
				QualificationPolicy::OwnsTransitionInstance(
					store.active->transitionID,
					store.active->ownershipToken,
					a_transitionID,
					a_ownershipToken)) {
				store.active.reset();
			}
		} catch (const std::exception& e) {
			logger::error("[VRRenderScale][DevBench] Failed to clear qualification ownership: {}", e.what());
		} catch (...) {
			logger::error("[VRRenderScale][DevBench] Failed to clear qualification ownership");
		}
	}

	void ReleaseQualificationWait(
		uint64_t a_transitionID,
		uint64_t a_ownershipToken) noexcept
	{
		try {
			auto& store = GetQualificationStore();
			std::lock_guard lock(store.mutex);
			if (store.active &&
				QualificationPolicy::OwnsTransitionInstance(
					store.active->transitionID,
					store.active->ownershipToken,
					a_transitionID,
					a_ownershipToken)) {
				store.active->waitInProgress = false;
			}
		} catch (const std::exception& e) {
			logger::error("[VRRenderScale][DevBench] Failed to release qualification waiter: {}", e.what());
		} catch (...) {
			logger::error("[VRRenderScale][DevBench] Failed to release qualification waiter");
		}
	}

	void RecordQualificationMilestones(
		QualificationTransition& a_transition,
		bool a_presentationStable,
		bool a_cleanupDrained,
		bool a_strictSatisfied,
		uint64_t a_tick,
		uint32_t a_frame)
	{
		const auto record = [&](QualificationTransition& a_value) {
			QualificationPolicy::RecordFirstObservation(
				a_presentationStable,
				a_tick,
				a_frame,
				a_value.presentationStable);
			QualificationPolicy::RecordFirstObservation(
				a_cleanupDrained,
				a_tick,
				a_frame,
				a_value.cleanupDrained);
			QualificationPolicy::RecordFirstObservation(
				a_strictSatisfied,
				a_tick,
				a_frame,
				a_value.strictSatisfied);
		};
		record(a_transition);

		auto& store = GetQualificationStore();
		std::lock_guard lock(store.mutex);
		if (store.active &&
			QualificationPolicy::OwnsTransitionInstance(
				store.active->transitionID,
				store.active->ownershipToken,
				a_transition.transitionID,
				a_transition.ownershipToken)) {
			record(*store.active);
		}
	}

	bool HasExactTargetCorrelatedPresentationProof(
		const json& a_evidence,
		const QualificationTransition& a_transition)
	{
		if (!a_transition.expectedTarget)
			return false;
		const auto& target = *a_transition.expectedTarget;
		if (!a_evidence.is_object() ||
			!a_evidence.contains("presentationProof") ||
			!a_evidence["presentationProof"].is_object()) {
			return false;
		}
		const auto& proof = a_evidence["presentationProof"];
		const bool vendorTarget =
			target.method == QualificationPolicy::Method::DLSS ||
			target.method == QualificationPolicy::Method::FSR;
		const auto proofKind =
			proof.value("kind", std::string{}) == "exact_vendor_evaluation" ?
				ReplacementTelemetry::PresentationProofKind::ExactVendorEvaluation :
			proof.value("kind", std::string{}) == "exact_native_presentation" ?
				ReplacementTelemetry::PresentationProofKind::ExactNativePresentation :
				ReplacementTelemetry::PresentationProofKind::None;
		if (!proof.value("proven", false) ||
			!ReplacementTelemetry::IsExactTargetProofKind(
				proofKind, vendorTarget) ||
			proof.value("method", std::string{}) !=
				GetQualificationMethodName(target.method) ||
			OptionalNonNegativeIntegerOrZero(proof, "qualityMode") !=
				target.qualityMode ||
			!proof.contains("renderScaleMode") ||
			!proof["renderScaleMode"].is_boolean() ||
			proof["renderScaleMode"].get<bool>() != target.renderScaleMode) {
			return false;
		}

		const uint64_t requestID =
			OptionalNonNegativeIntegerOrZero(proof, "requestId");
		const uint64_t transitionEpoch =
			OptionalNonNegativeIntegerOrZero(proof, "transitionEpoch");
		const uint64_t contractGeneration =
			OptionalNonNegativeIntegerOrZero(proof, "contractGeneration");
		const uint64_t publicationGeneration =
			OptionalNonNegativeIntegerOrZero(
				proof, "resourcePublicationGeneration");
		const uint64_t resourceRevision =
			OptionalNonNegativeIntegerOrZero(proof, "resourceRevision");
		const uint64_t deviceIdentity =
			OptionalNonNegativeIntegerOrZero(proof, "deviceIdentity");
		const uint64_t renderWidth =
			OptionalNonNegativeIntegerOrZero(proof, "renderWidth");
		const uint64_t renderHeight =
			OptionalNonNegativeIntegerOrZero(proof, "renderHeight");
		const uint64_t displayWidth =
			OptionalNonNegativeIntegerOrZero(proof, "displayWidth");
		const uint64_t displayHeight =
			OptionalNonNegativeIntegerOrZero(proof, "displayHeight");
		if (requestID == 0 ||
			requestID != a_transition.expectedReplacementRequestID ||
			transitionEpoch == 0 ||
			transitionEpoch != a_transition.expectedReplacementTransitionEpoch ||
			!ReplacementTelemetry::MatchesTargetContractGeneration(
				target.renderScaleMode,
				static_cast<uint32_t>(contractGeneration),
				a_transition.expectedReplacementContractGeneration) ||
			publicationGeneration == 0 ||
			resourceRevision == 0 || deviceIdentity == 0 ||
			deviceIdentity != a_transition.expectedReplacementDeviceIdentity ||
			renderWidth == 0 || renderHeight == 0 || displayWidth == 0 ||
			displayHeight == 0) {
			return false;
		}
		return target.renderScaleMode ?
		           renderWidth < displayWidth && renderHeight < displayHeight :
		           renderWidth == displayWidth && renderHeight == displayHeight;
	}

	void RecordQualificationReplacementTimeline(
		QualificationTransition& a_transition,
		const json& a_observation)
	{
		if (!a_observation.is_object() ||
			!a_observation.contains("replacementPresentation") ||
			!a_observation["replacementPresentation"].is_object()) {
			return;
		}
		json evidence = a_observation["replacementPresentation"];
		evidence["tick"] = OptionalNonNegativeIntegerOrZero(a_observation, "tick");
		evidence["frame"] = OptionalNonNegativeIntegerOrZero(a_observation, "frame");
		const bool mutationStarted =
			evidence.value("physicalMutationStarted", false);
		const bool hasReplacement =
			OptionalNonNegativeIntegerOrZero(evidence, "replacementRequestId") != 0 &&
			OptionalNonNegativeIntegerOrZero(evidence, "replacementTransitionEpoch") != 0;

		const auto record = [&](QualificationTransition& a_value) {
			const uint64_t replacementRequestID =
				OptionalNonNegativeIntegerOrZero(
					evidence, "replacementRequestId");
			const uint64_t replacementTransitionEpoch =
				OptionalNonNegativeIntegerOrZero(
					evidence, "replacementTransitionEpoch");
			const uint32_t replacementContractGeneration =
				static_cast<uint32_t>(OptionalNonNegativeIntegerOrZero(
					evidence, "replacementContractGeneration"));
			const uintptr_t replacementDeviceIdentity =
				static_cast<uintptr_t>(OptionalNonNegativeIntegerOrZero(
					evidence, "replacementDeviceIdentity"));
			if (a_value.expectedReplacementRequestID == 0 &&
				replacementRequestID != 0 && replacementTransitionEpoch != 0 &&
				replacementDeviceIdentity != 0) {
				a_value.expectedReplacementRequestID = replacementRequestID;
				a_value.expectedReplacementTransitionEpoch =
					replacementTransitionEpoch;
				a_value.expectedReplacementContractGeneration =
					replacementContractGeneration;
				a_value.expectedReplacementDeviceIdentity =
					replacementDeviceIdentity;
			} else if (a_value.expectedReplacementRequestID ==
						   replacementRequestID &&
					   a_value.expectedReplacementTransitionEpoch ==
						   replacementTransitionEpoch &&
					   a_value.expectedReplacementDeviceIdentity ==
						   replacementDeviceIdentity &&
					   a_value.expectedReplacementContractGeneration == 0 &&
					   replacementContractGeneration != 0) {
				a_value.expectedReplacementContractGeneration =
					replacementContractGeneration;
			}
			const bool firstMutationRecorded =
				!a_value.firstPhysicalMutationEvidence.is_null();
			if (ReplacementTelemetry::ShouldUpdateLastPreMutation(
					hasReplacement,
					mutationStarted,
					firstMutationRecorded)) {
				a_value.lastPreMutationEvidence = evidence;
				if (evidence.value("replacementAdmissionBlocked", false) &&
					a_value.blockedPreMutationEvidence.is_null()) {
					a_value.blockedPreMutationEvidence = evidence;
				}
			}
			if (!a_value.firstPhysicalMutationEvidence.is_null() &&
				a_value.firstPostMutationEvidence.is_null() &&
				mutationStarted &&
				OptionalNonNegativeIntegerOrZero(evidence, "tick") >=
					OptionalNonNegativeIntegerOrZero(
						a_value.firstPhysicalMutationEvidence, "tick") &&
				OptionalNonNegativeIntegerOrZero(evidence, "frame") >=
					OptionalNonNegativeIntegerOrZero(
						a_value.firstPhysicalMutationEvidence, "frame")) {
				a_value.firstPostMutationEvidence = evidence;
			}
			if (evidence.contains("mutationExpectation") &&
				evidence["mutationExpectation"].is_string()) {
				const auto expectation =
					evidence["mutationExpectation"].get<std::string>();
				auto observedExpectation =
					ReplacementTelemetry::MutationExpectation::Unknown;
				if (expectation == "required") {
					observedExpectation =
						ReplacementTelemetry::MutationExpectation::Required;
				} else if (expectation == "not_required") {
					observedExpectation =
						ReplacementTelemetry::MutationExpectation::NotRequired;
				}
				if (observedExpectation !=
						ReplacementTelemetry::MutationExpectation::Unknown &&
					evidence.contains("mutationExpectationReason") &&
					evidence["mutationExpectationReason"].is_string()) {
					MergeQualificationMutationExpectation(
						a_value,
						observedExpectation,
						evidence["mutationExpectationReason"].get<std::string>());
				}
			}
			const auto proof = evidence.find("presentationProof");
			const auto dispatchProof =
				a_value.dispatchPresentationEvidence.is_object() ?
					a_value.dispatchPresentationEvidence.find("presentationProof") :
					a_value.dispatchPresentationEvidence.end();
			const auto proofValueChanged = [&](const char* a_name) {
				if (proof == evidence.end() || !proof->is_object() ||
					dispatchProof == a_value.dispatchPresentationEvidence.end() ||
					!dispatchProof->is_object()) {
					return false;
				}
				const uint64_t before =
					OptionalNonNegativeIntegerOrZero(*dispatchProof, a_name);
				const uint64_t after =
					OptionalNonNegativeIntegerOrZero(*proof, a_name);
				return before != 0 && after != 0 && before != after;
			};
			const bool dimensionsReplaced =
				proofValueChanged("renderWidth") ||
				proofValueChanged("renderHeight") ||
				proofValueChanged("displayWidth") ||
				proofValueChanged("displayHeight");
			const bool publicationReplaced =
				proofValueChanged("resourcePublicationGeneration");
			if (dimensionsReplaced || publicationReplaced) {
				MergeQualificationMutationExpectation(
					a_value,
					ReplacementTelemetry::MutationExpectation::Required,
					dimensionsReplaced ? "physical_dimensions_replaced" :
										 "publication_generation_replaced");
			}
			if (a_value.mutationExpectation ==
					ReplacementTelemetry::MutationExpectation::NotRequired &&
				a_value.expectedTarget &&
				a_value.mutationNotRequiredTerminalProofEvidence.is_null() &&
				HasExactTargetCorrelatedPresentationProof(
					evidence, a_value)) {
				a_value.mutationNotRequiredTerminalProofEvidence = evidence;
				a_value.mutationNotRequiredTerminalProofEvidence["stressSessionId"] =
					a_value.baseline.stressSessionID;
				a_value.mutationNotRequiredTerminalProofEvidence
					["qualificationTransitionId"] = a_value.transitionID;
				a_value.mutationNotRequiredTerminalProofEvidence["ownershipToken"] =
					a_value.ownershipToken;
			}
		};
		record(a_transition);

		auto& store = GetQualificationStore();
		std::lock_guard lock(store.mutex);
		if (store.active &&
			QualificationPolicy::OwnsTransitionInstance(
				store.active->transitionID,
				store.active->ownershipToken,
				a_transition.transitionID,
				a_transition.ownershipToken)) {
			record(*store.active);
		}
	}

	void TryRecordQualificationReplacementTimeline(
		QualificationTransition& a_transition,
		const json& a_observation,
		bool& a_failureReported) noexcept
	{
		try {
			RecordQualificationReplacementTimeline(a_transition, a_observation);
		} catch (const std::exception& e) {
			if (!a_failureReported) {
				logger::error(
					"[VRRenderScale][DevBench] Replacement timeline observation was incomplete: {}",
					e.what());
				a_failureReported = true;
			}
		} catch (...) {
			if (!a_failureReported) {
				logger::error(
					"[VRRenderScale][DevBench] Replacement timeline observation was incomplete");
				a_failureReported = true;
			}
		}
	}

	void ImportQualificationReplacementTimeline(
		QualificationTransition& a_transition)
	{
		auto& store = GetQualificationStore();
		std::lock_guard lock(store.mutex);
		if (!store.active ||
			!QualificationPolicy::OwnsTransitionInstance(
				store.active->transitionID,
				store.active->ownershipToken,
				a_transition.transitionID,
				a_transition.ownershipToken)) {
			return;
		}
		a_transition.lastPreMutationEvidence =
			store.active->lastPreMutationEvidence;
		a_transition.blockedPreMutationEvidence =
			store.active->blockedPreMutationEvidence;
		a_transition.firstPhysicalMutationEvidence =
			store.active->firstPhysicalMutationEvidence;
		a_transition.firstPostMutationEvidence =
			store.active->firstPostMutationEvidence;
		a_transition.firstNewGenerationProvenEvidence =
			store.active->firstNewGenerationProvenEvidence;
		a_transition.mutationNotRequiredTerminalProofEvidence =
			store.active->mutationNotRequiredTerminalProofEvidence;
		a_transition.mutationExpectation =
			store.active->mutationExpectation;
		a_transition.mutationExpectationReason =
			store.active->mutationExpectationReason;
	}

	json QualificationFailureReasonsJson(uint64_t a_failures)
	{
		struct NamedReason
		{
			QualificationPolicy::FailureReason reason;
			const char* code;
			const char* category;
		};
		static constexpr std::array reasons{
			NamedReason{ QualificationPolicy::kFailureStressSession, "stress_session_changed", "ownership" },
			NamedReason{ QualificationPolicy::kFailureExactCell, "expected_cell_not_current", "destination" },
			NamedReason{ QualificationPolicy::kFailureLoadedInWorld, "player_not_loaded_in_world", "destination" },
			NamedReason{ QualificationPolicy::kFailureFreshObservation, "stale_source_observation", "destination" },
			NamedReason{ QualificationPolicy::kFailurePublicSnapshot, "upscaling_service_unavailable", "api" },
			NamedReason{ QualificationPolicy::kFailureProvider, "provider_not_ready", "api" },
			NamedReason{ QualificationPolicy::kFailureProfiles, "profile_mismatch", "profile" },
			NamedReason{ QualificationPolicy::kFailureDimensions, "invalid_dimensions", "physical" },
			NamedReason{ QualificationPolicy::kFailureAPIOperation, "api_operation_active", "api" },
			NamedReason{ QualificationPolicy::kFailureAPIConditions, "api_conditions_blocking", "api" },
			NamedReason{ QualificationPolicy::kFailureController, "controller_not_settled", "controller" },
			NamedReason{ QualificationPolicy::kFailureWorkGate, "vendor_work_gate_active", "controller" },
			NamedReason{ QualificationPolicy::kFailureRelatch, "relatch_pending", "controller" },
			NamedReason{ QualificationPolicy::kFailureRecovery, "recovery_pending", "controller" },
			NamedReason{ QualificationPolicy::kFailureMemoryTrim, "memory_trim_pending", "controller" },
			NamedReason{ QualificationPolicy::kFailureRetirement, "resource_retirement_pending", "controller" },
			NamedReason{ QualificationPolicy::kFailurePhysicalMutation, "physical_mutation_unresolved", "controller" },
			NamedReason{ QualificationPolicy::kFailureTerminal, "terminal_or_device_loss", "controller" },
			NamedReason{ QualificationPolicy::kFailureFoveationSettings, "foveation_settings_mismatch", "settings" },
			NamedReason{ QualificationPolicy::kFailureDiagnosticDelta, "terminal_diagnostic_delta", "diagnostics" },
			NamedReason{ QualificationPolicy::kFailureAPIActiveContract, "api_active_contract_mismatch", "active" },
			NamedReason{ QualificationPolicy::kFailurePhysicalActiveContract, "physical_active_contract_mismatch", "active" },
			NamedReason{ QualificationPolicy::kFailurePresentationPhase, "presentation_phase_not_stable", "active" },
			NamedReason{ QualificationPolicy::kFailureFidelity, "both_eye_fidelity_not_stable", "active" },
			NamedReason{ QualificationPolicy::kFailureVendorPresentation, "vendor_presentation_not_stable", "active" },
			NamedReason{ QualificationPolicy::kFailureLifecycle, "vendor_lifecycle_not_clean", "active" },
			NamedReason{ QualificationPolicy::kFailureFSRDispatch, "fsr_dispatch_not_stable", "active" },
			NamedReason{ QualificationPolicy::kFailureShaderCompilation, "shader_compilation_active", "active" },
			NamedReason{ QualificationPolicy::kFailureAPINativeContract, "api_native_contract_mismatch", "native" },
			NamedReason{ QualificationPolicy::kFailurePhysicalNativeContract, "physical_native_contract_mismatch", "native" },
			NamedReason{ QualificationPolicy::kFailureNativePresentation, "native_presentation_not_stable", "native" },
			NamedReason{ QualificationPolicy::kFailureResourcePublication, "resource_publication_not_current", "physical" },
			NamedReason{ QualificationPolicy::kFailureProviderTerminal, "provider_terminal_failure", "provider" },
		};
		json output = json::array();
		for (const auto& reason : reasons) {
			if ((a_failures & static_cast<uint64_t>(reason.reason)) != 0) {
				output.push_back({
					{ "code", reason.code },
					{ "category", reason.category },
				});
			}
		}
		return output;
	}

	json QualificationTerminalDiagnosticReasonsJson(
		const QualificationPolicy::TerminalDiagnosticDeltas& a_delta)
	{
		json output = json::array();
		const auto add = [&output](std::string_view a_code, uint64_t a_count) {
			if (a_count != 0) {
				output.push_back({
					{ "code", a_code },
					{ "category", "diagnostics" },
					{ "count", a_count },
				});
			}
		};
		add("stress_failure_event", a_delta.stressFailureEvents);
		add("stress_evidence_overwritten", a_delta.stressOverwrittenEvents);
		add("vendor_failure_stretch", a_delta.vendorFailureStretchEyeObservations);
		add("bounds_mismatch_fallback", a_delta.boundsMismatchFallbackEyeObservations);
		add("fidelity_mismatch", a_delta.fidelityMismatches);
		add("transition_failure", a_delta.transitionFailures);
		add("out_of_memory_failure", a_delta.outOfMemoryFailures);
		add("device_lost_failure", a_delta.deviceLostFailures);
		add("vendor_lifecycle_failure", a_delta.relevantLifecycleFailures);
		add("memory_trim_failure", a_delta.memoryTrimFailures);
		add("retirement_fence_failure", a_delta.retirementFenceFailures);
		if (a_delta.monotonicCounterRegression) {
			output.push_back({
				{ "code", "monotonic_diagnostic_counter_regressed" },
				{ "category", "diagnostics" },
			});
		}
		if (a_delta.traceApplicable) {
			if (a_delta.traceSessionLost) {
				output.push_back({
					{ "code", "dlss_trace_session_lost" },
					{ "category", "diagnostics" },
				});
			}
			add("dlss_trace_records_dropped", a_delta.traceDroppedRecords);
			add("dlss_trace_constants_failure", a_delta.traceConstantsFailures);
			add("dlss_trace_evaluate_failure", a_delta.traceEvaluateFailures);
		}
		return output;
	}

	bool ResourceKeysAgree(
		const Upscaling::VRRenderScaleResourceKey& a_left,
		const Upscaling::VRRenderScaleResourceKey& a_right)
	{
		return a_left.valid == a_right.valid &&
		       a_left.active == a_right.active &&
		       a_left.method == a_right.method &&
		       a_left.backend == a_right.backend &&
		       a_left.qualityMode == a_right.qualityMode &&
		       (a_left.method != Upscaling::UpscaleMethod::kDLSS ||
				   a_left.dlssPreset == a_right.dlssPreset) &&
		       a_left.displayEyeWidth == a_right.displayEyeWidth &&
		       a_left.displayEyeHeight == a_right.displayEyeHeight &&
		       a_left.renderEyeWidth == a_right.renderEyeWidth &&
		       a_left.renderEyeHeight == a_right.renderEyeHeight &&
		       a_left.contextCount == a_right.contextCount &&
		       a_left.foveatedVendorDispatch == a_right.foveatedVendorDispatch &&
		       a_left.peripheryTAA == a_right.peripheryTAA;
	}

	bool PhysicalProfilesAgree(
		const Upscaling::VRRenderScaleProfileSnapshot& a_left,
		const Upscaling::VRRenderScaleProfileSnapshot& a_right)
	{
		return QualificationPolicy::ProfilesAgree(
				   QualificationProfile(a_left), QualificationProfile(a_right)) &&
		       a_left.active == a_right.active &&
		       a_left.displayEyeWidth == a_right.displayEyeWidth &&
		       a_left.displayEyeHeight == a_right.displayEyeHeight &&
		       a_left.renderEyeWidth == a_right.renderEyeWidth &&
		       a_left.renderEyeHeight == a_right.renderEyeHeight &&
		       ResourceKeysAgree(a_left.resources, a_right.resources);
	}

	QualificationPolicy::PhysicalBackend ToQualificationPhysicalBackend(
		Upscaling::VRRenderScaleBackendKind a_backend)
	{
		switch (a_backend) {
		case Upscaling::VRRenderScaleBackendKind::DLSS:
			return QualificationPolicy::PhysicalBackend::DLSS;
		case Upscaling::VRRenderScaleBackendKind::FSRHost:
			return QualificationPolicy::PhysicalBackend::FSRHost;
		case Upscaling::VRRenderScaleBackendKind::FSRRuntime:
			return QualificationPolicy::PhysicalBackend::FSRRuntime;
		case Upscaling::VRRenderScaleBackendKind::FSR4Runtime:
			return QualificationPolicy::PhysicalBackend::FSR4Runtime;
		default:
			return QualificationPolicy::PhysicalBackend::None;
		}
	}

	bool PhysicalProfileMatchesTarget(
		const Upscaling::VRRenderScaleProfileSnapshot& a_profile,
		const QualificationTarget& a_target,
		const CSX::UpscalingAPI::Snapshot001& a_api,
		bool a_active)
	{
		if (!QualificationPolicy::MatchesTarget(QualificationProfile(a_profile), a_target) ||
			!a_profile.valid || a_profile.active != a_active ||
			a_profile.displayEyeWidth != a_api.displayEyeWidth ||
			a_profile.displayEyeHeight != a_api.displayEyeHeight ||
			a_profile.renderEyeWidth != a_api.renderEyeWidth ||
			a_profile.renderEyeHeight != a_api.renderEyeHeight) {
			return false;
		}
		const auto& resources = a_profile.resources;
		if (!resources.valid || resources.active != a_active ||
			resources.method != a_profile.method ||
			resources.qualityMode != a_profile.qualityMode ||
			resources.displayEyeWidth != a_api.displayEyeWidth ||
			resources.displayEyeHeight != a_api.displayEyeHeight ||
			resources.renderEyeWidth != a_api.renderEyeWidth ||
			resources.renderEyeHeight != a_api.renderEyeHeight ||
			resources.contextCount < 2) {
			return false;
		}
		if (a_target.method == QualificationPolicy::Method::DLSS &&
			a_target.matchDLSSProfile && resources.dlssPreset != a_target.dlssProfile) {
			return false;
		}
		return a_active ?
		           QualificationPolicy::MatchesActivePhysicalBackend(
					   a_target.method,
					   ToQualificationPhysicalBackend(resources.backend)) :
		           resources.backend == Upscaling::VRRenderScaleBackendKind::None;
	}

	bool FoveationSettingsMatch(
		const Upscaling::Settings& a_settings,
		const std::optional<QualificationFoveationTarget>& a_target)
	{
		if (!a_target)
			return true;
		return QualificationFoveationValuesMatch(
			CaptureQualificationFoveationSettings(a_settings), *a_target);
	}

	json ObservedFoveationJson(
		const Upscaling& a_upscaling,
		const Upscaling::VRRenderScaleProfileSnapshot& a_stable)
	{
		const auto method = a_upscaling.GetRuntimeUpscaleMethod();
		return {
			{ "settings", {
							  { "foveatedVendorDispatch", a_upscaling.settings.foveatedVendorDispatch },
							  { "foveatedCenterArea", a_upscaling.settings.foveatedCenterArea },
							  { "peripheryTAAEnable", a_upscaling.settings.periphery_taa_enable },
							  { "peripheryTAACenterArea", a_upscaling.settings.periphery_taa_center_area },
							  { "peripheryTAAOuterScale", a_upscaling.settings.periphery_taa_outer_scale },
						  } },
			{ "physical", {
							  { "valid", a_stable.resources.valid },
							  { "foveatedVendorDispatch", a_stable.resources.foveatedVendorDispatch },
							  { "peripheryTAA", a_stable.resources.peripheryTAA },
						  } },
			{ "liveExecution", {
								   { "foveatedVendorDispatch", a_upscaling.IsFoveatedVendorDispatchEnabled(method) },
								   { "peripheryTAA", a_upscaling.IsPeripheryTAAEnabled(method) },
							   } },
			{ "floatTolerance", kQualificationFoveationFloatTolerance },
		};
	}

	bool ActivePhysicalContractStable(
		const Upscaling& a_upscaling,
		const Upscaling::VRRenderScaleTransitionSnapshot& a_controller,
		const CSX::UpscalingAPI::Snapshot001& a_api,
		const QualificationTarget& a_target,
		const std::optional<QualificationFoveationTarget>& a_foveation)
	{
		const auto& applied = a_controller.applied;
		const auto& stable = a_controller.stable;
		// Publication is transient; owner keys and stable profiles are durable.
		if (a_controller.state != Upscaling::VRRenderScaleTransitionState::Active ||
			a_controller.physicalOwner.transitionEpoch != a_controller.stable.transitionEpoch ||
			a_controller.physicalOwner.contractGeneration != a_controller.stable.contractGeneration ||
			a_controller.presentationOwner.transitionEpoch != a_controller.stable.transitionEpoch ||
			a_controller.presentationOwner.contractGeneration != a_controller.stable.contractGeneration ||
			!PhysicalProfileMatchesTarget(applied, a_target, a_api, true) ||
			!PhysicalProfileMatchesTarget(stable, a_target, a_api, true) ||
			!PhysicalProfilesAgree(applied, stable) ||
			(a_target.renderScaleMode ?
					(a_api.renderEyeWidth >= a_api.displayEyeWidth ||
						a_api.renderEyeHeight >= a_api.displayEyeHeight) :
					(a_api.renderEyeWidth != a_api.displayEyeWidth ||
						a_api.renderEyeHeight != a_api.displayEyeHeight))) {
			return false;
		}
		if (a_foveation &&
			(a_upscaling.IsFoveatedVendorDispatchEnabled(stable.method) !=
					a_foveation->foveatedVendorDispatch ||
				a_upscaling.IsPeripheryTAAEnabled(stable.method) !=
					a_foveation->peripheryTAAEnable)) {
			return false;
		}
		return true;
	}

	bool NativePhysicalContractStable(
		const Upscaling& a_upscaling,
		const Upscaling::VRRenderScaleTransitionSnapshot& a_controller,
		const CSX::UpscalingAPI::Snapshot001& a_api,
		const QualificationTarget& a_target,
		const std::optional<QualificationFoveationTarget>& a_foveation)
	{
		const bool nativePipelineTarget =
			a_target.method == QualificationPolicy::Method::None ||
			a_target.method == QualificationPolicy::Method::TAA;
		if (nativePipelineTarget) {
			const bool statesAgree =
				(a_api.transitionState == CSX::UpscalingAPI::TransitionState::kIdle &&
					a_controller.state == Upscaling::VRRenderScaleTransitionState::Idle) ||
				(a_api.transitionState == CSX::UpscalingAPI::TransitionState::kActive &&
					a_controller.state == Upscaling::VRRenderScaleTransitionState::Active);
			const auto inactive = [](const Upscaling::VRRenderScaleProfileSnapshot& a_profile) {
				return !a_profile.active &&
				       (!a_profile.resources.valid ||
						   (!a_profile.resources.active &&
							   a_profile.resources.backend ==
								   Upscaling::VRRenderScaleBackendKind::None)) &&
				       !a_profile.resources.foveatedVendorDispatch &&
				       !a_profile.resources.peripheryTAA;
			};
			return statesAgree && inactive(a_controller.applied) &&
			       inactive(a_controller.stable) &&
			       a_api.renderEyeWidth == a_api.displayEyeWidth &&
			       a_api.renderEyeHeight == a_api.displayEyeHeight;
		}
		if (a_controller.state != Upscaling::VRRenderScaleTransitionState::Idle &&
			a_controller.state != Upscaling::VRRenderScaleTransitionState::Active) {
			return false;
		}
		const bool vendorTarget = QualificationPolicy::UsesVendorEvaluation(a_target);
		if (vendorTarget &&
			a_upscaling.GetRuntimeUpscaleMethod() != a_controller.stable.method) {
			return false;
		}
		if (vendorTarget && a_foveation &&
			(a_upscaling.IsFoveatedVendorDispatchEnabled(a_controller.stable.method) !=
					a_foveation->foveatedVendorDispatch ||
				a_upscaling.IsPeripheryTAAEnabled(a_controller.stable.method) !=
					a_foveation->peripheryTAAEnable)) {
			return false;
		}
		return PhysicalProfileMatchesTarget(a_controller.applied, a_target, a_api, false) &&
		       PhysicalProfileMatchesTarget(a_controller.stable, a_target, a_api, false) &&
		       PhysicalProfilesAgree(a_controller.applied, a_controller.stable) &&
		       !a_controller.applied.resources.foveatedVendorDispatch &&
		       !a_controller.applied.resources.peripheryTAA &&
		       !a_controller.stable.resources.foveatedVendorDispatch &&
		       !a_controller.stable.resources.peripheryTAA &&
		       a_api.renderEyeWidth == a_api.displayEyeWidth &&
		       a_api.renderEyeHeight == a_api.displayEyeHeight;
	}

	bool ActiveFidelityStable(
		const Upscaling::VRRenderScaleTransitionSnapshot& a_controller,
		uint32_t a_beginFrame)
	{
		const auto& stable = a_controller.stable;
		const auto& fidelity = a_controller.fidelity;
		if (!fidelity.active || !fidelity.bothEyesValid ||
			(fidelity.evaluationEyeMask & 0x3u) != 0x3u ||
			(fidelity.invariantEyeMask & 0x3u) != 0x3u ||
			fidelity.lastMismatchMask != 0 ||
			fidelity.method != stable.method ||
			fidelity.backend != stable.resources.backend ||
			fidelity.transitionEpoch != stable.transitionEpoch ||
			fidelity.contractGeneration != stable.contractGeneration) {
			return false;
		}
		const auto& left = fidelity.eyes[0];
		const auto& right = fidelity.eyes[1];
		return left.evaluated && left.valid && right.evaluated && right.valid &&
		       left.frame == right.frame &&
		       QualificationPolicy::FrameAdvanced(a_beginFrame, left.frame) &&
		       left.generation == stable.contractGeneration &&
		       right.generation == stable.contractGeneration &&
		       left.inputWidth == stable.renderEyeWidth &&
		       right.inputWidth == stable.renderEyeWidth &&
		       left.inputHeight == stable.renderEyeHeight &&
		       right.inputHeight == stable.renderEyeHeight &&
		       left.outputWidth == stable.displayEyeWidth &&
		       right.outputWidth == stable.displayEyeWidth &&
		       left.outputHeight == stable.displayEyeHeight &&
		       right.outputHeight == stable.displayEyeHeight;
	}

	bool PresentationEyesStable(
		const Upscaling::VRRenderScaleTransitionSnapshot& a_controller,
		Upscaling::VRRenderScalePresentationPath a_path,
		uint32_t a_beginFrame)
	{
		const auto& stable = a_controller.stable;
		const auto& presentation = a_controller.presentation;
		const auto& left = presentation.eyes[0];
		const auto& right = presentation.eyes[1];
		const bool vendorPresentation =
			a_path == Upscaling::VRRenderScalePresentationPath::VendorEvaluated;
		const bool common =
			left.valid && right.valid && left.path == a_path && right.path == a_path &&
			QualificationPolicy::HasCoherentPresentationFrames(
				vendorPresentation,
				a_beginFrame,
				left.frame,
				right.frame,
				left.compositorCycleToken,
				right.compositorCycleToken,
				presentation.lastBothEyesVendorFrame,
				presentation.lastBothEyesVendorCycle) &&
			left.transitionEpoch == stable.transitionEpoch &&
			right.transitionEpoch == stable.transitionEpoch &&
			left.contractGeneration == stable.contractGeneration &&
			right.contractGeneration == stable.contractGeneration &&
			left.method == stable.method && right.method == stable.method &&
			!left.loadingOrMenuContext && !right.loadingOrMenuContext &&
			!left.transitionCooldown && !right.transitionCooldown;
		if (!common)
			return false;
		if (!QualificationPolicy::HasRequiredPresentationHistory(
				vendorPresentation,
				left.consecutiveFrames,
				right.consecutiveFrames,
				presentation.consecutiveBothEyesVendorFrames)) {
			return false;
		}
		if (vendorPresentation) {
			if (left.inputWidth != stable.renderEyeWidth ||
				right.inputWidth != stable.renderEyeWidth ||
				left.inputHeight != stable.renderEyeHeight ||
				right.inputHeight != stable.renderEyeHeight ||
				left.expectedInputWidth != stable.renderEyeWidth ||
				right.expectedInputWidth != stable.renderEyeWidth ||
				left.expectedInputHeight != stable.renderEyeHeight ||
				right.expectedInputHeight != stable.renderEyeHeight ||
				left.outputWidth != stable.displayEyeWidth ||
				right.outputWidth != stable.displayEyeWidth ||
				left.outputHeight != stable.displayEyeHeight ||
				right.outputHeight != stable.displayEyeHeight) {
				return false;
			}
		} else if (left.inputWidth != stable.displayEyeWidth ||
				   right.inputWidth != stable.displayEyeWidth ||
				   left.inputHeight != stable.displayEyeHeight ||
				   right.inputHeight != stable.displayEyeHeight ||
				   left.expectedInputWidth != stable.displayEyeWidth ||
				   right.expectedInputWidth != stable.displayEyeWidth ||
				   left.expectedInputHeight != stable.displayEyeHeight ||
				   right.expectedInputHeight != stable.displayEyeHeight ||
				   left.outputWidth != stable.displayEyeWidth ||
				   right.outputWidth != stable.displayEyeWidth ||
				   left.outputHeight != stable.displayEyeHeight ||
				   right.outputHeight != stable.displayEyeHeight) {
			return false;
		}
		return true;
	}

	QualificationPolicy::NativeVendorEyeEvidence BuildNativeVendorEyeEvidence(
		const Upscaling::VRRenderScalePresentationEyeSnapshot& a_eye)
	{
		return {
			.valid = a_eye.valid &&
			         a_eye.path ==
			             Upscaling::VRRenderScalePresentationPath::NativeOriginal,
			.presentationFrame = a_eye.frame,
			.dispatchFrame = a_eye.vendorDispatchFrame,
			.backend = ToQualificationPhysicalBackend(a_eye.vendorBackend),
			.dispatchSerial = a_eye.vendorDispatchSerial,
			.runtimeFallback = a_eye.vendorRuntimeFallback,
		};
	}

	bool NativeVendorPresentationStable(
		const Upscaling::VRRenderScaleTransitionSnapshot& a_controller,
		const QualificationTarget& a_target,
		uint32_t a_beginFrame)
	{
		return QualificationPolicy::HasCoherentNativeVendorEvaluation(
			a_target,
			a_beginFrame,
			BuildNativeVendorEyeEvidence(a_controller.presentation.eyes[0]),
			BuildNativeVendorEyeEvidence(a_controller.presentation.eyes[1]));
	}

	json NativeVendorExecutionJson(
		const Upscaling::VRRenderScaleTransitionSnapshot& a_controller,
		const QualificationTarget* a_target,
		uint32_t a_beginFrame)
	{
		const auto& left = a_controller.presentation.eyes[0];
		const auto& right = a_controller.presentation.eyes[1];
		const bool required = a_target &&
		                      QualificationPolicy::UsesNativeVendorEvaluation(
								  *a_target);
		const bool coherent = required && NativeVendorPresentationStable(
											  a_controller,
											  *a_target,
											  a_beginFrame);
		const bool backendConverged =
			left.vendorBackend != Upscaling::VRRenderScaleBackendKind::None &&
			left.vendorBackend == right.vendorBackend;
		const char* actualBackend = backendConverged ?
		                                GetBackendName(left.vendorBackend) :
		                            left.vendorBackend != right.vendorBackend ?
		                                "mixed" :
		                                "none";
		const auto eyeJson = [](const Upscaling::VRRenderScalePresentationEyeSnapshot& a_eye) {
			return json{
				{ "valid", a_eye.valid },
				{ "presentationFrame", a_eye.frame },
				{ "dispatchFrame", a_eye.vendorDispatchFrame },
				{ "backend", GetBackendName(a_eye.vendorBackend) },
				{ "dispatchSerial", a_eye.vendorDispatchSerial },
				{ "runtimeFallback", a_eye.vendorRuntimeFallback },
			};
		};
		return {
			{ "required", required },
			{ "sameFrameBothEyesValid", coherent },
			{ "actualBackend", actualBackend },
			{ "actualRuntimeFallbackObserved",
				left.vendorRuntimeFallback || right.vendorRuntimeFallback },
			{ "dispatchFrame", coherent ? left.vendorDispatchFrame : 0u },
			{ "left", eyeJson(left) },
			{ "right", eyeJson(right) },
		};
	}

	bool LifecycleStable(
		const Upscaling::VRRenderScaleTransitionSnapshot& a_controller,
		const QualificationTarget& a_target)
	{
		const auto& stable = a_controller.stable;
		const auto& lifecycle = a_target.method == QualificationPolicy::Method::DLSS ?
		                            a_controller.dlssLifecycle :
		                            a_controller.fsrLifecycle;
		return lifecycle.phase == Upscaling::VRVendorRuntimeLifecyclePhase::Ready &&
		       lifecycle.resourcesPresent && lifecycle.readyForContract &&
		       lifecycle.method == stable.method &&
		       lifecycle.backend == stable.resources.backend &&
		       lifecycle.transitionEpoch == stable.transitionEpoch &&
		       lifecycle.requestedGeneration == stable.contractGeneration &&
		       lifecycle.runtimeGeneration == stable.contractGeneration;
	}

	bool FSRDispatchStable(
		const Upscaling::VRRenderScaleTransitionSnapshot& a_controller)
	{
		const auto& left = a_controller.fidelity.eyes[0];
		const auto& right = a_controller.fidelity.eyes[1];
		return left.fsrDispatchPathValid && right.fsrDispatchPathValid &&
		       left.fsrDispatchSerial != 0 && right.fsrDispatchSerial != 0 &&
		       left.fsrDispatchSerial != right.fsrDispatchSerial &&
		       left.frame == right.frame &&
		       left.fsrDispatchBackend == a_controller.stable.resources.backend &&
		       right.fsrDispatchBackend == a_controller.stable.resources.backend &&
		       !left.fsrRuntimeFallback && !right.fsrRuntimeFallback;
	}

	bool APIProfileStateChanged(
		const CSX::UpscalingAPI::Snapshot001& a_current,
		const CSX::UpscalingAPI::Snapshot001& a_baseline)
	{
		using namespace CSX::UpscalingAPI;
		constexpr uint32_t relevantPresence =
			kProfileRequested | kProfileApplying | kProfileEffective | kProfileStable;
		if ((a_current.profilePresence & relevantPresence) !=
			(a_baseline.profilePresence & relevantPresence)) {
			return true;
		}
		const auto changed = [](const Profile001& a_left, const Profile001& a_right) {
			return !QualificationPolicy::ProfilesAgree(
				QualificationProfile(a_left, true), QualificationProfile(a_right, true));
		};
		return changed(a_current.requested, a_baseline.requested) ||
		       changed(a_current.applying, a_baseline.applying) ||
		       changed(a_current.effective, a_baseline.effective) ||
		       changed(a_current.stable, a_baseline.stable);
	}

	json QualificationCleanupDebtJson(
		const Upscaling::VRRenderScaleTransitionSnapshot& a_controller,
		const Upscaling::VRVendorWorkGateSnapshot& a_gate,
		uint64_t a_physicalMutationEpoch,
		uint64_t a_physicalSerializationEpoch,
		bool a_emergencyRecoveryRequested,
		bool a_shaderCompilationActive)
	{
		return {
			{ "controller", {
								{ "state", Upscaling::GetVRRenderScaleTransitionStateName(
											   a_controller.state) },
								{ "targetEpoch", a_controller.targetEpoch },
								{ "stateFrame", a_controller.stateFrame },
							} },
			{ "workGate", VendorWorkGateJson(a_gate) },
			{ "memoryTrim", {
								{ "pending", a_controller.memoryTrim.pending },
								{ "reason", Upscaling::GetVRRenderScaleMemoryTrimReasonName(a_controller.memoryTrim.reason) },
								{ "ownerEpoch", a_controller.memoryTrim.ownerEpoch },
								{ "requestedFrame", a_controller.memoryTrim.requestedFrame },
								{ "fenceFailures", a_controller.memoryTrim.fenceFailures },
							} },
			{ "intermediateRetirement", {
											{ "pendingSets", a_controller.retirement.pendingSets },
											{ "oldestEpoch", a_controller.retirement.oldestEpoch },
											{ "newestEpoch", a_controller.retirement.newestEpoch },
											{ "nextCleanupFrame", a_controller.retirement.nextCleanupFrame },
											{ "fencePending", a_controller.retirement.fencePending },
											{ "capacityBlocked", a_controller.retirement.capacityBlocked },
										} },
			{ "engineTargetRetirement", {
											{ "pending", a_controller.engineTargetRetirement.pending },
											{ "oldestEpoch", a_controller.engineTargetRetirement.oldestEpoch },
											{ "newestEpoch", a_controller.engineTargetRetirement.newestEpoch },
											{ "pendingGenerations", a_controller.engineTargetRetirement.pendingGenerations },
											{ "pendingReleaseCount", a_controller.engineTargetRetirement.pendingReleaseCount },
											{ "fencePending", a_controller.engineTargetRetirement.fencePending },
											{ "capacityBlocked", a_controller.engineTargetRetirement.capacityBlocked },
										} },
			{ "postLoadRecovery", {
									  { "active", a_controller.postLoadRecovery.active },
									  { "recoveryEpoch", a_controller.postLoadRecovery.recoveryEpoch },
									  { "cleanupArmed", a_controller.postLoadRecovery.cleanupArmed },
									  { "cleanupDrained", a_controller.postLoadRecovery.cleanupDrained },
									  { "trimArmed", a_controller.postLoadRecovery.trimArmed },
									  { "trimCompleted", a_controller.postLoadRecovery.trimCompleted },
									  { "timedAttemptInProgress", a_controller.postLoadRecovery.timedAttemptInProgress },
								  } },
			{ "physicalMutation", {
									  { "epoch", a_physicalMutationEpoch },
									  { "serializationEpoch", a_physicalSerializationEpoch },
									  { "emergencyRecoveryRequested", a_emergencyRecoveryRequested },
								  } },
			{ "shaderCompilationActive", a_shaderCompilationActive },
		};
	}

	json CaptureQualificationObservation(
		const QualificationTransition& a_transition,
		const QualificationExpectedCell& a_expectedCell,
		const std::optional<QualificationTarget>& a_expectedTarget,
		const std::optional<QualificationFoveationTarget>& a_foveation)
	{
		using namespace CSX::UpscalingAPI;
		auto& upscaling = globals::features::upscaling;
		const auto controller = upscaling.GetVRRenderScaleTransitionSnapshot();
		const auto session = upscaling.GetVRRenderScaleStressSessionSnapshot();
		const auto gate = upscaling.GetVRVendorWorkGateSnapshot();
		const uint64_t physicalMutationEpoch =
			upscaling.vrRenderScaleUnresolvedPhysicalMutationEpoch.load(std::memory_order_acquire);
		const uint64_t physicalSerializationEpoch =
			upscaling.vrRenderScalePostMutationSerializationEpoch.load(std::memory_order_acquire);
		const bool emergencyRecoveryRequested =
			upscaling.vrRenderScaleEmergencyRecoveryRequested.load(std::memory_order_acquire);
		const uint32_t frame = globals::state ? globals::state->frameCount : 0;
		const uint64_t tick = QueryQualificationTick();
		const auto mutationBoundary = ReadOwnedMutationBoundary(
			a_transition.transitionID,
			a_transition.ownershipToken);
		const auto replacementPresentationEvidence =
			ReplacementPresentationEvidenceJson(
				upscaling,
				controller,
				mutationBoundary,
				physicalMutationEpoch,
				physicalSerializationEpoch,
				tick,
				frame);
		const auto diagnostics =
			CaptureQualificationDiagnostics(upscaling, controller, session);
		const auto delta = QualificationDiagnosticsDelta(
			diagnostics, a_transition.baseline.diagnostics);

		Snapshot001 apiSnapshot{};
		const auto* api = QueryUpscalingService();
		const auto apiStatus = api && api->GetSnapshot ?
		                           api->GetSnapshot(api->context, &apiSnapshot) :
		                           Status::kServiceUnavailable;
		const bool apiAvailable = apiStatus == Status::kSuccess;
		QualificationTarget target = a_expectedTarget.value_or(QualificationTarget{});
		if (!a_expectedTarget && apiAvailable &&
			(apiSnapshot.profilePresence & kProfileStable) != 0) {
			target = QualificationPolicy::ExactObservationTarget(
				QualificationProfile(apiSnapshot.stable, true));
		}
		const bool targetAvailable = QualificationPolicy::IsValidTarget(target);

		const auto* player = RE::PlayerCharacter::GetSingleton();
		const auto* cell = player ? player->GetParentCell() : nullptr;
		const uint32_t currentCellFormID = cell ? cell->GetFormID() : 0;
		const std::string currentCellEditorID = cell ? Util::GetFormEditorID(cell) : "";
		const bool formIDMatches =
			!a_expectedCell.formID || currentCellFormID == *a_expectedCell.formID;
		const bool editorIDMatches = !a_expectedCell.editorID ||
		                             EditorIDsEqual(currentCellEditorID, *a_expectedCell.editorID);
		const bool exactCell = formIDMatches && editorIDMatches;
		const bool destinationDiffersFromSource =
			(a_expectedCell.formID &&
				*a_expectedCell.formID != a_transition.baseline.sourceCellFormID) ||
			(a_expectedCell.editorID &&
				!EditorIDsEqual(*a_expectedCell.editorID,
					a_transition.baseline.sourceCellEditorID));
		const bool profileStateChanged = apiAvailable &&
		                                 APIProfileStateChanged(apiSnapshot, a_transition.baseline.apiSnapshot);

		QualificationPolicy::StabilityFacts facts;
		facts.providerTerminalClear = true;
		facts.stressSession = session.active &&
		                      session.sessionID == a_transition.baseline.stressSessionID;
		facts.exactCell = exactCell;
		facts.loadedInWorld = player && player->Get3D() && cell &&
		                      gate.completedWorldFrame &&
		                      gate.currentPlayerCellFormID == currentCellFormID &&
		                      gate.lastCompletedWorldRenderFrame != 0;
		facts.freshObservation = QualificationPolicy::IsFreshTransition(
									 exactCell,
									 destinationDiffersFromSource,
									 a_transition.dispatchFrame,
									 frame,
									 profileStateChanged) &&
		                         (a_expectedTarget.has_value() || profileStateChanged);
		facts.publicSnapshot = apiAvailable;

		if (apiAvailable) {
			constexpr uint32_t requiredProfiles =
				kProfileRequested | kProfileEffective | kProfileStable;
			const bool profilesPresent =
				(apiSnapshot.profilePresence & requiredProfiles) == requiredProfiles;
			const bool effectiveProfilePresent =
				(apiSnapshot.profilePresence & kProfileEffective) != 0;
			const bool requestedProfilePresent =
				(apiSnapshot.profilePresence & kProfileRequested) != 0;
			const bool stableProfilePresent =
				(apiSnapshot.profilePresence & kProfileStable) != 0;
			const auto requested = QualificationProfile(
				apiSnapshot.requested, requestedProfilePresent);
			const auto effective = QualificationProfile(
				apiSnapshot.effective, effectiveProfilePresent);
			const auto stable = QualificationProfile(
				apiSnapshot.stable, stableProfilePresent);
			facts.providerReady =
				(apiSnapshot.flags & kSnapshotProviderCheckComplete) != 0;
			facts.profilesAgree = targetAvailable && profilesPresent &&
			                      QualificationPolicy::MatchesTarget(requested, target) &&
			                      QualificationPolicy::MatchesTarget(effective, target) &&
			                      QualificationPolicy::MatchesTarget(stable, target) &&
			                      QualificationPolicy::ProfilesAgree(requested, effective) &&
			                      QualificationPolicy::ProfilesAgree(effective, stable);
			facts.dimensionsPositive = apiSnapshot.displayEyeWidth != 0 &&
			                           apiSnapshot.displayEyeHeight != 0 &&
			                           apiSnapshot.renderEyeWidth != 0 &&
			                           apiSnapshot.renderEyeHeight != 0;
			facts.apiOperationClear = apiSnapshot.activeOperationId == 0 &&
			                          (apiSnapshot.flags & kSnapshotTransitionActive) == 0;
			constexpr uint64_t blockingConditions =
				kConditionRaceSexMenu |
				kConditionRaceSexStartupTail |
				kConditionLoadingTransition |
				kConditionRelatchPending |
				kConditionTransitionPending |
				kConditionOpenCompositeUpscaling |
				kConditionFirstWorldFramePending |
				kConditionPostLoadRecovery |
				kConditionProviderCheckPending |
				kConditionProviderUnavailable |
				kConditionRestartRequired |
				kConditionResourceRecovery;
			facts.apiConditionsClear =
				(apiSnapshot.observedConditions & blockingConditions) == kConditionNone &&
				(apiSnapshot.flags & kSnapshotRestartRequired) == 0;
			const bool providerUnavailable = targetAvailable &&
			                                 QualificationPolicy::UsesVendorEvaluation(target) &&
			                                 (apiSnapshot.flags &
												 kSnapshotProviderCheckComplete) != 0 &&
			                                 (apiSnapshot.observedConditions &
												 kConditionProviderUnavailable) != 0;
			facts.providerTerminalClear = !providerUnavailable;

			constexpr uint64_t renderScaleFlags =
				kSnapshotRenderScaleRequested |
				kSnapshotRenderScaleLatched |
				kSnapshotRenderScaleActive;
			facts.apiActiveContract =
				(apiSnapshot.flags & renderScaleFlags) == renderScaleFlags &&
				apiSnapshot.renderScaleStatus == RenderScaleStatus::kActive &&
				apiSnapshot.transitionState == TransitionState::kActive;
			facts.apiNativeContract =
				(apiSnapshot.flags & renderScaleFlags) == 0 &&
				apiSnapshot.renderScaleStatus == RenderScaleStatus::kDisabled &&
				(apiSnapshot.transitionState == TransitionState::kIdle ||
					apiSnapshot.transitionState == TransitionState::kActive);
		}

		facts.controllerSettled =
			controller.state == Upscaling::VRRenderScaleTransitionState::Idle ||
			controller.state == Upscaling::VRRenderScaleTransitionState::Active;
		facts.workGateClear = !gate.active && gate.activeMask == 0 &&
		                      gate.effectiveLifecycleMask == 0 &&
		                      !gate.lifecycleMutationDeferred &&
		                      !gate.loadingTransitionSerialOpen;
		facts.relatchClear = !gate.relatchQueued && !gate.relatchInProgress &&
		                     !gate.relatchFramePending && !gate.relatchPostLoadSettle &&
		                     !gate.relatchPending && !gate.profileTransitionPending;
		facts.recoveryClear = !gate.recoveryPending && !gate.postLoadResetPending &&
		                      !controller.postLoadRecovery.active &&
		                      !controller.postLoadRecovery.timedAttemptInProgress &&
		                      !emergencyRecoveryRequested;
		facts.memoryTrimClear = !controller.memoryTrim.pending;
		facts.retirementClear = controller.retirement.pendingSets == 0 &&
		                        !controller.retirement.fencePending &&
		                        !controller.retirement.capacityBlocked &&
		                        controller.retirement.nextCleanupFrame == 0 &&
		                        !controller.engineTargetRetirement.pending &&
		                        !controller.engineTargetRetirement.fencePending &&
		                        !controller.engineTargetRetirement.capacityBlocked;
		facts.physicalMutationClear = physicalMutationEpoch == 0 &&
		                              physicalSerializationEpoch == 0 &&
		                              !emergencyRecoveryRequested;
		facts.terminalClear =
			!upscaling.vrRenderScaleTerminalFailureSignaled.load(std::memory_order_acquire) &&
			!upscaling.submitStageDeviceLost.load(std::memory_order_acquire);
		facts.foveationSettingsMatch =
			FoveationSettingsMatch(upscaling.settings, a_foveation) &&
			(!a_foveation || QualificationFoveationValuesMatch(
								 *a_foveation, a_transition.baseline.foveationSettings));
		State::RenderTargetResourcePublicationDiagnostics resourcePublication{
			.expectedWidth = 0,
			.expectedHeight = 0,
		};
		if (apiAvailable) {
			facts.physicalActiveContract = targetAvailable && ActivePhysicalContractStable(
																  upscaling, controller, apiSnapshot, target, a_foveation);
			facts.physicalNativeContract = targetAvailable && NativePhysicalContractStable(
																  upscaling, controller, apiSnapshot, target, a_foveation);
			if (globals::state) {
				resourcePublication =
					globals::state->GetCurrentMainRenderTargetResourcePublicationDiagnostics();
				facts.resourcePublicationCurrent = resourcePublication.current;
			}
		}
		facts.presentationPhaseStable =
			controller.presentationPhase == Upscaling::VRRenderScalePresentationPhase::StereoProven ||
			controller.presentationPhase == Upscaling::VRRenderScalePresentationPhase::Released;
		facts.fidelityStable =
			ActiveFidelityStable(controller, a_transition.dispatchFrame);
		facts.vendorPresentationStable = PresentationEyesStable(
			controller,
			Upscaling::VRRenderScalePresentationPath::VendorEvaluated,
			a_transition.dispatchFrame);
		facts.lifecycleStable = targetAvailable && LifecycleStable(controller, target);
		if (targetAvailable && QualificationPolicy::UsesVendorEvaluation(target)) {
			const auto& lifecycle = target.method == QualificationPolicy::Method::DLSS ?
			                            controller.dlssLifecycle :
			                            controller.fsrLifecycle;
			facts.providerTerminalClear = facts.providerTerminalClear &&
			                              lifecycle.phase !=
			                                  Upscaling::VRVendorRuntimeLifecyclePhase::Failed;
		}
		facts.fsrDispatchStable = FSRDispatchStable(controller);
		const bool shaderCompilationActive =
			globals::shaderCache && globals::shaderCache->IsCompiling();
		facts.shaderCompilationIdle = !shaderCompilationActive;
		facts.nativePresentationStable = PresentationEyesStable(
											 controller,
											 Upscaling::VRRenderScalePresentationPath::NativeOriginal,
											 a_transition.dispatchFrame) &&
		                                 (!targetAvailable ||
											 !QualificationPolicy::UsesNativeVendorEvaluation(target) ||
											 NativeVendorPresentationStable(
												 controller,
												 target,
												 a_transition.dispatchFrame));
		const bool vendorContractStable = targetAvailable &&
		                                  (target.renderScaleMode ?
												  (facts.physicalActiveContract && facts.vendorPresentationStable) :
												  (facts.physicalNativeContract && facts.nativePresentationStable));
		facts.requiredShaderCompilationComplete =
			!targetAvailable || !QualificationPolicy::UsesVendorEvaluation(target) ||
			target.method != QualificationPolicy::Method::FSR ||
			facts.shaderCompilationIdle ||
			(vendorContractStable && facts.fsrDispatchStable);
		const bool dlssLifecycleRelevant = targetAvailable &&
		                                   target.method == QualificationPolicy::Method::DLSS;
		const bool traceApplicable = dlssLifecycleRelevant &&
		                             (a_transition.baseline.diagnostics.dlssTraceActive ||
										 diagnostics.dlssTraceActive);
		auto monotonicCounterRegressions = QualificationMonotonicRegressionsJson(
			diagnostics,
			a_transition.baseline.diagnostics,
			dlssLifecycleRelevant,
			traceApplicable);
		QualificationPolicy::TerminalDiagnosticDeltas terminalDeltas{
			.stressFailureEvents = delta.stressFailureEvents,
			.stressOverwrittenEvents = delta.stressOverwrittenEvents,
			.presentationStretchEyeObservations = delta.presentationStretchEyeObservations,
			.vendorFailureStretchEyeObservations = delta.vendorFailureStretchEyeObservations,
			.boundsMismatchFallbackEyeObservations = delta.boundsMismatchFallbackEyeObservations,
			.fidelityMismatches = delta.fidelityMismatches,
			.transitionFailures = delta.transitionFailures,
			.outOfMemoryFailures = delta.outOfMemoryFailures,
			.deviceLostFailures = delta.deviceLostFailures,
			.relevantLifecycleFailures = dlssLifecycleRelevant ?
			                                 delta.dlssLifecycleFailures :
			                                 delta.fsrLifecycleFailures,
			.memoryTrimFailures = delta.memoryTrimFailures,
			.retirementFenceFailures = delta.retirementFenceFailures,
			.monotonicCounterRegression = !monotonicCounterRegressions.empty(),
			.traceApplicable = traceApplicable,
			.traceSessionLost = a_transition.baseline.diagnostics.dlssTraceActive &&
			                    (!diagnostics.dlssTraceActive ||
									diagnostics.dlssTraceSessionID !=
										a_transition.baseline.diagnostics.dlssTraceSessionID),
			.traceDroppedRecords = delta.dlssTraceDroppedRecords,
			.traceConstantsFailures = delta.dlssTraceConstantsFailures,
			.traceEvaluateFailures = delta.dlssTraceEvaluateFailures,
		};
		facts.diagnosticsClear =
			!QualificationPolicy::HasTerminalDiagnosticFailure(terminalDeltas);
		const auto milestoneEvaluation =
			QualificationPolicy::EvaluateMilestones(target, facts);
		const bool presentationStable = milestoneEvaluation.PresentationStable();
		const bool cleanupDrained = milestoneEvaluation.CleanupDrained();
		const bool strictSatisfied = milestoneEvaluation.StrictSatisfied();
		const bool terminalError = !facts.stressSession || !facts.publicSnapshot ||
		                           !facts.terminalClear ||
		                           !facts.providerTerminalClear ||
		                           QualificationPolicy::HasQualificationControlFailure(
									   terminalDeltas) ||
		                           QualificationPolicy::IsFoveationInvariantViolation(
									   a_foveation.has_value(),
									   facts.foveationSettingsMatch);

		json expectedCell = json::object();
		if (a_expectedCell.formID)
			expectedCell["formId"] = *a_expectedCell.formID;
		if (a_expectedCell.editorID)
			expectedCell["editorId"] = *a_expectedCell.editorID;
		auto terminalDiagnosticReasons =
			QualificationTerminalDiagnosticReasonsJson(terminalDeltas);
		const auto reasonsFor = [&](uint64_t a_failureMask) {
			auto reasons = QualificationFailureReasonsJson(a_failureMask);
			if ((a_failureMask & static_cast<uint64_t>(
									 QualificationPolicy::kFailureDiagnosticDelta)) != 0) {
				for (const auto& reason : terminalDiagnosticReasons)
					reasons.push_back(reason);
			}
			return reasons;
		};
		auto presentationFailureReasons =
			reasonsFor(milestoneEvaluation.presentationFailures);
		auto cleanupFailureReasons =
			reasonsFor(milestoneEvaluation.cleanupFailures);
		auto strictFailureReasons =
			reasonsFor(milestoneEvaluation.strictFailures);
		json output{
			{ "targetMode", a_expectedTarget ? "expected" : "externally_owned_observation" },
			{ "satisfied", strictSatisfied },
			{ "terminalError", terminalError },
			{ "failureMask", milestoneEvaluation.strictFailures },
			{ "failureReasons", strictFailureReasons },
			{ "presentationStable", presentationStable },
			{ "presentationFailureMask", milestoneEvaluation.presentationFailures },
			{ "presentationFailureReasons", std::move(presentationFailureReasons) },
			{ "cleanupDrained", cleanupDrained },
			{ "cleanupFailureMask", milestoneEvaluation.cleanupFailures },
			{ "cleanupFailureReasons", std::move(cleanupFailureReasons) },
			{ "strictSatisfied", strictSatisfied },
			{ "strictFailureMask", milestoneEvaluation.strictFailures },
			{ "strictFailureReasons", std::move(strictFailureReasons) },
			{ "terminalDiagnosticReasons", std::move(terminalDiagnosticReasons) },
			{ "monotonicCounterRegressions", std::move(monotonicCounterRegressions) },
			{ "tick", tick },
			{ "frame", frame },
			{ "expectedCell", std::move(expectedCell) },
			{ "currentCell", {
								 { "formId", currentCellFormID },
								 { "editorId", currentCellEditorID },
							 } },
			{ "destinationDiffersFromSource", destinationDiffersFromSource },
			{ "profileStateChanged", profileStateChanged },
			{ "facts", {
						   { "stressSession", facts.stressSession },
						   { "exactCell", facts.exactCell },
						   { "loadedInWorld", facts.loadedInWorld },
						   { "freshObservation", facts.freshObservation },
						   { "publicSnapshot", facts.publicSnapshot },
						   { "providerReady", facts.providerReady },
						   { "profilesAgree", facts.profilesAgree },
						   { "dimensionsPositive", facts.dimensionsPositive },
						   { "apiOperationClear", facts.apiOperationClear },
						   { "apiConditionsClear", facts.apiConditionsClear },
						   { "controllerSettled", facts.controllerSettled },
						   { "workGateClear", facts.workGateClear },
						   { "relatchClear", facts.relatchClear },
						   { "recoveryClear", facts.recoveryClear },
						   { "memoryTrimClear", facts.memoryTrimClear },
						   { "retirementClear", facts.retirementClear },
						   { "physicalMutationClear", facts.physicalMutationClear },
						   { "terminalClear", facts.terminalClear },
						   { "foveationSettingsMatch", facts.foveationSettingsMatch },
						   { "diagnosticsClear", facts.diagnosticsClear },
						   { "apiActiveContract", facts.apiActiveContract },
						   { "physicalActiveContract", facts.physicalActiveContract },
						   { "presentationPhaseStable", facts.presentationPhaseStable },
						   { "fidelityStable", facts.fidelityStable },
						   { "vendorPresentationStable", facts.vendorPresentationStable },
						   { "lifecycleStable", facts.lifecycleStable },
						   { "fsrDispatchStable", facts.fsrDispatchStable },
						   { "shaderCompilationIdle", facts.shaderCompilationIdle },
						   { "apiNativeContract", facts.apiNativeContract },
						   { "physicalNativeContract", facts.physicalNativeContract },
						   { "nativePresentationStable", facts.nativePresentationStable },
						   { "resourcePublicationCurrent", facts.resourcePublicationCurrent },
						   { "providerTerminalClear", facts.providerTerminalClear },
						   { "requiredShaderCompilationComplete", facts.requiredShaderCompilationComplete },
					   } },
			{ "resourcePublication", {
										 { "current", resourcePublication.current },
										 { "evaluated", resourcePublication.evaluated },
										 { "currentGeneration", resourcePublication.currentGeneration },
										 { "completedGeneration", resourcePublication.completedGeneration },
										 { "publishedGeneration", resourcePublication.publishedGeneration },
										 { "expectedWidth", resourcePublication.expectedWidth },
										 { "expectedHeight", resourcePublication.expectedHeight },
										 { "publishedWidth", resourcePublication.publishedWidth },
										 { "publishedHeight", resourcePublication.publishedHeight },
										 { "loadedFeatureSetupCount", resourcePublication.loadedFeatureSetupCount },
										 { "present", resourcePublication.present },
										 { "complete", resourcePublication.complete },
										 { "deferredSetupAcknowledged", resourcePublication.deferredSetupAcknowledged },
										 { "generationMatchesCurrent", resourcePublication.generationMatchesCurrent },
										 { "generationMatchesCompleted", resourcePublication.generationMatchesCompleted },
										 { "dimensionsMatch", resourcePublication.dimensionsMatch },
										 { "deviceMatches", resourcePublication.deviceMatches },
										 { "contextMatches", resourcePublication.contextMatches },
									 } },
			{ "apiStatus", static_cast<uint32_t>(apiStatus) },
			{ "upscalingSnapshot", APISnapshotJson(apiSnapshot) },
			{ "physical", {
							  { "state", Upscaling::GetVRRenderScaleTransitionStateName(controller.state) },
							  { "physicalPhase", GetPhysicalPhaseName(controller.physicalPhase) },
							  { "presentationPhase", GetPresentationPhaseName(controller.presentationPhase) },
							  { "applied", ProfileJson(controller.applied) },
							  { "stable", ProfileJson(controller.stable) },
							  { "fidelity", {
												{ "bothEyesValid", controller.fidelity.bothEyesValid },
												{ "evaluationEyeMask", controller.fidelity.evaluationEyeMask },
												{ "invariantEyeMask", controller.fidelity.invariantEyeMask },
												{ "lastMismatchMask", controller.fidelity.lastMismatchMask },
												{ "leftFrame", controller.fidelity.eyes[0].frame },
												{ "rightFrame", controller.fidelity.eyes[1].frame },
											} },
							  { "presentation", {
													{ "consecutiveBothEyesVendorFrames", controller.presentation.consecutiveBothEyesVendorFrames },
													{ "leftPath", Upscaling::GetVRRenderScalePresentationPathName(controller.presentation.eyes[0].path) },
													{ "rightPath", Upscaling::GetVRRenderScalePresentationPathName(controller.presentation.eyes[1].path) },
													{ "leftFrame", controller.presentation.eyes[0].frame },
													{ "rightFrame", controller.presentation.eyes[1].frame },
													{ "leftDeviceIdentity", static_cast<uint64_t>(controller.presentation.eyes[0].deviceIdentity) },
													{ "rightDeviceIdentity", static_cast<uint64_t>(controller.presentation.eyes[1].deviceIdentity) },
													{ "leftResourceRevision", controller.presentation.eyes[0].resourceRevision },
													{ "rightResourceRevision", controller.presentation.eyes[1].resourceRevision },
												} },
							  { "dlssLifecycle", LifecycleJson(controller.dlssLifecycle) },
							  { "fsrLifecycle", LifecycleJson(controller.fsrLifecycle) },
						  } },
			{ "nativeVendorExecution", NativeVendorExecutionJson(controller, targetAvailable ? &target : nullptr, a_transition.dispatchFrame) },
			{ "replacementPresentation", replacementPresentationEvidence },
			{ "foveation", ObservedFoveationJson(upscaling, controller.stable) },
			{ "cleanupDebt", QualificationCleanupDebtJson(controller, gate, physicalMutationEpoch, physicalSerializationEpoch, emergencyRecoveryRequested, shaderCompilationActive) },
			{ "diagnostics", {
								 { "baseline", QualificationDiagnosticsJson(a_transition.baseline.diagnostics) },
								 { "current", QualificationDiagnosticsJson(diagnostics) },
								 { "delta", QualificationDiagnosticsDeltaJson(diagnostics, a_transition.baseline.diagnostics) },
							 } },
		};
		if (targetAvailable)
			output["observedTarget"] = QualificationTargetJson(target);
		if (a_expectedTarget)
			output["expectedTarget"] = QualificationTargetJson(*a_expectedTarget);
		if (a_foveation)
			output["foveation"]["target"] = QualificationFoveationTargetJson(*a_foveation);
		if (presentationStable) {
			output["stereoEvidenceClass"] =
				target.renderScaleMode ?
					"render_scale_vendor_frames" :
				QualificationPolicy::UsesNativeVendorEvaluation(target) ?
					"native_vendor_frames" :
					"native_pipeline_frames";
			output["status"] = BuildStatus(upscaling);
		}
		return output;
	}

	json RenderScaleActions()
	{
		return json::array({ "status",
			"qualification_status",
			"qualification_begin",
			"qualification_dispatch",
			"qualification_wait",
			"qualification_cancel",
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
			"texture_lifetime_reset" });
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

	json RunOnMainThread(
		std::function<json()> a_run,
		std::chrono::milliseconds a_timeout = kMainThreadTimeout)
	{
		auto* taskInterface = SKSE::GetTaskInterface();
		if (!taskInterface)
			return { { "error", "SKSE task interface unavailable" } };

		auto promise = std::make_shared<std::promise<json>>();
		auto claim = std::make_shared<CSX::Api::MainThreadDispatchClaim>();
		auto future = promise->get_future();
		taskInterface->AddTask([promise, claim, run = std::move(a_run)]() mutable {
			CSX::Api::EnterRuntimeMainThreadTask();
			if (!claim->TryClaim())
				return;
			try {
				promise->set_value(run());
			} catch (const std::exception& e) {
				promise->set_value(json{ { "error", "main-thread task failed" }, { "detail", e.what() } });
			} catch (...) {
				promise->set_value(json{ { "error", "main-thread task failed" } });
			}
			claim->Complete();
		});

		if (future.wait_for(a_timeout) != std::future_status::ready) {
			if (!claim->TryCancel()) {
				return {
					{ "status", "in_progress" },
					{ "errorCode", "main_thread_in_progress" },
					{ "timeoutMs", a_timeout.count() },
				};
			}
			return {
				{ "error", "main thread did not run before the request deadline" },
				{ "errorCode", "main_thread_timeout" },
				{ "timeoutMs", a_timeout.count() },
			};
		}
		return future.get();
	}

	json BuildQualificationReceipt(
		QualificationTransition& a_transition,
		std::string_view a_outcome,
		const QualificationExpectedCell* a_expectedCell,
		const QualificationTarget* a_target,
		const std::optional<QualificationFoveationTarget>* a_foveation,
		uint64_t a_terminalTick,
		uint32_t a_terminalFrame,
		json a_observation,
		std::optional<std::string_view> a_terminalReason = std::nullopt)
	{
		ImportQualificationReplacementTimeline(a_transition);
		const bool satisfied = a_outcome == "stable" ||
		                       a_outcome == "presentation_stable" ||
		                       a_outcome == "cleanup_drained";
		const bool dispatched = a_transition.dispatchTick != 0;
		const std::string milestoneName{
			QualificationPolicy::GetMilestoneName(a_transition.milestone)
		};
		const char* requestedFailureReasonsField =
			a_transition.milestone == QualificationMilestone::Presentation ?
				"presentationFailureReasons" :
			a_transition.milestone == QualificationMilestone::Cleanup ?
				"cleanupFailureReasons" :
				"strictFailureReasons";
		json reasons = a_observation.is_object() &&
		                       a_observation.contains(requestedFailureReasonsField) ?
		                   a_observation[requestedFailureReasonsField] :
		                   json::array();
		if (!reasons.is_array())
			reasons = json::array();
		if (a_terminalReason) {
			reasons.push_back({
				{ "code", *a_terminalReason },
				{ "category", "qualification" },
			});
		}
		const auto elapsedMilliseconds = [&](const QualificationPolicy::FirstObservation& a_value) {
			return dispatched && a_value.Observed() ?
			           json(QualificationPolicy::ElapsedMilliseconds(
						   a_transition.dispatchTick,
						   a_value.tick,
						   a_transition.baseline.tickFrequency)) :
			           json(nullptr);
		};
		const auto elapsedFrames = [&](const QualificationPolicy::FirstObservation& a_value) {
			return dispatched && a_value.Observed() ?
			           json(QualificationPolicy::ElapsedFrames(
						   a_transition.dispatchFrame,
						   a_value.frame)) :
			           json(nullptr);
		};
		const auto milestoneJson = [&](const QualificationPolicy::FirstObservation& a_value) {
			return json{
				{ "observed", a_value.Observed() },
				{ "tick", a_value.Observed() ? json(a_value.tick) : json(nullptr) },
				{ "frame", a_value.Observed() ? json(a_value.frame) : json(nullptr) },
				{ "elapsedMs", elapsedMilliseconds(a_value) },
				{ "elapsedFrames", elapsedFrames(a_value) },
			};
		};
		const bool presentationAndCleanupObserved =
			a_transition.presentationStable.Observed() &&
			a_transition.cleanupDrained.Observed();
		const auto presentationToCleanupMilliseconds = [&]() -> json {
			if (!presentationAndCleanupObserved ||
				a_transition.baseline.tickFrequency == 0) {
				return nullptr;
			}
			const auto& presentation = a_transition.presentationStable;
			const auto& cleanup = a_transition.cleanupDrained;
			const long double ticks = cleanup.tick >= presentation.tick ?
			                              static_cast<long double>(cleanup.tick - presentation.tick) :
			                              -static_cast<long double>(presentation.tick - cleanup.tick);
			return static_cast<double>(
				(ticks * 1000.0L) /
				static_cast<long double>(a_transition.baseline.tickFrequency));
		};
		const auto presentationToCleanupFrames = [&]() -> json {
			if (!presentationAndCleanupObserved)
				return nullptr;
			return static_cast<int64_t>(a_transition.cleanupDrained.frame) -
			       static_cast<int64_t>(a_transition.presentationStable.frame);
		};
		const json cleanupDeltaMs = presentationToCleanupMilliseconds();
		const json cleanupDeltaFrames = presentationToCleanupFrames();
		const json cleanupTailMs = cleanupDeltaMs.is_number() ?
		                               json(std::max(0.0, cleanupDeltaMs.get<double>())) :
		                               json(nullptr);
		const json cleanupTailFrames = cleanupDeltaFrames.is_number_integer() ?
		                                   json(std::max<int64_t>(0, cleanupDeltaFrames.get<int64_t>())) :
		                                   json(nullptr);
		const bool milestonesShareObservation =
			presentationAndCleanupObserved &&
			a_transition.presentationStable.tick == a_transition.cleanupDrained.tick &&
			a_transition.presentationStable.frame == a_transition.cleanupDrained.frame;
		const auto observationValue = [&](std::string_view a_name, json a_default) {
			const std::string key{ a_name };
			return a_observation.is_object() && a_observation.contains(key) ?
			           a_observation[key] :
			           std::move(a_default);
		};
		const auto evidenceTick = [](const json& a_value) -> uint64_t {
			return a_value.is_object() ?
			           OptionalNonNegativeIntegerOrZero(a_value, "tick") :
			           0;
		};
		const auto phaseMilliseconds = [&](uint64_t a_begin, uint64_t a_end) -> json {
			if (a_begin == 0 || a_end == 0 || a_end < a_begin ||
				a_transition.baseline.tickFrequency == 0) {
				return nullptr;
			}
			return QualificationPolicy::ElapsedMilliseconds(
				a_begin, a_end, a_transition.baseline.tickFrequency);
		};
		json terminalPresentationEvidence =
			a_observation.is_object() &&
					a_observation.contains("replacementPresentation") ?
				a_observation["replacementPresentation"] :
				json(nullptr);
		if (terminalPresentationEvidence.is_object()) {
			terminalPresentationEvidence["tick"] =
				OptionalNonNegativeIntegerOrZero(a_observation, "tick");
			terminalPresentationEvidence["frame"] =
				OptionalNonNegativeIntegerOrZero(a_observation, "frame");
		}
		auto auditSnapshot = a_transition.presentationAudit;
		{
			auto& store = GetQualificationStore();
			std::lock_guard lock(store.mutex);
			if (store.active &&
				QualificationPolicy::OwnsTransitionInstance(
					store.active->transitionID,
					store.active->ownershipToken,
					a_transition.transitionID,
					a_transition.ownershipToken)) {
				auditSnapshot = store.active->presentationAudit;
			}
		}
		const uint64_t blockedTick = evidenceTick(
			a_transition.blockedPreMutationEvidence.is_null() ?
				a_transition.lastPreMutationEvidence :
				a_transition.blockedPreMutationEvidence);
		const uint64_t firstMutationTick = evidenceTick(
			a_transition.firstPhysicalMutationEvidence);
		const uint64_t firstNewGenerationTick = evidenceTick(
			a_transition.firstNewGenerationProvenEvidence);

		json receipt{
			{ "schemaRevision", 14 },
			{ "action", "qualification_wait" },
			{ "transitionId", a_transition.transitionID },
			{ "ownerId", a_transition.ownerID },
			{ "milestone", milestoneName },
			{ "targetMode", a_target ? "expected" : "externally_owned_observation" },
			{ "satisfied", satisfied },
			{ "outcome", a_outcome },
			{ "presentationStable", a_transition.presentationStable.Observed() },
			{ "presentationFailureMask", observationValue(
											 "presentationFailureMask", json(nullptr)) },
			{ "presentationFailureReasons", observationValue(
												"presentationFailureReasons", json::array()) },
			{ "presentationElapsedMs", elapsedMilliseconds(
										   a_transition.presentationStable) },
			{ "presentationElapsedFrames", elapsedFrames(
											   a_transition.presentationStable) },
			{ "cleanupDrained", a_transition.cleanupDrained.Observed() },
			{ "cleanupFailureMask", observationValue(
										"cleanupFailureMask", json(nullptr)) },
			{ "cleanupFailureReasons", observationValue(
										   "cleanupFailureReasons", json::array()) },
			{ "cleanupElapsedMs", elapsedMilliseconds(a_transition.cleanupDrained) },
			{ "cleanupElapsedFrames", elapsedFrames(a_transition.cleanupDrained) },
			{ "strictSatisfied", a_transition.strictSatisfied.Observed() },
			{ "strictFailureMask", observationValue(
									   "strictFailureMask", json(nullptr)) },
			{ "strictFailureReasons", observationValue(
										  "strictFailureReasons", json::array()) },
			{ "strictElapsedMs", elapsedMilliseconds(a_transition.strictSatisfied) },
			{ "strictElapsedFrames", elapsedFrames(a_transition.strictSatisfied) },
			{ "milestoneTimings", {
									  { "presentation", milestoneJson(a_transition.presentationStable) },
									  { "cleanup", milestoneJson(a_transition.cleanupDrained) },
									  { "strict", milestoneJson(a_transition.strictSatisfied) },
									  { "presentationToCleanupMs", cleanupDeltaMs },
									  { "presentationToCleanupFrames", cleanupDeltaFrames },
									  { "cleanupTailMs", cleanupTailMs },
									  { "cleanupTailFrames", cleanupTailFrames },
									  { "sameObservation", milestonesShareObservation },
									  { "cleanupPrecededPresentation",
										  cleanupDeltaMs.is_number() && cleanupDeltaMs.get<double>() < 0.0 },
								  } },
			{ "replacementTimeline", {
										 { "dispatch", a_transition.dispatchPresentationEvidence },
										 { "lastPreMutation", a_transition.lastPreMutationEvidence },
										 { "blockedPreMutation", a_transition.blockedPreMutationEvidence },
										 { "firstPhysicalMutation", a_transition.firstPhysicalMutationEvidence },
										 { "firstPostMutation", a_transition.firstPostMutationEvidence },
										 { "firstNewGenerationProven", a_transition.firstNewGenerationProvenEvidence },
										 { "mutationNotRequiredTerminalProof", a_transition.mutationNotRequiredTerminalProofEvidence },
										 { "terminal", terminalPresentationEvidence },
										 { "mutationExpectation", ReplacementTelemetry::GetMutationExpectationName(a_transition.mutationExpectation) },
										 { "mutationExpectationReason", a_transition.mutationExpectationReason },
									 } },
			{ "presentationCycleAudit", PresentationAuditJson(auditSnapshot) },
			{ "phaseDurations", {
									{ "dispatchToBlockedOrPreparationMs", phaseMilliseconds(a_transition.dispatchTick, blockedTick) },
									{ "blockedOrPreparationToFirstPhysicalMutationMs", phaseMilliseconds(blockedTick, firstMutationTick) },
									{ "firstPhysicalMutationToFirstNewGenerationMs", phaseMilliseconds(firstMutationTick, firstNewGenerationTick) },
									{ "firstNewGenerationToCleanupDrainedMs", phaseMilliseconds(firstNewGenerationTick, a_transition.cleanupDrained.Observed() ? a_transition.cleanupDrained.tick : 0) },
									{ "presentationToStrictCompletionMs", phaseMilliseconds(a_transition.presentationStable.Observed() ? a_transition.presentationStable.tick : 0, a_transition.strictSatisfied.Observed() ? a_transition.strictSatisfied.tick : 0) },
								} },
			{ "outstandingCleanupDebt", observationValue("cleanupDebt", json(nullptr)) },
			{ "baseline", a_transition.ready ? QualificationBaselineJson(a_transition.baseline) : json(nullptr) },
			{ "timing", {
							{ "clock", "query_performance_counter" },
							{ "elapsedOrigin", a_transition.cocCellEditorID ? "coc_command" : "qualification_dispatch" },
							{ "tickFrequency", a_transition.baseline.tickFrequency },
							{ "beginTick", a_transition.ready ? json(a_transition.baseline.beginTick) : json(nullptr) },
							{ "dispatchTick", dispatched ? json(a_transition.dispatchTick) : json(nullptr) },
							{ "presentationStableTick", a_transition.presentationStable.Observed() ? json(a_transition.presentationStable.tick) : json(nullptr) },
							{ "cleanupDrainedTick", a_transition.cleanupDrained.Observed() ? json(a_transition.cleanupDrained.tick) : json(nullptr) },
							{ "strictSatisfiedTick", a_transition.strictSatisfied.Observed() ? json(a_transition.strictSatisfied.tick) : json(nullptr) },
							{ satisfied ? "stableTick" : "terminalTick", a_terminalTick },
							{ QualificationPolicy::kElapsedMillisecondsReceiptField.data(), dispatched ? json(QualificationPolicy::ElapsedMilliseconds(a_transition.dispatchTick, a_terminalTick, a_transition.baseline.tickFrequency)) : json(nullptr) },
							{ QualificationPolicy::kElapsedFramesReceiptField.data(), dispatched ? json(QualificationPolicy::ElapsedFrames(a_transition.dispatchFrame, a_terminalFrame)) : json(nullptr) },
						} },
			{ "frames", {
							{ "begin", a_transition.ready ? json(a_transition.baseline.beginFrame) : json(nullptr) },
							{ "dispatch", dispatched ? json(a_transition.dispatchFrame) : json(nullptr) },
							{ "presentationStable", a_transition.presentationStable.Observed() ? json(a_transition.presentationStable.frame) : json(nullptr) },
							{ "cleanupDrained", a_transition.cleanupDrained.Observed() ? json(a_transition.cleanupDrained.frame) : json(nullptr) },
							{ "strictSatisfied", a_transition.strictSatisfied.Observed() ? json(a_transition.strictSatisfied.frame) : json(nullptr) },
							{ satisfied ? "stable" : "terminal", a_transition.ready ? json(a_terminalFrame) : json(nullptr) },
						} },
			{ "failureReasons", std::move(reasons) },
			{ "observation", std::move(a_observation) },
		};
		if (a_outcome == "timeout")
			receipt["timedOutMilestone"] = milestoneName;
		if (a_expectedCell) {
			json expected = json::object();
			if (a_expectedCell->formID)
				expected["formId"] = *a_expectedCell->formID;
			if (a_expectedCell->editorID)
				expected["editorId"] = *a_expectedCell->editorID;
			receipt["expectedCell"] = std::move(expected);
		}
		if (a_transition.cocCellEditorID)
			receipt["cocCellEditorId"] = *a_transition.cocCellEditorID;
		if (a_target)
			receipt["target"] = QualificationTargetJson(*a_target);
		if (a_foveation && *a_foveation)
			receipt["foveationTarget"] = QualificationFoveationTargetJson(**a_foveation);
		if (receipt["observation"].is_object()) {
			const auto& observation = receipt["observation"];
			if (!a_target && observation.contains("observedTarget"))
				receipt["observedTarget"] = observation["observedTarget"];
			if (observation.contains("stereoEvidenceClass"))
				receipt["stereoEvidenceClass"] = observation["stereoEvidenceClass"];
			if (observation.contains("currentCell"))
				receipt["currentCell"] = observation["currentCell"];
			if (observation.contains("upscalingSnapshot"))
				receipt["upscalingSnapshot"] = observation["upscalingSnapshot"];
			if (observation.contains("physical"))
				receipt["renderScaleHealth"] = observation["physical"];
			if (observation.contains("diagnostics"))
				receipt["diagnostics"] = observation["diagnostics"];
			if (observation.contains("foveation"))
				receipt["foveation"] = observation["foveation"];
			if (observation.contains("nativeVendorExecution"))
				receipt["nativeVendorExecution"] = observation["nativeVendorExecution"];
			if (observation.contains("status"))
				receipt["status"] = observation["status"];
		}
		return receipt;
	}

	std::chrono::milliseconds QualificationDispatchTimeout(
		uint64_t a_now,
		uint64_t a_deadline,
		uint64_t a_frequency)
	{
		if (a_frequency == 0 || a_now >= a_deadline)
			return std::chrono::milliseconds(1);
		const double remaining = QualificationPolicy::ElapsedMilliseconds(
			a_now, a_deadline, a_frequency);
		const auto rounded = static_cast<int64_t>(std::ceil(remaining));
		return std::chrono::milliseconds(
			std::clamp<int64_t>(rounded, 1, kMainThreadTimeout.count()));
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

		if (action == "qualification_status") {
			return {
				{ "action", "qualification_status" },
				{ "qualification", QualificationStateJson() },
			};
		}

		if (action == "qualification_begin") {
			uint64_t transitionID = 0;
			uint64_t ownershipToken = 0;
			std::string ownerID;
			json error;
			if (!TryParsePositiveInteger(
					a_args,
					"transitionId",
					std::numeric_limits<uint64_t>::max(),
					transitionID,
					error) ||
				!TryParseQualificationOwnerID(a_args, ownerID, error)) {
				return error;
			}
			if (!globals::game::isVR) {
				return {
					{ "error", "render-scale qualification requires Skyrim VR" },
					{ "errorCode", "unsupported_runtime" },
				};
			}

			auto& store = GetQualificationStore();
			{
				std::lock_guard lock(store.mutex);
				const uint64_t activeID = store.active ? store.active->transitionID : 0;
				if (!QualificationPolicy::CanBegin(activeID)) {
					return {
						{ "error", "a render-scale qualification transition is already active" },
						{ "errorCode", "qualification_owned" },
						{ "requestedTransitionId", transitionID },
						{ "activeTransitionId", activeID },
					};
				}
				QualificationTransition transition;
				transition.transitionID = transitionID;
				transition.ownerID = ownerID;
				transition.ownershipToken =
					AllocateQualificationOwnershipTokenLocked(store);
				ownershipToken = transition.ownershipToken;
				store.active = std::move(transition);
			}
			bool retainOwnership = false;
			const SKSE::stl::scope_exit releaseOnFailure([&]() noexcept {
				if (!retainOwnership)
					ClearQualificationOwnership(transitionID, ownershipToken);
			});

			auto response = RunOnMainThread([transitionID, ownershipToken, ownerID]() {
				auto& upscaling = globals::features::upscaling;
				QualificationBaseline baseline;
				baseline.beginTick = QueryQualificationTick();
				baseline.tickFrequency = GetQualificationTickFrequency();
				baseline.beginFrame = globals::state ? globals::state->frameCount : 0;
				if (baseline.beginTick == 0 || baseline.tickFrequency == 0) {
					return json{
						{ "error", "QueryPerformanceCounter is unavailable" },
						{ "errorCode", "monotonic_clock_unavailable" },
					};
				}

				const auto session = upscaling.GetVRRenderScaleStressSessionSnapshot();
				if (!session.active) {
					return json{
						{ "error", "start the render-scale stress session before qualification_begin" },
						{ "errorCode", "stress_session_required" },
					};
				}
				const auto* player = RE::PlayerCharacter::GetSingleton();
				const auto* cell = player ? player->GetParentCell() : nullptr;
				const auto gate = upscaling.GetVRVendorWorkGateSnapshot();
				if (!player || !player->Get3D() || !cell ||
					!gate.completedWorldFrame || gate.currentPlayerCellFormID != cell->GetFormID()) {
					return json{
						{ "error", "qualification_begin requires a loaded in-world source cell" },
						{ "errorCode", "source_scene_unavailable" },
					};
				}

				const auto* api = QueryUpscalingService();
				if (!api || !api->GetSnapshot ||
					api->GetSnapshot(api->context, &baseline.apiSnapshot) !=
						CSX::UpscalingAPI::Status::kSuccess) {
					return json{
						{ "error", "public upscaling service snapshot is unavailable" },
						{ "errorCode", "upscaling_service_unavailable" },
					};
				}

				const auto controller = upscaling.GetVRRenderScaleTransitionSnapshot();
				baseline.sourceCellFormID = cell->GetFormID();
				baseline.sourceCellEditorID = Util::GetFormEditorID(cell);
				baseline.stressSessionID = session.sessionID;
				baseline.apiStateRevision = baseline.apiSnapshot.stateRevision;
				baseline.controllerRevision = controller.revision;
				baseline.applied = controller.applied;
				baseline.stable = controller.stable;
				baseline.foveationSettings =
					CaptureQualificationFoveationSettings(upscaling.settings);
				baseline.diagnostics =
					CaptureQualificationDiagnostics(upscaling, controller, session);

				auto& qualificationStore = GetQualificationStore();
				{
					std::lock_guard lock(qualificationStore.mutex);
					if (!qualificationStore.active ||
						!QualificationPolicy::OwnsTransitionInstance(
							qualificationStore.active->transitionID,
							qualificationStore.active->ownershipToken,
							transitionID,
							ownershipToken) ||
						qualificationStore.active->cancelled->load(std::memory_order_acquire)) {
						return json{
							{ "error", "qualification ownership was cancelled during begin" },
							{ "errorCode", "qualification_cancelled" },
						};
					}
					qualificationStore.active->baseline = baseline;
					qualificationStore.active->ready = true;
				}
				return json{
					{ "action", "qualification_begin" },
					{ "transitionId", transitionID },
					{ "ownerId", ownerID },
					{ "accepted", true },
					{ "baseline", QualificationBaselineJson(baseline) },
				};
			});
			if (response.contains("error")) {
				json evidence{
					{ "action", "qualification_begin" },
					{ "transitionId", transitionID },
					{ "ownerId", ownerID },
					{ "satisfied", false },
					{ "outcome", "error" },
					{ "error", response.value("error", std::string("qualification_begin failed")) },
					{ "errorCode", response.value("errorCode", std::string("qualification_begin_failed")) },
					{ "failure", response },
				};
				FinishQualification(transitionID, ownershipToken, evidence);
				return evidence;
			}
			response["qualification"] = QualificationStateJson();
			retainOwnership = true;
			return response;
		}

		if (action == "qualification_dispatch") {
			uint64_t transitionID = 0;
			uint64_t ownershipToken = 0;
			std::string ownerID;
			std::optional<std::string> cocCellEditorID;
			bool startPerformanceTelemetry = false;
			json error;
			if (!TryParsePositiveInteger(
					a_args,
					"transitionId",
					std::numeric_limits<uint64_t>::max(),
					transitionID,
					error) ||
				!TryParseQualificationOwnerID(a_args, ownerID, error) ||
				!TryParseQualificationCocCellEditorID(
					a_args, cocCellEditorID, error)) {
				return error;
			}
			if (a_args.contains("startPerformanceTelemetry")) {
				if (!a_args["startPerformanceTelemetry"].is_boolean()) {
					return {
						{ "error", "startPerformanceTelemetry must be a boolean" },
						{ "errorCode", "invalid_start_performance_telemetry" },
					};
				}
				startPerformanceTelemetry =
					a_args["startPerformanceTelemetry"].get<bool>();
			}

			{
				auto& store = GetQualificationStore();
				std::lock_guard lock(store.mutex);
				if (!store.active || store.active->transitionID != transitionID ||
					store.active->ownerID != ownerID) {
					return {
						{ "error", "qualification_dispatch does not own the active transition" },
						{ "errorCode", "qualification_owner_mismatch" },
						{ "requestedTransitionId", transitionID },
						{ "requestedOwnerId", ownerID },
						{ "activeTransitionId", store.active ? store.active->transitionID : 0 },
						{ "activeOwnerId", store.active ? store.active->ownerID : std::string{} },
					};
				}
				if (!store.active->ready) {
					return {
						{ "error", "qualification_begin has not completed" },
						{ "errorCode", "qualification_not_armed" },
					};
				}
				if (store.active->cancelled->load(std::memory_order_acquire)) {
					return {
						{ "error", "qualification transition is being cancelled" },
						{ "errorCode", "qualification_cancelled" },
					};
				}
				if (store.active->waitInProgress || store.active->dispatchTick != 0) {
					return {
						{ "error", "qualification dispatch was already marked" },
						{ "errorCode", "qualification_dispatch_already_marked" },
					};
				}
				ownershipToken = store.active->ownershipToken;
			}

			auto response = RunOnMainThread(
				[transitionID,
					ownershipToken,
					ownerID,
					cocCellEditorID,
					startPerformanceTelemetry]() {
					const uint64_t observationTick = QueryQualificationTick();
					const uint32_t observationFrame =
						globals::state ?
							globals::state->frameCountAtomic.load(std::memory_order_relaxed) :
							0u;
					if (observationTick == 0) {
						return json{
							{ "error", "QueryPerformanceCounter is unavailable" },
							{ "errorCode", "monotonic_clock_unavailable" },
						};
					}
					auto& upscaling = globals::features::upscaling;
					const auto controller =
						upscaling.GetVRRenderScaleTransitionSnapshot();
					json dispatchPresentationEvidence =
						ReplacementPresentationEvidenceJson(
							upscaling,
							controller,
							{},
							upscaling.vrRenderScaleUnresolvedPhysicalMutationEpoch.load(
								std::memory_order_acquire),
							upscaling.vrRenderScalePostMutationSerializationEpoch.load(
								std::memory_order_acquire),
							observationTick,
							observationFrame);
					dispatchPresentationEvidence["observationTick"] = observationTick;
					dispatchPresentationEvidence["observationFrame"] = observationFrame;
					auto& store = GetQualificationStore();
					std::lock_guard lock(store.mutex);
					if (!store.active ||
						!QualificationPolicy::OwnsTransitionInstance(
							store.active->transitionID,
							store.active->ownershipToken,
							transitionID,
							ownershipToken) ||
						store.active->ownerID != ownerID ||
						store.active->cancelled->load(std::memory_order_acquire)) {
						return json{
							{ "error", "qualification ownership changed before dispatch" },
							{ "errorCode", "qualification_owner_mismatch" },
						};
					}
					if (store.active->dispatchTick != 0) {
						return json{
							{ "error", "qualification dispatch was already marked" },
							{ "errorCode", "qualification_dispatch_already_marked" },
						};
					}

					if (startPerformanceTelemetry &&
						(upscaling.IsVRRenderScaleCPUPerformanceTelemetryActive() ||
							upscaling.IsVRRenderScaleGPUPerformanceTelemetryActive())) {
						return json{
							{ "error", "qualification dispatch requires inactive CPU and GPU performance telemetry" },
							{ "errorCode", "performance_telemetry_already_active" },
							{ "cpuPerformance", CPUPerformanceJson(upscaling) },
							{ "gpuPerformance", BuildGPUPerformanceStatus(upscaling) },
						};
					}

					const uint64_t dispatchTick = QueryQualificationTick();
					const uint32_t dispatchFrame =
						globals::state ?
							globals::state->frameCountAtomic.load(std::memory_order_relaxed) :
							0u;
					if (dispatchTick == 0) {
						return json{
							{ "error", "QueryPerformanceCounter became unavailable" },
							{ "errorCode", "monotonic_clock_unavailable" },
						};
					}
					json performanceTelemetry = nullptr;
					if (startPerformanceTelemetry) {
						const uint64_t cpuSessionID =
							upscaling.StartVRRenderScaleCPUPerformanceTelemetry(
								dispatchFrame);
						if (cpuSessionID == 0) {
							return json{
								{ "error", "the CPU telemetry session ID allocator failed" },
								{ "errorCode", "cpu_performance_session_id_unavailable" },
								{ "cpuPerformance", CPUPerformanceJson(upscaling) },
							};
						}
						upscaling.StartVRRenderScaleGPUPerformanceTelemetry(
							dispatchFrame);
						const auto cpuSnapshot =
							upscaling.GetVRRenderScaleCPUPerformanceSnapshot();
						const auto gpuSnapshot =
							upscaling.GetVRRenderScaleGPUPerformanceSnapshot();
						const uint64_t cpuStartFrame = cpuSnapshot[static_cast<std::size_t>(
							Upscaling::VRRenderScaleCPUPerformanceCounter::WindowStartFrame)];
						const uint64_t gpuStartFrame = gpuSnapshot[static_cast<std::size_t>(
							Upscaling::VRRenderScaleGPUPerformanceCounter::WindowStartFrame)];
						if (cpuStartFrame != dispatchFrame ||
							gpuStartFrame != dispatchFrame) {
							upscaling.StopVRRenderScaleGPUPerformanceTelemetry();
							upscaling.StopVRRenderScaleCPUPerformanceTelemetry();
							return json{
								{ "error", "performance telemetry did not bind to the qualification dispatch frame" },
								{ "errorCode", "performance_telemetry_dispatch_frame_mismatch" },
								{ "dispatchFrame", dispatchFrame },
								{ "cpuStartFrame", cpuStartFrame },
								{ "gpuStartFrame", gpuStartFrame },
							};
						}
						performanceTelemetry = {
							{ "started", true },
							{ "dispatchFrame", dispatchFrame },
							{ "cpuPerformance", CPUPerformanceJson(upscaling) },
							{ "gpuPerformance", BuildGPUPerformanceStatus(upscaling) },
						};
					}
					dispatchPresentationEvidence["tick"] = dispatchTick;
					dispatchPresentationEvidence["frame"] = dispatchFrame;
					if (cocCellEditorID) {
						const auto command = std::format("coc {}", *cocCellEditorID);
						RE::Console::ExecuteCommand(command.c_str());
					}
					store.active->dispatchTick = dispatchTick;
					store.active->dispatchFrame = dispatchFrame;
					store.active->cocCellEditorID = cocCellEditorID;
					store.active->dispatchPresentationEvidence =
						std::move(dispatchPresentationEvidence);
					store.active->presentationAudit = {};
					store.active->presentationAudit.active = true;
					store.active->presentationAudit.ownerTransitionID = transitionID;
					store.active->presentationAudit.ownerToken = ownershipToken;
					json result{
						{ "action", "qualification_dispatch" },
						{ "transitionId", transitionID },
						{ "ownerId", ownerID },
						{ "accepted", true },
						{ "timing", {
										{ "clock", "query_performance_counter" },
										{ "elapsedOrigin", cocCellEditorID ?
															   "coc_command" :
															   "qualification_dispatch" },
										{ "dispatchTick", dispatchTick },
										{ "tickFrequency", store.active->baseline.tickFrequency },
									} },
						{ "dispatchFrame", dispatchFrame },
						{ "replacementPresentation", store.active->dispatchPresentationEvidence },
					};
					if (cocCellEditorID) {
						result["cocCellEditorId"] = *cocCellEditorID;
						result["cocIssued"] = true;
					}
					if (startPerformanceTelemetry)
						result["performanceTelemetry"] = std::move(performanceTelemetry);
					return result;
				});
			if (!response.contains("error"))
				response["qualification"] = QualificationStateJson();
			return response;
		}

		if (action == "qualification_cancel") {
			uint64_t transitionID = 0;
			std::string ownerID;
			json error;
			if (!TryParsePositiveInteger(
					a_args,
					"transitionId",
					std::numeric_limits<uint64_t>::max(),
					transitionID,
					error) ||
				!TryParseQualificationOwnerID(a_args, ownerID, error)) {
				return error;
			}

			QualificationTransition transition;
			{
				auto& store = GetQualificationStore();
				std::lock_guard lock(store.mutex);
				if (!store.active) {
					if (!QualificationEvidenceOwnedBy(
							store.lastEvidence, transitionID, ownerID)) {
						return {
							{ "error", "qualification_cancel does not own an active or retained transition" },
							{ "errorCode", "qualification_owner_mismatch" },
							{ "cancelled", false },
							{ "requestedTransitionId", transitionID },
							{ "requestedOwnerId", ownerID },
							{ "activeTransitionId", 0 },
							{ "activeOwnerId", "" },
						};
					}
					return {
						{ "action", "qualification_cancel" },
						{ "cancelled", false },
						{ "alreadyInactive", true },
						{ "transitionId", transitionID },
						{ "ownerId", ownerID },
						{ "lastEvidence", store.lastEvidence },
					};
				}
				if (transitionID != store.active->transitionID ||
					ownerID != store.active->ownerID) {
					return {
						{ "error", "qualification_cancel does not own the active transition" },
						{ "errorCode", "qualification_owner_mismatch" },
						{ "cancelled", false },
						{ "requestedTransitionId", transitionID },
						{ "requestedOwnerId", ownerID },
						{ "activeTransitionId", store.active->transitionID },
						{ "activeOwnerId", store.active->ownerID },
					};
				}
				transition = *store.active;
				transition.cancelled->store(true, std::memory_order_release);
			}
			if (transition.waitInProgress) {
				return {
					{ "action", "qualification_cancel" },
					{ "transitionId", transitionID },
					{ "ownerId", ownerID },
					{ "cancellationRequested", true },
					{ "cancelled", false },
					{ "outcome", "cancellation_requested" },
				};
			}

			bool completed = false;
			const SKSE::stl::scope_exit releaseOnFailure([&]() noexcept {
				if (!completed)
					ClearQualificationOwnership(
						transitionID, transition.ownershipToken);
			});
			const uint64_t tick = QueryQualificationTick();
			const uint32_t frame = transition.dispatchTick != 0 ?
			                           transition.dispatchFrame :
			                           transition.baseline.beginFrame;
			auto evidence = BuildQualificationReceipt(
				transition, "cancelled", nullptr, nullptr, nullptr,
				tick, frame, nullptr,
				"qualification_cancelled");
			evidence["action"] = "qualification_cancel";
			evidence["ownerId"] = ownerID;
			evidence["cancelled"] = true;
			FinishQualification(
				transitionID, transition.ownershipToken, evidence);
			completed = true;
			return evidence;
		}

		if (action == "qualification_wait") {
			uint64_t transitionID = 0;
			uint64_t timeoutMs = kQualificationMaximumTimeoutMs;
			std::string ownerID;
			QualificationMilestone milestone = QualificationMilestone::Strict;
			json error;
			if (!TryParsePositiveInteger(
					a_args,
					"transitionId",
					std::numeric_limits<uint64_t>::max(),
					transitionID,
					error) ||
				!TryParseQualificationOwnerID(a_args, ownerID, error)) {
				return error;
			}
			if (a_args.contains("timeoutMs") &&
				!TryParsePositiveInteger(
					a_args,
					"timeoutMs",
					kQualificationMaximumTimeoutMs,
					timeoutMs,
					error)) {
				return error;
			}
			QualificationExpectedCell expectedCell;
			std::optional<QualificationTarget> target;
			std::optional<QualificationFoveationTarget> foveation;
			if (!TryParseQualificationMilestone(a_args, milestone, error) ||
				!TryParseExpectedCell(a_args, expectedCell, error) ||
				!TryParseQualificationTarget(a_args, target, error) ||
				!TryParseQualificationFoveationTarget(a_args, foveation, error)) {
				return error;
			}
			const auto controllerAtWait = target ?
			                                  globals::features::upscaling.GetVRRenderScaleTransitionSnapshot() :
			                                  Upscaling::VRRenderScaleTransitionSnapshot{};

			QualificationTransition transition;
			{
				auto& store = GetQualificationStore();
				std::lock_guard lock(store.mutex);
				const uint64_t activeID = store.active ? store.active->transitionID : 0;
				if (!store.active ||
					!QualificationPolicy::OwnsTransition(activeID, transitionID) ||
					store.active->ownerID != ownerID) {
					return {
						{ "error", "qualification_wait does not own the active transition" },
						{ "errorCode", "qualification_owner_mismatch" },
						{ "requestedTransitionId", transitionID },
						{ "requestedOwnerId", ownerID },
						{ "activeTransitionId", activeID },
						{ "activeOwnerId", store.active ? store.active->ownerID : std::string{} },
						{ "lastEvidence", store.lastEvidence },
					};
				}
				if (store.active->cancelled->load(std::memory_order_acquire)) {
					return {
						{ "error", "qualification transition is being cancelled" },
						{ "errorCode", "qualification_cancelled" },
						{ "transitionId", transitionID },
					};
				}
				if (!store.active->ready) {
					return {
						{ "error", "qualification_begin has not completed" },
						{ "errorCode", "qualification_not_armed" },
					};
				}
				if (store.active->dispatchTick == 0) {
					return {
						{ "error", "qualification_dispatch has not completed" },
						{ "errorCode", "qualification_dispatch_required" },
					};
				}
				if (store.active->waitInProgress) {
					return {
						{ "error", "qualification_wait is already active for this transition" },
						{ "errorCode", "qualification_wait_active" },
					};
				}
				if (target) {
					SeedNativeTargetMutationExpectation(
						*store.active, *target, controllerAtWait);
				}
				store.active->expectedTarget = target;
				store.active->waitInProgress = true;
				store.active->milestone = milestone;
				transition = *store.active;
			}
			const SKSE::stl::scope_exit releaseWait([&]() noexcept {
				ReleaseQualificationWait(
					transitionID, transition.ownershipToken);
			});

			const uint64_t deadline = QualificationPolicy::SaturatingDeadlineTick(
				transition.dispatchTick,
				timeoutMs,
				transition.baseline.tickFrequency);
			json lastObservation = nullptr;
			bool replacementTimelineFailureReported = false;
			for (;;) {
				const uint64_t beforeTick = QueryQualificationTick();
				const uint32_t lastFrame = lastObservation.is_object() ?
				                               lastObservation.value("frame", transition.dispatchFrame) :
				                               transition.dispatchFrame;
				if (transition.cancelled->load(std::memory_order_acquire)) {
					auto receipt = BuildQualificationReceipt(
						transition, "cancelled", &expectedCell,
						target ? &*target : nullptr, &foveation,
						beforeTick, lastFrame, std::move(lastObservation),
						"qualification_cancelled");
					receipt["error"] = "qualification transition was cancelled";
					FinishQualification(
						transitionID, transition.ownershipToken, receipt);
					return receipt;
				}
				if (!QualificationPolicy::IsWithinDeadline(beforeTick, deadline)) {
					auto receipt = BuildQualificationReceipt(
						transition, "timeout", &expectedCell,
						target ? &*target : nullptr, &foveation,
						beforeTick, lastFrame, std::move(lastObservation),
						"qualification_timeout");
					receipt["timing"]["timeoutMs"] = timeoutMs;
					FinishQualification(
						transitionID, transition.ownershipToken, receipt);
					return receipt;
				}

				auto observation = RunOnMainThread(
					[transition, expectedCell, target, foveation]() {
						return CaptureQualificationObservation(
							transition, expectedCell, target, foveation);
					},
					QualificationDispatchTimeout(
						beforeTick, deadline, transition.baseline.tickFrequency));
				if (transition.cancelled->load(std::memory_order_acquire)) {
					const uint64_t tick = QueryQualificationTick();
					const uint32_t frame = observation.value("frame", lastFrame);
					auto receipt = BuildQualificationReceipt(
						transition, "cancelled", &expectedCell,
						target ? &*target : nullptr, &foveation,
						tick, frame, std::move(observation),
						"qualification_cancelled");
					receipt["error"] = "qualification transition was cancelled";
					FinishQualification(
						transitionID, transition.ownershipToken, receipt);
					return receipt;
				}
				if (observation.contains("error")) {
					const auto errorCode = observation.value("errorCode", std::string{});
					if (QualificationPolicy::IsTransientObservationDispatchError(errorCode))
						continue;
					const uint64_t tick = QueryQualificationTick();
					auto receipt = BuildQualificationReceipt(
						transition, "error", &expectedCell,
						target ? &*target : nullptr, &foveation,
						tick, lastFrame, std::move(lastObservation),
						"qualification_observation_error");
					receipt["error"] = "qualification observation failed";
					receipt["errorCode"] = "qualification_observation_error";
					receipt["failure"] = std::move(observation);
					FinishQualification(
						transitionID, transition.ownershipToken, receipt);
					return receipt;
				}

				const uint64_t observedTick = observation.value("tick", QueryQualificationTick());
				const uint32_t observedFrame = observation.value("frame", lastFrame);
				const bool withinDeadline =
					QualificationPolicy::IsWithinDeadline(observedTick, deadline);
				const QualificationPolicy::MilestoneEvaluation milestoneEvaluation{
					.presentationFailures = observation.value(
						"presentationFailureMask",
						std::numeric_limits<uint64_t>::max()),
					.cleanupFailures = observation.value(
						"cleanupFailureMask",
						std::numeric_limits<uint64_t>::max()),
					.strictFailures = observation.value(
						"strictFailureMask",
						std::numeric_limits<uint64_t>::max()),
				};
				if (withinDeadline) {
					RecordQualificationMilestones(
						transition,
						milestoneEvaluation.PresentationStable(),
						milestoneEvaluation.CleanupDrained(),
						milestoneEvaluation.StrictSatisfied(),
						observedTick,
						observedFrame);
					TryRecordQualificationReplacementTimeline(
						transition,
						observation,
						replacementTimelineFailureReported);
				}
				if (QualificationPolicy::IsMilestoneSatisfied(
						milestone, milestoneEvaluation) &&
					withinDeadline) {
					const std::string_view outcome =
						milestone == QualificationMilestone::Presentation ?
							"presentation_stable" :
						milestone == QualificationMilestone::Cleanup ?
							"cleanup_drained" :
							"stable";
					auto receipt = BuildQualificationReceipt(
						transition, outcome, &expectedCell,
						target ? &*target : nullptr, &foveation,
						observedTick, observedFrame, std::move(observation));
					receipt["timing"]["timeoutMs"] = timeoutMs;
					FinishQualification(
						transitionID, transition.ownershipToken, receipt);
					return receipt;
				}
				if (observation.value("terminalError", false)) {
					auto receipt = BuildQualificationReceipt(
						transition, "error", &expectedCell,
						target ? &*target : nullptr, &foveation,
						observedTick, observedFrame, std::move(observation),
						"qualification_terminal_error");
					receipt["error"] = "qualification reached a terminal state";
					FinishQualification(
						transitionID, transition.ownershipToken, receipt);
					return receipt;
				}
				if (!withinDeadline) {
					auto receipt = BuildQualificationReceipt(
						transition, "timeout", &expectedCell,
						target ? &*target : nullptr, &foveation,
						observedTick, observedFrame, std::move(observation),
						"qualification_timeout");
					receipt["timing"]["timeoutMs"] = timeoutMs;
					FinishQualification(
						transitionID, transition.ownershipToken, receipt);
					return receipt;
				}
				lastObservation = std::move(observation);
				std::this_thread::yield();
			}
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
			std::optional<uint64_t> expectedSessionID;
			json error;
			if (!TryParseOptionalIntegerExpectation(
					a_args,
					"expectedSessionId",
					1,
					"invalid_expected_session_id",
					expectedSessionID,
					error)) {
				return error;
			}
			auto& streamline = globals::features::upscaling.streamline;
			const auto capture = streamline.GetDLSSDevBenchTraceSnapshot(false);
			if (!capture.active) {
				return {
					{ "error", "no DLSS dispatch trace is active" },
					{ "capture", DLSSDevBenchTraceSummaryJson(capture) },
				};
			}
			if (expectedSessionID && capture.sessionID != *expectedSessionID) {
				return {
					{ "error", "dlss_trace_stop does not own the active trace session" },
					{ "errorCode", "dlss_trace_session_mismatch" },
					{ "expectedSessionId", *expectedSessionID },
					{ "activeSessionId", capture.sessionID },
					{ "capture", DLSSDevBenchTraceSummaryJson(capture) },
				};
			}
			if (!streamline.StopDLSSDevBenchTrace(expectedSessionID.value_or(0))) {
				const auto current = streamline.GetDLSSDevBenchTraceSnapshot(false);
				json response{
					{ "error", "the DLSS dispatch trace changed before it could be stopped" },
					{ "capture", DLSSDevBenchTraceSummaryJson(current) },
				};
				if (expectedSessionID) {
					response["errorCode"] = "dlss_trace_session_mismatch";
					response["expectedSessionId"] = *expectedSessionID;
					response["activeSessionId"] = current.active ? current.sessionID : 0;
				}
				return response;
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
				const uint64_t sessionID =
					upscaling.StartVRRenderScaleCPUPerformanceTelemetry();
				if (sessionID == 0) {
					return json{
						{ "error", "the CPU telemetry session ID allocator failed" },
						{ "errorCode", "cpu_performance_session_id_unavailable" },
						{ "cpuPerformance", CPUPerformanceJson(upscaling) },
					};
				}
				return json{
					{ "action", "cpu_performance_start" },
					{ "cpuPerformance", CPUPerformanceJson(upscaling) },
				};
			});
		}

		if (action == "cpu_performance_stop") {
			std::optional<uint64_t> expectedSessionID;
			std::optional<uint64_t> expectedStartFrame;
			json error;
			if (!TryParseOptionalIntegerExpectation(
					a_args,
					"expectedSessionId",
					1,
					"invalid_expected_session_id",
					expectedSessionID,
					error) ||
				!TryParseOptionalIntegerExpectation(
					a_args,
					"expectedStartFrame",
					0,
					"invalid_expected_start_frame",
					expectedStartFrame,
					error)) {
				return error;
			}
			return RunOnMainThread([expectedSessionID, expectedStartFrame]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale CPU telemetry requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				if (!upscaling.IsVRRenderScaleCPUPerformanceTelemetryActive()) {
					return json{
						{ "error", "no render-scale CPU telemetry capture is active" },
						{ "cpuPerformance", CPUPerformanceJson(upscaling) },
					};
				}
				const uint64_t activeSessionID =
					upscaling.GetVRRenderScaleCPUPerformanceSessionID();
				if (expectedSessionID && activeSessionID != *expectedSessionID) {
					return json{
						{ "error", "cpu_performance_stop does not own the active capture session" },
						{ "errorCode", "cpu_performance_session_mismatch" },
						{ "expectedSessionId", *expectedSessionID },
						{ "activeSessionId", activeSessionID },
						{ "cpuPerformance", CPUPerformanceJson(upscaling) },
					};
				}
				const auto snapshot =
					upscaling.GetVRRenderScaleCPUPerformanceSnapshot();
				const uint64_t activeStartFrame = snapshot[static_cast<std::size_t>(
					Upscaling::VRRenderScaleCPUPerformanceCounter::WindowStartFrame)];
				if (expectedStartFrame && activeStartFrame != *expectedStartFrame) {
					return json{
						{ "error", "cpu_performance_stop does not own the active capture window" },
						{ "errorCode", "cpu_performance_start_frame_mismatch" },
						{ "expectedStartFrame", *expectedStartFrame },
						{ "activeStartFrame", activeStartFrame },
						{ "activeSessionId", activeSessionID },
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
			std::optional<uint64_t> expectedStartFrame;
			json error;
			if (!TryParseOptionalIntegerExpectation(
					a_args,
					"expectedStartFrame",
					0,
					"invalid_expected_start_frame",
					expectedStartFrame,
					error)) {
				return error;
			}
			return RunOnMainThread([expectedStartFrame]() {
				if (!globals::game::isVR)
					return json{ { "error", "GPU performance capture requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				if (!upscaling.IsVRRenderScaleGPUPerformanceTelemetryActive())
					return json{ { "error", "no GPU performance capture is active" }, { "capture", BuildGPUPerformanceStatus(upscaling) } };
				const auto snapshot = upscaling.GetVRRenderScaleGPUPerformanceSnapshot();
				const uint64_t activeStartFrame = snapshot[static_cast<std::size_t>(
					Upscaling::VRRenderScaleGPUPerformanceCounter::WindowStartFrame)];
				if (expectedStartFrame && activeStartFrame != *expectedStartFrame) {
					return json{
						{ "error", "gpu_performance_stop does not own the active capture window" },
						{ "errorCode", "gpu_performance_start_frame_mismatch" },
						{ "expectedStartFrame", *expectedStartFrame },
						{ "activeStartFrame", activeStartFrame },
						{ "capture", BuildGPUPerformanceStatus(upscaling) },
					};
				}
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
			std::optional<uint64_t> expectedSessionID;
			json error;
			if (!TryParseOptionalIntegerExpectation(
					a_args,
					"expectedSessionId",
					1,
					"invalid_expected_session_id",
					expectedSessionID,
					error)) {
				return error;
			}
			return RunOnMainThread([expectedSessionID]() {
				if (!globals::game::isVR)
					return json{ { "error", "render-scale iteration control requires Skyrim VR" } };
				auto& upscaling = globals::features::upscaling;
				const auto session = upscaling.GetVRRenderScaleStressSessionSnapshot();
				if (!session.active)
					return json{ { "error", "no stress capture is active" }, { "status", BuildStatus(upscaling) } };
				if (expectedSessionID && session.sessionID != *expectedSessionID) {
					return json{
						{ "error", "stop does not own the active stress capture" },
						{ "errorCode", "stress_session_mismatch" },
						{ "expectedSessionId", *expectedSessionID },
						{ "activeSessionId", session.sessionID },
						{ "status", BuildStatus(upscaling) },
					};
				}
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
				const bool directMenuEdit = true;
				const auto applied = upscaling.ApplyCSMenuUpscalingTransition(
					method,
					enabled,
					qualityMode,
					dlssPreset,
					"devbench render-scale iteration",
					Upscaling::VRUpscalingTransitionOrigin::CSMenu,
					0,
					std::nullopt,
					VRVendorRelatchPolicy::StartupNativeFallbackControl::None,
					directMenuEdit);

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
					{ "directMenuEdit", directMenuEdit },
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
			{ "usage", R"(Invoke the top-level devbench tool with {"action":"status"} when exposed. Status includes bounded, exact-owner render-scale preparation stage telemetry while a stress session is active. For a server-side transition barrier, reserve a caller-owned ID and ownerId with qualification_begin, mark the server QPC immediately before the command with qualification_dispatch, dispatch exactly one COC or apply, then call qualification_wait with the same ownership pair, exact destination, optional target profile, and bounded timeout. Omit target when an external controller owns profile selection; the waiter then requires a post-dispatch profile change and returns the first mutually coherent observed profile without mutating it. qualification_cancel requires the same ownership pair and requests a terminal cancellation receipt without releasing an active waiter early. Pass expectedSessionId to stop, dlss_trace_stop, or cpu_performance_stop to refuse cleanup when the active capture is not the caller's. Pass expectedStartFrame to gpu_performance_stop as its ownership guard; it remains a legacy optional secondary guard for CPU telemetry. CPU performance status, start, and stop responses return cpuPerformance.sessionId; stop retains it and reset clears it to zero. If dynamic tools are unavailable, use equivalent communityshaders.renderscale steps in devbench scenario.)" },
			{ "actions", RenderScaleActions() },
		};
		result["usage"] =
			"qualification_dispatch accepts optional cocCellEditorId to execute "
			"exactly one validated COC on its main-thread operation. When present, "
			"the QPC timer is read immediately before that command and "
			"startPerformanceTelemetry binds CPU/GPU counters before it. Omit "
			"target when an external controller owns profile selection. Native targets "
			"require coherent requested, effective, and stable public profiles plus "
			"an exact native presentation; "
			"qualification_wait then observes the coherent selected profile without "
			"mutating it. milestone defaults to strict for backward compatibility; "
			"presentation and cleanup expose their independent first-observed "
			"timestamps, signed presentation-to-cleanup deltas, and cleanup tail "
			"without changing strict qualification. The schema-revision-12 "
			"terminal receipt retains the immutable dispatch, blocked and last "
			"pre-mutation, first physical mutation, first post-mutation, first "
			"proven new-generation, exact owner-bound no-mutation proof, and "
			"terminal facets. It also reports the "
			"owner-bound authoritative compositor-cycle audit and independent "
			"preparation and replacement-mutation admissions.";
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
	void RecordPhysicalMutationBoundary(
		std::uint64_t a_transitionEpoch,
		PhysicalMutationBoundarySource a_source,
		std::uint32_t a_providerMethod) noexcept
	{
		if (a_transitionEpoch == 0)
			return;
		try {
			auto& upscaling = globals::features::upscaling;
			const auto controller =
				upscaling.GetVRRenderScaleTransitionSnapshot();
			if (controller.targetEpoch != a_transitionEpoch)
				return;
			const auto* replacement = [&]() -> const Upscaling::VRRenderScaleProfileSnapshot* {
				for (const auto* profile : {
						 std::addressof(controller.requested),
						 std::addressof(controller.applying) }) {
					if (profile->valid && profile->requestID != 0 &&
						profile->transitionEpoch == a_transitionEpoch) {
						return profile;
					}
				}
				return nullptr;
			}();
			if (!replacement)
				return;

			const auto session =
				upscaling.GetVRRenderScaleStressSessionSnapshot();
			const uint64_t tick = QueryQualificationTick();
			const uint32_t frame = globals::state ? globals::state->frameCount : 0;
			const uint64_t deviceIdentity =
				reinterpret_cast<uintptr_t>(globals::d3d::device);
			if (!session.active || tick == 0 || frame == 0 || deviceIdentity == 0)
				return;

			auto& store = GetQualificationStore();
			std::lock_guard lock(store.mutex);
			if (!store.active ||
				!store.active->firstPhysicalMutationEvidence.is_null()) {
				return;
			}
			const json dispatchProof =
				store.active->dispatchPresentationEvidence.is_object() &&
						store.active->dispatchPresentationEvidence.contains("presentationProof") ?
					store.active->dispatchPresentationEvidence["presentationProof"] :
					json(nullptr);
			if (!dispatchProof.is_object() ||
				!dispatchProof.value("proven", false) ||
				(a_source == PhysicalMutationBoundarySource::ProviderInvalidation &&
					(dispatchProof.value("kind", std::string{}) !=
							"exact_vendor_evaluation" ||
						OptionalNonNegativeIntegerOrZero(
							dispatchProof, "methodValue") != a_providerMethod)) ||
				!ReplacementTelemetry::OwnsMutationBoundary({
					.ownerActive = true,
					.auditActive = store.active->presentationAudit.active,
					.stressSessionMatches =
						store.active->baseline.stressSessionID == session.sessionID,
					.qualificationTransitionID = store.active->transitionID,
					.ownershipToken = store.active->ownershipToken,
					.dispatchTick = store.active->dispatchTick,
					.boundaryTick = tick,
					.dispatchTransitionEpoch =
						OptionalNonNegativeIntegerOrZero(
							dispatchProof, "transitionEpoch"),
					.controllerTargetEpoch = controller.targetEpoch,
					.boundaryTransitionEpoch = a_transitionEpoch,
					.replacementRequestID = replacement->requestID,
					.replacementTransitionEpoch = replacement->transitionEpoch,
					.replacementContractGeneration =
						replacement->contractGeneration,
					.dispatchDeviceIdentity = static_cast<uintptr_t>(
						OptionalNonNegativeIntegerOrZero(
							dispatchProof, "deviceIdentity")),
					.currentDeviceIdentity = static_cast<uintptr_t>(deviceIdentity),
				})) {
				return;
			}

			const std::string_view source =
				a_source == PhysicalMutationBoundarySource::ProviderInvalidation ?
					"provider_invalidation" :
					"engine_target_creator";
			const std::string_view reason =
				a_source == PhysicalMutationBoundarySource::ProviderInvalidation ?
					"provider_resource_invalidation" :
					"engine_target_creator";
			store.active->firstPhysicalMutationEvidence = {
				{ "tick", tick },
				{ "frame", frame },
				{ "stressSessionId", session.sessionID },
				{ "qualificationTransitionId", store.active->transitionID },
				{ "ownershipToken", store.active->ownershipToken },
				{ "ownerTransitionId", store.active->transitionID },
				{ "ownerToken", store.active->ownershipToken },
				{ "replacementRequestId", replacement->requestID },
				{ "replacementTransitionEpoch", replacement->transitionEpoch },
				{ "replacementContractGeneration", replacement->contractGeneration },
				{ "replacementDeviceIdentity", deviceIdentity },
				{ "physicalMutationStarted", true },
				{ "physicalMutationSource", source },
				{ "mutationExpectation", "required" },
				{ "mutationExpectationReason", reason },
			};
			MergeQualificationMutationExpectation(
				*store.active,
				ReplacementTelemetry::MutationExpectation::Required,
				reason);
		} catch (...) {
			// Qualification evidence must not interfere with mutation ownership.
		}
	}

	void RecordPresentationAuditObservation(
		const PresentationAuditObservation& a_observation) noexcept
	{
		if (!a_observation.valid || a_observation.eyeIndex >= 2 ||
			a_observation.compositorCycleToken == 0) {
			return;
		}
		try {
			auto& upscaling = globals::features::upscaling;
			const auto controller = upscaling.GetVRRenderScaleTransitionSnapshot();
			const auto gate = upscaling.GetVRVendorWorkGateSnapshot();
			const uint64_t targetEpoch = controller.targetEpoch;
			const uint64_t observationTick = QueryQualificationTick();
			State::RenderTargetResourcePublicationDiagnostics publication{};
			if (globals::state) {
				publication = globals::state->GetCurrentMainRenderTargetResourcePublicationDiagnostics();
			}
			const auto* replacement = [&]() -> const Upscaling::VRRenderScaleProfileSnapshot* {
				for (const auto* profile : {
						 std::addressof(controller.applied),
						 std::addressof(controller.stable),
						 std::addressof(controller.applying),
						 std::addressof(controller.requested) }) {
					if (profile->valid && targetEpoch != 0 &&
						profile->transitionEpoch == targetEpoch) {
						return profile;
					}
				}
				return nullptr;
			}();
			const auto* publishedReplacement =
				controller.applied.valid && targetEpoch != 0 &&
						controller.applied.transitionEpoch == targetEpoch ?
					std::addressof(controller.applied) :
				controller.stable.valid && targetEpoch != 0 &&
						controller.stable.transitionEpoch == targetEpoch ?
					std::addressof(controller.stable) :
					nullptr;
			auto& store = GetQualificationStore();
			std::lock_guard lock(store.mutex);
			if (!store.active || store.active->dispatchTick == 0 ||
				!store.active->presentationAudit.active) {
				return;
			}
			const auto& boundaryEvidence =
				store.active->firstPhysicalMutationEvidence;
			const uint64_t boundaryStressSessionID =
				OptionalNonNegativeIntegerOrZero(
					boundaryEvidence, "stressSessionId");
			const uint64_t boundaryTransitionID =
				OptionalNonNegativeIntegerOrZero(
					boundaryEvidence, "qualificationTransitionId");
			const uint64_t boundaryOwnershipToken =
				OptionalNonNegativeIntegerOrZero(
					boundaryEvidence, "ownershipToken");
			const uint32_t boundaryFrame = static_cast<uint32_t>(
				OptionalNonNegativeIntegerOrZero(boundaryEvidence, "frame"));
			const uint64_t boundaryTick =
				OptionalNonNegativeIntegerOrZero(boundaryEvidence, "tick");
			const uint64_t boundaryRequestID =
				OptionalNonNegativeIntegerOrZero(
					boundaryEvidence, "replacementRequestId");
			const uint64_t boundaryTransitionEpoch =
				OptionalNonNegativeIntegerOrZero(
					boundaryEvidence, "replacementTransitionEpoch");
			const uint32_t boundaryContractGeneration =
				static_cast<uint32_t>(OptionalNonNegativeIntegerOrZero(
					boundaryEvidence, "replacementContractGeneration"));
			const uintptr_t boundaryDeviceIdentity =
				static_cast<uintptr_t>(OptionalNonNegativeIntegerOrZero(
					boundaryEvidence, "replacementDeviceIdentity"));
			const bool boundaryRecorded = boundaryEvidence.is_object() &&
			                              boundaryStressSessionID != 0 &&
			                              boundaryStressSessionID ==
			                                  store.active->baseline.stressSessionID &&
			                              boundaryTransitionID ==
			                                  store.active->transitionID &&
			                              boundaryOwnershipToken ==
			                                  store.active->ownershipToken &&
			                              boundaryRequestID != 0 &&
			                              boundaryTransitionEpoch != 0 &&
			                              boundaryDeviceIdentity != 0 &&
			                              boundaryFrame != 0 && boundaryTick != 0;
			const bool physicalMutationStarted =
				ReplacementTelemetry::IsAtOrAfterMutationBoundary(
					a_observation.frame,
					observationTick,
					{
						.valid = boundaryRecorded,
						.frame = boundaryFrame,
						.qpcTick = boundaryTick,
					});
			const auto& dispatchEvidence =
				store.active->dispatchPresentationEvidence;
			const json dispatchProof =
				dispatchEvidence.is_object() &&
						dispatchEvidence.contains("presentationProof") ?
					dispatchEvidence["presentationProof"] :
					json(nullptr);
			const bool dispatchProven = dispatchProof.is_object() &&
			                            dispatchProof.value("proven", false);
			const uint64_t dispatchRequestID =
				OptionalNonNegativeIntegerOrZero(dispatchProof, "requestId");
			const uint64_t dispatchTransitionEpoch =
				OptionalNonNegativeIntegerOrZero(dispatchProof, "transitionEpoch");
			const uint64_t dispatchContractGeneration =
				OptionalNonNegativeIntegerOrZero(dispatchProof, "contractGeneration");
			const uint64_t dispatchPublicationGeneration =
				OptionalNonNegativeIntegerOrZero(
					dispatchProof, "resourcePublicationGeneration");
			const uint64_t dispatchResourceRevision =
				OptionalNonNegativeIntegerOrZero(dispatchProof, "resourceRevision");
			const uint64_t dispatchDeviceIdentity =
				OptionalNonNegativeIntegerOrZero(dispatchProof, "deviceIdentity");
			const auto dispatchMethod = static_cast<uint32_t>(
				OptionalNonNegativeIntegerOrZero(dispatchProof, "methodValue"));
			const auto dispatchQualityMode = static_cast<uint32_t>(
				OptionalNonNegativeIntegerOrZero(dispatchProof, "qualityMode"));
			const bool dispatchRenderScaleMode =
				dispatchProof.contains("renderScaleMode") &&
				dispatchProof["renderScaleMode"].is_boolean() &&
				dispatchProof["renderScaleMode"].get<bool>();
			const auto dispatchBackend = static_cast<uint32_t>(
				OptionalNonNegativeIntegerOrZero(dispatchProof, "backendValue"));
			const auto observationMethod = static_cast<Upscaling::UpscaleMethod>(
				a_observation.method);
			const auto observationBackend =
				static_cast<Upscaling::VRRenderScaleBackendKind>(
					a_observation.backend);
			const bool observationVendorDispatchProven =
				HasCurrentVendorDispatch(
					observationMethod,
					observationBackend,
					a_observation.frame,
					a_observation.vendorDispatchFrame,
					a_observation.vendorDispatchSerial,
					a_observation.vendorRuntimeFallback);
			const bool sharedVendorDispatchRequired =
				observationMethod == Upscaling::UpscaleMethod::kFSR &&
				a_observation.path == static_cast<uint32_t>(
										  Upscaling::VRRenderScalePresentationPath::NativeOriginal);
			const auto exactPathMatchesTarget =
				[&](Upscaling::UpscaleMethod a_method, bool a_renderScaleMode) {
					if (a_method == Upscaling::UpscaleMethod::kDLSS ||
						a_method == Upscaling::UpscaleMethod::kFSR) {
						const auto expectedPath = a_renderScaleMode ?
					                                  Upscaling::VRRenderScalePresentationPath::VendorEvaluated :
					                                  Upscaling::VRRenderScalePresentationPath::NativeOriginal;
						return a_observation.path == static_cast<uint32_t>(expectedPath) &&
					           observationVendorDispatchProven;
					}
					return a_observation.path == static_cast<uint32_t>(
													 Upscaling::VRRenderScalePresentationPath::NativeOriginal) &&
				           observationBackend ==
				               Upscaling::VRRenderScaleBackendKind::None;
				};
			const bool selectionSuppressesCurrent =
				a_observation.selection ==
					PresentationAuditSelection::BlackKeepalive ||
				a_observation.selection ==
					PresentationAuditSelection::Quarantine;
			const bool suppressesPreviousBeforeMutation =
				selectionSuppressesCurrent && !physicalMutationStarted;
			const bool observedDispatchDimensions =
				a_observation.renderWidth != 0 &&
				a_observation.renderHeight != 0 &&
				a_observation.displayWidth != 0 &&
				a_observation.displayHeight != 0 &&
				a_observation.renderWidth ==
					OptionalNonNegativeIntegerOrZero(
						dispatchProof, "renderWidth") &&
				a_observation.renderHeight ==
					OptionalNonNegativeIntegerOrZero(
						dispatchProof, "renderHeight") &&
				a_observation.displayWidth ==
					OptionalNonNegativeIntegerOrZero(
						dispatchProof, "displayWidth") &&
				a_observation.displayHeight ==
					OptionalNonNegativeIntegerOrZero(
						dispatchProof, "displayHeight");
			const bool exactCurrent = dispatchProven &&
			                          (suppressesPreviousBeforeMutation ||
										  (a_observation.contractGeneration ==
												  dispatchContractGeneration &&
											  a_observation.transitionEpoch ==
												  dispatchTransitionEpoch &&
											  static_cast<uint64_t>(a_observation.deviceIdentity) ==
												  dispatchDeviceIdentity &&
											  a_observation.resourceRevision ==
												  dispatchResourceRevision &&
											  a_observation.backend ==
												  dispatchBackend &&
											  observedDispatchDimensions &&
											  publication.current &&
											  publication.publishedGeneration ==
												  dispatchPublicationGeneration &&
											  a_observation.method ==
												  dispatchMethod &&
											  exactPathMatchesTarget(
												  static_cast<Upscaling::UpscaleMethod>(dispatchMethod),
												  dispatchRenderScaleMode)));
			const bool differsFromDispatch = !dispatchProven ||
			                                 a_observation.transitionEpoch !=
			                                     dispatchTransitionEpoch ||
			                                 a_observation.contractGeneration !=
			                                     dispatchContractGeneration ||
			                                 a_observation.method != dispatchMethod ||
			                                 static_cast<uint64_t>(a_observation.deviceIdentity) !=
			                                     dispatchDeviceIdentity ||
			                                 a_observation.resourceRevision !=
			                                     dispatchResourceRevision;
			const auto providerGenerationForProfile =
				[&](const auto& a_profile) {
					if (a_profile.method == Upscaling::UpscaleMethod::kDLSS)
						return controller.dlssLifecycle.runtimeGeneration;
					if (a_profile.method == Upscaling::UpscaleMethod::kFSR)
						return controller.fsrLifecycle.runtimeGeneration;
					return uint32_t{ 0 };
				};
			const auto providerBackendForProfile =
				[&](const auto& a_profile) {
					if (a_profile.method == Upscaling::UpscaleMethod::kDLSS)
						return controller.dlssLifecycle.backend;
					if (a_profile.method == Upscaling::UpscaleMethod::kFSR)
						return controller.fsrLifecycle.backend;
					return Upscaling::VRRenderScaleBackendKind::None;
				};
			const auto exactResourceContractMatches =
				[&](const auto& a_profile) {
					const auto& resources = a_profile.resources;
					const bool vendorProfile =
						a_profile.method == Upscaling::UpscaleMethod::kDLSS ||
						a_profile.method == Upscaling::UpscaleMethod::kFSR;
					const auto expectedBackend =
						vendorProfile && !a_profile.active ?
							providerBackendForProfile(a_profile) :
							resources.backend;
					return resources.valid &&
				           resources.active == a_profile.active &&
				           resources.method == a_profile.method &&
				           resources.qualityMode == a_profile.qualityMode &&
				           (a_profile.method != Upscaling::UpscaleMethod::kDLSS ||
							   resources.dlssPreset == a_profile.dlssPreset) &&
				           resources.renderEyeWidth == a_profile.renderEyeWidth &&
				           resources.renderEyeHeight == a_profile.renderEyeHeight &&
				           resources.displayEyeWidth == a_profile.displayEyeWidth &&
				           resources.displayEyeHeight == a_profile.displayEyeHeight &&
				           resources.contextCount == 2u &&
				           a_observation.backend ==
				               static_cast<uint32_t>(expectedBackend) &&
				           (vendorProfile ?
								   (a_profile.active ?
										   resources.backend != Upscaling::VRRenderScaleBackendKind::None :
										   resources.backend == Upscaling::VRRenderScaleBackendKind::None &&
											   expectedBackend != Upscaling::VRRenderScaleBackendKind::None) :
								   resources.backend == Upscaling::VRRenderScaleBackendKind::None);
				};
			const auto exactProfileMatches = [&](const auto& a_profile) {
				return a_profile.valid &&
				       a_profile.contractGeneration != 0 &&
				       a_observation.transitionEpoch == a_profile.transitionEpoch &&
				       a_observation.contractGeneration == a_profile.contractGeneration &&
				       a_observation.method == static_cast<uint32_t>(a_profile.method) &&
				       a_observation.renderWidth == a_profile.renderEyeWidth &&
				       a_observation.renderHeight == a_profile.renderEyeHeight &&
				       a_observation.displayWidth == a_profile.displayEyeWidth &&
				       a_observation.displayHeight == a_profile.displayEyeHeight &&
				       a_observation.deviceIdentity ==
				           reinterpret_cast<uintptr_t>(globals::d3d::device) &&
				       a_observation.resourceRevision != 0;
			};
			const auto boundaryMatchesProfile = [&](const auto& a_profile) {
				return boundaryRecorded && a_profile.valid &&
				       a_profile.requestID == boundaryRequestID &&
				       a_profile.transitionEpoch == boundaryTransitionEpoch &&
				       ReplacementTelemetry::MatchesMutationBoundaryGeneration(
						   boundaryContractGeneration,
						   a_profile.contractGeneration) &&
				       a_observation.deviceIdentity == boundaryDeviceIdentity;
			};
			const auto exactPathMatchesProfile = [&](const auto& a_profile) {
				return exactPathMatchesTarget(
					a_profile.method,
					a_profile.renderScaleModeEnabled);
			};
			const bool exactReplacement =
				publishedReplacement &&
				ReplacementTelemetry::IsPublishedReplacementProven({
					.physicalMutationStarted = physicalMutationStarted,
					.differsFromDispatch = differsFromDispatch,
					.observed =
						a_observation.selection ==
						PresentationAuditSelection::Observed,
					.profileMatches = exactProfileMatches(*publishedReplacement),
					.mutationBoundaryMatches =
						boundaryMatchesProfile(*publishedReplacement),
					.presentationPathMatches =
						exactPathMatchesProfile(*publishedReplacement),
					.resourceContractMatches =
						exactResourceContractMatches(*publishedReplacement),
					.providerGenerationMatches =
						(!publishedReplacement->active ||
							(publishedReplacement->method != Upscaling::UpscaleMethod::kDLSS &&
								publishedReplacement->method != Upscaling::UpscaleMethod::kFSR) ||
							providerGenerationForProfile(*publishedReplacement) ==
								publishedReplacement->contractGeneration),
					.publicationCurrent = publication.current,
				});
			const uint32_t identityMethod = suppressesPreviousBeforeMutation ?
			                                    dispatchMethod :
			                                    a_observation.method;
			const uint32_t identityBackend = suppressesPreviousBeforeMutation ?
			                                     dispatchBackend :
			                                     a_observation.backend;
			const auto* identityProfile =
				exactReplacement ? publishedReplacement : nullptr;
			const uint32_t identityQualityMode = exactCurrent ?
			                                         dispatchQualityMode :
			                                     identityProfile ? identityProfile->qualityMode :
			                                                       0;
			const bool identityRenderScaleMode = exactCurrent ?
			                                         dispatchRenderScaleMode :
			                                     identityProfile ?
			                                         identityProfile->renderScaleModeEnabled :
			                                         false;
			const uint32_t providerGeneration =
				identityMethod ==
						static_cast<uint32_t>(Upscaling::UpscaleMethod::kDLSS) ?
					controller.dlssLifecycle.runtimeGeneration :
				identityMethod ==
						static_cast<uint32_t>(Upscaling::UpscaleMethod::kFSR) ?
					controller.fsrLifecycle.runtimeGeneration :
					0;
			const bool blockedPreMutation = !physicalMutationStarted &&
			                                (controller.relatchPlan.projectedResidencyDeferred ||
												controller.relatchPlan.systemCommitDeferred ||
												controller.relatchPlan.pressureDeferred ||
												gate.lifecycleMutationDeferred ||
												(globals::shaderCache && globals::shaderCache->IsCompiling()));
			ReplacementTelemetry::PresentationDisposition disposition =
				ToAuditDisposition(static_cast<
					Upscaling::VRRenderScalePresentationPath>(a_observation.path));
			const bool exactVendorObservationPath =
				a_observation.path == static_cast<uint32_t>(
										  Upscaling::VRRenderScalePresentationPath::VendorEvaluated) ||
				a_observation.path == static_cast<uint32_t>(
										  Upscaling::VRRenderScalePresentationPath::NativeOriginal);
			if (observationVendorDispatchProven && exactVendorObservationPath)
				disposition =
					ReplacementTelemetry::PresentationDisposition::ExactVendor;
			if (a_observation.selection ==
				PresentationAuditSelection::BlackKeepalive) {
				disposition =
					ReplacementTelemetry::PresentationDisposition::BlackKeepalive;
			} else if (a_observation.selection ==
					   PresentationAuditSelection::Quarantine) {
				disposition =
					ReplacementTelemetry::PresentationDisposition::Quarantine;
			}
			const uint64_t identityRequestID = exactCurrent ?
			                                       dispatchRequestID :
			                                   identityProfile ?
			                                       identityProfile->requestID :
			                                   replacement ? replacement->requestID :
			                                                 controller.stable.requestID;
			const uint64_t identityTransitionEpoch = suppressesPreviousBeforeMutation ?
			                                             dispatchTransitionEpoch :
			                                             a_observation.transitionEpoch;
			const uint32_t identityContractGeneration = suppressesPreviousBeforeMutation ?
			                                                static_cast<uint32_t>(dispatchContractGeneration) :
			                                                a_observation.contractGeneration;
			const uint64_t identityPublicationGeneration = exactCurrent ?
			                                                   dispatchPublicationGeneration :
			                                                   publication.publishedGeneration;
			const uint64_t identityResourceRevision = suppressesPreviousBeforeMutation ?
			                                              dispatchResourceRevision :
			                                              a_observation.resourceRevision;
			const auto identityDevice = suppressesPreviousBeforeMutation ?
			                                static_cast<std::uintptr_t>(dispatchDeviceIdentity) :
			                                a_observation.deviceIdentity;
			const json dispatchEye = suppressesPreviousBeforeMutation &&
			                                 dispatchProof.contains(a_observation.eyeIndex == 0 ? "leftEye" : "rightEye") ?
			                             dispatchProof[a_observation.eyeIndex == 0 ? "leftEye" : "rightEye"] :
			                             json(nullptr);
			const bool identityLoadingOrMenuContext =
				suppressesPreviousBeforeMutation && dispatchEye.is_object() ?
					dispatchEye.value("loadingOrMenuContext", false) :
					a_observation.loadingOrMenuContext;
			const bool identityTransitionCooldown =
				suppressesPreviousBeforeMutation && dispatchEye.is_object() ?
					dispatchEye.value("transitionCooldown", false) :
					a_observation.transitionCooldown;
			ReplacementTelemetry::EyeObservation eye{
				.valid = true,
				.eyeIndex = a_observation.eyeIndex,
				.frame = a_observation.frame,
				.qpcTick = observationTick,
				.compositorCycleToken = a_observation.compositorCycleToken,
				.requestID = identityRequestID,
				.transitionEpoch = identityTransitionEpoch,
				.contractGeneration = identityContractGeneration,
				.providerGeneration = providerGeneration,
				.publicationGeneration = identityPublicationGeneration,
				.resourceRevision = identityResourceRevision,
				.deviceIdentity = identityDevice,
				.renderWidth = a_observation.renderWidth,
				.renderHeight = a_observation.renderHeight,
				.displayWidth = a_observation.displayWidth,
				.displayHeight = a_observation.displayHeight,
				.method = identityMethod,
				.qualityMode = identityQualityMode,
				.renderScaleMode = identityRenderScaleMode,
				.backend = identityBackend,
				.vendorDispatchFrame = a_observation.vendorDispatchFrame,
				.vendorDispatchSerial = a_observation.vendorDispatchSerial,
				.vendorRuntimeFallback = a_observation.vendorRuntimeFallback,
				.vendorDispatchProven = observationVendorDispatchProven,
				.sharedVendorDispatchRequired = sharedVendorDispatchRequired,
				.disposition = disposition,
				.loadingOrMenuContext = identityLoadingOrMenuContext,
				.transitionCooldown = identityTransitionCooldown,
				.submitted = a_observation.submitted,
				.exactCurrent = exactCurrent,
				.exactReplacement = exactReplacement,
				.blockedPreMutation = blockedPreMutation,
				.physicalMutationStarted = physicalMutationStarted,
			};
			ReplacementTelemetry::CompleteCycle completed{};
			const bool cycleCompleted = ReplacementTelemetry::ObserveEye(
				store.active->presentationAudit,
				store.active->transitionID,
				store.active->ownershipToken,
				eye,
				completed);
			if (cycleCompleted && completed.afterMutation &&
				completed.coherent && completed.submitted &&
				completed.exactReplacement &&
				store.active->firstNewGenerationProvenEvidence.is_null() &&
				boundaryRequestID == completed.requestID &&
				boundaryTransitionEpoch == completed.transitionEpoch &&
				ReplacementTelemetry::MatchesMutationBoundaryGeneration(
					boundaryContractGeneration,
					completed.contractGeneration) &&
				boundaryDeviceIdentity == completed.deviceIdentity) {
				const auto eyeEvidence = [&](uint32_t a_eyeIndex) {
					const bool leftEye = a_eyeIndex == 0;
					return json{
						{ "frame", leftEye ? completed.leftFrame : completed.rightFrame },
						{ "qpcTick", leftEye ? completed.leftQpcTick : completed.rightQpcTick },
						{ "compositorCycleToken", completed.compositorCycleToken },
						{ "transitionEpoch", completed.transitionEpoch },
						{ "generation", completed.contractGeneration },
						{ "method", GetUpscaleMethodName(
										static_cast<Upscaling::UpscaleMethod>(completed.method)) },
						{ "backend", GetBackendName(
										 static_cast<Upscaling::VRRenderScaleBackendKind>(completed.backend)) },
						{ "vendorDispatchFrame", leftEye ?
													 completed.leftVendorDispatchFrame :
													 completed.rightVendorDispatchFrame },
						{ "vendorDispatchSerial", leftEye ?
													  completed.leftVendorDispatchSerial :
													  completed.rightVendorDispatchSerial },
						{ "vendorRuntimeFallback", completed.vendorRuntimeFallback },
						{ "deviceIdentity", static_cast<uint64_t>(completed.deviceIdentity) },
						{ "resourceRevision", completed.resourceRevision },
						{ "renderWidth", completed.renderWidth },
						{ "renderHeight", completed.renderHeight },
						{ "displayWidth", completed.displayWidth },
						{ "displayHeight", completed.displayHeight },
					};
				};
				const json presentationProof{
					{ "proven", true },
					{ "kind", completed.disposition ==
									  ReplacementTelemetry::PresentationDisposition::ExactVendor ?
								  "exact_vendor_evaluation" :
								  "exact_native_presentation" },
					{ "frame", completed.frame },
					{ "qpcTick", completed.qpcTick },
					{ "requestId", completed.requestID },
					{ "transitionEpoch", completed.transitionEpoch },
					{ "contractGeneration", completed.contractGeneration },
					{ "providerRuntimeGeneration", completed.providerGeneration },
					{ "resourcePublicationGeneration", completed.publicationGeneration },
					{ "resourceRevision", completed.resourceRevision },
					{ "deviceIdentity", static_cast<uint64_t>(completed.deviceIdentity) },
					{ "renderWidth", completed.renderWidth },
					{ "renderHeight", completed.renderHeight },
					{ "displayWidth", completed.displayWidth },
					{ "displayHeight", completed.displayHeight },
					{ "method", GetUpscaleMethodName(
									static_cast<Upscaling::UpscaleMethod>(completed.method)) },
					{ "methodValue", completed.method },
					{ "qualityMode", completed.qualityMode },
					{ "renderScaleMode", completed.renderScaleMode },
					{ "backend", GetBackendName(
									 static_cast<Upscaling::VRRenderScaleBackendKind>(completed.backend)) },
					{ "backendValue", completed.backend },
					{ "vendorDispatchProven", completed.vendorDispatchProven },
					{ "sharedVendorDispatchRequired",
						completed.sharedVendorDispatchRequired },
					{ "compositorCycleToken", completed.compositorCycleToken },
					{ "leftEye", eyeEvidence(0) },
					{ "rightEye", eyeEvidence(1) },
				};
				store.active->firstNewGenerationProvenEvidence = {
					{ "tick", completed.qpcTick },
					{ "frame", completed.frame },
					{ "stressSessionId", boundaryStressSessionID },
					{ "qualificationTransitionId", store.active->transitionID },
					{ "ownershipToken", store.active->ownershipToken },
					{ "physicalMutationStarted", true },
					{ "physicalMutationSource", boundaryEvidence.value(
													"physicalMutationSource", std::string{}) },
					{ "selectedPresentationDisposition",
						ReplacementTelemetry::GetDispositionName(completed.disposition) },
					{ "presentationProof", presentationProof },
				};
			}
		} catch (...) {
			// DevBench evidence must never affect the production submit path.
		}
	}

	void Install()
	{
		g_registered.store(false, std::memory_order_release);
		auto* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench) {
			logger::info("VRRenderScaleDevBenchBridge: devbench host not present; iteration tool not registered");
			return;
		}

		static constexpr const char* diagnosticDescriptor =
			R"json({"description":"Control and inspect CSX VR render-scale, including bounded exact-owner preparation stage telemetry and a single-owner, QPC-timed server-side qualification barrier that returns the first coherent exact-cell/profile observation without menu queries or client polling. qualification_begin requires an active stress session plus caller-supplied transitionId and ownerId; qualification_dispatch freezes the latency origin immediately before the command and can atomically reset/start CPU plus GPU performance telemetry on that dispatch frame; qualification_wait accepts the same ownership pair, an exact editor ID and/or form ID, an optional target profile, an optional exact foveation fixture, and a timeout that defaults to 120000ms and cannot exceed it. None and TAA targets validate the authoritative effective profile and native presentation without requiring inactive controller projections to mirror TAA. target.fsrRuntime matches the configured preference; coherent desired, authoritative, resource, lifecycle, and eye-dispatch evidence independently validates the physical FSR backend, including capability fallback. Omit target when an external controller owns profile selection; the waiter then requires a post-dispatch profile change and validates the mutually coherent observed profile without changing it. DLSS dispatch tracing remains opt-in and non-blocking. stop, dlss_trace_stop, and cpu_performance_stop accept expectedSessionId to fail closed if capture ownership changed; gpu_performance_stop accepts expectedStartFrame as its ownership guard; expectedStartFrame remains a legacy optional secondary guard for CPU telemetry. CPU performance status, start, and stop responses expose cpuPerformance.sessionId and state; stop retains the session ID and reset clears it to zero. Every response identifies the producing DLL; expectedBuildId fails closed on a stale build.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["status","qualification_status","qualification_begin","qualification_dispatch","qualification_wait","qualification_cancel","cpu_performance_status","cpu_performance_start","cpu_performance_stop","cpu_performance_reset","gpu_performance_status","gpu_performance_start","gpu_performance_stop","gpu_performance_reset","dlss_trace_status","dlss_trace_start","dlss_trace_read","dlss_trace_stop","dlss_trace_reset","record","start","apply","stop","reset","probe_start","probe_stop","probe_record","probe_reset","ham_status","ham_reset","trim","texture_lifetime_start","texture_lifetime_status","texture_lifetime_checkpoint","texture_lifetime_stop","texture_lifetime_reset"]},"method":{"type":"string","enum":["dlss","fsr"]},"enabled":{"type":"boolean"},"qualityMode":{"type":"integer","minimum":0,"maximum":6},"dlssPreset":{"type":"integer","minimum":0,"maximum":5},"transitionId":{"type":"integer","minimum":1,"description":"Caller-owned nonzero qualification transition ID. Begin, dispatch, wait, and cancel must present it."},"ownerId":{"type":"string","minLength":1,"maxLength":128,"description":"Caller-generated qualification owner identity. Begin, dispatch, wait, and cancel must present the same value."},"startPerformanceTelemetry":{"type":"boolean","default":false,"description":"qualification_dispatch only: require inactive CPU and GPU captures, reset/start both on the dispatch frame, and return their ownership receipts."},"expectedCell":{"type":"integer","minimum":1,"maximum":4294967295,"description":"Optional exact destination cell form ID. qualification_wait requires this or expectedCellEditorId; when both are supplied both must match."},"expectedCellEditorId":{"type":"string","minLength":1,"maxLength":128,"description":"Preferred stable exact destination cell editor ID for qualification_wait."},"timeoutMs":{"type":"integer","minimum":1,"maximum":120000,"default":120000,"description":"Maximum qualification deadline measured from qualification_dispatch on the server QPC clock; the waiter returns immediately when the requested milestone is satisfied."},"target":{"type":"object","additionalProperties":false,"properties":{"method":{"type":"string","enum":["none","taa","dlss","fsr"]},"qualityMode":{"type":"integer","minimum":0,"maximum":6},"renderScaleMode":{"type":"boolean"},"dlssProfile":{"type":"string","enum":["J","K","L","M","F","E"]},"fsrRuntime":{"type":"string","enum":["fsr3","fsr4"],"description":"Configured FSR runtime preference only. Physical backend fallback is validated independently."}},"required":["method","qualityMode","renderScaleMode"],"description":"Optional exact expected profile for a runner-owned selection. None and TAA require qualityMode 0 and renderScaleMode false. Omit it for an externally owned selection; the waiter observes and returns the exact coherent profile without mutating upscaling state."},"foveation":{"type":"object","additionalProperties":false,"properties":{"foveatedVendorDispatch":{"type":"boolean"},"foveatedCenterArea":{"type":"number","minimum":0,"maximum":1},"peripheryTAAEnable":{"type":"boolean"},"peripheryTAACenterArea":{"type":"number","minimum":0,"maximum":1},"peripheryTAAOuterScale":{"type":"number","minimum":0,"maximum":1}},"required":["foveatedVendorDispatch","foveatedCenterArea","peripheryTAAEnable","peripheryTAACenterArea","peripheryTAAOuterScale"],"description":"Optional exact settings fixture. Float comparisons use the tolerance returned in each receipt; active physical flags must agree with the requested enable states."},"afterSequence":{"type":"integer","minimum":0,"description":"For dlss_trace_read, return records after this sequence."},"limit":{"type":"integer","minimum":1,"maximum":256,"description":"Maximum ring records returned by dlss_trace_read; defaults to 32 and pinned failures are returned separately."},"expectedSessionId":{"type":"integer","minimum":1,"description":"Optional ownership guard for stop, dlss_trace_stop, and cpu_performance_stop. The corresponding active session must match before it is stopped."},"expectedStartFrame":{"type":"integer","minimum":0,"description":"Optional ownership guard for gpu_performance_stop and legacy secondary guard for cpu_performance_stop. When present, the active capture window start frame must match before it is stopped."},"expectedBuildId":{"type":"string","description":"Exact 64-character CSX Build ID required for this operation."}},"required":["action"]}})json";
		static const std::string runtimeDiagnosticDescriptor = [&] {
			auto descriptor = json::parse(diagnosticDescriptor);
			auto description = descriptor["description"].get<std::string>();
			constexpr std::string_view previousNativeDescription =
				"None and TAA targets validate the authoritative effective profile "
				"and native presentation without requiring inactive controller "
				"projections to mirror TAA.";
			constexpr std::string_view nativeDescription =
				"Native targets require coherent requested, effective, and stable "
				"public profiles plus exact target-correlated stereo presentation; "
				"fixed-resolution vendor targets prove same-frame vendor dispatch "
				"while their render-scale resource key remains inactive.";
			if (const auto position = description.find(previousNativeDescription);
				position != std::string::npos) {
				description.replace(
					position,
					previousNativeDescription.size(),
					nativeDescription);
			}
			descriptor["description"] = description;
			descriptor["inputSchema"]["properties"]["foveation"]["description"] =
				"Optional exact settings fixture. Float comparisons use the "
				"tolerance returned in each receipt; live execution flags must "
				"agree with the requested enable states.";
			descriptor["description"] =
				descriptor["description"].get<std::string>() +
				" qualification_wait accepts milestone strict, presentation, or "
				"cleanup; absent milestone preserves strict combined semantics. Its "
				"terminal receipt reports independent milestone timings, cleanup "
				"tail, and the replacement-presentation timeline.";
			descriptor["description"] =
				descriptor["description"].get<std::string>() +
				" Main-thread actions cancelled before admission return "
				"main_thread_timeout; an action already admitted returns "
				"main_thread_in_progress and may complete after the response.";
			descriptor["inputSchema"]["properties"]["milestone"] = {
				{ "type", "string" },
				{ "enum", json::array({ "strict", "presentation", "cleanup" }) },
				{ "default", "strict" },
				{ "description",
					"qualification_wait only: strict requires both coherent current "
					"presentation and drained cleanup; presentation or cleanup may "
					"return its named milestone without weakening strict evidence." },
			};
			descriptor["inputSchema"]["properties"]["cocCellEditorId"] = {
				{ "type", "string" },
				{ "minLength", 1 },
				{ "maxLength", 128 },
				{ "description",
					"qualification_dispatch only: an ASCII editor ID containing "
					"letters, digits, or underscores. The action issues exactly one "
					"COC on the same main-thread operation; its QPC timer is read "
					"immediately before that command." },
			};
			return descriptor.dump();
		}();
		devBench->RegisterTool(
			"communityshaders.renderscale",
			runtimeDiagnosticDescriptor.c_str(),
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
