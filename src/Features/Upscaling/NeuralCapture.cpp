#include "Features/Upscaling.h"
#include "Features/Upscaling/NeuralRendering/CharacterPreparationEvidence.h"
#include "Features/Upscaling/NeuralRendering/CharacterPreparationEvidenceJson.h"
#include "Features/Upscaling/NeuralRendering/ExecutionEvidenceJson.h"
#include "Features/Upscaling/NeuralRendering/Renderer.h"
#include "Globals.h"
#include "State.h"
#include "Utils/ContentHash.h"
#include <algorithm>
#include <cstring>
#include <memory>

void to_json(nlohmann::json&, const Upscaling::Settings&);

namespace
{
	using Json = nlohmann::json;
	Json ConfigurationJson(const Upscaling::Settings& settings, const NeuralRendering::Color::Configuration& color)
	{
		Json values;
		to_json(values, settings);
		// Capture fingerprints include transient controls even though saves omit them.
		values["neuralCharacterDebugView"] = settings.neuralCharacterDebugView;
		values["neuralCharacterMaskTestMode"] = settings.neuralCharacterMaskTestMode;
		return { { "upscaling", std::move(values) }, { "color", NeuralRendering::Color::ConfigurationEvidenceJson(color) } };
	}
	Json Unavailable(std::string_view reason)
	{
		return { { "schemaVersion", 1 }, { "available", false }, { "reason", reason } };
	}
}

nlohmann::json Upscaling::GetNeuralRequestedConfiguration() const
{
	return ConfigurationJson(settings, NeuralRendering::Color::Registry::Instance().Snapshot());
}

std::string Upscaling::GetNeuralRequestedConfigurationFingerprint() const
{
	return Util::ContentHash::HashString(GetNeuralRequestedConfiguration().dump()).ToHex();
}

void Upscaling::RecordNeuralCaptureCamera(uint32_t frame) noexcept
{
	if (!NeuralRendering::Color::Registry::Instance().CaptureEvidenceEnabled())
		return;
	std::scoped_lock lock(neuralCaptureMutex);
	neuralCaptureCamera.viewSourceFrame = frame;
	const auto eyeCount = globals::game::isVR ? 2u : 1u;
	for (uint32_t eye = 0; eye < eyeCount; ++eye) {
		const auto& view = globals::game::frameBufferCached.GetCameraView(eye);
		const auto& projection = globals::game::frameBufferCached.GetCameraProjUnjittered(eye);
		const auto& position = globals::game::frameBufferCached.GetCameraPosAdjust(eye);
		static_assert(sizeof(view) == sizeof(neuralCaptureCamera.view[eye]));
		static_assert(sizeof(projection) == sizeof(neuralCaptureCamera.projection[eye]));
		static_assert(sizeof(position) == sizeof(neuralCaptureCamera.positionAdjust[eye]));
		std::memcpy(neuralCaptureCamera.view[eye].data(), &view, sizeof(view));
		std::memcpy(neuralCaptureCamera.projection[eye].data(), &projection, sizeof(projection));
		std::memcpy(neuralCaptureCamera.positionAdjust[eye].data(), &position, sizeof(position));
	}
}

