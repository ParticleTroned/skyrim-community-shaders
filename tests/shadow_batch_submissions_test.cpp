#include "EngineFixes/ShadowBatchSubmissions.h"
#include "EngineFixes/VRShadowBatchPolicy.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
	std::atomic_size_t allocations{};
	void Check(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}
	struct Pass
	{
		Pass* next{};
		int draw{};
	};
	struct Payload
	{
		Pass pass;
		std::shared_ptr<int> owner;
	};
	using Store = ShadowBatch::Submissions<std::uint64_t, Payload>;
	Store::Admission Add(Store& store, std::uint64_t bucket, std::uint64_t key, int draw = 1, std::shared_ptr<int> owner = {})
	{
		return store.Admit(bucket, key, 123, [&](Payload& payload) { payload = { { nullptr, draw }, owner }; });
	}
	void Link(Pass*& head, Store::Admission admission)
	{
		if (admission.inserted) {
			admission.record->payload.pass.next = head;
			head = &admission.record->payload.pass;
		}
	}
	std::size_t Length(Pass* head)
	{
		std::size_t count = 0;
		for (; head; head = head->next) {
			Check(++count < 100000, "cycle in protected list");
		}
		return count;
	}

	void DuplicateAndOverlap()
	{
		Store store;
		Pass* head{};
		auto a = Add(store, 1, 10);
		Link(head, a);
		Link(head, Add(store, 1, 10));
		Check(Length(head) == 1, "A-A must be idempotent");
		Link(head, Add(store, 1, 20));
		Link(head, Add(store, 1, 10));
		Check(Length(head) == 2 && store.Active() == 2, "A-B-A must be acyclic");
		Pass* other{};
		auto otherA = Add(store, 2, 10);
		Link(other, otherA);
		Check(otherA.record != a.record && Length(head) == 2, "overlapping buckets need independent nodes");
		Store otherBatch;
		Check(Add(otherBatch, 1, 10).record != a.record, "overlapping renderers need independent nodes");
		Link(head, Add(store, 1, 11, 2));
		Check(Length(head) == 3 && head->draw == 2, "changed draw state must remain a distinct submission");
		store.Prune([](std::uint64_t key) { return key == 2; });
		Check(store.Active() == 3 && Length(head) == 3, "partial drain must preserve live buckets");
		store.Prune([](std::uint64_t) { return false; });
		Check(!Add(store, 1, 10).inserted, "autoClear=false must retain membership");
		head = nullptr;
		store.Clear(1);
		Check(Add(store, 1, 10).inserted, "same-frame next cascade must admit the same source");
		store.Clear();
		Check(store.Active() == 0, "reset must retire all memberships");
	}

	void SparseBucketsAndDuplicateGrowth()
	{
		Store store;
		std::size_t probes{};
		for (std::uint64_t bucket = 0; bucket < 1024; ++bucket) {
			store.RefreshBucket(bucket, [&] { ++probes; return true; });
			Add(store, bucket, 1);
			store.Clear(bucket);
		}
		Check(probes == 0, "empty retained buckets must skip native probes");
		for (auto bucket : { 2ULL, 511ULL, 1000ULL })
			Add(store, bucket, 1);
		store.Prune([&](std::uint64_t bucket) { ++probes; return bucket == 511; });
		Check(probes == 3 && store.Active() == 2, "sparse pruning must preserve head and tail after removing the middle");
		store.Clear(1000);
		Add(store, 511, 1);
		store.RefreshBucket(2, [&] { ++probes; return true; });
		Check(store.Active() == 1 && Add(store, 2, 1).inserted, "native drain must allow a fresh generation");
		store.RefreshBucket(511, [] { return false; });
		Check(!Add(store, 511, 1).inserted, "nonempty native bucket must retain duplicate membership");
		store.Clear();
		Check(store.Active() == 0, "reset must drain the active list after sparse reuse");
		for (std::uint64_t key = 0; key < 8; ++key)
			Add(store, 2048, key);
		const auto before = allocations.load();
		Check(!Add(store, 2048, 0).inserted, "full bucket must reject duplicates");
		Check(allocations.load() == before, "duplicate at capacity must not grow the membership table");
		store.Clear();
	}

	void LifetimeAndReentrancy()
	{
		Store store;
		auto owner = std::make_shared<int>(42);
		std::weak_ptr<int> weak = owner;
		auto first = Add(store, 1, 10, 7, owner);
		owner.reset();
		Check(!weak.expired(), "submission must retain its owner");
		{
			Store::ReadScope reading{ store };
			{
				Store::ReadScope nested{ store };
				store.Clear(1);
				Check(!weak.expired(), "reentrant reset must not free a traversed node");
				auto replacement = Add(store, 1, 10, 8);
				Check(replacement.inserted && replacement.record != first.record, "new generation cannot reuse an active reader's storage");
			}
			Check(first.record->payload.pass.draw == 7 && !weak.expired(), "outer reader must retain the old generation");
		}
		Check(weak.expired(), "retired owner must release after the final reader");
		Check(store.Active() == 1, "retired generation must not clear its replacement");
		store.Clear();
	}

	void MixedBucketLifetimes()
	{
		Store store;
		std::array<std::array<Store::Record*, 8>, 64> expected{};
		std::uint32_t seed = 0x6131c953;
		auto random = [&] {
			seed ^= seed << 13;
			seed ^= seed >> 17;
			seed ^= seed << 5;
			return seed;
		};
		for (unsigned step = 0; step < 25000; ++step) {
			const auto bucket = random() % expected.size();
			const auto key = random() % expected[bucket].size();
			switch (random() % 8) {
			case 0:
				store.Clear(bucket);
				expected[bucket].fill(nullptr);
				break;
			case 1:
				{
					std::size_t occupied{}, probes{};
					for (const auto& entries : expected) {
						for (const auto* entry : entries) {
							if (entry) {
								++occupied;
								break;
							}
						}
					}
					store.Prune([&](std::uint64_t current) {
						++probes;
						if ((current & 3) != (bucket & 3))
							return false;
						expected[current].fill(nullptr);
						return true;
					});
					Check(probes == occupied, "mixed pruning must visit every occupied bucket exactly once");
					break;
				}
			case 2:
				store.Clear();
				for (auto& entries : expected) entries.fill(nullptr);
				break;
			default:
				{
					const auto admission = Add(store, bucket, key);
					Check(admission.inserted == !expected[bucket][key], "mixed generations changed duplicate admission");
					if (expected[bucket][key])
						Check(admission.record == expected[bucket][key], "live record identity changed");
					expected[bucket][key] = admission.record;
					break;
				}
			}
			std::size_t active{};
			for (const auto& entries : expected)
				for (const auto* entry : entries) active += entry != nullptr;
			Check(store.Active() == active, "mixed pruning lost or retained a submission");
		}
		store.Clear();
	}

	void FailureAndReuse()
	{
		Store store;
		try {
			store.Admit(1, 10, 1, [](Payload&) { throw std::bad_alloc{}; });
			Check(false, "preparation failure must propagate");
		} catch (const std::bad_alloc&) {
			Check(store.Active() == 0, "failed preparation cannot publish membership");
		}
		Check(Add(store, 1, 10).inserted, "failed record must be reusable");
		store.Clear();
		constexpr std::uint64_t count = 4096;
		for (std::uint64_t i = 0; i < count; ++i)
			Add(store, 1, i);
		store.Clear();
		const auto capacity = store.Capacity();
		const auto before = allocations.load();
		const auto start = std::chrono::steady_clock::now();
		for (int repeat = 0; repeat < 100; ++repeat) {
			for (std::uint64_t i = 0; i < count; ++i)
				Add(store, 1, i);
			store.Clear();
		}
		const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		Check(allocations.load() == before, "warmed admission/reset must not allocate per draw");
		Check(store.Capacity() == capacity, "warmed records must reuse stable capacity");
		std::cout << "controller-only: " << elapsed / 100 << " ms per 4096 admissions + reset; not in-game performance\n";
	}

	void ReentrantOwnerRelease()
	{
		for (const bool prune : { false, true }) {
			Store store;
			auto owner = std::shared_ptr<int>(new int(42), [&](int* value) {
				delete value;
				for (std::uint64_t bucket = 100; bucket < 200; ++bucket)
					Add(store, bucket, 1);
			});
			Add(store, 1, 1, 1, owner);
			Add(store, 2, 1);
			owner.reset();
			if (prune)
				store.Prune([](std::uint64_t) { return true; });
			else
				store.Clear();
			Check(store.Active() == 100, "owner release must run after bucket iteration and preserve new submissions");
			store.Clear();
		}
	}

	void SerializedProducers()
	{
		Store store;
		std::recursive_mutex mutex;
		Pass* head{};
		std::vector<std::thread> threads;
		for (int t = 0; t < 4; ++t) {
			threads.emplace_back([&] {
				for (std::uint64_t i = 0; i < 512; ++i) {
					const std::lock_guard lock{ mutex };
					Link(head, Add(store, 1, i));
				}
			});
		}
		for (auto& thread : threads)
			thread.join();
		Check(Length(head) == 512, "serialized duplicate producers must not share live links");
	}

	void PolicyAndLiveBytes(const char* snapshot)
	{
		using namespace VRShadowBatch;
		Check(IsIsolatedShadowPass(true, 0xC0C6, 0, 0), "captured sun pass must be protected");
		Check(!IsIsolatedShadowPass(false, 0xC0C6, 0, 0), "non-utility shaders must remain native");
		Check(!IsIsolatedShadowPass(true, 0xC0C6, 1, 0), "borrowed light arrays must not be copied shallowly");
		Check(!IsIsolatedShadowPass(true, 0xC0C6, 0, 1), "shadow light ownership remains outside this path");
		Check(!IsIsolatedShadowPass(true, 0x202B, 0, 0), "depth replay must remain outside this path");
		Check(!IsIsolatedShadowPass(true, 0, 0, 0), "technique subtraction must not underflow");
		Check(BucketIndex(0, 0) == 0 && BucketIndex(0, 0x200) == 1 &&
				  BucketIndex(1ULL << 36, 0) == 2 && BucketIndex(1ULL << 36, 0x200) == 3 &&
				  BucketIndex(1ULL << 54, 0x200) == 4,
			"native bucket selection mismatch");
		for (const auto& site : kHookSites) {
			Check(Matches(site.prefix, site), "known hook prefix must match");
			auto changed = site.prefix;
			changed[0] ^= 1;
			Check(!Matches(changed, site), "foreign hook must be rejected before publication");
			Check(!Matches(std::span(site.prefix).first(8), site), "truncated code must fail closed");
		}
		if (snapshot) {
			std::ifstream input(snapshot, std::ios::binary);
			std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
			Check(bytes.size() == 0x7000, "live lifecycle snapshot must be complete");
			for (const auto& site : kHookSites)
				Check(Matches(std::span(bytes).subspan(site.rva - 0x1347000), site), "hook prefix differs from current live snapshot");
			Check(Matches(std::span(bytes).subspan(kContiguousGroups.rva - 0x1347000), kContiguousGroups), "live native group stride differs");
			std::cout << "all seven hook prefixes and group layout match PID 2912 live bytes\n";
		}
	}
}

void* operator new(std::size_t size)
{
	++allocations;
	if (auto* memory = std::malloc(size ? size : 1))
		return memory;
	throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

int main(int argc, char** argv)
{
	try {
		DuplicateAndOverlap();
		SparseBucketsAndDuplicateGrowth();
		LifetimeAndReentrancy();
		MixedBucketLifetimes();
		FailureAndReuse();
		ReentrantOwnerRelease();
		SerializedProducers();
		PolicyAndLiveBytes(argc > 1 ? argv[1] : nullptr);
		std::cout << "shadow batch submission tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
