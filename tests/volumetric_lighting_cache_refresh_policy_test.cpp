#include "Features/VolumetricLightingCacheRefreshPolicy.h"

namespace
{
	using namespace VolumetricLightingCacheRefreshPolicy;

	constexpr bool CoversEveryState()
	{
		for (unsigned bits = 0; bits < 8; ++bits) {
			const State state{
				.requested = (bits & 1u) != 0,
				.diskCacheActive = (bits & 2u) != 0,
				.shaderCompilationActive = (bits & 4u) != 0
			};

			Action expected = Action::None;
			if (state.requested) {
				expected = !state.diskCacheActive ?
				               Action::ConsumeWithoutRefresh :
				               (state.shaderCompilationActive ? Action::WaitForCompiler : Action::Apply);
			}

			if (SelectAction(state) != expected)
				return false;
		}
		return true;
	}

	static_assert(CoversEveryState());
}

int main()
{
	return CoversEveryState() ? 0 : 1;
}
