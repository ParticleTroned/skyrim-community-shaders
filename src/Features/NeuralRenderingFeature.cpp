#include "NeuralRenderingFeature.h"
#include "BuildProvenance.h"
#include "State.h"
#include "Upscaling.h"
#include "Upscaling/NeuralRendering/CaptureEvidence.h"
#include "Upscaling/NeuralRendering/ConfigurationSerialization.h"
#include "Upscaling/VRRenderScaleDevBenchBridge.h"
#include "Utils/UI.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <imgui.h>
#include <stdexcept>
#include <string_view>
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <DevBenchAPI.h>
#endif

namespace
{
	using namespace NeuralRendering::Color;
	using Json = nlohmann::json;
	constexpr std::array<const char*, 4> modes{ "legacy_raw", "managed", "preserve_source", "neural_lighting" };
	static_assert(modes.size() == static_cast<std::size_t>(Mode::Count));
	constexpr std::array<const char*, 3> domains{ "unknown", "linear", "srgb" };
	constexpr std::array<const char*, 3> transforms{ "identity", "linear_to_srgb", "reversible_proxy" };
	constexpr std::array<const char*, 3> exposureSources{ "manual", "captured_hdr", "captured_hdr_previous" };

	template <class T, std::size_t N>
	T Parse(const Json& value, const std::array<const char*, N>& names)
	{
		const auto text = value.get<std::string>();
		for (std::size_t i = 0; i < names.size(); ++i)
			if (text == names[i])
				return static_cast<T>(i);
		throw std::invalid_argument("unknown enum value: " + text);
	}
	template <class T, std::size_t N>
	const char* Name(T value, const std::array<const char*, N>& names)
	{
		const auto index = static_cast<std::size_t>(value);
		return index < names.size() ? names[index] : "invalid";
	}
	void Keys(const Json& object, std::initializer_list<std::string_view> allowed)
	{
		if (!object.is_object())
			throw std::invalid_argument("expected an object");
		for (const auto& [key, value] : object.items()) {
			(void)value;
			if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
				throw std::invalid_argument("unknown field: " + key);
		}
	}
	float Number(const Json& value)
	{
		if (!value.is_number())
			throw std::invalid_argument("expected a number");
		const auto number = value.get<float>();
		if (!Finite(number))
			throw std::invalid_argument("expected a finite number");
		return number;
	}
	void ReadSettings(const Json& object, Settings& settings)
	{
		Keys(object, { "schemaVersion", "enabled", "mode", "detailStrength", "appearanceMix", "maximumDetailStops", "lightingPreservation" });
		if (object.contains("schemaVersion") && (!object.at("schemaVersion").is_number_integer() || object.at("schemaVersion") != 1))
			throw std::invalid_argument("unsupported colour-settings schema");
		if (object.contains("enabled"))
			settings.enabled = object.at("enabled").get<bool>();
		if (object.contains("mode"))
			settings.mode = Parse<Mode>(object.at("mode"), modes);
		if (object.contains("detailStrength"))
			settings.detailStrength = Number(object.at("detailStrength"));
		if (object.contains("appearanceMix"))
			settings.appearanceMix = Number(object.at("appearanceMix"));
		if (object.contains("maximumDetailStops"))
			settings.maximumDetailStops = Number(object.at("maximumDetailStops"));
		if (object.contains("lightingPreservation")) {
			const auto& value = object.at("lightingPreservation");
			const float number = Number(value);
			// Check the JSON value before float rounding can hide an out-of-range input.
			if (value < 0.0 || value > 1.0)
				throw std::invalid_argument("lighting preservation outside supported range");
			settings.lightingPreservation = number;
		}
		if (!Valid(settings))
			throw std::invalid_argument("colour settings outside supported ranges");
	}
	Json SettingsJson(const Settings& settings)
	{
		return { { "schemaVersion", 1 }, { "enabled", settings.enabled }, { "mode", Name(settings.mode, modes) },
			{ "detailStrength", settings.detailStrength }, { "appearanceMix", settings.appearanceMix },
			{ "maximumDetailStops", settings.maximumDetailStops }, { "lightingPreservation", settings.lightingPreservation } };
	}
	Json ProfileJson(const Profile& profile)
	{
		return { { "domain", Name(profile.domain, domains) }, { "transform", Name(profile.transform, transforms) },
			{ "exposureMultiplier", profile.exposureMultiplier }, { "exposureSource", Name(profile.exposureSource, exposureSources) },
			{ "domainOrigin", profile.domain == Domain::Unknown ? "unknown" : "explicit_candidate" },
			{ "exposureOrigin", profile.exposureSource == ExposureSource::CapturedHDR         ? "frame_matched_engine_hdr_capture_required" :
								profile.exposureSource == ExposureSource::CapturedHDRPrevious ? "previous_source_frame_hdr_capture_required" :
																								(profile.transform == Transform::Identity ? "not_applied" : "manual_calibration") },
			{ "nrModelDomainVerified", false }, { "srInternalExposureAvailable", false } };
	}
	void ReadProfile(const Json& object, Profile& profile)
	{
		Keys(object, { "domain", "transform", "exposureMultiplier", "exposureSource" });
		if (object.contains("domain"))
			profile.domain = Parse<Domain>(object.at("domain"), domains);
		if (object.contains("transform"))
			profile.transform = Parse<Transform>(object.at("transform"), transforms);
		if (object.contains("exposureMultiplier"))
			profile.exposureMultiplier = Number(object.at("exposureMultiplier"));
		if (object.contains("exposureSource"))
			profile.exposureSource = Parse<ExposureSource>(object.at("exposureSource"), exposureSources);
		if (!Valid(profile))
			throw std::invalid_argument("codec requires explicit linear source; identity requires manual multiplier one");
	}
	Json EvidenceJson(const ExposureEvidence& e)
	{
		const bool spatialUnit = e.values[3] == 1.0f && e.values[2] == 1.0f &&
		                         std::any_of(e.texels.begin(), e.texels.begin() + std::min<std::size_t>(e.sourceWidth * e.sourceHeight, e.texels.size()),
									 [&](const auto& texel) { return texel[0] != e.values[0] || texel[1] != e.values[1]; });
		const auto scalarStatus = !e.readbackComplete ? "pending" :
		                          spatialUnit         ? "measured_unit_ratio" :
		                          e.values[3] == 1.0f ? "measured_uniform_ratio" :
		                          e.values[3] == 2.0f ? "unmeasured_unit_fallback" :
		                          e.values[3] == 3.0f ? "non_uniform_avgtex" :
		                                                "invalid_values";
		return { { "frame", e.stamp.frame }, { "epoch", e.stamp.epoch }, { "sequence", e.stamp.sequence },
			{ "ambiguous", e.stamp.ambiguous }, { "sourceFormat", e.sourceFormat }, { "sourceViewFormat", e.sourceViewFormat },
			{ "outputViewFormat", e.outputViewFormat }, { "sourceIdentity", e.sourceIdentity }, { "shaderIdentity", e.shaderIdentity },
			{ "sourceViewIdentity", e.sourceViewIdentity }, { "samplerIdentity", e.samplerIdentity },
			{ "sourceWidth", e.sourceWidth }, { "sourceHeight", e.sourceHeight }, { "sourceMip", e.sourceMip },
			{ "texels", e.texels }, { "scalarStatus", scalarStatus },
			{ "producer", e.producer }, { "readbackComplete", e.readbackComplete }, { "rawValues", e.values },
			{ "engineRatioValid", e.readbackComplete && !e.stamp.ambiguous && e.values[3] == 1.0f },
			{ "frameGammaExponent", e.gammaKnown ? Json(e.frameGammaExponent) : Json(nullptr) },
			{ "gammaInterpretation", "FrameParams.x power exponent; not proof of IEC sRGB or NR model input domain" } };
	}
	Json CaptureJson()
	{
		const auto capture = ExposureCapture::Instance().GetStatus();
		Json samples = Json::array();
		for (const auto& e : capture.samples)
			if (e.stamp.sequence)
				samples.push_back(EvidenceJson(e));
		const auto& b = capture.lastBinding;
		return { { "requested", capture.requested }, { "hooksInstalled", 0 }, { "producersRegistered", capture.producersRegistered }, { "epoch", capture.epoch },
			{ "captures", capture.captures }, { "rejected", capture.rejected }, { "droppedReadbacks", capture.dropped },
			{ "producerScopes", capture.producerScopes }, { "lastProducerFrame", capture.lastProducerFrame },
			{ "graphicsStateFlushes", capture.graphicsStateFlushes }, { "lastGraphicsStateFlushFrame", capture.lastGraphicsStateFlushFrame },
			{ "drawCounts", capture.drawCounts },
			{ "drawKinds", { "DrawIndexed", "Draw", "DrawIndexedInstanced", "DrawInstanced", "DrawAuto", "DrawIndexedInstancedIndirect", "DrawInstancedIndirect" } },
			{ "lastReason", capture.lastReason }, { "samples", samples },
			{ "captureBoundary", "scoped_HDR_finalized_graphics_bindings_or_D3D11_draw_entry" },
			{ "lastBinding", { { "frame", b.frame }, { "width", b.width }, { "height", b.height },
								 { "viewWidth", b.viewWidth }, { "viewHeight", b.viewHeight }, { "visibleMips", b.visibleMips },
								 { "samplerIdentity", b.samplerIdentity }, { "samplerFilter", b.samplerFilter },
								 { "samplerAddressU", b.samplerAddressU }, { "samplerAddressV", b.samplerAddressV },
								 { "mip", b.mip }, { "mipLevels", b.mipLevels }, { "arraySize", b.arraySize }, { "samples", b.samples },
								 { "sourceFormat", b.sourceFormat }, { "viewFormat", b.viewFormat }, { "viewDimension", b.viewDimension },
								 { "sourceIdentity", b.sourceIdentity }, { "shaderIdentity", b.shaderIdentity },
								 { "expectedShaderIdentity", b.expectedShaderIdentity }, { "viewIdentity", b.viewIdentity } } },
			{ "formula", "ISHDR BLEND AvgTex.y / AvgTex.x; zero input has a distinct unmeasured unit fallback" } };
	}
	Json ObservationJson(const Observation& o)
	{
		return { { "frame", o.frame }, { "sourceWorldFrame", o.sourceWorldFrame }, { "physicalSlot", o.slot },
			{ "insertionPoint", o.insertion }, { "generation", o.generation }, { "revision", o.revision },
			{ "measurementBatchId", o.measurementBatchId }, { "expectedMeasurementSlotMask", o.expectedMeasurementSlotMask },
			{ "rect", { o.rect.baseX, o.rect.baseY, o.rect.width, o.rect.height } },
			{ "sourceFormat", o.sourceFormat }, { "outputFormat", o.outputFormat }, { "effectiveMode", Name(o.mode, modes) },
			{ "profile", ProfileJson(o.profile) }, { "transportBypass", o.bypass }, { "modelEditShown", o.modelEditShown },
			{ "lightingPreservation", o.lightingPreservation }, { "atomicColourBatch", o.atomicStereo }, { "processed", o.processed }, { "retainedColourTextureBytes", o.retainedBytes },
			{ "exposureAgeFrames", o.exposureState == ExposureBindingState::SnapshotQueued && o.exposure.stamp.frame <= o.sourceWorldFrame ?
									   Json(o.sourceWorldFrame - o.exposure.stamp.frame) :
									   Json(nullptr) },
			{ "preparationCpuMicroseconds", o.preparationCpuMicroseconds }, { "reconstructionCpuMicroseconds", o.reconstructionCpuMicroseconds },
			{ "exposureBinding", ExposureBindingName(o.exposureState) }, { "exposure", EvidenceJson(o.exposure) }, { "failure", o.failure } };
	}
	Json MeasurementJson(const Measurement& m)
	{
		return { { "source", ObservationJson(m.source) }, { "values", m.data }, { "measurementVersion", 2 },
			{ "invalidForwardSamples", m.data[16] }, { "invalidInverseSamples", m.data[17] },
			{ "effectiveExposure", m.data[18] }, { "effectiveExposureValid", m.data[19] == 1.0f },
			{ "capturedAverage", m.data[20] }, { "capturedTarget", m.data[21] }, { "capturedRatio", m.data[22] },
			{ "capturedRatioValid", m.source.exposureState == ExposureBindingState::SnapshotQueued && m.data[23] == 1.0f } };
	}
	Json StatusJson()
	{
		const auto config = Registry::Instance().Snapshot();
		const auto status = Registry::Instance().GetStatus();
		Json slots = Json::array(), measurements = Json::array(), batches = Json::array();
		for (const auto& o : status.slots)
			if (o.revision)
				slots.push_back(ObservationJson(o));
		for (const auto& m : status.measurements) {
			if (!m.source.revision)
				continue;
			measurements.push_back(MeasurementJson(m));
		}
		for (const auto& batch : status.measurementBatches) {
			if (!batch.Complete())
				continue;
			batches.push_back(MeasurementBatchEvidenceJson(batch));
		}
		return { { "ok", true }, { "apiVersion", 3 }, { "revision", config.revision }, { "settings", SettingsJson(config.settings) },
			{ "effectiveMode", Name(config.EffectiveMode(), modes) },
			{ "experiments", { { "upscaled_center", ProfileJson(config.experiments.profiles[0]) },
								 { "final_ldr_pre_ui", ProfileJson(config.experiments.profiles[1]) },
								 { "transportBypass", config.experiments.transportBypass }, { "diagnostics", config.experiments.diagnostics },
								 { "captureEngineExposure", config.experiments.captureEngineExposure },
								 { "captureFrameEvidence", config.experiments.captureFrameEvidence }, { "applyModelEdit", config.experiments.applyModelEdit } } },
			{ "captureEvidenceSchemaVersion", 1 },
			{ "inputEpoch", config.inputEpoch }, { "slots", slots }, { "measurements", measurements }, { "engineCapture", CaptureJson() },
			{ "measurementBatches", std::move(batches) },
			{ "counts", { { "prepared", status.prepared }, { "reconstructed", status.reconstructed }, { "failed", status.failed },
							{ "bypassed", status.bypassed }, { "samples", status.samples }, { "dropped", status.dropped },
							{ "evictedIncompleteBatches", status.evictedIncompleteBatches } } },
			{ "note", "Configuration acceptance is not render success. Require fresh matching-eye measurements. Capture is engine evidence, not a verified NR colour-space contract. CPU enqueue times are not GPU timings." } };
	}
	Json AssetsJson()
	{
		const std::array<const char*, 6> paths{
			"Data/Shaders/Features/NeuralRendering.ini",
			"Data/Shaders/Upscaling/NeuralRendering/ColorCommon.hlsli",
			"Data/Shaders/Upscaling/NeuralRendering/ColorPrepareCS.hlsl",
			"Data/Shaders/Upscaling/NeuralRendering/ColorReconstructCS.hlsl",
			"Data/Shaders/Upscaling/NeuralRendering/ColorMeasureCS.hlsl",
			"Data/Shaders/Upscaling/NeuralRendering/ColorExposureCS.hlsl"
		};
		Json assets = Json::array();
		bool complete = true;
		for (const auto* path : paths) {
			std::error_code error;
			const bool present = std::filesystem::is_regular_file(path, error);
			complete = complete && present && !error;
			assets.push_back({ { "path", path }, { "present", present }, { "error", error.message() } });
		}
		return { { "ok", true }, { "action", "assets" }, { "allPresent", complete }, { "assets", assets },
			{ "hashVerified", false }, { "note", "Presence only. Run tools/nr-color/verify_assets.py against staged/deployed Data for exact source hash parity." } };
	}
#ifdef DEVBENCH_BRIDGE_ENABLED
	void Handler(void*, const char* arguments, void* sink, DevBenchAPI::WriteFn write)
	{
		if (!write)
			return;
		Json response;
		std::string errorCode = "nr_color_invalid_request";
		try {
			const auto request = Json::parse(arguments ? arguments : "{}");
			const auto action = request.at("action").get<std::string>();
			if (action == "status" || action == "assets") {
				Keys(request, { "action" });
			} else if (action == "capture_diagnostics") {
				Keys(request, { "action", "stamps" });
				const auto& stamps = request.at("stamps");
				if (!stamps.is_array() || stamps.empty() || stamps.size() > 64)
					throw std::invalid_argument("stamps requires one to 64 exact exposure identities");
				Json exposures = Json::array();
				for (const auto& key : stamps) {
					Keys(key, { "frame", "epoch", "sequence" });
					for (const auto* field : { "frame", "epoch", "sequence" })
						if (!key.at(field).is_number_unsigned() || key.at(field) == 0)
							throw std::invalid_argument("exposure identities must be positive unsigned integers");
					if (key.at("frame").get<uint64_t>() > UINT32_MAX)
						throw std::invalid_argument("exposure frame exceeds uint32");
					ExposureStamp stamp{};
					stamp.frame = key.at("frame").get<uint32_t>();
					stamp.epoch = key.at("epoch").get<uint64_t>();
					stamp.sequence = key.at("sequence").get<uint64_t>();
					const auto found = ExposureCapture::Instance().GetEvidence(stamp);
					exposures.push_back(ExposureLookupEvidenceJson(found));
				}
				response = { { "ok", true }, { "apiVersion", 3 }, { "captureEvidenceSchemaVersion", 1 }, { "exposures", std::move(exposures) } };
			} else if (action == "configure" || action == "reset_experiments") {
				Keys(request, { "action", "settings", "experiments", "expectedRevision" });
				auto config = Registry::Instance().Snapshot();
				if (request.contains("expectedRevision")) {
					const auto& expected = request.at("expectedRevision");
					if (!expected.is_number_integer() || expected <= 0)
						throw std::invalid_argument("invalid revision");
					if (expected.get<std::uint64_t>() != config.revision) {
						errorCode = "nr_color_revision_conflict";
						throw std::invalid_argument("configuration changed; read status again");
					}
				}
				if (action == "reset_experiments") {
					if (request.contains("settings") || request.contains("experiments"))
						throw std::invalid_argument("reset_experiments does not accept settings");
					config.experiments = {};
				} else {
					if (!request.contains("settings") && !request.contains("experiments"))
						throw std::invalid_argument("configure requires settings or experiments");
					if (request.contains("settings"))
						ReadSettings(request.at("settings"), config.settings);
					if (request.contains("experiments")) {
						const auto& e = request.at("experiments");
						Keys(e, { "upscaled_center", "final_ldr_pre_ui", "transportBypass", "diagnostics", "captureEngineExposure", "captureFrameEvidence", "applyModelEdit" });
						if (e.contains("upscaled_center"))
							ReadProfile(e.at("upscaled_center"), config.experiments.profiles[0]);
						if (e.contains("final_ldr_pre_ui"))
							ReadProfile(e.at("final_ldr_pre_ui"), config.experiments.profiles[1]);
						if (e.contains("transportBypass"))
							config.experiments.transportBypass = e.at("transportBypass").get<bool>();
						if (e.contains("diagnostics"))
							config.experiments.diagnostics = e.at("diagnostics").get<bool>();
						if (e.contains("captureEngineExposure"))
							config.experiments.captureEngineExposure = e.at("captureEngineExposure").get<bool>();
						if (e.contains("captureFrameEvidence"))
							config.experiments.captureFrameEvidence = e.at("captureFrameEvidence").get<bool>();
						if (e.contains("applyModelEdit"))
							config.experiments.applyModelEdit = e.at("applyModelEdit").get<bool>();
					}
				}
				if (!Registry::Instance().Configure(config.settings, config.experiments, config.revision)) {
					errorCode = "nr_color_revision_conflict";
					throw std::invalid_argument("invalid or concurrently changed configuration");
				}
			} else
				throw std::invalid_argument("unknown action");
			if (action != "capture_diagnostics")
				response = action == "assets" ? AssetsJson() : StatusJson();
			response["action"] = action;
		} catch (const std::exception& error) {
			response = { { "ok", false }, { "error", error.what() }, { "errorCode", errorCode } };
		} catch (...) {
			response = { { "ok", false }, { "error", "colour handler failed" }, { "errorCode", "nr_color_handler_failed" } };
		}
		try {
			BuildProvenance::AttachProducer(response);
			const auto text = response.dump();
			write(sink, text.c_str());
		} catch (...) {
			write(sink, R"({"ok":false,"error":"colour status serialization failed"})");
		}
	}
	Json Descriptor()
	{
		return Json::parse(R"schema({
  "description": "NR colour v3: Managed is experimental and selectable in the menu only in Developer Mode (Debug/Trace). Existing saved managed selections remain visible and unchanged outside Developer Mode; automation retains the managed value. opt-in captureFrameEvidence freezes CPU configuration and outer stereo outcomes for accepted HMD screenshots without enabling colour passes or changing input epochs. Shared live controls, display-only A/B, engine HDR exposure capture and asynchronous measurements. Accepted screenshots additionally retain exact CPU companions in terminal actual.captureDiagnostics; callers need not poll rolling status to recover those captures. measurementBatches retains up to four complete private-reconstruction batches, each with an immutable batch ID, expected physical-slot mask and matching frame/revision/generation. Pending readbacks drain even when a region becomes inactive; latest-per-slot measurements remain diagnostic compatibility fields. Complete batches do not prove outer stereo commit or headset presentation. status also reports registered HDR producers and rejected draw bindings. expectedShaderIdentity is the exact shader recorded by the engine/replacement binding hook for this context, producer, engine selection, frame and capture epoch, or the original engine shader when no matching association exists; the live draw must still match it. Capture accepts one visible mip of a 1x1 or 2x2 AvgTex with ordinary non-border sampling. Capture observes finalized engine graphics bindings after BSGraphics_SetDirtyStates and CS state updates, before the HDR draw, as well as all seven D3D11 draw forms inside the exact HDR effect scope. The engine boundary remains valid when D3D11 replaces its per-context draw method entries. Compute flushes and unrelated effects are excluded. producerScopes, lastProducerFrame, graphicsStateFlushes, lastGraphicsStateFlushFrame and drawCounts expose the reached boundaries. Each snapshot producer identifies its actual capture boundary. captured_hdr requires the exact source frame. captured_hdr_previous explicitly requires sourceWorldFrame minus one for pre-HDR experiments; the producer stamp is unchanged and exposureAgeFrames reports the real age. Older, ambiguous and cross-epoch captures are rejected. GPU scalar validity requires identical raw pairs or finite positive x == y in every texel (measured_unit_ratio); texels retain row-major per-texel average, target, ratio and validity, while scalarStatus distinguishes non_uniform_avgtex from a measured_uniform_ratio or an unmeasured_unit_fallback. Other differing fields are observed but never averaged into a correction. Capture alone does not enable reconstruction. configure/reset change only the registry. assets checks presence, not compilation. No NVIDIA ABI assumptions or game/profile mutations.",
  "outputSchema": {
    "type": "object",
    "properties": {
      "apiVersion": { "const": 3 },
      "captureEvidenceSchemaVersion": { "const": 1 },
      "exposures": { "type": "array", "maxItems": 64, "items": { "type": "object" } },
      "engineCapture": {
        "type": "object",
        "properties": {
          "producerScopes": { "type": "integer", "minimum": 0 },
          "lastProducerFrame": { "type": "integer", "minimum": 0 },
          "drawCounts": { "type": "array", "minItems": 7, "maxItems": 7, "items": { "type": "integer", "minimum": 0 } },
          "drawKinds": { "type": "array", "minItems": 7, "maxItems": 7 },
          "samples": {
            "type": "array", "maxItems": 8,
            "items": {
              "type": "object",
              "properties": {
                "sourceWidth": { "type": "integer", "minimum": 1, "maximum": 2 },
                "sourceHeight": { "type": "integer", "minimum": 1, "maximum": 2 },
                "sourceMip": { "type": "integer", "minimum": 0 },
                "sourceViewIdentity": { "type": "integer", "minimum": 0 },
                "samplerIdentity": { "type": "integer", "minimum": 0 },
                "scalarStatus": { "enum": ["pending", "measured_uniform_ratio", "measured_unit_ratio", "unmeasured_unit_fallback", "non_uniform_avgtex", "invalid_values"] },
                "texels": {
                  "type": "array", "minItems": 4, "maxItems": 4,
                  "description": "Row-major average, target, ratio, validity for sourceWidth*sourceHeight texels; unused entries are invalid. Validity 0=invalid, 1=measured, 2=unit fallback. The scalar summary also uses 3=non-uniform.",
                  "items": { "type": "array", "minItems": 4, "maxItems": 4, "items": { "type": ["number", "null"] } }
                }
              }
            }
          },
          "lastBinding": {
            "type": "object",
            "properties": {
              "viewWidth": { "type": "integer", "minimum": 0 },
              "viewHeight": { "type": "integer", "minimum": 0 },
              "visibleMips": { "type": "integer", "minimum": 0 },
              "samplerIdentity": { "type": "integer", "minimum": 0 },
              "samplerFilter": { "type": "integer", "minimum": 0 },
              "samplerAddressU": { "type": "integer", "minimum": 0 },
              "samplerAddressV": { "type": "integer", "minimum": 0 },
              "expectedShaderIdentity": {
                "type": "integer", "minimum": 0,
                "description": "Exact recorded engine/replacement selection for this context, HDR producer, engine selection, frame and capture epoch; otherwise the original engine shader."
              }
            }
          }
        }
      },
      "measurementBatches": {
        "type": "array", "maxItems": 4,
        "items": {
          "type": "object",
          "required": ["measurementBatchId", "expectedMeasurementSlotMask", "frame", "sourceWorldFrame", "generation", "revision", "insertionPoint", "atomicColourBatch", "measurements"],
          "properties": {
            "measurementBatchId": { "type": "integer", "minimum": 1 },
            "expectedMeasurementSlotMask": { "type": "integer", "minimum": 1, "maximum": 255 },
            "frame": { "type": "integer", "minimum": 0, "maximum": 4294967295 },
            "sourceWorldFrame": { "type": "integer", "minimum": 0, "maximum": 4294967294 },
            "generation": { "type": "integer", "minimum": 0 },
            "revision": { "type": "integer", "minimum": 1 },
            "insertionPoint": { "enum": [0, 1] },
            "atomicColourBatch": { "type": "boolean" },
            "measurements": { "type": "array", "minItems": 1, "maxItems": 4, "items": { "type": "object" } }
          }
        }
      }
    }
  },
  "inputSchema": {
    "type": "object",
    "additionalProperties": false,
    "required": [
      "action"
    ],
    "properties": {
      "action": {
        "type": "string",
        "enum": [
          "status",
          "configure",
          "reset_experiments",
          "capture_diagnostics",
          "assets"
        ]
      },
      "expectedRevision": {
        "type": "integer",
        "minimum": 1
      },
      "stamps": {
        "type": "array", "minItems": 1, "maxItems": 64,
        "items": { "type": "object", "additionalProperties": false,
          "required": ["frame", "epoch", "sequence"],
          "properties": {
            "frame": { "type": "integer", "minimum": 1, "maximum": 4294967295 },
            "epoch": { "type": "integer", "minimum": 1 },
            "sequence": { "type": "integer", "minimum": 1 }
          }
        }
      },
      "settings": {
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "schemaVersion": {
            "const": 1
          },
          "enabled": {
            "type": "boolean"
          },
          "mode": {
            "type": "string",
            "description": "managed is experimental colour/exposure reconstruction, not a calibrated production preset. Its menu entry requires Developer Mode; API and saved enum values remain supported. preserve_source suppresses broad lighting according to lightingPreservation; neural_lighting retains source colour while admitting the full bounded neural brightness field.",
            "enum": [
              "legacy_raw",
              "managed",
              "preserve_source",
              "neural_lighting"
            ]
          },
          "detailStrength": {
            "type": "number",
            "minimum": 0,
            "maximum": 2
          },
          "appearanceMix": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          },
          "lightingPreservation": {
            "type": "number",
            "minimum": 0,
            "maximum": 1,
            "description": "Fraction of the existing smooth luminance residual removed in Preserve Source; default 1. Reconstruction only; does not reset inference history."
          },
          "maximumDetailStops": {
            "type": "number",
            "minimum": 0,
            "maximum": 2
          }
        }
      },
      "experiments": {
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "upscaled_center": {
            "type": "object",
            "additionalProperties": false,
            "properties": {
              "domain": {
                "type": "string",
                "enum": [
                  "unknown",
                  "linear",
                  "srgb"
                ]
              },
              "transform": {
                "type": "string",
                "enum": [
                  "identity",
                  "linear_to_srgb",
                  "reversible_proxy"
                ]
              },
              "exposureMultiplier": {
                "type": "number",
                "minimum": 0.00390625,
                "maximum": 256
              },
              "exposureSource": {
                "type": "string",
                "enum": [
                  "manual",
                  "captured_hdr",
                  "captured_hdr_previous"
                ]
              }
            }
          },
          "final_ldr_pre_ui": {
            "type": "object",
            "additionalProperties": false,
            "properties": {
              "domain": {
                "type": "string",
                "enum": [
                  "unknown",
                  "linear",
                  "srgb"
                ]
              },
              "transform": {
                "type": "string",
                "enum": [
                  "identity",
                  "linear_to_srgb",
                  "reversible_proxy"
                ]
              },
              "exposureMultiplier": {
                "type": "number",
                "minimum": 0.00390625,
                "maximum": 256
              },
              "exposureSource": {
                "type": "string",
                "enum": [
                  "manual",
                  "captured_hdr",
                  "captured_hdr_previous"
                ]
              }
            }
          },
          "transportBypass": {
            "type": "boolean"
          },
          "diagnostics": {
            "type": "boolean"
          },
          "captureEngineExposure": {
            "type": "boolean"
          },
          "captureFrameEvidence": {
            "type": "boolean",
            "description": "Arm CPU-only frozen NR configuration/outcome evidence for HMD screenshots. Does not enable colour passes or change input epochs."
          },
          "applyModelEdit": {
            "type": "boolean"
          }
        }
      }
    }
  }
})schema");
	}