void Upscaling::BeginNeuralCaptureFrame(NeuralStereoRouteRole role, uint32_t frame, uint64_t cycle) noexcept
{
	if (!NeuralRendering::Color::Registry::Instance().CaptureEvidenceEnabled())
		return;
	try {
		const auto index = static_cast<size_t>(role);
		if (index >= neuralCaptureBeginnings.size())
			return;
		const auto color = NeuralRendering::Color::Registry::Instance().Snapshot();
		const auto captureEpoch = NeuralRendering::Color::Registry::Instance().CaptureEpoch();
		const auto exposureEpoch = NeuralRendering::Color::ExposureCapture::Instance().GetSourceFrameEvidence(frame).key.epoch;
		std::scoped_lock lock(neuralCaptureMutex);
		auto& begin = neuralCaptureBeginnings[index];
		if (begin.begun && begin.frame == frame && begin.cycle == cycle && begin.captureEpoch == captureEpoch)
			return;
		if (!neuralCaptureConfigurationEpoch || neuralCaptureConfigurationSettings != settings || neuralCaptureConfigurationColorRevision != color.revision) {
			neuralCaptureConfigurationSettings = settings;
			neuralCaptureConfigurationColorRevision = color.revision;
			neuralCaptureConfigurationEpoch = NeuralRendering::NextExecutionSubmissionId();
		}
		begin = {};
		begin.begun = true;
		begin.frame = frame;
		begin.cycle = cycle;
		begin.exposureEpoch = exposureEpoch;
		begin.sourceTransactionId = NeuralRendering::NextExecutionSubmissionId();
		begin.captureEpoch = captureEpoch;
		begin.configurationEpoch = neuralCaptureConfigurationEpoch;
		begin.evidenceFailuresAtBegin = neuralCaptureEvidenceFailures.load(std::memory_order_relaxed);
		begin.settings = settings;
		begin.color = color;
		begin.viewSourceFrame = neuralCaptureCamera.viewSourceFrame;
		begin.view = neuralCaptureCamera.view;
		begin.projection = neuralCaptureCamera.projection;
		begin.positionAdjust = neuralCaptureCamera.positionAdjust;
	} catch (...) {
		neuralCaptureEvidenceFailures.fetch_add(1, std::memory_order_relaxed);
	}
}

void Upscaling::SetNeuralExecutionContext(NeuralRendering::RendererApplyArgs& args,
	const UpscalingDLSS::ViewportCrop& dlssCrop, const std::array<uint32_t, 2>& colorOrigin,
	const std::array<uint32_t, 2>& guideOrigin) noexcept
try {
	if (!NeuralRendering::Color::Registry::Instance().CaptureEvidenceEnabled())
		return;
	const auto role = NeuralRendering::ClassifyFeatureSlotMask(1u << args.featureSlot);
	const auto index = role == NeuralRendering::FeatureSlotRoute::Main ? 0u : 1u;
	const auto dispatchJitter = GetJitterForDispatch();
	std::scoped_lock lock(neuralCaptureMutex);
	auto& begin = neuralCaptureBeginnings[index];
	if (!begin.begun || begin.frame != args.frameId ||
		begin.captureEpoch != NeuralRendering::Color::Registry::Instance().CaptureEpoch())
		return;
	NeuralRendering::ExecutionContext context;
	context.sourceTransactionId = begin.sourceTransactionId;
	context.captureEpoch = begin.captureEpoch;
	context.configurationEpoch = begin.configurationEpoch;
	context.renderingMode = NeuralRendering::ClampRenderingMode(begin.settings.neuralRenderingMode);
	context.fovOnly = begin.settings.neuralRenderingFovOnly;
	context.renderscaleFov = begin.settings.neuralRenderingRenderscaleFov;
	context.dlssViewportCrop = dlssCrop;
	context.jitterPixels = { dispatchJitter.x, dispatchJitter.y };
	context.sourceColorOrigin = colorOrigin;
	context.sourceGuideOrigin = guideOrigin;
	context.inputPreparation = std::make_shared<Util::PassTimingCapture>();
	context.sourceContext = *context.renderingMode == NeuralRendering::RenderingMode::ReducedResolution ?
	                            "render_resolution_before_dlss" :
	                        args.insertionPoint == NeuralRendering::InsertionPoint::FinalLdrPreUi ?
	                            "final_ldr_before_ui" :
	                            "dlss_output_center";
	begin.sourceContexts[args.featureSlot & 1u] = context;
	args.executionContext = std::move(context);
} catch (...) {
	args.executionContext = {};
	neuralCaptureEvidenceFailures.fetch_add(1, std::memory_order_relaxed);
}

Util::PassTimingHandle Upscaling::CaptureNeuralStage(NeuralStereoRouteRole role, uint32_t eye,
	uint32_t frame, uint32_t world, uint64_t generation, const char* name,
	std::optional<uint64_t> pixels, std::optional<uint64_t> bytes) noexcept
