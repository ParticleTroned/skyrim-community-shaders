#include "Features/VRDepthCullingTelemetry.h"
#include "Features/VRDepthCullingTelemetryPolicy.h"
#include "Features/VRDepthCullingTemporal.h"

#include <array>
#include <barrier>
#include <latch>
#include <limits>
#include <stdexcept>
#include <thread>

namespace
{
	void CheckNativeVisibilityObservation()
	{
		using namespace VRNativeVisibilityTelemetry;
		using namespace VRDepthCullingTelemetryPolicy;
		Counters counters;
		WriterGate gate;
		std::array<std::uint32_t, 5> results{ 0, 1, 0, 7, 0 };
		std::uint32_t reads = 0;
		const auto readBatch = [&] {
			++reads;
			return Batch{ results.data(), static_cast<std::uint32_t>(results.size()), 0 };
		};
		gate.SetEnabled(false);
		{
			const Scope disabled(counters, gate, readBatch);
		}
		if (reads != 0 || counters.Read().batches != 0)
			throw std::runtime_error("disabled native visibility telemetry inspected results");
		gate.SetEnabled(true);
		{
			const Scope observation(counters, gate, readBatch);
			if (TryReset(gate, [&]() noexcept { counters.Reset(); }))
				throw std::runtime_error("reset split native visibility before and after recovery");
			gate.SetEnabled(false);
			if (gate.IsFrozen())
				throw std::runtime_error("native visibility observation lost freeze exclusion");
			results[0] = 1;
		}
		const auto measured = counters.Read();
		if (!gate.IsFrozen() || reads != 1 || measured.batches != 1 || measured.testedObjects != 5 ||
			measured.occludedBeforeRecovery != 3 || measured.visibleBeforeRecovery != 2 ||
			measured.occludedAfterRecovery != 2 || measured.visibleAfterRecovery != 3)
			throw std::runtime_error("native visibility lost nonzero results or recovery promotions");
		if (!TryReset(gate, [&]() noexcept { counters.Reset(); }))
			throw std::runtime_error("native visibility counters could not reset after draining");
		gate.SetEnabled(true);
		for (const auto batch : { Batch{}, Batch{ nullptr, 2, 0 }, Batch{ results.data(), 5, 2 },
				 Batch{ results.data(), VRDepthCullingTemporalPolicy::kMaximumObjects + 1, 0 }, Batch{ nullptr, 0, 1 } }) {
			const Scope invalid(counters, gate, [batch] { return batch; });
		}
		const auto rejected = counters.Read();
		if (rejected.batches || rejected.testedObjects || rejected.occludedBeforeRecovery || rejected.visibleBeforeRecovery ||
			rejected.occludedAfterRecovery || rejected.visibleAfterRecovery || rejected.unreadableBatches != 4 || rejected.emptyBatches != 1)
			throw std::runtime_error("invalid native batches were scanned or reported as visible results");
		std::array<std::uint32_t, VRDepthCullingTemporalPolicy::kMaximumObjects> maximum{};
		maximum.back() = 1;
		{
			const Scope limit(counters, gate, [&] { return Batch{ maximum.data(), static_cast<std::uint32_t>(maximum.size()), 1 }; });
		}
		const auto boundary = counters.Read();
		if (boundary.batches != 1 || boundary.testedObjects != maximum.size() || boundary.occludedBeforeRecovery != maximum.size() - 1 ||
			boundary.visibleBeforeRecovery != 1 || boundary.occludedAfterRecovery != boundary.occludedBeforeRecovery || boundary.visibleAfterRecovery != 1)
			throw std::runtime_error("native visibility lost a maximum-sized batch");
		if (!TryReset(gate, [&]() noexcept { counters.Reset(); }))
			throw std::runtime_error("native visibility reset remained busy");
		const auto cleared = counters.Read();
		if (cleared.batches || cleared.emptyBatches || cleared.unreadableBatches || cleared.testedObjects ||
			cleared.occludedBeforeRecovery || cleared.visibleBeforeRecovery || cleared.occludedAfterRecovery || cleared.visibleAfterRecovery)
			throw std::runtime_error("native visibility reset retained counts");
	}

