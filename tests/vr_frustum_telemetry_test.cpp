#include "Diagnostics/VRFrustumTelemetryPolicy.h"

#include <cstdio>
#include <cstring>
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
		if (!value) {
			std::fprintf(stderr, "%s\n", message);
			throw std::runtime_error(message);
		}
	}
	std::uint64_t Value(const Row& row, Counter c) { return row.counters[static_cast<std::size_t>(c)].load(); }

	struct Memory
	{
		static constexpr std::uintptr_t Base = 0x10000, Owner = Base, Operators = Base + 0x1000, Planes = Base + 0x3000;
		std::array<std::byte, 0x7000> bytes{};
		std::size_t reads = 0;
		bool operator()(std::uintptr_t address, void* output, std::size_t size)
		{
			++reads;
			if (address < Base || address - Base > bytes.size() || size > bytes.size() - (address - Base))
				return false;
			std::memcpy(output, bytes.data() + (address - Base), size);
			return true;
		}
		template <class T>
		void Put(std::uintptr_t address, T value)
		{
			Check(address >= Base && address - Base + sizeof(T) <= bytes.size(), "bad fixture write");
			std::memcpy(bytes.data() + address - Base, &value, sizeof(value));
		}
		void Op(std::uint32_t i, NativeOperator value) { Put(Operators + i * sizeof(value), value); }
		void Init(std::uint32_t operators = 6, std::uint32_t planes = 1)
		{
			Put(Owner, Planes);
			Put(Owner + 0x18, Operators);
			Put(Owner + 0x10, planes);
			Put(Owner + 0x28, operators);
			Put(Owner + 0xB8, planes);
			Put(Owner + 0xBC, operators);
			Put(Owner + 0xC0, 0u);
			Op(0, { 8, 2, 4 });
			Op(1, { 0, 0, 0 });
			Op(2, { 3, 0, 0 });
			Op(4, { 2, 0, 0 });
			Put(Planes + 0x60, 63u);
			Put(Owner + 0x200 + 0xF0, 1u);
		}
		Detail Start()
		{
			Detail d;
			d.owner = Owner;
			d.object = Owner + 0x200;
			d.boundValid = ReadBound(d.object, d.boundBits, *this);
			d.structure = ReadStructure(Owner, *this);
			d.cursor = d.structure.header.firstOp;
			d.chainValid = d.structure.header.valid;
			return d;
		}
	};

	void TestBatchedWork()
	{
		auto counters = std::make_unique<ThreadCounters>();
		const RowKey key{ Routine::Compound, Pass::Depth, 0, 0xD99D2D };
		Traversal* active = nullptr;
		Row *compound = nullptr, *sphere = nullptr;
		for (std::uint32_t object = 0; object < 100; ++object) {
			Traversal t;
			t.compound = compound = counters->Resolve(key);
			t.compound->Increment(Counter::Calls);
			t.sphereRows[0] = counters->Resolve({ Routine::SphereIntersect, key.pass, 0, 0xDA345E });
			t.sphereRows[1] = sphere = counters->Resolve({ Routine::SphereNotFullyInside, key.pass, 0, 0xDA343F });
			TraversalScope scope(active, &t);
			for (unsigned test = 0; test < 64; ++test) {
				++active->sphere[1].calls;
				active->sphere[1].Complete(test % 2 == 0);
			}
			t.Complete(object % 2 == 0);
		}
		Check(counters->rowLookups == 3, "hot sphere path must not hash addresses or resolve rows per test/object");
		Check(Value(*compound, Counter::Calls) == 100 && Value(*compound, Counter::Completed) == 100, "compound totals lost");
		Check(Value(*sphere, Counter::Calls) == 6400 && Value(*sphere, Counter::RawTrue) == 3200 && Value(*sphere, Counter::RawFalse) == 3200, "batch result totals changed");
		Check(compound->sphereCallsByOutcome[0][1] == 3200 && compound->sphereCallsByOutcome[1][1] == 3200, "accepted/rejected work was conflated");
		Check(active == nullptr, "traversal context leaked");
		Traversal outer;
		outer.compound = compound;
		outer.sphereRows[0] = sphere;
		try {
			TraversalScope first(active, &outer);
			{
				TraversalScope disabled(active, nullptr);
				Check(!active, "disabled nested call inherited parent");
			}
			Check(active == &outer, "nested disabled scope lost parent");
			Traversal inner;
			inner.sphereRows[1] = sphere;
			{
				TraversalScope second(active, &inner);
				++active->sphere[1].calls;
				active->sphere[1].Complete(true);
				inner.Complete(true);
			}
			Check(active == &outer && outer.sphere[1].calls == 0, "nested work charged to parent");
			++outer.sphere[0].calls;
			throw NativeFault{};
		} catch (const NativeFault&) {}
		Check(!active && Value(*sphere, Counter::Calls) == 6402 && Value(*sphere, Counter::Completed) == 6401, "unwind lost partial counts or invented completion");
		Check(Value(*compound, Counter::Completed) == 100, "faulted native traversal completed");
		Traversal empty;
		empty.compound = compound;
		empty.Complete(false);
		empty.Complete(true);
		Check(compound->zeroTestObjectsByOutcome[0] == 1 && compound->zeroTestObjectsByOutcome[1] == 1, "zero-test return classification lost");
		Traversal lost;
		lost.lost = &counters->unattributedCalls;
		lost.sphere[1].calls = 123;
		lost.Publish();
		Check(counters->unattributedCalls == 123, "capacity-limited sphere calls disappeared");
		int calls = 0;
		auto native = [&](int a, int b) {++calls;Check(a==17&&b==24,"native arguments changed");return calls%2==0; };
		Check(!Forward(compound, native, 17, 24) && Forward(nullptr, native, 17, 24) && calls == 2, "native result or forwarding changed");
		try {
			Forward(compound, []() -> bool { throw NativeFault{}; });
			Check(false, "native exception swallowed");
		} catch (const NativeFault&) {}
	}

	void TestContexts()
	{
		auto counters = std::make_unique<ThreadCounters>();
		const RowKey key{ Routine::Compound, Pass::Depth, 0, 100 };
		auto* base = counters->Resolve(key);
		for (auto changed : { RowKey{ key.routine, Pass::World, 0, 100 }, RowKey{ key.routine, key.pass, 1, 100 }, RowKey{ Routine::SphereIntersect, key.pass, 0, 100 }, RowKey{ key.routine, key.pass, 0, 101 }, RowKey{ key.routine, key.pass, UnknownCameraIndex, 100 } })
			Check(counters->Resolve(changed) && counters->Resolve(changed) != base, "row context conflated");
		counters->ObserveFrame(42, true);
		counters->ObserveFrame(42, true);
		counters->ObserveFrame(0, false);
		counters->ObserveFrame(43, true);
		Check(counters->observedFrames == 2 && counters->firstObservedFrame == 42 && counters->lastObservedFrame == 43, "observed frame denominator changed");
		auto full = std::make_unique<ThreadCounters>();
		std::size_t retained = 0;
		for (std::size_t i = 0; i < RowCapacity * 4; ++i) retained += full->Resolve({ key.routine, key.pass, 0, i }) != nullptr;
		Check(retained + full->rowOverflow == RowCapacity * 4 && full->rowOverflow > 0, "row capacity loss missing");
		Context context;
		try {
			ContextScope first(context, { Pass::Depth, 7, Generation(InitialControl) });
			Check(ActiveContext(context, InitialControl).cameraIndex == 7, "raw camera discarded");
			auto disabled = ChangeControl(InitialControl, 1, false);
			Check(ActiveContext(context, disabled).pass == Pass::Unknown, "disabled stale context");
			auto enabled = ChangeControl(disabled, 1, true);
			Check(ActiveContext(context, enabled).pass == Pass::Unknown, "toggle resurrected old context");
			{
				ContextScope second(context, { Pass::World, 0, Generation(enabled) });
				Check(ActiveContext(context, enabled).pass == Pass::World, "new scope rejected");
			}
			Check(ActiveContext(context, enabled).pass == Pass::Unknown, "nested restore resurrected old generation");
			Check(ChangeControl(enabled, 1, true) == enabled, "idempotent control changed generation");
			Check(Generation(ChangeControl(enabled, 2, false)) == Generation(enabled) + 1, "detail toggle missing generation boundary");
			throw NativeFault{};
		} catch (const NativeFault&) {}
		Check(context.pass == Pass::Unknown, "exception leaked pass scope");
		SamplingBudget budget;
		Check(!budget.Admit(InitialControl, 1, false) && budget.Admit(InitialControl, 1, true), "first sample or unknown frame admission wrong");
		for (unsigned i = 0; i < DetailInterval * 2; ++i) Check(!budget.Admit(InitialControl, 1, true), "more than one sample per frame");
		unsigned count = 0;
		for (unsigned i = 0; i < DetailInterval; ++i) count += budget.Admit(InitialControl, 2, true);
		Check(count == 1, "sample stride changed");
		auto noDetails = ChangeControl(InitialControl, 2, false);
		Check(!budget.Admit(noDetails, 3, true), "counts-only sampled details");
		Check(budget.Admit(ChangeControl(noDetails, 2, true), 3, true), "new generation did not reset sample admission");
		Check(!budget.Admit(ChangeControl(InitialControl, 1, false), 4, true), "disabled counts sampled details");
	}

	void TestNativeReads()
	{
		auto memory = std::make_unique<Memory>();
		memory->Init();
		const auto original = memory->bytes;
		auto detail = memory->Start();
		Check(detail.structure.header.valid && detail.structure.copied == 6, "verified native structure not read");
		Check(detail.BeforeSphere(Routine::SphereNotFullyInside, Memory::Planes, *memory), "opcode 8 plane operand not associated");
		memory->Put(Memory::Planes + 0x60, 7u);
		detail.AfterSphere(Memory::Planes, true, *memory);
		detail.Finish(false, *memory);
		Check(detail.terminalVerified && detail.headerStable && !detail.accepted && detail.terminalOpcode == 3, "observed true sphere branch did not reach native rejection");
		Check(detail.steps[0].beforeMask == 63 && detail.steps[0].afterMask == 7 && detail.planes[0].bits[24] == 63, "active masks or plane snapshot wrong");
		memory->Put(Memory::Planes + 0x60, 63u);
		Check(memory->bytes == original, "diagnostic reader modified native memory");
		memory->Put(Memory::Owner + 0x200 + 0xF0, 0u);
		auto zero = memory->Start();
		zero.boundValid = true;
		zero.Finish(false, *memory);
		Check(zero.completionKind == CompletionKind::ZeroBound, "zero-bound native early rejection unexplained");
		memory->Put(Memory::Owner + 0xBC, 0u);
		memory->Put(Memory::Owner + 0x200 + 0xF0, 1u);
		auto empty = memory->Start();
		empty.boundValid = true;
		empty.boundBits[3] = 1;
		empty.Finish(true, *memory);
		Check(empty.completionKind == CompletionKind::EmptyProgram, "empty-program native early acceptance unexplained");
		memory->Init();

		detail = memory->Start();
		Check(detail.BeforeSphere(Routine::SphereNotFullyInside, Memory::Planes, *memory), "second traversal invalid");
		detail.AfterSphere(Memory::Planes, false, *memory);
		detail.Finish(true, *memory);
		Check(detail.terminalVerified && detail.terminalOpcode == 2, "false sphere branch was mistaken for rejection");
		memory->Init(8, 1);
		memory->Op(0, { 4, 2, 6 });
		memory->Op(6, { 8, 2, 4 });
		memory->Op(7, { 0, 0, 0 });
		detail = memory->Start();
		Check(detail.BeforeSphere(Routine::SphereNotFullyInside, Memory::Planes, *memory), "verified control operator before sphere was omitted");
		detail.AfterSphere(Memory::Planes, true, *memory);
		detail.Finish(false, *memory);
		Check(detail.terminalVerified && detail.stepCount == 2 && detail.steps[0].opcode == 4, "control edge/native sphere result join wrong");
		memory->Init();
		memory->Op(0, { 8, 2, 5 });
		memory->Op(5, { 5, 2, 4 });
		detail = memory->Start();
		Check(detail.BeforeSphere(Routine::SphereNotFullyInside, Memory::Planes, *memory), "control-tail fixture invalid");
		detail.AfterSphere(Memory::Planes, false, *memory);
		detail.Finish(true, *memory);
		Check(detail.terminalVerified && detail.stepCount == 2 && detail.steps[1].opcode == 5, "control edge before terminal was omitted");
		memory->Init();

		detail = memory->Start();
		detail.BeforeSphere(Routine::SphereNotFullyInside, Memory::Planes, *memory);
		detail.AfterSphere(Memory::Planes, false, *memory);
		detail.Finish(false, *memory);
		Check(!detail.terminalVerified, "contradictory terminal accepted");
		detail = memory->Start();
		Check(!detail.BeforeSphere(Routine::SphereIntersect, Memory::Planes, *memory) && !detail.chainValid, "routine/opcode mismatch guessed");
		detail = memory->Start();
		Check(!detail.BeforeSphere(Routine::SphereNotFullyInside, Memory::Planes + 0x70, *memory), "plane identity mismatch guessed");
		memory->Op(0, { 9, 2, 4 });
		detail = memory->Start();
		Check(!detail.BeforeSphere(Routine::SphereNotFullyInside, Memory::Planes, *memory), "unknown opcode guessed");
		memory->Init();
		memory->Op(1, { 1, 0, 0 });
		detail = memory->Start();
		Check(!detail.BeforeSphere(Routine::SphereNotFullyInside, Memory::Planes, *memory), "out-of-range plane read");
		memory->Init();
		detail = memory->Start();
		detail.cursor = 99;
		Check(!detail.BeforeSphere(Routine::SphereNotFullyInside, Memory::Planes, *memory) && detail.readFault, "out-of-range operator read");
		memory->Init();
		detail = memory->Start();
		detail.BeforeSphere(Routine::SphereNotFullyInside, Memory::Planes, *memory);
		detail.AfterSphere(Memory::Planes, true, *memory);
		memory->Put(Memory::Owner + 0xC0, 4u);
		detail.Finish(false, *memory);
		Check(!detail.headerStable && !detail.terminalVerified, "mutating construction was treated as stable");
		memory->Init();
		memory->Put(Memory::Owner + 0xB8, 2u);
		Check(!ReadHeader(Memory::Owner, *memory).valid, "used count exceeded storage");
		Check(!ReadHeader(UINTPTR_MAX, *memory).valid && !ReadHeader(1, *memory).valid, "invalid owner accepted");
		std::uintptr_t address = 0;
		Check(!ArrayAddress(UINTPTR_MAX - 8, 2, 12, address) && !ArrayAddress(0, 0, 12, address), "address overflow accepted");
		memory->Init();
		memory->Op(0, { 8, 0, 0 });
		detail = memory->Start();
		for (std::size_t i = 0; i < DetailSteps; ++i) {
			Check(detail.BeforeSphere(Routine::SphereNotFullyInside, Memory::Planes, *memory), "bounded repeated operator failed");
			detail.AfterSphere(Memory::Planes, true, *memory);
		}
		Check(!detail.BeforeSphere(Routine::SphereNotFullyInside, Memory::Planes, *memory) && detail.stepLimit, "unbounded operator trace");
		Check(detail.planeCount == 1, "repeated same plane copied repeatedly");
		memory->Init();
		memory->Op(0, { 4, 0, 0 });
		detail = memory->Start();
		Check(!detail.BeforeSphere(Routine::SphereNotFullyInside, Memory::Planes, *memory) && detail.stepLimit && detail.stepCount == DetailSteps, "cyclic control graph was not bounded");

		memory->Init(80, 40);
		detail = memory->Start();
		for (std::uint32_t i = 0; i < 33; ++i) {
			memory->Op(i * 2, { 7, (i + 1) * 2, 79 });
			memory->Op(i * 2 + 1, { i, 0, 0 });
			Check(detail.BeforeSphere(Routine::SphereIntersect, Memory::Planes + i * 0x70, *memory), "plane-limit fixture invalid");
			detail.AfterSphere(Memory::Planes + i * 0x70, true, *memory);
		}
		Check(detail.planeLimit && detail.planeCount == DetailPlanes && detail.stepCount == 33, "plane limit stopped aggregate/operator evidence");
		memory->Init(200, 1);
		auto structure = ReadStructure(Memory::Owner, *memory);
		Check(structure.truncated && structure.copied == DetailSteps, "large operator storage not bounded");
		memory->Op(180, { 7, 197, 198 });
		auto appendedOps = ReadStructure(Memory::Owner, *memory, 180);
		Check(appendedOps.start == 180 && appendedOps.copied == 20 && !appendedOps.truncated && appendedOps.operators[0].onTrue == 197, "appended operators outside prefix lost");
		Check(ReadStructure(Memory::Owner, *memory, 201).readFault, "invalid appended operator range accepted");

		memory->Init(80, 40);
		auto header = ReadHeader(Memory::Owner, *memory);
		auto appended = ReadPlanes(header, 38, *memory);
		Check(appended.count == 2 && appended.planes[0].index == 38 && !appended.truncated && !appended.readFault, "appended plane range wrong");
		auto limited = ReadPlanes(header, 0, *memory);
		Check(limited.count == DetailPlanes && limited.truncated, "construction plane copy unbounded");
		Check(ReadPlanes(header, 41, *memory).readFault, "invalid appended plane start accepted");
	}

	void TestChangingNativeEvidence()
	{
		auto memory = std::make_unique<Memory>();
		memory->Init(8, 1);
		auto detail = memory->Start();
		Check(detail.BeforeSphere(Routine::SphereNotFullyInside, Memory::Planes, *memory), "mutable operator fixture invalid");
		memory->Op(0, { 8, 6, 4 });
		memory->Op(6, { 3, 0, 0 });
		detail.AfterSphere(Memory::Planes, true, *memory);
		detail.Finish(false, *memory);
		Check(!detail.terminalVerified && !detail.chainValid && detail.operatorChanged, "changed operator falsely verified a cached branch");
		memory->Init();
		detail = memory->Start();
		detail.object = Memory::Owner + 0x200;
		detail.boundValid = true;
		detail.boundBits.fill(0);
		memory->Put(detail.object + 0xF0, 1u);
		detail.Finish(false, *memory);
		Check(detail.completionKind == CompletionKind::Unverified, "changed bound falsely explained a zero-bound return");
		Check(!detail.boundStable, "changed bound was reported stable");
		memory->Init();
		detail = memory->Start();
		Check(detail.BeforeSphere(Routine::SphereNotFullyInside, Memory::Planes, *memory), "mutable plane operand fixture invalid");
		memory->Op(1, { 1, 0, 0 });
		detail.AfterSphere(Memory::Planes, true, *memory);
		detail.Finish(false, *memory);
		Check(detail.operatorChanged && !detail.terminalVerified, "changed plane operand falsely verified a chain");
		memory->Init();
		memory->Put(Memory::Owner + 0x200 + 0xF0, 0u);
		detail = memory->Start();
		detail.totals[0].calls = 1;
		detail.Finish(false, *memory);
		Check(detail.completionKind == CompletionKind::Unverified, "unretained sphere call falsely classified as an early return");
	}

	void TestConcurrentPublication()
	{
		auto counters = std::make_unique<ThreadCounters>();
		auto ring = std::make_unique<SnapshotRing<std::array<std::uint64_t, 2>, 4>>();
		for (std::uint64_t i = 0; i < 6; ++i) Check(ring->Publish({ i, i }), "uncontended sample dropped");
		SnapshotRing<std::array<std::uint64_t, 2>, 4>::Snapshot copy;
		Check(ring->TrySnapshot(copy) && copy.published == 6, "ring snapshot unavailable");
		Check(ring->Publish({ 6, 6 }) && copy.published == 6, "copied ring metadata changed with later publication");
		for (auto entry : copy) Check(entry.sequence >= 3 && entry.sequence <= 6, "ring eviction incorrect");
		std::atomic_bool done = false;
		const RowKey key{ Routine::Compound, Pass::Depth, 0, 100 };
		std::jthread writer([&] {for(std::uint64_t i=0;i<10000;++i){auto* row=counters->Resolve(key);row->Increment(Counter::Calls);Forward(row,[]{return true;});ring->Publish({i,i});}done.store(true,std::memory_order_release); });
		while (!done.load(std::memory_order_acquire)) {
			for (const auto& row : counters->rows)
				if (row.published.load(std::memory_order_acquire)) {
					Check(row.key == key, "row identity published incompletely");
					for (const auto& value : row.counters) Check(value <= 10000, "torn counter");
				}
			if (ring->TrySnapshot(copy))
				for (const auto& e : copy) Check(e.value[0] == e.value[1] && e.sequence <= copy.published, "torn diagnostic sample or metadata");
		}
		writer.join();
		Check(ring->published + ring->dropped == 10007, "sample contention loss unaccounted");
		auto* row = counters->Resolve(key);
		Check(Value(*row, Counter::Calls) == 10000 && Value(*row, Counter::Completed) == 10000, "snapshot reader lost counts");
	}
}
int main()
{
	TestBatchedWork();
	TestContexts();
	TestNativeReads();
	TestChangingNativeEvidence();
	TestConcurrentPublication();
}
