#include "NeuralColor.h"
#include "Upscaling/NeuralRendering/ColorPipeline.h"
#include <imgui.h>
#include <array>
#include <algorithm>
#include <cmath>
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

	template <class T>
	T Parse(const Json& value, const std::array<const char*, 3>& names)
	{
		const auto text = value.get<std::string>();
		for (std::size_t i = 0; i < names.size(); ++i)
			if (text == names[i]) return static_cast<T>(i);
		throw std::invalid_argument("unknown enum value: " + text);
	}
	template <class T>
	const char* Name(T value, const std::array<const char*, 3>& names)
	{
		const auto index = static_cast<std::size_t>(value);
		return index < names.size() ? names[index] : "invalid";
	}
	void Keys(const Json& object, std::initializer_list<std::string_view> allowed)
	{
		if (!object.is_object()) throw std::invalid_argument("expected an object");
		for (const auto& [key, value] : object.items()) {
			(void)value;
			if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
				throw std::invalid_argument("unknown field: " + key);
		}
	}
	float Number(const Json& value)
	{
		if (!value.is_number()) throw std::invalid_argument("expected a number");
		const auto number = value.get<float>();
		if (!Finite(number)) throw std::invalid_argument("expected a finite number");
		return number;
	}
	void ReadSettings(const Json& object, Settings& settings)
	{
		Keys(object, { "schemaVersion", "mode", "detailStrength", "appearanceMix", "maximumDetailStops" });
		if (object.contains("schemaVersion") && (!object.at("schemaVersion").is_number_integer() || object.at("schemaVersion") != 1))
			throw std::invalid_argument("unsupported colour-settings schema");
		if (object.contains("mode")) settings.mode = Parse<Mode>(object.at("mode"), modes);
		if (object.contains("detailStrength")) settings.detailStrength = Number(object.at("detailStrength"));
		if (object.contains("appearanceMix")) settings.appearanceMix = Number(object.at("appearanceMix"));
		if (object.contains("maximumDetailStops")) settings.maximumDetailStops = Number(object.at("maximumDetailStops"));
		if (!Valid(settings)) throw std::invalid_argument("colour settings outside supported ranges");
	}
	Json SettingsJson(const Settings& settings)
	{
		return { { "schemaVersion", 1 }, { "mode", Name(settings.mode, modes) },
			{ "detailStrength", settings.detailStrength }, { "appearanceMix", settings.appearanceMix },
			{ "maximumDetailStops", settings.maximumDetailStops } };
	}
	Json ProfileJson(const Profile& profile)
	{
		return { { "domain", Name(profile.domain, domains) }, { "transform", Name(profile.transform, transforms) },
			{ "exposureMultiplier", profile.exposureMultiplier },
			{ "domainOrigin", profile.domain == Domain::Unknown ? "unknown" : "manual_experiment" },
			{ "exposureOrigin", profile.transform == Transform::Identity ? "not_applied" : "manual_experiment" },
			{ "engineExposureKnown", false }, { "srInternalExposureAvailable", false } };
	}
	void ReadProfile(const Json& object, Profile& profile)
	{
		Keys(object, { "domain", "transform", "exposureMultiplier" });
		if (object.contains("domain")) profile.domain = Parse<Domain>(object.at("domain"), domains);
		if (object.contains("transform")) profile.transform = Parse<Transform>(object.at("transform"), transforms);
		if (object.contains("exposureMultiplier")) profile.exposureMultiplier = Number(object.at("exposureMultiplier"));
		if (!Valid(profile)) throw std::invalid_argument("encoding/proxy requires explicit linear domain; identity requires multiplier one");
	}
	Json ObservationJson(const Observation& observation)
	{
		return { { "frame", observation.frame }, { "sourceWorldFrame", observation.sourceWorldFrame },
			{ "physicalSlot", observation.slot }, { "insertionPoint", observation.insertion },
			{ "generation", observation.generation }, { "revision", observation.revision },
			{ "rect", { observation.rect.baseX, observation.rect.baseY, observation.rect.width, observation.rect.height } },
			{ "sourceFormat", observation.sourceFormat }, { "outputFormat", observation.outputFormat },
			{ "effectiveMode", Name(observation.mode, modes) }, { "profile", ProfileJson(observation.profile) },
			{ "transportBypass", observation.bypass }, { "atomicColourBatch", observation.atomicStereo },
			{ "processed", observation.processed }, { "retainedColourTextureBytes", observation.retainedBytes },
			{ "preparationCpuMicroseconds", observation.preparationCpuMicroseconds },
			{ "reconstructionCpuMicroseconds", observation.reconstructionCpuMicroseconds },
			{ "failure", observation.failure } };
	}
	Json StatusJson()
	{
		const auto config = Registry::Instance().Snapshot();
		const auto status = Registry::Instance().GetStatus();
		Json slots = Json::array(), measurements = Json::array();
		for (const auto& observation : status.slots) if (observation.revision) slots.push_back(ObservationJson(observation));
		for (const auto& measurement : status.measurements) {
			if (!measurement.source.revision) continue;
			measurements.push_back({ { "source", ObservationJson(measurement.source) }, { "values", measurement.data } });
		}
		return { { "apiVersion", 1 }, { "revision", config.revision }, { "settings", SettingsJson(config.settings) },
			{ "experiments", { { "upscaled_center", ProfileJson(config.experiments.profiles[0]) },
				{ "final_ldr_pre_ui", ProfileJson(config.experiments.profiles[1]) },
				{ "transportBypass", config.experiments.transportBypass }, { "diagnostics", config.experiments.diagnostics } } },
			{ "inputEpoch", config.inputEpoch }, { "slots", slots }, { "measurements", measurements },
			{ "counts", { { "prepared", status.prepared }, { "reconstructed", status.reconstructed },
				{ "failed", status.failed }, { "bypassed", status.bypassed }, { "samples", status.samples }, { "dropped", status.dropped } } },
			{ "note", "Profiles are transient assertions, not detected NVIDIA contracts. Changes take effect at the next NR transaction. Additional pass times are CPU enqueue only." } };
	}

