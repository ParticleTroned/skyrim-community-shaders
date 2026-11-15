#include "Features/GrassOptimizations/GrassHiZPolicy.h"

#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
	using namespace DepthPyramidPolicy;

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void CheckActiveAllocation()
	{
		const auto full = ActiveExtent(4032, 2240, 2, 4);
		const auto half = ActiveExtent(2016, 1120, 2, 4);
		Require(full == Extent{ 1024, 1024 } && half == Extent{ 512, 512 }, "Allocation ignored active resolution");
		Require(OutputTexelWork(*full, 2, 7) == 4 * OutputTexelWork(*half, 2, 7), "Reduced dimensions retained full-size work");
		for (uint32_t eyes : { 1u, 2u }) {
			for (uint32_t reduction : { 2u, 4u }) {
				for (uint32_t eyeWidth : { 1u, 2u, 3u, 127u, 128u, 129u, 2016u, 4096u, 8192u }) {
					for (uint32_t height : { 1u, 3u, 255u, 256u, 257u, 2240u, 8192u }) {
						const auto extent = ActiveExtent(eyeWidth * eyes, height, eyes, reduction);
						Require(extent.has_value(), "Supported active extent rejected");
						Require(extent->width / eyes * reduction >= eyeWidth && extent->height * reduction >= height,
							"Allocation omitted active source texels");
						Require(std::has_single_bit(extent->width / eyes) && std::has_single_bit(extent->height),
							"Allocation lost independent power-of-two eyes");
						const auto mips = MipCount(*extent, eyes, 7);
						Require(mips > 0 && mips <= 7 && (extent->width >> (mips - 1)) >= eyes,
							"Mip reduction crossed an eye boundary");
					}
				}
			}
		}
		for (const auto invalid : { std::array{ 0u, 1u, 1u, 4u }, std::array{ 1u, 0u, 1u, 4u },
				 std::array{ 100u, 100u, 0u, 4u }, std::array{ 100u, 100u, 3u, 4u },
				 std::array{ 101u, 100u, 2u, 4u }, std::array{ 100u, 100u, 2u, 0u },
				 std::array{ 100u, 100u, 2u, 3u } })
			Require(!ActiveExtent(invalid[0], invalid[1], invalid[2], invalid[3]), "Invalid layout accepted");
		Require(ActiveExtent(UINT32_MAX, UINT32_MAX, 1, 2) == Extent{ 0x80000000u, 0x80000000u },
			"Large dimensions overflowed during rounding");
		Require(MipCount({ 128, 512 }, 2, 0) == 7 && MipCount({ 128, 512 }, 1, 0) == 8,
			"Flat and stereo mip limits did not retain separate eye columns");
		Require(MipCount({ 100, 512 }, 2, 7) == 0 && MipCount({ 0, 0 }, 1, 7) == 0,
			"Invalid allocations produced a usable mip chain");
	}

	void CheckRetention()
	{
		AllocationRetention retention;
		const Extent full{ 1024, 1024 }, half{ 512, 512 };
		Require(retention.Observe({}, half, 0, 3) == half, "Initial allocation retained backing dimensions");
		Require(retention.Observe(half, full, 1, 3) == full, "Growth delayed coverage of the active viewport");
		Require(retention.Observe(full, half, 2, 3) == full, "Transient reduction resized immediately");
		for (int repeat = 0; repeat < 10; ++repeat)
			Require(retention.Observe(full, half, 2, 3) == full, "Repeated calls counted as rendered frames");
		Require(retention.Observe(full, half, 3, 3) == full, "Shrink bypassed the retention interval");
		Require(retention.Observe(full, half, 4, 3) == half, "Stable reduced demand never reclaimed work");
		for (uint32_t frame = 5; frame < 105; ++frame)
			Require(retention.Observe(full, frame % 2 ? half : full, frame, 3) == full, "Oscillating dimensions churned allocations");
		Require(retention.Observe(full, half, 105, 3) == full && retention.Observe(full, half, 107, 3) == full &&
					retention.Observe(full, half, 108, 3) == full && retention.Observe(full, half, 109, 3) == half,
			"Skipped frames did not restart shrink qualification");
		Require(retention.Observe(full, half, UINT32_MAX - 1, 3) == full &&
					retention.Observe(full, half, UINT32_MAX, 3) == full && retention.Observe(full, half, 0, 3) == half,
			"Frame wrap broke sustained shrink detection");
		Require(retention.Observe(full, { 2048, 512 }, 1, 3) == Extent{ 2048, 1024 },
			"Growth in one dimension shrank the other without qualification");
		Require(retention.Observe({}, half, 2, 3) == half, "Replacement source inherited obsolete allocation dimensions");
	}

	void CheckWorkAdmission()
	{
		using namespace GrassHiZPolicy;
		const Extent full{ 1024, 1024 }, half{ 512, 512 };
		Require(!ShouldBuild(0, full, 2) && !ShouldBuild(AddCandidateIndexWork(0, 1, 2, 24), full, 2),
			"Empty or single-clump demand built a full hierarchy");
		const auto moderate = AddCandidateIndexWork(0, 10000, 2, 24);
		Require(!ShouldBuild(moderate, full, 2) && ShouldBuild(moderate, half, 2),
			"Admission ignored the hierarchy's actual reduction work");
		Require(ShouldBuild(AddCandidateIndexWork(0, 100000, 2, 48), full, 2), "Dense indexed work was always excluded");
		const auto threshold = OutputTexelWork(full, 2, kMaximumMips);
		Require(!ShouldBuild(threshold, full, 2) && ShouldBuild(threshold + 1, full, 2), "Work threshold lost its strict boundary");
		Require(AddCandidateIndexWork(100, 2, 2, 24) == 196 &&
					AddCandidateIndexWork(UINT64_MAX - 5, 1, 1, 6) == UINT64_MAX &&
					AddCandidateIndexWork(0, UINT32_MAX, 2, UINT32_MAX) == UINT64_MAX,
			"Candidate work overflowed or lost stereo multiplicity");
		Require(!ShouldBuild(UINT64_MAX, {}, 2) && OutputTexelWork({ 0x80000000u, 0x80000000u }, 1, 0) > (uint64_t(1) << 62),
			"Invalid or large hierarchy work was underestimated");
	}
}

int main()
{
	try {
		CheckActiveAllocation();
		CheckRetention();
		CheckWorkAdmission();
		std::cout << "Active stereo allocation, bounded shrink retention and grass Hi-Z workload admission passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
