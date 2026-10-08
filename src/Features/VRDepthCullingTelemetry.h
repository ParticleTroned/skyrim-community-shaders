#pragma once

#ifdef DEVBENCH_BRIDGE_ENABLED

#	include "VRDepthCullingTelemetryPolicy.h"

#	include <array>
#	include <atomic>
#	include <chrono>
#	include <cstdint>
#	include <type_traits>

namespace VRDepthCullingTelemetry
{
	inline constexpr std::array<std::uint64_t, 16> DurationUpperBoundsNanoseconds{
		1'000, 2'000, 4'000, 8'000, 16'000, 32'000, 64'000, 128'000,
		256'000, 512'000, 1'000'000, 2'000'000, 4'000'000, 8'000'000, 16'000'000, 64'000'000
	};
	inline constexpr std::size_t DurationBinCount = DurationUpperBoundsNanoseconds.size() + 1;

	struct StageTiming
	{
		std::uint64_t samples = 0;
		std::uint64_t totalNanoseconds = 0;
		std::uint64_t maximumNanoseconds = 0;
		std::array<std::uint64_t, DurationBinCount> durationHistogram{};
	};

	/** Counters participate in the caller's shared writer admission and reset boundary. */
	class TimingCounters
	{
	public:
		void Add(std::uint64_t a_nanoseconds) noexcept
		{
			samples.fetch_add(1, std::memory_order_relaxed);
			total.fetch_add(a_nanoseconds, std::memory_order_relaxed);
			VRDepthCullingTelemetryPolicy::UpdateMaximum(maximum, a_nanoseconds);
			histogram[VRDepthCullingTelemetryPolicy::DurationBin(a_nanoseconds, DurationUpperBoundsNanoseconds)].fetch_add(1, std::memory_order_relaxed);
		}

		[[nodiscard]] StageTiming Read() const noexcept
		{
			StageTiming result{ samples.load(std::memory_order_relaxed), total.load(std::memory_order_relaxed), maximum.load(std::memory_order_relaxed) };
			for (std::size_t index = 0; index < histogram.size(); ++index)
				result.durationHistogram[index] = histogram[index].load(std::memory_order_relaxed);
			return result;
		}

		void Reset() noexcept
		{
			samples.store(0, std::memory_order_relaxed);
			total.store(0, std::memory_order_relaxed);
			maximum.store(0, std::memory_order_relaxed);
			for (auto& bin : histogram)
				bin.store(0, std::memory_order_relaxed);
		}

	private:
		std::atomic_uint64_t samples{ 0 }, total{ 0 }, maximum{ 0 };
		std::array<std::atomic_uint64_t, DurationBinCount> histogram{};
	};

	/** Clock work and publication occur only inside an admitted telemetry writer. */
	class Scope
	{
	public:
		Scope(TimingCounters& a_counters, VRDepthCullingTelemetryPolicy::WriterGate& a_gate) noexcept : writer(a_gate), counters(a_counters)
		{
			if (writer)
				started = std::chrono::steady_clock::now();
		}
		~Scope()
		{
			if (writer)
				counters.Add(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count()));
		}
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;
		explicit operator bool() const noexcept { return static_cast<bool>(writer); }

	private:
		VRDepthCullingTelemetryPolicy::WriterScope writer;
		TimingCounters& counters;
		std::chrono::steady_clock::time_point started{};
	};

	/** A single nonblocking slot; a missed publication invalidates the previously readable value. */
	template <class T>
	class SnapshotSlot
	{
	public:
		static_assert(std::is_trivially_copyable_v<T>);

		[[nodiscard]] bool Publish(const T& a_value) noexcept
		{
			const auto ticket = version.fetch_add(1, std::memory_order_acq_rel) + 1;
			if (busy.test_and_set(std::memory_order_acquire))
				return false;
			value = a_value;
			published = ticket;
			busy.clear(std::memory_order_release);
			return true;
		}

		[[nodiscard]] bool Read(T& a_value, bool* a_busy = nullptr) const noexcept
		{
			if (a_busy)
				*a_busy = false;
			const auto ticket = version.load(std::memory_order_acquire);
			if (busy.test_and_set(std::memory_order_acquire)) {
				if (a_busy)
					*a_busy = true;
				return false;
			}
			const bool available = published != 0 && published == ticket;
			if (available)
				a_value = value;
			busy.clear(std::memory_order_release);
			return available && version.load(std::memory_order_acquire) == ticket;
		}

		void Invalidate() noexcept { version.fetch_add(1, std::memory_order_release); }

	private:
		mutable std::atomic_flag busy = ATOMIC_FLAG_INIT;
		std::atomic_uint64_t version{ 0 };
		std::uint64_t published = 0;
		T value{};
	};
}

#endif
