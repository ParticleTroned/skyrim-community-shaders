#include "Features/Upscaling/NeuralRendering/ColorMeasurementBatch.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

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
	// Capture ownership survives rolling eviction and out-of-order completion.
	MeasurementBatchHistory<unsigned, 2, 1> retained;
	const auto captureKey = Key(1, 3);
	auto capture = retained.Pin(captureKey);
	Require(capture && retained.Pin(captureKey) == capture);
	Require(retained.Record(captureKey, 0, 10));
	Require(retained.Record(Key(3, 3), 0, 30));
	Require(!retained.Record(captureKey, 1, 11));
	Require(capture->Snapshot().Complete() && capture->Snapshot().samples[1] == 11);
	auto wrong = captureKey;
	++wrong.revision;
	Require(!retained.Record(wrong, 1, 91));
	Require(capture->Snapshot().invalid);
	Require(!retained.Pin({}));
	capture.reset();
	Require(retained.Pin(captureKey)->Snapshot().invalid);

	Util::CaptureRetention<unsigned, unsigned, 2> bounded;
	auto first = bounded.Pin(1, 10), second = bounded.Pin(2, 20);
	Require(first && second && !bounded.Pin(3, 30));
	Require(bounded.Pin(1, 99) == first && first->Snapshot() == 10);
	bounded.Update(2, [](auto& value) { value = 21; });
	Require(second->Snapshot() == 21 && first->Snapshot() == 10);
	auto overlappingCapture = bounded.Pin(1, 99);
	first.reset();
	Require(!bounded.Pin(3, 30));
	overlappingCapture.reset();
	Require(static_cast<bool>(bounded.Pin(3, 30)));
	// Consumers must never observe half of a concurrent CPU publication.
	Util::CaptureRetention<unsigned, std::array<unsigned, 2>> concurrent;
	const auto shared = concurrent.Pin(1, {});
	std::atomic_bool finished = false;
	std::thread consumer([&] {
		do {
			const auto value = shared->Snapshot();
			Require(value[0] == value[1]);
		} while (!finished.load());
	});
	for (unsigned value = 1; value <= 10000; ++value)
		concurrent.Update(1, [&](auto& stored) { stored = { value, value }; });
	finished.store(true);
	consumer.join();
	Require(shared->Snapshot()[0] == 10000);
	std::printf("%u measurement batch checks passed\n", checks);
}