#ifdef DEVBENCH_BRIDGE_ENABLED
	void Handler(void*, const char* arguments, void* sink, DevBenchAPI::WriteFn write)
	{
		if (!write) return;
		Json response;
		try {
			const auto request = Json::parse(arguments ? arguments : "{}");
			const auto action = request.at("action").get<std::string>();
			if (action == "status") {
				Keys(request, { "action" });
			} else if (action == "configure" || action == "reset_experiments") {
				Keys(request, { "action", "settings", "experiments", "expectedRevision" });
				auto config = Registry::Instance().Snapshot();
				if (request.contains("expectedRevision")) {
					const auto& expected = request.at("expectedRevision");
					if (!expected.is_number_integer() || expected <= 0 || expected.get<std::uint64_t>() != config.revision)
						throw std::invalid_argument("configuration revision changed or invalid; read status again");
				}
				if (action == "reset_experiments") {
					if (request.contains("settings") || request.contains("experiments"))
						throw std::invalid_argument("reset_experiments does not accept settings");
					config.experiments = {};
				} else {
					if (request.contains("settings")) ReadSettings(request.at("settings"), config.settings);
					if (request.contains("experiments")) {
						const auto& e = request.at("experiments");
						Keys(e, { "upscaled_center", "final_ldr_pre_ui", "transportBypass", "diagnostics" });
						if (e.contains("upscaled_center")) ReadProfile(e.at("upscaled_center"), config.experiments.profiles[0]);
						if (e.contains("final_ldr_pre_ui")) ReadProfile(e.at("final_ldr_pre_ui"), config.experiments.profiles[1]);
						if (e.contains("transportBypass")) config.experiments.transportBypass = e.at("transportBypass").get<bool>();
						if (e.contains("diagnostics")) config.experiments.diagnostics = e.at("diagnostics").get<bool>();
					}
				}
				// Only the thread-safe registry is changed here. No engine or GPU calls
				// execute on a DevBench thread; Renderer captures the next snapshot.
				if (!Registry::Instance().Configure(config.settings, config.experiments, config.revision))
					throw std::invalid_argument("invalid or concurrently changed colour configuration");
			} else throw std::invalid_argument("unknown action");
			response = StatusJson();
		} catch (const std::exception& error) {
			response = { { "error", error.what() } };
		} catch (...) {
			response = { { "error", "colour handler failed" } };
		}
		try { const auto text = response.dump(); write(sink, text.c_str()); }
		catch (...) { write(sink, R"({"error":"colour status serialization failed"})"); }
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
	try { ReadSettings(object, settings); }
	catch (const std::exception& error) {
		logger::warn("[NRColor] Invalid saved settings; using Legacy Raw: {}", error.what());
		settings = {};
	}
	(void)Registry::Instance().Configure(settings, {});
}
void NeuralColor::SaveSettings(nlohmann::json& object)
{
	object = SettingsJson(Registry::Instance().Snapshot().settings);
}
void NeuralColor::RestoreDefaultSettings()
{
	(void)Registry::Instance().Configure({}, {});
}
void NeuralColor::DrawSettings()
{
	auto config = Registry::Instance().Snapshot();
	bool changed = false;
	ImGui::TextWrapped("Shared by standard NR, character ROI and multi-ROI. Legacy Raw preserves the existing path. Other modes are experimental and do not imply a confirmed NVIDIA colour contract.");
	int mode = static_cast<int>(config.settings.mode);
	changed |= ImGui::Combo("Colour processing", &mode, "Legacy Raw\0Managed (experimental)\0Preserve Source (experimental)\0");
	config.settings.mode = static_cast<Mode>(mode);
	if (config.settings.mode == Mode::PreserveSource) {
		changed |= ImGui::SliderFloat("Detail contribution", &config.settings.detailStrength, 0.0f, 2.0f);
		changed |= ImGui::SliderFloat("Neural appearance mix", &config.settings.appearanceMix, 0.0f, 1.0f);
		changed |= ImGui::SliderFloat("Maximum detail gain (stops)", &config.settings.maximumDetailStops, 0.0f, 2.0f);
		ImGui::TextWrapped("Appearance mix includes tone and colour, not only chroma. Zero detail and zero appearance return the source. This is a bounded detail-transfer heuristic, not physical relighting.");
	}
	if (ImGui::TreeNode("Transient colour experiments")) {
		ImGui::TextWrapped("Not saved. Source domain is unknown unless you explicitly assert it here. DLSS internal auto-exposure is not read. Profiles are separate for early and late insertion. Never apply an HDR proxy to tone-mapped LDR.");
		for (std::size_t i = 0; i < config.experiments.profiles.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			ImGui::Separator();
			ImGui::TextUnformatted(i == 0 ? "Upscaled Centre" : "Final LDR pre-UI");
			auto& profile = config.experiments.profiles[i];
			int domain = static_cast<int>(profile.domain), transform = static_cast<int>(profile.transform);
			changed |= ImGui::Combo("Source domain assertion", &domain, "Unknown/native\0Linear RGB\0sRGB encoded\0");
			profile.domain = static_cast<Domain>(domain);
			if (profile.domain != Domain::Linear && profile.transform != Transform::Identity) { profile.transform = Transform::Identity; changed = true; }
			if (profile.domain == Domain::Linear) {
				transform = static_cast<int>(profile.transform);
				changed |= ImGui::Combo("Model input transform", &transform, "Identity\0Linear to sRGB\0Reversible proxy + sRGB\0");
				profile.transform = static_cast<Transform>(transform);
			}
			if (profile.transform == Transform::Identity) profile.exposureMultiplier = 1.0f;
			else {
				float stops = std::log2(profile.exposureMultiplier);
				if (ImGui::SliderFloat("Manual exposure multiplier (EV)", &stops, -8.0f, 8.0f)) {
					profile.exposureMultiplier = std::exp2(stops); changed = true;
				}
			}
			ImGui::PopID();
		}
		changed |= ImGui::Checkbox("Transport bypass (skip neural evaluation)", &config.experiments.transportBypass);
		changed |= ImGui::Checkbox("Bounded asynchronous colour samples", &config.experiments.diagnostics);
		if (ImGui::Button("Reset transient experiments")) { config.experiments = {}; changed = true; }
		ImGui::TreePop();
	}
	if (changed) (void)Registry::Instance().Configure(config.settings, config.experiments, config.revision);
	ImGui::TextWrapped("Colour-enabled stereo uses an atomic batch and private reconstruction staging, including Direct Commit requests. Existing outer menu, character and foveation composition remains unchanged.");
	if (ImGui::TreeNode("Colour diagnostics")) {
		const auto text = StatusJson().dump(2);
		ImGui::TextUnformatted(text.c_str());
		ImGui::TreePop();
	}
}
void NeuralColor::DataLoaded()
{
#ifdef DEVBENCH_BRIDGE_ENABLED
	if (auto* host = DevBenchAPI::GetDevBenchInterface001()) {
		static constexpr const char* descriptor = R"({"description":"Configure shared CSX NR colour processing and inspect frame-attributed asynchronous samples. Profiles and bypass are transient experiments. Changes apply at the next NR transaction; shader compilation and NVIDIA runtime compatibility are not implied.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["status","configure","reset_experiments"]},"settings":{"type":"object"},"experiments":{"type":"object"},"expectedRevision":{"type":"integer","minimum":1}},"required":["action"],"additionalProperties":false}})";
		host->RegisterTool("communityshaders.nr_color", descriptor, &Handler, nullptr);
		logger::info("[NRColor] Registered communityshaders.nr_color");
	}
#endif
}
