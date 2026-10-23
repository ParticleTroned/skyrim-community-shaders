#include "Diagnostics/ReflexMeasurementState.h"
#include "Utils/VRFrameTiming.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace CSX::Diagnostics::ReflexMeasurement;
namespace
{
	void Check(bool value, const char* reason)
	{
		if (!value) {
			std::fprintf(stderr, "%s\n", reason);
			std::exit(EXIT_FAILURE);
		}
	}
	void Append(Journal& j, uint32_t frame, Tick enter, Tick exit, Exit reason = Exit::Updated)
	{
		const auto ticket = j.Begin(enter);
		j.End(ticket, AppRecord{ ticket.enter, exit, frame, 1, true, reason }, j.app);
	}
	std::vector<CompositorRecord> History(uint32_t first, uint32_t last)
	{
		std::vector<CompositorRecord> rows;
		for (auto frame = first; frame <= last; ++frame) {
			CompositorRecord r{};
			r.frame = frame;
			r.systemSeconds = frame * .01;
			r.clientMs = 10;
			r.retrieved = frame * 10;
			rows.push_back(r);
		}
		return rows;
	}
	void AdmissionAndDrain()
	{
		Journal j;
		Check(!j.Begin(1).admitted, "inactive collector admitted a frame");
		j.Start(Mode::Minimal, 16, 10, false);
		Check(!j.Begin(15).admitted, "arming admitted before serialization/settling finished");
		j.Open(20);
		Check(!j.Begin(21, true).admitted, "minimal observer admitted a native operation");
		j.Close(25);
		j.Freeze(26, true, false);
		j.Start(Mode::Passive, 16, 27);
		for (auto reason : { Exit::Uninitialized, Exit::AdapterUnavailable, Exit::PluginUnavailable, Exit::FrameGeneration, Exit::Updated })
			Append(j, static_cast<uint32_t>(j.app.size()), 30 + 10 * static_cast<Tick>(j.app.size()), 31 + 10 * static_cast<Tick>(j.app.size()), reason);
		const auto tail = j.Begin(90);
		j.Close(100);
		Check(!j.Begin(101).admitted && j.Outstanding() == 1, "stop failed to close admission or drain tickets");
		const auto child = j.AdmitChild(tail);
		Check(child.admitted && child.inherited && child.enter == tail.enter && j.Outstanding() == 2, "admitted application frame lost its native drain admission");
		j.End(child, NativeRecord{ 110, 120 }, j.native);
		j.End(tail, AppRecord{ tail.enter, 4000, 5, 1, true, Exit::Updated }, j.app);
		j.Freeze(4010, true, false);
		Check(j.quality.Complete() && j.app.back().exit == 4000, "long stall was discarded or successful drain invalidated");
		Check(j.app.size() == 6 && j.app[0].reason == Exit::Uninitialized, "early exits not preserved");
	}
	void LossAndTimeout()
	{
		Journal j;
		j.Start(Mode::Passive, 16, 10);
		for (uint32_t i = 0; i <= j.appLimit; ++i) Append(j, i, 20 + i, 21 + i);
		const auto unfinished = j.Begin(40);
		j.Close(50);
		j.Freeze(100, false, true);
		Check(j.quality.overflow == 1 && j.quality.unfinished == 1 && j.quality.missingTail && j.quality.postrollTimedOut, "missing loss or timeout receipt");
		const auto frozen = j.app.size();
		bool rejected{};
		try {
			j.Start(Mode::Minimal, 16, 110);
		} catch (const std::logic_error&) {
			rejected = true;
		}
		Check(rejected, "capture reused while an old ticket was outstanding");
		j.End(unfinished, AppRecord{ 40, 200, 18, 1, true, Exit::Updated }, j.app);
		Check(j.app.size() == frozen && j.quality.unfinished == 1, "late completion mutated frozen export");
		j.Start(Mode::Passive, 16, 210);
		Check(!j.AdmitChild(unfinished).admitted, "old application admission leaked into a later capture epoch");
		Append(j, 1, 220, 219);
		Append(j, 1, 221, 222);
		Append(j, 4, 224, 225);
		j.Close(230);
		j.Freeze(240, true, false);
		Check(j.quality.invalidClocks == 1 && j.quality.appDuplicates == 1 && j.quality.appGaps == 2, "invalid clock/frame progression not counted");
	}
	void MissingCompletions()
	{
		Journal j;
		j.Start(Mode::Passive, 16, 10);
		const auto app = j.Begin(20);
		j.RecordStart(app, AppRecord{ 20, 0, 1, 1, true }, j.appStarted, j.appLimit);
		const auto native = j.AdmitChild(app);
		NativeRecord prepared{};
		prepared.operationEnter = 21;
		prepared.admitted = app.enter;
		prepared.applicationSequence = app.sequence;
		j.RecordStart(native, prepared, j.nativeStarted, j.nativeLimit);
		j.Close(30);
		j.Freeze(40, false, true);
		Check(j.quality.unfinished == 2 && j.quality.missingCompletions == 2, "freeze lost pending entry evidence");
		Check(j.app.size() == 1 && !j.app[0].completed && j.native.size() == 1 && !j.native[0].completed, "pending entries mistaken for completed calls");
		j.End(native, NativeRecord{ 22, 50 }, j.native);
		j.End(app, AppRecord{ 20, 51 }, j.app);
		Check(j.app.size() == 1 && !j.app[0].completed && !j.Outstanding(), "late completion changed frozen evidence or stranded admission");
	}
	void FailedPreparationAndFinalization()
	{
		Journal j;
		j.Start(Mode::Passive, 16, 10, false);
		j.Close(11);
		j.Freeze(12, false, false);
		Check(!j.quality.Complete() && !j.Outstanding(), "failed preparation became valid evidence");
		j.Start(Mode::Passive, 16, 20);
		const auto ticket = j.Begin(21);
		j.RecordStart(ticket, AppRecord{ 21, 0, 1, 1, true }, j.appStarted, j.appLimit);
		j.Close(22);
		bool rejected = false;
		try {
			j.FreezeWith(23, true, false, [] { throw std::runtime_error("injected finalization failure"); });
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		Check(rejected && !j.frozen && j.freeze == 0, "partial finalization was published as frozen");
		j.Freeze(24, true, false);
		Check(j.frozen && j.freeze == 24 && j.app.size() == 1 && j.quality.missingCompletions == 1,
			"finalization retry lost or duplicated the unfinished record");
		j.End(ticket, AppRecord{ 21, 25, 1, 1, true }, j.app);
		Check(j.app.size() == 1 && !j.app[0].completed, "late completion modified frozen evidence");
	}

	void IndependentHistory()
	{
		CompositorCursor c;
		std::vector<CompositorRecord> destination;
		auto emit = [&](auto row) { destination.push_back(row); };
		c.Consume(History(90, 100), emit);
		Check(destination.empty() && c.last == 98, "source seed imported pre-observer history");
		c.stopTarget = 102;
		c.Consume(History(95, 102), emit);
		Check(c.last == 100 && !c.Drained(), "newest two incomplete compositor entries were admitted");
		c.Consume(History(97, 104), emit);
		Check(c.Drained() && destination.size() == 4 && destination.back().frame == 102, "delayed post-roll did not close the exact source boundary");
		CompositorCursor lost;
		lost.Consume(History(90, 100), emit);
		destination.clear();
		lost.Consume(History(200, 220), emit);
		Check(lost.quality.sourceLost == 101 && !destination.empty(), "empty destination hid lost source history");
		auto invalid = History(210, 222);
		invalid[10].systemSeconds = std::numeric_limits<double>::quiet_NaN();
		lost.Consume(invalid, emit);
		Check(lost.quality.invalidCompositor != 0, "invalid compositor clock not retained as a failure");
		lost.Consume(History(1, 10), emit);
		Check(lost.quality.resets == 1, "runtime reset not recorded");
	}
	void Restoration()
	{
		PreferenceLease l;
		const Preferences original{ true, false, false, true, 60 };
		l.Acquire(original);
		auto next = original;
		next.boost = true;
		Check(l.Write(original, next), "lease rejected its owned condition");
		Check(l.Restore(next, 100), "owned settings failed requested restoration");
		l.Confirm(101, 3, false);
		Check(l.requestedRestored && !l.acceptedRestored && l.active, "variable write masqueraded as native acceptance");
		l.Confirm(102, 4, true);
		Check(l.acceptedRestored && l.acceptedGeneration == 4 && !l.active, "subsequent accepted generation not retained");
		l.Acquire(original);
		Check(l.Write(original, next), "second lease failed");
		auto user = next;
		user.fps = 45;
		Check(!l.Restore(user, 200) && l.conflict && !l.requestedRestored && !l.active, "restoration overwrote intervening user changes or stranded its conflicted lease");
	}
	void SingleFrameHistory()
	{
		struct Timing
		{
			uint32_t m_nFrameIndex{};
		};
		CompositorCursor cursor;
		std::vector<CompositorRecord> output;
		auto poll = [&](uint32_t latest, uint32_t advance, int failAgo = -1) {
			std::array<Timing, HistoryCapacity> source{};
			uint32_t calls{};
			const auto count = Util::VRFrameTiming::ReadRecentHistory(std::span(source.data(), source.size()), cursor.seeded ? cursor.last : 0,
				[&](auto& timing, uint32_t ago) {
					++calls;
					timing.m_nFrameIndex = latest + (ago ? advance : 0) - ago;
					return static_cast<int>(ago) != failAgo;
				});
			Check(calls <= 18, "single-frame history exceeded the bounded catch-up budget");
			std::array<CompositorRecord, HistoryCapacity> rows{};
			for (size_t i = 0; i < count; ++i)
				rows[i] = History(source[i].m_nFrameIndex, source[i].m_nFrameIndex).front();
			cursor.Consume(std::span(rows.data(), count), [&](auto row) { output.push_back(row); });
		};
		poll(100, 1);
		Check(cursor.seeded && cursor.last == 98 && output.empty(), "advancing reads did not establish a completed boundary");
		for (uint32_t frame = 101; frame < 300; ++frame) {
			poll(frame, frame % 2);
			poll(frame, 0);
		}
		Check(output.size() == 199 && cursor.last == 297 && cursor.quality.Complete(),
			"duplicate/advancing single-frame reads lost frames or emitted incomplete data");
		cursor.Close();
		poll(302, 1);
		Check(cursor.Drained() && output.back().frame == 299, "single-frame post-roll did not stop at the exact target");
		cursor.stopTarget.reset();
		poll(303, 0, 1);
		Check(cursor.quality.sourceFailures == 1, "failed single-frame read was hidden");
		poll(304, 0);
		Check(cursor.last == 302 && cursor.quality.sourceLost == 0, "next history failed to recover retained frames");
		poll(350, 0);
		Check(cursor.quality.sourceLost > 0, "bounded history concealed an actual source gap");
		poll(100, 0);
		Check(cursor.quality.resets == 1, "single-frame source restart was not recorded");
	}
}
int main()
{
	AdmissionAndDrain();
	LossAndTimeout();
	MissingCompletions();
	FailedPreparationAndFinalization();
	IndependentHistory();
	SingleFrameHistory();
	Restoration();
	return EXIT_SUCCESS;
}
