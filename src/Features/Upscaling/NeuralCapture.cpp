#include "Features/Upscaling.h"
#include "Features/Upscaling/NeuralRendering/Renderer.h"
#include "Globals.h"
#include "State.h"
#include "Utils/ContentHash.h"
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
		const auto exposureEpoch = NeuralRendering::Color::ExposureCapture::Instance().GetSourceFrameEvidence(frame).key.epoch;
		std::scoped_lock lock(neuralCaptureMutex);
		auto& begin = neuralCaptureBeginnings[index];
		if (begin.begun && begin.frame == frame && begin.cycle == cycle)
			return;
		begin = {};
		begin.begun = true;
		begin.frame = frame;
		begin.cycle = cycle;
		begin.exposureEpoch = exposureEpoch;
		begin.settings = settings;
		begin.color = color;
		begin.viewSourceFrame = neuralCaptureCamera.viewSourceFrame;
		begin.view = neuralCaptureCamera.view;
		begin.projection = neuralCaptureCamera.projection;
		begin.positionAdjust = neuralCaptureCamera.positionAdjust;
	} catch (...) {
		// Missing evidence is rejected by the importer; rendering remains independent.
	}
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
		std::scoped_lock lock(neuralCaptureMutex);
		auto record = neuralCaptureBeginnings[index];
		record.route = route;
		record.settingsChanged = record.settings != settings;
		record.inputsMatched = inputs[index].Matches(static_cast<uint32_t>(index), route.frame,
			route.temporalAdmission.sourceWorldFrame, route.generation, route.insertionPoint);
		if (record.inputsMatched) {
			record.inputs = inputs[index];
			record.color = record.inputs.configuration;
		}
		neuralCaptureRecords[index] = std::make_shared<const NeuralCaptureRecord>(std::move(record));
	} catch (...) {
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
	                       color.experiments.captureFrameEvidence && (!route.requested || record.inputsMatched);
	const char* reason = available ? "" : !record.begun                        ? "frame_not_observed" :
	                                  record.settingsChanged                   ? "settings_changed_during_frame" :
	                                  !record.inputsMatched && route.requested ? "renderer_transaction_unmatched" :
	                                                                             "frame_evidence_unavailable";
	const bool cameraValid = record.viewSourceFrame != 0 && record.viewSourceFrame == world;
	Json result{
		{ "schemaVersion", 1 }, { "available", available }, { "reason", reason },
		{ "transactionId", std::format("{}:{}:{}:{}:{}:{}", static_cast<uint32_t>(route.role), route.frame, world, route.generation, route.insertionPoint, fingerprint) },
		{ "configurationFingerprint", fingerprint }, { "configurationFingerprintAlgorithm", "xxh3-128-json" },
		{ "configuration", configuration }, { "route", GetNeuralStereoRouteRoleName(route.role) },
		{ "generation", route.generation }, { "insertionPoint", route.insertionPoint },
		{ "pairComplete", route.pairComplete }, { "fallbackReason", GetNeuralStereoFallbackReasonName(route.fallbackReason) },
		{ "cameraEvidence", { { "available", cameraValid }, { "reason", cameraValid ? "" : "source_world_matrix_unmatched" },
								{ "sourceWorldFrame", record.viewSourceFrame }, { "view", record.view }, { "projection", record.projection },
								{ "positionAdjust", record.positionAdjust },
								{ "provenance", "engine_cached_unjittered_world_matrices" } } }
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
			{ "outputCommitted", (route.committedEyeMask & bit) != 0 && color.experiments.applyModelEdit && !color.experiments.transportBypass },
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
	return { { "schemaVersion", 1 }, { "enabled", NeuralRendering::Color::Registry::Instance().CaptureEvidenceEnabled() }, { "routes", std::move(routes) } };
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
