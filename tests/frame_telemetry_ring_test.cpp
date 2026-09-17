#include "Features/Upscaling/FrameTelemetryRing.h"

#include <cassert>
#include <cstdint>
#include <limits>

namespace
{
	struct Snapshot
	{
		bool valid = false;
		std::uint32_t frame = 0;
		std::uint32_t value = 0;
	};
}

int main()
{
	using UpscalingTelemetry::FrameTelemetryRing;
	using UpscalingTelemetry::SaturatingIncrement;

	FrameTelemetryRing<Snapshot, 2> ring;
	ring.GetOrCreate(10).value = 100;
	ring.GetOrCreate(11).value = 110;
	assert(ring.Find(10) && ring.Find(10)->value == 100);
	assert(ring.Find(11) && ring.Find(11)->value == 110);
	assert(&ring.GetOrCreate(10) == ring.Find(10));

	ring.GetOrCreate(12).value = 120;
	assert(!ring.Find(10));
	assert(ring.Find(11) && ring.Find(11)->value == 110);
	assert(ring.Find(12) && ring.Find(12)->value == 120);

	ring.GetOrCreate(9).value = 90;
	assert(ring.Find(12) && ring.Find(12)->value == 120);
	assert(ring.Find(9) && ring.Find(9)->value == 90);

	std::uint32_t value = std::numeric_limits<std::uint32_t>::max() - 1;
	SaturatingIncrement(value);
	assert(value == std::numeric_limits<std::uint32_t>::max());
	SaturatingIncrement(value);
	assert(value == std::numeric_limits<std::uint32_t>::max());
	return 0;
}