	void CheckStageTimingAdmissionAndDistribution()
	{
		using namespace VRDepthCullingTelemetry;
		TimingCounters counters;
		VRDepthCullingTelemetryPolicy::WriterGate gate;
		std::uint64_t total = 0;
		for (const auto bound : DurationUpperBoundsNanoseconds) {
			VRDepthCullingTelemetryPolicy::WriterScope writer(gate);
			counters.Add(bound);
			total += bound;
		}
		counters.Add(DurationUpperBoundsNanoseconds.back() + 1);
		total += DurationUpperBoundsNanoseconds.back() + 1;
		const auto measured = counters.Read();
		if (measured.samples != DurationBinCount || measured.totalNanoseconds != total ||
			measured.maximumNanoseconds != DurationUpperBoundsNanoseconds.back() + 1)
			throw std::runtime_error("stage timing aggregates lost a sample");
		for (const auto count : measured.durationHistogram)
			if (count != 1)
				throw std::runtime_error("stage histogram does not distinguish microsecond and millisecond work");
		{
			const Scope sample(counters, gate);
			if (!sample || VRDepthCullingTelemetryPolicy::TryReset(gate, [&]() noexcept { counters.Reset(); }))
				throw std::runtime_error("stage timing did not retain reset exclusion through scope completion");
		}
		if (counters.Read().samples != measured.samples + 1)
			throw std::runtime_error("admitted stage scope did not publish its sample");
		gate.SetEnabled(false);
		{
			const Scope sample(counters, gate);
			if (sample)
				throw std::runtime_error("disabled telemetry admitted a stage timer");
		}
		if (counters.Read().samples != measured.samples + 1 ||
			!VRDepthCullingTelemetryPolicy::TryReset(gate, [&]() noexcept { counters.Reset(); }))
			throw std::runtime_error("disabled stage timing changed counters or prevented reset");
		const auto cleared = counters.Read();
		if (cleared.samples || cleared.totalNanoseconds || cleared.maximumNanoseconds)
			throw std::runtime_error("stage timing reset left aggregate samples");
		for (const auto count : cleared.durationHistogram)
			if (count)
				throw std::runtime_error("stage timing reset left histogram samples");
	}

	void CheckSnapshotPublicationCoherence()
	{
		struct Snapshot
		{
			std::uint64_t sequence = 0;
			std::array<std::uint64_t, 128> values{};
		};
		VRDepthCullingTelemetry::SnapshotSlot<Snapshot> slot;
		Snapshot observed{};
		bool busy = false;
		if (slot.Read(observed, &busy) || busy)
			throw std::runtime_error("unpublished snapshot was available");
		Snapshot sample{ 7 };
		sample.values.fill(sample.sequence);
		if (!slot.Publish(sample) || !slot.Read(observed) || observed.sequence != sample.sequence)
			throw std::runtime_error("snapshot publication was lost without contention");
		slot.Invalidate();
		if (slot.Read(observed))
			throw std::runtime_error("invalidated snapshot remained readable");
		std::barrier start{ 2 };
		std::atomic_bool done{ false }, torn{ false };
		std::thread writer([&] {
			start.arrive_and_wait();
			for (std::uint64_t sequence = 1; sequence <= 10'000; ++sequence) {
				Snapshot next{ sequence };
				next.values.fill(sequence);
				(void)slot.Publish(next);
				if ((sequence & 7) == 0)
					slot.Invalidate();
			}
			done.store(true, std::memory_order_release);
		});
		start.arrive_and_wait();
		do {
			if (slot.Read(observed))
				for (const auto value : observed.values)
					if (value != observed.sequence)
						torn.store(true, std::memory_order_relaxed);
		} while (!done.load(std::memory_order_acquire));
		writer.join();
		if (torn.load(std::memory_order_relaxed))
			throw std::runtime_error("snapshot reader observed a mixed publication");
		if (slot.Read(observed))
			throw std::runtime_error("final concurrent invalidation left a snapshot readable");
		if (!slot.Publish(sample) || !slot.Read(observed) || observed.sequence != sample.sequence)
			throw std::runtime_error("snapshot slot did not recover after contention");
		slot.Invalidate();
		if (slot.Read(observed))
			throw std::runtime_error("snapshot reset left stale metadata available");
	}

	void CheckCombinedReset()
	{
		using namespace VRDepthCullingTelemetryPolicy;
		WriterGate gate;
		std::atomic_uint64_t advanced{ 7 }, hybrid{ 11 };
		bool resetCalled = false;
		bool resetAdmittedWriter = false;
		const auto resetBoth = [&]() noexcept {
			resetCalled = true;
			WriterScope writer(gate);
			resetAdmittedWriter = static_cast<bool>(writer);
			advanced.store(0);
			hybrid.store(0);
		};
		{
			WriterScope hybridSample(gate);
			if (!hybridSample)
				throw std::runtime_error("Hybrid sample was not admitted");
			if (TryReset(gate, resetBoth) || resetCalled || advanced.load() != 7 || hybrid.load() != 11)
				throw std::runtime_error("busy Hybrid sample allowed a partial combined reset");
			gate.SetEnabled(false);
			if (gate.IsFrozen())
				throw std::runtime_error("an admitted writer was reported as frozen");
			{
				WriterScope blockedAdvanced(gate), blockedHybrid(gate);
				if (blockedAdvanced || blockedHybrid)
					throw std::runtime_error("disabled telemetry admitted a method sample");
			}
			// Disabling new samples cannot discard an already admitted observation.
			hybrid.fetch_add(1);
			if (TryReset(gate, resetBoth) || resetCalled)
				throw std::runtime_error("disable released an admitted Hybrid sample");
		}
		if (advanced.load() != 7 || hybrid.load() != 12)
			throw std::runtime_error("disabled Hybrid sample did not finish its publication");
		if (!gate.IsFrozen())
			throw std::runtime_error("drained disabled telemetry was not frozen");
		if (!TryReset(gate, resetBoth) || !resetCalled || resetAdmittedWriter ||
			advanced.load() != 0 || hybrid.load() != 0 || gate.IsEnabled())
			throw std::runtime_error("combined reset failed to clear both methods while remaining disabled");

		gate.SetEnabled(true);
		if (gate.IsFrozen())
			throw std::runtime_error("enabled telemetry was reported as frozen");
		resetCalled = false;
		{
			WriterScope advancedSample(gate);
			if (!advancedSample)
				throw std::runtime_error("Advanced sample was not admitted after re-enabling telemetry");
			advanced.fetch_add(1);
			if (TryReset(gate, resetBoth) || resetCalled || advanced.load() != 1 || hybrid.load() != 0)
				throw std::runtime_error("busy Advanced sample allowed a partial combined reset");
		}
		if (!TryReset(gate, resetBoth) || !gate.IsEnabled() || advanced.load() != 0 || hybrid.load() != 0)
			throw std::runtime_error("writer scope leaked admission or changed telemetry enablement");
	}
}