try {
	if (!NeuralRendering::Color::Registry::Instance().CaptureEvidenceEnabled())
		return {};
	std::scoped_lock lock(neuralCaptureMutex);
	auto& begin = neuralCaptureBeginnings[static_cast<size_t>(role)];
	if (!begin.begun || begin.frame != frame || begin.captureEpoch != NeuralRendering::Color::Registry::Instance().CaptureEpoch())
		return {};
	if (begin.stageCount == begin.stages.size()) {
		++begin.droppedStages;
		return {};
	}
	auto timing = std::make_shared<Util::PassTimingCapture>();
	begin.stages[begin.stageCount++] = { name, eye, frame, world, generation, pixels, bytes, timing };
	return timing;
} catch (...) {
	neuralCaptureEvidenceFailures.fetch_add(1, std::memory_order_relaxed);
	return {};
}

void Upscaling::RecordNeuralStageWork(const Util::PassTimingHandle& capture, uint64_t pixels, std::optional<uint64_t> bytes) noexcept
try {
	if (!capture)
		return;
	std::scoped_lock lock(neuralCaptureMutex);
	for (auto& begin : neuralCaptureBeginnings)
		for (uint32_t i = 0; i < begin.stageCount; ++i)
			if (begin.stages[i].timing == capture) {
				begin.stages[i].dirtyPixels = pixels;
				begin.stages[i].copiedLogicalBytes = bytes;
				return;
			}
} catch (...) {
	neuralCaptureEvidenceFailures.fetch_add(1, std::memory_order_relaxed);
}

void Upscaling::RecordNeuralCaptureRoute(const NeuralStereoRouteSnapshot& route) noexcept
{
	if (!NeuralRendering::Color::Registry::Instance().CaptureEvidenceEnabled())
		return;
	try {
		const auto index = static_cast<size_t>(route.role);
		if (index >= neuralCaptureBeginnings.size())
			return;
		const auto inputs = NeuralRendering::Renderer::Instance().GetCaptureInputs();
		std::array<std::shared_ptr<const NeuralRendering::CharacterPreparationEvidence>, 2> characters;
		for (uint32_t eye = 0; eye < 2; ++eye)
			characters[eye] = NeuralRendering::CharacterRendering::Instance().GetPreparationEvidence(
				route.frame, route.temporalAdmission.sourceWorldFrame, route.generation, static_cast<uint32_t>(index) * 2 + eye);
		std::array<Streamline::DLSSViewportCropTelemetrySnapshot, 6> dlss{};
		for (uint32_t viewport = 0; viewport < 3; ++viewport)
			for (uint32_t eye = 0; eye < 2; ++eye)
				dlss[viewport * 2 + eye] = streamline.GetDLSSViewportCropTelemetrySnapshot(
					static_cast<Streamline::DLSSViewportRole>(viewport), eye);
		std::scoped_lock lock(neuralCaptureMutex);
		auto record = neuralCaptureBeginnings[index];
		record.route = route;
		record.sourceEvidenceFailures = neuralCaptureEvidenceFailures.load(std::memory_order_relaxed) - record.evidenceFailuresAtBegin;
		for (uint32_t eye = 0; eye < 2; ++eye)
			if (characters[eye] && characters[eye]->key.captureEpoch == record.captureEpoch)
				record.characters[eye] = characters[eye];
		for (size_t i = 0; i < dlss.size(); ++i) {
			const auto& observation = dlss[i];
			if (observation.valid && observation.frame == route.frame &&
				observation.captureEpoch == record.captureEpoch && observation.compositorCycle == record.cycle &&
				static_cast<uint32_t>(observation.route) == index)
				record.dlssObservations[i] = observation;
		}
		record.settingsChanged = record.settings != settings;
		record.inputsMatched = inputs[index].Matches(static_cast<uint32_t>(index), route.frame,
			route.temporalAdmission.sourceWorldFrame, route.generation, route.insertionPoint);
		record.inputsMatched = record.inputsMatched && inputs[index].sourceTransactionId == record.sourceTransactionId &&
		                       inputs[index].captureEpoch == record.captureEpoch;
		if (record.inputsMatched) {
			record.inputs = inputs[index];
			record.settingsChanged |= record.color.revision != record.inputs.configuration.revision;
			record.color = record.inputs.configuration;
		}
		neuralCaptureRecords[index] = std::make_shared<const NeuralCaptureRecord>(std::move(record));
	} catch (...) {
		neuralCaptureEvidenceFailures.fetch_add(1, std::memory_order_relaxed);
		std::scoped_lock lock(neuralCaptureMutex);
		const auto index = static_cast<size_t>(route.role);
		if (index < neuralCaptureRecords.size())
			neuralCaptureRecords[index].reset();
	}
}

