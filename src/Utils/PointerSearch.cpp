#include "PointerSearch.h"

#include <Windows.h>
#include <intrin.h>

namespace Util::PointerSearch
{
	bool HasAvx2() noexcept
	{
		static const bool available = [] {
			int cpu[4]{};
			__cpuidex(cpu, 0, 0);
			if (cpu[0] < 7)
				return false;
			__cpuidex(cpu, 1, 0);
			constexpr int avxState = (1 << 26) | (1 << 27) | (1 << 28);
			if ((cpu[2] & avxState) != avxState)
				return false;
			constexpr auto requiredState = XSTATE_MASK_LEGACY_SSE | XSTATE_MASK_AVX;
			if ((GetEnabledXStateFeatures() & requiredState) != requiredState)
				return false;
			__cpuidex(cpu, 7, 0);
			return (cpu[1] & (1 << 5)) != 0;
		}();
		return available;
	}
}
