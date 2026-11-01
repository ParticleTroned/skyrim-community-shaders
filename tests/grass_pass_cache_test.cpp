#include "Features/GrassOptimizations/GrassFadeHistory.h"
#include "Features/GrassOptimizations/GrassPassCache.h"

#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <optional>
#include <stdexcept>

namespace
{
	std::atomic_size_t allocations{};
	std::atomic_bool failAllocation{};
}

void* operator new(std::size_t size)
{
	if (failAllocation.load())
		throw std::bad_alloc{};
	++allocations;
	if (auto* memory = std::malloc(size ? size : 1))
		return memory;
	throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

namespace
{
	using GrassPassCache::Cache;
	using GrassPassCache::Key;
	using GrassPassCache::Outcome;

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void CheckPassDecisions()
	{
		const std::array<Key, 5> keys{ { { 1, 2, 3, 4 }, { 2, 2, 3, 4 }, { 1, 3, 3, 4 }, { 1, 2, 4, 4 }, { 1, 2, 3, 5 } } };
		Cache cache;
		for (size_t i = 0; i < keys.size(); ++i) {
			auto [outcome, first] = cache.Acquire(keys[i]);
			Require(first && *outcome == Outcome::Native, "Every descriptor field must identify a distinct native-first pass");
			if (i % 2 == 0)
				*outcome = Outcome::Batched;
		}
		for (size_t i = keys.size(); i-- > 0;) {
			auto [outcome, first] = cache.Acquire(keys[i]);
			Require(!first, "An existing pass must never attempt batching again in the same frame");
			Require(*outcome == (i % 2 == 0 ? Outcome::Batched : Outcome::Native), "Successful and failed batch decisions must survive interleaved passes");
		}
		Cache otherBucket;
		auto [other, first] = otherBucket.Acquire(keys.front());
		Require(first && *other == Outcome::Native, "A batch decision must not leak between buckets");
		cache.Clear();
		for (const auto& key : keys) {
			auto [outcome, fresh] = cache.Acquire(key);
			Require(fresh && *outcome == Outcome::Native, "Every new frame must reconsider both successful and failed passes");
		}
	}

	void CheckFadeHistoryAllocations()
	{
		GrassPolicy::FadeHistory<int> history;
		for (int group = 0; group < 32; ++group)
			history.Generated(group, 10.f);
		const auto before = allocations.load();
		for (int repeat = 0; repeat < 1000; ++repeat) {
			for (int group = 0; group < 32; ++group) {
				history.Generated(group, 20.f, true);
				history.Generated(group, 30.f);
				Require(history.Capture(group) == 20.f && history.Start(group) == 20.f,
					"Repeated capture must preserve the current group's spawn age");
			}
		}
		Require(allocations.load() == before, "Warm fade reads, recaptures and replacements must not allocate");
		bool rejected = false;
		failAllocation = true;
		try {
			history.Capture(32);
		} catch (const std::bad_alloc&) {
			rejected = true;
		}
		failAllocation = false;
		Require(rejected, "New history allocation failures must reach the renderer's fallback handler");
		Require(history.Start(0) == 20.f && history.Start(32) < 0.f,
			"Failed history growth must preserve existing ages and keep unknown grass visible");
		Require(history.Capture(32) < 0.f, "Retrying delayed capture must not start a new fade");
	}

	void CheckCapacityAndReuse()
	{
		Cache cache;
		constexpr uint32_t passCount = 257;
		for (uint32_t i = 0; i < passCount; ++i) {
			auto [outcome, first] = cache.Acquire({ i, i * 7, i * 11, i * 13 });
			Require(first, "Growing the cache must retain every distinct pass");
			*outcome = Outcome::Batched;
		}
		for (uint32_t i = 0; i < passCount; ++i) {
			auto [outcome, first] = cache.Acquire({ i, i * 7, i * 11, i * 13 });
			Require(!first && *outcome == Outcome::Batched, "Storage growth must preserve previous decisions");
		}
		const auto before = allocations.load();
		for (uint32_t frame = 0; frame < 100; ++frame) {
			cache.Clear();
			for (uint32_t i = 0; i < passCount; ++i) {
				const auto ordinal = frame % 2 ? passCount - 1 - i : i;
				const Key key{ ordinal + frame, ordinal * 7, ordinal * 11, ordinal * 13 };
				auto [outcome, first] = cache.Acquire(key);
				Require(first && *outcome == Outcome::Native, "Cleared storage must not retain stale keys or outcomes");
				*outcome = Outcome::Batched;
				auto [repeated, fresh] = cache.Acquire(key);
				Require(!fresh && *repeated == Outcome::Batched, "Repeated native groups must observe the completed batch");
			}
		}
		Require(allocations.load() == before, "A warmed pass cache must allocate zero times across frames");
	}

	void CheckAllocationFailure()
	{
		Cache cache;
		bool rejected = false;
		failAllocation = true;
		try {
			cache.Acquire({ 1, 2, 3, 4 });
		} catch (const std::bad_alloc&) {
			rejected = true;
		}
		failAllocation = false;
		Require(rejected, "Storage failure must reach the renderer's native fallback handler");
		auto [outcome, first] = cache.Acquire({ 1, 2, 3, 4 });
		Require(first && *outcome == Outcome::Native, "Failed admission must never report a completed batch");
	}
	void CheckPopulatedAllocationFailure()
	{
		Cache cache;
		const Key initial{ 1000, 2, 3, 4 };
		auto [initialOutcome, initialFirst] = cache.Acquire(initial);
		Require(initialFirst, "Initial pass must be admitted");
		*initialOutcome = Outcome::Batched;
		std::array<Key, 256> inserted{};
		size_t insertedCount = 0;
		std::optional<Key> rejected;
		for (uint32_t i = 0; i < inserted.size(); ++i) {
			const Key key{ 999 - i, 2, 3, 4 };
			failAllocation = true;
			try {
				auto [outcome, first] = cache.Acquire(key);
				failAllocation = false;
				Require(first, "New pass must not alias existing decisions");
				*outcome = Outcome::Batched;
				inserted[insertedCount++] = key;
			} catch (const std::bad_alloc&) {
				failAllocation = false;
				rejected = key;
				break;
			}
		}
		Require(rejected.has_value(), "Populated growth must exercise allocation failure");
		auto [preserved, fresh] = cache.Acquire(initial);
		Require(!fresh && *preserved == Outcome::Batched, "Failed growth must preserve the earlier batch decision");
		for (size_t i = 0; i < insertedCount; ++i) {
			auto [outcome, first] = cache.Acquire(inserted[i]);
			Require(!first && *outcome == Outcome::Batched, "Failed insertion must preserve all sorted entries");
		}
		auto [retried, first] = cache.Acquire(*rejected);
		Require(first && *retried == Outcome::Native, "Failed admission must be native-first when retried");
	}
	void CheckMainOwnership()
	{
		Cache cache;
		for (const auto first : { Outcome::Native, Outcome::Batched }) {
			cache.Clear();
			Require(!cache.MainOutcome(), "New frames must reconsider coverage ownership");
			cache.CommitMain(first);
			cache.Acquire({ 8, 8, 8, 0 });
			cache.Acquire({ 0, 0, 0, 0 });
			cache.CommitMain(first == Outcome::Native ? Outcome::Batched : Outcome::Native);
			Require(cache.MainOutcome() == first, "Depth and colour must not change the first main-view route");
		}
	}

}

int main()
{
	try {
		CheckPassDecisions();
		CheckCapacityAndReuse();
		CheckFadeHistoryAllocations();
		CheckAllocationFailure();
		CheckPopulatedAllocationFailure();
		CheckMainOwnership();
		std::cout << "Grass pass isolation, fade history, fallback, growth, frame reset and zero warm allocations passed\n";
		return 0;
	} catch (const std::exception& error) {
		failAllocation = false;
		std::cerr << error.what() << '\n';
		return 1;
	}
}
