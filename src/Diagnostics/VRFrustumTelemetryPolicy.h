#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include <array>
#	include <atomic>
#	include <cstddef>
#	include <cstdint>

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
		FirstAddressKeys,
		RepeatedAddressKeys,
		TrackingOverflow,
		UnknownFrame,
		Count
	};
	inline constexpr std::size_t CounterCount = static_cast<std::size_t>(Counter::Count);
	inline constexpr std::size_t RowCapacity = 128;
	inline constexpr std::size_t IdentityCapacity = 4096;
	inline constexpr std::size_t ProbeLimit = 8;
	inline constexpr std::int64_t UnknownCameraIndex = -1;

	struct Context
	{
		Pass pass = Pass::Unknown;
		std::int64_t cameraIndex = UnknownCameraIndex;
		std::uint64_t generation = 0;
	};

	/** Context observed before a collection toggle cannot identify work after it. */
	inline Context ActiveContext(const Context& a_context, std::uint64_t a_control) noexcept
	{
		return (a_control & 1) != 0 && a_context.generation == (a_control >> 1) ? a_context : Context{};
	}

	/** Nest context on the calling thread; worker jobs must not inherit global pass state. */
	class ContextScope
	{
	public:
		ContextScope(Context& a_current, Context a_next) : current(a_current), previous(a_current) { current = a_next; }
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

	inline std::size_t Hash(std::uint64_t a_value) noexcept
	{
		a_value ^= a_value >> 30;
		a_value *= 0xbf58476d1ce4e5b9ULL;
		a_value ^= a_value >> 27;
		a_value *= 0x94d049bb133111ebULL;
		return static_cast<std::size_t>(a_value ^ (a_value >> 31));
	}

	struct Row
	{
		std::atomic_bool published{ false };
		RowKey key{};
		std::array<std::atomic_uint64_t, CounterCount> counters{};

		// Each row has one owning thread; readers only inspect atomic counters.
		void Increment(Counter a_counter) noexcept
		{
			auto& value = counters[static_cast<std::size_t>(a_counter)];
			value.store(value.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
		}
		void Complete(bool a_result) noexcept
		{
			Increment(a_result ? Counter::RawTrue : Counter::RawFalse);
			Increment(Counter::Completed);
		}
	};

	struct AddressKey
	{
		std::uint64_t generation = 0;
		std::uint32_t frame = 0;
		std::size_t row = 0;
		std::uintptr_t owner = 0;
		std::uintptr_t objectOrBound = 0;
		std::uintptr_t planes = 0;
		bool operator==(const AddressKey&) const = default;
	};

	/** Forward native arguments and the raw boolean unchanged, even when collection is unavailable. */
	template <class F, class... Args>
	bool Forward(Row* a_row, F a_function, Args... a_args)
	{
		const bool result = a_function(a_args...);
		if (a_row)
			a_row->Complete(result);
		return result;
	}

	/** Single-writer, bounded tracking; a repeated address is evidence, never an admission cache. */
	class ThreadCounters
	{
	public:
		std::array<Row, RowCapacity> rows{};
		std::atomic_uint64_t rowOverflow{ 0 };
		std::atomic_uint64_t observedFrames{ 0 };
		std::atomic_uint32_t firstObservedFrame{ 0 };
		std::atomic_uint32_t lastObservedFrame{ 0 };

		Row* Begin(RowKey a_key, AddressKey a_address, bool a_frameKnown) noexcept
		{
			if (a_frameKnown) {
				const auto observed = observedFrames.load(std::memory_order_relaxed);
				if (observed == 0)
					firstObservedFrame.store(a_address.frame, std::memory_order_relaxed);
				if (observed == 0 || lastObservedFrame.load(std::memory_order_relaxed) != a_address.frame) {
					lastObservedFrame.store(a_address.frame, std::memory_order_relaxed);
					observedFrames.store(observed + 1, std::memory_order_relaxed);
				}
			}
			const auto start = Hash(a_key.caller ^ (static_cast<std::uint64_t>(a_key.pass) << 8) ^
									(static_cast<std::uint64_t>(a_key.cameraIndex) << 16) ^ static_cast<std::uint64_t>(a_key.routine));
			for (std::size_t probe = 0; probe < ProbeLimit; ++probe) {
				const auto index = (start + probe) % rows.size();
				auto& row = rows[index];
				if (!row.published.load(std::memory_order_relaxed)) {
					row.key = a_key;
					row.published.store(true, std::memory_order_release);
				} else if (row.key != a_key) {
					continue;
				}
				row.Increment(Counter::Calls);
				a_address.row = index;
				row.Increment(a_frameKnown ? Track(a_address) : Counter::UnknownFrame);
				return &row;
			}
			rowOverflow.store(rowOverflow.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
			return nullptr;
		}

	private:
		std::array<AddressKey, IdentityCapacity> identities{};

		Counter Track(const AddressKey& a_key) noexcept
		{
			const auto start = Hash(a_key.owner ^ a_key.objectOrBound ^ (a_key.planes << 1) ^ a_key.row);
			for (std::size_t probe = 0; probe < ProbeLimit; ++probe) {
				auto& entry = identities[(start + probe) % identities.size()];
				if (entry.generation != a_key.generation || entry.frame != a_key.frame) {
					entry = a_key;
					return Counter::FirstAddressKeys;
				}
				if (entry == a_key)
					return Counter::RepeatedAddressKeys;
			}
			return Counter::TrackingOverflow;
		}
	};
}

#endif