nlohmann::json Upscaling::SerializeNeuralCaptureRecord(const NeuralCaptureRecord& record)
{
	using namespace NeuralRendering::Color;
	const auto& route = record.route;
	const auto& color = record.color;
	const auto world = route.temporalAdmission.sourceWorldFrame;
	const auto configuration = ConfigurationJson(record.settings, color);
	const auto fingerprint = Util::ContentHash::HashString(configuration.dump()).ToHex();
	const bool available = record.begun && record.frame == route.frame && route.valid && !record.settingsChanged &&
	                       color.experiments.captureFrameEvidence && record.sourceTransactionId != 0;
	const char* reason = available ? "" : !record.begun      ? "frame_not_observed" :
	                                  record.settingsChanged ? "settings_changed_during_frame" :
	                                                           "frame_evidence_unavailable";
	const bool cameraValid = record.viewSourceFrame != 0 && record.viewSourceFrame == world;
	Json result{
		{ "schemaVersion", 1 }, { "available", available }, { "reason", reason },
		{ "transactionId", std::format("{}:{}:{}:{}:{}:{}:{}:{}:{}", record.sourceTransactionId, static_cast<uint32_t>(route.role),
							   route.frame, world, route.generation, route.insertionPoint, record.cycle, record.captureEpoch, fingerprint) },
		{ "sourceTransactionId", record.sourceTransactionId }, { "captureEpoch", record.captureEpoch },
		{ "configurationEpoch", record.configurationEpoch }, { "publicationSequence", route.sequence },
		{ "configurationFingerprint", fingerprint }, { "configurationFingerprintAlgorithm", "xxh3-128-json" },
		{ "configuration", configuration }, { "route", GetNeuralStereoRouteRoleName(route.role) },
		{ "generation", route.generation }, { "insertionPoint", route.insertionPoint },
		{ "pairComplete", route.pairComplete }, { "fallbackReason", GetNeuralStereoFallbackReasonName(route.fallbackReason) },
		{ "cameraEvidence", { { "available", cameraValid }, { "reason", cameraValid ? "" : "source_world_matrix_unmatched" },
								{ "sourceWorldFrame", record.viewSourceFrame }, { "view", record.view }, { "projection", record.projection },
								{ "positionAdjust", record.positionAdjust },
								{ "provenance", "engine_cached_unjittered_world_matrices" } } }
	};
	Json executions = Json::array(), stages = Json::array(), dlss = Json::array();
	for (const auto& execution : record.inputs.executions) {
		if (!execution || execution->Descriptor().context.sourceTransactionId != record.sourceTransactionId ||
			execution->Descriptor().context.captureEpoch != record.captureEpoch)
			continue;
		auto value = NeuralRendering::Evidence::ExecutionJson(*execution);
		value["colourExposureConfiguration"] = ConfigurationEvidenceJson(color);
		value["configurationFingerprint"] = fingerprint;
		executions.push_back(std::move(value));
	}
	for (uint32_t i = 0; i < record.stageCount; ++i) {
		const auto& stage = record.stages[i];
		stages.push_back({ { "name", stage.name }, { "eye", stage.eye }, { "frame", stage.frame },
			{ "sourceWorldFrame", stage.sourceWorldFrame }, { "generation", stage.generation },
			{ "matchesProducer", stage.frame == route.frame && stage.sourceWorldFrame == world && stage.generation == route.generation },
			{ "dirtyDispatchPixels", NeuralRendering::Evidence::OptionalJson(stage.dirtyPixels) },
			{ "copiedLogicalBytes", NeuralRendering::Evidence::OptionalJson(stage.copiedLogicalBytes) },
			{ "timing", NeuralRendering::Evidence::PassTimingJson(stage.timing) } });
	}
	for (const auto& observation : record.dlssObservations) {
		if (!observation.valid)
			continue;
		dlss.push_back({ { "eye", observation.eyeIndex }, { "viewportRole", static_cast<uint32_t>(observation.viewportRole) },
			{ "frame", observation.frame }, { "compositorCycle", observation.compositorCycle }, { "captureEpoch", observation.captureEpoch },
			{ "generation", observation.generation }, { "viewport", observation.viewport },
			{ "evaluationSucceeded", observation.evaluationSucceeded }, { "effectiveReset", observation.effectiveReset },
			{ "cropResetReason", static_cast<uint32_t>(observation.resetReason) },
			{ "grid", NeuralRendering::Evidence::ViewportJson(observation.current) },
			{ "motionVectorScale", { observation.motionVectorScaleX, observation.motionVectorScaleY } } });
	}
	const auto mode = NeuralRendering::ClampRenderingMode(record.settings.neuralRenderingMode);
	const bool rendererEvidenceAvailable = record.inputsMatched && !executions.empty();
	const auto workOutcome = [&](uint32_t eye) {
		const uint32_t bit = 1u << eye;
		return !route.requested || color.experiments.transportBypass || (route.bypassedEyeMask & bit) ? "NoWork" :
		       (route.appliedEyeMask & bit)                                                           ? "successful" :
		       (route.attemptedEyeMask & bit)                                                         ? "failed" :
		                                                                                                "unavailable";
	};
	result["executionEvidence"] = {
		{ "schemaVersion", 1 }, { "sourceTransactionId", record.sourceTransactionId },
		{ "publicationSequence", route.sequence },
		{ "frame", route.frame }, { "sourceWorldFrame", world }, { "generation", route.generation },
		{ "route", GetNeuralStereoRouteRoleName(route.role) }, { "logicalEyeCount", globals::game::isVR ? 2u : 1u },
		{ "mode", NeuralRendering::GetRenderingModeName(mode) }, { "fovOnly", record.settings.neuralRenderingFovOnly },
		{ "renderscaleFov", record.settings.neuralRenderingRenderscaleFov },
		{ "characterSelectionEnabled", record.settings.neuralCharacterRenderingEnabled && record.settings.neuralCharacterVisualIsolationEnabled },
		{ "captureEpoch", record.captureEpoch }, { "configurationEpoch", record.configurationEpoch },
		{ "sourceContext", mode == NeuralRendering::RenderingMode::ReducedResolution                                     ? "render_resolution_before_dlss" :
						   route.insertionPoint == static_cast<uint32_t>(NeuralRendering::InsertionPoint::FinalLdrPreUi) ? "final_ldr_before_ui" :
																														   "dlss_output_center" },
		{ "rendererEvidenceAvailable", rendererEvidenceAvailable },
		{ "rendererEvidenceIncomplete", record.inputs.executionEvidenceFailures != 0 || record.sourceEvidenceFailures != 0 ||
											record.droppedStages != 0 || (route.attemptedEyeMask != 0 && !rendererEvidenceAvailable) },
		{ "retainedExecutionCount", record.inputs.executionCount }, { "executionEvidenceFailures", record.inputs.executionEvidenceFailures },
		{ "sourceEvidenceFailures", record.sourceEvidenceFailures },
		{ "rendererEvidenceReason", rendererEvidenceAvailable ? "" : !route.attemptedEyeMask ? "no_renderer_evaluation" :
																 !record.inputsMatched       ? "renderer_transaction_unmatched" :
																							   "renderer_attempt_without_retained_execution" },
		{ "sourceContexts", { NeuralRendering::Evidence::ContextJson(record.sourceContexts[0]), NeuralRendering::Evidence::ContextJson(record.sourceContexts[1]) } },
		{ "executions", std::move(executions) }, { "sourceStages", std::move(stages) }, { "droppedStageCount", record.droppedStages },
		{ "dlssDispatches", std::move(dlss) },
		{ "characters", { NeuralRendering::Evidence::CharacterPreparationJson(record.characters[0]), NeuralRendering::Evidence::CharacterPreparationJson(record.characters[1]) } },
		{ "workOutcome", { workOutcome(0), workOutcome(1) } },
		{ "producerBoundary", { { "pairComplete", route.pairComplete }, { "committedEyeMask", route.committedEyeMask },
								  { "bypassedEyeMask", route.bypassedEyeMask }, { "disposition", GetNeuralStereoPairDispositionName(route.disposition) },
								  { "outcome", !route.requested || route.bypassedEyeMask == NeuralRendering::RequiredEyeMask(globals::game::isVR) ? "NoWork" :
											   route.disposition == NeuralStereoPairDisposition::NeuralPair                                       ? "successful" :
																																					"fallback" } } },
		{ "timingPolicy", "inclusive_scopes_overlap_do_not_sum_cpu_gpu_or_distinct_queue_clocks" },
		{ "bytePolicy", "logical_texel_bytes_exclude_driver_alignment_residency_and_private_provider_allocations" }
	};
	const auto engineExposure = ExposureCapture::Instance().GetSourceFrameEvidence(world, record.exposureEpoch);
	result["engineExposure"] = engineExposure.evidence ? ExposureEvidenceJson(*engineExposure.evidence) :
	                                                     Json{ { "frame", world }, { "epoch", record.exposureEpoch }, { "sequence", 0 }, { "engineRatioValid", false } };
	result["engineExposure"]["reason"] = engineExposure.reason;
	result["engineExposure"]["sourceWorldFrame"] = world;
	result["engineExposure"]["role"] = "engine_observation_not_inference_binding";
	const auto colorValues = ConfigurationEvidenceJson(color);
	for (uint32_t eye = 0; eye < 2; ++eye) {
		Json observations = Json::array();
		Json exposure{ { "valid", false }, { "sourceWorldFrame", world }, { "frame", nullptr }, { "age", nullptr }, { "reason", "not_requested_or_unmeasured" } };
		for (uint32_t slot = eye; slot < record.inputs.slots.size(); slot += 2) {
			if (!(record.inputs.slotMask & (1u << slot)))
				continue;
			const auto& observation = record.inputs.slots[slot];
			const auto value = ObservationEvidenceJson(observation);
			observations.push_back(value);
			const auto& e = observation.exposure;
			const bool valid = observation.exposureState == ExposureBindingState::SnapshotQueued && e.readbackComplete &&
			                   !e.stamp.ambiguous && e.values[3] == 1.0f && e.stamp.frame <= world;
			exposure = value.at("exposure");
			exposure["valid"] = valid;
			exposure["sourceWorldFrame"] = world;
			exposure["age"] = e.stamp.frame && e.stamp.frame <= world ? Json(world - e.stamp.frame) : Json(nullptr);
			exposure["reason"] = valid ? "" : "exact_scalar_not_ready_or_invalid";
		}
		const uint32_t bit = 1u << eye;
		result[eye == 0 ? "left" : "right"] = {
			{ "frame", route.frame }, { "sourceWorldFrame", world }, { "colorRevision", color.revision },
			{ "inputEpoch", color.inputEpoch[std::min(route.insertionPoint, 1u)] },
			{ "effectiveMode", color.settings.enabled ? colorValues.at("settings").at("mode") : Json("legacy_raw") },
			{ "applyModelEdit", color.experiments.applyModelEdit }, { "nrEnabled", route.requested },
			{ "inferenceAttempted", (route.attemptedEyeMask & bit) != 0 },
			{ "inferenceSucceeded", (route.appliedEyeMask & bit) != 0 },
			{ "pipelineCommitted", (route.committedEyeMask & bit) != 0 },
			{ "outputCommitted", (route.committedEyeMask & route.appliedEyeMask & bit) != 0 && color.experiments.applyModelEdit && !color.experiments.transportBypass },
			{ "disposition", GetNeuralStereoPairDispositionName(route.disposition) },
			{ "exposure", std::move(exposure) }, { "physicalRegions", std::move(observations) }
		};
	}
	return result;
}

