#include "Features/Upscaling/NeuralRendering/MainDepthPresentation.h"

int main()
{
	using NeuralRendering::MainDepthPresentationProof;
	constexpr MainDepthPresentationProof missing{};
	constexpr MainDepthPresentationProof produced{ 42, 0x1000 };
	static_assert(missing.SupportsOutputLayout(100, 80, 100, 80, 42, 0x1000));
	static_assert(!missing.SupportsOutputLayout(50, 40, 100, 80, 42, 0x1000));
	static_assert(produced.SupportsOutputLayout(50, 40, 100, 80, 42, 0x1000));
	static_assert(produced.SupportsOutputLayout(100, 40, 100, 80, 42, 0x1000));
	static_assert(!produced.SupportsOutputLayout(50, 40, 100, 80, 43, 0x1000));
	static_assert(!produced.SupportsOutputLayout(50, 40, 100, 80, 42, 0x2000));
	static_assert(!produced.SupportsOutputLayout(50, 40, 100, 80, 42, 0));
	static_assert(!produced.SupportsOutputLayout(0, 40, 100, 80, 42, 0x1000));
	static_assert(!produced.SupportsOutputLayout(200, 40, 100, 80, 42, 0x1000));
	static_assert(!missing.SupportsOutputLayout(100, 80, 100, 80, ~0u, 0x1000));
	constexpr MainDepthPresentationProof reversed{ 42, 0x1000, Depth::Convention::Reversed, 7 };
	static_assert(reversed.SupportsOutputLayout(50, 40, 100, 80, 42, 0x1000, Depth::Convention::Reversed, 7));
	static_assert(!reversed.SupportsOutputLayout(50, 40, 100, 80, 42, 0x1000, Depth::Convention::Standard, 7));
	static_assert(!reversed.SupportsOutputLayout(50, 40, 100, 80, 42, 0x1000, Depth::Convention::Reversed, 8));
	static_assert(!produced.SupportsOutputLayout(50, 40, 100, 80, 42, 0x1000, Depth::Convention::Reversed, 7));
	static_assert(missing.SupportsOutputLayout(100, 80, 100, 80, 42, 0x1000, Depth::Convention::Reversed, 7));
}
