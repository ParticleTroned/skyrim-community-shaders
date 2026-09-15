#include "Features/Upscaling/NeuralRendering/ExposurePolicy.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace NeuralRendering::Color;
static unsigned checks = 0;
static void Require(bool value)
{
	++checks;
	if (!value) {
		std::fprintf(stderr, "Exposure check %u failed\n", checks);
		std::abort();
	}
}
int main()
{
	Require(ExposureDrawRejection(1, 1, 2) == nullptr);
	Require(ExposureDrawRejection(0, 1, 2) != nullptr);
	Require(ExposureDrawRejection(1, 0, 2) != nullptr);
	Require(ExposureDrawRejection(1, 2, 3) != nullptr);
	Require(ExposureDrawRejection(1, 1, 0) != nullptr);
	Require(ExposureDrawRejection(0, 0, 0) != nullptr);
	for (float average : { 0.125f, 0.5f, 1.0f, 4.0f, 16.0f }) {
		for (float target : { 0.0625f, 0.5f, 1.0f, 8.0f }) {
			const auto value = EvaluateHDRExposure(average, target);
			Require(value.validity == ExposureValidity::Ratio);
			Require(value.ratio == target / average);
			for (auto transform : { Transform::LinearToSRGB, Transform::ReversibleProxy }) {
				Profile requested{ Domain::Linear, transform, 1.0f, ExposureSource::CapturedHDR }, effective;
				Require(Valid(requested));
				Require(ResolveExposureProfile(requested, value, effective));
				Require(effective.exposureSource == ExposureSource::Manual);
				Require(effective.exposureMultiplier == value.ratio);
				RGB source{ 0.1f, 0.05f, 0.01f }, prepared{}, inverse{};
				Require(Forward(source, effective, prepared));
				Require(Inverse(prepared, effective, inverse));
				for (unsigned i = 0; i < 3; ++i) Require(std::abs(inverse[i] - source[i]) < 1e-4f);
				Require(Reconstruct(source, prepared, prepared, effective) == source);
				Require(!Forward(source, requested, prepared));  // Must resolve actual exposure first.
			}
		}
	}
	Profile captured{ Domain::Linear, Transform::ReversibleProxy, 1, ExposureSource::CapturedHDR }, effective;
	for (auto values : { std::array<float, 2>{ 0, 1 }, { 1, 0 }, { 0, 0 } }) {
		auto e = EvaluateHDRExposure(values[0], values[1]);
		Require(e.validity == ExposureValidity::UnitFallback);
		Require(e.ratio == 1);
		Require(!ResolveExposureProfile(captured, e, effective));
	}
	for (float bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
		Require(EvaluateHDRExposure(bad, 1).validity == ExposureValidity::Invalid);
		Require(EvaluateHDRExposure(1, bad).validity == ExposureValidity::Invalid);
	}
	Require(EvaluateHDRExposure(1, 257).validity == ExposureValidity::Invalid);
	Require(EvaluateHDRExposure(257, 1).validity == ExposureValidity::Invalid);
	Require(EvaluateHDRExposure(-1, 1).validity == ExposureValidity::Invalid);
	Require(!MatchesExposure({}, 10, 1));
	ExposureStamp stamp{ 10, 2, 4, false };
	Require(MatchesExposure(stamp, 10, 2));
	Require(!MatchesExposure(stamp, 11, 2));
	Require(!MatchesExposure(stamp, 10, 3));
	stamp.ambiguous = true;
	Require(!MatchesExposure(stamp, 10, 2));
	Configuration a{}, b{};
	b.settings.mode = Mode::Managed;
	b.experiments.profiles[0] = captured;
	Require(NeedsExposureCapture(b));
	Require(ChangesInput(a, b, 0));
	Require(!ChangesInput(a, b, 1));
	a = b;
	b.experiments.applyModelEdit = false;
	Require(!ChangesInput(a, b, 0));
	Require(b.Enabled());
	b.experiments.diagnostics = true;
	Require(!ChangesInput(a, b, 0));
	b.settings.enabled = false;
	Require(ChangesInput(a, b, 0));
	Require(!NeedsExposureCapture(b));
	b.experiments.captureEngineExposure = true;
	Require(NeedsExposureCapture(b));
	b = {};
	Require(!b.Enabled());
	b.experiments.captureEngineExposure = true;
	Require(NeedsExposureCapture(b));
	Require(!b.Enabled());
	Require(!ChangesInput(Configuration{}, b, 0));
	b.experiments.applyModelEdit = false;
	Require(b.Enabled());
	captured.transform = Transform::Identity;
	Require(!Valid(captured));
	std::printf("Passed %u exposure/capture-policy checks\n", checks);
}
