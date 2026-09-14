#include "Features/Upscaling/NeuralRendering/ExposurePolicy.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace NeuralRendering::Color;
static unsigned checks;
static void Check(bool ok) { ++checks; if (!ok) { std::fprintf(stderr, "Adversarial policy check %u failed\n", checks); std::abort(); } }
int main()
{
	// No same-sign negative "exposure" is admissible just because its ratio is positive.
	for (auto pair : { std::array<float, 2>{-1, -1}, {-8, -2}, {0, -1}, {-1, 0} })
		Check(EvaluateHDRExposure(pair[0], pair[1]).validity == ExposureValidity::Invalid);
	Check(EvaluateHDRExposure(0, 1).validity == ExposureValidity::UnitFallback);
	Check(EvaluateHDRExposure(2, 4).validity == ExposureValidity::Ratio);
	Profile captured{ Domain::Linear, Transform::LinearToSRGB, 1, ExposureSource::CapturedHDR }, effective;
	for (auto pair : { std::array<float, 2>{-1, -1}, {0, 1}, {1, 0},
		{std::numeric_limits<float>::quiet_NaN(), 1}, {1, std::numeric_limits<float>::quiet_NaN()} })
		Check(!ResolveExposureProfile(captured, {pair[0], pair[1], 1, ExposureValidity::Ratio}, effective));
	Profile encoded{ Domain::Linear, Transform::LinearToSRGB, 1 };
	RGB prepared{}, neural{};
	Check(Forward({0.25f, 0.25f, 0.25f}, encoded, prepared));
	Check(Forward({0.5f, 0.5f, 0.5f}, encoded, neural));
	for (auto invalidBase : { RGB{-1, 0, 1}, RGB{33, 1, 1} })
		Check(Reconstruct(invalidBase, prepared, neural, encoded) == invalidBase);
	Check(Reconstruct({std::numeric_limits<float>::infinity(), 1, 1}, prepared, neural, encoded) == RGB{});
	Check(Representable({65024, 65024, 64512}, Storage::R11G11B10));
	Check(!Representable({65024, 65024, 65024}, Storage::R11G11B10));
	Check(!Representable({-0.1f, 1, 1}, Storage::R11G11B10));
	Check(Representable({-65504, 65504, 0}, Storage::Float16));
	Check(!Representable({65536, 0, 0}, Storage::Float16));
	Check(Representable({0, 1, 0.5f}, Storage::UNorm));
	Check(!Representable({0, 1.01f, 0.5f}, Storage::UNorm));
	for (auto storage : {Storage::Float32, Storage::Float16, Storage::R11G11B10, Storage::UNorm}) {
		Check(!Representable({std::numeric_limits<float>::quiet_NaN(), 0, 0}, storage));
		Check(!Representable({0, std::numeric_limits<float>::infinity(), 0}, storage));
	}
	Check(!Representable({1, 1, 1}, static_cast<Storage>(0x400)));
	// Storage boundaries are per channel, not one guessed HDR maximum.
	for (int i = -16; i <= 16; ++i) {
		float value = 64512.0f + i * 32.0f;
		Check(Representable({value, value, 0}, Storage::R11G11B10) == (value <= 65024));
		Check(Representable({0, 0, value}, Storage::R11G11B10) == (value <= 64512));
	}
	std::printf("Passed %u adversarial policy checks\n", checks);
}
