#pragma once
#ifdef DEVBENCH_BRIDGE_ENABLED
#	include <array>
#	include <atomic>
#	include <cstddef>
#	include <cstdint>
#	include <limits>
#	include <mutex>
namespace VRFrustumTelemetry
{
	enum class Routine : std::uint8_t
	{
		Compound,
		SphereIntersect,
		SphereNotFullyInside
	};
	enum class Pass : std::uint8_t
	{
		Unknown,
		PlayerView,
		Depth,
		World,
		FirstPerson,
		Water,
		Shadowmask,
		DirectionalShadow,
		SpotShadow,
		PointShadow,
		Cubemap,
		Recovery,
		Count
	};
	enum class Counter : std::size_t
	{
		Calls,
		Completed,
		RawTrue,
		RawFalse,
		Count
	};
	inline constexpr std::size_t CounterCount = static_cast<std::size_t>(Counter::Count), RowCapacity = 128, ProbeLimit = 8, DetailSteps = 128, DetailOperators = 256, DetailPlanes = DetailSteps, DetailRingCapacity = 4;
	inline constexpr std::uint32_t DetailInterval = 256, ConstructionInterval = 16, UnknownIndex = UINT32_MAX;
	inline constexpr std::int64_t UnknownCameraIndex = -1;
	inline constexpr std::uint64_t InitialControl = 7;
	inline constexpr std::uint64_t Generation(std::uint64_t c) { return c >> 2; }
	inline constexpr std::uint64_t ChangeControl(std::uint64_t c, std::uint64_t bit, bool enabled) { return ((c & bit) != 0) == enabled ? c : ((Generation(c) + 1) << 2) | ((c & 3 & ~bit) | (enabled ? bit : 0)); }
	struct Context
	{
		Pass pass = Pass::Unknown;
		std::int64_t cameraIndex = UnknownCameraIndex;
		std::uint64_t generation = 0;
	};
	inline Context ActiveContext(const Context& c, std::uint64_t control) noexcept { return (control & 1) && c.generation == Generation(control) ? c : Context{}; }
	class ContextScope
	{
	public:
		ContextScope(Context& c, Context next) : current(c), previous(c) { current = next; }
		~ContextScope() { current = previous; }
		ContextScope(const ContextScope&) = delete;
		ContextScope& operator=(const ContextScope&) = delete;

	private:
		Context& current;
		Context previous;
	};
	struct RowKey
	{
		Routine routine{};
		Pass pass{};
		std::int64_t cameraIndex = UnknownCameraIndex;
		std::uintptr_t caller = 0;
		bool operator==(const RowKey&) const = default;
	};
	inline std::size_t Hash(std::uint64_t v) noexcept
	{
		v ^= v >> 30;
		v *= 0xbf58476d1ce4e5b9ULL;
		v ^= v >> 27;
		v *= 0x94d049bb133111ebULL;
		return static_cast<std::size_t>(v ^ (v >> 31));
	}
	inline void Add(std::atomic_uint64_t& v, std::uint64_t n = 1) noexcept { v.store(v.load(std::memory_order_relaxed) + n, std::memory_order_relaxed); }
	struct Totals
	{
		std::uint64_t calls = 0, completed = 0, rawTrue = 0;
		void Complete(bool r) noexcept
		{
			++completed;
			rawTrue += r;
		}
	};
	struct Row
	{
		std::atomic_bool published{ false };
		RowKey key{};
		std::array<std::atomic_uint64_t, CounterCount> counters{};
		// One owning writer; outcome totals measure native work, not saved draws.
		std::array<std::array<std::atomic_uint64_t, 2>, 2> sphereCallsByOutcome{};
		std::array<std::atomic_uint64_t, 2> zeroTestObjectsByOutcome{};
		void Increment(Counter c) noexcept { Add(counters[static_cast<std::size_t>(c)]); }
		void Complete(bool r) noexcept
		{
			Increment(r ? Counter::RawTrue : Counter::RawFalse);
			Increment(Counter::Completed);
		}
		void Merge(const Totals& t) noexcept
		{
			Add(counters[0], t.calls);
			Add(counters[1], t.completed);
			Add(counters[2], t.rawTrue);
			Add(counters[3], t.completed - t.rawTrue);
		}
	};
	/** Cache immutable row identities; sphere batches publish once per object. */
	class ThreadCounters
	{
	public:
		std::array<Row, RowCapacity> rows{};
		std::atomic_uint64_t rowOverflow{ 0 }, rowLookups{ 0 }, unattributedCalls{ 0 }, observedFrames{ 0 };
		std::atomic_uint32_t firstObservedFrame{ 0 }, lastObservedFrame{ 0 };
		void ObserveFrame(std::uint32_t frame, bool known) noexcept
		{
			if (!known)
				return;
			const auto n = observedFrames.load(std::memory_order_relaxed);
			if (!n)
				firstObservedFrame.store(frame, std::memory_order_relaxed);
			if (!n || lastObservedFrame.load(std::memory_order_relaxed) != frame) {
				lastObservedFrame.store(frame, std::memory_order_relaxed);
				Add(observedFrames);
			}
		}
		Row* Resolve(RowKey key) noexcept
		{
			auto& cache = lastRows[static_cast<std::size_t>(key.routine)];
			if (cache && cache->key == key)
				return cache;
			Add(rowLookups);
			const auto start = Hash(key.caller ^ (static_cast<std::uint64_t>(key.pass) << 8) ^ (static_cast<std::uint64_t>(key.cameraIndex) << 16) ^ static_cast<std::uint64_t>(key.routine));
			for (std::size_t p = 0; p < ProbeLimit; ++p) {
				auto& r = rows[(start + p) % rows.size()];
				if (!r.published.load(std::memory_order_relaxed)) {
					r.key = key;
					r.published.store(true, std::memory_order_release);
				} else if (r.key != key)
					continue;
				return cache = &r;
			}
			Add(rowOverflow);
			return nullptr;
		}

