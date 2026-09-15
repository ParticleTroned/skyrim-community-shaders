#include "Features/Upscaling/NeuralRendering/ColorMeasurementBatch.h"
#include <cstdio>
#include <cstdlib>

using namespace NeuralRendering::Color;
static unsigned checks = 0;
static void Require(bool value)
{
	++checks;
	if (!value) {
		std::fprintf(stderr, "Failed batch check %u\n", checks);
		std::abort();
	}
}
static MeasurementBatchKey Key(std::uint64_t id, std::uint32_t mask = 0x33)
{
	return { id, 7, 4, static_cast<std::uint32_t>(100 + id), static_cast<std::uint32_t>(100 + id), 0, mask, true };
}
int main()
{
	MeasurementBatchHistory<unsigned, 4, 3> history;
	const auto split = Key(1);
	Require(history.Record(split, 0, 100));
	Require(history.Record(split, 1, 101));
	Require(history.Record(split, 4, 104));
	Require(!history.Latest()[0].Complete());
	const auto single = Key(2, 3);
	Require(history.Record(single, 0, 200));
	Require(history.Record(single, 1, 201));
	Require(history.Latest()[0].key.id == 2);
	// A retired secondary can complete the older batch without borrowing new eyes.
	Require(history.Record(split, 5, 105));
	auto complete = history.Latest();
	Require(complete[0].key.id == 2 && complete[1].key.id == 1);
	Require(complete[1].samples[0] == 100 && complete[1].samples[5] == 105);
	Require(complete[0].key.expectedSlotMask == 3 && complete[1].key.expectedSlotMask == 0x33);
	const auto asymmetric = Key(3, 0x13);
	for (unsigned slot : { 4u, 1u, 0u }) Require(history.Record(asymmetric, slot, 300 + slot));
	Require(history.Latest()[0].Complete());
	Require(!history.Record(asymmetric, 4, 399));
	Require(history.Latest()[0].key.id == 2);
	Require(history.Record(Key(4), 0, 400));
	auto changed = Key(4);
	changed.generation++;
	Require(!history.Record(changed, 1, 401));
	Require(!history.Record(Key(4), 4, 404));
	Require(history.Record(Key(5, 3), 0, 500));
	Require(!history.Record(split, 0, 999));
	Require(history.Record(Key(8, 3), 0, 800));
	Require(history.EvictedIncomplete() == 1);
	Require(history.Record(Key(9, 3), 0, 900));
	Require(history.EvictedIncomplete() == 2);
	Require(!history.Record(Key(10, 0xff), 0, 1000));
	Require(!history.Record(Key(11, 0x30), 4, 1104));
	Require(!history.Record(Key(12), 8, 1208));
	auto submit = Key(13, 0xcc);
	submit.frame = 0;
	submit.sourceWorldFrame = 0;
	for (unsigned slot : { 2u, 3u, 6u, 7u }) Require(history.Record(submit, slot, slot));
	Require(history.Latest()[0].key.id == 13);
	auto mono = Key(14, 0x11);
	mono.atomicStereo = false;
	Require(history.Record(mono, 0, 1) && history.Record(mono, 4, 2));
	Require(history.Latest()[0].Complete() && !history.Latest()[0].key.atomicStereo);
	std::printf("%u measurement batch checks passed\n", checks);
}
