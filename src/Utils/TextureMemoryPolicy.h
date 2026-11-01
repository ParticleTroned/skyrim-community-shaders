#pragma once

#include <cstdint>

namespace Util::TextureMemoryPolicy
{
	inline constexpr std::uint64_t MiB = 1024ull * 1024;
	inline constexpr std::uint32_t DefaultReserveMiB = 1024;
	inline constexpr std::uint32_t ReserveStepMiB = 256;
	inline constexpr std::uint32_t MaximumReserveMiB = 8192;
	inline constexpr std::uint64_t RefillGapBytes = 1024 * MiB;
	inline constexpr std::uint64_t ReductionOverlapBytes = 128 * MiB;
#ifdef DEVBENCH_BRIDGE_ENABLED
	inline constexpr std::uint32_t MaximumPressureSimulationMilliseconds = 30000;
	inline constexpr std::uint32_t MaximumPressureSimulationHeadroomMiB = 65536;
#endif
}
