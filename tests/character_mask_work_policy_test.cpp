#include "Features/Upscaling/NeuralRendering/CharacterMaskWorkPolicy.h"

#include <limits>

int main()
{
	using namespace NeuralRendering;
	using enum CharacterDepthExtentPolicy;
	static_assert(!UseGpuCharacterMaskSupport({}, 1024, 1024));
	static_assert(!UseGpuCharacterMaskSupport({ 0, 0, 255, 256 }, 256, 256));
	static_assert(UseGpuCharacterMaskSupport({ 0, 0, 256, 256 }, 256, 256));
	static_assert(!UseGpuCharacterMaskSupport({ 100, 100, 256, 256 }, 1024, 1024));
	static_assert(!UseGpuCharacterMaskSupport({ 1000, 1000, 256, 256 }, 1024, 1024));
	static_assert(UseGpuCharacterMaskSupport({ 0, 0, UINT32_MAX, UINT32_MAX }, UINT32_MAX, UINT32_MAX));
	static_assert(IsCharacterDepthExtentValid(1920, 1080, 1280, 720, ContainsActiveInput));
	static_assert(IsCharacterDepthExtentValid(1280, 720, 1280, 720, ContainsActiveInput));
	static_assert(!IsCharacterDepthExtentValid(1920, 1080, 1280, 720, ExactCapture));
	static_assert(IsCharacterDepthExtentValid(1280, 720, 1280, 720, ExactCapture));
	static_assert(!IsCharacterDepthExtentValid(1279, 1080, 1280, 720, ContainsActiveInput));
	static_assert(!IsCharacterDepthExtentValid(1920, 719, 1280, 720, ContainsActiveInput));
	static_assert(!IsCharacterDepthExtentValid(1920, 1080, 0, 720, ContainsActiveInput));
	static_assert(!IsCharacterDepthExtentValid(1920, 1080, 1280, 0, ExactCapture));
	static_assert(UnionCharacterWorkRects({}, {}) == ComputeSubrect{});
	static_assert(UnionCharacterWorkRects({}, { 3, 5, 7, 9 }) ==
				  ComputeSubrect{ 3, 5, 7, 9 });
	static_assert(UnionCharacterWorkRects({ 3, 5, 7, 9 }, {}) ==
				  ComputeSubrect{ 3, 5, 7, 9 });
	static_assert(UnionCharacterWorkRects({ 3, 5, 7, 9 }, { 12, 1, 8, 7 }) ==
				  ComputeSubrect{ 3, 1, 17, 13 });
	static_assert(!UnionCharacterWorkRects(
		{ std::numeric_limits<std::uint32_t>::max(), 1, 1, 1 },
		{ 0, 0, 1, 1 })
			.IsValid());
	static_assert(ExpandCharacterWorkRect({ 20, 30, 10, 15 }, 100, 100, 4) ==
				  ComputeSubrect{ 16, 26, 18, 23 });
	static_assert(ExpandCharacterWorkRect({ 1, 2, 98, 97 }, 100, 100, 4) ==
				  ComputeSubrect{ 0, 0, 100, 100 });
	static_assert(ExpandCharacterWorkRect({ 0, 0, 1, 1 }, 100, 100,
					  std::numeric_limits<std::uint32_t>::max()) == ComputeSubrect{ 0, 0, 100, 100 });
	static_assert(!ExpandCharacterWorkRect({}, 100, 100, 4).IsValid());
	static_assert(!ExpandCharacterWorkRect({ 99, 0, 2, 1 }, 100, 100, 4).IsValid());
	static_assert(ExpandCharacterWorkRect({ 20, 30, 10, 15 }, 100, 100, 0) ==
				  ComputeSubrect{ 20, 30, 10, 15 });
	return 0;
}