	private:
		std::array<Row*, 3> lastRows{};
	};
	template <class F, class... Args>
	bool Forward(Row* r, F f, Args... args)
	{
		const bool result = f(args...);
		if (r)
			r->Complete(result);
		return result;
	}
	struct Traversal
	{
		std::uintptr_t owner = 0, bound = 0;
		std::atomic_uint64_t* lost = nullptr;
		Row* compound = nullptr;
		std::array<Row*, 2> sphereRows{};
		std::array<Totals, 2> sphere{};
		void Complete(bool accepted) noexcept
		{
			if (!compound)
				return;
			compound->Complete(accepted);
			for (std::size_t i = 0; i < 2; ++i) Add(compound->sphereCallsByOutcome[accepted][i], sphere[i].calls);
			if (!sphere[0].calls && !sphere[1].calls)
				Add(compound->zeroTestObjectsByOutcome[accepted]);
		}
		void Publish() noexcept
		{
			for (std::size_t i = 0; i < 2; ++i)
				if (sphereRows[i])
					sphereRows[i]->Merge(sphere[i]);
				else if (lost)
					Add(*lost, sphere[i].calls);
		}
	};
	class TraversalScope
	{
	public:
		TraversalScope(Traversal*& c, Traversal* next) : current(c), previous(c), active(next) { current = next; }
		~TraversalScope()
		{
			if (active)
				active->Publish();
			current = previous;
		}
		TraversalScope(const TraversalScope&) = delete;
		TraversalScope& operator=(const TraversalScope&) = delete;

