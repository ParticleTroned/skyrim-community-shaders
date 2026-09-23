#include "Features/Upscaling/NeuralRendering/CaptureEvidence.h"
#include "Features/Upscaling/NeuralRendering/ConfigurationSerialization.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
using namespace NeuralRendering::Color;
using Json = nlohmann::json;
namespace DevBenchAPI
{
	using WriteFn = void (*)(void*, const char*);
}
namespace BuildProvenance
{
	void AttachProducer(Json&) {}
}
namespace logger
{
	template <class... T>
	void warn(T&&...)
	{}
}
namespace globals::features
{
	struct Upscaling
	{
		Json rendering = { { "neuralRenderingMode", 0u } };
		Json GetNeuralRenderingConfiguration() const { return rendering; }
		bool ApplyNeuralRenderingConfiguration(const Json& value, std::string&)
		{
			rendering.update(value);
			return true;
		}
		bool ResetNeuralRenderingConfiguration()
		{
			rendering = { { "neuralRenderingMode", 0u } };
			return true;
		}
	} upscaling;
}
class NeuralRenderingFeature
{
public:
	void LoadSettings(Json&);
	void SaveSettings(Json&);
	void RestoreDefaultSettings();
};
static bool exposureRequested = false;
namespace NeuralRendering::Color
{
	ExposureCapture::ExposureCapture() : state_(nullptr) {}
	ExposureCapture& ExposureCapture::Instance()
	{
		static ExposureCapture capture;
		return capture;
	}
	void ExposureCapture::Request(bool value) noexcept { exposureRequested = value; }
	ExposureEvidenceLookup ExposureCapture::GetEvidence(const ExposureStamp& key) const { return { key }; }
	const char* ExposureBindingName(ExposureBindingState) noexcept { return "test"; }
}
#include "settings_under_test.h"
static unsigned checks = 0;
static void Require(bool value, const char* reason)
{
	++checks;
	if (!value) {
		std::fprintf(stderr, "%s\n", reason);
		std::abort();
	}
}
static Json Call(Json request)
{
	Json result;
	const auto text = request.dump();
	Handler(nullptr, text.c_str(), &result, [](void* sink, const char* response) { *static_cast<Json*>(sink) = Json::parse(response); });
	return result;
}
int main()
{
	NeuralRenderingFeature feature;
	Settings neuralLightingSettings{};
	ReadSettings({ { "mode", "neural_lighting" }, { "appearanceMix", 0.75 }, { "lightingPreservation", 1.0 } }, neuralLightingSettings);
	Require(neuralLightingSettings.mode == Mode::NeuralLighting && SettingsJson(neuralLightingSettings)["mode"] == "neural_lighting",
		"Neural Lighting settings parse and serialize without rewriting Preserve Source controls");
	const auto neuralLightingEffective = ResolveReconstructionSettings(neuralLightingSettings);
	Require(neuralLightingEffective.mode == Mode::PreserveSource && neuralLightingEffective.appearanceMix == 0 &&
				neuralLightingEffective.lightingPreservation == 0 && neuralLightingSettings.appearanceMix == 0.75f &&
				neuralLightingSettings.lightingPreservation == 1,
		"Neural Lighting derives the shared full-tone reconstruction without mutating saved controls");
	auto old = Json{ { "mode", "preserve_source" }, { "detailStrength", 0.75 } };
	feature.LoadSettings(old);
	auto original = Registry::Instance().Snapshot();
	Require(original.settings.lightingPreservation == 1, "old settings load at 100%");
	auto change = Call({ { "action", "configure" }, { "expectedRevision", original.revision }, { "settings", { { "lightingPreservation", 0.5 } } } });
	Require(change["ok"] == true, "slider configure accepted");
	auto half = Registry::Instance().Snapshot();
	Require(half.revision == original.revision + 1 && half.inputEpoch == original.inputEpoch && !exposureRequested,
		"post-inference change advances revision only, without exposure request");
	Require(Call({ { "action", "configure" }, { "settings", { { "lightingPreservation", 0.5 } } } })["revision"] == half.revision,
		"same value is a no-op");
	Require(Call({ { "action", "configure" }, { "expectedRevision", original.revision }, { "settings", { { "lightingPreservation", 0 } } } })["ok"] == false,
		"stale revision rejected");
	Require(Call({ { "action", "configure" }, { "settings", { { "appearanceMix", 0.25 } } } })["settings"]["lightingPreservation"] == 0.5,
		"partial live settings preserve current value");
	for (const Json invalid : { Json(-0.01), Json(1.01), Json(nullptr), Json("0.5"), Json(true), Json(false),
			 Json(std::nextafter(1.0, 2.0)), Json(-std::numeric_limits<double>::min()),
			 Json(std::numeric_limits<float>::infinity()), Json(std::numeric_limits<float>::quiet_NaN()) }) {
		auto before = Registry::Instance().Snapshot();
		Settings copy = before.settings;
		bool rejected = false;
		try {
			ReadSettings({ { "detailStrength", 0 }, { "lightingPreservation", invalid } }, copy);
		} catch (const std::exception&) {
			rejected = true;
		}
		Require(rejected, "parser rejects malformed/nonfinite slider value");
		Require(Call({ { "action", "configure" }, { "settings", { { "detailStrength", 0 }, { "lightingPreservation", invalid } } } })["ok"] == false,
			"malformed multi-field API request rejected");
		const auto after = Registry::Instance().Snapshot();
		Require(before.settings == after.settings && before.revision == after.revision && before.inputEpoch == after.inputEpoch,
			"invalid request applies no partial mutation");
	}
	Json saved;
	feature.SaveSettings(saved);
	feature.RestoreDefaultSettings();
	Require(Registry::Instance().Snapshot().settings == Settings{}, "defaults restore 100%");
	feature.LoadSettings(saved);
	Require(SettingsJson(Registry::Instance().Snapshot().settings) == saved.at("colour"), "nested colour save/load round trip");
	Require(saved.at("schemaVersion") == 1 && saved.at("rendering") == globals::features::upscaling.rendering,
		"independent feature saves rendering and colour together");
	Require(Call({ { "action", "reset_experiments" } })["settings"] == saved.at("colour"), "experiment reset retains ordinary setting");
	Json oldNested = saved;
	oldNested.at("colour").erase("lightingPreservation");
	feature.LoadSettings(oldNested);
	Require(Registry::Instance().Snapshot().settings.lightingPreservation == 1,
		"pre-slider nested colour saves use the compatibility default");
	feature.LoadSettings(saved);
	const auto transaction = Registry::Instance().Snapshot();
	std::array<Work, 4> regions;
	for (unsigned i = 0; i < regions.size(); ++i) {
		Observation observation;
		observation.slot = i < 2 ? i : i + 2;
		observation.insertion = i % 2;
		Latch(regions[i], transaction, observation);
	}
	Require(Call({ { "action", "configure" }, { "settings", { { "lightingPreservation", 0 } } } })["ok"] == true, "later update accepted");
	for (const auto& region : regions) {
		Measurement sample;
		sample.source = region.observation;
		Require(sample.source.lightingPreservation == 0.5f && region.configuration.settings.lightingPreservation == 0.5f,
			"both eyes and all regions retain latched value after registry update");
		Require(ObservationEvidenceJson(sample.source)["lightingPreservation"] == 0.5,
			"delayed observation serializer uses sampled value");
	}
	Require(ConfigurationEvidenceJson(transaction)["settings"]["lightingPreservation"] == 0.5,
		"frozen screenshot configuration agrees with observation");
	MeasurementBatch<Measurement> batch;
	batch.receivedSlotMask = 0x33;
	for (const auto& region : regions) batch.samples[region.observation.slot].source = region.observation;
	const auto json = MeasurementBatchEvidenceJson(batch);
	for (const auto& sample : json["measurements"])
		Require(sample["source"]["lightingPreservation"] == 0.5, "batch companion preserves delayed sample setting");
	for (float preservation : { 0.f, 0.25f, 0.5f, 1.f }) {
		Settings settings;
		settings.lightingPreservation = preservation;
		settings.maximumDetailStops = 2;
		settings.detailStrength = 0.75f;
		for (float residual : { -1.f, 0.f, 0.375f, 1.f })
			Require(std::abs(DetailGain(residual, residual, 1, settings) - std::exp2((1 - preservation) * residual * 0.75f)) < 1e-6f,
				"CPU analytic constant residual reference");
	}
	auto& registry = Registry::Instance();
	auto captureConfig = registry.Snapshot();
	captureConfig.experiments.captureFrameEvidence = false;
	Require(registry.Configure(captureConfig.settings, captureConfig.experiments), "stop capture");
	const auto beforeCapture = registry.CaptureEpoch();
	captureConfig.experiments.captureFrameEvidence = true;
	Require(registry.Configure(captureConfig.settings, captureConfig.experiments), "start capture");
	Require(registry.CaptureEpoch() == beforeCapture + 1, "capture start creates new epoch");
	Require(registry.Configure(captureConfig.settings, captureConfig.experiments), "unchanged capture configure");
	Require(registry.CaptureEpoch() == beforeCapture + 1, "noop configure must retain capture epoch");
	captureConfig.settings.detailStrength = captureConfig.settings.detailStrength == 1.0f ? 0.5f : 1.0f;
	Require(registry.Configure(captureConfig.settings, captureConfig.experiments), "settings edit while capturing");
	Require(registry.CaptureEpoch() == beforeCapture + 1, "settings edit must not restart capture");
	captureConfig.experiments.captureFrameEvidence = false;
	Require(registry.Configure(captureConfig.settings, captureConfig.experiments), "second stop capture");
	Require(registry.CaptureEpoch() == beforeCapture + 1, "stop retains prior identity");
	captureConfig.experiments.captureFrameEvidence = true;
	Require(!registry.Configure(captureConfig.settings, captureConfig.experiments, registry.Snapshot().revision + 1), "stale CAS rejects restart");
	Require(registry.CaptureEpoch() == beforeCapture + 1, "rejected restart must not consume epoch");
	Require(registry.Configure(captureConfig.settings, captureConfig.experiments), "restart capture");
	Require(registry.CaptureEpoch() == beforeCapture + 2, "same-frame restart cannot reuse identity");
	std::printf("Passed %u production settings/registry/evidence checks\n", checks);
}
