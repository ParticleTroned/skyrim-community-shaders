#include "Diagnostics/EngineStutterRecorder.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using namespace CSX::Diagnostics::Stutters;
void Check(bool ok, const char* message)
{
	if (!ok)
		throw std::runtime_error(message);
}
const Phase& Active(const Batch& batch, unsigned thread)
{
	for (const auto& phase : batch.active)
		if (phase.thread == thread)
			return phase;
	throw std::runtime_error("missing thread slot");
}
int main()
{
	try {
		Recorder recorder;
		Check(!recorder.Begin(1, "disabled", 100).phase.id, "disabled recorder captured work");
		recorder.Start(50, 1);
		Context context{ .qpc = 90, .frame = 42, .cell = 123, .position = { 1, 2, 3 }, .playerAvailable = true };
		recorder.PublishContext(context, recorder.Epoch());
		auto outer = recorder.Begin(1, "engine", 100, Boundary::Engine);
		auto inner = recorder.Begin(1, "driver", 110);
		recorder.End(inner, 160);
		auto batch = recorder.Drain(160);
		Check(batch.count == 1 && batch.events[0].end == 160 && batch.events[0].complete, "inclusive threshold failed");
		Check(std::string(Active(batch, 1).name.data()) == "engine", "nested scope did not restore engine");
		Check(batch.events[0].phase.context.frame == 42, "entry context lost");
		auto shortChild = recorder.Begin(1, "short child", 165);
		batch = recorder.Drain(170);
		Check(batch.overdue[outer.slot].id == outer.phase.id && Active(batch, 1).id == shortChild.phase.id, "short child hid long outer scope");
		Check(recorder.StillCurrent(outer.phase, false), "long outer scope was not current");
		recorder.End(shortChild, 175);
		recorder.End(outer, 180);
		recorder.End(outer, 190);
		auto next = recorder.Begin(1, "engine", 200, Boundary::Engine);
		batch = recorder.Drain(200);
		Check(batch.count == 2 && !batch.events[0].gap && batch.events[1].gap, "duplicate end or cadence observation wrong");
		const auto epoch = recorder.Epoch();
		recorder.Stop();
		recorder.Start(50, 250);
		auto fresh = recorder.Begin(1, "fresh", 300);
		recorder.End(next, 999);
		recorder.PublishContext(context, epoch);
		Check(recorder.Drain(310).context.frame == 0, "old context contaminated restart");
		Check(Active(recorder.Drain(310), 1).id == fresh.phase.id, "stale token replaced active scope");
		recorder.InvalidateExit(next);
		Check(recorder.StillCurrent(fresh.phase, false), "old failed exit invalidated new capture");
		recorder.End(fresh, 349);
		Check(recorder.Drain(349).count == 0, "sub-threshold scope captured");
		recorder.Start(50, 1);
		auto parent = recorder.Begin(1, "parent", 100);
		auto child = recorder.Begin(1, "lost exit", 110);
		auto other = recorder.Begin(2, "other thread", 110);
		recorder.InvalidateExit(child);
		batch = recorder.Drain(200);
		Check(!Active(batch, 1).id && !recorder.StillCurrent(child.phase, false), "lost exit left phantom active stall");
		Check(recorder.StillCurrent(other.phase, false), "unrelated thread loss discarded active scope");
		recorder.End(parent, 200);
		recorder.End(other, 200);
		Check(recorder.Drain(200).count == 1, "invalidated scopes were fabricated as completed");
		recorder.Start(50, 1);
		parent = recorder.Begin(1, "parent", 100);
		child = recorder.Begin(1, "child", 110);
		recorder.End(parent, 160);
		Check(!recorder.StillCurrent(parent.phase, false) && !recorder.StillCurrent(child.phase, false), "out-of-order exit retained closed parent");
		recorder.End(child, 180);
		Check(recorder.Drain(200).count == 0, "out-of-order exit fabricated completed scope");
		recorder.Start(50, 1);
		Check(!recorder.Begin(0, "invalid", 10).phase.id && !recorder.Begin(1, "invalid", 0).phase.id &&
				  !recorder.Begin(1, "invalid", 10, Boundary::Count).phase.id,
			"invalid hook input accepted");
		auto longName = recorder.Begin(1, std::string(300, 'x'), 10);
		Check(longName.phase.name.back() == 0 && longName.phase.nameTruncated, "dynamic label is not safely bounded");
		recorder.End(longName, 0);
		Check(!recorder.StillCurrent(longName.phase, false), "failed clock left active scope");
		recorder.Start(50, 1);
		for (int i = 0; i < 300; ++i) {
			auto t = recorder.Begin(1, "slow", 1000 + i * 100);
			recorder.End(t, 1050 + i * 100);
		}
		batch = recorder.Drain(40000);
		Check(batch.count == 256 && batch.droppedEvents == 44, "bounded queue hid overflow");
		Check(recorder.Drain(40000).count == 0, "drain replayed events");
		recorder.Start(50, 1);
		for (unsigned i = 1; i <= 16; ++i) recorder.Begin(i, "thread", 10);
		Check(!recorder.Begin(17, "overflow", 10).phase.id && recorder.Drain(100).lostUpdates == 1, "thread capacity overflow hidden");
		recorder.Start(50, 1);
		for (unsigned i = 1; i <= 100; ++i) {
			auto t = recorder.Begin(i, "recycled", 10);
			Check(t.phase.id != 0, "inactive slot was not recycled");
			recorder.End(t, 11);
		}
		recorder.Start(50, 1);
		std::array<Token, kScopeDepth> nested{};
		for (auto& token : nested) token = recorder.Begin(1, "nested", 10);
		Check(!recorder.Begin(1, "depth overflow", 10).phase.id, "unbounded nesting accepted");
		for (auto i = nested.rbegin(); i != nested.rend(); ++i) recorder.End(*i, 11);
		Check(!Active(recorder.Drain(100), 1).id, "overflow corrupted nested restoration");
		recorder.Start(50, 1);
		auto complete = recorder.Begin(1, "complete", 10);
		recorder.End(complete, 60);
		recorder.Begin(0, "later loss", 70);
		Check(recorder.Drain(80).events[0].complete, "later unrelated loss rewrote completed evidence");
		recorder.Start(50, 1);
		std::vector<std::thread> producers;
		for (unsigned id = 1; id <= 4; ++id) producers.emplace_back([&, id] {
			for (int i = 0; i < 10000; ++i) {
				auto t = recorder.Begin(id, "concurrent", 100 + i * 100);
				recorder.End(t, 160 + i * 100);
			}
		});
		for (int i = 0; i < 100; ++i) {
			batch = recorder.Drain(1000000);
			for (unsigned j = 0; j < batch.count; ++j) Check(batch.events[j].end >= batch.events[j].phase.begin, "torn event under contention");
		}
		for (auto& producer : producers) producer.join();
		recorder.Start(50, 100, 200);
		auto deadlineScope = recorder.Begin(1, "deadline", 150);
		recorder.End(deadlineScope, 200);
		Check(!recorder.Enabled() && !recorder.Begin(1, "after deadline", 201).phase.id, "hook deadline depended on the watchdog making progress");
		recorder.Stop();
		Check(!recorder.Enabled(), "stop failed");
		std::cout << "Engine stutter recorder tests passed\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
