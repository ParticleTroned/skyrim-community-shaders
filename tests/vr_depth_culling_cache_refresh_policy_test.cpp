#include "Features/VRDepthCullingCacheRefreshPolicy.h"

namespace
{
	using namespace VRDepthCullingCacheRefreshPolicy;

	constexpr bool CoversRequestPolicy()
	{
		for (unsigned bits = 0; bits < 8; ++bits) {
			const bool effective = (bits & 1u) != 0;
			const bool completed = (bits & 2u) != 0;
			const bool pending = (bits & 4u) != 0;
			if (ShouldRequest(effective, completed, pending) !=
				(effective && !completed && !pending))
				return false;
		}
		return true;
	}

	constexpr bool CoversActionPolicy()
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

	static_assert(CoversRequestPolicy());
	static_assert(CoversActionPolicy());
}

int main()
{
	return CoversRequestPolicy() && CoversActionPolicy() ? 0 : 1;
}
