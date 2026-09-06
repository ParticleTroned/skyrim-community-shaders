#include "Features/Upscaling/NeuralRendering/CharacterRegionPolicy.h"

#include <cstdint>

int main()
{
	using NeuralRendering::CharacterRect;
	using NeuralRendering::CharacterRegionPolicy::IsWithinHoldWindow;
	using NeuralRendering::CharacterRegionPolicy::Union;

	static_assert(IsWithinHoldWindow(12, 10, 2));
	static_assert(!IsWithinHoldWindow(13, 10, 2));
	static_assert(!IsWithinHoldWindow(1, 0xFFFFFFFFu, 3));

	constexpr CharacterRect left{ 10, 20, 30, 40 };
	constexpr CharacterRect right{ 25, 5, 50, 35 };
	static_assert(Union(left, right) == CharacterRect{ 10, 5, 50, 40 });
	static_assert(Union({}, left) == left);
	static_assert(Union(right, {}) == right);
	static_assert(Union({}, {}) == CharacterRect{});
	return 0;
}