#endif
}

namespace NeuralRendering::Color
{
	nlohmann::json ConfigurationEvidenceJson(const Configuration& config)
	{
		return { { "settings", SettingsJson(config.settings) },
			{ "experiments", { { "upscaled_center", ProfileJson(config.experiments.profiles[0]) },
								 { "final_ldr_pre_ui", ProfileJson(config.experiments.profiles[1]) },
								 { "transportBypass", config.experiments.transportBypass }, { "diagnostics", config.experiments.diagnostics },
								 { "captureEngineExposure", config.experiments.captureEngineExposure },
								 { "captureFrameEvidence", config.experiments.captureFrameEvidence }, { "applyModelEdit", config.experiments.applyModelEdit } } } };
	}
	nlohmann::json ObservationEvidenceJson(const Observation& observation) { return ObservationJson(observation); }
	nlohmann::json ExposureEvidenceJson(const ExposureEvidence& evidence) { return EvidenceJson(evidence); }
	nlohmann::json ExposureLookupEvidenceJson(const ExposureEvidenceLookup& lookup)
	{
		const auto& k = lookup.key;
		return { { "key", { { "frame", k.frame }, { "epoch", k.epoch }, { "sequence", k.sequence } } },
			{ "available", lookup.evidence && lookup.evidence->readbackComplete },
			{ "reason", lookup.reason }, { "evidence", lookup.evidence ? EvidenceJson(*lookup.evidence) : Json(nullptr) } };
	}
	nlohmann::json MeasurementBatchEvidenceJson(const MeasurementBatch<Measurement>& batch)
	{
		Json samples = Json::array();
		for (std::uint32_t slot = 0; slot < batch.samples.size(); ++slot)
			if ((batch.receivedSlotMask & (1u << slot)) != 0)
				samples.push_back(MeasurementJson(batch.samples[slot]));
		const auto& k = batch.key;
		return { { "measurementBatchId", k.id }, { "expectedMeasurementSlotMask", k.expectedSlotMask },
			{ "frame", k.frame }, { "sourceWorldFrame", k.sourceWorldFrame }, { "generation", k.generation },
			{ "revision", k.revision }, { "insertionPoint", k.insertion }, { "atomicColourBatch", k.atomicStereo },
			{ "measurements", std::move(samples) } };
	}
}