nlohmann::json Upscaling::GetNeuralCaptureStatus() const
{
	std::array<std::shared_ptr<const NeuralCaptureRecord>, 2> records;
	{
		std::scoped_lock lock(neuralCaptureMutex);
		records = neuralCaptureRecords;
	}
	Json routes = Json::array();
	for (const auto& record : records)
		if (record)
			routes.push_back(SerializeNeuralCaptureRecord(*record));
	return { { "schemaVersion", 1 }, { "enabled", NeuralRendering::Color::Registry::Instance().CaptureEvidenceEnabled() },
		{ "evidenceFailureCount", neuralCaptureEvidenceFailures.load(std::memory_order_relaxed) }, { "routes", std::move(routes) } };
}

std::function<nlohmann::json()> Upscaling::PinNeuralExecutionDiagnostics(uint64_t transaction, uint64_t publication) const
{
	std::scoped_lock lock(neuralCaptureMutex);
	std::shared_ptr<const NeuralCaptureRecord> record;
	for (const auto& candidate : neuralCaptureRecords)
		if (candidate && candidate->sourceTransactionId == transaction && candidate->route.sequence == publication)
			record = candidate;
	const auto& retained = submitStageNeuralStereoState.publishedCapture;
	if (!record && retained && retained->sourceTransactionId == transaction && retained->route.sequence == publication)
		record = retained;
	if (!record)
		return [] { return Unavailable("source_transaction_not_retained"); };
	const auto lease = neuralExecutionRetention.Pin({ transaction, publication }, std::move(record));
	if (!lease)
		return [] { return Unavailable("capture_retention_capacity_exhausted"); };
	return [lease] {
		auto result = SerializeNeuralCaptureRecord(*lease->Snapshot()).at("executionEvidence");
		result["finalized"] = true;
		return result;
	};
}

