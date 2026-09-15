#include "NeuralColor.h"
#include "BuildProvenance.h"
#include "Upscaling/NeuralRendering/ColorPipeline.h"
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
	constexpr std::array<const char*, 3> modes{ "legacy_raw", "managed", "preserve_source" };
	constexpr std::array<const char*, 3> domains{ "unknown", "linear", "srgb" };
	constexpr std::array<const char*, 3> transforms{ "identity", "linear_to_srgb", "reversible_proxy" };
	constexpr std::array<const char*, 2> exposureSources{ "manual", "captured_hdr" };

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
		Keys(object, { "schemaVersion", "enabled", "mode", "detailStrength", "appearanceMix", "maximumDetailStops" });
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
		if (!Valid(settings))
			throw std::invalid_argument("colour settings outside supported ranges");
	}
	Json SettingsJson(const Settings& settings)
	{
		return { { "schemaVersion", 1 }, { "enabled", settings.enabled }, { "mode", Name(settings.mode, modes) },
			{ "detailStrength", settings.detailStrength }, { "appearanceMix", settings.appearanceMix },
			{ "maximumDetailStops", settings.maximumDetailStops } };
	}
	Json ProfileJson(const Profile& profile)
	{
		return { { "domain", Name(profile.domain, domains) }, { "transform", Name(profile.transform, transforms) },
			{ "exposureMultiplier", profile.exposureMultiplier }, { "exposureSource", Name(profile.exposureSource, exposureSources) },
			{ "domainOrigin", profile.domain == Domain::Unknown ? "unknown" : "explicit_candidate" },
			{ "exposureOrigin", profile.exposureSource == ExposureSource::CapturedHDR ? "frame_matched_engine_hdr_capture_required" :
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
		return { { "frame", e.stamp.frame }, { "epoch", e.stamp.epoch }, { "sequence", e.stamp.sequence },
			{ "ambiguous", e.stamp.ambiguous }, { "sourceFormat", e.sourceFormat }, { "sourceViewFormat", e.sourceViewFormat },
			{ "outputViewFormat", e.outputViewFormat }, { "sourceIdentity", e.sourceIdentity }, { "shaderIdentity", e.shaderIdentity },
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
			{ "lastReason", capture.lastReason }, { "samples", samples },
			{ "captureBoundary", "after_SetDirtyStates_before_HDR_draw" },
			{ "lastBinding", { { "frame", b.frame }, { "width", b.width }, { "height", b.height },
								 { "mip", b.mip }, { "mipLevels", b.mipLevels }, { "arraySize", b.arraySize }, { "samples", b.samples },
								 { "sourceFormat", b.sourceFormat }, { "viewFormat", b.viewFormat }, { "viewDimension", b.viewDimension },
								 { "sourceIdentity", b.sourceIdentity }, { "shaderIdentity", b.shaderIdentity } } },
			{ "formula", "ISHDR BLEND AvgTex.y / AvgTex.x; zero input has a distinct unmeasured unit fallback" } };
	}
	Json ObservationJson(const Observation& o)
	{
		return { { "frame", o.frame }, { "sourceWorldFrame", o.sourceWorldFrame }, { "physicalSlot", o.slot },
			{ "insertionPoint", o.insertion }, { "generation", o.generation }, { "revision", o.revision },
			{ "rect", { o.rect.baseX, o.rect.baseY, o.rect.width, o.rect.height } },
			{ "sourceFormat", o.sourceFormat }, { "outputFormat", o.outputFormat }, { "effectiveMode", Name(o.mode, modes) },
			{ "profile", ProfileJson(o.profile) }, { "transportBypass", o.bypass }, { "modelEditShown", o.modelEditShown },
			{ "atomicColourBatch", o.atomicStereo }, { "processed", o.processed }, { "retainedColourTextureBytes", o.retainedBytes },
			{ "preparationCpuMicroseconds", o.preparationCpuMicroseconds }, { "reconstructionCpuMicroseconds", o.reconstructionCpuMicroseconds },
			{ "exposureBinding", ExposureBindingName(o.exposureState) }, { "exposure", EvidenceJson(o.exposure) }, { "failure", o.failure } };
	}
	Json StatusJson()
	{
		const auto config = Registry::Instance().Snapshot();
		const auto status = Registry::Instance().GetStatus();
		Json slots = Json::array(), measurements = Json::array();
		for (const auto& o : status.slots)
			if (o.revision)
				slots.push_back(ObservationJson(o));
		for (const auto& m : status.measurements) {
			if (!m.source.revision)
				continue;
			measurements.push_back({ { "source", ObservationJson(m.source) }, { "values", m.data }, { "measurementVersion", 2 },
				{ "invalidForwardSamples", m.data[16] }, { "invalidInverseSamples", m.data[17] },
				{ "effectiveExposure", m.data[18] }, { "effectiveExposureValid", m.data[19] == 1.0f },
				{ "capturedAverage", m.data[20] }, { "capturedTarget", m.data[21] }, { "capturedRatio", m.data[22] },
				{ "capturedRatioValid", m.source.exposureState == ExposureBindingState::SnapshotQueued && m.data[23] == 1.0f } });
		}
		return { { "ok", true }, { "apiVersion", 2 }, { "revision", config.revision }, { "settings", SettingsJson(config.settings) },
			{ "effectiveMode", Name(config.EffectiveMode(), modes) },
			{ "experiments", { { "upscaled_center", ProfileJson(config.experiments.profiles[0]) },
								 { "final_ldr_pre_ui", ProfileJson(config.experiments.profiles[1]) },
								 { "transportBypass", config.experiments.transportBypass }, { "diagnostics", config.experiments.diagnostics },
								 { "captureEngineExposure", config.experiments.captureEngineExposure }, { "applyModelEdit", config.experiments.applyModelEdit } } },
			{ "inputEpoch", config.inputEpoch }, { "slots", slots }, { "measurements", measurements }, { "engineCapture", CaptureJson() },
			{ "counts", { { "prepared", status.prepared }, { "reconstructed", status.reconstructed }, { "failed", status.failed },
							{ "bypassed", status.bypassed }, { "samples", status.samples }, { "dropped", status.dropped } } },
			{ "note", "Configuration acceptance is not render success. Require fresh matching-eye measurements. Capture is engine evidence, not a verified NR colour-space contract. CPU enqueue times are not GPU timings." } };
	}
	Json AssetsJson()
	{
		const std::array<const char*, 6> paths{
			"Data/Shaders/Features/NeuralColor.ini",
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
						Keys(e, { "upscaled_center", "final_ldr_pre_ui", "transportBypass", "diagnostics", "captureEngineExposure", "applyModelEdit" });
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
  "description": "NR colour v2: shared live controls, display-only A/B, frame-matched engine HDR exposure capture and bounded asynchronous measurements. status reports registered HDR producers and the last draw-boundary binding, including rejected texture dimensions, mip and formats. Capture alone does not enable colour reconstruction. configure/reset change only the thread-safe registry. assets checks installed presence, not compilation. No NVIDIA ABI assumptions or game/profile mutations.",
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
          "assets"
        ]
      },
      "expectedRevision": {
        "type": "integer",
        "minimum": 1
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
            "enum": [
              "legacy_raw",
              "managed",
              "preserve_source"
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
                  "captured_hdr"
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
                  "captured_hdr"
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

NeuralColor& NeuralColor::Instance()
{
	static NeuralColor instance;
	return instance;
}
void NeuralColor::LoadSettings(nlohmann::json& object)
{
	Settings settings;
	try {
		ReadSettings(object, settings);
	} catch (const std::exception& error) {
		logger::warn("[NRColor] Invalid saved settings; using Legacy Raw: {}", error.what());
		settings = {};
	}
	(void)Registry::Instance().Configure(settings, {});
}
void NeuralColor::SaveSettings(nlohmann::json& object) { object = SettingsJson(Registry::Instance().Snapshot().settings); }
void NeuralColor::RestoreDefaultSettings() { (void)Registry::Instance().Configure({}, {}); }
void NeuralColor::EarlyPrepass() { ExposureCapture::Instance().RefreshProducers(); }
void NeuralColor::DrawSettings()
{
	auto config = Registry::Instance().Snapshot();
	bool changed = false;
	ImGui::TextWrapped("Shared by standard NR, character ROI and multi-ROI. No configuration INI editing is needed. Use Save Settings for ordinary controls; assessment overrides are transient.");
	changed |= ImGui::Checkbox("Enable colour processing", &config.settings.enabled);
	int mode = static_cast<int>(config.settings.mode);
	changed |= ImGui::Combo("Colour processing", &mode, "Legacy Raw\0Managed (experimental)\0Preserve Source (experimental)\0");
	config.settings.mode = static_cast<Mode>(mode);
	changed |= ImGui::Checkbox("Apply neural edit (A/B; inference stays running)", &config.experiments.applyModelEdit);
	ImGui::TextWrapped("Uncheck Apply neural edit to show the untouched source without changing model input/history or skipping inference. This is not an NR-off performance measurement.");
	if (config.settings.mode == Mode::PreserveSource) {
		changed |= ImGui::SliderFloat("Detail contribution", &config.settings.detailStrength, 0, 2);
		changed |= ImGui::SliderFloat("Neural appearance mix", &config.settings.appearanceMix, 0, 1);
		changed |= ImGui::SliderFloat("Maximum detail gain (stops)", &config.settings.maximumDetailStops, 0, 2);
	}
	if (ImGui::TreeNode("Exposure capture and colour assessment")) {
		changed |= ImGui::Checkbox("Capture engine HDR exposure", &config.experiments.captureEngineExposure);
		ImGui::TextWrapped("Captures the actual HDR-pass AvgTex.y/x and frame-gamma evidence. A matching source frame is required; capture arriving after early NR is unavailable, not silently taken from the previous frame. Capturing exposure does not identify NR's expected colour space.");
		for (std::size_t i = 0; i < config.experiments.profiles.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			ImGui::Separator();
			ImGui::TextUnformatted(i ? "Final LDR pre-UI" : "Upscaled Centre");
			auto& p = config.experiments.profiles[i];
			int domain = static_cast<int>(p.domain), transform = static_cast<int>(p.transform);
			changed |= ImGui::Combo("Source-domain candidate", &domain, "Unknown/native\0Linear RGB\0sRGB encoded\0");
			p.domain = static_cast<Domain>(domain);
			if (p.domain != Domain::Linear && p.transform != Transform::Identity) {
				p.transform = Transform::Identity;
				changed = true;
			}
			if (p.domain == Domain::Linear) {
				transform = static_cast<int>(p.transform);
				changed |= ImGui::Combo("Model input transform", &transform, "Identity\0Linear to sRGB\0Reversible proxy + sRGB\0");
				p.transform = static_cast<Transform>(transform);
			}
			if (p.transform == Transform::Identity) {
				p.exposureMultiplier = 1;
				p.exposureSource = ExposureSource::Manual;
			} else {
				int source = static_cast<int>(p.exposureSource);
				changed |= ImGui::Combo("Exposure source", &source, "Manual calibration\0Captured engine HDR (frame matched)\0");
				p.exposureSource = static_cast<ExposureSource>(source);
				float stops = std::log2(p.exposureMultiplier);
				if (ImGui::SliderFloat("Calibration multiplier (EV)", &stops, -8, 8)) {
					p.exposureMultiplier = std::exp2(stops);
					changed = true;
				}
			}
			ImGui::PopID();
		}
		changed |= ImGui::Checkbox("Transport bypass (skip neural evaluation)", &config.experiments.transportBypass);
		changed |= ImGui::Checkbox("Bounded asynchronous colour samples", &config.experiments.diagnostics);
		if (ImGui::Button("Reset assessment overrides")) {
			config.experiments = {};
			changed = true;
		}
		ImGui::TreePop();
	}
	if (changed && !Registry::Instance().Configure(config.settings, config.experiments, config.revision))
		ImGui::TextWrapped("Settings changed concurrently; retry after the next UI refresh.");
	if (ImGui::TreeNode("Colour diagnostics")) {
		const auto text = StatusJson().dump(2);
		ImGui::TextUnformatted(text.c_str());
		ImGui::TreePop();
	}
	static std::string assets;
	if (ImGui::Button("Check installed colour shaders"))
		assets = AssetsJson().dump(2);
	if (!assets.empty())
		ImGui::TextUnformatted(assets.c_str());
}
void NeuralColor::DataLoaded()
{
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (auto* host = DevBenchAPI::GetDevBenchInterface001()) {
		static const std::string descriptor = Descriptor().dump();
		host->RegisterTool("communityshaders.nr_color", descriptor.c_str(), &Handler, nullptr);
		logger::info("[NRColor] Registered communityshaders.nr_color v2");
	}
#endif
}
