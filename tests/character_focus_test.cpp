#include "Features/Upscaling/NeuralRendering/CharacterFocusPolicy.h"

#include <limits>

#define CHECK(...)           \
	do {                     \
		if (!(__VA_ARGS__))  \
			return __LINE__; \
	} while (false)

int main()
{
	using namespace NeuralRendering;
	CharacterFocusMask mask{};
	constexpr CharacterRect central{ 450, 450, 550, 550 };
	constexpr CharacterRect peripheral{ 940, 400, 999, 600 };
	CHECK(CharacterFocusDistance(central, 1000, 1000, 0.75f, mask, 0) == 0.0f);
	CHECK(CharacterFocusDistance(peripheral, 1000, 1000, 0.75f, mask, 0) > 1.08f);
	CHECK(CharacterFocusDistance({ 800, 400, 999, 600 }, 1000, 1000, 0.75f, mask, 0) < 1.0f);
	CHECK(CharacterFocusDistance({ 0, 0, 1000, 1000 }, 1000, 1000, 0.25f, mask, 0) == 0.0f);
	CHECK(std::isinf(CharacterFocusDistance({}, 1000, 1000, 0.75f, mask, 0)));
	CHECK(CharacterFocusDistance(central, 0, 1000, 0.75f, mask, 0) == 0.0f);
	mask.visibleScale = 0.65f;
	mask.offsets[0] = { 0.07f, -0.02f };
	// A 0.75 selection is exactly 0.4875 in full-view units, with the same centre.
	const float boundary = FoveatedCommon::MaskDistanceUV(0.57f + 0.4875f * 0.5f,
		0.48f, mask.visibleScale, mask.horizontalScale, 0.07f, -0.02f, 0.75f);
	CHECK(std::abs(boundary - 1.0f) < 1e-6f);
	const float smallBoundary = FoveatedCommon::MaskDistanceUV(0.57f + 0.1625f * 0.5f,
		0.48f, mask.visibleScale, mask.horizontalScale, 0.07f, -0.02f, 0.25f);
	CHECK(std::abs(smallBoundary - 1.0f) < 1e-6f);
	mask = {};
	mask.offsets[1][0] = 0.15f;
	const float left = CharacterFocusDistance(peripheral, 1000, 1000, 0.75f, mask, 0);
	const float right = CharacterFocusDistance(peripheral, 1000, 1000, 0.75f, mask, 1);
	CHECK(left > 1.0f && right < 1.0f);
	CharacterFocusState state{};
	CHECK(ResolveCharacterFocusFade(100, std::min(left, right), true, false, 3, state) == 0);
	for (unsigned frame = 101; frame <= 120; ++frame)
		CHECK(ResolveCharacterFocusFade(frame, 1.04f, true, false, 3, state) == 0);
	for (unsigned frame = 121; frame <= 123; ++frame)
		CHECK(ResolveCharacterFocusFade(frame, 2.0f, true, false, 3, state) == 0);
	for (unsigned frame = 124; frame <= 131; ++frame) {
		const auto fade = ResolveCharacterFocusFade(frame, 2.0f, true, false, 3, state);
		CHECK(fade == std::min(255u, (frame - 123u) * 32u));
		CHECK(ResolveCharacterFocusFade(frame, 0.0f, true, false, 3, state) == fade);
	}
	CHECK(ResolveCharacterFocusFade(132, 1.04f, true, false, 3, state) == 255);
	CHECK(ResolveCharacterFocusFade(133, 0.99f, true, false, 3, state) == 223);
	CHECK(ResolveCharacterFocusFade(134, 1.04f, true, false, 3, state) == 191);
	CHECK(ResolveCharacterFocusFade(135, 2.0f, true, true, 3, state) == 0);
	CHECK(ResolveCharacterFocusFade(136, 2.0f, false, false, 3, state) == 0);
	CHECK(ResolveCharacterFocusFade(137, std::numeric_limits<float>::quiet_NaN(), true, false, 3, state) == 0);
	state = {};
	CHECK(ResolveCharacterFocusFade(0xfffffffcu, 0.0f, true, false, 0, state) == 0);
	CHECK(ResolveCharacterFocusFade(0u, 2.0f, true, false, 0, state) == 128);
	CHECK(ResolveCharacterFocusFade(4u, 2.0f, true, false, 0, state) == 255);
	return 0;
}
