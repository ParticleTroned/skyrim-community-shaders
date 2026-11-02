#include "Features/Upscaling/NeuralRendering/ColorPolicy.h"
#include <limits>
#include <stdexcept>

using namespace NeuralRendering::Color;

static void Require(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}

int main()
{
	Profile profile{ Domain::Linear, Transform::NvidiaReference, 1.0f };
#ifndef DEVBENCH_BRIDGE_ENABLED
	Require(!Valid(profile), "Production accepted an experimental reference colour transform");
	return 0;
#else
	for (const float exposure : { 1.0f / 256.0f, 0.125f, 1.0f, 4.0f, 256.0f }) {
		profile.exposureMultiplier = exposure;
		for (const float value : { 0.0f, 0.001f, 0.18f, 1.0f, 8.0f, 65504.0f }) {
			const RGB source{ value, value * 0.5f, value * 0.125f };
			RGB prepared{}, inverse{};
			Require(Forward(source, profile, prepared), "Reference SDR preparation failed");
			Require(Representable(prepared, Storage::UNorm), "Reference input escaped SDR bounds");
			Require(Reconstruct(source, prepared, prepared, profile) == source, "Unchanged model output altered source HDR");
			Require(!Inverse(prepared, profile, inverse), "A clipped SDR curve was advertised as an invertible codec");
		}
	}
	profile.exposureMultiplier = 1.0f;
	const RGB skin{ 0.18f, 0.12f, 0.08f };
	RGB prepared{};
	Require(Forward(skin, profile, prepared), "Skin preparation failed");
	auto edited = prepared;
	edited[0] += 0.025f;
	const auto restored = Reconstruct(skin, prepared, edited, profile);
	Require(Finite(restored) && restored[0] > skin[0], "Reference reconstruction discarded all neural detail");
	const RGB highlight{ 32.0f, 16.0f, 8.0f };
	Require(Forward(highlight, profile, prepared), "Highlight preparation failed");
	Require(Reconstruct(highlight, prepared, RGB{}, profile) == highlight, "Highlight recovery did not preserve original HDR");
	Require(Reconstruct(skin, prepared, { std::numeric_limits<float>::infinity(), 0, 0 }, profile) == skin,
		"Invalid neural output escaped the source fallback");
	profile.domain = Domain::Unknown;
	Require(!Valid(profile), "Reference processing guessed an unknown source domain");
	Configuration original{}, changed{};
	changed.experiments.nativeControlMask = true;
	Require(!changed.Enabled() && ChangesInput(original, changed, 0), "Mask comparison enabled colour passes or missed its history boundary");
	changed = original;
	changed.experiments.nativeJitter = true;
	Require(!changed.Enabled() && ChangesInput(original, changed, 0), "Jitter comparison enabled colour passes or missed its history boundary");
#endif
}
