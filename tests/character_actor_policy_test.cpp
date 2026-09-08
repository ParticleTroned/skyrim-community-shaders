#include "Features/Upscaling/NeuralRendering/CharacterActorPolicy.h"

#include <array>
#include <limits>

int main()
{
	using namespace NeuralRendering;
	std::array<CharacterClipPoint, 4> corners{
		CharacterClipPoint{ -0.5f, -0.5f, 1.0f },
		CharacterClipPoint{ 0.5f, -0.5f, 1.0f },
		CharacterClipPoint{ -0.5f, 0.5f, 1.0f },
		CharacterClipPoint{ 0.5f, 0.5f, 1.0f },
	};
	CharacterRect rect{};
	if (ResolveCharacterProjection(corners, 100, 200, rect) != CharacterProjectionResult::Visible ||
		rect != CharacterRect{ 25, 50, 75, 150 })
		return 1;
	const auto visibleCorners = corners;
	for (auto& corner : corners)
		corner.x += 3.0f;
	if (ResolveCharacterProjection(corners, 100, 200, rect) != CharacterProjectionResult::Offscreen || rect.IsValid())
		return 2;
	// A stereo eye with a known-offscreen bound does not require the full eye.
	CharacterRect otherEye{};
	if (ResolveCharacterProjection(visibleCorners, 100, 200, otherEye) != CharacterProjectionResult::Visible ||
		rect.IsValid())
		return 3;
	corners = visibleCorners;
	for (auto& corner : corners)
		corner.w = -1.0f;
	if (ResolveCharacterProjection(corners, 100, 200, rect) != CharacterProjectionResult::Offscreen)
		return 4;
	corners[0].w = 1.0f;
	if (ResolveCharacterProjection(corners, 100, 200, rect) != CharacterProjectionResult::Uncertain ||
		rect != CharacterRect{ 0, 0, 100, 200 })
		return 5;
	corners = visibleCorners;
	corners[0].x = std::numeric_limits<float>::quiet_NaN();
	if (ResolveCharacterProjection(corners, 100, 200, rect) != CharacterProjectionResult::Uncertain || !rect.IsValid())
		return 6;
	corners = visibleCorners;
	for (auto& corner : corners)
		corner.w = 1.0e-6f;
	if (ResolveCharacterProjection(corners, 100, 200, rect) != CharacterProjectionResult::Uncertain)
		return 7;

	CharacterActorAdmissionState actorA{};
	CharacterActorAdmissionState actorB{};
	if (ResolveCharacterActorAdmission(100, 63, 2.0f, false, 64, 3, false, actorA) ||
		!ResolveCharacterActorAdmission(101, 64, 2.0f, false, 64, 3, false, actorA) ||
		!ResolveCharacterActorAdmission(102, 48, 2.0f, false, 64, 3, false, actorA))
		return 8;
	// Separate actors cannot borrow the admitted actor's size hysteresis.
	if (ResolveCharacterActorAdmission(102, 48, 2.0f, false, 64, 3, false, actorB))
		return 9;
	if (!ResolveCharacterActorAdmission(105, 47, 2.0f, false, 64, 3, false, actorA) ||
		ResolveCharacterActorAdmission(106, 47, 2.0f, false, 64, 3, false, actorA))
		return 10;
	actorA = {};
	if (ResolveCharacterActorAdmission(1, 95, 10.0f, false, 1, 0, true, actorA) ||
		!ResolveCharacterActorAdmission(2, 96, 10.0f, false, 1, 0, true, actorA) ||
		!ResolveCharacterActorAdmission(3, 72, 10.0f, false, 1, 0, true, actorA) ||
		ResolveCharacterActorAdmission(4, 71, 10.0f, false, 1, 0, true, actorA))
		return 11;
	actorA = {};
	if (!ResolveCharacterActorAdmission(1, 20, 8.0f, false, 1, 0, true, actorA) ||
		!ResolveCharacterActorAdmission(2, 20, 9.0f, false, 1, 0, true, actorA) ||
		ResolveCharacterActorAdmission(3, 20, 9.01f, false, 1, 0, true, actorA))
		return 12;
	actorA = {};
	if (!ResolveCharacterActorAdmission(0xFFFFFFFEu, 64, 2.0f, false, 64, 2, false, actorA) ||
		!ResolveCharacterActorAdmission(0, 1, 2.0f, false, 64, 2, false, actorA) ||
		ResolveCharacterActorAdmission(1, 1, 2.0f, false, 64, 2, false, actorA))
		return 13;
	actorA = {};
	if (!ResolveCharacterActorAdmission(1, 0, 0.0f, true, 4096, 0, true, actorA))
		return 14;
	return 0;
}