int main()
{
	using namespace VRDepthCullingTelemetryPolicy;
	CheckCombinedReset();
	CheckStageTimingAdmissionAndDistribution();
	CheckSnapshotPublicationCoherence();
	CheckNativeVisibilityObservation();
	static_assert(VRDepthCullingTemporal::Status::DurationBinCount == DurationBinCount);
	if (DurationBin(0) != 0 || DurationBin(std::numeric_limits<std::uint64_t>::max()) != DurationBinCount - 1) {
		throw std::runtime_error("duration histogram boundary is incorrect");
	}
	for (std::size_t index = 0; index < DurationUpperBoundsNanoseconds.size(); ++index) {
		const auto upper = DurationUpperBoundsNanoseconds[index];
		if (DurationBin(upper - 1) != index || DurationBin(upper) != index || DurationBin(upper + 1) != index + 1)
			throw std::runtime_error("duration histogram boundary is incorrect");
	}

	WriterGate gate;
	std::latch entered{ 1 };
	std::latch release{ 1 };
	std::thread writer([&] {
		if (!gate.TryEnter())
			throw std::runtime_error("writer was unexpectedly rejected");
		entered.count_down();
		release.wait();
		gate.Leave();
	});
	entered.wait();
	if (gate.TryLockForReset())
		throw std::runtime_error("reset entered while a writer was active");
	release.count_down();
	writer.join();
	if (!gate.TryLockForReset())
		throw std::runtime_error("idle reset was rejected");
	if (gate.TryEnter() || gate.TryLockForReset())
		throw std::runtime_error("reset did not exclude writers and other resets");
	gate.SetEnabled(false);
	if (gate.IsEnabled() || gate.TryEnter())
		throw std::runtime_error("disabling telemetry lost reset ownership");
	gate.UnlockAfterReset();

	if (gate.IsEnabled() || gate.TryEnter())
		throw std::runtime_error("disabled telemetry admitted a writer");
	if (!gate.TryLockForReset())
		throw std::runtime_error("disabled telemetry could not be reset");
	gate.SetEnabled(true);
	if (!gate.IsEnabled() || gate.TryEnter())
		throw std::runtime_error("enabling telemetry lost reset ownership");
	gate.UnlockAfterReset();
	if (!gate.TryEnter() || !gate.TryEnter())
		throw std::runtime_error("enabled telemetry did not admit multiple writers");
	gate.SetEnabled(false);
	if (gate.TryLockForReset() || gate.TryEnter())
		throw std::runtime_error("disable lost active writer ownership");
	gate.Leave();
	if (gate.TryLockForReset())
		throw std::runtime_error("reset ignored a remaining writer");
	gate.Leave();
	if (!gate.TryLockForReset())
		throw std::runtime_error("finished disabled writers kept reset busy");
	gate.UnlockAfterReset();
	gate.SetEnabled(true);

	// Race two writers against reset admission. The separate markers detect
	// overlapping admitted scopes without relying on non-atomic test data.
	std::barrier start{ 3 };
	std::atomic_uint32_t activeWriters{ 0 };
	std::atomic_bool activeReset{ false };
	std::atomic_bool overlap{ false };
	auto raceWriter = [&] {
		for (std::size_t iteration = 0; iteration < 10'000; ++iteration) {
			start.arrive_and_wait();
			if (WriterScope writerScope{ gate }; writerScope) {
				activeWriters.fetch_add(1);
				if (activeReset.load())
					overlap.store(true);
				std::this_thread::yield();
				activeWriters.fetch_sub(1);
			}
		}
	};
	std::thread firstWriter(raceWriter);
	std::thread secondWriter(raceWriter);
	for (std::size_t iteration = 0; iteration < 10'000; ++iteration) {
		start.arrive_and_wait();
		(void)TryReset(gate, [&]() noexcept {
			activeReset.store(true);
			if (activeWriters.load() != 0)
				overlap.store(true);
			std::this_thread::yield();
			activeReset.store(false);
		});
	}
	firstWriter.join();
	secondWriter.join();
	if (overlap.load())
		throw std::runtime_error("reset and recovery writers were admitted together");
	if (!gate.TryLockForReset())
		throw std::runtime_error("admission race leaked a writer or reset");
	gate.UnlockAfterReset();
	return 0;
}
