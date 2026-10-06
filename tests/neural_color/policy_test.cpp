#include "Features/Upscaling/NeuralRendering/ColorPolicy.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace NeuralRendering::Color;
static unsigned checks = 0;
static void Require(bool value)
{
	++checks;
	if (!value) {
		std::fprintf(stderr, "Failed check %u\n", checks);
		std::abort();
	}
}
int main()
{
	Require(Valid(Settings{}));
	Require(Valid(Experiments{}));
	Settings s{};
	s.detailStrength = std::numeric_limits<float>::quiet_NaN();
	Require(!Valid(s));
	s = {};
	s.appearanceMix = 1.01f;
	Require(!Valid(s));
	Profile p{};
	p.transform = Transform::ReversibleProxy;
	Require(!Valid(p));
	p.domain = Domain::Linear;
	Require(Valid(p));
	for (auto transform : { Transform::Identity, Transform::LinearToSRGB, Transform::ReversibleProxy }) {
		p.transform = transform;
		for (float exposure : { 0.0625f, 1.0f, 4.0f }) {
			p.exposureMultiplier = transform == Transform::Identity ? 1.0f : exposure;
			for (float x : { 0.0f, 0.00001f, 0.003f, 0.04f, 0.18f, 0.5f, 1.0f, 4.0f }) {
				RGB b{ x, x * 0.5f, x * 0.125f }, prepared{}, inverse{};
				Require(Forward(b, p, prepared));
				Require(Inverse(prepared, p, inverse));
				for (unsigned i = 0; i < 3; ++i)
					Require(std::abs(inverse[i] - b[i]) < 0.0001f * (1.0f + b[i]));
				Require(Reconstruct(b, prepared, prepared, p) == b);
			}
		}
	}
	p = { Domain::Linear, Transform::ReversibleProxy, 1.0f };
	RGB output{};
	Require(!Forward({ -1.0f, 0.0f, 1.0f }, p, output));
	Require(!Forward({ 33.0f, 0.0f, 1.0f }, p, output));
	Require(!Inverse({ 1.0f, 0.0f, 0.0f }, p, output));
	Require(!Inverse({ std::numeric_limits<float>::infinity(), 0.0f, 0.0f }, p, output));
	s = {};
	Require(DetailGain(2.0f, 2.0f, 1.0f, s) == 1.0f);
	Require(DetailGain(1.0f, 0.0f, 0.0f, s) == 1.0f);
	s.detailStrength = 0.0f;
	Require(DetailGain(2.0f, 0.0f, 1.0f, s) == 1.0f);
	s.detailStrength = 2.0f;
	Require(DetailGain(8.0f, 0.0f, 1.0f, s) <= 2.0f);
	s = {};
	s.mode = Mode::NeuralLighting;
	s.appearanceMix = 0.75f;
	s.lightingPreservation = 1.0f;
	const auto neuralLighting = ResolveReconstructionSettings(s);
	Require(neuralLighting.mode == Mode::PreserveSource);
	Require(neuralLighting.appearanceMix == 0.0f && neuralLighting.lightingPreservation == 0.0f);
	Require(neuralLighting.detailStrength == s.detailStrength && neuralLighting.maximumDetailStops == s.maximumDetailStops);
	Require(DetailGain(0.5f, 0.5f, 1.0f, s) == std::exp2(0.5f));
	s.enabled = false;
	Require(ResolveReconstructionSettings(s).mode == Mode::LegacyRaw);
	Configuration a{}, b{};
	b.experiments.captureFrameEvidence = true;
	Require(!b.Enabled());
	Require(!ChangesInput(a, b, 0));
	Require(!ChangesInput(a, b, 1));
	b.settings.mode = Mode::PreserveSource;
	Require(!ChangesInput(a, b, 0));
	b.experiments.profiles[0] = p;
	Require(ChangesInput(a, b, 0));
	Require(!ChangesInput(a, b, 1));
	a = b;
	b.settings.detailStrength = 0.2f;
	Require(!ChangesInput(a, b, 0));
	b.experiments.transportBypass = true;
	Require(ChangesInput(a, b, 0));
	Require(ChangesInput(a, b, 1));
	std::printf("Passed %u colour-policy checks\n", checks);
}
