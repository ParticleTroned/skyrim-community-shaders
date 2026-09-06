#include "Features/Upscaling/NeuralRendering/ComputeSubrect.h"
#include "Features/Upscaling/NeuralRendering/CharacterComputeSubrect.h"

#include <array>
#include <limits>

int main()
{
	using NeuralRendering::BuildCenteredComputeSubrect;
	using NeuralRendering::BuildCharacterComputeSubrect;
	using NeuralRendering::ComputeSubrect;
	using NeuralRendering::MapComputeSubrect;

	static_assert(!ComputeSubrect{}.IsValid());
	static_assert(!ComputeSubrect{}.Fits(100, 100));
	static_assert(ComputeSubrect{ 90, 80, 10, 20 }.Fits(100, 100));
	static_assert(!ComputeSubrect{ 90, 80, 11, 20 }.Fits(100, 100));
	static_assert(ComputeSubrect{ 2, 3, 4, 5 }.Area() == 20);

	static_assert(
		MapComputeSubrect({ 1, 1, 1, 1 }, 3, 3, 10, 10) ==
		ComputeSubrect{ 3, 3, 4, 4 });
	static_assert(
		MapComputeSubrect({ 7, 3, 3, 2 }, 10, 5, 3, 2) ==
		ComputeSubrect{ 2, 1, 1, 1 });
	static_assert(
		MapComputeSubrect({ 0, 0, 10, 5 }, 10, 5, 3, 2) ==
		ComputeSubrect{ 0, 0, 3, 2 });
	static_assert(!MapComputeSubrect({}, 10, 10, 20, 20).IsValid());
	static_assert(!MapComputeSubrect({ 0, 0, 1, 1 }, 0, 1, 1, 1).IsValid());

	constexpr std::array characterRegions{
		NeuralRendering::CharacterRect{ 13, 17, 29, 35 },
		NeuralRendering::CharacterRect{ 80, 70, 95, 99 },
	};
	if (BuildCharacterComputeSubrect(characterRegions, 100, 100) !=
		ComputeSubrect{ 8, 8, 92, 92 }) {
		return 1;
	}
	constexpr std::array fullRegion{
		NeuralRendering::CharacterRect{ 0, 0, 1512, 1680 },
	};
	if (BuildCharacterComputeSubrect(fullRegion, 1512, 1680) !=
		ComputeSubrect{ 0, 0, 1512, 1680 }) {
		return 2;
	}
	constexpr std::array invalidRegion{
		NeuralRendering::CharacterRect{ 90, 90, 101, 100 },
	};
	if (BuildCharacterComputeSubrect(invalidRegion, 100, 100).IsValid())
		return 3;

	if (BuildCenteredComputeSubrect(1512, 1680, 0.5f) !=
		ComputeSubrect{ 378, 420, 756, 840 }) {
		return 4;
	}
	if (BuildCenteredComputeSubrect(3, 3, 0.25f) !=
		ComputeSubrect{ 1, 1, 1, 1 }) {
		return 5;
	}
	if (BuildCenteredComputeSubrect(
			100, 80, std::numeric_limits<float>::quiet_NaN()) !=
		ComputeSubrect{ 0, 0, 100, 80 }) {
		return 6;
	}
	if (BuildCenteredComputeSubrect(0, 80, 1.0f).IsValid())
		return 7;
	return 0;
}
