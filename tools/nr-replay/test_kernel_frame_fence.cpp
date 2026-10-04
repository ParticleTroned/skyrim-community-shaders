#include "Features/Upscaling/NeuralRendering/DevBench/KernelFrameFence.h"

#include <iostream>
#include <stdexcept>

namespace
{
	unsigned checks = 0;
	void Check(bool value)
	{
		++checks;
		if (!value)
			throw std::runtime_error("kernel frame fence CPU check failed");
	}
	void Test()
	{
		using namespace NeuralRendering;
		LifetimeFenceSnapshot before;
		before.device = 1;
		before.queue = 2;
		before.fence = 3;
		before.initialized = before.completedKnown = before.recording = true;
		before.issued = 11;
		before.completed = 8;
		before.contexts = { 0, 8, 10 };
		auto after = before;
		after.recording = false;
		after.issued = 12;
		after.contexts[0] = 12;
		Check(KernelFrameFence::Submission(before, after) == 12);
		for (unsigned mutation = 0; mutation < 15; ++mutation) {
			auto changed = after;
			switch (mutation) {
			case 0:
				changed.device = 0;
				break;
			case 1:
				changed.device = 4;
				break;
			case 2:
				changed.queue = 4;
				break;
			case 3:
				changed.fence = 4;
				break;
			case 4:
				changed.initialized = false;
				break;
			case 5:
				changed.completedKnown = false;
				break;
			case 6:
				changed.deviceRemoved = true;
				break;
			case 7:
				changed.completed = UINT64_MAX;
				break;
			case 8:
				changed.recording = true;
				break;
			case 9:
				changed.contexts = before.contexts;
				break;
			case 10:
				changed.contexts[1] = 12;
				break;
			case 11:
				changed.contexts[0] = 11;
				break;
			case 12:
				changed.issued = 13;
				break;
			case 13:
				changed.issued = 11;
				break;
			case 14:
				changed.contexts[0] = 0;
				break;
			}
			bool rejected = false;
			try {
				(void)KernelFrameFence::Submission(before, changed);
			} catch (const std::runtime_error&) {
				rejected = true;
			}
			Check(rejected);
		}
		before.recording = false;
		bool rejected = false;
		try {
			(void)KernelFrameFence::Submission(before, after);
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		Check(rejected);
	}
}
int main()
{
	try {
		Test();
		std::cout << checks << " kernel frame fence CPU checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
