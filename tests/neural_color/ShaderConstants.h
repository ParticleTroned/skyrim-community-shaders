#pragma once

#include <cstddef>

namespace NeuralColorTest
{
	struct Constants
	{
		unsigned x = 5, y = 7, width = 17, height = 19;
		unsigned mode = 1, domain = 1, transform = 0, bypass = 0;
		float exposure = 1, detail = 1, appearance = 0, maximumStops = 1;
		float lightingPreservation = 1;
		float padding[3]{};
	};
	static_assert(sizeof(Constants) == 64);
	static_assert(offsetof(Constants, exposure) == 32);
	static_assert(offsetof(Constants, lightingPreservation) == 48);
}