	private:
		Traversal*& current;
		Traversal* previous;
		Traversal* active;
	};
	struct SamplingBudget
	{
		std::uint64_t generation = 0, attempts = 0;
		std::uint32_t lastFrame = 0;
		bool sampledFrame = false;
		bool Admit(std::uint64_t control, std::uint32_t frame, bool known, std::uint32_t interval = DetailInterval) noexcept
		{
			if ((control & 3) != 3 || !known || !interval)
				return false;
			if (generation != Generation(control)) {
				generation = Generation(control);
				attempts = 0;
				sampledFrame = false;
			}
			if ((attempts++ % interval) != 0 || (sampledFrame && lastFrame == frame))
				return false;
			lastFrame = frame;
			sampledFrame = true;
			return true;
		}
	};
	struct NativeHeader
	{
		std::uintptr_t planes = 0, operators = 0;
		std::uint32_t planeStorage = 0, operatorStorage = 0, planeCount = 0, operatorCount = 0, firstOp = 0;
		std::uint8_t prethreaded = 0;
		bool valid = false;
		bool operator==(const NativeHeader&) const = default;
	};
	struct NativeOperator
	{
		std::uint32_t opcode = 0, onTrue = 0, onFalse = 0;
	};
	static_assert(sizeof(NativeOperator) == 12);
	inline bool ArrayAddress(std::uintptr_t base, std::uint32_t index, std::size_t stride, std::uintptr_t& address) noexcept
	{
		if (!base || !stride || index > (std::numeric_limits<std::uintptr_t>::max() - base) / stride)
			return false;
		address = base + index * stride;
		return address <= std::numeric_limits<std::uintptr_t>::max() - stride;
	}
	/** Only read layouts verified in the live snapshot; unavailable metadata never changes admission. */
	template <class Read>
	NativeHeader ReadHeader(std::uintptr_t owner, Read&& read)
	{
		NativeHeader h;
		if (!owner || owner > std::numeric_limits<std::uintptr_t>::max() - 0xC8)
			return h;
		if (!read(owner, &h.planes, 8) || !read(owner + 0x18, &h.operators, 8) || !read(owner + 0x10, &h.planeStorage, 4) || !read(owner + 0x28, &h.operatorStorage, 4) || !read(owner + 0xB8, &h.planeCount, 4) || !read(owner + 0xBC, &h.operatorCount, 4) || !read(owner + 0xC0, &h.firstOp, 4) || !read(owner + 0xC5, &h.prethreaded, 1))
			return h;
		h.valid = h.planeCount <= h.planeStorage && h.operatorCount <= h.operatorStorage && h.planeStorage <= 1'000'000 && h.operatorStorage <= 1'000'000 && (!h.planeCount || h.planes) && (!h.operatorCount || (h.operators && h.firstOp < h.operatorCount));
		return h;
	}
	template <class Read>
	bool ReadOperator(const NativeHeader& h, std::uint32_t i, NativeOperator& op, Read&& read)
	{
		std::uintptr_t a;
		return h.valid && i < h.operatorCount && ArrayAddress(h.operators, i, sizeof(op), a) && read(a, &op, sizeof(op));
	}
	/** Native branch targets can name terminal slots beyond the constructed body. */
	template <class Read>
	bool ReadInstruction(const NativeHeader& h, std::uint32_t i, NativeOperator& op, Read&& read)
	{
		if (i < h.operatorCount)
			return ReadOperator(h, i, op, read);
		std::uintptr_t address;
		return h.valid && i < h.operatorStorage && ArrayAddress(h.operators, i, sizeof(op), address) &&
		       read(address, &op, sizeof(op)) && (op.opcode == 2 || op.opcode == 3);
	}
	struct Structure
	{
		NativeHeader header;
		std::array<NativeOperator, DetailOperators> operators{};
		std::uint32_t start = 0, copied = 0;
		bool readFault = false, truncated = false;
	};
	template <class Read>
	Structure ReadStructure(std::uintptr_t owner, Read&& read, std::uint32_t start = 0)
	{
		Structure s;
		s.start = start;
		s.header = ReadHeader(owner, read);
		if (!s.header.valid || start > s.header.operatorCount) {
			s.readFault = true;
			return s;
		}
		const auto remaining = s.header.operatorCount - start;
		const auto n = remaining < DetailOperators ? remaining : static_cast<std::uint32_t>(DetailOperators);
		if (n) {
			std::uintptr_t begin, end;
			if (!ArrayAddress(s.header.operators, start, sizeof(NativeOperator), begin) || !ArrayAddress(s.header.operators, start + n - 1, sizeof(NativeOperator), end) || !read(begin, s.operators.data(), n * sizeof(NativeOperator)))
				s.readFault = true;
			else
				s.copied = n;
		}
		s.truncated = s.copied < remaining;
		return s;
	}
	struct Step
	{
		std::uint32_t op = UnknownIndex, opcode = 0, plane = UnknownIndex, onTrue = 0, onFalse = 0, beforeMask = 0, afterMask = 0;
		bool result = false, maskValid = false;
	};
	struct PlaneSample
	{
		std::uint32_t index = UnknownIndex;
		std::array<std::uint32_t, 28> bits{};
	};
	/** Preserve bounded plane bytes without interpreting or changing native masks. */
	template <class Read>
	bool ReadPlane(const NativeHeader& header, std::uint32_t index, PlaneSample& plane, Read&& read)
	{
		std::uintptr_t address;
		plane.index = index;
		return header.valid && index < header.planeCount && ArrayAddress(header.planes, index, 0x70, address) && read(address, plane.bits.data(), 0x70);
	}
	struct PlaneRange
	{
		std::array<PlaneSample, DetailPlanes> planes{};
		std::uint32_t start = 0, count = 0;
		bool readFault = false, truncated = false;
	};
	template <class Read>
	PlaneRange ReadPlanes(const NativeHeader& header, std::uint32_t start, Read&& read)
	{
		PlaneRange range;
		range.start = start;
		if (!header.valid || start > header.planeCount) {
			range.readFault = true;
			return range;
		}
		while (range.count < DetailPlanes && start + range.count < header.planeCount) {
			if (!ReadPlane(header, start + range.count, range.planes[range.count], read)) {
				range.readFault = true;
				break;
			}
			++range.count;
		}
		range.truncated = start + range.count < header.planeCount;
		return range;
	}
	template <class Read>
	bool ReadBound(std::uintptr_t object, std::array<std::uint32_t, 4>& bits, Read&& read)
	{
		return object && object <= std::numeric_limits<std::uintptr_t>::max() - 0xF4 && read(object + 0xE4, bits.data(), sizeof(bits));
	}
	enum class CompletionKind : std::uint8_t
	{
		Unverified,
		ZeroBound,
		EmptyProgram,
		Operator
	};
	struct Detail
	{
		std::uint64_t generation = 0, generationAtEnd = 0;
		std::uint32_t frame = 0;
		std::uintptr_t owner = 0, object = 0;
		RowKey context;
		Structure structure;
		std::array<std::uint32_t, 4> boundBits{};
		bool boundValid = false;
		CompletionKind completionKind = CompletionKind::Unverified;
		std::array<Step, DetailSteps> steps{};
		std::array<PlaneSample, DetailPlanes> planes{};
		std::uint32_t stepCount = 0, planeCount = 0, cursor = UnknownIndex, terminalOpcode = 0;
		bool operatorChanged = false, boundStable = false, headerStable = false, chainValid = true, stepLimit = false, planeLimit = false, readFault = false, completed = false, accepted = false, terminalVerified = false;
		std::array<Totals, 2> totals{};
		template <class Read>
		bool ReadNextObservedOperator(NativeOperator& op, Read&& read)
		{
			while (chainValid) {
				if (!ReadInstruction(structure.header, cursor, op, read)) {
					readFault = true;
					chainValid = false;
					return false;
				}
				if (op.opcode == 2 || op.opcode == 3 || op.opcode == 7 || op.opcode == 8)
					return true;
				if (op.opcode < 4 || op.opcode > 6) {
					chainValid = false;
					return false;
				}
				if (stepCount == DetailSteps) {
					stepLimit = true;
					chainValid = false;
					return false;
				}
				// Verified control opcodes take +8 without calling a sphere routine.
				steps[stepCount++] = { cursor, op.opcode, UnknownIndex, op.onTrue, op.onFalse };
				cursor = op.onFalse;
			}
			return false;
		}
		template <class Read>
		bool BeforeSphere(Routine routine, std::uintptr_t address, Read&& read)
		{
			if (!chainValid || stepCount == DetailSteps) {
				stepLimit |= stepCount == DetailSteps;
				chainValid = false;
				return false;
			}
			const auto& h = structure.header;
			NativeOperator op, operand;
			std::uintptr_t expected;
			if (!ReadNextObservedOperator(op, read))
				return false;
			if (stepCount == DetailSteps) {
				stepLimit = true;
				chainValid = false;
				return false;
			}
			if (cursor == UINT32_MAX || !ReadOperator(h, cursor + 1, operand, read)) {
				readFault = true;
				chainValid = false;
				return false;
			}
			const auto code = routine == Routine::SphereIntersect ? 7u : 8u;
			if (op.opcode != code || operand.opcode >= h.planeCount || !ArrayAddress(h.planes, operand.opcode, 0x70, expected) || expected != address) {
				chainValid = false;
				return false;
			}
			auto& step = steps[stepCount];
			step = { cursor, code, operand.opcode, op.onTrue, op.onFalse };
			step.maskValid = read(address + 0x60, &step.beforeMask, 4);
			readFault |= !step.maskValid;
			bool seen = false;
			for (std::uint32_t i = 0; i < planeCount; ++i) seen |= planes[i].index == step.plane;
			if (!seen) {
				if (planeCount == DetailPlanes)
					planeLimit = true;
				else {
					auto& p = planes[planeCount];
					if (ReadPlane(h, step.plane, p, read))
						++planeCount;
					else
						readFault = true;
				}
			}
			return true;
		}
		template <class Read>
		void AfterSphere(std::uintptr_t address, bool result, Read&& read)
		{
			auto& s = steps[stepCount++];
			s.result = result;
			NativeOperator op, operand;
			if (!ReadOperator(structure.header, s.op, op, read) || !ReadOperator(structure.header, s.op + 1, operand, read)) {
				readFault = true;
				chainValid = false;
			} else if (op.opcode != s.opcode || op.onTrue != s.onTrue || op.onFalse != s.onFalse || operand.opcode != s.plane) {
				operatorChanged = true;
				chainValid = false;
			}
			s.maskValid = read(address + 0x60, &s.afterMask, 4) && s.maskValid;
			readFault |= !s.maskValid;
			cursor = result ? s.onTrue : s.onFalse;
		}
		template <class Read>
		void Finish(bool result, Read&& read)
		{
			completed = true;
			accepted = result;
			const auto end = ReadHeader(owner, read);
			headerStable = end.valid && end == structure.header;
			readFault |= !end.valid;
			std::array<std::uint32_t, 4> endBound{};
			const bool endBoundValid = ReadBound(object, endBound, read);
			boundStable = boundValid && endBoundValid && endBound == boundBits;
			readFault |= !endBoundValid;
			if (!boundStable)
				chainValid = false;
			const bool withoutTests = !stepCount && !totals[0].calls && !totals[1].calls;
			if (boundStable && boundBits[3] == 0 && !result && withoutTests) {
				completionKind = CompletionKind::ZeroBound;
				return;
			}
			if (headerStable && boundStable && boundBits[3] != 0 && !structure.header.operatorCount && result && withoutTests) {
				completionKind = CompletionKind::EmptyProgram;
				return;
			}
			NativeOperator op;
			if (headerStable && chainValid && (stepCount || (boundValid && boundBits[3] != 0))) {
				if (!ReadNextObservedOperator(op, read))
					return;
				terminalOpcode = op.opcode;
				terminalVerified = (op.opcode == 2 && result) || (op.opcode == 3 && !result);
				if (terminalVerified)
					completionKind = CompletionKind::Operator;
			}
		}
	};
	/** Bounded diagnostic copies use a nonblocking lock, never a rendering dependency. */
	template <class T, std::size_t N = DetailRingCapacity>
	class SnapshotRing
	{
	public:
		struct Entry
		{
			std::uint64_t sequence = 0;
			T value{};
		};
		struct Snapshot
		{
			std::array<Entry, N> entries{};
			std::uint64_t published = 0, dropped = 0;
			auto begin() const { return entries.begin(); }
			auto end() const { return entries.end(); }
		};
		std::atomic_uint64_t dropped{ 0 }, published{ 0 };
		bool Publish(const T& value)
		{
			std::unique_lock lock(mutex, std::try_to_lock);
			if (!lock.owns_lock()) {
				Add(dropped);
				return false;
			}
			const auto seq = published.load(std::memory_order_relaxed) + 1;
			entries[(seq - 1) % N] = { seq, value };
			published.store(seq, std::memory_order_relaxed);
			return true;
		}
		bool TrySnapshot(Snapshot& copy)
		{
			std::unique_lock lock(mutex, std::try_to_lock);
			if (!lock.owns_lock())
				return false;
			copy.entries = entries;
			copy.published = published.load(std::memory_order_relaxed);
			copy.dropped = dropped.load(std::memory_order_relaxed);
			return true;
		}

	private:
		std::mutex mutex;
		std::array<Entry, N> entries{};
	};
}
#endif
