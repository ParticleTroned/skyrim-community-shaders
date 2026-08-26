#include "Utils/GenerationClaim.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <latch>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{
	enum class EntryStatus
	{
		Pending,
		Completed,
		Failed
	};

	struct Entry
	{
		EntryStatus status = EntryStatus::Pending;
		uint64_t generation = 0;
		bool hasPayload = false;
	};

	struct EntryTraits
	{
		static bool IsPending(const Entry& a_entry) { return a_entry.status == EntryStatus::Pending; }
		static bool IsCompleted(const Entry& a_entry) { return a_entry.status == EntryStatus::Completed; }
		static bool HasPayload(const Entry& a_entry) { return a_entry.hasPayload; }
		static uint64_t GetGeneration(const Entry& a_entry) { return a_entry.generation; }
	};

	using Map = std::unordered_map<std::string, Entry>;
	using Util::GenerationClaim::ClaimOutcome;
	using Util::GenerationClaim::PublishOutcome;

	void Require(bool a_condition)
	{
		if (!a_condition) {
			std::abort();
		}
	}

	ClaimOutcome Claim(
		Map& a_map,
		const std::string& a_key,
		std::optional<uint64_t> a_callerGeneration,
		uint64_t a_liveGeneration)
	{
		const auto claim = Util::GenerationClaim::TryClaim<EntryTraits>(
			a_map,
			a_key,
			a_callerGeneration,
			a_liveGeneration,
			[](uint64_t a_generation) {
				return Entry{ EntryStatus::Pending, a_generation, false };
			});
		return claim.first;
	}

	PublishOutcome Publish(
		Map& a_map,
		const std::string& a_key,
		std::optional<uint64_t> a_callerGeneration,
		uint64_t a_liveGeneration,
		bool a_success)
	{
		return Util::GenerationClaim::TryPublish<EntryTraits>(
			a_map,
			a_key,
			a_callerGeneration,
			a_liveGeneration,
			a_success,
			[](uint64_t a_generation, bool a_ok) {
				return Entry{
					a_ok ? EntryStatus::Completed : EntryStatus::Failed,
					a_generation,
					a_ok
				};
			});
	}

	class ClaimTable
	{
	public:
		ClaimOutcome ClaimBlocking(
			const std::string& a_key,
			std::optional<uint64_t> a_callerGeneration,
			uint64_t a_liveGeneration)
		{
			std::unique_lock lock{ mutex };
			for (;;) {
				const auto claim = Util::GenerationClaim::TryClaim<EntryTraits>(
					map,
					a_key,
					a_callerGeneration,
					a_liveGeneration,
					[](uint64_t a_generation) {
						return Entry{ EntryStatus::Pending, a_generation, false };
					});
				const auto outcome = claim.first;
				if (outcome != ClaimOutcome::MustWait) {
					return outcome;
				}
				condition.wait(lock);
			}
		}

		PublishOutcome PublishAndNotify(
			const std::string& a_key,
			std::optional<uint64_t> a_callerGeneration,
			uint64_t a_liveGeneration,
			bool a_success)
		{
			PublishOutcome outcome;
			{
				std::scoped_lock lock{ mutex };
				outcome = Publish(
					map,
					a_key,
					a_callerGeneration,
					a_liveGeneration,
					a_success);
			}
			if (outcome != PublishOutcome::RejectedStale) {
				condition.notify_all();
			}
			return outcome;
		}

	private:
		Map map;
		std::mutex mutex;
		std::condition_variable condition;
	};

	void TestBasicLifecycle()
	{
		Map map;
		Require(Claim(map, "shader", 5, 5) == ClaimOutcome::Claimed);
		Require(Publish(map, "shader", 5, 5, true) == PublishOutcome::Published);
		Require(Claim(map, "shader", 5, 5) == ClaimOutcome::CacheHit);
		Require(map.at("shader").generation == 5);
	}

	void TestStaleOwnership()
	{
		Map map;
		Require(Claim(map, "shader", 5, 5) == ClaimOutcome::Claimed);
		Require(Publish(map, "shader", 5, 6, true) == PublishOutcome::RejectedStaleCleanedPending);
		Require(!map.contains("shader"));

		Require(Claim(map, "shader", 6, 6) == ClaimOutcome::Claimed);
		Require(Publish(map, "shader", 5, 6, true) == PublishOutcome::RejectedStale);
		Require(map.at("shader").status == EntryStatus::Pending);
		Require(map.at("shader").generation == 6);
	}

	void TestStaleClaims()
	{
		Map map;
		Require(Claim(map, "absent", 4, 5) == ClaimOutcome::RejectedStale);
		Require(!map.contains("absent"));

		Require(Claim(map, "pending", 5, 5) == ClaimOutcome::Claimed);
		Require(Claim(map, "pending", 4, 5) == ClaimOutcome::RejectedStale);
		Require(map.at("pending").status == EntryStatus::Pending);
		Require(map.at("pending").generation == 5);

		Require(Publish(map, "completed", 5, 5, true) == PublishOutcome::Published);
		Require(Claim(map, "completed", 4, 5) == ClaimOutcome::RejectedStale);
		Require(map.at("completed").status == EntryStatus::Completed);
		Require(map.at("completed").generation == 5);
	}

	void TestCompletedAndFailedEntries()
	{
		Map map;
		Require(Publish(map, "completed", 5, 5, true) == PublishOutcome::Published);
		Require(Claim(map, "completed", 7, 7) == ClaimOutcome::CacheHit);
		Require(map.at("completed").generation == 5);

		Require(Claim(map, "failed", std::nullopt, 8) == ClaimOutcome::Claimed);
		Require(Publish(map, "failed", std::nullopt, 9, false) == PublishOutcome::Published);
		Require(map.at("failed").generation == 9);
		Require(Claim(map, "failed", 9, 9) == ClaimOutcome::Claimed);
	}

	void TestConcurrentClaimers()
	{
		constexpr int threadCount = 8;
		ClaimTable table;
		std::latch start{ threadCount };
		std::atomic<int> claimedCount = 0;
		std::atomic<int> cacheHitCount = 0;
		std::vector<std::thread> threads;

		for (int i = 0; i < threadCount; ++i) {
			threads.emplace_back([&] {
				start.arrive_and_wait();
				if (table.ClaimBlocking("shader", 1, 1) == ClaimOutcome::Claimed) {
					claimedCount.fetch_add(1, std::memory_order_relaxed);
					std::this_thread::yield();
					table.PublishAndNotify("shader", 1, 1, true);
				} else {
					cacheHitCount.fetch_add(1, std::memory_order_relaxed);
				}
			});
		}
		for (auto& thread : threads) {
			thread.join();
		}

		Require(claimedCount.load(std::memory_order_relaxed) == 1);
		Require(cacheHitCount.load(std::memory_order_relaxed) == threadCount - 1);
	}
}

int main()
{
	TestBasicLifecycle();
	TestStaleOwnership();
	TestStaleClaims();
	TestCompletedAndFailedEntries();
	TestConcurrentClaimers();
	return 0;
}