void Upscaling::PinNeuralCapturePresentation(VRRenderScalePresentationObservation& observation, ID3D11Texture2D* output) const noexcept
{
	observation.neuralCapture.reset();
	observation.neuralCaptureTexture = 0;
	if (!NeuralRendering::Color::Registry::Instance().CaptureEvidenceEnabled() || !output)
		return;
	std::scoped_lock lock(neuralCaptureMutex);
	if (observation.retainedNeuralPair) {
		observation.neuralCapture = submitStageNeuralStereoState.publishedCapture;
	} else {
		const auto& submit = neuralCaptureRecords[1];
		const bool mainOwned = neuralInsertionPointClaimFrame == observation.frame && neuralInsertionPointClaimRole == NeuralStereoRouteRole::Main;
		const auto& selected = neuralCaptureRecords[mainOwned ? 0 : 1];
		if (!selected || !submit || selected->frame != observation.frame || submit->cycle != observation.compositorCycleToken ||
			selected->route.temporalAdmission.sourceWorldFrame != submit->route.temporalAdmission.sourceWorldFrame)
			return;
		observation.neuralCapture = selected;
	}
	observation.neuralCaptureTexture = reinterpret_cast<std::uintptr_t>(output);
}

nlohmann::json Upscaling::CaptureNeuralSubmission(vr::EVREye eye, uint64_t cycle, ID3D11Texture2D* texture,
	std::string_view path, const VRRenderScalePresentationObservation* observation) const
{
	if (!NeuralRendering::Color::Registry::Instance().CaptureEvidenceEnabled())
		return Unavailable("capture_frame_evidence_disabled");
	const uint32_t eyeIndex = eye == vr::Eye_Right ? 1u : 0u;
	std::shared_ptr<const NeuralCaptureRecord> record;
	{
		std::scoped_lock lock(neuralCaptureMutex);
		if (observation && observation->valid) {
			if (observation->neuralCaptureTexture != reinterpret_cast<std::uintptr_t>(texture) ||
				observation->compositorCycleToken != cycle || observation->eyeIndex != eyeIndex)
				return Unavailable("presentation_texture_identity_unmatched");
			record = observation->neuralCapture;
		} else if (path == "original" && (!observation || !observation->valid)) {
			record = neuralCaptureRecords[0];
		} else {
			return Unavailable("presentation_path_unproven");
		}
	}
	if (!record || !globals::state || (!observation || !observation->retainedNeuralPair) && record->frame != globals::state->frameCount)
		return Unavailable("submission_frame_unmatched");
	if (record->route.role == NeuralStereoRouteRole::Submit && (!observation || !observation->retainedNeuralPair) && record->cycle != cycle)
		return Unavailable("submission_cycle_unmatched");
	auto result = SerializeNeuralCaptureRecord(*record);
	result["submittedEye"] = eyeIndex == 0 ? "left" : "right";
	result["submittedCycle"] = cycle;
	result["submittedTextureIdentity"] = std::format("0x{:x}", reinterpret_cast<uintptr_t>(texture));
	result["presentationPath"] = path;
	result["retainedPair"] = observation && observation->retainedNeuralPair;
	return result;
}
