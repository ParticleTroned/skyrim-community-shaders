#include "Features/Upscaling/ReflexCapture.h"

#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void Lifecycle()
	{
		ReflexDiagnostics::Capture<3> capture;
		using ReflexDiagnostics::Record;
		Require(!capture.Start(100, 0, 30, true), "invalid clock admitted");
		Require(!capture.Start(100, 1000, 121, true), "unbounded duration admitted");
		Require(capture.Start(100, 1000, 30, true), "start failed");
		const auto id = capture.Read(100).sessionId;
		Require(!capture.Start(100, 1000, 30, true), "active capture replaced");
		Require(!capture.Stop(id + 1), "foreign owner stopped capture");
		Require(!capture.Reset(id), "active capture reset");
		Record first;
		Require(capture.Begin(110, first) && first.queryState, "initial query missing");
		Record overlapping;
		Require(!capture.Begin(111, overlapping), "overlapping hook could query the SDK concurrently");
		first.sleepResult = 0;
		first.stateResult = 0;
		first.lowLatencyAvailable = false;
		Require(capture.Stop(id), "stop failed");
		Require(!capture.Reset(id), "in-flight capture reset");
		capture.Finish(first);
		auto snapshot = capture.Read(120, 0, 256);
		Require(!snapshot.active && !snapshot.inFlight && snapshot.retained == 1, "stop did not drain");
		Require(snapshot.dropped == 1, "overlapping hook was not accounted for");
		Require(snapshot.records[0].sleepResult == 0 && snapshot.records[0].lowLatencyAvailable == false, "zero/false evidence lost");
		Require(!snapshot.records[0].latencyReportAvailable, "missing state fabricated");
		Require(snapshot.totals.sleepCalls == 1 && snapshot.totals.sleepFailures == 0 &&
					snapshot.totals.lowLatencyUnavailable == 1,
			"successful API with unavailable SDK state conflated");
		Require(!capture.Start(120, 1000, 30, true), "retained evidence replaced");
		Require(!capture.Reset(id + 1) && capture.Reset(id), "reset ownership failed");
		Require(capture.Start(200, 1000, 1, true), "restart failed");
		Record stale;
		Require(!capture.Begin(199, stale), "timestamp from a previous capture was admitted");
		Require(capture.Read(200).sessionId != id, "session identity reused");
		Record a, b;
		Require(capture.Begin(201, a) && a.queryState, "new session query missing");
		capture.Finish(a);
		Require(capture.Begin(250, b) && !b.queryState && b.intervalTicks == 49, "query pacing/interval wrong");
		capture.Finish(b);
		Require(capture.Read(300, 1, 1).records[0].sequence == 2, "cursor pagination wrong");
		Require(capture.Read(300, UINT64_MAX, 256).records.empty(), "large cursor overflowed");
		Require(!capture.Read(1200).active, "duration not enforced without new frames");
		Record late;
		Require(!capture.Begin(1201, late), "late sample admitted");
	}

	void CapacityAndPassive()
	{
		ReflexDiagnostics::Capture<2> capture;
		Require(capture.Start(100, 1000, 30, false), "passive start failed");
		for (uint64_t tick : { 200, 2200 }) {
			ReflexDiagnostics::Record record;
			Require(capture.Begin(tick, record) && !record.queryState, "passive capture queries SDK");
			capture.Finish(record);
		}
		const auto snapshot = capture.Read(2300, 0, 256);
		Require(!snapshot.active && snapshot.retained == 2 && snapshot.dropped == 0, "capacity did not stop without overwriting");
		Require(std::string_view(snapshot.stopReason) == "capacity", "capacity reason lost");
	}

	void ConcurrentRead()
	{
		ReflexDiagnostics::Capture<> capture;
		Require(capture.Start(1, 1000000, 30, true), "concurrent start failed");
		std::atomic_bool done{ false };
		std::thread reader([&] {
			while (!done)
				(void)capture.Read(1, 0, 256);
		});
		for (uint64_t tick = 2; tick < 10002; ++tick) {
			ReflexDiagnostics::Record record;
			if (capture.Begin(tick, record))
				capture.Finish(record);
		}
		done = true;
		reader.join();
		const auto snapshot = capture.Read(10002);
		Require(snapshot.retained + snapshot.dropped == 10000 && !snapshot.inFlight, "concurrent sampling lost unaccounted records");
	}

	void DelayedStateQuery()
	{
		ReflexDiagnostics::Capture<> capture;
		Require(capture.Start(100, 1000, 30, true), "delayed query start failed");
		ReflexDiagnostics::Record delayed, early, due;
		Require(capture.Begin(110, delayed) && delayed.queryState, "first query missing");
		delayed.stateResult = 0;
		delayed.stateTick = 2100;
		delayed.stateTicks = 300;
		capture.Finish(delayed);
		Require(capture.Begin(2450, early) && !early.queryState, "long frame caused back-to-back SDK queries");
		capture.Finish(early);
		Require(capture.Begin(3400, due) && due.queryState, "query did not resume after completion interval");
		capture.Finish(due);
	}
}

int main()
{
	try {
		Lifecycle();
		CapacityAndPassive();
		ConcurrentRead();
		DelayedStateQuery();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
