#include "Features/GrassDrawBatch.h"

#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
	void Require(bool condition)
	{
		if (!condition)
			throw std::runtime_error("Grass batch policy failed");
	}
}

int main()
{
	try {
		using namespace GrassDrawBatch;
		std::vector<Range> ranges;
		uint32_t count = 123;
		const std::array<Group, 3> sparse{ Group{ 3, 0 }, Group{ 129, 7 }, Group{ 2, 959 } };
		Require(BuildRanges(sparse, ranges, count));
		Require(count == 134 && ranges.size() == 3);
		Require(ranges[0].end == 3 && ranges[1].end == 132 && ranges[2].end == 134);
		Require(ranges[1].fadeIndex == 7 && ranges[2].fadeIndex == 959);
		for (const auto invalid : std::array<std::array<Group, 2>, 4>{
				 std::array{ Group{ 1, 0 }, Group{ 0, 1 } },
				 std::array{ Group{ 1, 0 }, Group{ 1, 960 } },
				 std::array{ Group{ kMaxInstances, 0 }, Group{ 1, 1 } },
				 std::array{ Group{ UINT32_MAX, 0 }, Group{ UINT32_MAX, 1 } } }) {
			Require(!BuildRanges(invalid, ranges, count));
			Require(ranges.empty() && count == 0);
		}
		Require(!BuildRanges({}, ranges, count));
		Require(!BuildRanges(std::span(sparse).first(1), ranges, count));
		const std::array<Group, 2> limit{ Group{ kMaxInstances - 1, 0 }, Group{ 1, 959 } };
		Require(BuildRanges(limit, ranges, count) && count == kMaxInstances);
		std::cout << "Sparse fade groups, empty batches and whole-batch capacity fallback passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
