#include "Features/Upscaling/NeuralRendering/FaceDetailPolicy.h"
#include "Features/Upscaling/NeuralRendering/MonoExperiments.h"
#include "Features/Upscaling/NeuralRendering/MonoSettingsSerialization.h"
#include <cmath>
#include <stdexcept>

using namespace NeuralRendering;
static void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}

int main()
{
	MonoSettings settings;
	Require(ApplyMonoSettingsPatch({ { "guidedConfidenceRejectionPercent", 65u } }, settings).empty(), "confidence setting was rejected");
	Require(settings.guidedConfidenceRejectionPercent == 65 && nlohmann::json(settings).at("guidedConfidenceRejectionPercent") == 65, "confidence did not round trip");
	const auto previous = settings;
	for (const auto& invalid : { nlohmann::json(-1), nlohmann::json(101), nlohmann::json(1.5), nlohmann::json(true) }) {
		Require(!ApplyMonoSettingsPatch({ { "guidedConfidenceRejectionPercent", invalid } }, settings).empty(), "invalid confidence was admitted");
		Require(settings == previous, "rejected configuration partially changed settings");
	}
	Require(!ApplyMonoSettingsPatch({ { "nvidiaReferenceColor", true } }, settings).empty(), "session experiment entered persisted settings");
#ifdef DEVBENCH_BRIDGE_ENABLED
	MonoExperiments experiments;
	Require(ApplyMonoExperimentsPatch({ { "faceDetailProtectionPercent", 100u }, { "footprintSubdivisions", 4u }, { "nativeJitter", true } }, experiments).empty(), "valid experiment rejected");
	const auto original = experiments;
	for (const auto& invalid : { nlohmann::json{ { "footprintSubdivisions", 3 } }, nlohmann::json{ { "faceDetailProtectionPercent", 101 } }, nlohmann::json{ { "nativeJitter", 1 } }, nlohmann::json{ { "unknown", true } } }) {
		Require(!ApplyMonoExperimentsPatch(invalid, experiments).empty() && experiments == original, "invalid experiment partially applied");
	}
	FaceDetailRegions faces;
	faces.current = true;
	faces.count = 1;
	faces.bounds[0] = { .25f, .25f, .40f, .50f };
	auto plan = BuildMonoCompressionPlan({ 1920, 1080 }, { 1280, 720 }, 100, 50, false, false);
	const auto extent = plan.model;
	ApplyFaceDetailProtection(plan, faces, 100, .5f);
	Require(plan.mapping.faceDetail.enabled && plan.model == extent, "face protection changed native capacity or did not redistribute it");
	for (unsigned y = 0; y <= 60; ++y)
		for (unsigned x = 0; x <= 60; ++x) {
			const PeripheralMapping::Point source{ x / 60.f, y / 60.f };
			const auto target = plan.mapping.Forward(source), inverse = plan.mapping.Inverse(target);
			for (unsigned axis = 0; axis < 2; ++axis)
				Require(std::abs(source[axis] - inverse[axis]) < 3e-6f, "face mapping lost invertibility");
		}
	for (unsigned y = 0; y < 10; ++y)
		for (unsigned x = 0; x < 10; ++x) {
			const PeripheralMapping::Point lower{ x / 10.f, y / 10.f }, upper{ (x + 1) / 10.f, (y + 1) / 10.f };
			const auto bounds = plan.mapping.SourceBounds(lower, upper);
			for (unsigned tap = 0; tap <= 16; ++tap) {
				const auto source = plan.mapping.Inverse({ lower[0] + (upper[0] - lower[0]) * (tap % 5) / 4.f, lower[1] + (upper[1] - lower[1]) * (tap / 5) / 4.f });
				for (unsigned axis = 0; axis < 2; ++axis)
					Require(source[axis] >= bounds.first[axis] - 1e-6f && source[axis] <= bounds.second[axis] + 1e-6f, "face footprint escaped conservative bounds");
			}
		}
	auto stale = BuildMonoCompressionPlan({ 1920, 1080 }, { 1280, 720 }, 100, 50, false, false);
	faces.current = false;
	ApplyFaceDetailProtection(stale, faces, 100, .5f);
	Require(!stale.mapping.faceDetail.enabled, "stale face observations changed the current frame");
#endif
}
