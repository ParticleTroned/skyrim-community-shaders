#pragma once

#include <ankerl/unordered_dense.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <new>
#include <unordered_map>
#include <utility>

namespace ShadowBatch
{
	/** Own stable submission records until their native bucket is empty. Caller serializes access. */
	template <class Key, class Payload, class Hash = ankerl::unordered_dense::hash<Key>, bool AutoRelease = true>
	class Submissions
	{
	public:
		struct Record
		{
			Payload payload{};
			std::uintptr_t caller{};
			Record* next{};
		};

		struct Admission
		{
			Record* record;
			bool inserted;
		};

		/** Native traversal may retain a local node even after a reentrant bucket reset. */
		class ReadScope
		{
		public:
			explicit ReadScope(Submissions& a_store) noexcept : store(a_store) { ++store.readers; }
			ReadScope(const ReadScope&) = delete;
			ReadScope& operator=(const ReadScope&) = delete;
			~ReadScope()
			{
				if (--store.readers == 0 && AutoRelease)
					store.ReleaseRetired();
			}

		private:
			Submissions& store;
		};

		/** Prepare ownership before publication; preparation must not reenter this store. */
		template <class Prepare>
		Admission Admit(std::uint64_t a_bucket, const Key& a_key, std::uintptr_t a_caller, Prepare&& a_prepare)
		{
			auto& bucket = GetBucket(a_bucket);
			if (bucket.entries && bucket.entries->size() >= bucket.capacity) {
				if (const auto found = bucket.entries->find(a_key); found != bucket.entries->end())
					return { found->second, false };
			}
			auto& entries = bucket.ReserveForInsert();
			const auto [entry, inserted] = entries.try_emplace(a_key, nullptr);
			if (!inserted)
				return { entry->second, false };
			Record* record = nullptr;
			try {
				if (!free) {
					records.emplace_back();
					free = &records.back();
				}
				record = free;
				free = record->next;
				record->next = nullptr;
				a_prepare(record->payload);
				entry->second = record;
			} catch (...) {
				entries.erase(entry);
				if (!record)
					throw;
				if constexpr (AutoRelease) {
					record->payload = {};
					record->next = free;
					free = record;
				} else {
					record->next = retired;
					retired = record;
				}
				throw;
			}
			record->caller = a_caller;
			if (!bucket.head) {
				bucket.activeNext = activeBuckets;
				if (activeBuckets)
					activeBuckets->activePrevious = &bucket;
				activeBuckets = &bucket;
			}
			record->next = bucket.head;
			bucket.head = record;
			++active;
			return { record, true };
		}

		/** Only a bucket with retained native links needs a native emptiness probe. */
		template <class IsEmpty>
		void RefreshBucket(std::uint64_t a_bucket, IsEmpty&& a_isEmpty)
		{
			auto& bucket = GetBucket(a_bucket);
			if (!bucket.head)
				return;
			if (a_isEmpty())
				ClearBucket(bucket);
		}

		/** Release only after native code has removed all links into this bucket. */
		void Clear(std::uint64_t a_bucket)
		{
			if (const auto found = buckets.find(a_bucket); found != buckets.end())
				ClearBucket(*found->second);
		}

		/** Native reset/abort discards every bucket, independently of frame count. */
		void Clear()
		{
			const ReadScope deferReleases{ *this };
			while (activeBuckets)
				ClearBucket(*activeBuckets);
		}

		/** Retire only buckets proven empty, including skipped ranges and partial drains. */
		template <class IsEmpty>
		void Prune(IsEmpty&& a_isEmpty)
		{
			const ReadScope deferReleases{ *this };
			for (auto* bucket = activeBuckets; bucket;) {
				auto* next = bucket->activeNext;
				if (a_isEmpty(bucket->key))
					ClearBucket(*bucket);
				bucket = next;
			}
		}

		[[nodiscard]] std::size_t Active() const noexcept { return active; }
		[[nodiscard]] std::size_t Capacity() const noexcept { return records.size(); }
		[[nodiscard]] bool HasRetired() const noexcept { return retired != nullptr; }

		/** Detach retired storage under the caller lock, then release its payloads outside that lock. */
		Record* TakeRetired() noexcept
		{
			return readers == 0 ? std::exchange(retired, nullptr) : nullptr;
		}

		/** Return an exclusively detached, released chain under the caller lock. */
		void Recycle(Record* a_head, Record* a_tail) noexcept
		{
			a_tail->next = free;
			free = a_head;
		}

	private:
		struct Bucket
		{
			using Entries = ankerl::unordered_dense::map<Key, Record*, Hash>;
			std::unique_ptr<Entries> entries;
			Record* head{};
			std::size_t capacity{};
			std::uint64_t key{};
			Bucket* activePrevious{};
			Bucket* activeNext{};

			Entries& ReserveForInsert()
			{
				if (entries && entries->size() < capacity)
					return *entries;
				const auto limit = Entries::max_size();
				if (capacity == limit)
					throw std::bad_alloc{};
				const auto next = capacity == 0 ? std::size_t{ 8 } : capacity > limit / 2 ? limit :
				                                                                            capacity * 2;
				// Grow off to the side: a failed dense-map rehash must not damage live membership.
				auto replacement = std::make_unique<Entries>(next);
				if (entries) {
					for (const auto& entry : *entries)
						replacement->emplace(entry);
				}
				entries.swap(replacement);
				capacity = next;
				return *entries;
			}
		};

		Bucket& GetBucket(std::uint64_t a_key)
		{
			if (lastBucket && lastBucketKey == a_key)
				return *lastBucket;
			if (const auto found = buckets.find(a_key); found != buckets.end()) {
				lastBucketKey = a_key;
				lastBucket = found->second.get();
				return *lastBucket;
			}
			auto bucket = std::make_unique<Bucket>();
			bucket->key = a_key;
			auto* result = bucket.get();
			buckets.emplace(a_key, std::move(bucket));
			lastBucketKey = a_key;
			lastBucket = result;
			return *result;
		}

		void ClearBucket(Bucket& a_bucket)
		{
			if (!a_bucket.head)
				return;
			if (a_bucket.activePrevious)
				a_bucket.activePrevious->activeNext = a_bucket.activeNext;
			else
				activeBuckets = a_bucket.activeNext;
			if (a_bucket.activeNext)
				a_bucket.activeNext->activePrevious = a_bucket.activePrevious;
			a_bucket.activePrevious = a_bucket.activeNext = nullptr;
			auto* record = std::exchange(a_bucket.head, nullptr);
			if (a_bucket.entries)
				a_bucket.entries->clear();
			while (record) {
				auto* next = record->next;
				--active;
				record->next = retired;
				retired = record;
				record = next;
			}
			if (readers == 0 && AutoRelease)
				ReleaseRetired();
		}

		void ReleaseRetired()
		{
			while (retired) {
				auto* record = retired;
				retired = record->next;
				record->payload = {};
				record->next = free;
				free = record;
			}
		}

		std::unordered_map<std::uint64_t, std::unique_ptr<Bucket>> buckets;
		Bucket* activeBuckets{};
		Bucket* lastBucket{};
		std::uint64_t lastBucketKey{};
		std::deque<Record> records;
		Record* free{};
		Record* retired{};
		std::size_t active{};
		std::size_t readers{};
	};
}
