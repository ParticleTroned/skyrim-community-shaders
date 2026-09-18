#include "Diagnostics/VRFrustumTelemetryPolicy.h"

#include <memory>
#include <stdexcept>
#include <thread>

using namespace VRFrustumTelemetry;

namespace
{
	struct NativeFault
	{};
	void Check(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}
	std::uint64_t Value(const Row& row, Counter counter)
	{
		return row.counters[static_cast<std::size_t>(counter)].load();
	}
}

int main()
{
	auto counters = std::make_unique<ThreadCounters>();
	const RowKey key{ Routine::Compound, Pass::World, 0, 0x1000 };
	AddressKey address{ 1, 42, 0, 0x2000, 0x3000, 0 };
	auto* row = counters->Begin(key, address, true);
	Check(row && Value(*row, Counter::FirstAddressKeys) == 1, "first object-address context was not counted");
	int nativeCalls = 0;
	const auto native = [&](std::uintptr_t owner, std::uintptr_t object) {
		++nativeCalls;
		Check(owner == address.owner && object == address.objectOrBound, "native arguments changed");
		return nativeCalls != 1;
	};
	Check(!Forward(row, native, address.owner, address.objectOrBound), "false native result changed");
	Check(counters->Begin(key, address, true) == row, "repeat changed its row");
	Check(Forward(row, native, address.owner, address.objectOrBound), "true native result changed");
	Check(nativeCalls == 2 && Value(*row, Counter::Calls) == 2 && Value(*row, Counter::Completed) == 2 &&
			  Value(*row, Counter::RawFalse) == 1 && Value(*row, Counter::RawTrue) == 1 && Value(*row, Counter::RepeatedAddressKeys) == 1,
		"repeat or native completion accounting failed");
	Check(Forward(nullptr, native, address.owner, address.objectOrBound) && nativeCalls == 3,
		"disabled or capacity-limited telemetry must still forward exactly once");
	counters->Begin(key, address, true);
	try {
		Forward(row, []() -> bool { throw NativeFault{}; });
		Check(false, "native exceptions were swallowed");
	} catch (const NativeFault&) {}
	Check(Value(*row, Counter::Calls) == 3 && Value(*row, Counter::Completed) == 2, "faulted native call was counted as completed");

	++address.frame;
	counters->Begin(key, address, true);
	++address.generation;
	counters->Begin(key, address, true);
	Check(Value(*row, Counter::FirstAddressKeys) == 3, "frame or collection generation inherited a prior admission");
	counters->Begin(key, address, false);
	Check(Value(*row, Counter::UnknownFrame) == 1, "unavailable frame identity was not explicit");
	Check(counters->observedFrames.load() == 2 && counters->firstObservedFrame.load() == 42 && counters->lastObservedFrame.load() == 43,
		"unknown frames or generation changes corrupted the observed frame count");
	for (auto changed : { RowKey{ Routine::Compound, Pass::Depth, key.cameraIndex, key.caller },
			 RowKey{ Routine::Compound, key.pass, 1, key.caller },
			 RowKey{ Routine::SphereIntersect, key.pass, key.cameraIndex, key.caller },
			 RowKey{ key.routine, key.pass, key.cameraIndex, key.caller + 1 } }) {
		auto* separate = counters->Begin(changed, address, true);
		Check(separate && separate != row && Value(*separate, Counter::RepeatedAddressKeys) == 0,
			"different camera index, pass, routine or caller was combined as repeated work");
	}
	auto worker = std::make_unique<ThreadCounters>();
	Check(Value(*worker->Begin(key, address, true), Counter::RepeatedAddressKeys) == 0,
		"a separate thread inherited another thread's object tracking");

	auto saturated = std::make_unique<ThreadCounters>();
	for (std::size_t index = 0; index <= ProbeLimit; ++index) {
		// Equal owner/object bits collide deliberately, while the full keys differ.
		AddressKey colliding{ 1, 42, 0, index + 1, index + 1, 0 };
		row = saturated->Begin(key, colliding, true);
	}
	Check(row && Value(*row, Counter::FirstAddressKeys) == ProbeLimit && Value(*row, Counter::TrackingOverflow) == 1 &&
			  Value(*row, Counter::RepeatedAddressKeys) == 0,
		"identity collisions were misreported as unique or repeated objects");
	saturated->Begin(key, { 1, 42, 0, 1, 1, 0 }, true);
	Check(Value(*row, Counter::RepeatedAddressKeys) == 1, "capacity pressure destroyed an already tracked identity");

	auto fullRows = std::make_unique<ThreadCounters>();
	std::uint64_t retained = 0;
	for (std::size_t index = 0; index < RowCapacity * 4; ++index) {
		if (fullRows->Begin({ key.routine, key.pass, key.cameraIndex, index }, address, false))
			++retained;
	}
	Check(retained + fullRows->rowOverflow.load() == RowCapacity * 4 && fullRows->rowOverflow.load() != 0,
		"row-capacity loss was silently omitted");

	Context current;
	try {
		ContextScope outer(current, { Pass::World, 0 });
		{
			ContextScope inner(current, { Pass::Recovery, current.cameraIndex });
			Check(current.pass == Pass::Recovery && current.cameraIndex == 0, "nested context was not applied");
		}
		Check(current.pass == Pass::World, "nested context was not restored");
		throw NativeFault{};
	} catch (const NativeFault&) {}
	Check(current.pass == Pass::Unknown && current.cameraIndex == UnknownCameraIndex, "exception leaked a pass or camera context");

	{
		ContextScope outer(current, { Pass::World, 7, 1 });
		Check(ActiveContext(current, 3).cameraIndex == 7, "raw camera indices beyond 0/1 were discarded");
		Check(ActiveContext(current, 4).pass == Pass::Unknown, "disabled collection exposed old context");
		Check(ActiveContext(current, 7).pass == Pass::Unknown, "re-enabled collection inherited a pre-toggle pass");
		{
			ContextScope fresh(current, { Pass::Depth, 0, 3 });
			Check(ActiveContext(current, 7).pass == Pass::Depth, "new-generation scope was not admitted");
		}
		Check(ActiveContext(current, 7).pass == Pass::Unknown, "nested scope restoration resurrected stale context");
		{
			ContextScope disabled(current, {});
			Check(ActiveContext(current, 9).pass == Pass::Unknown, "enabling inside a disabled scope guessed its parent pass");
		}
	}
	for (const auto cameraIndex : { std::int64_t{ 7 }, std::int64_t{ 0xFFFFFFFF }, UnknownCameraIndex }) {
		auto* separate = counters->Begin({ key.routine, key.pass, cameraIndex, key.caller }, address, true);
		Check(separate && separate->key.cameraIndex == cameraIndex && Value(*separate, Counter::RepeatedAddressKeys) == 0,
			"raw camera index and unavailable context were conflated");
	}

	auto concurrent = std::make_unique<ThreadCounters>();
	std::atomic_bool finished{ false };
	std::jthread writer([&] {
		for (std::uint32_t frame = 1; frame <= 10'000; ++frame) {
			auto* entry = concurrent->Begin(key, { 1, frame, 0, 1, 2, 3 }, true);
			Forward(entry, [] { return true; });
		}
		finished.store(true, std::memory_order_release);
	});
	while (!finished.load(std::memory_order_acquire)) {
		for (const auto& entry : concurrent->rows) {
			if (entry.published.load(std::memory_order_acquire)) {
				Check(entry.key == key, "reader observed an unpublished row identity");
				for (const auto& value : entry.counters)
					Check(value.load(std::memory_order_relaxed) <= 10'000, "reader observed invalid counter data");
			}
		}
	}
	writer.join();
	for (const auto& entry : concurrent->rows) {
		if (entry.published.load())
			Check(Value(entry, Counter::Calls) == 10'000 && Value(entry, Counter::Completed) == 10'000,
				"concurrent snapshot readers lost completed work");
	}
}