NeuralRenderingFeature& NeuralRenderingFeature::Instance()
{
	static NeuralRenderingFeature instance;
	return instance;
}
void NeuralRenderingFeature::LoadSettings(nlohmann::json& object)
{
	Settings colour;
	try {
		if (object.contains("colour")) {
			Keys(object, { "schemaVersion", "rendering", "colour" });
			if (object.value("schemaVersion", 0) != 1)
				throw std::invalid_argument("unsupported neural-rendering settings schema");
			ReadSettings(object.at("colour"), colour);
		} else {
			ReadSettings(object, colour);
		}
		if (object.contains("rendering")) {
			std::string error;
			if (!globals::features::upscaling.ApplyNeuralRenderingConfiguration(NeuralRendering::PersistentRenderingSettings(object.at("rendering")), error))
				throw std::invalid_argument(error);
		}
		if (!Registry::Instance().Configure(colour, {}))
			throw std::runtime_error("colour configuration was not accepted");
	} catch (const std::exception& error) {
		logger::warn("[NeuralRendering] Invalid saved settings: {}", error.what());
	}
}
void NeuralRenderingFeature::SaveSettings(nlohmann::json& object)
{
	auto rendering = NeuralRendering::PersistentRenderingSettings(globals::features::upscaling.GetNeuralRenderingConfiguration());
	object = { { "schemaVersion", 1 },
		{ "rendering", std::move(rendering) },
		{ "colour", SettingsJson(Registry::Instance().Snapshot().settings) } };
}
void NeuralRenderingFeature::RestoreDefaultSettings()
{
	if (!globals::features::upscaling.ResetNeuralRenderingConfiguration()) {
		logger::warn("[NeuralRendering] Backend reset remains pending");
		return;
	}
	if (!Registry::Instance().Configure({}, {}))
		logger::warn("[NeuralRendering] Colour defaults could not be applied");
}
void NeuralRenderingFeature::EarlyPrepass()
{
	globals::features::upscaling.SetNeuralRenderingFeatureAvailable(loaded);
	ExposureCapture::Instance().RefreshProducers();
}
void NeuralRenderingFeature::DrawSettings()
{
	globals::features::upscaling.DrawNeuralRenderingSettings(
		globals::features::upscaling.GetUpscaleMethod());
	const auto& upscaling = globals::features::upscaling;
	auto fovAvailabilityGuard = Util::DisableGuard(
		NeuralRendering::RequiresFoveatedMask(upscaling.GetNeuralRenderingMode(), upscaling.settings.neuralRenderingFovOnly, globals::game::isVR, upscaling.settings.neuralRenderingRenderscaleFov) &&
		!upscaling.IsNeuralRenderingFovConfigurationAvailable(upscaling.GetUpscaleMethod()));
	ImGui::SeparatorText("Colour processing");
	const bool showDiagnostics = globals::state && globals::state->IsDeveloperMode();
	auto config = Registry::Instance().Snapshot();
	bool changed = false;
	ImGui::TextWrapped("These controls apply to full-resolution, FOV and before-upscaling NR, including characters.");
	changed |= ImGui::Checkbox("Enable colour processing", &config.settings.enabled);
	if (auto tooltip = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted("Applies the selected colour mode and preservation settings. Off uses Original model output while NR stays on; your colour settings are retained.");
	static constexpr std::array colourModes{ "Original", "Managed (experimental)", "Preserve source", "Neural lighting" };
	static constexpr std::array colourModeHelp{
		"Uses the model output directly, without source-colour preservation. NR remains active.",
		"Uses experimental colour and exposure reconstruction with session-only calibration. Preservation sliders do not apply.",
		"Keeps the game's colour and adds neural brightness detail. The preservation sliders control how much lighting and model appearance may change.",
		"Keeps the game's source colour and alpha while applying the full bounded neural brightness field. Model chroma is not applied."
	};
	static_assert(colourModes.size() == static_cast<std::size_t>(Mode::Count) && colourModeHelp.size() == colourModes.size());
	const bool colourModeOpen = ImGui::BeginCombo("Colour mode", colourModes[static_cast<std::size_t>(config.settings.mode)]);
	if (!colourModeOpen) {
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted(colourModeHelp[static_cast<std::size_t>(config.settings.mode)]);
	}
	if (colourModeOpen) {
		for (std::size_t index = 0; index < colourModes.size(); ++index) {
			const auto mode = static_cast<Mode>(index);
			if (mode == Mode::Managed && !showDiagnostics)
				continue;
			const bool selected = config.settings.mode == mode;
			if (ImGui::Selectable(colourModes[index], selected)) {
				config.settings.mode = mode;
				changed = true;
			}
			if (auto tooltip = Util::HoverTooltipWrapper())
				ImGui::TextUnformatted(colourModeHelp[index]);
			if (selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::TextWrapped("Original uses the model output directly. Preserve source retains selected neural detail. Neural lighting keeps source colour while applying neural brightness. Turning colour processing off uses Original while NR stays enabled.");
	if (config.settings.mode == Mode::Managed) {
		ImGui::TextWrapped("Managed is experimental colour/exposure reconstruction with no validated production calibration. Preservation sliders do not apply.");
		ImGui::TextWrapped(showDiagnostics ?
							   "Adjust its session-only calibration under Colour experiments and diagnostics. Identity calibration can look like Original." :
							   "Your saved mode is retained. Choose Original or Preserve source, or set Log Level to Debug to inspect its calibration.");
	}
	const bool usesSourceColourReconstruction =
		config.settings.mode == Mode::PreserveSource || config.settings.mode == Mode::NeuralLighting;
	{
		const bool preservationActive = config.EffectiveMode() == Mode::PreserveSource && config.settings.appearanceMix < 1.0f &&
		                                config.settings.detailStrength > 0.0f && config.settings.maximumDetailStops > 0.0f;
		auto preservationGuard = Util::DisableGuard(!preservationActive);
		float preservationPercent = config.settings.lightingPreservation * 100.0f;
		if (ImGui::SliderFloat("Lighting preservation", &preservationPercent, 0, 100, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
			config.settings.lightingPreservation = preservationPercent / 100.0f;
			changed = true;
		}
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted("100% suppresses broad neural brightness changes; 0% allows them. Fine detail can remain at either end. Applies to Preserve source; Neural lighting always uses 0% without changing this saved value.");
	}
	if (config.settings.mode == Mode::NeuralLighting)
		ImGui::TextWrapped("Neural lighting admits the full bounded neural brightness field. Lighting preservation and Neural appearance mix do not apply.");
	else if (config.settings.mode != Mode::PreserveSource)
		ImGui::TextWrapped("Choose Preserve source to adjust lighting preservation.");
	else if (!config.settings.enabled)
		ImGui::TextWrapped("Enable colour processing to apply lighting preservation. Your settings are retained.");
	else if (config.settings.appearanceMix == 1.0f)
		ImGui::TextWrapped("Lower Neural appearance mix below 1 to use lighting preservation.");
	else if (config.settings.detailStrength == 0.0f || config.settings.maximumDetailStops == 0.0f)
		ImGui::TextWrapped("Raise Detail contribution and Maximum detail gain above zero to use lighting preservation.");
	if (usesSourceColourReconstruction) {
		changed |= ImGui::SliderFloat("Detail contribution", &config.settings.detailStrength, 0, 2);
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted("Sets the strength of neural brightness applied to source colour. 0 removes it; 1 uses its normal strength; 2 doubles it before the gain limit. Shared by Preserve source and Neural lighting.");
	}
	if (config.settings.mode == Mode::PreserveSource) {
		changed |= ImGui::SliderFloat("Neural appearance mix", &config.settings.appearanceMix, 0, 1);
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted("0 uses source colour with the selected neural detail; 1 uses the reconstructed model appearance. Higher values admit more model colour and lighting and bypass more preservation.");
	}
	if (usesSourceColourReconstruction) {
		changed |= ImGui::SliderFloat("Maximum detail gain (stops)", &config.settings.maximumDetailStops, 0, 2);
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted("Caps neural brightening and darkening: 1 stop allows up to twice or half the source brightness. Shared by Preserve source and Neural lighting; Preserve source appearance mix is outside this limit.");
	}
	const bool outputOverrideActive = config.experiments.transportBypass || !config.experiments.applyModelEdit ||
	                                  (config.EffectiveMode() != Mode::LegacyRaw &&
										  std::any_of(config.experiments.profiles.begin(), config.experiments.profiles.end(),
											  [](const Profile& profile) { return profile.transform != Transform::Identity; }));
	if (!showDiagnostics && outputOverrideActive) {
		ImGui::TextWrapped("Colour experiments are affecting the image. Set Log Level to Debug to inspect them.");
		if (ImGui::Button("Restore normal colour processing")) {
			config.experiments = {};
			changed = true;
		}
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted("Clears session-only colour experiments and restores visible neural edits. Keeps your saved colour mode and preservation sliders.");
	}
	if (showDiagnostics && ImGui::TreeNode("Colour experiments and diagnostics")) {
		ImGui::TextWrapped("Save Settings stores the colour mode and sliders. Assessment overrides are session only.");
		changed |= ImGui::Checkbox("Apply neural edit (A/B; inference stays running)", &config.experiments.applyModelEdit);
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted("Shows or hides the neural edit while keeping inference running for same-scene comparisons. Use the main NR switch for NR-off performance measurements.");
		ImGui::TextWrapped("Uncheck Apply neural edit to show the original image while inference keeps running. Use the main NR switch to measure NR-off performance.");
		ImGui::SeparatorText("Exposure and assessment");
		changed |= ImGui::Checkbox("Capture engine HDR exposure", &config.experiments.captureEngineExposure);
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted("Records the game's HDR exposure for diagnostics and exposure experiments. Recording alone does not change NR colour or select an exposure correction.");
		changed |= ImGui::Checkbox("Capture HMD frame provenance", &config.experiments.captureFrameEvidence);
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted("Attaches source-frame and NR configuration evidence to HMD captures. Does not alter the image; useful for attributing comparisons to exact settings.");
		ImGui::TextWrapped("Captures the actual HDR-pass AvgTex.y/x and frame-gamma evidence. A matching source frame is required; capture arriving after early NR is unavailable, not silently taken from the previous frame. Capturing exposure does not identify NR's expected colour space.");
		for (std::size_t i = 0; i < config.experiments.profiles.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			ImGui::Separator();
			ImGui::TextUnformatted(i ? "Final LDR pre-UI" : "Upscaled Centre");
			auto& p = config.experiments.profiles[i];
			int domain = static_cast<int>(p.domain), transform = static_cast<int>(p.transform);
			changed |= ImGui::Combo("Source-domain candidate", &domain, "Unknown/native\0Linear RGB\0sRGB encoded\0");
			if (auto tooltip = Util::HoverTooltipWrapper())
				ImGui::TextUnformatted("Declares how this route's input colour should be interpreted for experiments. Non-identity transforms require Linear RGB; changing away from it restores Identity.");
			p.domain = static_cast<Domain>(domain);
			if (p.domain != Domain::Linear && p.transform != Transform::Identity) {
				p.transform = Transform::Identity;
				changed = true;
			}
			if (p.domain == Domain::Linear) {
				transform = static_cast<int>(p.transform);
				changed |= ImGui::Combo("Model input transform", &transform, "Identity\0Linear to sRGB\0Reversible proxy + sRGB\0");
				if (auto tooltip = Util::HoverTooltipWrapper())
					ImGui::TextUnformatted("Identity leaves input colour unchanged. Linear to sRGB encodes it; Reversible proxy also compresses its range. Experimental transforms are reversed during reconstruction.");
				p.transform = static_cast<Transform>(transform);
			}
			if (p.transform == Transform::Identity) {
				p.exposureMultiplier = 1;
				p.exposureSource = ExposureSource::Manual;
			} else {
				int source = static_cast<int>(p.exposureSource);
				changed |= ImGui::Combo("Exposure source", &source, "Manual calibration\0Captured engine HDR (current frame)\0Captured engine HDR (previous frame)\0");
				if (auto tooltip = Util::HoverTooltipWrapper())
					ImGui::TextUnformatted("Uses manual calibration or captured HDR exposure from the matching current or previous source frame. Previous-frame capture is for early-route experiments; missing evidence cannot supply that correction.");
				p.exposureSource = static_cast<ExposureSource>(source);
				float stops = std::log2(p.exposureMultiplier);
				if (ImGui::SliderFloat("Calibration multiplier (EV)", &stops, -8, 8)) {
					p.exposureMultiplier = std::exp2(stops);
					changed = true;
				}
				if (auto tooltip = Util::HoverTooltipWrapper())
					ImGui::TextUnformatted("Adjusts exposure before the model, then removes it during reconstruction. +1 EV doubles exposure; -1 EV halves it. Multiplies the selected exposure source.");
			}
			ImGui::PopID();
		}
		changed |= ImGui::Checkbox("Transport bypass (skip neural evaluation)", &config.experiments.transportBypass);
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted("Skips model evaluation to inspect the colour transport path. This is a diagnostic bypass, not a valid NR performance result.");
		changed |= ImGui::Checkbox("Bounded asynchronous colour samples", &config.experiments.diagnostics);
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted("Collects small, asynchronous input and output colour measurements for diagnostics. Off stops new samples; this does not change the selected colour mode.");
		if (ImGui::Button("Reset assessment overrides")) {
			config.experiments = {};
			changed = true;
		}
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted("Resets all session-only colour experiments, including calibration, captures and bypasses. Keeps the saved colour mode and preservation sliders.");
		if (ImGui::TreeNode("Colour diagnostics")) {
			const auto text = StatusJson().dump(2);
			ImGui::TextUnformatted(text.c_str());
			ImGui::TreePop();
		}
		static std::string assets;
		if (ImGui::Button("Check installed colour shaders"))
			assets = AssetsJson().dump(2);
		if (auto tooltip = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted("Checks that the required colour shader files are present and reports their identities. This does not compile shaders or verify their rendered output.");
		if (!assets.empty())
			ImGui::TextUnformatted(assets.c_str());
		ImGui::TreePop();
	}
	if (changed && !Registry::Instance().Configure(config.settings, config.experiments, config.revision))
		ImGui::TextWrapped("Settings changed concurrently; retry after the next UI refresh.");
}
void NeuralRenderingFeature::DrawEssentialSettings()
{
	globals::features::upscaling.DrawNeuralRenderingSettings(globals::features::upscaling.GetUpscaleMethod(), true);
}
void NeuralRenderingFeature::DataLoaded()
{
	globals::features::upscaling.SetNeuralRenderingFeatureAvailable(loaded);
	BuildProvenance::InstallDevBench();
	VRRenderScaleDevBenchBridge::RegisterNeuralRenderingTool();
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (auto* host = DevBenchAPI::GetDevBenchInterface001()) {
		static const std::string descriptor = Descriptor().dump();
		host->RegisterTool("communityshaders.nr_color", descriptor.c_str(), &Handler, nullptr);
		logger::info("[NRColor] Registered communityshaders.nr_color v3");
	}
#endif
}
