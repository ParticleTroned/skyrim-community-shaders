#include "EngineFixes/AlphaPassListPolicy.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace
{
	std::atomic_size_t allocationCount{};
	std::atomic_ptrdiff_t failAfter{ -1 };
}
void* operator new(std::size_t size)
{
	if (failAfter.load() >= 0 && failAfter.fetch_sub(1) == 0)
		throw std::bad_alloc();
	++allocationCount;
	if (auto* memory = std::malloc(size ? size : 1))
		return memory;
	throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

namespace
{
	using namespace AlphaPassList;
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}
	struct NativeQueue
	{
		Queue state{ .group = 1, .flags = 1 };
		std::unordered_map<std::uintptr_t, std::uintptr_t> links;
		std::size_t reads{}, forwards{};
		bool Read(std::uintptr_t node, std::uintptr_t& next)
		{
			++reads;
			auto found = links.find(node);
			if (found == links.end())
				return false;
			next = found->second;
			return true;
		}
		void Append(std::uintptr_t pass, bool front = false)
		{
			++forwards;
			const auto previousHead = state.head;
			if (!state.head)
				state.head = state.tail = pass;
			else if (front)
				state.head = pass;
			else {
				links[state.tail] = pass;
				state.tail = pass;
			}
			links[pass] = front ? previousHead : 0;
			++state.count;
		}
		template <class MembershipCache>
		AppendResult Guard(MembershipCache* cache, std::uintptr_t pass, bool front = false, std::size_t* failures = nullptr)
		{
			return AppendUnique(cache, state, pass, front, [&](auto node, auto& next) { return Read(node, next); }, [&] { Append(pass, front); }, [&] { return state; }, [&] { if (failures) ++*failures; });
		}
		std::vector<std::uintptr_t> Walk()
		{
			std::vector<std::uintptr_t> result;
			auto node = state.head;
			while (node && result.size() < MaximumWalkNodes) {
				result.push_back(node);
				node = links.at(node);
			}
			return result;
		}
	};
	void RecordedCorruption()
	{
		struct Capture
		{
			std::uintptr_t calls, duplicateIndex;
			std::size_t reachable;
		};
		for (const auto& capture : { Capture{ 226, 143, 102 }, Capture{ 207, 122, 104 }, Capture{ 210, 163, 66 }, Capture{ 203, 158, 64 } }) {
			NativeQueue native, fixed;
			Cache cache;
			for (std::uintptr_t i = 0; i < capture.calls; ++i) {
				const auto pass = i == capture.duplicateIndex ? 20 : i + 1;
				native.Append(pass);
				fixed.Guard(&cache, pass);
			}
			Require(native.state.count == capture.calls && native.Walk().size() == capture.reachable, "Captured duplicate did not reproduce native truncation");
			Require(Contains(native.state.head, 61, [&](auto n, auto& next) { return native.Read(n, next); }) == Membership::absent, "Native cloud unexpectedly reachable");
			Require(fixed.state.count == capture.calls - 1 && fixed.Walk().size() == capture.calls - 1 && fixed.forwards == capture.calls - 1, "Duplicate guard lost a unique queued pass");
			Require(Contains(fixed.state.head, 61, [&](auto n, auto& next) { return fixed.Read(n, next); }) == Membership::present, "Guard failed to preserve cloud membership");
			Require(fixed.reads <= 61, "Normal guarded appends repeatedly walked native links");
		}
	}
	void Lifetimes()
	{
		NativeQueue queue;
		Cache a, b;
		queue.Guard(&a, 1);
		queue.Guard(&a, 2);
		Require(!queue.Guard(&a, 1, true).forwarded && !queue.Guard(&a, 2).forwarded, "Head/tail duplicates were admitted");
		Require(queue.Guard(&a, 3, true).forwarded && queue.state.head == 3 && queue.Walk().size() == 3, "Unique front insertion changed");
		Require(!queue.Guard(&b, 2).forwarded, "Thread handoff missed queued membership");
		queue.Guard(&b, 4);
		Require(!queue.Guard(&a, 4).forwarded, "Ownership return reused stale membership");
		++queue.state.frame;
		++queue.state.generation;
		Require(!queue.Guard(&a, 1).forwarded, "Frame/configuration change discarded a still-pending queue");
		queue.state.head = queue.state.tail = queue.state.count = 0;
		Require(queue.Guard(&a, 1).forwarded && queue.state.count == 1, "Consumed queue retained stale membership");
		for (std::uintptr_t group = 2; group < MaximumCachedGroups + 3; ++group) {
			NativeQueue other;
			other.state.group = group;
			Require(other.Guard(&a, group * 10).forwarded, "Different queue identity was suppressed");
		}
		Require(!queue.Guard(&a, 1).forwarded, "Cache eviction lost pending membership");
	}
	struct FailingCache
	{
		bool check{}, commit{};
		std::size_t invalidations{};
		Membership Check(const Queue&, std::uintptr_t, auto&)
		{
			if (check)
				throw std::bad_alloc();
			return Membership::absent;
		}
		void Commit(const Queue&, const Queue&, std::uintptr_t, bool)
		{
			if (commit)
				throw std::bad_alloc();
		}
		void Invalidate() noexcept { ++invalidations; }
	};
	void SameFrameRetirement()
	{
		NativeQueue queue;
		Cache cache;
		queue.Guard(&cache, 1);
		queue.Guard(&cache, 2);
		queue.Guard(&cache, 3);
		queue.state.head = queue.state.tail = queue.state.count = 0;
		++queue.state.generation;
		queue.Append(1);
		queue.Append(4);
		queue.Append(3);
		Require(!queue.Guard(&cache, 4).forwarded, "Same-frame ABA missed a newly pending duplicate");
		Require(queue.Guard(&cache, 2).forwarded, "Same-frame ABA retained a retired pass identity");
		Require(queue.Walk().size() == 4, "Same-frame retirement corrupted a rebuilt queue");
	}
	void InterleavedQueues()
	{
		std::array<NativeQueue, 8> queues;
		std::array<std::vector<std::uintptr_t>, 8> expected;
		Cache first, second;
		std::uint32_t random = 0x19A17E;
		for (std::size_t i = 0; i < queues.size(); ++i) queues[i].state.group = i + 1;
		for (std::size_t step = 0; step < 10000; ++step) {
			random = random * 1664525u + 1013904223u;
			const auto index = (random >> 8) % queues.size();
			auto& queue = queues[index];
			auto& model = expected[index];
			const auto operation = (random >> 12) % 16;
			if (operation == 0) {
				model.clear();
				queue.state.head = queue.state.tail = queue.state.count = 0;
				++queue.state.generation;
			} else if (operation == 1 && !model.empty()) {
				queue.state.head = queue.links.at(queue.state.head);
				if (!queue.state.head)
					queue.state.tail = 0;
				--queue.state.count;
				model.erase(model.begin());
				++queue.state.generation;
			} else if (operation == 2) {
				++queue.state.frame;
			} else {
				const auto pass = index * 100 + ((random >> 16) % 64) + 1;
				const bool front = (random & 16) != 0;
				const bool pending = std::find(model.begin(), model.end(), pass) != model.end();
				const auto result = queue.Guard((random & 1) ? &first : &second, pass, front);
				Require(result.forwarded != pending && result.membershipKnown, "Interleaved ownership or lifetime misclassified a pass");
				if (!pending)
					model.insert(front ? model.begin() : model.end(), pass);
			}
			Require(queue.Walk() == model && queue.state.count == model.size(), "Guarded native queue differs from unique FIFO/front-insertion semantics");
		}
	}
	void WarmPath()
	{
		NativeQueue queue;
		Cache cache;
		for (std::uintptr_t i = 1; i <= 512; ++i) queue.Guard(&cache, i);
		queue.state.head = queue.state.tail = queue.state.count = 0;
		++queue.state.frame;
		++queue.state.generation;
		queue.reads = 0;
		const auto before = allocationCount.load();
		for (std::uintptr_t i = 1; i <= 512; ++i) queue.Guard(&cache, i);
		Require(allocationCount == before && queue.reads == 0, "Warm unique appends allocated or walked native links");
	}
	void NativeAllocationFailures()
	{
		for (bool pending : { false, true }) {
			for (std::ptrdiff_t failure = 0; failure < 5; ++failure) {
				NativeQueue queue;
				queue.links.emplace(1, 0);
				queue.links.emplace(2, 0);
				if (pending)
					queue.Append(1);
				Cache cache;
				std::size_t failures{};
				failAfter = failure;
				const auto result = queue.Guard(&cache, 2, false, &failures);
				failAfter = -1;
				Require(result.forwarded && queue.forwards == (pending ? 2u : 1u), "Real cache allocation failure dropped or replayed a native append");
				if (failure == 0)
					Require(failures == 1, "Allocation failure injection did not reach the membership cache");
				Require(!queue.Guard(&cache, 2).forwarded, "Cache recovery lost the pending pass after allocation failure");
				Require(queue.Walk().size() == queue.forwards, "Cache allocation failure damaged native membership");
			}
		}
	}
	void AllocationGrowthRecovery()
	{
		for (const bool reseed : { false, true }) {
			for (std::ptrdiff_t failure = 0; failure < 10; ++failure) {
				NativeQueue queue;
				Cache cache;
				const std::uintptr_t pending = reseed ? 16 : 3;
				for (std::uintptr_t pass = 1; pass <= pending + 2; ++pass)
					queue.links.emplace(pass, 0);
				for (std::uintptr_t pass = 1; pass <= pending; ++pass) {
					if (reseed)
						queue.Append(pass);
					else
						queue.Guard(&cache, pass);
				}
				std::size_t failures{};
				failAfter = failure;
				const auto result = queue.Guard(&cache, pending + 1, false, &failures);
				failAfter = -1;
				Require(result.forwarded && queue.forwards == pending + 1, "Failed growth dropped or replayed the native append");
				if (failure == 0)
					Require(failures == 1, "Growth fault did not reach the membership cache");
				for (std::uintptr_t group = 2; group <= MaximumCachedGroups + 2; ++group) {
					NativeQueue other;
					other.state.group = group;
					Require(other.Guard(&cache, group).forwarded && !other.Guard(&cache, group).forwarded, "Failed bucket storage survived entry reuse");
				}
				Require(!queue.Guard(&cache, pending + 1).forwarded, "Recovery missed a pending duplicate");
				Require(queue.Guard(&cache, pending + 2).forwarded && queue.Walk().size() == pending + 2, "Recovery damaged native membership");
			}
		}
	}
	void BoundsAndFailures()
	{
		NativeQueue queue;
		constexpr std::size_t limit = 8;
		MembershipCache<limit> cache;
		for (std::uintptr_t i = 1; i <= limit + 3; ++i)
			queue.Guard(&cache, i);
		Require(!queue.Guard(&cache, limit + 2).forwarded, "Cache capacity missed an uncached duplicate");
		Require(queue.Walk().size() == limit + 3, "Capacity fallback lost unique passes");
		auto unreadable = [](auto, auto&) { return false; };
		Require(Contains(100, 101, unreadable) == Membership::unknown, "Unreadable links proved absence");
		auto cycle = [](auto, auto& next) { next = 100;return true; };
		Require(Contains(100, 101, cycle) == Membership::unknown, "Cyclic list did not terminate conservatively");

		NativeQueue failed;
		std::size_t failures{};
		FailingCache before{ true, false };
		Require(failed.Guard(&before, 1, false, &failures).forwarded && failed.forwards == 1 && failures == 1, "Cache-check failure replayed or dropped the native call");
		FailingCache after{ false, true };
		Require(failed.Guard(&after, 2, false, &failures).forwarded && failed.forwards == 2 && failures == 2, "Cache-commit failure replayed the native call");
		Require(!failed.Guard(static_cast<Cache*>(nullptr), 1).forwarded, "Allocation-free fallback missed a duplicate");
	}
}
int main()
{
	try {
		RecordedCorruption();
		Lifetimes();
		SameFrameRetirement();
		BoundsAndFailures();
		WarmPath();
		InterleavedQueues();
		NativeAllocationFailures();
		AllocationGrowthRecovery();
		std::cout << "PASS alpha-list replay, lifetime, handoff, bounds and allocation fallbacks\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
